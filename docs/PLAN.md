# Plan

Work through the milestones in order. Each one ends with evidence shown to the owner; don't start the next until the owner confirms.

## M0 — Verify devices and environment (no app code)

1. **Mac toolchain.** Android Studio or the command-line SDK, a pinned NDK, CMake, the JDK the Android Gradle Plugin requires, Homebrew `libusb`, and a Python venv in `tools/py` with a `requirements.txt`. Initialize the git repo (the submodules and benchmark results depend on it). Record versions in `docs/DEVICE.md`.
2. **Camera descriptors (camera plugged into the Mac).** Write `tools/py/dump_descriptors.py` (pyusb + libusb) that prints the device, configuration, interface and endpoint descriptors and parses the UVC class-specific descriptors: VideoStreaming format(s) and GUID, frame sizes, frame intervals, and whether streaming uses isochronous or bulk endpoints (with wMaxPacketSize per alternate setting). Standard descriptor reads only — no class or vendor requests. Expect a 256×196 frame at 25 Hz; if the width isn't 256, stop and tell the owner.
3. **Tablet (over wireless adb).** Record `ro.build.version.release`, `ro.build.version.sdk`, `ro.product.model`, `ro.product.device` (expect `dagu`), `ro.build.display.id`, `ro.product.cpu.abi` (expect `arm64-v8a`), and whether `android.hardware.usb.host` appears in `pm list features`. For the display: `wm size`; the panel's xdpi/ydpi and supported display modes (refresh rates) from `dumpsys display`, cross-checked with `dumpsys SurfaceFlinger` (note that `wm density` is the logical density, not the panel's ppi; Xiaomi quotes 244 ppi and 60/120 Hz); and the GLES version from `dumpsys SurfaceFlinger`. Build a hello-world APK and install it over wireless adb to prove the toolchain end to end.
4. **Compatibility matrix.** Install every Android app listed in PRIOR_ART.md under "works" or "plausibly compatible" (the owner can install any APK; InfiCam has no APK and is needed only if InfiCamPlus misbehaves). For each, ask the owner to plug in the camera, then record: connects / streams / crashes / errors, any messages it shows (note which app shows the "T3 device doesn't support HD" message), an `adb exec-out screencap -p` screenshot, the on-screen size of the thermal image in that screenshot, and whether the app offers zoom or area measurement (and how they behave). Record the matrix in PRIOR_ART.md. Apps that stream become reference apps for M3.
5. **Prior-art pass 1** (PRIOR_ART.md): answer the capture and protocol questions with file/function pointers and verdicts, before any capture code is written.

**Acceptance:** `docs/DEVICE.md` lists every fact above with the command that verified it; the compatibility matrix and pass-1 findings are in PRIOR_ART.md. Any contradiction with CLAUDE.md or PROTOCOL.md has been raised with the owner.

## M1 — Capture skeleton, raw dumper and replay

- Android project: Kotlin + Compose; minSdk 31 (the tablet launched on Android 12); targetSdk = the verified API level. CMake native build; libusb and libuvc submodules at pinned commits.
- USB, all per PROTOCOL.md "Android USB notes": attach intent filter and device filter, permission flow, CAMERA runtime permission (and a clear message if the camera privacy toggle blocks it), file descriptor handed to native code, libusb/libuvc wrapped.
- Command gate per CLAUDE.md rule 1, with its unit tests (they run on the Mac under CTest).
- Start sequence (CLAUDE.md rule 1): start streaming at the verified format and frame interval → `0x8004` (InfiRay's demo waits 300 ms after starting the stream) → drop frames until they pass the sanity checks → `0x8020` → `0x8000` about 0.5 s later → drop frames through the shutter cycle, until 10 fresh frames follow the freeze (8 s cap). If the stream began with repeated frames, the camera is calibrating after power-up (DEVICE.md): skip the `0x8000` and hold until it finishes. If streaming first gives trouble, switch to the approved fallback (`0x8004` and `0x8020` before streaming) and log why.
- The frame callback copies into a small lock-free ring and returns immediately. The ring never overwrites: if processing falls behind, the new frame is dropped and counted as an overrun, so "process every frame" can be checked. Process every frame; render the latest, handed over through a triple buffer. A dedicated native thread runs libusb's event loop (PRIOR_ART.md, pass 1 items 1 and 6).
- Stall watchdog and field log (PRIOR_ART.md, pass 1 item 6). If no frame arrives for 0.5 s (tunable) while streaming, log it, show a banner, and restart the stream through the normal start sequence. A small rotating on-device log records connects, commands, drops, stalls and restarts, and is pulled with adb.
- Frame checks: accept only complete 256×196 frames that pass the sanity checks (PROTOCOL.md); drop the rest and count them as dropped. If frames still fail once start-up is over, stop and tell the owner: the camera may be streaming uncompensated data, which breaks the temperature inputs.
- Baseline preview: per-frame min/max linear stretch of the 14-bit image to grayscale, nearest-neighbor, drawn with GLES. This is the benchmark baseline, so keep it reachable behind a debug toggle permanently.
- Orientation: with the owner, check that the image isn't mirrored or rotated relative to the scene (e.g. they raise their right hand). If it is, fix it with one fixed transform in the renderer, and map overlay coordinates the same way. No UI setting unless the owner asks.
- Keep the screen on while streaming.
- Debug overlay: fps, frame-interval jitter, dropped frames (gaps in libuvc's `frame->sequence`, which reveal frames the callback never saw; arrival gaps longer than 1.5× the nominal interval, which reveal frames lost on the bus; and frames the checks rejected), app-side latency (p50/p95), frame size, decoded metadata (FPA, shutter and core temperatures, cal_00–cal_05, firmware/serial/product strings, user-area values), shutter-cycle count, command log.
- Shutter-cycle characterization (PROTOCOL.md "Runtime behavior"): log per-frame metadata and frame statistics across several `0x8000` cycles. Then stream for at least 15 minutes with no further `0x8000` to learn whether the camera ever cycles by itself; in one run, leave out the start-up `0x8000` to learn whether `0x8020` alone triggers a cycle (the owner listens for the click). With the camera facing a uniform surface, log the FPA temperature and the growth of fixed-pattern noise since the last shutter — M4 sizes the recalibration policy from this. Record in `docs/DEVICE.md` the actual frame rate, cycle behavior and duration, what frames look like during a cycle, and any metadata field that flags one.
- Debug-only raw dumper: saves N consecutive frames (default 200) to app-specific external storage as `<name>.raw` (concatenated little-endian uint16 frames, the full 256×196 including metadata) plus `<name>.json` (width, height, metadata_rows = 4, frame count, per-frame timestamps, device strings, app version, command log). Pulled with `adb pull`. AOSP's adbd lets the shell read `/sdcard/Android/data` on Android 13–14, but confirm it on the tablet first.
- Debug-only replay: plays a dump into the capture ring at its recorded frame timing, looping, so everything downstream (pipeline, readouts, GPU, UI) runs exactly as it does with the camera. No camera needed; dumps from the Mac arrive with `adb push`.
- `tools/py/raw2npy.py`: converts a dump into per-frame `.npy` arrays of shape (196, 256), dtype uint16 — the format ht301_hacklib's `CameraEmulator` loads.

**Acceptance:** the metadata sanity checks pass, the firmware string decodes as readable ASCII, and the serial and product strings are located (PROTOCOL.md); 25 fps for 10 minutes with no drops or crashes; clean unplug/replug and background/foreground; the command log shows only allowlisted values; shutter-cycle behavior is recorded in DEVICE.md; a replayed dump plays through the preview.

## M2 — Temperature decode + golden tests

- `native/core`: a frame view (image and metadata accessors) and the temperature LUT (PROTOCOL.md math, width-256 constants), computed in double precision.
- `tools/harness temps <dump>`: per-frame low/high/center °C, plus the LUT.
- `tools/py/oracle.py`: runs a vendored copy of ht301_hacklib's `CameraEmulator` (not IR-Py-Thermal's, which is broken at its HEAD) on the same frames, one frame file per emulator. Never construct its `Camera()` class: it opens any attached camera and sends `0x8004` and `0x8000` outside our gate. Pin numpy and opencv-python-headless in `requirements.txt` — the oracle's LUT is float32 under numpy 1.x and float64 under 2.x. GPL, fine for personal use; credit it in THIRD_PARTY.md.
- Golden test: every LUT entry matches the oracle within 0.01 °C (NaN matches NaN) on at least three dumps — a cold scene, a room, and a hot object around 80–100 °C. This includes the folded entries below the LUT's vertex (PROTOCOL.md), even though the app never displays them.
- On-device readouts over the measurement region (the whole frame until M6 adds zoom and the box): low and high (value plus a marker at the pixel), and the crosshair at the region's center. Compute them from raw values with our own argmin/argmax, excluding pixels in the bad-pixel map (empty until M4 stage 2 builds it). Raw values below the LUT's vertex (where it bottoms out), and NaN entries, show as "--". The crosshair value is the mean of the 1–4 pixels nearest the region's center. A low/high marker moves only when a new extreme beats the marked pixel by a small margin (tunable; start around 0.2 °C), so it doesn't jitter between near-equal pixels. Show the camera's own min/max/center fields in the debug overlay for comparison. Smooth displayed numbers with an EMA (~0.3 s), one decimal.
- Device check: replay one of the golden-test dumps on the tablet; the unsmoothed on-device readouts match `tools/harness temps` for the same frames.
- Real-world cross-check: the same static scene read by Xtherm, Hti Image and our app, one at a time (replug between apps), with the center on a uniform surface several pixels wide. Leave each app's emissivity and other environmental settings at their defaults and note them; if they differ from the user-area values our overlay shows, the comparison isn't like for like — sort that out first. Our center value should agree with theirs within ±1 °C. If it doesn't, stop and investigate before M3 — the width-256 constants are the first suspect. The table policies agree right after a shutter cycle but can drift apart between cycles: the vendor apps refresh their table only after a shutter, while we rebuild it every frame (PROTOCOL.md). Compare at both times, and keep whichever policy tracks the references better.
- Absolute check: the apps all share the same vendor-derived math, so they can agree with each other and still be wrong. And the seller claims −20 to 120 °C, while the only S0H listing found specifies measurement over 30–45 °C only (CLAUDE.md). So compare against a contact thermometer (a kitchen probe is fine) across the use-case range: ice water, warm (~40 °C) and hot (~80 °C) water in a mug. Note the camera's emissivity setting: water is ~0.96, and a 0.02 mismatch is worth roughly 1 °C at 80 °C and 0.5 °C at 0 °C. Record the errors per temperature. If they're large outside 30–45 °C, stop and discuss with the owner — rule 2 leaves no room for a quiet correction.

**Acceptance:** golden tests pass under CTest on the Mac, the replayed dump matches on the device, the cross-check is within tolerance, and the absolute-check errors are recorded and reviewed with the owner.

## M3 — Benchmark set + harness metrics

Scenes (the owner captures them; use a fixed mount for the static ones):

| ID | Scene | Tests |
| --- | --- | --- |
| `hand` | open hand ~40 cm in front of a room-temperature wall | edges, halos |
| `motion` | a hand moving slowly across a room-temperature wall | ghost trails from temporal denoise |
| `pcb` | powered PCB at 20–30 cm | fine detail, small hot parts |
| `floor` | underfloor heating, or a wall with hot-water pipes | low ΔT, broad gradients |
| `room` | room interior | wide dynamic range |
| `night` | outdoor night scene with trees or grass (handheld is fine; never the sun; note the air temperature — the S0H listing rates the module for 10–40 °C ambient) | low contrast, sky, motion, foliage flicker |
| `flat` | camera facing a uniform surface | noise and stripe measurement |

For each scene: a 200-frame dump from our debug build, plus references from every app that streams the camera in the M0 matrix — an `adb exec-out screencap -p` screenshot of the same view and a 10-second `adb shell screenrecord` clip to show temporal behavior (noise crawl, AGC flicker, shutter hiccups, ghosting). Store them in `bench/<scene>/`. Raw dumps and recordings are large, so `bench/**/*.raw` and `bench/**/*.mp4` are gitignored and kept locally; commit sidecars, ROI files, screenshots and results.

Metrics (`tools/harness bench` → `bench/results/<git-sha>.json`, suffixed `-dirty` for uncommitted code, plus contact sheets):

- `temporal_noise` — median per-pixel temporal std on `flat`, in display units and in °C.
- `stripes` — std of column means and of row means of the time-averaged `flat` frame, after removing a low-order 2D polynomial.
- `sharpness` — edge acutance on marked edge ROIs in `hand` and `pcb` (`bench/<scene>/roi.json`).
- `overshoot` — max overshoot beyond the plateau across the `hand` edge profile (halo detector).
- `flicker` — std over time of the mean display intensity on static scenes (AGC stability).
- On device: fps, p95 processing time, app-side latency.

Contact sheet per scene: baseline | our pipeline (both palettes) | a screenshot from each reference app, with ours rendered at the on-screen size that app shows the image at (measured in M0). Temporal effects don't show in stills, so the harness also renders side-by-side clips (PNG frames → `ffmpeg`) to review against the reference apps' recordings. The owner picks the best reference app per scene; that pick is the bar to beat for that scene.

**Acceptance:** results exist for the M1 baseline, the owner has reviewed the contact sheets and clips, and the per-scene best reference is recorded.

## M4 — Image pipeline v1

Lives in `native/core`. Start with **prior-art pass 2** (PRIOR_ART.md) before implementing any stage. Every stage is toggleable in debug builds and benchmarked, and its PIPELINE_LOG entry names the approach it adopts or improves on. Stages 1–6 affect the display only; readouts always use raw values (they consult the bad-pixel map only to exclude pixels).

1. **Shutter-cycle handling.** Detect shutter cycles from repeated identical frames (M1: 30–31 per cycle, 1.23–1.27 s, whether or not we commanded it) and hold the last good output. Afterwards, blend back over ~0.3 s instead of jumping. Readouts also hold their last values during a cycle, because a frozen frame gives stale numbers.

   Recalibration policy (the owner approves the numbers; until then only start-up and the Recalibrate button send `0x8000`):
   - The camera calibrates itself only in its first ~65 s after power-up (DEVICE.md). After that, drift correction is up to us.
   - Propose a trigger from M1's drift measurements: FPA-temperature change since the last cycle, whether ours or the camera's, plus a time cap. The fixed pattern didn't grow over 14 minutes at a steady FPA, which is why temperature change comes first.
   - Starting points: the vendor's 380 s, InfiCamPlus's warm-up ramp (7.5 s → 180 s) and the camera's own power-up schedule. Never below 10 s.
   - Don't fire during the camera's power-up series.
2. **Bad pixels.** Build a software map from temporal statistics (stuck or abnormally noisy relative to the 3×3 neighborhood, persistent across many frames). Replace them with the neighborhood median. Never written to the camera.
3. **Residual stripes.** Estimate per-column and per-row offsets from the high-pass component, heavily smoothed over time, and subtract them. Keep this stage only if `stripes` improves on `flat` without eroding real straight edges in `pcb`.
4. **Temporal denoise.** A per-pixel recursive filter with motion adaptation: the blend weight comes from |current − filtered| relative to the noise level measured on `flat`. Reset on shutter cycles and on large global motion. Goal: big `temporal_noise` gains on static scenes with no ghost trails in `motion`.
5. **Tone mapping.**
   - Auto: statistics come from the measurement region (M6). Robust range from percentiles (start at 0.3% / 99.7%), never narrower than a minimum span centered on the region's range (tunable; tune on `flat` and `floor`), so a uniform area shows calm colors rather than stretched noise; plateau histogram equalization blended with linear (blend tunable); temporal smoothing of the mapping — expand fast, contract slowly, with a small deadband — to kill flicker. When the region itself changes (box moved or resized, zoom, pan), retarget at once and ease over ~0.3 s. The mapping applies to the whole frame, so pixels outside the region's range clip to the palette ends. FLIR's AGC (box-driven histogram, "Max Gain", damping) and thermal-cat's hysteresis controller are the closest prior art (PRIOR_ART.md).
   - Manual: user-locked low/high °C, converted to raw through the monotonic part of the current LUT; linear inside the range, clipped outside. The measurement region doesn't affect a manual range.
   - The harness takes `--box x,y,w,h` (camera pixels); `pcb` (around one chip) and `floor` (only the floor) get a second benchmark case with the box from `bench/<scene>/box.json`.
6. **Detail enhancement.** Guided-filter base/detail split on the tone-mapped signal. Compress the base, amplify the detail with a gain that tapers where local detail is near the noise floor (so noise isn't boosted), and clamp to prevent halos.
7. **Output** float intensity in [0, 1] at 256×192 to the GPU. Never quantize to 8 bits before upscaling.

Tuning rule: add one stage at a time, run `tools/harness bench`, log the numbers and a contact sheet in `docs/PIPELINE_LOG.md`, and keep only the improvements the owner approves.

**Acceptance:** every stage has a PIPELINE_LOG entry (metrics before and after, contact sheet, clips where temporal, the owner's verdict), and only approved stages are enabled.

## M5 — GPU display

- Upload the float intensity as a single-channel texture each frame: R32F, read with `texelFetch`. The upscalers do their own filtering, so it doesn't matter that GLES can't filter R32F without `OES_texture_float_linear`.
- The renderer draws into a 4:3 view rect centered on the landscape screen, sampling a sub-rectangle of the frame (the whole frame at 1× zoom). The view size and zoom come from M6's controls; until then, from a debug setting. The upscaler renders straight into the view rect at native panel resolution — no intermediate scaling step.
- Upscale the scalar intensity in the fragment shader. Implement Catmull-Rom bicubic and Lanczos-3 with an anti-ringing clamp (clamp to the min/max of the nearest 2×2 source texels), plus anything on the pass-2 shortlist, then choose by benchmark and owner review at every view-size preset and at high zoom — the best kernel may differ by scale. Also compare the presets with nearby integer scales (5×, 6×, 8× on this panel); if integer scales look cleaner, snap the presets to them.
- Apply the palette after upscaling, through a 1024-entry LUT stored as a 1024×1 2D texture (GLES has no 1D textures). Interpolating the scalar first and mapping second avoids the color fringes you get from interpolating RGB.
- Palettes — control points in `palettes/*.json`, baked into the 1024-entry LUT:
  - `white_hot`: black (cold) → white (hot), interpolated in OKLab with lightness linear in intensity so equal steps look equal.
  - `rainbow_hc`: a continuous rainbow gradient, never discrete bands — cold = green, mid = yellow, hot = red, sweeping smoothly through every hue in between (yellow-green, then orange). Interpolate along the hue arc in OKLCh with chroma kept high, gamut-clipped to sRGB, so the in-between hues stay vivid; straight-line OKLab or RGB mixing would dull them. Starting control points: 0.0 green `#00FF00`, 0.25 yellow-green `#80FF00`, 0.5 yellow `#FFFF00`, 0.75 orange `#FF8000`, 1.0 red `#FF0000`. The owner tunes hues and stop positions by eye; the hue order stays green → yellow → red.
  - For tuning, `tools/harness palette` renders each palette as a gradient strip plus applied to the benchmark frames.
- CPU reference: `native/core` mirrors the display path (upscalers, palette LUT, dimming outside the box), so the harness renders contact sheets at any on-screen size. The shaders must match it: replay a dump, read a frame back with `glReadPixels`, and compare with the harness output for the same frame (start with a tolerance of 2/255 per channel).
- Overlays at display resolution: low/high markers with values, the crosshair with its value, the box outline with its edge handles, dimming outside the box, and the palette scale bar with °C endpoints (endpoints only: with histogram equalization the mapping isn't linear, so evenly spaced labels in between would be wrong). Overlay positions come from camera coordinates, so they follow view size and zoom; glyphs and text stay a fixed physical size. The scale bar sits in the margin beside the image when it fits there, otherwise along the image's edge.
- Render when a new camera frame arrives, paced with Choreographer. Neither 60 nor 120 Hz is a multiple of 25, so pacing can't be perfectly even. At 120 Hz frames alternate between 4 and 5 refreshes (33 or 42 ms); at 60 Hz, between 2 and 3 (33 or 50 ms). So keep the panel at 120 Hz (e.g. via `preferredDisplayModeId`; the tablet's own refresh-rate setting may override it). Don't just call `ANativeWindow_setFrameRate(25, FIXED_SOURCE)`: with no multiple of 25 available, the system picks by "minimal error" and may choose 60 Hz. If M0 finds a 50 or 100 Hz mode, use it instead, and record the mode actually in use in DEVICE.md.

**Acceptance:** on-device screenshots of every benchmark scene (via replay), reviewed by the owner next to each scene's best reference app at the matching on-screen size; the GPU-vs-CPU check passes; performance budget met.

## M6 — UI

Landscape-first, full-screen, minimal chrome; controls auto-hide (tap the image to show them).

- **Controls:** palette toggle; Auto / Manual range (Manual = "lock current range" plus a range slider in °C); view size; Box on/off; Recalibrate (sends `0x8000` through the gate and stays disabled for 10 s).
- **View size:** the image always keeps its native 4:3 shape — no crop, no stretch. One button cycles Phone → Small tablet → Full, each defined as the image's physical diagonal using the panel density verified in M0. Starting values: Phone ≈ 4.5" (what a ~6.5" phone shows held sideways), Small tablet ≈ 7.5", Full = the largest 4:3 fit (2133×1600 px, 10.9" at the verified 244.5 dpi). At that density, Phone is about 880×660 px. The owner tunes them by eye. A smaller view adds no detail; it looks sharper because each camera pixel appears smaller, like viewing from farther away.
- **Zoom:** pinch on the image zooms 1×–8× (tunable), keeping the point under the fingers fixed; re-baseline when a finger is added or lifted. Two fingers pan while pinching; when zoomed, a one-finger drag pans too, unless it starts on the box. Pan is clamped so the image always fills the view. Double-tap returns to 1×. Zoom is display-only: the pipeline always processes the full frame. Zooming adds no detail — it enlarges camera pixels — but the measurement region shrinks with the view, which focuses the colors and readouts.
- **Box:** the Box button shows or hides a rectangle for measuring one area. It appears where it was last (initially centered, half the view's width and height). Drag handles on its edges and corners resize it; dragging inside it moves it. Touch targets stay large even when the box is small (hit-testing and minimum-size handling as in the Android croppers listed in PRIOR_ART.md). It snaps to whole camera pixels, is at least 4×4 of them, and is stored in camera coordinates, so view size and zoom don't disturb it. Outside the box, the image is dimmed (start at 50% brightness; tunable), keeping the box's color mapping — those pixels often clip at the palette ends.
- **Measurement region:** the visible area (the whole frame at 1× zoom), intersected with the box when it's on. If the box is entirely out of view, it's ignored (and nothing is dimmed) until it's back in view. The auto range (M4 stage 5), the low/high markers and the scale bar come from this region, and the crosshair sits at its center — so with the box on, you get the box's hottest point, coldest point and center, and nothing from outside it. "Lock current range" locks whatever range is showing, so box → lock → box off keeps the box's range for the whole frame. Readouts still come from raw values (CLAUDE.md rule 2).
- **Persistence:** the palette, the view size and the box's last position and size are kept across launches; each launch starts in Auto range, at 1× zoom, with the box off.

**Acceptance:** every control works as specified, live and on replay; a 100 mm bar drawn using the verified density measures 100 mm with a ruler (the owner checks); the performance budget still holds with the box on and zoomed in.

## M7 — Final tuning and sign-off

A final tuning pass on all benchmark scenes, at every view-size preset, then owner sign-off: in the owner's judgment, our output beats each scene's best reference app.

## Later — ask the owner before starting any of these

- **Focus assist:** a live sharpness score or edge-peaking overlay while the owner turns the focus ring. Cheap to build, and focus is the biggest real-world sharpness factor.
- **Scene presets:** PCB / underfloor heating / outdoor night, each mapped to tuned pipeline parameters.
- **Neural super-resolution** (display-only experiment): must never feed readouts, and must pass a halo and hallucination review.
- Snapshots and video (currently out of scope).
