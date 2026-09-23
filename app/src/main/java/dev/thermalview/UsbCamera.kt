package dev.thermalview

import android.Manifest
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbManager
import androidx.core.content.ContextCompat
import androidx.core.content.IntentCompat

/**
 * Finds the camera, gets USB permission and hands its file descriptor to native code
 * (docs/PROTOCOL.md "Android USB notes"). This class owns the descriptor and is the only place
 * that closes it.
 */
class UsbCamera(
    private val context: Context,
    private val onNeedCameraPermission: () -> Unit,
    private val onMessage: (String) -> Unit,
) {
    private val usb = context.getSystemService(UsbManager::class.java)
    private val permissionAction = "${context.packageName}.USB_PERMISSION"
    private var connection: UsbDeviceConnection? = null
    private var device: UsbDevice? = null
    private var permissionPending = false
    private var started = false

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(ctx: Context, intent: Intent) {
            val dev = IntentCompat.getParcelableExtra(intent, UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
            when (intent.action) {
                permissionAction -> {
                    permissionPending = false
                    if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false) && dev != null) {
                        open(dev)
                    } else {
                        onMessage(
                            "USB permission denied. If the camera privacy toggle is on (Quick Settings), " +
                                "turn it off, then replug the camera.",
                        )
                    }
                }
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> if (dev.isCamera()) connect()
                UsbManager.ACTION_USB_DEVICE_DETACHED -> if (dev != null && dev.deviceName == device?.deviceName) {
                    close()
                    onMessage("Camera unplugged")
                }
            }
        }
    }

    fun start() {
        if (started) return
        started = true
        val filter = IntentFilter().apply {
            addAction(permissionAction)
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        // Not exported: the permission result comes from the system through our own PendingIntent.
        ContextCompat.registerReceiver(context, receiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED)
    }

    fun stop() {
        if (!started) return
        started = false
        context.unregisterReceiver(receiver)
        close()
    }

    /** Opens the camera if it's attached and permitted; otherwise asks for what's missing. */
    fun connect() {
        if (connection != null) return
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            onNeedCameraPermission()
            return
        }
        val dev = usb.deviceList.values.firstOrNull { it.isCamera() }
        if (dev == null) {
            onMessage("Plug in the camera")
            return
        }
        if (usb.hasPermission(dev)) {
            open(dev)
        } else if (!permissionPending) {
            permissionPending = true
            // Mutable so the system can add EXTRA_DEVICE / EXTRA_PERMISSION_GRANTED; explicit, as
            // targetSdk 34 requires for mutable PendingIntents.
            val intent = Intent(permissionAction).setPackage(context.packageName)
            usb.requestPermission(dev, PendingIntent.getBroadcast(context, 0, intent, PendingIntent.FLAG_MUTABLE))
        }
    }

    fun close() {
        val conn = connection ?: return
        NativeBridge.closeCamera()
        conn.close()
        connection = null
        device = null
    }

    private fun open(dev: UsbDevice) {
        if (connection != null) return
        val conn = usb.openDevice(dev)
        if (conn == null) {
            onMessage("Could not open the camera")
            return
        }
        val serial = runCatching { dev.serialNumber }.getOrNull()
        if (!NativeBridge.openCamera(conn.fileDescriptor, dev.manufacturerName, dev.productName, serial)) {
            conn.close()  // the native side explains why in the status banner
            return
        }
        connection = conn
        device = dev
        onMessage("")
    }

    private fun UsbDevice?.isCamera() = this != null && vendorId == VENDOR_ID && productId == PRODUCT_ID

    companion object {
        const val VENDOR_ID = 0x1514
        const val PRODUCT_ID = 0x0001
    }
}
