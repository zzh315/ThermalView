package dev.thermalview

import android.content.SharedPreferences
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.calculateCentroid
import androidx.compose.foundation.gestures.calculateCentroidSize
import androidx.compose.foundation.gestures.calculatePan
import androidx.compose.foundation.gestures.calculateZoom
import androidx.compose.runtime.saveable.Saver
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputScope
import androidx.compose.ui.input.pointer.positionChange
import androidx.compose.ui.input.pointer.positionChanged
import androidx.compose.ui.unit.dp
import kotlin.math.abs
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * M6's box, in whole camera pixels (so view size and zoom don't disturb it), at least [MIN] a side.
 * It starts centered, half the frame's width and height, and is kept across launches.
 */
data class CamBox(val x: Int, val y: Int, val w: Int, val h: Int) {
    val right get() = x + w
    val bottom get() = y + h

    /** After a drag of (dx, dy) camera pixels on [grab], from this box: snapped, kept in the frame. */
    fun edited(grab: Grab, dx: Float, dy: Float): CamBox {
        val fw = CamRect.FRAME_W.toInt()
        val fh = CamRect.FRAME_H.toInt()
        if (grab == Grab.Move) {
            val nx = (x + dx).roundToInt().coerceIn(0, fw - w)
            val ny = (y + dy).roundToInt().coerceIn(0, fh - h)
            return copy(x = nx, y = ny)
        }
        var x0 = x
        var y0 = y
        var x1 = right
        var y1 = bottom
        if (grab.left) x0 = (x + dx).roundToInt().coerceIn(0, x1 - MIN)
        if (grab.right) x1 = (right + dx).roundToInt().coerceIn(x0 + MIN, fw)
        if (grab.top) y0 = (y + dy).roundToInt().coerceIn(0, y1 - MIN)
        if (grab.bottom) y1 = (bottom + dy).roundToInt().coerceIn(y0 + MIN, fh)
        return CamBox(x0, y0, x1 - x0, y1 - y0)
    }

    fun save(prefs: SharedPreferences) {
        prefs.edit().putString("box", "$x,$y,$w,$h").apply()
    }

    companion object {
        const val MIN = 4
        val DEFAULT = CamBox(64, 48, 128, 96)

        fun load(prefs: SharedPreferences): CamBox {
            val v = prefs.getString("box", null)?.split(',')?.mapNotNull { it.toIntOrNull() }
            if (v == null || v.size != 4) return DEFAULT
            val fw = CamRect.FRAME_W.toInt()
            val fh = CamRect.FRAME_H.toInt()
            val w = v[2].coerceIn(MIN, fw)
            val h = v[3].coerceIn(MIN, fh)
            return CamBox(v[0].coerceIn(0, fw - w), v[1].coerceIn(0, fh - h), w, h)
        }

        val Saver = Saver<CamBox, IntArray>(save = { intArrayOf(it.x, it.y, it.w, it.h) }, restore = { CamBox(it[0], it[1], it[2], it[3]) })
    }
}

/** What a touch on the box takes hold of: the whole box, an edge or a corner. */
enum class Grab(val left: Boolean, val top: Boolean, val right: Boolean, val bottom: Boolean) {
    Move(false, false, false, false),
    Left(true, false, false, false), Right(false, false, true, false),
    Top(false, true, false, false), Bottom(false, false, false, true),
    TopLeft(true, true, false, false), TopRight(false, true, true, false),
    BottomLeft(true, false, false, true), BottomRight(false, false, true, true),
}

/**
 * Hit-testing in view pixels (the box's corners at l, t, r, b), with targets that stay large on a small
 * box, as the Android croppers do (PRIOR_ART.md): well inside moves; else the nearest corner, then the
 * nearest edge within reach; else anywhere inside moves.
 */
fun hitBox(p: Offset, l: Float, t: Float, r: Float, b: Float, reach: Float): Grab? {
    val inside = p.x in l..r && p.y in t..b
    val depth = if (inside) min(min(p.x - l, r - p.x), min(p.y - t, b - p.y)) else -1f
    if (inside && depth >= min(reach * 0.6f, 0.25f * min(r - l, b - t))) return Grab.Move
    val corners = listOf(Grab.TopLeft to Offset(l, t), Grab.TopRight to Offset(r, t), Grab.BottomLeft to Offset(l, b), Grab.BottomRight to Offset(r, b))
    corners.minBy { (it.second - p).getDistance() }.let { (g, c) -> if ((c - p).getDistance() <= reach) return g }
    val edges = listOf(
        Grab.Left to if (p.y in t - reach..b + reach) abs(p.x - l) else Float.MAX_VALUE,
        Grab.Right to if (p.y in t - reach..b + reach) abs(p.x - r) else Float.MAX_VALUE,
        Grab.Top to if (p.x in l - reach..r + reach) abs(p.y - t) else Float.MAX_VALUE,
        Grab.Bottom to if (p.x in l - reach..r + reach) abs(p.y - b) else Float.MAX_VALUE,
    )
    edges.minBy { it.second }.let { (g, d) -> if (d <= reach * 0.8f) return g }
    return if (inside) Grab.Move else null
}

/**
 * The image's gestures: with the box on, a finger that lands on it edits it (until it lifts, or a
 * second finger turns the gesture into a zoom); anything else zooms and pans (as
 * detectTransformGestures: pinch around the fingers, drag to pan). Positions are view pixels.
 */
suspend fun PointerInputScope.imageGestures(
    boxAt: () -> CamBox?,           // the box if it's on
    rect: () -> CamRect,            // the camera pixels shown
    viewW: Float,
    viewH: Float,
    onBox: (CamBox) -> Unit,
    onTransform: (centroid: Offset, pan: Offset, zoom: Float) -> Unit,
) {
    val reach = 28.dp.toPx()
    awaitEachGesture {
        val down = awaitFirstDown(requireUnconsumed = false)
        val box = boxAt()
        val r = rect()
        val grab = box?.let {
            val l = (it.x - r.x) / r.w * viewW
            val t = (it.y - r.y) / r.h * viewH
            val rr = (it.right - r.x) / r.w * viewW
            val bb = (it.bottom - r.y) / r.h * viewH
            hitBox(down.position, l, t, rr, bb, reach)
        }
        if (box != null && grab != null) {
            var total = Offset.Zero
            var dragging = false
            while (true) {
                val event = awaitPointerEvent()
                if (event.changes.count { it.pressed } > 1) {  // a second finger: zoom from here
                    transform(onTransform)
                    return@awaitEachGesture
                }
                val change = event.changes.firstOrNull { it.id == down.id }
                if (change == null || !change.pressed) break
                total += change.positionChange()
                if (!dragging && total.getDistance() > viewConfiguration.touchSlop) dragging = true
                if (dragging) {
                    change.consume()
                    onBox(box.edited(grab, total.x / viewW * r.w, total.y / viewH * r.h))
                }
            }
        } else {
            transform(onTransform)
        }
    }
}

/** detectTransformGestures' loop (Compose foundation), for the rest of a gesture already begun. */
private suspend fun AwaitPointerEventScope.transform(onTransform: (Offset, Offset, Float) -> Unit) {
    var zoom = 1f
    var pan = Offset.Zero
    var pastSlop = false
    val slop = viewConfiguration.touchSlop
    do {
        val event = awaitPointerEvent()
        if (event.changes.any { it.isConsumed }) break
        val zoomChange = event.calculateZoom()
        val panChange = event.calculatePan()
        if (!pastSlop) {
            zoom *= zoomChange
            pan += panChange
            val centroidSize = event.calculateCentroidSize(useCurrent = false)
            if (abs(1 - zoom) * centroidSize > slop || pan.getDistance() > slop) pastSlop = true
        }
        if (pastSlop) {
            val centroid = event.calculateCentroid(useCurrent = false)
            if (zoomChange != 1f || panChange != Offset.Zero) onTransform(centroid, panChange, zoomChange)
            event.changes.forEach { if (it.positionChanged()) it.consume() }
        }
    } while (event.changes.any { it.pressed })
}
