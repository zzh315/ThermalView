package dev.thermalview

import android.Manifest
import android.content.Intent
import android.hardware.usb.UsbManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.mutableStateOf
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat

class MainActivity : ComponentActivity() {
    private lateinit var camera: UsbCamera
    private val message = mutableStateOf("")

    private val cameraPermission = registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) camera.connect() else message.value = "The camera permission is needed to use a USB camera."
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        NativeBridge.init((getExternalFilesDir(null) ?: filesDir).absolutePath, BuildConfig.VERSION_NAME)
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
        setContent { AppScreen(message = message.value, dumpsDir = dumpsDir()) }
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
        if (intent.action == UsbManager.ACTION_USB_DEVICE_ATTACHED) camera.connect()
    }

    private fun dumpsDir() = "${(getExternalFilesDir(null) ?: filesDir).absolutePath}/dumps"
}
