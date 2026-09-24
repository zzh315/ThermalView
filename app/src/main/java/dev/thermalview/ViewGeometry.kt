package dev.thermalview

import kotlin.math.max
import kotlin.math.min

/** The 4:3 view inside a w x h surface, in surface pixels, as the renderer draws it (renderer.cpp draw()). */
data class ViewBox(val x: Float, val y: Float, val w: Float, val h: Float)

fun viewBox(w: Int, h: Int, viewWidthPx: Int): ViewBox {
    var vw = w
    var vh = h
    if (w.toLong() * 3 > h.toLong() * 4) vw = h * 4 / 3 else vh = w * 3 / 4
    if (viewWidthPx in 1 until vw) {  // a smaller view-size preset, centered
        vw = viewWidthPx
        vh = viewWidthPx * 3 / 4
    }
    return ViewBox((w - vw) / 2f, (h - vh) / 2f, vw.toFloat(), vh.toFloat())
}

/**
 * Zoom and pan (docs/PLAN.md M6): the visible part of the frame, in camera pixels, from a zoom factor
 * (1 = the whole frame) and the camera point at the view's center, clamped so the image always fills
 * the view.
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

    /**
     * After a pinch or drag: the camera point that was under the fingers' previous centroid stays under
     * the new one, with the zoom multiplied by gestureZoom (both in surface pixels, within [box]).
     */
    fun transformed(box: ViewBox, centroidX: Float, centroidY: Float, panX: Float, panY: Float, gestureZoom: Float): CamRect {
        val px = x + (centroidX - panX - box.x) / box.w * w
        val py = y + (centroidY - panY - box.y) / box.h * h
        val z = (zoom * gestureZoom).coerceIn(1f, MAX_ZOOM)
        val nw = FRAME_W / z
        val nh = FRAME_H / z
        val nx = px - (centroidX - box.x) / box.w * nw
        val ny = py - (centroidY - box.y) / box.h * nh
        return of(z, nx + nw / 2, ny + nh / 2)
    }

    /** A camera point on the surface (pixel centers at +0.5). */
    fun toSurface(box: ViewBox, camX: Float, camY: Float) =
        Pair(box.x + (camX + 0.5f - x) / w * box.w, box.y + (camY + 0.5f - y) / h * box.h)

    fun contains(camX: Float, camY: Float) = camX + 0.5f >= x && camX + 0.5f <= x + w && camY + 0.5f >= y && camY + 0.5f <= y + h

    @Suppress("unused")
    private fun clampInside(v: Float, lo: Float, hi: Float) = max(lo, min(hi, v))
}
