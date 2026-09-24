package dev.thermalview

import android.Manifest
import android.content.Intent
import android.hardware.usb.UsbManager
import android.os.Bundle
import android.util.Log
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
    val captureOnReady: Boolean = true,  // the Ready button records a capture on the tablet itself
    val autoRange: Boolean = false,      // automatic range switching (off until the M2 iron session)
    val highMathInfiCam: Boolean = false,
    val rangePairOnReady: Boolean = false,  // Ready captures the scene in both ranges (M2 iron session)
    val lockoutEnabled: Boolean = true,     // off only for tests with hot objects within the sensor's rating
    val rangeSettleMs: Int = 500,           // wait between a range command and its 0x8000 (M2 settling test)
    val shutterHold: Boolean = true,        // M4 stage 1 (approved): hold through shutter cycles, crossfade back
    val badPixels: Boolean = true,          // M4 stage 2 (approved): replace the camera's known bad pixels
    val drift: Boolean = true,              // M4 stage 3 (approved): drift compensation + stripe cleanup
    val denoise: Boolean = true,            // M4 stage 4 (approved): motion-adaptive temporal filter
    val tone: Boolean = true,               // M4 stage 5 (approved): automatic tone mapping, gain cap 2
    val detail: Boolean = true,             // M4 stage 6 (approved): the mid-scale texture layer; a setting
    val textureStrength: Float = 1.5f,      // stage 6's strength: owner, 1.5 by default, up to 3 (TEXTURE_STRENGTHS)
    val bigCores: Boolean = true,           // processing thread on the big cores (little ones: ~8x slower)
    val perfHint: Boolean = true,           // ADPF: ask for the clock the frame budget needs
    val upscaler: Int = 1,                  // M5 preview: 0 nearest (M1), 1 cardinal B-spline + 2x2 clamp
    val palette: Int = 0,                   // M5 preview: 0 gray (as before), 1 white_hot, 2 rainbow_hc
    val viewSize: Int = 2,                  // M6 presets, debug until then: 0 Phone, 1 Small tablet, 2 Full
) {
    /** The pipeline stages these toggles select, for [NativeBridge.setPipeline]. */
    fun stages(): String = listOf(
        "shutter=" + bit(shutterHold), "badPixels=" + bit(badPixels), "drift=" + bit(drift), "destripe=" + bit(drift),
        "denoise=" + bit(denoise), "tone=" + bit(tone), "detail=" + bit(detail),
        "detailMid=" + textureStrength,
    ).joinToString(",")

    private fun bit(on: Boolean) = if (on) "1" else "0"
}

class MainActivity : ComponentActivity() {
    private lateinit var camera: UsbCamera
    private val message = mutableStateOf("")
    private val options = mutableStateOf(DebugOptions())

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
        applyDebugExtras(intent)
        setContent {
            AppScreen(
                message = message.value,
                dumpsDir = "${storageDir()}/dumps",
                options = options.value,
                onOptions = ::setOptions,
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

    private fun setOptions(value: DebugOptions) {
        options.value = value
        NativeBridge.setOptions(
            value.skipStartupShutter, value.statsCsv, value.fallbackOrder, value.dumpOnLockout,
            value.autoRange, value.highMathInfiCam, value.lockoutEnabled, value.rangeSettleMs, value.bigCores,
            value.perfHint,
        )
        NativeBridge.setPipeline(value.stages()).takeIf { it.isNotEmpty() }?.let { Log.w(TAG, "pipeline: $it") }
        val palette = PALETTES.getOrNull(value.palette - 1)?.let { name ->
            runCatching { assets.open("$name.json").bufferedReader().use { it.readText() } }.getOrDefault("")
        } ?: ""
        NativeBridge.setDisplay(value.upscaler, palette).takeIf { it.isNotEmpty() }?.let { Log.w(TAG, "display: $it") }
        NativeBridge.setViewWidth(VIEW_WIDTHS.getOrElse(value.viewSize) { 0 })
    }

    /**
     * Debug builds only: lets the M1 runs be driven over adb, e.g.
     * `adb shell am start -n dev.thermalview/.MainActivity --ei dump 200`.
     * Extras: csv, skipStartupShutter, fallbackOrder, lockoutDump, autoRange, highMathInfi,
     * shutterHold, badPixels, drift, denoise, tone, detail, bigCores, perfHint (booleans), upscaler, palette, viewSize (ints), textureStrength (float; applied first); reconnect, shutter, lockout, stopReplay, readback, logOverlay (booleans);
     * dump (frames); replay (dump path without extension); range ("high" or "normal"); pipeline
     * (stage text, e.g. "shutter=0").
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
        if (extras.containsKey("rangePair")) o = o.copy(rangePairOnReady = extras.getBoolean("rangePair"))
        if (extras.containsKey("lockoutEnabled")) o = o.copy(lockoutEnabled = extras.getBoolean("lockoutEnabled"))
        if (extras.containsKey("rangeSettleMs")) o = o.copy(rangeSettleMs = extras.getInt("rangeSettleMs"))
        if (extras.containsKey("shutterHold")) o = o.copy(shutterHold = extras.getBoolean("shutterHold"))
        if (extras.containsKey("badPixels")) o = o.copy(badPixels = extras.getBoolean("badPixels"))
        if (extras.containsKey("drift")) o = o.copy(drift = extras.getBoolean("drift"))
        if (extras.containsKey("denoise")) o = o.copy(denoise = extras.getBoolean("denoise"))
        if (extras.containsKey("tone")) o = o.copy(tone = extras.getBoolean("tone"))
        if (extras.containsKey("detail")) o = o.copy(detail = extras.getBoolean("detail"))
        if (extras.containsKey("bigCores")) o = o.copy(bigCores = extras.getBoolean("bigCores"))
        if (extras.containsKey("perfHint")) o = o.copy(perfHint = extras.getBoolean("perfHint"))
        if (extras.containsKey("upscaler")) o = o.copy(upscaler = extras.getInt("upscaler"))
        if (extras.containsKey("palette")) o = o.copy(palette = extras.getInt("palette"))
        if (extras.containsKey("viewSize")) o = o.copy(viewSize = extras.getInt("viewSize"))
        if (extras.containsKey("textureStrength")) o = o.copy(textureStrength = extras.getFloat("textureStrength"))
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
            NativeBridge.requestReadback(prefix, PALETTES.getOrNull(options.value.palette - 1) ?: "gray")
            Log.i(TAG, "adb: readback $prefix")
        }
        extras.getString("replay")?.let { Log.i(TAG, "adb: replay " + NativeBridge.startReplay(it).ifEmpty { "started" }) }
        // Experiments: any stage text, until the next toggle change re-applies the toggles.
        extras.getString("pipeline")?.let { Log.i(TAG, "adb: pipeline " + NativeBridge.setPipeline(it).ifEmpty { it }) }
        intent.replaceExtras(Bundle())  // don't re-apply on configuration changes
    }

    private fun storageDir() = (getExternalFilesDir(null) ?: filesDir).absolutePath

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
        val TEXTURE_STRENGTHS = listOf(1.5f, 2.0f, 2.5f, 3.0f)  // stage 6's settings (owner, 2026-09-25)
        // PLAN M6's starting view sizes at the panel's verified 244.5 dpi (DEVICE.md): Phone ~4.5" (880 px
        // wide), Small tablet 7.5" (1467 px), Full the largest 4:3 fit (2133 x 1600, 10.9").
        val VIEW_WIDTHS = listOf(880, 1467, 0)
        val VIEW_NAMES = listOf("Phone 4.5\"", "Small tablet 7.5\"", "Full 10.9\"")
    }
}
