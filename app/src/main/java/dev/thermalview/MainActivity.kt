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
)

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
        NativeBridge.setOptions(value.skipStartupShutter, value.statsCsv, value.fallbackOrder, value.dumpOnLockout)
    }

    /**
     * Debug builds only: lets the M1 runs be driven over adb, e.g.
     * `adb shell am start -n dev.thermalview/.MainActivity --ei dump 200`.
     * Extras: csv, skipStartupShutter, fallbackOrder, lockoutDump (booleans, applied first);
     * reconnect, shutter, lockout, stopReplay (booleans); dump (frames); replay (dump path
     * without extension).
     */
    private fun applyDebugExtras(intent: Intent?) {
        val extras = intent?.extras ?: return
        if (!BuildConfig.DEBUG) return
        var o = options.value
        if (extras.containsKey("csv")) o = o.copy(statsCsv = extras.getBoolean("csv"))
        if (extras.containsKey("skipStartupShutter")) o = o.copy(skipStartupShutter = extras.getBoolean("skipStartupShutter"))
        if (extras.containsKey("fallbackOrder")) o = o.copy(fallbackOrder = extras.getBoolean("fallbackOrder"))
        if (extras.containsKey("lockoutDump")) o = o.copy(dumpOnLockout = extras.getBoolean("lockoutDump"))
        setOptions(o)
        if (extras.getBoolean("reconnect")) {
            camera.close()
            camera.connect()
            Log.i(TAG, "adb: reconnect")
        }
        if (extras.containsKey("dump")) Log.i(TAG, "adb: dump " + NativeBridge.startDump(extras.getInt("dump")))
        if (extras.getBoolean("shutter")) Log.i(TAG, "adb: 0x8000 " + NativeBridge.sendShutter())
        if (extras.getBoolean("lockout")) Log.i(TAG, "adb: " + NativeBridge.triggerLockout())
        if (extras.getBoolean("stopReplay")) NativeBridge.stopReplay()
        extras.getString("replay")?.let { Log.i(TAG, "adb: replay " + NativeBridge.startReplay(it).ifEmpty { "started" }) }
        intent.replaceExtras(Bundle())  // don't re-apply on configuration changes
    }

    private fun storageDir() = (getExternalFilesDir(null) ?: filesDir).absolutePath

    companion object {
        private const val TAG = "ThermalView"
    }
}
