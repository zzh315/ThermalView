package dev.thermalview

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.geometry.isSpecified
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.text.TextLayoutResult
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/** One readout as the native side reports it (camera pixels). */
data class Spot(val tempC: Float, val x: Float, val y: Float, val flags: Int) {
    val valid get() = flags and 1 != 0 && !tempC.isNaN()
    val overRange get() = flags and 2 != 0
    val placed get() = !x.isNaN() && !y.isNaN()
}

/** The readouts and the scale bar's endpoints, as [NativeBridge.readouts] reports them. */
data class Readings(
    val high: Spot,
    val low: Spot,
    val center: Spot,
    val highRange: Boolean,
    val scaleLoC: Float,     // the temperatures at the ends of the color mapping (NaN: none yet)
    val scaleHiC: Float,
    val scaleHiOver: Boolean,  // the mapping's top is over range
    val locked: Boolean,       // the range is locked (M6): the ends are the lock's, adjustable
    val marks: FloatArray = floatArrayOf(Float.NaN, Float.NaN, Float.NaN),  // high, low, center on the scale (intensity)
) {
    /** "45.3 °C" ([unit] false: "45.3°"), "> 120 °C" over range, "--" without a reading. */
    fun text(s: Spot, unit: Boolean = true): String = when {
        s.overRange -> if (highRange) "> max" else if (unit) "> 120 °C" else "> 120°"
        s.valid -> "%.1f".format(s.tempC) + if (unit) " °C" else "°"
        else -> "--"
    }

    companion object {
        fun parse(a: FloatArray): Readings? {
            if (a.size < 13) return null
            fun spot(i: Int) = Spot(a[4 * i], a[4 * i + 1], a[4 * i + 2], a[4 * i + 3].toInt())
            return Readings(
                high = spot(0), low = spot(1), center = spot(2), highRange = a[12] > 0.5f,
                scaleLoC = a.getOrElse(13) { Float.NaN }, scaleHiC = a.getOrElse(14) { Float.NaN },
                scaleHiOver = a.getOrElse(15) { 0f } > 0.5f,
                locked = a.getOrElse(16) { 0f } > 0.5f,
                marks = FloatArray(3) { a.getOrElse(17 + it) { Float.NaN } },
            )
        }
    }
}

private val labelStyle = TextStyle(fontSize = 15.sp, fontWeight = FontWeight.SemiBold, fontFeatureSettings = "tnum")
private val bannerStyle = TextStyle(fontSize = 15.sp, color = Color.White, textAlign = TextAlign.Center)

private class Marker(val at: Offset, val arm: Float, val gap: Float, val color: Color, val label: TextLayoutResult)

/**
 * What's drawn over the image (PLAN M5/M6): crosshairs at the hottest (red) and coldest (blue)
 * points and at the center (white), each with its temperature, and the banner. Marker positions
 * follow the renderer's 4:3 view (renderer.cpp draw(); [viewBox]) and the zoom ([rect]: the camera
 * pixels shown). Nothing leaves the view: the crosshairs are clipped to it, and each label takes the
 * first place around its marker that stays inside and clear of the other markers, the labels placed
 * before it and the banner, keeping its last place while that still works so labels don't hop. The
 * banner sits at the top or the bottom of the image, whichever is clear of the markers (the middle
 * when there's no image). [covered]: a part of the image under a panel, which labels keep out of.
 */
@Composable
fun ImageOverlay(
    readings: Readings?,
    box: ViewBox,
    rect: CamRect,
    banner: String,
    live: Boolean,
    covered: Rect? = null,
    camBox: CamBox? = null,
) {
    val measurer = rememberTextMeasurer()
    val last = remember { IntArray(4) { -1 } }  // the place each label (and the banner) took last time
    val lastAt = remember { Array(3) { Offset.Unspecified } }  // where each marker was then
    Canvas(Modifier.fillMaxSize()) {
        val view = Rect(box.x, box.y, box.x + box.w, box.y + box.h)
        val inner = view.deflate(6.dp.toPx())
        val px = box.w / rect.w  // one camera pixel on screen

        val markers = ArrayList<Marker?>(3)
        if (readings != null) {
            fun marker(s: Spot, color: Color, textColor: Color, arm: Float): Marker? {
                if (!s.placed) return null
                // Any camera pixel the view shows, even partly (the readouts measure those too,
                // Session::setViewRect): the marker goes to the middle of its visible part.
                val x0 = max(s.x, rect.x)
                val x1 = min(s.x + 1f, rect.x + rect.w)
                val y0 = max(s.y, rect.y)
                val y1 = min(s.y + 1f, rect.y + rect.h)
                if (x1 <= x0 || y1 <= y0) return null
                val (x, y) = rect.toSurface(box, (x0 + x1) / 2 - 0.5f, (y0 + y1) / 2 - 0.5f)
                // The gap keeps the marked pixel itself visible, however far zoomed in.
                val gap = max(3.dp.toPx(), 0.6f * px)
                val text = measurer.measure(readings.text(s), labelStyle.copy(color = textColor))
                return Marker(Offset(x, y), gap + arm, gap, color, text)
            }
            // The center first: it's always there, and the others work around it.
            markers += marker(readings.center, Ui.Center, Color.White, 13.dp.toPx())
            markers += marker(readings.high, Ui.Hot, Ui.HotText, 10.dp.toPx())
            markers += marker(readings.low, Ui.Cold, Ui.ColdText, 10.dp.toPx())
        }
        val squares = markers.map { m -> m?.let { Rect(it.at, it.arm + 2.dp.toPx()) } }

        // The banner, away from the hot and cold markers (and the room their labels need).
        var bannerRect: Rect? = null
        var bannerText: TextLayoutResult? = null
        if (banner.isNotEmpty()) {
            val maxW = min(box.w - 48.dp.toPx(), 520.dp.toPx()).toInt().coerceAtLeast(1)
            val t = measurer.measure(banner, bannerStyle, constraints = Constraints(maxWidth = maxW))
            val size = Size(t.size.width + 28.dp.toPx(), t.size.height + 18.dp.toPx())
            val left = box.x + (box.w - size.width) / 2
            val choices = if (!live) {
                listOf(Rect(Offset(left, box.y + (box.h - size.height) / 2), size))
            } else {
                listOf(
                    Rect(Offset(left, inner.top + 6.dp.toPx()), size),
                    Rect(Offset(left, inner.bottom - 6.dp.toPx() - size.height), size),
                )
            }
            val room = 70.dp.toPx()
            val keepClear = squares.drop(1).filterNotNull().map { it.inflate(room) }
            val pick = choices.indices.minBy { i ->
                keepClear.sumOf { overlap(choices[i], it).toDouble() } - if (i == last[3]) 1.0 else 0.0
            }
            last[3] = pick
            bannerRect = choices[pick]
            bannerText = t
        }

        // The labels.
        val pad = Size(6.dp.toPx(), 3.dp.toPx())
        val placed = ArrayList<Rect>()
        val labels = markers.mapIndexed { k, m ->
            if (m == null) return@mapIndexed null
            val size = Size(m.label.size.width + 2 * pad.width, m.label.size.height + 2 * pad.height)
            val armW = 3.dp.toPx()
            val ownArms = listOf(
                Rect(m.at.x - m.arm, m.at.y - armW, m.at.x + m.arm, m.at.y + armW),
                Rect(m.at.x - armW, m.at.y - m.arm, m.at.x + armW, m.at.y + m.arm),
            )
            val obstacles = squares.filterIndexed { j, s -> j != k && s != null }.map { it!! } +
                listOfNotNull(bannerRect, covered) + placed + ownArms
            val candidates = labelPlaces(m.at, m.arm, 4.dp.toPx(), size)
            // A marker that jumped (a new extreme elsewhere) starts from the best place again.
            if (!lastAt[k].isSpecified || (lastAt[k] - m.at).getDistance() > 40.dp.toPx()) last[k] = -1
            lastAt[k] = m.at
            // (a few dp of clamping are tolerated before a label hops from where it was)
            val (best, bestRect) = placeLabel(candidates, size, inner, obstacles, last[k], stickiness = 2.dp.toPx())
            last[k] = best
            placed += bestRect
            bestRect
        }

        clipRect(view.left, view.top, view.right, view.bottom) {
            if (camBox != null) measuringBox(camBox, rect, box)
            for (m in markers) if (m != null) crosshair(m.at, m.arm, m.gap, m.color)
        }
        markers.forEachIndexed { k, m ->
            val r = labels[k] ?: return@forEachIndexed
            if (m == null) return@forEachIndexed
            drawRoundRect(Ui.LabelBack, r.topLeft, r.size, CornerRadius(6.dp.toPx()))
            drawText(m.label, topLeft = Offset(r.left + pad.width, r.top + pad.height))
        }
        if (bannerRect != null && bannerText != null) {
            drawRoundRect(Color(0xE0101418), bannerRect.topLeft, bannerRect.size, CornerRadius(10.dp.toPx()))
            drawText(
                bannerText,
                topLeft = Offset(
                    bannerRect.left + (bannerRect.width - bannerText.size.width) / 2,
                    bannerRect.top + (bannerRect.height - bannerText.size.height) / 2,
                ),
            )
        }
    }
}

/**
 * The label's place (its index among [candidates], top-left corners, and its rectangle), kept inside
 * [inner]: the one overlapping [obstacles] least, then moved least by the clamp into [inner], then
 * earliest; [remembered] (the last place) wins ties by up to [stickiness] px of clamping.
 */
internal fun placeLabel(
    candidates: List<Offset>,
    size: Size,
    inner: Rect,
    obstacles: List<Rect>,
    remembered: Int,
    stickiness: Float,
): Pair<Int, Rect> {
    var best = 0
    var bestCost = Float.MAX_VALUE
    var bestRect = Rect.Zero
    candidates.forEachIndexed { i, topLeft ->
        val wanted = Rect(topLeft, size)
        val r = clampInto(wanted, inner)
        val moved = abs(r.left - wanted.left) + abs(r.top - wanted.top)
        var cost = obstacles.sumOf { overlap(r, it).toDouble() }.toFloat() + 0.5f * moved + 0.01f * i
        if (i == remembered) cost -= stickiness
        if (cost < bestCost) {
            bestCost = cost
            best = i
            bestRect = r
        }
    }
    return best to bestRect
}

/** Where a label may go around its marker, best first: beside, then diagonal, then above and below. */
internal fun labelPlaces(at: Offset, arm: Float, d: Float, size: Size): List<Offset> {
    val q = arm * 0.62f
    val (w, h) = size.width to size.height
    return listOf(
        Offset(at.x + arm + d, at.y - h / 2),      // right
        Offset(at.x - arm - d - w, at.y - h / 2),  // left
        Offset(at.x + q + d, at.y - q - d - h),    // up right
        Offset(at.x + q + d, at.y + q + d),        // down right
        Offset(at.x - q - d - w, at.y - q - d - h),  // up left
        Offset(at.x - q - d - w, at.y + q + d),    // down left
        Offset(at.x - w / 2, at.y - arm - d - h),  // above
        Offset(at.x - w / 2, at.y + arm + d),      // below
    )
}

internal fun clampInto(r: Rect, bounds: Rect): Rect {
    val dx = when {
        r.left < bounds.left -> bounds.left - r.left
        r.right > bounds.right -> bounds.right - r.right
        else -> 0f
    }
    val dy = when {
        r.top < bounds.top -> bounds.top - r.top
        r.bottom > bounds.bottom -> bounds.bottom - r.bottom
        else -> 0f
    }
    return r.translate(dx, dy)
}

internal fun overlap(a: Rect, b: Rect): Float {
    val w = min(a.right, b.right) - max(a.left, b.left)
    val h = min(a.bottom, b.bottom) - max(a.top, b.top)
    return if (w > 0f && h > 0f) w * h else 0f
}

/** The box's outline and its eight handles (the image outside is dimmed by the renderer). */
private fun DrawScope.measuringBox(b: CamBox, rect: CamRect, box: ViewBox) {
    val l = box.x + (b.x - rect.x) / rect.w * box.w
    val t = box.y + (b.y - rect.y) / rect.h * box.h
    val r = box.x + (b.right - rect.x) / rect.w * box.w
    val bt = box.y + (b.bottom - rect.y) / rect.h * box.h
    val size = Size(r - l, bt - t)
    drawRect(Color(0xB0000000), Offset(l, t), size, style = androidx.compose.ui.graphics.drawscope.Stroke(3.5.dp.toPx()))
    drawRect(Color.White, Offset(l, t), size, style = androidx.compose.ui.graphics.drawscope.Stroke(1.5.dp.toPx()))
    val hs = 4.dp.toPx()
    for (p in listOf(Offset(l, t), Offset(r, t), Offset(l, bt), Offset(r, bt),
                     Offset((l + r) / 2, t), Offset((l + r) / 2, bt), Offset(l, (t + bt) / 2), Offset(r, (t + bt) / 2))) {
        drawRect(Color(0xB0000000), Offset(p.x - hs - 1.dp.toPx(), p.y - hs - 1.dp.toPx()), Size(2 * hs + 2.dp.toPx(), 2 * hs + 2.dp.toPx()))
        drawRect(Color.White, Offset(p.x - hs, p.y - hs), Size(2 * hs, 2 * hs))
    }
}

/** A crosshair with an open center, outlined in black so it reads on any part of the palette. */
internal fun DrawScope.crosshair(at: Offset, arm: Float, gap: Float, color: Color, width: Float = 2.dp.toPx()) {
    val outline = width + 2.5.dp.toPx()
    for ((c, w) in listOf(Color(0xB0000000) to outline, color to width)) {
        drawLine(c, Offset(at.x - arm, at.y), Offset(at.x - gap, at.y), w, StrokeCap.Round)
        drawLine(c, Offset(at.x + gap, at.y), Offset(at.x + arm, at.y), w, StrokeCap.Round)
        drawLine(c, Offset(at.x, at.y - arm), Offset(at.x, at.y - gap), w, StrokeCap.Round)
        drawLine(c, Offset(at.x, at.y + gap), Offset(at.x, at.y + arm), w, StrokeCap.Round)
    }
}
