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
    external fun setViewWidth(px: Int)  // the image's long side in panel pixels; 0: the largest fit
    external fun setRotation(quarterTurns: Int)  // the image turned clockwise on the screen (M6's orientation)
    external fun setSharpen(strength: Float)  // M7's edge sharpening on the upscaled image (0: off)
    external fun setViewRect(x: Float, y: Float, w: Float, h: Float)  // zoom and pan: camera pixels shown
    external fun setBox(on: Boolean, x: Int, y: Int, w: Int, h: Int, dim: Float)  // M6's box, camera pixels; dim: outside it

    /** Stage 3's drift map for a camera serial: the bundled native/core/data/drift_<serial>.f32. */
    external fun registerDriftMap(serial: String, data: ByteArray)

    /** Returns "" on success, otherwise the reason. [base] is the dump path without extension. */
    external fun startReplay(base: String): String
    external fun stopReplay()

    external fun sendShutter(): String
    external fun setOptions(
        skipStartupShutter: Boolean, statsCsv: Boolean, fallbackOrder: Boolean, dumpOnLockout: Boolean,
        autoRange: Boolean, highMathInfiCam: Boolean, lockoutEnabled: Boolean, rangeSettleMs: Int,
        bigCores: Boolean, perfHint: Boolean, gpuNr: Boolean,
    )
    external fun requestNrCheck()  // debug: stage 4b's GPU against the CPU reference, into the field log
    external fun setRange(high: Boolean): String  // debug: manual range switch
    external fun triggerLockout(): String
    external fun cancelCapture(): String  // a capture's steps or a recording in progress, dropped
    external fun mark(label: String)  // debug: "owner mark: <label>" in the field log
    /** Debug: mark, recalibrate, dump 200 frames; with [rangePair], again in the high range, then back. */
    external fun readyCapture(label: String, rangePair: Boolean): String

    /**
     * Shown readouts: {temp °C, x, y, flags} for high, low and center (camera pixels), then 1 if
     * the high range is active. flags: 1 = valid temperature, 2 = over range. temp is NaN if invalid.
     * Then the scale bar: the temperatures at the ends of the color mapping (NaN: none yet), 1 if
     * the top one is over range, and 1 if the range is locked. See [Readings.parse].
     */
    external fun readouts(): FloatArray

    /** M6's range lock: hold the colors to the temperatures they show now (on), or automatic again. */
    external fun setRangeLock(on: Boolean): String
    external fun setRangeEnds(loC: Float, hiC: Float)  // the locked range's ends, °C

    /** [n] colors (0xAARRGGBB) of a palette file (palettes/, as the display bakes it); empty if it doesn't parse. */
    external fun paletteColors(json: String, n: Int): IntArray
}

data class Status(
    val state: String = "",
    val streaming: Boolean = false,
    val banner: String = "",
    val dump: String = "",
    val replay: String = "",
    val fps: Float = 0f,           // frames a second reaching the pipeline
    val lagMs: Float = 0f,         // latency, frame arrival to the screen: median and 95th percentile
    val lagP95Ms: Float = 0f,
    val dropped: Long = 0,         // frames lost since the stream started (never arrived, or no time for them)
    val dumpDone: Int = 0,         // a recording in progress: frames written of dumpTotal (0 / 0: none)
    val dumpTotal: Int = 0,
    val capture: String = "",      // the capture's current step ("": none), for its button
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
                fps = fields["fps"]?.toFloatOrNull() ?: 0f,
                lagMs = fields["lat50"]?.toFloatOrNull() ?: 0f,
                lagP95Ms = fields["lat95"]?.toFloatOrNull() ?: 0f,
                dropped = fields["dropped"]?.toLongOrNull() ?: 0,
                dumpDone = fields["dumpDone"]?.toIntOrNull() ?: 0,
                dumpTotal = fields["dumpTotal"]?.toIntOrNull() ?: 0,
                capture = fields["capture"].orEmpty(),
            )
        }
    }
}
