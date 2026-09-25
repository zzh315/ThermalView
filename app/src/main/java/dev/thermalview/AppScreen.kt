package dev.thermalview

import android.view.SurfaceHolder
import android.view.SurfaceView
import android.widget.Toast
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import java.io.File
import kotlinx.coroutines.delay

private object SurfaceCallbacks : SurfaceHolder.Callback {
    override fun surfaceCreated(holder: SurfaceHolder) = NativeBridge.setSurface(holder.surface)
    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) = Unit
    override fun surfaceDestroyed(holder: SurfaceHolder) = NativeBridge.setSurface(null)
}

// The panel's colours: dark, so it sits over the image without glare.
private val PanelColor = Color(0xF2151A21)
private val SubtleText = Color(0xFF9AA4B2)
private val Accent = Color(0xFF7FB2FF)
private val Warning = Color(0xFFFFB74D)
private val PanelScheme = darkColorScheme(primary = Accent, secondaryContainer = Color(0xFF2C3E57), onSecondaryContainer = Color.White)

@Composable
fun AppScreen(message: String, dumpsDir: String, options: DebugOptions, onOptions: (DebugOptions) -> Unit) {
    var status by remember { mutableStateOf(Status()) }
    var overlay by remember { mutableStateOf("") }
    // What's on screen (debug builds): a one-line summary by default; the full diagnostics only when
    // asked for (Settings › Diagnostics). adb reads the full text either way (--ez logOverlay true).
    var showStats by rememberSaveable { mutableStateOf(true) }
    var showDetails by rememberSaveable { mutableStateOf(false) }
    // Frames dropped in the last 10 s, from the stream's running count sampled every 250 ms.
    val droppedHistory = remember { ArrayDeque<Pair<Long, Long>>() }
    var recentDrops by remember { mutableStateOf(0L) }

    LaunchedEffect(Unit) {
        while (true) {
            status = Status.parse(NativeBridge.status())
            val now = System.currentTimeMillis()
            droppedHistory.addLast(now to status.dropped)
            while (droppedHistory.size > 1 && now - droppedHistory.first().first > 10_000) droppedHistory.removeFirst()
            recentDrops = (status.dropped - droppedHistory.first().second).coerceAtLeast(0)
            if (showDetails) overlay = NativeBridge.overlayText()
            delay(250)
        }
    }
    val view = LocalView.current
    DisposableEffect(status.streaming) {
        view.keepScreenOn = status.streaming
        onDispose { view.keepScreenOn = false }
    }

    // Zoom and pan (PLAN M6): pinch zooms 1x-8x around the fingers, a drag pans, a double tap goes
    // back to 1x. Display-only: the pipeline still processes the whole frame, but the auto range and
    // the readouts measure what's shown.
    var zoom by rememberSaveable { mutableStateOf(1f) }
    var zoomCx by rememberSaveable { mutableStateOf(CamRect.FRAME_W / 2) }
    var zoomCy by rememberSaveable { mutableStateOf(CamRect.FRAME_H / 2) }
    val camRect = CamRect.of(zoom, zoomCx, zoomCy)
    LaunchedEffect(camRect) { NativeBridge.setViewRect(camRect.x, camRect.y, camRect.w, camRect.h) }
    val viewWidthPx = MainActivity.VIEW_WIDTHS.getOrElse(options.viewSize) { 0 }

    Box(Modifier.fillMaxSize().background(Color.Black)) {
        AndroidView(
            factory = { ctx -> SurfaceView(ctx).apply { holder.addCallback(SurfaceCallbacks) } },
            modifier = Modifier.fillMaxSize(),
        )
        Box(
            Modifier.fillMaxSize()
                .pointerInput(viewWidthPx) {
                    detectTransformGestures { centroid, pan, gestureZoom, _ ->
                        val box = viewBox(size.width, size.height, viewWidthPx)
                        val r = CamRect.of(zoom, zoomCx, zoomCy)
                            .transformed(box, centroid.x, centroid.y, pan.x, pan.y, gestureZoom)
                        zoom = r.zoom
                        zoomCx = r.cx
                        zoomCy = r.cy
                    }
                }
                .pointerInput(Unit) {
                    detectTapGestures(onDoubleTap = {
                        zoom = 1f
                        zoomCx = CamRect.FRAME_W / 2
                        zoomCy = CamRect.FRAME_H / 2
                    })
                },
        )
        ReadoutOverlay(
            active = status.streaming || status.replay.isNotEmpty(),
            viewWidthPx = viewWidthPx,
            rect = camRect,
        )
        if (zoom > 1.01f) {
            Text(
                "%.1fx".format(zoom),
                color = Color.White,
                fontSize = 16.sp,
                modifier = Modifier.align(Alignment.TopCenter).padding(8.dp).background(Color(0x99000000))
                    .padding(horizontal = 10.dp, vertical = 4.dp),
            )
        }
        // A replay runs without the camera, so the "plug in" prompt doesn't apply then.
        val banner = status.banner.ifEmpty { if (status.replay.isNotEmpty()) "" else message }
        if (banner.isNotEmpty()) {
            Text(
                banner,
                color = Color.White,
                modifier = Modifier.align(Alignment.Center).background(Color(0xCC000000)).padding(16.dp),
            )
        }
        if (BuildConfig.DEBUG) {
            Column(Modifier.align(Alignment.TopStart).padding(8.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                if (showStats && (status.streaming || status.replay.isNotEmpty())) StatsChip(status, recentDrops)
                if (showDetails && overlay.isNotEmpty()) {
                    Text(
                        overlay,
                        color = Color.White,
                        fontFamily = FontFamily.Monospace,
                        fontSize = 11.sp,
                        lineHeight = 14.sp,
                        modifier = Modifier.background(Color(0x99000000), RoundedCornerShape(6.dp)).padding(6.dp),
                    )
                }
            }
            if (status.replay.isNotEmpty()) {
                Text(
                    "REPLAY  ${status.replay}",
                    color = Color.White,
                    fontFamily = FontFamily.Monospace,
                    fontSize = 12.sp,
                    modifier = Modifier.align(Alignment.BottomStart).padding(8.dp)
                        .background(Color(0xCCB00020), RoundedCornerShape(6.dp)).padding(horizontal = 8.dp, vertical = 4.dp),
                )
            }
            DebugPanel(
                modifier = Modifier.align(Alignment.BottomEnd),
                dumpsDir = dumpsDir,
                showStats = showStats,
                onShowStats = { showStats = it },
                showDetails = showDetails,
                onShowDetails = { showDetails = it },
                options = options,
                onOptions = onOptions,
            )
        }
    }
}

/**
 * The one-line summary: frame rate and lag, with the 20 ms budget's p95 when it's broken and the
 * frames dropped in the last 10 s when there were any (both in amber).
 */
@Composable
private fun StatsChip(status: Status, recentDrops: Long) {
    val overBudget = status.lagP95Ms > 20f
    Row(
        Modifier.background(Color(0xB0000000), RoundedCornerShape(8.dp)).padding(horizontal = 10.dp, vertical = 5.dp),
        horizontalArrangement = Arrangement.spacedBy(10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text("%.0f fps".format(status.fps), color = Color.White, fontSize = 13.sp)
        Text(
            "lag %.0f ms".format(status.lagMs) + if (overBudget) " (spikes %.0f)".format(status.lagP95Ms) else "",
            color = if (overBudget) Warning else Color.White,
            fontSize = 13.sp,
        )
        if (recentDrops > 0) {
            Text("$recentDrops frame" + (if (recentDrops == 1L) "" else "s") + " dropped", color = Warning, fontSize = 13.sp)
        }
    }
}

private enum class Page(val title: String) {
    Main("Settings"),
    Image("Image processing"),
    Camera("Camera & range"),
    Recording("Recording & replay"),
    Diagnostics("Diagnostics"),
}

@Composable
private fun DebugPanel(
    modifier: Modifier,
    dumpsDir: String,
    showStats: Boolean,
    onShowStats: (Boolean) -> Unit,
    showDetails: Boolean,
    onShowDetails: (Boolean) -> Unit,
    options: DebugOptions,
    onOptions: (DebugOptions) -> Unit,
) {
    val context = LocalContext.current
    val toast = { text: String -> Toast.makeText(context, text, Toast.LENGTH_SHORT).show() }
    var expanded by rememberSaveable { mutableStateOf(false) }
    var page by rememberSaveable { mutableStateOf(Page.Main) }
    var choosingReplay by remember { mutableStateOf(false) }

    // The back gesture goes up a level, then closes the panel.
    BackHandler(enabled = expanded) { if (page != Page.Main) page = Page.Main else expanded = false }
    MaterialTheme(colorScheme = PanelScheme) {
        Column(modifier.padding(12.dp), horizontalAlignment = Alignment.End) {
            if (expanded) {
                // Takes only the height left above the bottom row, and scrolls, however long a page grows.
                Surface(
                    color = PanelColor,
                    shape = RoundedCornerShape(16.dp),
                    modifier = Modifier.width(460.dp).weight(1f, fill = false),
                ) {
                    Column(Modifier.padding(horizontal = 16.dp, vertical = 12.dp)) {
                        PanelHeader(page, onBack = { page = Page.Main })
                        Column(Modifier.verticalScroll(rememberScrollState())) {
                            when (page) {
                                Page.Main -> MainPage(options, onOptions, showStats, onShowStats, toast) { page = it }
                                Page.Image -> ImagePage(options, onOptions)
                                Page.Camera -> CameraPage(options, onOptions, toast)
                                Page.Recording -> RecordingPage(options, onOptions, toast) { choosingReplay = true }
                                Page.Diagnostics -> DiagnosticsPage(options, onOptions, showDetails, onShowDetails, toast)
                            }
                            Spacer(Modifier.height(4.dp))
                        }
                    }
                }
                Spacer(Modifier.height(8.dp))
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                // Lets the owner signal a test step from the tablet; test scripts wait for this log line.
                var marks by rememberSaveable { mutableStateOf(0) }
                FilledTonalButton(onClick = {
                    marks += 1
                    if (options.captureOnReady) {
                        toast("Ready #$marks: " + NativeBridge.readyCapture("ready #$marks", options.rangePairOnReady))
                    } else {
                        NativeBridge.mark("ready #$marks")
                        toast("Ready #$marks sent")
                    }
                }) { Text("Ready") }
                FilledTonalButton(onClick = {
                    expanded = !expanded
                    if (expanded) page = Page.Main
                }) { Text(if (expanded) "Close" else "Settings") }
            }
        }
    }

    if (choosingReplay) {
        val dumps = File(dumpsDir).listFiles { f -> f.name.endsWith(".raw") }
            ?.map { it.path.removeSuffix(".raw") }?.sortedDescending().orEmpty()
        AlertDialog(
            onDismissRequest = { choosingReplay = false },
            confirmButton = { TextButton(onClick = { choosingReplay = false }) { Text("Cancel") } },
            title = { Text("Replay a recording") },
            text = {
                if (dumps.isEmpty()) {
                    Text("No recordings in $dumpsDir")
                } else {
                    LazyColumn {
                        items(dumps) { base ->
                            TextButton(onClick = {
                                choosingReplay = false
                                val error = NativeBridge.startReplay(base)
                                toast(if (error.isEmpty()) "Replaying ${File(base).name}" else "Replay: $error")
                            }) { Text(File(base).name) }
                        }
                    }
                }
            },
        )
    }
}

@Composable
private fun PanelHeader(page: Page, onBack: () -> Unit) {
    Row(Modifier.fillMaxWidth().padding(bottom = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        if (page != Page.Main) {
            TextButton(onClick = onBack) { Text("‹ Back", fontSize = 15.sp) }
        }
        Text(
            page.title,
            color = Color.White,
            fontSize = 18.sp,
            fontWeight = FontWeight.SemiBold,
            modifier = Modifier.weight(1f).padding(start = if (page == Page.Main) 4.dp else 0.dp, top = 6.dp, bottom = 6.dp),
        )
    }
}

// --- The pages -------------------------------------------------------------------------------------

@Composable
private fun MainPage(
    options: DebugOptions,
    onOptions: (DebugOptions) -> Unit,
    showStats: Boolean,
    onShowStats: (Boolean) -> Unit,
    toast: (String) -> Unit,
    open: (Page) -> Unit,
) {
    Section("Picture")
    Choice(
        "Noise reduction",
        MainActivity.LEVELS,
        MainActivity.nrLevelOf(options),
        note = if (MainActivity.nrLevelOf(options) == null) "custom (set in Image processing)" else null,
    ) { onOptions(MainActivity.withNrLevel(options, it)) }
    Choice(
        "Texture",
        MainActivity.LEVELS,
        MainActivity.textureLevelOf(options),
        note = if (MainActivity.textureLevelOf(options) == null) "custom (set in Image processing)" else null,
    ) { onOptions(MainActivity.withTextureLevel(options, it)) }
    Choice("Palette", listOf("Gray", "White hot", "Rainbow"), options.palette.coerceIn(0, 2)) {
        onOptions(options.copy(palette = it))
    }
    Choice("View size", listOf("Phone", "Tablet", "Full"), options.viewSize.coerceIn(0, 2)) {
        onOptions(options.copy(viewSize = it))
    }
    Section("Camera")
    ActionRow("Recalibrate", "Refreshes the camera's calibration (the shutter clicks)") {
        toast("Recalibrate: " + NativeBridge.sendShutter())
    }
    Section("Screen")
    SwitchRow("Frame rate and lag", "A one-line summary at the top left", showStats, onShowStats)
    Section("More")
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        NavTile("Image processing", "Stages and tuning", Modifier.weight(1f)) { open(Page.Image) }
        NavTile("Camera & range", "Range, lockout, start-up", Modifier.weight(1f)) { open(Page.Camera) }
    }
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        NavTile("Recording & replay", "Record, replay, tests", Modifier.weight(1f)) { open(Page.Recording) }
        NavTile("Diagnostics", "Detailed stats, checks", Modifier.weight(1f)) { open(Page.Diagnostics) }
    }
}

@Composable
private fun ImagePage(options: DebugOptions, onOptions: (DebugOptions) -> Unit) {
    Section("Stages")
    SwitchRow("1  Shutter hold", "Holds the image through calibrations, then crossfades", options.shutterHold) {
        onOptions(options.copy(shutterHold = it))
    }
    SwitchRow("2  Bad pixels", "Replaces the camera's known bad pixels", options.badPixels) { onOptions(options.copy(badPixels = it)) }
    SwitchRow("3  Drift and fixed stripes", "Between calibrations", options.drift) { onOptions(options.copy(drift = it)) }
    SwitchRow("3c Flickering stripes", "The per-frame column and row noise", options.stripes) { onOptions(options.copy(stripes = it)) }
    SwitchRow("4b Noise reduction", null, options.nr) { onOptions(options.copy(nr = it)) }
    SwitchRow("5  Tone mapping", "Automatic contrast", options.tone) { onOptions(options.copy(tone = it)) }
    SwitchRow("6  Texture", "Mid-scale contrast", options.detail) { onOptions(options.copy(detail = it)) }
    Section("Noise reduction (4b)")
    Choice("Filter", listOf("Non-local means", "BM3D (preview)"), options.nrMethod.coerceIn(0, 1)) {
        onOptions(options.copy(nrMethod = it))
    }
    Choice(
        "Strength",
        MainActivity.NR_STRENGTHS.map { "%.1f".format(it) },
        MainActivity.NR_STRENGTHS.indexOf(options.nrStrength).takeIf { it >= 0 },
    ) { onOptions(options.copy(nrStrength = MainActivity.NR_STRENGTHS[it])) }
    Choice(
        "Search window",
        MainActivity.NR_SEARCHES.map { "${2 * it + 1}×${2 * it + 1}" },
        MainActivity.NR_SEARCHES.indexOf(options.nrSearch).takeIf { it >= 0 },
    ) { onOptions(options.copy(nrSearch = MainActivity.NR_SEARCHES[it])) }
    SwitchRow("On the GPU", "Off: the CPU with a 5×5 search", options.gpuNr) { onOptions(options.copy(gpuNr = it)) }
    Section("Texture (6)")
    Choice(
        "Strength",
        MainActivity.TEXTURE_STRENGTHS.map { "×%.1f".format(it) },
        MainActivity.TEXTURE_STRENGTHS.indexOf(options.textureStrength).takeIf { it >= 0 },
    ) { onOptions(options.copy(textureStrength = MainActivity.TEXTURE_STRENGTHS[it])) }
    Section("Display")
    Choice("Upscaling", listOf("Nearest", "B-spline"), options.upscaler.coerceIn(0, 1)) { onOptions(options.copy(upscaler = it)) }
}

@Composable
private fun CameraPage(options: DebugOptions, onOptions: (DebugOptions) -> Unit, toast: (String) -> Unit) {
    Section("Temperature range")
    Row(Modifier.fillMaxWidth().padding(vertical = 6.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        FilledTonalButton(onClick = { toast(NativeBridge.setRange(false)) }) { Text("Normal range") }
        FilledTonalButton(onClick = { toast(NativeBridge.setRange(true)) }) { Text("High range (parked)") }
    }
    SwitchRow("Over-range lockout", "Protects the sensor from very hot scenes", options.lockoutEnabled) {
        onOptions(options.copy(lockoutEnabled = it))
    }
    SwitchRow("Automatic range switching", "Parked with the high range", options.autoRange) { onOptions(options.copy(autoRange = it)) }
    SwitchRow("High-range maths: InfiCam's", "Off: ht301's", options.highMathInfiCam) {
        onOptions(options.copy(highMathInfiCam = it))
    }
    Section("Next start")
    SwitchRow("Skip the start-up 0x8000", null, options.skipStartupShutter) { onOptions(options.copy(skipStartupShutter = it)) }
    SwitchRow("Fallback start order", "0x8004 and 0x8020 before streaming", options.fallbackOrder) {
        onOptions(options.copy(fallbackOrder = it))
    }
}

@Composable
private fun RecordingPage(
    options: DebugOptions,
    onOptions: (DebugOptions) -> Unit,
    toast: (String) -> Unit,
    chooseReplay: () -> Unit,
) {
    Section("Recordings")
    ActionRow("Record 200 frames", "Into the app's dumps folder") { toast("Recording: " + NativeBridge.startDump(200)) }
    ActionRow("Replay a recording…", "Runs the pipeline on it, no camera needed") { chooseReplay() }
    ActionRow("Stop the replay", null) { NativeBridge.stopReplay() }
    Section("Tests")
    SwitchRow("Ready records a capture", "Off: Ready only marks the log", options.captureOnReady) {
        onOptions(options.copy(captureOnReady = it))
    }
    SwitchRow("Ready: both ranges", "The M2 range test", options.rangePairOnReady) { onOptions(options.copy(rangePairOnReady = it)) }
    SwitchRow("Stats CSV", "A row of statistics per frame", options.statsCsv) { onOptions(options.copy(statsCsv = it)) }
}

@Composable
private fun DiagnosticsPage(
    options: DebugOptions,
    onOptions: (DebugOptions) -> Unit,
    showDetails: Boolean,
    onShowDetails: (Boolean) -> Unit,
    toast: (String) -> Unit,
) {
    Section("On screen")
    SwitchRow("Detailed stats", "Everything the pipeline and camera report (large)", showDetails, onShowDetails)
    Section("Performance")
    SwitchRow("Processing on big cores", "Little cores are ~8× slower", options.bigCores) { onOptions(options.copy(bigCores = it)) }
    SwitchRow("Performance hint (ADPF)", "This ROM declines it", options.perfHint) { onOptions(options.copy(perfHint = it)) }
    Section("Checks")
    ActionRow("Check GPU noise reduction", "Against the CPU; the result goes to the field log") {
        NativeBridge.requestNrCheck()
        toast("Check requested: see the field log")
    }
}

// --- Building blocks -------------------------------------------------------------------------------

@Composable
private fun Section(title: String) {
    Text(
        title.uppercase(),
        color = SubtleText,
        fontSize = 11.sp,
        fontWeight = FontWeight.SemiBold,
        letterSpacing = 1.sp,
        modifier = Modifier.padding(top = 10.dp, bottom = 2.dp, start = 4.dp),
    )
}

@Composable
private fun RowText(title: String, note: String?, modifier: Modifier = Modifier) {
    Column(modifier) {
        Text(title, color = Color.White, fontSize = 15.sp)
        if (note != null) Text(note, color = SubtleText, fontSize = 12.sp)
    }
}

@Composable
private fun SwitchRow(title: String, note: String?, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(
        Modifier.fillMaxWidth().clickable { onChange(!checked) }.padding(horizontal = 4.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        RowText(title, note, Modifier.weight(1f))
        Switch(checked = checked, onCheckedChange = onChange)
    }
}

@Composable
private fun ActionRow(title: String, note: String?, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().clickable(onClick = onClick).padding(horizontal = 4.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        RowText(title, note, Modifier.weight(1f))
        Text("Run", color = Accent, fontSize = 14.sp)
    }
}

/** A tile that opens a deeper page. */
@Composable
private fun NavTile(title: String, note: String, modifier: Modifier, onClick: () -> Unit) {
    Surface(color = Color(0x16FFFFFF), shape = RoundedCornerShape(12.dp), modifier = modifier) {
        Row(Modifier.clickable(onClick = onClick).padding(horizontal = 12.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
            RowText(title, note, Modifier.weight(1f))
            Text("›", color = SubtleText, fontSize = 22.sp)
        }
    }
}

/**
 * A labelled row of segmented buttons, one of them selected (none: a custom value, explained by note).
 * Up to three choices sit beside their label; more get the full width below it.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun Choice(title: String, labels: List<String>, selected: Int?, note: String? = null, onSelect: (Int) -> Unit) {
    val buttons = @Composable { modifier: Modifier ->
        SingleChoiceSegmentedButtonRow(modifier) {
            labels.forEachIndexed { i, label ->
                SegmentedButton(
                    selected = i == selected,
                    onClick = { onSelect(i) },
                    shape = SegmentedButtonDefaults.itemShape(index = i, count = labels.size),
                    icon = {},
                ) { Text(label, fontSize = 13.sp, maxLines = 1) }
            }
        }
    }
    if (labels.size <= 3) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 4.dp, vertical = 5.dp), verticalAlignment = Alignment.CenterVertically) {
            RowText(title, note, Modifier.width(140.dp))
            buttons(Modifier.weight(1f))
        }
    } else {
        Column(Modifier.fillMaxWidth().padding(horizontal = 4.dp, vertical = 6.dp)) {
            RowText(title, note)
            Spacer(Modifier.height(4.dp))
            buttons(Modifier.fillMaxWidth())
        }
    }
}
