package dev.thermalview

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.drag
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.abs
import kotlin.math.roundToInt
import kotlinx.coroutines.delay

// The side bars (owner, 2026-09-26: "there are empty black spaces on both side of the screen, utilise
// them for quick action menus and items that I can toggle and activate"): the controls live in the
// margins beside the image, so nothing covers it. Each bar's column hugs the screen's outer edge, so
// the buttons stay under the thumbs and don't move when the view size changes. In portrait (M6's
// orientation) the margins are above and below the image, and the bars become rows there.

/** The widest a bar's column gets (the bars themselves are ~107 dp at Full, wider at smaller views). */
internal val BarMaxWidth: Dp = 150.dp

/** The tallest a bar's row gets (portrait: the rows above and below the image, ~107 dp at Full). */
internal val BarMaxHeight: Dp = 96.dp

/** True in a narrow bar (Full view) or a row: values get a smaller size so they fit. */
internal val LocalCompact = compositionLocalOf { false }

/** True in portrait: the bars are rows above and below the image. */
internal val LocalPortrait = compositionLocalOf { false }

/** The bars as the owner sees them, for text that points at them. */
@Composable
internal fun leftBarName() = if (LocalPortrait.current) "top row" else "left bar"

@Composable
internal fun rightBarName() = if (LocalPortrait.current) "bottom row" else "right bar"

/**
 * What a bar's content lays itself out with: in a column (landscape: the bars beside the image), each
 * item takes the bar's width; in a row (portrait: above and below it), its own width and the row's
 * height.
 */
internal class BarScope(val horizontal: Boolean, private val row: RowScope?, private val column: ColumnScope?) {
    /** An item: the column's width, or [rowWidth] wide and the row's height. */
    fun Modifier.item(rowWidth: Dp): Modifier = if (horizontal) width(rowWidth).fillMaxHeight() else fillMaxWidth()

    /** A tile inside an item's Box (a tile with a menu): it fills the item. */
    val inBox: Modifier get() = if (horizontal) Modifier.fillMaxHeight() else Modifier

    /** The rest of the bar's length. */
    fun Modifier.rest(): Modifier = if (row != null) with(row) { this@rest.weight(1f) } else with(column!!) { this@rest.weight(1f) }

    /** Where a tile's menu opens: toward the image. */
    fun menuSide(leftBar: Boolean) = when {
        horizontal -> if (leftBar) PopupSide.Below else PopupSide.Above
        else -> if (leftBar) PopupSide.Right else PopupSide.Left
    }
}

/** A bar: a column at most [BarMaxWidth] wide, or a row at most [BarMaxHeight] tall. */
@Composable
private fun Bar(modifier: Modifier, horizontal: Boolean, content: @Composable BarScope.() -> Unit) {
    if (horizontal) {
        CompositionLocalProvider(LocalCompact provides true) {
            Row(
                modifier.fillMaxWidth().heightIn(max = BarMaxHeight).fillMaxHeight(),
                horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically,
            ) { BarScope(true, this, null).content() }
        }
    } else {
        BoxWithConstraints(modifier.fillMaxHeight().widthIn(max = BarMaxWidth)) {
            CompositionLocalProvider(LocalCompact provides (maxWidth < 120.dp)) {
                Column(Modifier.fillMaxHeight(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    BarScope(false, null, this).content()
                }
            }
        }
    }
}

/**
 * The left bar (the top row in portrait): how the image looks. The palette and the view (each tap opens
 * its menu) and the box; the frame rate (debug), and while zoomed in, a way back to 1x. Noise and
 * texture are in Settings (owner, 2026-09-26).
 */
@Composable
fun LeftBar(
    modifier: Modifier,
    horizontal: Boolean,
    options: DebugOptions,
    onOptions: (DebugOptions) -> Unit,
    paletteColors: (palette: Int, rainbowPreset: Int) -> List<Color>,
    stats: (@Composable () -> Unit)?,
    zoom: Float,
    onResetZoom: () -> Unit,
    boxOn: Boolean,
    onBox: (Boolean) -> Unit,
    replay: String,
    onStopReplay: () -> Unit,
) {
    val pad = if (horizontal) Modifier.padding(start = 8.dp, end = 8.dp, top = 8.dp, bottom = 6.dp)
    else Modifier.padding(start = 8.dp, end = 6.dp, top = 8.dp, bottom = 8.dp)
    Bar(modifier.then(pad), horizontal) {
        stats?.invoke()
        // The palette: a tap opens the picker (owner, 2026-09-26: "a selection window to select color
        // palette and the colors if rainbow is picked").
        var picking by remember { mutableStateOf(false) }
        Box(Modifier.item(150.dp)) {
            Tile(inBox, onClick = { picking = true }, color = if (picking) Ui.TileActive else Ui.Tile) {
                Caption("Palette")
                Value(PALETTE_NAMES.getOrElse(options.palette) { "?" })
                Spacer(Modifier.height(5.dp))
                Swatch(paletteColors(options.palette, options.rainbowPreset), Modifier.fillMaxWidth())
                if (options.palette == 2) {
                    Spacer(Modifier.height(3.dp))
                    Note(MainActivity.RAINBOW_PRESETS.getOrElse(options.rainbowPreset) { MainActivity.RAINBOW_PRESETS[0] }.name)
                }
            }
            SidePopup(picking, { picking = false }, menuSide(leftBar = true), width = 300.dp) {
                PalettePicker(options, onOptions, paletteColors) { picking = false }
            }
        }
        // The view: its size and how the screen turns (owner, 2026-09-26).
        var viewing by remember { mutableStateOf(false) }
        Box(Modifier.item(128.dp)) {
            Tile(inBox, onClick = { viewing = true }, color = if (viewing) Ui.TileActive else Ui.Tile) {
                Caption("View")
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Value(MainActivity.VIEW_NAMES.getOrElse(options.viewSize) { "?" }, Modifier.weight(1f))
                    ViewGlyph(options.viewSize)
                }
                Note(MainActivity.ORIENTATION_NAMES.getOrElse(options.orientation) { "" })
            }
            SidePopup(viewing, { viewing = false }, menuSide(leftBar = true), width = 360.dp) {
                ViewMenu(options, onOptions) { viewing = false }
            }
        }
        // The box (PLAN M6): the readouts and the colors come from inside it; drag it, its edges or corners.
        Tile(Modifier.item(108.dp), onClick = { onBox(!boxOn) }, color = if (boxOn) Ui.TileActive else Ui.Tile) {
            Caption("Box")
            Row(verticalAlignment = Alignment.CenterVertically) {
                Value(if (boxOn) "On" else "Off", Modifier.weight(1f))
                BoxGlyph(boxOn)
            }
        }
        Spacer(Modifier.rest())
        if (zoom > 1.01f) {
            Tile(Modifier.item(96.dp), onClick = onResetZoom, color = Ui.TileActive) {
                Caption("Zoom")
                Value("%.1f×".format(zoom))
                Note("tap for 1×")
            }
        }
        if (replay.isNotEmpty()) {  // (a replay runs: the way back to the camera)
            Tile(Modifier.item(150.dp), onClick = onStopReplay, color = Ui.TileAlert) {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Caption("Replay", Color(0xFFFFB4B4), Modifier.weight(1f))
                    Text("✕", color = Color(0xFFFFB4B4), fontSize = 13.sp)
                }
                Value("Exit replay")
                Note(replay)
            }
        }
    }
}

/** The frame rate and lag, and frames dropped in the last 10 s in amber (debug; the details are in Diagnostics). */
@Composable
fun StatsLines(status: Status, recentDrops: Long) {
    Column(Modifier.padding(start = 4.dp)) {
        Text("%.0f fps".format(status.fps), color = Ui.Text, fontSize = 13.sp, style = Tabular)
        Text("lag %.0f ms".format(status.lagMs), color = Ui.Subtle, fontSize = 12.sp, style = Tabular)
        if (recentDrops > 0) {
            Text("$recentDrops dropped", color = Ui.Warning, fontSize = 12.sp, style = Tabular)
        }
    }
}

/**
 * The right bar (the bottom row in portrait): measuring and acting. The three readouts, the range
 * (Auto, or locked: M6) and the scale bar with the temperatures at its ends, Recalibrate, Capture
 * (debug) and Settings.
 */
@Composable
fun RightBar(
    modifier: Modifier,
    horizontal: Boolean,
    readings: Readings?,
    palette: IntArray,      // the palette's colors, cold to hot (the scale bar)
    marksLocked: Boolean,   // the palette marks pixels beyond a locked range (the rainbow's grey)
    live: Boolean,
    status: Status,
    panelOpen: Boolean,
    onPanel: () -> Unit,
) {
    val pad = if (horizontal) Modifier.padding(start = 8.dp, end = 8.dp, top = 6.dp, bottom = 8.dp)
    else Modifier.padding(start = 6.dp, end = 8.dp, top = 8.dp, bottom = 8.dp)
    Bar(modifier.then(pad), horizontal) {
        Tile(Modifier.item(112.dp)) {
            ReadoutRow(Ui.Hot, Ui.HotText, readings?.let { it.text(it.high, unit = false) })
            ReadoutRow(Ui.Center, Color.White, readings?.let { it.text(it.center, unit = false) })
            ReadoutRow(Ui.Cold, Ui.ColdText, readings?.let { it.text(it.low, unit = false) })
        }
        val locked = readings?.locked == true
        Tile(Modifier.item(104.dp), onClick = { NativeBridge.setRangeLock(!locked) }, enabled = live, color = if (locked) Ui.TileActive else Ui.Tile) {
            Caption("Range")
            Row(verticalAlignment = Alignment.CenterVertically) {
                Value(if (locked) "Locked" else "Auto", Modifier.weight(1f))
                LockGlyph(locked)
            }
        }
        ScaleBar(readings, palette, marksLocked, Modifier.rest().then(if (horizontal) Modifier.fillMaxHeight() else Modifier.fillMaxWidth()), horizontal)
        CalibrateTile(Modifier.item(104.dp), live)
        if (BuildConfig.DEBUG) CaptureTile(this, status, live)
        Tile(Modifier.item(92.dp), onClick = onPanel, color = if (panelOpen) Ui.TileActive else Ui.Tile) {
            Value(if (panelOpen) "Close" else "Settings")
        }
    }
}

@Composable
private fun ReadoutRow(marker: Color, textColor: Color, text: String?) {
    Row(Modifier.padding(vertical = 2.dp), verticalAlignment = Alignment.CenterVertically) {
        Canvas(Modifier.size(14.dp)) {
            val c = Offset(size.width / 2, size.height / 2)
            crosshair(c, size.width / 2 - 1.dp.toPx(), 2.dp.toPx(), marker, width = 1.6.dp.toPx())
        }
        Spacer(Modifier.width(8.dp))
        Text(
            text ?: "--", color = textColor, fontSize = if (LocalCompact.current) 16.sp else 18.sp, fontWeight = FontWeight.SemiBold,
            style = Tabular, maxLines = 1,
        )
    }
}

/** The scale bar's temperature axis along the bar: hot at the top (a column) or on the right (a row). */
private class ScaleAxis(val start: Float, val length: Float, val horizontal: Boolean, val lo: Float, val hi: Float) {
    fun pos(t: Float): Float {
        val f = (t - lo) / (hi - lo)
        return start + (if (horizontal) f else 1f - f) * length
    }

    fun temp(p: Float): Float {
        val f = (p - start) / length
        return lo + (if (horizontal) f else 1f - f) * (hi - lo)
    }
}

/**
 * The scale bar (PLAN M6; owner, 2026-09-26: "for xtherm the 2 ranges can be dragged and altered
 * separately"): a temperature axis spanning the scene and the color range, with a handle at each end
 * of the range. Between the handles, the palette as the image uses it (the mapping's curve); beyond
 * them, what the image shows there (the rainbow's grey marks when locked, else the palette's ends).
 * Dragging a handle moves that end alone, locking the range first if it was Auto; the axis holds
 * still while a finger is down. Ticks mark the high, center and low readouts at their temperatures.
 * Upright in a column (hot at the top), across in a row (hot on the right).
 */
@Composable
private fun ScaleBar(readings: Readings?, palette: IntArray, marksLocked: Boolean, modifier: Modifier, horizontal: Boolean) {
    val locked = readings?.locked == true
    val measurer = rememberTextMeasurer()
    // While dragging, and after it until the native side shows the same ends (at most 3 s), the ends
    // shown are ours. Unlocked, they're the native side's again at once.
    var ours by remember { mutableStateOf<Pair<Float, Float>?>(null) }
    var released by remember { mutableIntStateOf(0) }
    var frozenAxis by remember { mutableStateOf<Pair<Float, Float>?>(null) }
    val native by rememberUpdatedState((readings?.scaleLoC ?: Float.NaN) to (readings?.scaleHiC ?: Float.NaN))
    LaunchedEffect(locked) { if (!locked && frozenAxis == null) ours = null }
    LaunchedEffect(released) {
        if (released == 0) return@LaunchedEffect
        val until = System.currentTimeMillis() + 3000
        while (System.currentTimeMillis() < until) {
            val o = ours ?: return@LaunchedEffect
            if (abs(native.first - o.first) < 0.05f && abs(native.second - o.second) < 0.05f) break
            delay(100)
        }
        if (frozenAxis == null) ours = null
    }
    val lo = ours?.first ?: native.first
    val hi = ours?.second ?: native.second
    // The axis: the range and the scene's own extremes, with a margin (held while dragging).
    val axis = frozenAxis ?: run {
        val sceneLo = readings?.low?.takeIf { it.valid }?.tempC ?: lo
        val sceneHi = readings?.high?.takeIf { it.valid }?.tempC ?: hi
        val a0 = minOf(lo, sceneLo)
        val a1 = maxOf(hi, sceneHi)
        val pad = maxOf(0.06f * (a1 - a0), 0.3f)
        (a0 - pad) to (a1 + pad)
    }
    val current by rememberUpdatedState(Triple(lo, hi, axis))
    val lockedNow by rememberUpdatedState(locked)  // (the gesture outlives compositions)
    val labelStyle = TextStyle(fontSize = 13.sp, fontFeatureSettings = "tnum", color = if (locked) Ui.Accent else Ui.Text)
    // The axis's ends on the bar: 10 dp in from a column's ends, 16 dp from a row's (its labels are centered on the handles).
    fun axisIn(width: Float, height: Float, ax: Pair<Float, Float>, dp: Float) =
        if (horizontal) ScaleAxis(16 * dp, width - 32 * dp, true, ax.first, ax.second)
        else ScaleAxis(10 * dp, height - 20 * dp, false, ax.first, ax.second)
    Box(
        modifier.pointerInput(horizontal) {
            awaitEachGesture {
                val down = awaitFirstDown()
                val (l0, h0, ax) = current
                if (l0.isNaN() || h0.isNaN() || ax.first.isNaN()) return@awaitEachGesture
                val a = axisIn(size.width.toFloat(), size.height.toFloat(), ax, 1.dp.toPx())
                if (a.length <= 0f) return@awaitEachGesture  // (no room for the bar)
                fun along(p: Offset) = if (horizontal) p.x else p.y
                // The nearer handle takes the drag.
                val hot = abs(along(down.position) - a.pos(h0)) <= abs(along(down.position) - a.pos(l0))
                frozenAxis = ax
                if (!lockedNow) NativeBridge.setRangeLock(true)
                val minSpan = minOf(0.5f, h0 - l0)
                try {
                    drag(down.id) { change ->
                        change.consume()
                        val t = a.temp(along(change.position)).coerceIn(ax.first, ax.second)
                        val next = if (hot) l0 to maxOf(t, l0 + minSpan) else minOf(t, h0 - minSpan) to h0
                        ours = next
                        NativeBridge.setRangeEnds(next.first, next.second)
                    }
                } finally {
                    frozenAxis = null
                    released += 1
                }
            }
        },
    ) {
        Canvas(Modifier.fillMaxSize()) {
            val ax = axis
            if (lo.isNaN() || hi.isNaN() || ax.first.isNaN() || palette.isEmpty()) return@Canvas
            val a = axisIn(size.width, size.height, ax, 1.dp.toPx())
            if (a.length <= 0f) return@Canvas
            val barW = 16.dp.toPx()
            fun color(i: Float) = Color(palette[(i.coerceIn(0f, 1f) * (palette.size - 1)).roundToInt()])
            val curve = readings?.curve ?: FloatArray(0)
            val above = if (marksLocked && locked) Color(0xFFC8C8C8) else color(0.97f)  // (palettes/rainbow_*.json)
            val below = if (marksLocked && locked) Color(0xFF555555) else color(0.03f)
            fun colorAt(t: Float) = when {
                t > hi -> above
                t < lo -> below
                curve.size >= 2 && hi > lo -> {
                    val f = (t - lo) / (hi - lo) * (curve.size - 1)
                    val k = f.toInt().coerceIn(0, curve.size - 2)
                    val v = curve[k] + (f - k) * (curve[k + 1] - curve[k])
                    color(if (v.isNaN()) (t - lo) / (hi - lo) * 0.94f + 0.03f else v)
                }
                else -> color((t - lo) / (hi - lo) * 0.94f + 0.03f)
            }
            val hiText = measurer.measure(if (readings?.scaleHiOver == true && ours == null) "> 120°" else "%.1f°".format(hi), labelStyle)
            val loText = measurer.measure("%.1f°".format(lo), labelStyle)
            val gap = 2.dp.toPx()
            // The bar across its length: a column's at its right edge; a row's under the labels, the
            // pair centered in the row's height.
            val across = if (horizontal) {
                (size.height - (hiText.size.height + 9.dp.toPx() + barW + 5.dp.toPx())) / 2 + hiText.size.height + 9.dp.toPx()
            } else size.width - barW - 10.dp.toPx()
            fun rect(p0: Float, p1: Float, o: Float = 0f) =
                if (horizontal) Offset(p0, across - o) to Size(p1 - p0, barW + 2 * o) else Offset(across - o, p0) to Size(barW + 2 * o, p1 - p0)
            // The bar, 2 px at a time: the mapping between the ends, the marks or the palette's ends beyond.
            val step = 2f
            var p = a.start
            while (p < a.start + a.length) {
                val q = minOf(p + step, a.start + a.length)
                val (tl, sz) = rect(p, q)
                drawRect(colorAt(a.temp((p + q) / 2)), tl, sz)
                p += step
            }
            rect(a.start, a.start + a.length).let { (tl, sz) -> drawRect(Color(0x80000000), tl, sz, style = Stroke(1.dp.toPx())) }
            // The readouts at their temperatures.
            if (readings != null) {
                for ((s, c) in listOf(readings.low to Ui.Cold, readings.center to Ui.Center, readings.high to Ui.Hot)) {
                    val t = when {
                        s.overRange -> ax.second
                        s.valid -> s.tempC
                        else -> continue
                    }
                    val at = a.pos(t.coerceIn(ax.first, ax.second))
                    val o = 3.dp.toPx()
                    fun line(color: Color, inset: Float, width: Float) = if (horizontal) {
                        drawLine(color, Offset(at, across - o + inset), Offset(at, across + barW + o - inset), width)
                    } else {
                        drawLine(color, Offset(across - o + inset, at), Offset(across + barW + o - inset, at), width)
                    }
                    line(Color(0xC0000000), 0f, 4.dp.toPx())
                    line(c, 1.dp.toPx(), 2.dp.toPx())
                }
            }
            // The handles, and their temperatures beside them (a column: to the left; a row: above),
            // pushed apart when they'd touch.
            val pHi = a.pos(hi)
            val pLo = a.pos(lo)
            val knobColor = if (locked) Ui.Accent else Color.White
            for (at in listOf(pHi, pLo)) {
                val knob = if (horizontal) Size(8.dp.toPx(), barW + 10.dp.toPx()) else Size(barW + 10.dp.toPx(), 8.dp.toPx())
                val tl = if (horizontal) Offset(at - knob.width / 2, across - 5.dp.toPx()) else Offset(across - 5.dp.toPx(), at - knob.height / 2)
                drawRoundRect(Color(0xE0000000), tl - Offset(1.dp.toPx(), 1.dp.toPx()), Size(knob.width + 2.dp.toPx(), knob.height + 2.dp.toPx()),
                    CornerRadius(4.dp.toPx()))
                drawRoundRect(knobColor, tl, knob, CornerRadius(3.dp.toPx()))
            }
            if (horizontal) {
                var hiX = pHi - hiText.size.width / 2f
                var loX = pLo - loText.size.width / 2f
                val overlap = loX + loText.size.width + 2 * gap - hiX
                if (overlap > 0) {
                    hiX += overlap / 2
                    loX -= overlap / 2
                }
                // (kept in the row, and the low one left of the high one; never throwing when it's too narrow)
                loX = loX.coerceAtMost(size.width - loText.size.width - hiText.size.width - 2 * gap).coerceAtLeast(0f)
                hiX = hiX.coerceAtLeast(loX + loText.size.width + 2 * gap).coerceAtMost(maxOf(0f, size.width - hiText.size.width))
                val y = across - 9.dp.toPx() - hiText.size.height
                drawText(hiText, topLeft = Offset(hiX, y))
                drawText(loText, topLeft = Offset(loX, y))
            } else {
                var hiLabelY = pHi - hiText.size.height / 2f
                var loLabelY = pLo - loText.size.height / 2f
                val overlap = hiLabelY + hiText.size.height + gap - loLabelY
                if (overlap > 0) {
                    hiLabelY -= overlap / 2
                    loLabelY += overlap / 2
                }
                hiLabelY = hiLabelY.coerceAtMost(size.height - hiText.size.height).coerceAtLeast(0f)
                loLabelY = loLabelY.coerceAtMost(size.height - loText.size.height).coerceAtLeast(0f)
                drawText(hiText, topLeft = Offset(across - 8.dp.toPx() - hiText.size.width, hiLabelY))
                drawText(loText, topLeft = Offset(across - 8.dp.toPx() - loText.size.width, loLabelY))
            }
        }
    }
}

/** A padlock, closed when [locked]. */
@Composable
private fun LockGlyph(locked: Boolean) {
    Canvas(Modifier.size(width = 14.dp, height = 16.dp)) {
        val c = if (locked) Ui.Accent else Ui.Subtle
        val w = 1.6.dp.toPx()
        val bodyTop = size.height * 0.45f
        drawRoundRect(c, Offset(0f, bodyTop), androidx.compose.ui.geometry.Size(size.width, size.height - bodyTop),
            androidx.compose.ui.geometry.CornerRadius(2.dp.toPx()))
        val r = size.width * 0.3f
        val cx = size.width / 2
        val lift = if (locked) 0f else 3.dp.toPx()  // open: the shackle up
        drawArc(
            c, 180f, 180f, false, Offset(cx - r, bodyTop - r - 2.dp.toPx() - lift),
            androidx.compose.ui.geometry.Size(2 * r, 2 * r), style = androidx.compose.ui.graphics.drawscope.Stroke(w),
        )
        drawLine(c, Offset(cx - r, bodyTop - 2.dp.toPx() - lift), Offset(cx - r, bodyTop), w)
        if (locked) drawLine(c, Offset(cx + r, bodyTop - 2.dp.toPx()), Offset(cx + r, bodyTop), w)
    }
}

/** Recalibrate (PLAN M6): sends 0x8000 through the gate, then waits out its 10 s before the next. */
@Composable
private fun CalibrateTile(modifier: Modifier, live: Boolean) {
    var until by remember { mutableLongStateOf(0L) }
    var now by remember { mutableLongStateOf(System.currentTimeMillis()) }
    var result by remember { mutableStateOf("") }
    LaunchedEffect(until) {
        while (System.currentTimeMillis() < until) {
            now = System.currentTimeMillis()
            delay(200)
        }
        now = System.currentTimeMillis()
    }
    LaunchedEffect(result) {
        if (result.isNotEmpty()) {
            delay(3000)
            result = ""
        }
    }
    val left = ((until - now + 999) / 1000).coerceAtLeast(0)
    val ready = live && left == 0L
    Tile(modifier, onClick = {
        if (ready) {
            val r = NativeBridge.sendShutter()
            if (r == "sent") until = System.currentTimeMillis() + 10_000 else result = r
        }
    }, enabled = ready) {
        Value("Calibrate")
        Note(
            when {
                result.isNotEmpty() -> result
                left > 0 -> "again in $left s"
                else -> "shutter + NUC"
            },
        )
    }
}

private class CaptureMode(val title: String, val caption: String, val note: String, val description: String)

// (Mark, which only wrote a line to the field log for test scripts, is gone: the owner, 2026-09-26.)
private val CAPTURE_MODES = listOf(
    CaptureMode("Record", "Record", "200 frames", "Records 200 raw frames now"),
    CaptureMode("Recalibrate + record", "Recal", "shutter, then 200", "Waits for the shutter to cool, recalibrates, then records 200 frames"),
)

/**
 * Capture (was Ready): runs the chosen mode and shows its progress on the tile; the mode row (or a long
 * press) picks the mode. While it runs, a tap cancels it (owner, 2026-09-26: "a button to cancel the
 * record if misclicked"): the frames so far are dropped. Each run logs "owner mark: …" first, so test
 * scripts can still wait for it.
 */
@Composable
private fun CaptureTile(bar: BarScope, status: Status, live: Boolean) {
    var mode by rememberSaveable { mutableIntStateOf(0) }
    var count by rememberSaveable { mutableIntStateOf(0) }
    var menu by remember { mutableStateOf(false) }
    var flash by remember { mutableStateOf("") }
    var wasRecording by remember { mutableStateOf(false) }
    // (a dump is still being written after its last frame: "capturing" until it says "saved")
    val writing = status.dumpTotal == 0 && status.dump.startsWith("capturing")
    LaunchedEffect(status.dumpTotal, status.dump) {
        val recording = status.dumpTotal > 0 || status.dump.startsWith("capturing")
        if (wasRecording && !recording) flash = if (status.dump.startsWith("saved")) "saved" else status.dump
        wasRecording = recording
    }
    LaunchedEffect(flash) {
        if (flash.isNotEmpty()) {
            delay(3000)
            flash = ""
        }
    }
    val running = status.dumpTotal > 0 || status.capture.isNotEmpty()  // (cancellable)
    val busy = running || writing
    val m = CAPTURE_MODES[mode.coerceIn(0, CAPTURE_MODES.size - 1)]
    val tap = {
        when {
            running -> {
                NativeBridge.cancelCapture()
                flash = "cancelled"
            }
            live && !busy -> {
                count += 1
                val label = "${m.caption.lowercase()} #$count"
                NativeBridge.mark(label)
                flash = if (mode == 0) NativeBridge.startDump(200).let { if (it.startsWith("dump_")) "" else it }
                else NativeBridge.readyCapture(label, false).let { if (it == "capture started") "" else it }
            }
        }
    }
    Box(with(bar) { Modifier.item(112.dp) }) {
        Tile(bar.inBox, onClick = tap, onLongClick = { if (!busy) menu = true }, enabled = live || busy, color = if (busy) Ui.TileBusy else Ui.Tile) {
            if (running) {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Caption(m.caption, Ui.Warning, Modifier.weight(1f))
                    Text("✕", color = Ui.Warning, fontSize = 13.sp)
                }
                Value("Cancel")
            } else {
                // The mode, and the way to change it: the whole caption row (and a long press anywhere).
                Row(
                    Modifier.fillMaxWidth().combinedClickableCompat { if (!busy) menu = true }.padding(vertical = 2.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Caption(m.caption, Ui.Accent, Modifier.weight(1f))
                    Chevron(Ui.Accent)
                }
                Value("Capture")
            }
            val progress = when {
                status.dumpTotal > 0 -> "${status.dumpDone} / ${status.dumpTotal}"
                writing -> "saving…"
                status.capture.isNotEmpty() -> status.capture
                flash.isNotEmpty() -> flash
                else -> m.note
            }
            Note(progress, if (busy) Ui.Warning else Ui.Subtle)
            if (status.dumpTotal > 0) {
                Spacer(Modifier.height(4.dp))
                Box(Modifier.fillMaxWidth().height(3.dp).background(Ui.Faint, RoundedCornerShape(2.dp))) {
                    Box(
                        Modifier.fillMaxWidth(status.dumpDone.toFloat() / status.dumpTotal).height(3.dp)
                            .background(Ui.Warning, RoundedCornerShape(2.dp)),
                    )
                }
            }
        }
        SidePopup(menu, { menu = false }, bar.menuSide(leftBar = false), width = 300.dp) {
            MenuHeading("Capture")
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                CAPTURE_MODES.forEachIndexed { i, cm ->
                    ChoiceCard(i == mode, {
                        mode = i
                        menu = false
                    }, Modifier.fillMaxWidth()) {
                        ChoiceName(cm.title, i == mode, check = true)
                        Spacer(Modifier.height(2.dp))
                        Text(cm.description, color = Ui.Subtle, fontSize = 12.sp, lineHeight = 15.sp)
                    }
                }
            }
        }
    }
}

/** A small downward chevron: "more choices here" (drawn: the font's ▾ is tiny on this tablet). */
@Composable
private fun Chevron(color: Color) {
    Canvas(Modifier.size(width = 10.dp, height = 6.dp)) {
        val w = 1.6.dp.toPx()
        drawLine(color, Offset(w / 2, w / 2), Offset(size.width / 2, size.height - w / 2), w, StrokeCap.Round)
        drawLine(color, Offset(size.width / 2, size.height - w / 2), Offset(size.width - w / 2, w / 2), w, StrokeCap.Round)
    }
}

/** A measuring box: a dashed frame in a frame. */
@Composable
private fun BoxGlyph(on: Boolean) {
    Canvas(Modifier.size(width = 20.dp, height = 15.dp)) {
        val c = if (on) Ui.Accent else Ui.Subtle
        val w = 1.5.dp.toPx()
        drawRect(c.copy(alpha = 0.45f), Offset.Zero, this.size, style = androidx.compose.ui.graphics.drawscope.Stroke(w))
        val inset = androidx.compose.ui.geometry.Size(this.size.width * 0.5f, this.size.height * 0.5f)
        drawRect(c, Offset(this.size.width * 0.25f, this.size.height * 0.25f), inset, style = androidx.compose.ui.graphics.drawscope.Stroke(w))
    }
}

/** The view size as a small 4:3 frame that grows with it. */
@Composable
private fun ViewGlyph(size: Int) {
    Canvas(Modifier.size(width = 20.dp, height = 15.dp)) {
        val f = listOf(0.45f, 0.7f, 1f).getOrElse(size) { 1f }
        val w = this.size.width * f
        val h = this.size.height * f
        drawRect(Ui.Accent, Offset(this.size.width - w, this.size.height - h), androidx.compose.ui.geometry.Size(w, h),
            style = androidx.compose.ui.graphics.drawscope.Stroke(1.5.dp.toPx()))
    }
}

// --- Building blocks -------------------------------------------------------------------------------

internal val Tabular = TextStyle(fontFeatureSettings = "tnum")
internal val PALETTE_NAMES = listOf("Gray", "White hot", "Rainbow")

/** A disabled tile's text is dimmed (its colors, not a layer: see [Tile]). */
internal val LocalTileEnabled = compositionLocalOf { true }

private fun Color.dimmedUnless(enabled: Boolean) = if (enabled) this else copy(alpha = alpha * 0.45f)

/**
 * A side-bar button. Drawn without graphics layers: with a clip and an alpha layer, some tiles came
 * back from the app's background with no background drawn (on this tablet, 2026-09-26). So: a shaped
 * background, a drawn highlight while pressed instead of the ripple, and dimmed text when disabled.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
internal fun Tile(
    modifier: Modifier = Modifier,
    onClick: (() -> Unit)? = null,
    onLongClick: (() -> Unit)? = null,
    enabled: Boolean = true,
    color: Color = Ui.Tile,
    content: @Composable ColumnScope.() -> Unit,
) {
    val shape = RoundedCornerShape(14.dp)
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    CompositionLocalProvider(LocalTileEnabled provides enabled) {
        Column(
            modifier.fillMaxWidth().background(if (pressed) lerp(color, Color.White, 0.10f) else color, shape)
                .then(
                    if (onClick != null) {
                        Modifier.combinedClickable(
                            interactionSource = interaction, indication = null, enabled = enabled,
                            onClick = onClick, onLongClick = onLongClick,
                        )
                    } else Modifier,
                )
                .padding(horizontal = 9.dp, vertical = 7.dp),
            verticalArrangement = Arrangement.Center,  // (in a row, tiles share its height)
            content = content,
        )
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun Modifier.combinedClickableCompat(onClick: () -> Unit) =
    combinedClickable(interactionSource = remember { MutableInteractionSource() }, indication = null, onClick = onClick)

@Composable
internal fun Caption(text: String, modifier: Modifier = Modifier) = Caption(text, Ui.Subtle, modifier)

@Composable
internal fun Caption(text: String, color: Color, modifier: Modifier = Modifier) {
    Text(
        text.uppercase(), color = color.dimmedUnless(LocalTileEnabled.current), fontSize = 10.sp, fontWeight = FontWeight.SemiBold,
        letterSpacing = 0.8.sp, maxLines = 1, modifier = modifier,
    )
}

@Composable
internal fun Value(text: String, modifier: Modifier = Modifier, size: TextUnit = if (LocalCompact.current) 14.sp else 16.sp) {
    Text(
        text, color = Ui.Text.dimmedUnless(LocalTileEnabled.current), fontSize = size, fontWeight = FontWeight.Medium,
        maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = modifier,
    )
}

@Composable
internal fun Note(text: String, color: Color = Ui.Subtle) {
    Text(
        text, color = color.dimmedUnless(LocalTileEnabled.current), fontSize = 11.sp, maxLines = 2,
        overflow = TextOverflow.Ellipsis, lineHeight = 13.sp, style = Tabular,
    )
}
