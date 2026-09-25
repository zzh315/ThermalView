package dev.thermalview

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
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
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
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
                Modifier.fillMaxWidth().height(6.dp).clip(RoundedCornerShape(3.dp))
                    .background(Brush.horizontalGradient(paletteColors(options.palette).ifEmpty { listOf(Color.Black, Color.White) })),
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
 * The right bar: measuring and acting. The three readouts, the scale bar with the temperatures at its
 * ends, Recalibrate, Capture (debug) and Settings.
 */
@Composable
fun RightBar(
    modifier: Modifier,
    readings: Readings?,
    scaleColors: List<Color>,
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
        ScaleBar(readings, scaleColors, Modifier.weight(1f))
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
 * The palette with the temperatures at the ends of the mapping (PLAN M5: endpoints only, since
 * the curve isn't linear). The colors run from the mapping's top (hot) to its bottom.
 */
@Composable
private fun ScaleBar(readings: Readings?, colors: List<Color>, modifier: Modifier) {
    val top = when {
        readings == null -> ""
        readings.scaleHiOver -> "> 120°"
        readings.scaleHiC.isNaN() -> "--"
        else -> "%.1f°".format(readings.scaleHiC)
    }
    val bottom = when {
        readings == null -> ""
        readings.scaleLoC.isNaN() -> "--"
        else -> "%.1f°".format(readings.scaleLoC)
    }
    Column(modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally) {
        Text(top, color = Ui.Text, fontSize = 14.sp, style = Tabular, maxLines = 1)
        Spacer(Modifier.height(4.dp))
        Box(
            Modifier.weight(1f).width(16.dp).clip(RoundedCornerShape(8.dp))
                .background(Brush.verticalGradient(colors.ifEmpty { listOf(Color.White, Color.Black) })),
        )
        Spacer(Modifier.height(4.dp))
        Text(bottom, color = Ui.Text, fontSize = 14.sp, style = Tabular, maxLines = 1)
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

private class CaptureMode(val title: String, val note: String, val description: String)

private val CAPTURE_MODES = listOf(
    CaptureMode("Mark", "log only", "Marks the field log (for test scripts)"),
    CaptureMode("Record", "200 frames", "Records 200 raw frames now"),
    CaptureMode("Calibrated", "shutter, then 200", "Waits for the shutter to cool, recalibrates, then records 200 frames"),
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
    LaunchedEffect(status.dumpTotal, status.dump) {
        if (wasRecording && status.dumpTotal == 0) flash = if (status.dump.startsWith("saved")) "saved" else status.dump
        wasRecording = status.dumpTotal > 0
    }
    LaunchedEffect(flash) {
        if (flash.isNotEmpty()) {
            delay(3000)
            flash = ""
        }
    }
    val busy = status.dumpTotal > 0 || status.capture.isNotEmpty()
    val m = CAPTURE_MODES[mode]
    val run = {
        if (live && !busy) {
            count += 1
            val label = "${m.title.lowercase()} #$count"
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
                Modifier.fillMaxWidth().clip(RoundedCornerShape(6.dp)).combinedClickableCompat { menu = true }
                    .padding(vertical = 2.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Caption(m.title, Ui.Accent, Modifier.weight(1f))
                Chevron(Ui.Accent)
            }
            Value("Capture")
            val progress = when {
                status.dumpTotal > 0 -> "${status.dumpDone} / ${status.dumpTotal}"
                status.capture.isNotEmpty() -> status.capture
                flash.isNotEmpty() -> flash
                else -> m.note
            }
            Note(progress, if (busy) Ui.Warning else Ui.Subtle)
            if (status.dumpTotal > 0) {
                Spacer(Modifier.height(4.dp))
                Box(Modifier.fillMaxWidth().height(3.dp).clip(RoundedCornerShape(2.dp)).background(Ui.Faint)) {
                    Box(
                        Modifier.fillMaxWidth(status.dumpDone.toFloat() / status.dumpTotal).height(3.dp)
                            .background(Ui.Warning),
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
                            Modifier.size(width = 5.dp, height = (6 + 5 * i).dp).clip(RoundedCornerShape(2.dp))
                                .background(if (level >= i) Ui.Accent else Ui.Faint),
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
    Column(
        Modifier.fillMaxWidth().clip(shape).background(color)
            .then(
                if (onClick != null) Modifier.combinedClickable(enabled = enabled, onClick = onClick, onLongClick = onLongClick)
                else Modifier,
            )
            .alpha(if (enabled) 1f else 0.5f)
            .padding(horizontal = 9.dp, vertical = 7.dp),
        content = content,
    )
}

@OptIn(ExperimentalFoundationApi::class)
private fun Modifier.combinedClickableCompat(onClick: () -> Unit) = combinedClickable(onClick = onClick)

@Composable
internal fun Caption(text: String, modifier: Modifier = Modifier) = Caption(text, Ui.Subtle, modifier)

@Composable
internal fun Caption(text: String, color: Color, modifier: Modifier = Modifier) {
    Text(
        text.uppercase(), color = color, fontSize = 10.sp, fontWeight = FontWeight.SemiBold,
        letterSpacing = 0.8.sp, maxLines = 1, modifier = modifier,
    )
}

@Composable
internal fun Value(text: String, modifier: Modifier = Modifier, size: TextUnit = if (LocalCompact.current) 14.sp else 16.sp) {
    Text(text, color = Ui.Text, fontSize = size, fontWeight = FontWeight.Medium, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = modifier)
}

@Composable
internal fun Note(text: String, color: Color = Ui.Subtle) {
    Text(text, color = color, fontSize = 11.sp, maxLines = 2, overflow = TextOverflow.Ellipsis, lineHeight = 13.sp, style = Tabular)
}
