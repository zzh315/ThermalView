package dev.thermalview

import androidx.compose.ui.geometry.Rect

/**
 * The image's view inside a w x h surface, in surface pixels, as the renderer draws it (renderer.cpp
 * draw()): 4:3, or 3:4 with the image turned a quarter.
 */
data class ViewBox(val x: Float, val y: Float, val w: Float, val h: Float)

/**
 * [longSidePx]: a view-size preset, the image's long side (0: the largest fit); [rot]: the image's
 * quarter turns clockwise on the screen (M6's orientation). Placed as the renderer's glViewport places
 * it (x from the left, y from the bottom, both rounded down).
 */
fun viewBox(w: Int, h: Int, longSidePx: Int, rot: Int = 0): ViewBox {
    val upright = rot % 2 == 0
    val aw = if (upright) 4 else 3
    val ah = if (upright) 3 else 4
    var vw = w
    var vh = h
    if (w.toLong() * ah > h.toLong() * aw) vw = h * aw / ah else vh = w * ah / aw
    if (longSidePx in 1 until maxOf(vw, vh)) {  // a smaller view-size preset, centered
        vw = if (upright) longSidePx else longSidePx * 3 / 4
        vh = if (upright) longSidePx * 3 / 4 else longSidePx
    }
    return ViewBox(((w - vw) / 2).toFloat(), (h - vh - (h - vh) / 2).toFloat(), vw.toFloat(), vh.toFloat())
}

/**
 * M6's orientation: with the image turned [rot] quarter turns clockwise on the screen, the point
 * (s, t) of the view (across and down it, 0..1) shows the camera's (a, b) (across and down the part of
 * the frame shown), as the renderer's vertex shader maps it.
 */
fun camUV(rot: Int, s: Float, t: Float): Pair<Float, Float> = when (rot and 3) {
    1 -> t to 1f - s
    2 -> 1f - s to 1f - t
    3 -> 1f - t to s
    else -> s to t
}

/** Where the camera's (a, b) shows in the view: the inverse of [camUV]. */
fun viewUV(rot: Int, a: Float, b: Float): Pair<Float, Float> = when (rot and 3) {
    1 -> 1f - b to a
    2 -> 1f - a to 1f - b
    3 -> b to 1f - a
    else -> a to b
}

/**
 * Zoom and pan (docs/PLAN.md M6): the visible part of the frame, in camera pixels, from a zoom factor
 * (1 = the whole frame) and the camera point at the view's center, clamped so the image always fills
 * the view. Camera coordinates are continuous: pixel p spans [p, p + 1).
 */
data class CamRect(val x: Float, val y: Float, val w: Float, val h: Float) {
    companion object {
        const val FRAME_W = 256f
        const val FRAME_H = 192f
        const val MAX_ZOOM = 8f  // PLAN M6: 1x-8x

        fun of(zoom: Float, cx: Float, cy: Float): CamRect {
            val z = zoom.coerceIn(1f, MAX_ZOOM)
            val w = FRAME_W / z
            val h = FRAME_H / z
            return CamRect((cx - w / 2).coerceIn(0f, FRAME_W - w), (cy - h / 2).coerceIn(0f, FRAME_H - h), w, h)
        }
    }

    val zoom get() = FRAME_W / w
    val cx get() = x + w / 2
    val cy get() = y + h / 2

    /** A camera point on the surface, with the image turned [rot] quarters in [box]. */
    fun toSurface(box: ViewBox, camX: Float, camY: Float, rot: Int = 0): Pair<Float, Float> {
        val (s, t) = viewUV(rot, (camX - x) / w, (camY - y) / h)
        return Pair(box.x + s * box.w, box.y + t * box.h)
    }

    /** The camera point at a surface point: the inverse of [toSurface]. */
    fun toCamera(box: ViewBox, sx: Float, sy: Float, rot: Int = 0): Pair<Float, Float> {
        val (a, b) = camUV(rot, (sx - box.x) / box.w, (sy - box.y) / box.h)
        return Pair(x + a * w, y + b * h)
    }

    /** A move of (dx, dy) surface pixels as a move in camera pixels. */
    fun cameraDelta(box: ViewBox, dx: Float, dy: Float, rot: Int = 0): Pair<Float, Float> {
        val (a0, b0) = camUV(rot, 0f, 0f)
        val (a1, b1) = camUV(rot, dx / box.w, dy / box.h)
        return Pair((a1 - a0) * w, (b1 - b0) * h)
    }

    /** One camera pixel's size on the surface. */
    fun pixelSize(box: ViewBox, rot: Int = 0) = if (rot % 2 == 0) box.w / w else box.w / h

    /** A camera rectangle (x0, y0 to x1, y1) on the surface. */
    fun toSurface(box: ViewBox, x0: Float, y0: Float, x1: Float, y1: Float, rot: Int = 0): Rect {
        val (ax, ay) = toSurface(box, x0, y0, rot)
        val (bx, by) = toSurface(box, x1, y1, rot)
        return Rect(minOf(ax, bx), minOf(ay, by), maxOf(ax, bx), maxOf(ay, by))
    }

    /**
     * After a pinch or drag: the camera point that was under the fingers' previous centroid stays under
     * the new one, with the zoom multiplied by gestureZoom (both in surface pixels, within [box]).
     */
    fun transformed(
        box: ViewBox, centroidX: Float, centroidY: Float, panX: Float, panY: Float, gestureZoom: Float, rot: Int = 0,
    ): CamRect {
        val (px, py) = toCamera(box, centroidX - panX, centroidY - panY, rot)
        val z = (zoom * gestureZoom).coerceIn(1f, MAX_ZOOM)
        val nw = FRAME_W / z
        val nh = FRAME_H / z
        val (a, b) = camUV(rot, (centroidX - box.x) / box.w, (centroidY - box.y) / box.h)
        return of(z, px - a * nw + nw / 2, py - b * nh + nh / 2)
    }
}
