package dev.thermalview

import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
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
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.boundsInRoot
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.min
import androidx.compose.ui.viewinterop.AndroidView
import kotlin.math.roundToInt
import kotlinx.coroutines.delay

private object SurfaceCallbacks : SurfaceHolder.Callback {
    override fun surfaceCreated(holder: SurfaceHolder) = NativeBridge.setSurface(holder.surface)
    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) = Unit
    override fun surfaceDestroyed(holder: SurfaceHolder) = NativeBridge.setSurface(null)
}

/**
 * The screen: the image in its 4:3 view (drawn natively into the SurfaceView), the markers and any
 * banner over it, and the controls in the margins beside it (SideBars.kt). The settings panel opens
 * over the image's right side; a tap on the image closes it.
 */
@Composable
fun AppScreen(
    message: String,
    dumpsDir: String,
    options: DebugOptions,
    onOptions: (DebugOptions) -> Unit,
    paletteColors: (Int) -> IntArray,
    zoomRequest: ZoomRequest? = null,
    onZoomRequestDone: () -> Unit = {},
) {
    var status by remember { mutableStateOf(Status()) }
    var readings by remember { mutableStateOf<Readings?>(null) }
    var showStats by rememberSaveable { mutableStateOf(true) }
    var panelOpen by rememberSaveable { mutableStateOf(false) }
    var page by rememberSaveable { mutableStateOf(Page.Main) }
    var panelBounds by remember { mutableStateOf<Rect?>(null) }  // where the open panel covers the image
    // Frames dropped in the last 10 s, from the stream's running count sampled every 250 ms.
    val droppedHistory = remember { ArrayDeque<Pair<Long, Long>>() }
    var recentDrops by remember { mutableStateOf(0L) }

    LaunchedEffect(Unit) {
        while (true) {
            status = Status.parse(NativeBridge.status())
            val now = System.currentTimeMillis()
            droppedHistory.addLast(now to status.dropped)
            while (droppedHistory.size > 1 && now - droppedHistory.first().first > 10_000) droppedHistory.removeFirst()
            recentDrops = (status.dropped - droppedHistory.first().second).coerceAtLeast(0)
            delay(250)
        }
    }
    val live = status.streaming || status.replay.isNotEmpty()
    LaunchedEffect(live) {
        while (live) {
            readings = Readings.parse(NativeBridge.readouts())
            delay(40)  // a frame's time: the markers keep up with the image when the camera moves
        }
        readings = null
    }
    val view = LocalView.current
    DisposableEffect(status.streaming) {
        view.keepScreenOn = status.streaming
        onDispose { view.keepScreenOn = false }
    }

    // The palette's colors: the swatch (left to right, cold to hot) and the scale bar's table.
    val swatches = remember(options.rainbowStyle) { HashMap<Int, List<Color>>() }
    val swatch = { p: Int ->
        swatches.getOrPut(p) {
            val argb = paletteColors(p)
            if (argb.isEmpty()) listOf(Color.Black, Color.White) else (0 until 16).map { Color(argb[it * (argb.size - 1) / 15]) }
        }
    }
    val scaleLut = remember(options.palette, options.rainbowStyle) { paletteColors(options.palette) }

    // Zoom and pan (PLAN M6): pinch zooms 1x-8x around the fingers, a drag pans, a double tap goes
    // back to 1x. Display-only: the pipeline still processes the whole frame, but the auto range and
    // the readouts measure what's shown.
    var zoom by rememberSaveable { mutableStateOf(1f) }
    var zoomCx by rememberSaveable { mutableStateOf(CamRect.FRAME_W / 2) }
    var zoomCy by rememberSaveable { mutableStateOf(CamRect.FRAME_H / 2) }
    val camRect = CamRect.of(zoom, zoomCx, zoomCy)
    LaunchedEffect(camRect) { NativeBridge.setViewRect(camRect.x, camRect.y, camRect.w, camRect.h) }
    val viewWidthPx = MainActivity.VIEW_WIDTHS.getOrElse(options.viewSize) { 0 }
    val resetZoom = {
        zoom = 1f
        zoomCx = CamRect.FRAME_W / 2
        zoomCy = CamRect.FRAME_H / 2
    }
    // The box (PLAN M6): where it was last, off at each launch; the measurement region and the dimming.
    val context = LocalContext.current
    val prefs = remember { context.getSharedPreferences("settings", android.content.Context.MODE_PRIVATE) }
    var boxOn by rememberSaveable { mutableStateOf(false) }
    var camBox by rememberSaveable(stateSaver = CamBox.Saver) { mutableStateOf(CamBox.load(prefs)) }
    LaunchedEffect(boxOn, camBox, options.boxDim) {
        NativeBridge.setBox(boxOn, camBox.x, camBox.y, camBox.w, camBox.h, options.boxDim)
        if (boxOn) camBox.save(prefs)
    }
    LaunchedEffect(zoomRequest) {
        zoomRequest?.let {
            val r = CamRect.of(it.zoom, it.cx, it.cy)
            zoom = r.zoom
            zoomCx = r.cx
            zoomCy = r.cy
            onZoomRequestDone()
        }
    }

    MaterialTheme(colorScheme = Ui.Scheme) {
        BoxWithConstraints(Modifier.fillMaxSize().background(Color.Black)) {
            val density = LocalDensity.current
            val box = viewBox(constraints.maxWidth, constraints.maxHeight, viewWidthPx)
            val leftBar = with(density) { box.x.toDp() }
            val rightBar = with(density) { (constraints.maxWidth - box.x - box.w).toDp() }

            AndroidView(
                factory = { ctx -> SurfaceView(ctx).apply { holder.addCallback(SurfaceCallbacks) } },
                modifier = Modifier.fillMaxSize(),
            )
            // Gestures on the image only (the bars have their own taps).
            Box(
                Modifier.offset { IntOffset(box.x.roundToInt(), box.y.roundToInt()) }
                    .size(with(density) { box.w.toDp() }, with(density) { box.h.toDp() })
                    .pointerInput(viewWidthPx, box) {
                        // (in this layer's coordinates: the view starts at 0, 0)
                        val local = ViewBox(0f, 0f, box.w, box.h)
                        imageGestures(
                            boxAt = { if (boxOn) camBox else null },
                            rect = { CamRect.of(zoom, zoomCx, zoomCy) },
                            viewW = box.w,
                            viewH = box.h,
                            onBox = { camBox = it },
                        ) { centroid, pan, gestureZoom ->
                            val r = CamRect.of(zoom, zoomCx, zoomCy)
                                .transformed(local, centroid.x, centroid.y, pan.x, pan.y, gestureZoom)
                            zoom = r.zoom
                            zoomCx = r.cx
                            zoomCy = r.cy
                        }
                    }
                    .pointerInput(Unit) {
                        detectTapGestures(
                            onTap = { if (panelOpen) panelOpen = false },
                            onDoubleTap = { resetZoom() },
                        )
                    },
            )
            // A replay runs without the camera, so the "plug in" prompt doesn't apply then.
            val banner = status.banner.ifEmpty { if (status.replay.isNotEmpty()) "" else message }
            ImageOverlay(
                readings, box, camRect, banner, live,
                covered = if (panelOpen) panelBounds else null,
                camBox = if (boxOn) camBox else null,
            )

            // Each bar's column hugs the screen's outer edge (and is at most BarMaxWidth wide).
            Box(Modifier.align(Alignment.TopStart).width(leftBar).fillMaxHeight()) {
            LeftBar(
                modifier = Modifier.align(Alignment.TopStart),
                options = options,
                onOptions = onOptions,
                paletteColors = swatch,
                stats = if (BuildConfig.DEBUG && showStats && live) {
                    { StatsLines(status, recentDrops) }
                } else null,
                zoom = zoom,
                onResetZoom = resetZoom,
                boxOn = boxOn,
                onBox = { boxOn = it },
                replay = if (BuildConfig.DEBUG) status.replay else "",
                onStopReplay = { NativeBridge.stopReplay() },
            )
            }
            Box(Modifier.align(Alignment.TopEnd).width(rightBar).fillMaxHeight()) {
            RightBar(
                modifier = Modifier.align(Alignment.TopEnd),
                readings = readings,
                palette = scaleLut,
                marksLocked = options.palette == 2,  // (the rainbow palettes' lockedAbove / lockedBelow)
                live = live,
                status = status,
                panelOpen = panelOpen,
                onPanel = {
                    panelOpen = !panelOpen
                    if (panelOpen) page = Page.Main
                },
            )
            }
            if (BuildConfig.DEBUG && panelOpen) {
                // Beside the right bar, over the image's right side, as tall as its content allows.
                SettingsPanel(
                    modifier = Modifier.align(Alignment.BottomEnd)
                        .padding(end = rightBar + 2.dp, top = 8.dp, bottom = 8.dp)
                        .width(min(460.dp, with(density) { box.w.toDp() } - 16.dp))
                        .onGloballyPositioned { panelBounds = it.boundsInRoot() },
                    page = page,
                    onPage = { page = it },
                    onClose = { panelOpen = false },
                    dumpsDir = dumpsDir,
                    options = options,
                    onOptions = onOptions,
                    showStats = showStats,
                    onShowStats = { showStats = it },
                )
            }
        }
    }
}
