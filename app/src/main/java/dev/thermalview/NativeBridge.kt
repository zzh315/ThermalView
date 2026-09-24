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

    /** Debug: pipeline stages as text, e.g. "shutter=0" (native/core parseStages). "" or the error. */
    external fun setPipeline(stages: String): String
    external fun setDisplay(upscaler: Int, paletteJson: String): String  // "" or an error
    external fun requestReadback(prefix: String, palette: String)  // debug: M5's GPU-vs-CPU check
    external fun setViewWidth(px: Int)  // the view's width in panel pixels; 0: the largest 4:3 fit

    /** Stage 3's drift map for a camera serial: the bundled native/core/data/drift_<serial>.f32. */
    external fun registerDriftMap(serial: String, data: ByteArray)

    /** Returns "" on success, otherwise the reason. [base] is the dump path without extension. */
    external fun startReplay(base: String): String
    external fun stopReplay()

    external fun sendShutter(): String
    external fun setOptions(
        skipStartupShutter: Boolean, statsCsv: Boolean, fallbackOrder: Boolean, dumpOnLockout: Boolean,
        autoRange: Boolean, highMathInfiCam: Boolean, lockoutEnabled: Boolean, rangeSettleMs: Int,
        bigCores: Boolean, perfHint: Boolean,
    )
    external fun setRange(high: Boolean): String  // debug: manual range switch
    external fun triggerLockout(): String
    external fun mark(label: String)  // debug: "owner mark: <label>" in the field log
    /** Debug: mark, recalibrate, dump 200 frames; with [rangePair], again in the high range, then back. */
    external fun readyCapture(label: String, rangePair: Boolean): String

    /**
     * Shown readouts: {temp °C, x, y, flags} for high, low and center (camera pixels), then 1 if
     * the high range is active. flags: 1 = valid temperature, 2 = over range. temp is NaN if invalid.
     */
    external fun readouts(): FloatArray
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
