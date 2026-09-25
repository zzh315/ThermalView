package dev.thermalview

import android.Manifest
import android.content.Intent
import android.content.pm.ActivityInfo
import android.hardware.usb.UsbManager
import android.os.Bundle
import android.util.Log
import android.view.Surface
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.mutableStateOf
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat

data class DebugOptions(
    val skipStartupShutter: Boolean = false,
    val statsCsv: Boolean = false,
    val fallbackOrder: Boolean = false,
    val dumpOnLockout: Boolean = false,
    val autoRange: Boolean = false,      // automatic range switching (off until the M2 iron session)
    val highMathInfiCam: Boolean = false,
    val lockoutEnabled: Boolean = true,     // off only for tests with hot objects within the sensor's rating
    val rangeSettleMs: Int = 500,           // wait between a range command and its 0x8000 (M2 settling test)
    val shutterHold: Boolean = true,        // M4 stage 1 (approved): hold through shutter cycles (no crossfade: owner, 2026-09-26)
    val badPixels: Boolean = true,          // M4 stage 2 (approved): replace the camera's known bad pixels
    val drift: Boolean = true,              // M4 stage 3 (approved): drift compensation + stripe cleanup
    val stripes: Boolean = true,            // M4 stage 3c: the per-frame column/row noise; part of NR_LEVELS' Low and High
    val tone: Boolean = true,               // M4 stage 5 (approved): automatic tone mapping, gain cap 2
    val nr: Boolean = true,                 // M4 stage 4b (approved): spatial noise reduction
    val nrStrength: Float = 0.8f,           // its strength (h in noise sigmas): Low, the default (owner, 2026-09-26)
    val nrSearch: Int = 5,                  // its search radius: 2 (5x5), 3 (7x7), 5 (11x11); NR_SEARCHES
    val gpuNr: Boolean = true,              // stage 4b on the GPU; off: the CPU at a 5x5 search (h scaled to match)
    val nrMethod: Int = 1,                  // stage 4b's filter: 0 non-local means, 1 BM3D (Low and High; owner, 2026-09-26)
    val detail: Boolean = true,             // M4 stage 6 (approved): the mid-scale texture layer; a setting
    val textureStrength: Float = 1.5f,      // stage 6's strength: owner, 1.5 by default, up to 3 (TEXTURE_STRENGTHS)
    val bigCores: Boolean = true,           // processing thread on the big cores (little ones: ~8x slower)
    val perfHint: Boolean = true,           // ADPF: ask for the clock the frame budget needs
    val upscaler: Int = 1,                  // M5 preview: 0 nearest (M1), 1 cardinal B-spline + 2x2 clamp
    val palette: Int = 1,                   // M5: 1 white_hot, 2 rainbow_hc (0: the old plain gray, adb only)
    val viewSize: Int = 2,                  // M6 presets, debug until then: 0 Phone, 1 Small tablet, 2 Full
    val boxDim: Float = 0.5f,               // M6: the brightness outside the box (start at 50%; tunable)
    val rainbowPreset: Int = 0,             // the rainbow's look: 0 Deep, 1 Soft (RAINBOW_PRESETS; kept)
    val orientation: Int = 1,               // M6: how the screen turns, ORIENTATION_NAMES (0 Auto, 1 Landscape, 2 Portrait; kept)
) {
    /** The pipeline stages these toggles select, for [NativeBridge.setPipeline]. */
    fun stages(): String = listOf(
        "shutter=" + bit(shutterHold), "badPixels=" + bit(badPixels), "drift=" + bit(drift), "destripe=" + bit(drift),
        "stripes=" + bit(stripes),
        "denoise=0", "tone=" + bit(tone), "detail=" + bit(detail),  // stage 4 removed (owner, 2026-09-25)
        "nr=" + bit(nr), "nrSearch=$nrSearch", "nrStrength=$nrStrength",  // (the CPU falls back to 5x5)
        "detailMid=" + textureStrength,
    ).plus(if (nrMethod == 1) listOf(
        // BM3D's real-time shape, at the strength that leaves the noise non-local means would (PIPELINE_LOG)
        "nrMethod=bm3d", "bm3dStrength=" + MainActivity.bm3dStrengthFor(nrStrength), "bm3dBlock=8", "bm3dStride=6",
        "bm3dSearch=5", "bm3dGroup1=8", "bm3dGroup2=8", "bm3dTau1=0", "bm3dTau2=0",
    ) else emptyList()).plus(
        // (the rainbow presets' own auto contrast; white hot keeps stage 5 as it is)
        (if (palette == 2) MainActivity.RAINBOW_PRESETS.getOrElse(rainbowPreset) { MainActivity.RAINBOW_PRESETS[0] }.tone else "")
            .let { if (it.isEmpty()) emptyList() else listOf(it) },
    ).joinToString(",")

    private fun bit(on: Boolean) = if (on) "1" else "0"
}

/** Debug: a zoom set over adb (pinches can't be sent with `input`). */
data class ZoomRequest(val zoom: Float, val cx: Float, val cy: Float)

class MainActivity : ComponentActivity() {
    private lateinit var camera: UsbCamera
    private val message = mutableStateOf("")
    private val options = mutableStateOf(DebugOptions())
    private val zoomRequest = mutableStateOf<ZoomRequest?>(null)  // debug: set over adb (--ef zoom)
    private val prefs by lazy { getSharedPreferences("settings", MODE_PRIVATE) }

    private val cameraPermission = registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) camera.connect() else message.value = "The camera permission is needed to use a USB camera."
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        NativeBridge.init(storageDir(), BuildConfig.VERSION_NAME)
        registerDriftMaps()
        camera = UsbCamera(
            context = this,
            onNeedCameraPermission = { cameraPermission.launch(Manifest.permission.CAMERA) },
            onMessage = { message.value = it },
        )
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
        setOptions(restored(options.value))  // the app's defaults and the kept settings, before any debug extras
        NativeBridge.setRangeLock(false)  // each launch starts in Auto range (PLAN M6; the session outlives us)
        applyDebugExtras(intent)
        setContent {
            AppScreen(
                message = message.value,
                dumpsDir = "${storageDir()}/dumps",
                options = options.value,
                onOptions = ::setOptions,
                paletteColors = { palette, preset -> NativeBridge.paletteColors(paletteJson(palette, preset), 256) },
                zoomRequest = zoomRequest.value,
                onZoomRequestDone = { zoomRequest.value = null },
            )
        }
    }

    override fun onStart() {
        super.onStart()
        camera.start()
        camera.connect()
    }

    override fun onStop() {
        // Release the camera in the background (like InfiCam); onStart reconnects.
        NativeBridge.stopReplay()
        camera.stop()
        super.onStop()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        applyDebugExtras(intent)
        if (intent.action == UsbManager.ACTION_USB_DEVICE_ATTACHED) camera.connect()
    }

    /**
     * PLAN M6's persistence: the palette, the view size and orientation and the noise and texture
     * settings are kept across launches (a custom setting from the debug pages isn't: the next launch
     * starts from its preset). Everything else starts from the defaults.
     */
    private fun restored(o: DebugOptions): DebugOptions {
        if (prefs.getInt("defaults", 1) < 3) {  // (2026-09-26: Noise and Texture Low by default, the owner)
            prefs.edit().remove("nrLevel").remove("textureLevel").putInt("defaults", 3).apply()
        }
        var r = o.copy(
            palette = prefs.getInt("palette", o.palette).coerceIn(1, PALETTES.size),
            viewSize = prefs.getInt("viewSize", o.viewSize).coerceIn(0, VIEW_WIDTHS.size - 1),
            rainbowPreset = prefs.getInt("rainbow", o.rainbowPreset).coerceIn(0, RAINBOW_PRESETS.size - 1),
            orientation = prefs.getInt("orientation", o.orientation).coerceIn(0, ORIENTATIONS.size - 1),
        )
        prefs.getInt("nrLevel", -1).takeIf { it in LEVELS.indices }?.let { r = withNrLevel(r, it) }
        prefs.getInt("textureLevel", -1).takeIf { it in LEVELS.indices }?.let { r = withTextureLevel(r, it) }
        return r
    }

    private fun keep(o: DebugOptions) {
        prefs.edit().apply {
            if (o.palette in 1..PALETTES.size) putInt("palette", o.palette)
            putInt("viewSize", o.viewSize)
            putInt("rainbow", o.rainbowPreset)
            putInt("orientation", o.orientation)
            nrLevelOf(o)?.let { putInt("nrLevel", it) }
            textureLevelOf(o)?.let { putInt("textureLevel", it) }
        }.apply()
    }

    private fun setOptions(value: DebugOptions) {
        options.value = value
        keep(value)
        NativeBridge.setOptions(
            value.skipStartupShutter, value.statsCsv, value.fallbackOrder, value.dumpOnLockout,
            value.autoRange, value.highMathInfiCam, value.lockoutEnabled, value.rangeSettleMs, value.bigCores,
            value.perfHint, value.gpuNr,
        )
        NativeBridge.setPipeline(value.stages()).takeIf { it.isNotEmpty() }?.let { Log.w(TAG, "pipeline: $it") }
        NativeBridge.setDisplay(value.upscaler, paletteJson(value.palette)).takeIf { it.isNotEmpty() }?.let { Log.w(TAG, "display: $it") }
        NativeBridge.setViewWidth(VIEW_WIDTHS.getOrElse(value.viewSize) { 0 })
        val turn = ORIENTATIONS.getOrElse(value.orientation) { ORIENTATIONS[1] }
        if (requestedOrientation != turn) requestedOrientation = turn
    }

    /**
     * Debug builds only: lets the M1 runs be driven over adb, e.g.
     * `adb shell am start -n dev.thermalview/.MainActivity --ei dump 200`.
     * Extras: csv, skipStartupShutter, fallbackOrder, lockoutDump, autoRange, highMathInfi,
     * shutterHold, badPixels, drift, stripes, tone, detail, nr, gpuNr, bigCores, perfHint (booleans), upscaler, palette, viewSize, orientation, nrSearch, nrMethod, nrLevel, textureLevel (ints), textureStrength, nrStrength (floats; applied first); reconnect, shutter, lockout, stopReplay, readback, nrCheck, logOverlay (booleans);
     * dump (frames); replay (dump path without extension); range ("high" or "normal"); pipeline
     * (stage text, e.g. "shutter=0"); zoom, zoomX, zoomY (floats: the zoom and the camera point at
     * the view's center); message (a test banner, "" to clear).
     */
    private fun applyDebugExtras(intent: Intent?) {
        val extras = intent?.extras ?: return
        if (!BuildConfig.DEBUG) return
        var o = options.value
        if (extras.containsKey("csv")) o = o.copy(statsCsv = extras.getBoolean("csv"))
        if (extras.containsKey("skipStartupShutter")) o = o.copy(skipStartupShutter = extras.getBoolean("skipStartupShutter"))
        if (extras.containsKey("fallbackOrder")) o = o.copy(fallbackOrder = extras.getBoolean("fallbackOrder"))
        if (extras.containsKey("lockoutDump")) o = o.copy(dumpOnLockout = extras.getBoolean("lockoutDump"))
        if (extras.containsKey("autoRange")) o = o.copy(autoRange = extras.getBoolean("autoRange"))
        if (extras.containsKey("highMathInfi")) o = o.copy(highMathInfiCam = extras.getBoolean("highMathInfi"))
        if (extras.containsKey("lockoutEnabled")) o = o.copy(lockoutEnabled = extras.getBoolean("lockoutEnabled"))
        if (extras.containsKey("rangeSettleMs")) o = o.copy(rangeSettleMs = extras.getInt("rangeSettleMs"))
        if (extras.containsKey("shutterHold")) o = o.copy(shutterHold = extras.getBoolean("shutterHold"))
        if (extras.containsKey("badPixels")) o = o.copy(badPixels = extras.getBoolean("badPixels"))
        if (extras.containsKey("drift")) o = o.copy(drift = extras.getBoolean("drift"))
        if (extras.containsKey("stripes")) o = o.copy(stripes = extras.getBoolean("stripes"))
        if (extras.containsKey("tone")) o = o.copy(tone = extras.getBoolean("tone"))
        if (extras.containsKey("detail")) o = o.copy(detail = extras.getBoolean("detail"))
        if (extras.containsKey("bigCores")) o = o.copy(bigCores = extras.getBoolean("bigCores"))
        if (extras.containsKey("perfHint")) o = o.copy(perfHint = extras.getBoolean("perfHint"))
        if (extras.containsKey("upscaler")) o = o.copy(upscaler = extras.getInt("upscaler"))
        if (extras.containsKey("palette")) o = o.copy(palette = extras.getInt("palette"))
        if (extras.containsKey("viewSize")) o = o.copy(viewSize = extras.getInt("viewSize"))
        if (extras.containsKey("orientation")) o = o.copy(orientation = extras.getInt("orientation").coerceIn(0, ORIENTATIONS.size - 1))
        if (extras.containsKey("textureStrength")) o = o.copy(textureStrength = extras.getFloat("textureStrength"))
        if (extras.containsKey("nr")) o = o.copy(nr = extras.getBoolean("nr"))
        if (extras.containsKey("nrStrength")) o = o.copy(nrStrength = extras.getFloat("nrStrength"))
        if (extras.containsKey("gpuNr")) o = o.copy(gpuNr = extras.getBoolean("gpuNr"))
        if (extras.containsKey("nrSearch")) o = o.copy(nrSearch = extras.getInt("nrSearch"))
        if (extras.containsKey("nrMethod")) o = o.copy(nrMethod = extras.getInt("nrMethod"))
        if (extras.containsKey("nrLevel")) o = withNrLevel(o, extras.getInt("nrLevel"))
        if (extras.containsKey("textureLevel")) o = withTextureLevel(o, extras.getInt("textureLevel"))
        setOptions(o)
        if (extras.getBoolean("reconnect")) {
            camera.close()
            camera.connect()
            Log.i(TAG, "adb: reconnect")
        }
        if (extras.containsKey("dump")) Log.i(TAG, "adb: dump " + NativeBridge.startDump(extras.getInt("dump")))
        if (extras.getBoolean("shutter")) Log.i(TAG, "adb: 0x8000 " + NativeBridge.sendShutter())
        if (extras.getBoolean("lockout")) Log.i(TAG, "adb: " + NativeBridge.triggerLockout())
        extras.getString("range")?.let { Log.i(TAG, "adb: " + NativeBridge.setRange(it == "high")) }
        if (extras.getBoolean("stopReplay")) NativeBridge.stopReplay()
        if (extras.getBoolean("logOverlay")) Log.i(TAG, "overlay:\n" + NativeBridge.overlayText())
        if (extras.getBoolean("readback")) {
            val dir = java.io.File(storageDir(), "readback").apply { mkdirs() }
            val prefix = java.io.File(dir, "rb_" + System.currentTimeMillis()).absolutePath
            NativeBridge.requestReadback(prefix, paletteName(options.value.palette, options.value.rainbowPreset) ?: "gray")
            Log.i(TAG, "adb: readback $prefix")
        }
        if (extras.getBoolean("nrCheck")) {  // stage 4b's GPU against the CPU reference: the field log has it
            NativeBridge.requestNrCheck()
            Log.i(TAG, "adb: nrCheck")
        }
        extras.getString("replay")?.let { Log.i(TAG, "adb: replay " + NativeBridge.startReplay(it).ifEmpty { "started" }) }
        if (extras.containsKey("zoom")) {
            zoomRequest.value = ZoomRequest(
                extras.getFloat("zoom"),
                extras.getFloat("zoomX", CamRect.FRAME_W / 2),
                extras.getFloat("zoomY", CamRect.FRAME_H / 2),
            )
        }
        extras.getString("message")?.let { message.value = it }
        // Experiments: any stage text, until the next toggle change re-applies the toggles.
        extras.getString("pipeline")?.let { Log.i(TAG, "adb: pipeline " + NativeBridge.setPipeline(it).ifEmpty { it }) }
        intent.replaceExtras(Bundle())  // don't re-apply on configuration changes
    }

    private fun storageDir() = (getExternalFilesDir(null) ?: filesDir).absolutePath

    /** A palette file's text ("" for 0, the plain gray). */
    private fun paletteJson(palette: Int, rainbowPreset: Int = options.value.rainbowPreset): String =
        paletteName(palette, rainbowPreset)?.let { name ->
            runCatching { assets.open("$name.json").bufferedReader().use { it.readText() } }.getOrDefault("")
        } ?: ""

    /** Stage 3: every bundled drift map (assets/drift_<serial>.f32, from native/core/data). */
    private fun registerDriftMaps() {
        val names = assets.list("")?.filter { it.startsWith("drift_") && it.endsWith(".f32") } ?: return
        for (name in names) {
            val serial = name.removePrefix("drift_").removeSuffix(".f32")
            NativeBridge.registerDriftMap(serial, assets.open(name).use { it.readBytes() })
        }
    }

    companion object {
        private const val TAG = "ThermalView"
        val PALETTES = listOf("white_hot", "rainbow_hc")  // assets from palettes/, DebugOptions.palette 1 and 2
        // The rainbow's looks (the owner's pick, 2026-09-26: Deep "looks more colorful and intense", and
        // Soft kept as an option; Vivid and Room looked washed out, Deep+ like Deep): the palette file and
        // stage 5's balance, which centres the scene's median on the palette (PIPELINE_LOG).
        class RainbowPreset(val name: String, val palette: String, val tone: String)
        val RAINBOW_PRESETS = listOf(
            RainbowPreset("Deep", "rainbow_deep", "toneBalance"),
            RainbowPreset("Soft", "rainbow_soft", "toneBalance"),
        )
        fun paletteName(palette: Int, rainbowPreset: Int): String? =
            if (palette == 2) RAINBOW_PRESETS.getOrElse(rainbowPreset) { RAINBOW_PRESETS[0] }.palette
            else PALETTES.getOrNull(palette - 1)
        val TEXTURE_STRENGTHS = listOf(1.5f, 2.0f, 2.5f, 3.0f)  // stage 6's settings (owner, 2026-09-25)
        val NR_STRENGTHS = listOf(0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f, 1.4f)  // stage 4b's, h in noise sigmas
        val NR_SEARCHES = listOf(2, 3, 5)  // stage 4b's search radius: 5x5, 7x7, 11x11
        // The simple settings (owner, 2026-09-26: "simple presets that abstract settings into simple low
        // and high effects"): each level sets the stages underneath, and a setting changed by hand in the
        // debug panel shows as "custom". Noise reduction is stage 3c (the per-frame stripes) with stage
        // 4b's BM3D at the owner's Low or High strength (owner, 2026-09-26: BM3D the default method): the
        // strengths that leave the noise non-local means leaves at h 0.8 and 1.1 (PIPELINE_LOG). It costs
        // latency (p95 ~30 ms), which the owner's rule puts after image quality. (nrStrength is NLM's h:
        // BM3D's strength comes from it, and the CPU's non-local means takes over if the GPU can't.)
        val LEVELS = listOf("Off", "Low", "High")
        fun withNrLevel(o: DebugOptions, level: Int): DebugOptions = when (level) {
            0 -> o.copy(nr = false, stripes = false)
            1 -> o.copy(nr = true, nrMethod = 1, nrStrength = 0.8f, nrSearch = 5, stripes = true)
            else -> o.copy(nr = true, nrMethod = 1, nrStrength = 1.1f, nrSearch = 5, stripes = true)
        }
        fun nrLevelOf(o: DebugOptions): Int? = LEVELS.indices.firstOrNull { withNrLevel(o, it) == o }
        // Texture (stage 6's mid-scale contrast; PLAN M6's setting): its strength x1.5 (Low, the default)
        // or x3 (High, the owner's pick when trying it, 2026-09-26).
        fun withTextureLevel(o: DebugOptions, level: Int): DebugOptions = when (level) {
            0 -> o.copy(detail = false)
            1 -> o.copy(detail = true, textureStrength = 1.5f)
            else -> o.copy(detail = true, textureStrength = 3.0f)
        }
        fun textureLevelOf(o: DebugOptions): Int? = LEVELS.indices.firstOrNull { withTextureLevel(o, it) == o }
        // BM3D's strength (sigma multiple) that leaves the noise non-local means leaves at each preset's h, on
        // known texture through stage 3c (PIPELINE_LOG, 2026-09-26); between and beyond them, linear.
        private val BM3D_MATCH = listOf(0.8f to 1.04f, 0.9f to 1.23f, 1.1f to 1.65f)
        fun bm3dStrengthFor(h: Float): Float {
            val i = when {
                h <= BM3D_MATCH[1].first -> 0
                else -> 1
            }
            val (x0, y0) = BM3D_MATCH[i]
            val (x1, y1) = BM3D_MATCH[i + 1]
            return y0 + (h - x0) * (y1 - y0) / (x1 - x0)
        }
        // PLAN M6's starting view sizes at the panel's verified 244.5 dpi (DEVICE.md): Phone ~4.5" (880 px
        // wide), Small tablet 7.5" (1467 px), Full the largest 4:3 fit (2133 x 1600, 10.9").
        val BOX_DIMS = listOf(0.3f, 0.5f, 0.7f)  // the box's outside, debug choices
        val VIEW_WIDTHS = listOf(880, 1467, 0)  // (the image's long side, whichever way it's turned)
        val VIEW_NAMES = listOf("Phone", "Tablet", "Full")
        val VIEW_DIAGONALS = listOf("4.5″", "7.5″", "10.9″")
        // M6's orientation (owner, 2026-09-26: "there should be auto orientation that changes with
        // device orientation as well"): Auto turns the screen all four ways with the tablet (whatever
        // the system's rotation lock); Landscape and Portrait either way up.
        val ORIENTATION_NAMES = listOf("Auto", "Landscape", "Portrait")
        val ORIENTATION_NOTES = listOf(
            "The screen turns with the tablet, all four ways.",
            "Landscape, either way up.",
            "Portrait, either way up.",
        )
        val ORIENTATIONS = listOf(
            ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR,
            ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE,
            ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT,
        )
        // The camera is fixed to the tablet and turns with it; the image is upright at this display
        // rotation (DEVICE.md, the M1 orientation check), and at any other it's turned back by the
        // difference: (UPRIGHT_ROTATION - rotation) quarter turns clockwise.
        const val UPRIGHT_ROTATION = Surface.ROTATION_90
        fun imageTurns(displayRotation: Int) = (UPRIGHT_ROTATION - displayRotation + 4) % 4
    }
}
