package dev.thermalview

import android.graphics.Paint
import android.graphics.Typeface
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay

/** One readout as the native side reports it (camera pixels). */
private data class Spot(val tempC: Float, val x: Float, val y: Float, val flags: Int, val high: Boolean) {
    val valid get() = flags and 1 != 0
    val overRange get() = flags and 2 != 0
    fun label(): String = when {
        overRange -> if (high) "> max" else "> 120 °C"  // the high range's ceiling is measured in M2
        valid && !tempC.isNaN() -> "%.1f °C".format(tempC)
        else -> "--"
    }
}

private fun parse(a: FloatArray): List<Spot>? =
    if (a.size < 13) null else (0 until 3).map { i ->
        Spot(a[4 * i], a[4 * i + 1], a[4 * i + 2], a[4 * i + 3].toInt(), high = a[12] > 0.5f)
    }

/**
 * Low / high / center readouts over the image (M2; M6 refines the UI). Marker positions follow
 * the renderer's 4:3 view (renderer.cpp draw()): centered, viewWidthPx wide when that's smaller than
 * the largest fit (the view-size presets), image row 0 at the top, no mirroring.
 */
@Composable
fun ReadoutOverlay(active: Boolean, viewWidthPx: Int = 0) {
    var spots by remember { mutableStateOf<List<Spot>?>(null) }
    LaunchedEffect(active) {
        while (active) {
            spots = parse(NativeBridge.readouts())
            delay(100)
        }
    }
    val s = spots
    if (!active || s == null) return
    val (high, low, center) = s
    Box(Modifier.fillMaxSize()) {
        Canvas(Modifier.fillMaxSize()) {
            val w = size.width.toInt()
            val h = size.height.toInt()
            var vw = w
            var vh = h
            if (w.toLong() * 3 > h.toLong() * 4) vw = h * 4 / 3 else vh = w * 3 / 4
            if (viewWidthPx in 1 until vw) {
                vw = viewWidthPx
                vh = viewWidthPx * 3 / 4
            }
            val ox = (w - vw) / 2f
            val oy = (h - vh) / 2f
            fun map(x: Float, y: Float) = Offset(ox + (x + 0.5f) * vw / 256f, oy + (y + 0.5f) * vh / 192f)
            val px = vw / 256f  // one camera pixel on screen
            if (!high.x.isNaN()) marker(map(high.x, high.y), Color(0xFFFF3B30), up = true, px, high.label())
            if (!low.x.isNaN()) marker(map(low.x, low.y), Color(0xFF30A0FF), up = false, px, low.label())
            crosshair(map(center.x, center.y), px, center.label())
        }
        Row(
            Modifier.align(Alignment.TopEnd).padding(8.dp).background(Color(0x99000000)).padding(horizontal = 10.dp, vertical = 6.dp),
        ) {
            val badge = if (high.high) "HIGH RANGE   " else ""
            Text("$badge▲ ${high.label()}   ▼ ${low.label()}   ✛ ${center.label()}", color = Color.White,
                fontFamily = FontFamily.Monospace, fontSize = 16.sp)
        }
    }
}

private val labelPaint = Paint().apply {
    isAntiAlias = true
    textSize = 34f
    typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
    color = Color.White.toArgb()
    setShadowLayer(4f, 0f, 0f, Color.Black.toArgb())
}

private fun DrawScope.marker(at: Offset, color: Color, up: Boolean, px: Float, label: String) {
    val r = maxOf(10f, 2.5f * px)
    val tri = Path().apply {
        if (up) {
            moveTo(at.x, at.y - r); lineTo(at.x - r, at.y + r * 0.7f); lineTo(at.x + r, at.y + r * 0.7f)
        } else {
            moveTo(at.x, at.y + r); lineTo(at.x - r, at.y - r * 0.7f); lineTo(at.x + r, at.y - r * 0.7f)
        }
        close()
    }
    drawPath(tri, color)
    drawPath(tri, Color.Black, style = Stroke(width = 2f))
    drawContext.canvas.nativeCanvas.drawText(label, at.x + r + 6f, at.y + 12f, labelPaint)
}

private fun DrawScope.crosshair(at: Offset, px: Float, label: String) {
    val r = maxOf(14f, 3f * px)
    for (c in listOf(Color.Black to 5f, Color.White to 2f)) {
        drawLine(c.first, Offset(at.x - r, at.y), Offset(at.x + r, at.y), strokeWidth = c.second)
        drawLine(c.first, Offset(at.x, at.y - r), Offset(at.x, at.y + r), strokeWidth = c.second)
    }
    drawContext.canvas.nativeCanvas.drawText(label, at.x + r + 6f, at.y - 8f, labelPaint)
}
