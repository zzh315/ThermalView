package dev.thermalview

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.ui.graphics.lerp
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.drag
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import kotlin.math.roundToInt
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.abs
import kotlinx.coroutines.delay

// The side bars (owner, 2026-09-26: "there are empty black spaces on both side of the screen, utilise
// them for quick action menus and items that I can toggle and activate"): the controls live in the
// margins beside the 4:3 image, so nothing covers it. Each bar's column hugs the screen's outer edge,
// so the buttons stay under the thumbs and don't move when the view size changes.

/** The widest a bar's column gets (the bars themselves are ~107 dp at Full, wider at smaller views). */
internal val BarMaxWidth: Dp = 150.dp

/** True in a narrow bar (Full view): values get a smaller size so they fit. */
internal val LocalCompact = compositionLocalOf { false }

/** A bar's column: at most [BarMaxWidth] wide, with compact text when it's narrow. */
@Composable
private fun BarColumn(modifier: Modifier, content: @Composable ColumnScope.() -> Unit) {
    BoxWithConstraints(modifier) {
        CompositionLocalProvider(LocalCompact provides (maxWidth < 120.dp)) {
            Column(Modifier.fillMaxHeight(), verticalArrangement = Arrangement.spacedBy(8.dp), content = content)
        }
    }
}

/**
 * The left bar: how the image looks. Palette, noise reduction, texture and view size, each a tap to
 * change; the frame rate (debug), and while zoomed in, a way back to 1x.
 */
@Composable
fun LeftBar(
    modifier: Modifier,
    options: DebugOptions,
    onOptions: (DebugOptions) -> Unit,
    paletteColors: (Int) -> List<Color>,
    stats: (@Composable () -> Unit)?,
    zoom: Float,
    onResetZoom: () -> Unit,
    boxOn: Boolean,
    onBox: (Boolean) -> Unit,
    replay: String,
    onStopReplay: () -> Unit,
) {
    BarColumn(modifier.fillMaxHeight().padding(start = 8.dp, end = 6.dp, top = 8.dp, bottom = 8.dp).widthIn(max = BarMaxWidth)) {
        stats?.invoke()
        Tile(onClick = { onOptions(options.copy(palette = if (options.palette == 1) 2 else 1)) }) {
            Caption("Palette")
            Value(PALETTE_NAMES.getOrElse(options.palette) { "?" })
            Spacer(Modifier.height(5.dp))
            Box(
                Modifier.fillMaxWidth().height(6.dp)
                    .background(Brush.horizontalGradient(paletteColors(options.palette).ifEmpty { listOf(Color.Black, Color.White) }), RoundedCornerShape(3.dp)),
            )
        }
        LevelTile("Noise", MainActivity.nrLevelOf(options)) { onOptions(MainActivity.withNrLevel(options, it)) }
        LevelTile("Texture", MainActivity.textureLevelOf(options)) { onOptions(MainActivity.withTextureLevel(options, it)) }
        Tile(onClick = { onOptions(options.copy(viewSize = (options.viewSize + 1) % MainActivity.VIEW_WIDTHS.size)) }) {
            Caption("View")
            Row(verticalAlignment = Alignment.CenterVertically) {
                Value(MainActivity.VIEW_NAMES.getOrElse(options.viewSize) { "?" }, Modifier.weight(1f))
                ViewGlyph(options.viewSize)
            }
        }
        // The box (PLAN M6): the readouts and the colors come from inside it; drag it, its edges or corners.
        Tile(onClick = { onBox(!boxOn) }, color = if (boxOn) Ui.TileActive else Ui.Tile) {
            Caption("Box")
            Row(verticalAlignment = Alignment.CenterVertically) {
                Value(if (boxOn) "On" else "Off", Modifier.weight(1f))
                BoxGlyph(boxOn)
            }
        }
        Spacer(Modifier.weight(1f))
        if (zoom > 1.01f) {
            Tile(onClick = onResetZoom, color = Ui.TileActive) {
                Caption("Zoom")
                Value("%.1f×".format(zoom))
                Note("tap for 1×")
            }
        }
        if (replay.isNotEmpty()) {
            Tile(onClick = onStopReplay, color = Ui.TileAlert) {
                Caption("Replay", Color(0xFFFFB4B4))
                Text(replay, color = Ui.Text, fontSize = 12.sp, maxLines = 2, overflow = TextOverflow.Ellipsis, lineHeight = 14.sp)
                Note("tap to stop")
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
 * The right bar: measuring and acting. The three readouts, the range (Auto, or locked: M6) and the
 * scale bar with the temperatures at its ends, Recalibrate, Capture (debug) and Settings.
 */
@Composable
fun RightBar(
    modifier: Modifier,
    readings: Readings?,
    palette: IntArray,      // the palette's colors, cold to hot (the scale bar)
    marksLocked: Boolean,   // the palette marks pixels beyond a locked range (the rainbow's grey)
    live: Boolean,
    status: Status,
    panelOpen: Boolean,
    onPanel: () -> Unit,
) {
    BarColumn(modifier.fillMaxHeight().padding(start = 6.dp, end = 8.dp, top = 8.dp, bottom = 8.dp).widthIn(max = BarMaxWidth)) {
        Tile {
            ReadoutRow(Ui.Hot, Ui.HotText, readings?.let { it.text(it.high, unit = false) })
            ReadoutRow(Ui.Center, Color.White, readings?.let { it.text(it.center, unit = false) })
            ReadoutRow(Ui.Cold, Ui.ColdText, readings?.let { it.text(it.low, unit = false) })
        }
        val locked = readings?.locked == true
        Tile(onClick = { NativeBridge.setRangeLock(!locked) }, enabled = live, color = if (locked) Ui.TileActive else Ui.Tile) {
            Caption("Range")
            Row(verticalAlignment = Alignment.CenterVertically) {
                Value(if (locked) "Locked" else "Auto", Modifier.weight(1f))
                LockGlyph(locked)
            }
        }
        ScaleBar(readings, palette, marksLocked, Modifier.weight(1f))
        CalibrateTile(live)
        if (BuildConfig.DEBUG) CaptureTile(status, live)
        Tile(onClick = onPanel, color = if (panelOpen) Ui.TileActive else Ui.Tile) {
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
        Text(text ?: "--", color = textColor, fontSize = 18.sp, fontWeight = FontWeight.SemiBold, style = Tabular, maxLines = 1)
    }
}

/**
 * The scale bar (PLAN M6; owner, 2026-09-26: "for xtherm the 2 ranges can be dragged and altered
 * separately"): a temperature axis spanning the scene and the color range, with a handle at each end
 * of the range. Between the handles, the palette as the image uses it (the mapping's curve); beyond
 * them, what the image shows there (the rainbow's grey marks when locked, else the palette's ends).
 * Dragging a handle moves that end alone, locking the range first if it was Auto; the axis holds
 * still while a finger is down. Ticks mark the high, center and low readouts at their temperatures.
 */
@Composable
private fun ScaleBar(readings: Readings?, palette: IntArray, marksLocked: Boolean, modifier: Modifier) {
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
    val labelStyle = TextStyle(fontSize = 13.sp, fontFeatureSettings = "tnum", color = if (locked) Ui.Accent else Ui.Text)
    Box(
        modifier.fillMaxWidth().pointerInput(Unit) {
            awaitEachGesture {
                val down = awaitFirstDown()
                val (l0, h0, ax) = current
                if (l0.isNaN() || h0.isNaN() || ax.first.isNaN()) return@awaitEachGesture
                val barTop = 10.dp.toPx()
                val barH = size.height - 2 * barTop
                fun yOf(t: Float) = barTop + (ax.second - t) / (ax.second - ax.first) * barH
                fun tOf(y: Float) = ax.second - (y - barTop) / barH * (ax.second - ax.first)
                // The nearer handle takes the drag.
                val hot = abs(down.position.y - yOf(h0)) <= abs(down.position.y - yOf(l0))
                frozenAxis = ax
                if (!locked) NativeBridge.setRangeLock(true)
                val minSpan = minOf(0.5f, h0 - l0)
                try {
                    drag(down.id) { change ->
                        change.consume()
                        val t = tOf(change.position.y).coerceIn(ax.first, ax.second)
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
            val barW = 16.dp.toPx()
            val barX = size.width - barW - 10.dp.toPx()
            val barTop = 10.dp.toPx()
            val barH = size.height - 2 * barTop
            fun yOf(t: Float) = barTop + (ax.second - t) / (ax.second - ax.first) * barH
            fun color(i: Float) = Color(palette[(i.coerceIn(0f, 1f) * (palette.size - 1)).roundToInt()])
            val curve = readings?.curve ?: FloatArray(0)
            val above = if (marksLocked && locked) Color(0xFFC8C8C8) else color(0.97f)  // (palettes/rainbow_*.json)
            val below = if (marksLocked && locked) Color(0xFF555555) else color(0.03f)
            // The bar, row by row: the mapping between the ends, the marks or the palette's ends beyond.
            val step = 2f
            var y = barTop
            while (y < barTop + barH) {
                val t = ax.second - (y + step / 2 - barTop) / barH * (ax.second - ax.first)
                val c = when {
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
                drawRect(c, Offset(barX, y), Size(barW, minOf(step, barTop + barH - y)))
                y += step
            }
            drawRect(Color(0x80000000), Offset(barX, barTop), Size(barW, barH), style = Stroke(1.dp.toPx()))
            // The readouts at their temperatures.
            if (readings != null) {
                for ((s, c) in listOf(readings.low to Ui.Cold, readings.center to Ui.Center, readings.high to Ui.Hot)) {
                    val t = when {
                        s.overRange -> ax.second
                        s.valid -> s.tempC
                        else -> continue
                    }
                    val ty = yOf(t.coerceIn(ax.first, ax.second))
                    val o = 3.dp.toPx()
                    drawLine(Color(0xC0000000), Offset(barX - o, ty), Offset(barX + barW + o, ty), 4.dp.toPx())
                    drawLine(c, Offset(barX - o + 1.dp.toPx(), ty), Offset(barX + barW + o - 1.dp.toPx(), ty), 2.dp.toPx())
                }
            }
            // The handles, and their temperatures beside them (pushed apart when they'd touch).
            val yHi = yOf(hi)
            val yLo = yOf(lo)
            val hiText = measurer.measure(if (readings?.scaleHiOver == true && ours == null) "> 120°" else "%.1f°".format(hi), labelStyle)
            val loText = measurer.measure("%.1f°".format(lo), labelStyle)
            val gap = 2.dp.toPx()
            var hiLabelY = yHi - hiText.size.height / 2f
            var loLabelY = yLo - loText.size.height / 2f
            val overlap = hiLabelY + hiText.size.height + gap - loLabelY
            if (overlap > 0) {
                hiLabelY -= overlap / 2
                loLabelY += overlap / 2
            }
            hiLabelY = hiLabelY.coerceIn(0f, size.height - hiText.size.height)
            loLabelY = loLabelY.coerceIn(0f, size.height - loText.size.height)
            for ((hy, text, ly) in listOf(Triple(yHi, hiText, hiLabelY), Triple(yLo, loText, loLabelY))) {
                val knob = Size(barW + 10.dp.toPx(), 8.dp.toPx())
                val tl = Offset(barX - 5.dp.toPx(), hy - knob.height / 2)
                drawRoundRect(Color(0xE0000000), tl - Offset(1.dp.toPx(), 1.dp.toPx()), Size(knob.width + 2.dp.toPx(), knob.height + 2.dp.toPx()),
                    CornerRadius(4.dp.toPx()))
                drawRoundRect(if (locked) Ui.Accent else Color.White, tl, knob, CornerRadius(3.dp.toPx()))
                drawText(text, topLeft = Offset(barX - 8.dp.toPx() - text.size.width, ly))
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
private fun CalibrateTile(live: Boolean) {
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
    Tile(onClick = {
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

private val CAPTURE_MODES = listOf(
    CaptureMode("Mark", "Mark", "log only", "Marks the field log (for test scripts)"),
    CaptureMode("Record", "Record", "200 frames", "Records 200 raw frames now"),
    CaptureMode("Recalibrate + record", "Recal", "shutter, then 200", "Waits for the shutter to cool, recalibrates, then records 200 frames"),
)

/**
 * The Ready button, grown up (owner, 2026-09-26: "more versatile and powerful and more intuitive"):
 * Capture runs the chosen mode and shows its progress on the button itself; ▾ (or a long press)
 * picks the mode. Every mode logs "owner mark: …" first, so test scripts can wait for it.
 */
@Composable
private fun CaptureTile(status: Status, live: Boolean) {
    var mode by rememberSaveable { mutableIntStateOf(1) }
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
    val busy = status.dumpTotal > 0 || writing || status.capture.isNotEmpty()
    val m = CAPTURE_MODES[mode]
    val run = {
        if (live && !busy) {
            count += 1
            val label = "${m.caption.lowercase()} #$count"
            flash = when (mode) {
                0 -> { NativeBridge.mark(label); "marked #$count" }
                1 -> {
                    NativeBridge.mark(label)
                    NativeBridge.startDump(200).let { if (it.startsWith("dump_")) "" else it }
                }
                else -> NativeBridge.readyCapture(label, false).let { if (it == "capture started") "" else it }
            }
        }
    }
    Box {
        Tile(onClick = run, onLongClick = { menu = true }, enabled = live, color = if (busy) Ui.TileBusy else Ui.Tile) {
            // The mode, and the way to change it: the whole caption row (and a long press anywhere).
            Row(
                Modifier.fillMaxWidth().combinedClickableCompat { menu = true }
                    .padding(vertical = 2.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Caption(m.caption, Ui.Accent, Modifier.weight(1f))
                Chevron(Ui.Accent)
            }
            Value("Capture")
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
        DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
            CAPTURE_MODES.forEachIndexed { i, cm ->
                DropdownMenuItem(
                    text = {
                        Column(Modifier.widthIn(max = 280.dp)) {
                            Text(cm.title + if (i == mode) "  ✓" else "", fontSize = 15.sp)
                            Text(cm.description, fontSize = 12.sp, color = Ui.Subtle)
                        }
                    },
                    onClick = {
                        mode = i
                        menu = false
                    },
                )
            }
        }
    }
}

/** Noise or texture: Off / Low / High, a tap for the next (a custom setting from the debug pages goes to Low). */
@Composable
private fun LevelTile(title: String, level: Int?, onLevel: (Int) -> Unit) {
    Tile(onClick = { onLevel(if (level == null) 1 else (level + 1) % MainActivity.LEVELS.size) }) {
        Caption(title)
        Row(verticalAlignment = Alignment.CenterVertically) {
            Value(level?.let { MainActivity.LEVELS[it] } ?: "Custom", Modifier.weight(1f))
            if (level != null) {
                Row(horizontalArrangement = Arrangement.spacedBy(3.dp), verticalAlignment = Alignment.Bottom) {
                    for (i in 1..2) {
                        Box(
                            Modifier.size(width = 5.dp, height = (6 + 5 * i).dp)
                                .background(if (level >= i) Ui.Accent else Ui.Faint, RoundedCornerShape(2.dp)),
                        )
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
            Modifier.fillMaxWidth().background(if (pressed) lerp(color, Color.White, 0.10f) else color, shape)
                .then(
                    if (onClick != null) {
                        Modifier.combinedClickable(
                            interactionSource = interaction, indication = null, enabled = enabled,
                            onClick = onClick, onLongClick = onLongClick,
                        )
                    } else Modifier,
                )
                .padding(horizontal = 9.dp, vertical = 7.dp),
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
