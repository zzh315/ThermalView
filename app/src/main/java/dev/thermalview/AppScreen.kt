package dev.thermalview

import android.view.SurfaceHolder
import android.view.SurfaceView
import android.widget.Toast
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.font.FontFamily
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

@Composable
fun AppScreen(message: String, dumpsDir: String, options: DebugOptions, onOptions: (DebugOptions) -> Unit) {
    var status by remember { mutableStateOf(Status()) }
    var overlay by remember { mutableStateOf("") }
    var showOverlay by rememberSaveable { mutableStateOf(BuildConfig.DEBUG) }

    LaunchedEffect(Unit) {
        while (true) {
            status = Status.parse(NativeBridge.status())
            if (showOverlay) overlay = NativeBridge.overlayText()
            delay(250)
        }
    }
    val view = LocalView.current
    DisposableEffect(status.streaming) {
        view.keepScreenOn = status.streaming
        onDispose { view.keepScreenOn = false }
    }

    Box(Modifier.fillMaxSize().background(Color.Black)) {
        AndroidView(
            factory = { ctx -> SurfaceView(ctx).apply { holder.addCallback(SurfaceCallbacks) } },
            modifier = Modifier.fillMaxSize(),
        )
        ReadoutOverlay(active = status.streaming || status.replay.isNotEmpty())
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
            if (showOverlay && overlay.isNotEmpty()) {
                Text(
                    overlay,
                    color = Color.White,
                    fontFamily = FontFamily.Monospace,
                    fontSize = 11.sp,
                    lineHeight = 14.sp,
                    modifier = Modifier.align(Alignment.TopStart).background(Color(0x99000000)).padding(6.dp),
                )
            }
            if (status.replay.isNotEmpty()) {
                Text(
                    "REPLAY  ${status.replay}",
                    color = Color.White,
                    fontFamily = FontFamily.Monospace,
                    fontSize = 12.sp,
                    modifier = Modifier.align(Alignment.BottomStart).padding(8.dp)
                        .background(Color(0xCCB00020)).padding(horizontal = 8.dp, vertical = 4.dp),
                )
            }
            DebugPanel(
                modifier = Modifier.align(Alignment.BottomEnd),
                dumpsDir = dumpsDir,
                showOverlay = showOverlay,
                onShowOverlay = { showOverlay = it },
                options = options,
                onOptions = onOptions,
            )
        }
    }
}

@Composable
private fun DebugPanel(
    modifier: Modifier,
    dumpsDir: String,
    showOverlay: Boolean,
    onShowOverlay: (Boolean) -> Unit,
    options: DebugOptions,
    onOptions: (DebugOptions) -> Unit,
) {
    val context = LocalContext.current
    val toast = { text: String -> Toast.makeText(context, text, Toast.LENGTH_SHORT).show() }
    var expanded by rememberSaveable { mutableStateOf(false) }
    var choosingReplay by remember { mutableStateOf(false) }

    Column(modifier.padding(8.dp), horizontalAlignment = Alignment.End) {
        if (expanded) {
            Surface(color = Color(0xE0202020), shape = RoundedCornerShape(8.dp)) {
                Column(Modifier.padding(8.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    Toggle("Overlay", showOverlay, onShowOverlay)
                    Toggle("Skip start-up 0x8000 (next start)", options.skipStartupShutter) {
                        onOptions(options.copy(skipStartupShutter = it))
                    }
                    Toggle("Stats CSV", options.statsCsv) { onOptions(options.copy(statsCsv = it)) }
                    Toggle("Ready records a capture", options.captureOnReady) {
                        onOptions(options.copy(captureOnReady = it))
                    }
                    Toggle("Ready: range test (normal + high)", options.rangePairOnReady) {
                        onOptions(options.copy(rangePairOnReady = it))
                    }
                    Toggle("Auto range switching", options.autoRange) { onOptions(options.copy(autoRange = it)) }
                    Toggle("Over-range lockout", options.lockoutEnabled) { onOptions(options.copy(lockoutEnabled = it)) }
                    Toggle("Stage 1: shutter hold + crossfade", options.shutterHold) {
                        onOptions(options.copy(shutterHold = it))
                    }
                    Toggle("Stage 2: bad pixels", options.badPixels) { onOptions(options.copy(badPixels = it)) }
                    Toggle("Stage 3: drift + stripes", options.drift) { onOptions(options.copy(drift = it)) }
                    Toggle("Stage 4: temporal filter", options.denoise) { onOptions(options.copy(denoise = it)) }
                    Toggle("Stage 5: tone mapping", options.tone) { onOptions(options.copy(tone = it)) }
                    Toggle("Stage 6: detail + sharpening", options.detail) { onOptions(options.copy(detail = it)) }
                    Toggle("Processing on big cores", options.bigCores) { onOptions(options.copy(bigCores = it)) }
                    Toggle("Upscaler: B-spline (off: nearest)", options.upscaler == 1) {
                        onOptions(options.copy(upscaler = if (it) 1 else 0))
                    }
                    Button(onClick = { onOptions(options.copy(palette = (options.palette + 1) % 3)) }) {
                        Text("Palette: " + listOf("gray", "white_hot", "rainbow_hc")[options.palette.coerceIn(0, 2)])
                    }
                    Button(onClick = { onOptions(options.copy(viewSize = (options.viewSize + 1) % 3)) }) {
                        Text("View: " + MainActivity.VIEW_NAMES[options.viewSize.coerceIn(0, 2)])
                    }
                    Toggle("High-range math: InfiCam (off: ht301)", options.highMathInfiCam) {
                        onOptions(options.copy(highMathInfiCam = it))
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(onClick = { toast(NativeBridge.setRange(false)) }) { Text("Normal range") }
                        Button(onClick = { toast(NativeBridge.setRange(true)) }) { Text("High range") }
                    }
                    Toggle("Fallback start order (next start)", options.fallbackOrder) {
                        onOptions(options.copy(fallbackOrder = it))
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(onClick = { toast("Dump: " + NativeBridge.startDump(200)) }) { Text("Dump 200") }
                        Button(onClick = { toast("0x8000: " + NativeBridge.sendShutter()) }) { Text("Send 0x8000") }
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(onClick = { choosingReplay = true }) { Text("Replay…") }
                        Button(onClick = { NativeBridge.stopReplay() }) { Text("Stop replay") }
                    }
                }
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            // Lets the owner signal a test step from the tablet; test scripts wait for this log line.
            var marks by rememberSaveable { mutableStateOf(0) }
            Button(onClick = {
                marks += 1
                if (options.captureOnReady) {
                    toast("Ready #$marks: " + NativeBridge.readyCapture("ready #$marks", options.rangePairOnReady))
                } else {
                    NativeBridge.mark("ready #$marks")
                    toast("Ready #$marks sent")
                }
            }) { Text("Ready") }
            TextButton(onClick = { expanded = !expanded }) {
                Text(if (expanded) "Close debug" else "Debug", color = Color.White)
            }
        }
    }

    if (choosingReplay) {
        val dumps = File(dumpsDir).listFiles { f -> f.name.endsWith(".raw") }
            ?.map { it.path.removeSuffix(".raw") }?.sortedDescending().orEmpty()
        AlertDialog(
            onDismissRequest = { choosingReplay = false },
            confirmButton = { TextButton(onClick = { choosingReplay = false }) { Text("Cancel") } },
            title = { Text("Replay a dump") },
            text = {
                if (dumps.isEmpty()) {
                    Text("No dumps in $dumpsDir")
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
private fun Toggle(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Checkbox(checked = checked, onCheckedChange = onChange)
        Text(label, color = Color.White)
    }
}
