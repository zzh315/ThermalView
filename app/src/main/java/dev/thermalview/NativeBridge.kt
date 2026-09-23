package dev.thermalview

import android.view.Surface

/** JNI surface of libthermalview (native/android/jni_bridge.cpp). */
object NativeBridge {
    init {
        System.loadLibrary("thermalview")
    }

    external fun init(storageDir: String, appVersion: String)

    /** The caller keeps ownership of [fd] and closes it after [closeCamera]. */
    external fun openCamera(fd: Int, manufacturer: String?, product: String?, serial: String?): Boolean
    external fun closeCamera()
    external fun setSurface(surface: Surface?)

    external fun overlayText(): String

    /** "key=value;..." — see [Status.parse]. */
    external fun status(): String

    external fun startDump(frames: Int): String

    /** Returns "" on success, otherwise the reason. [base] is the dump path without extension. */
    external fun startReplay(base: String): String
    external fun stopReplay()

    external fun sendShutter(): String
    external fun setOptions(skipStartupShutter: Boolean, statsCsv: Boolean, fallbackOrder: Boolean)
}

data class Status(
    val state: String = "",
    val streaming: Boolean = false,
    val banner: String = "",
    val dump: String = "",
    val replay: String = "",
) {
    companion object {
        fun parse(line: String): Status {
            val fields = line.split(';').mapNotNull {
                val parts = it.split('=', limit = 2)
                if (parts.size == 2) parts[0] to parts[1] else null
            }.toMap()
            return Status(
                state = fields["state"].orEmpty(),
                streaming = fields["streaming"] == "1",
                banner = fields["banner"].orEmpty(),
                dump = fields["dump"].orEmpty(),
                replay = fields["replay"].orEmpty(),
            )
        }
    }
}
