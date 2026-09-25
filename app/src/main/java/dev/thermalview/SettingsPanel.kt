package dev.thermalview

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.io.File
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import kotlinx.coroutines.Dispatchers

enum class Page(val title: String) {
    Main("Settings"),
    Image("Image processing"),
    Camera("Camera & range"),
    Recording("Recording & replay"),
    Diagnostics("Diagnostics"),
}

/**
 * The settings (debug builds; owner, 2026-09-26: "menu hierachy for advanced options in deeper menus
 * and routine simple options and toggles in shallow hierachy"). The routine controls are on the side
 * bars, so the first page only leads to the deeper ones. An action's result shows under the title
 * for a few seconds (no toasts: they'd sit over the image).
 */
@Composable
fun SettingsPanel(
    modifier: Modifier,
    page: Page,
    onPage: (Page) -> Unit,
    onClose: () -> Unit,
    dumpsDir: String,
    options: DebugOptions,
    onOptions: (DebugOptions) -> Unit,
    showStats: Boolean,
    onShowStats: (Boolean) -> Unit,
    ruler: Boolean = false,
    onRuler: (Boolean) -> Unit = {},
) {
    var notice by remember { mutableStateOf("") }
    var choosingReplay by remember { mutableStateOf(false) }
    val say = { text: String -> notice = text }
    LaunchedEffect(notice) {
        if (notice.isNotEmpty()) {
            delay(4000)
            notice = ""
        }
    }
    // The back gesture goes up a level, then closes the panel.
    BackHandler { if (page != Page.Main) onPage(Page.Main) else onClose() }

    Surface(color = Ui.Panel, shape = RoundedCornerShape(16.dp), modifier = modifier) {
        Column(Modifier.padding(horizontal = 16.dp, vertical = 10.dp)) {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                if (page != Page.Main) TextButton(onClick = { onPage(Page.Main) }) { Text("‹ Back", fontSize = 15.sp) }
                Text(
                    page.title, color = Color.White, fontSize = 18.sp, fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.weight(1f).padding(start = if (page == Page.Main) 4.dp else 0.dp, top = 8.dp, bottom = 8.dp),
                )
            }
            if (notice.isNotEmpty()) {
                Text(
                    notice, color = Ui.Accent, fontSize = 13.sp,
                    modifier = Modifier.fillMaxWidth().padding(bottom = 4.dp)
                        .background(Color(0x1A7FB2FF), RoundedCornerShape(8.dp)).padding(horizontal = 10.dp, vertical = 6.dp),
                )
            }
            Column(Modifier.verticalScroll(rememberScrollState())) {
                when (page) {
                    Page.Main -> MainPage(showStats, onShowStats, onPage)
                    Page.Image -> ImagePage(options, onOptions)
                    Page.Camera -> CameraPage(options, onOptions, say)
                    Page.Recording -> RecordingPage(options, onOptions, say) { choosingReplay = true }
                    Page.Diagnostics -> DiagnosticsPage(options, onOptions, say, ruler, onRuler)
                }
                Spacer(Modifier.height(6.dp))
            }
        }
    }

    if (choosingReplay) {
        var dumps by remember { mutableStateOf<List<DumpEntry>?>(null) }
        LaunchedEffect(dumpsDir) { dumps = withContext(Dispatchers.IO) { listDumps(dumpsDir) } }
        AlertDialog(
            onDismissRequest = { choosingReplay = false },
            confirmButton = { TextButton(onClick = { choosingReplay = false }) { Text("Cancel") } },
            title = { Text("Replay a recording") },
            text = {
                val list = dumps
                when {
                    list == null -> Text("Reading the recordings…")
                    list.isEmpty() -> Text("No recordings in $dumpsDir")
                    else -> LazyColumn(Modifier.heightIn(max = 460.dp)) {
                        items(list) { d ->
                            Row(
                                Modifier.fillMaxWidth().clickable {
                                    choosingReplay = false
                                    val error = NativeBridge.startReplay(d.base)
                                    say(if (error.isEmpty()) "Replaying ${d.title}" else "Replay: $error")
                                }.padding(vertical = 8.dp, horizontal = 4.dp),
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                RowText(d.title, d.note, Modifier.weight(1f))
                            }
                        }
                    }
                }
            },
        )
    }
}

/** A recording in the dumps folder, as the replay list shows it. */
private class DumpEntry(val base: String, val title: String, val note: String)

/**
 * The recordings, newest first: each by its date and time (from its name, dump_YYYYMMDD_HHMMSS),
 * with its length and source from the sidecar JSON.
 */
private fun listDumps(dir: String): List<DumpEntry> {
    val stamp = java.text.SimpleDateFormat("yyyyMMdd_HHmmss", java.util.Locale.US)
    val shown = java.text.SimpleDateFormat("EEE d MMM, HH:mm:ss", java.util.Locale.getDefault())
    return File(dir).listFiles { f -> f.name.endsWith(".raw") }.orEmpty()
        .sortedByDescending { it.name }
        .map { raw ->
            val base = raw.path.removeSuffix(".raw")
            val name = raw.name.removeSuffix(".raw")
            val date = runCatching { stamp.parse(name.removePrefix("dump_")) }.getOrNull()
            val meta = runCatching { org.json.JSONObject(File("$base.json").readText()) }.getOrNull()
            val frames = meta?.optInt("frame_count", 0)?.takeIf { it > 0 }
            val note = listOfNotNull(
                frames?.let { "$it frames (%.0f s)".format(it / 25.0) },
                meta?.optString("source")?.takeIf { it.isNotEmpty() && it != "camera" },
                if (date != null) name else null,
            ).joinToString(" · ")
            DumpEntry(base, date?.let { shown.format(it) } ?: name, note)
        }
}

// --- The pages -------------------------------------------------------------------------------------

@Composable
private fun MainPage(showStats: Boolean, onShowStats: (Boolean) -> Unit, open: (Page) -> Unit) {
    Text(
        "Palette, noise, texture and view size are on the left bar; recalibrate and capture on the right.",
        color = Ui.Subtle, fontSize = 13.sp, modifier = Modifier.padding(start = 4.dp, end = 4.dp, bottom = 4.dp),
    )
    Section("On screen")
    SwitchRow("Frame rate and lag", "On the left bar", showStats, onShowStats)
    Section("More")
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        NavTile("Image processing", "Stages and tuning", Modifier.weight(1f)) { open(Page.Image) }
        NavTile("Camera & range", "Lockout, high range, start-up", Modifier.weight(1f)) { open(Page.Camera) }
    }
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        NavTile("Recording & replay", "Replay, logging", Modifier.weight(1f)) { open(Page.Recording) }
        NavTile("Diagnostics", "Detailed stats, checks", Modifier.weight(1f)) { open(Page.Diagnostics) }
    }
    Text(
        "ThermalView ${BuildConfig.VERSION_NAME}", color = Ui.Subtle, fontSize = 11.sp,
        modifier = Modifier.padding(start = 4.dp, top = 10.dp),
    )
}

@Composable
private fun ImagePage(options: DebugOptions, onOptions: (DebugOptions) -> Unit) {
    Text(
        "The parts behind Noise and Texture on the left bar. Changing one here shows that setting as Custom.",
        color = Ui.Subtle, fontSize = 13.sp, modifier = Modifier.padding(start = 4.dp, end = 4.dp, bottom = 2.dp),
    )
    Section("Stages")
    SwitchRow("1  Shutter hold", "Holds the image through calibrations", options.shutterHold) {
        onOptions(options.copy(shutterHold = it))
    }
    SwitchRow("2  Bad pixels", "Replaces the camera's known bad pixels", options.badPixels) { onOptions(options.copy(badPixels = it)) }
    SwitchRow("3  Drift and fixed stripes", "Between calibrations", options.drift) { onOptions(options.copy(drift = it)) }
    SwitchRow("3c Flickering stripes", "Per-frame column and row noise (part of Noise)", options.stripes) {
        onOptions(options.copy(stripes = it))
    }
    SwitchRow("4b Noise reduction", "The filter below (part of Noise)", options.nr) { onOptions(options.copy(nr = it)) }
    SwitchRow("5  Tone mapping", "Automatic contrast", options.tone) { onOptions(options.copy(tone = it)) }
    SwitchRow("6  Texture", "Mid-scale contrast", options.detail) { onOptions(options.copy(detail = it)) }
    Section("Noise reduction (4b)")
    Choice("Filter", listOf("Non-local means", "BM3D"), options.nrMethod.coerceIn(0, 1)) { onOptions(options.copy(nrMethod = it)) }
    Choice(
        "Strength",
        MainActivity.NR_STRENGTHS.map { "%.1f".format(it) },
        MainActivity.NR_STRENGTHS.indexOf(options.nrStrength).takeIf { it >= 0 },
        note = if (options.nrMethod == 1) "BM3D takes the strength that leaves this much noise" else null,
    ) { onOptions(options.copy(nrStrength = MainActivity.NR_STRENGTHS[it])) }
    if (options.nrMethod == 0) {
        Choice(
            "Search window",
            MainActivity.NR_SEARCHES.map { "${2 * it + 1}×${2 * it + 1}" },
            MainActivity.NR_SEARCHES.indexOf(options.nrSearch).takeIf { it >= 0 },
        ) { onOptions(options.copy(nrSearch = MainActivity.NR_SEARCHES[it])) }
    }
    SwitchRow("On the GPU", "Off: the CPU's non-local means, 5×5 search", options.gpuNr) { onOptions(options.copy(gpuNr = it)) }
    Section("Texture (6)")
    Choice(
        "Strength",
        MainActivity.TEXTURE_STRENGTHS.map { "×%.1f".format(it) },
        MainActivity.TEXTURE_STRENGTHS.indexOf(options.textureStrength).takeIf { it >= 0 },
    ) { onOptions(options.copy(textureStrength = MainActivity.TEXTURE_STRENGTHS[it])) }
    Section("Display")
    Choice("Upscaling", listOf("Nearest", "B-spline"), options.upscaler.coerceIn(0, 1)) { onOptions(options.copy(upscaler = it)) }
    Choice(
        "Outside the box",
        MainActivity.BOX_DIMS.map { "%.0f%%".format(100 * it) },
        MainActivity.BOX_DIMS.indexOf(options.boxDim).takeIf { it >= 0 },
        note = "Its brightness",
    ) { onOptions(options.copy(boxDim = MainActivity.BOX_DIMS[it])) }
}

@Composable
private fun CameraPage(options: DebugOptions, onOptions: (DebugOptions) -> Unit, say: (String) -> Unit) {
    Section("Protection")
    SwitchRow("Over-range lockout", "Closes the shutter when the scene is too hot for the sensor", options.lockoutEnabled) {
        onOptions(options.copy(lockoutEnabled = it))
    }
    Section("High range (parked)")
    Text(
        "Parked since M2: it takes 1.5–2 minutes to settle and its maths is unknown. For investigating only.",
        color = Ui.Subtle, fontSize = 12.sp, modifier = Modifier.padding(start = 4.dp, end = 4.dp, bottom = 4.dp),
    )
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        PillButton("Normal range", Modifier.weight(1f)) { say(NativeBridge.setRange(false)) }
        PillButton("High range", Modifier.weight(1f)) { say(NativeBridge.setRange(true)) }
    }
    SwitchRow("Automatic switching", null, options.autoRange) { onOptions(options.copy(autoRange = it)) }
    SwitchRow("Maths: InfiCam's", "Off: ht301's", options.highMathInfiCam) { onOptions(options.copy(highMathInfiCam = it)) }
    ActionRow("Range test", "Records 50 frames in each range, then switches back (M2)") {
        say("Range test: " + NativeBridge.readyCapture("range test", true))
    }
    Section("Start-up (from the next connect)")
    SwitchRow("Skip the start-up 0x8000", null, options.skipStartupShutter) { onOptions(options.copy(skipStartupShutter = it)) }
    SwitchRow("Fallback start order", "0x8004 and 0x8020 before streaming", options.fallbackOrder) {
        onOptions(options.copy(fallbackOrder = it))
    }
}

@Composable
private fun RecordingPage(options: DebugOptions, onOptions: (DebugOptions) -> Unit, say: (String) -> Unit, chooseReplay: () -> Unit) {
    Text(
        "To record, use Capture on the right bar: the mode above it (or a long press) picks what it does.",
        color = Ui.Subtle, fontSize = 13.sp, modifier = Modifier.padding(start = 4.dp, end = 4.dp, bottom = 2.dp),
    )
    Section("Replay")
    ActionRow("Replay a recording…", "Runs the pipeline on it, no camera needed") { chooseReplay() }
    ActionRow("Stop the replay", null) {
        NativeBridge.stopReplay()
        say("Replay stopped")
    }
    Section("Logging")
    SwitchRow("Stats CSV", "A row of statistics per frame", options.statsCsv) { onOptions(options.copy(statsCsv = it)) }
    SwitchRow("Record lockouts", "Dumps the frames around each over-range lockout", options.dumpOnLockout) {
        onOptions(options.copy(dumpOnLockout = it))
    }
}

@Composable
private fun DiagnosticsPage(
    options: DebugOptions,
    onOptions: (DebugOptions) -> Unit,
    say: (String) -> Unit,
    ruler: Boolean,
    onRuler: (Boolean) -> Unit,
) {
    var details by remember { mutableStateOf(false) }
    var text by remember { mutableStateOf("") }
    LaunchedEffect(details) {
        while (details) {
            text = NativeBridge.overlayText()
            delay(500)
        }
    }
    Section("Details")
    SwitchRow("Detailed stats", "Everything the pipeline and camera report, here (not over the image)", details) { details = it }
    if (details && text.isNotEmpty()) {
        Text(
            text, color = Color.White, fontFamily = FontFamily.Monospace, fontSize = 10.sp, lineHeight = 13.sp,
            modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp)
                .background(Color(0x66000000), RoundedCornerShape(8.dp)).padding(8.dp),
        )
    }
    Section("Screen")
    SwitchRow("100 mm ruler", "PLAN M6's check of the view sizes: measure it with a ruler", ruler, onRuler)
    Section("Performance")
    SwitchRow("Processing on big cores", "Little cores are ~8× slower", options.bigCores) { onOptions(options.copy(bigCores = it)) }
    Section("Checks")
    ActionRow("Check GPU noise reduction", "Against the CPU; the result goes to the field log") {
        NativeBridge.requestNrCheck()
        say("Check requested: see the field log")
    }
}

// --- Building blocks -------------------------------------------------------------------------------

@Composable
private fun Section(title: String) {
    Text(
        title.uppercase(), color = Ui.Subtle, fontSize = 11.sp, fontWeight = FontWeight.SemiBold, letterSpacing = 1.sp,
        modifier = Modifier.padding(top = 12.dp, bottom = 2.dp, start = 4.dp),
    )
}

@Composable
private fun RowText(title: String, note: String?, modifier: Modifier = Modifier) {
    Column(modifier) {
        Text(title, color = Color.White, fontSize = 15.sp)
        if (note != null) Text(note, color = Ui.Subtle, fontSize = 12.sp)
    }
}

@Composable
private fun SwitchRow(title: String, note: String?, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(
        Modifier.fillMaxWidth().clickable { onChange(!checked) }.padding(horizontal = 4.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        RowText(title, note, Modifier.weight(1f).padding(end = 8.dp))
        Switch(checked = checked, onCheckedChange = onChange)
    }
}

@Composable
private fun ActionRow(title: String, note: String?, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().clickable(onClick = onClick).padding(horizontal = 4.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        RowText(title, note, Modifier.weight(1f).padding(end = 8.dp))
        Text("Run", color = Ui.Accent, fontSize = 14.sp)
    }
}

@Composable
private fun PillButton(text: String, modifier: Modifier, onClick: () -> Unit) {
    Surface(onClick = onClick, color = Color(0x1FFFFFFF), shape = RoundedCornerShape(20.dp), modifier = modifier) {
        Text(text, color = Color.White, fontSize = 14.sp, modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp))
    }
}

/** A tile that opens a deeper page. */
@Composable
private fun NavTile(title: String, note: String, modifier: Modifier, onClick: () -> Unit) {
    Surface(onClick = onClick, color = Color(0x16FFFFFF), shape = RoundedCornerShape(12.dp), modifier = modifier) {
        Row(Modifier.padding(horizontal = 12.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
            RowText(title, note, Modifier.weight(1f))
            Text("›", color = Ui.Subtle, fontSize = 22.sp)
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
