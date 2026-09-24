# Prior art — apps for this camera family, and image-processing sources

This repo is written from scratch. We harvest the best *approaches* from every app that works with this camera (or speaks its protocol), and from any image-processing source. Every adoption has to earn its place on the benchmark.

## Ground rules

- **Open-source code:** read the code, adopt ideas, and adapt small, well-understood pieces into our architecture with attribution in THIRD_PARTY.md, within each license (personal use). No forks and no bulk copying. Code without a license is ideas only.
- **Closed-source apps:** learn only by observing them — their output (screenshots, screen recordings), features, and behavior with the camera. Never decompile or disassemble them, or extract their code or assets.
- **Decompilation by others** (owner decision, 2026-09-24): open-source projects built partly by decompiling vendor libraries (InfiCam, InfiCamPlus, and ht301_hacklib's math through them) count as open source. InfiRay's SDK demo source in InfiCam's early history may be read for protocol facts only. Never copy vendor code; note provenance in each entry.
- **Image-quality algorithms:** any source — papers, and open-source code for any sensor or for image processing in general. Harness experiments may run third-party code; what ships is adapted into our architecture.
- Clone open-source repos into a scratch directory outside this repo.
- Every adopted approach gets an entry under Findings: source → what it does → verdict (adopt / adapt / reject) → why, plus benchmark evidence (a PIPELINE_LOG link) where it touches image quality. Rejections get a one-line reason.

Commits below are the ones checked on 2026-09-24.

## Catalog

### Works with this exact camera (verified in the M0 matrix) — the reference apps

| App | Type | Notes |
|---|---|---|
| Hti Image (`com.hti.Xtherm`) | closed, Android | The owner's current best image. Play Store: v6.4.20240702 (July 2024) by WIFI-VIEW (hti-meter.com), "HT301 thermal imager supporting software". Its package name and versioning suggest it's derived from Xtherm's code base and tuned for the HT-301; a 2020 Xtherm review calls the two apps identical. Two 2022 reviews of v6.3 say it has no manual or locked range; check whether 6.4 added one. Primary image-quality reference. |
| Xtherm (`com.infiRay.Xtherm`) | closed, Android | Official InfiRay app; its listing says "APP for T3s/T3pro", and pressing its HD button with our camera says "T3 device doesn't support HD function". Point/line/surface measurement and 6 environment corrections (listing). Removed from Google Play (404 since 2025); Xiaomi's store carries v7.0.260325 (March 2026). The owner finds its image weaker than Hti Image. Temperature cross-check reference (M2). Per InfiCam's notes and InfiRay's demo, it sends `0x8000` 1 s after connect and then every 380 s, and rebuilds its temperature table after each shutter. |
| InfiCamPlus — github.com/diminDDL/InfiCamPlus @ `6fad1f3` | open (MIT), Android, Java + C++ | Its device list (taken from Xtherm's, per PR #17) accepts product names containing "S0" or "Xmodule", and "S0H-40" from "Infiray" takes its V1 (onboard-processed) path — confirmed in M0 (`raw=0`, steady 25 fps). Its V2 support comes from Ghidra work on Xtherm's libraries (PR #17). The project targets "V2" raw-sensor cameras: V1 T2S+/T2L users saw readings ~40–45 °C too high on 1.0.x, and the maintainer points them to the original InfiCam (issue #2); v1.0.4 crashed on a T2S+ V1 (#19). HEAD rejects the HT-301; older builds crashed on it (issue #5). |

### Tested in M0, doesn't work with this camera

| App | Type | Result |
|---|---|---|
| 天眼热成像 (formerly 艾睿天眼) / Xinfrared (`com.infiRayX.Search`, English name infiRaySearch) | closed, Android | InfiRay's app for the Search / Eagle Eye series, with the MATRIX IV algorithm and an "X-Clear" outdoor enhancement mode (listings); not on Google Play, Xiaomi store v4.4.250120. It never recognizes the camera — its USB filter omits it — so it's no night-scene reference. |
| ThruTracker Recorder — github.com/ThruTrackerAnalytics/thrutracker-recorder (README and CHANGELOG only) | closed (free through 2027), Android 10+ | v1.1.0 (2026-07-25) added experimental support for VID:PID 1514:0001, assuming the HT-301's 384×292 geometry. It streams from our camera but rejects every 256-wide frame, so it shows nothing. Its capture approach is still worth learning from (pass 1 item 6). |

### Not tested

| App | Type | Why |
|---|---|---|
| 专业级热像仪 / Xtherm Power (`com.infiRay.XthermPower`) | closed, Android | Same developer as Xtherm, but its listing targets InfiRay's industrial DX-series cameras (point/line/box measurement, −20–550 °C). Skipped by the owner's choice. |
| InfiCam — gitlab.com/netman69/inficam @ `531aa81` | open (MIT), Android, Java + C++ | No published APK, so it needs a source build (old Gradle, targetSdk 27). Built for onboard-processed ("V1") cameras. Not needed: InfiCamPlus works. |

### Same protocol, desktop only — source to read, not run

| Project | License | Take |
|---|---|---|
| ht301_hacklib — github.com/stawel/ht301_hacklib @ `2f1498d` | GPL-3.0 | Temperature math (the M2 oracle), frame and metadata layout, command values. Its matplotlib viewer draws a box that limits only the min/max markers (the color range still uses the whole frame), auto range with 2 °C hysteresis, 'a' to lock the current range, and manual center + span in 1 °C steps. |
| IR-Py-Thermal — github.com/diminDDL/IR-Py-Thermal @ `edf7d56` | GPL-3.0 (fork of ht301_hacklib) | Adds T2L/T2S+; a raw mode that does sensor processing in software for cameras without onboard compensation (shutter reference frame, dead-pixel inpainting); a temperature-offset option. Its emulator is broken at HEAD. Its history holds a real T2S+ v2 frame and vendor log (PROTOCOL.md). |
| xtherm-python — github.com/lamnguyenvu98/xtherm-python @ `816c3d0` | none | Lineage only: carried InfiCam's math into ht301_hacklib. |
| InfiRay XthermDemo SDK — InfiCam history, commit `7028129` | vendor source, license unknown | Facts only, never code (owner decision). The official flow: stream first, `0x8004` 300 ms later, `0x8000` 1 s after connect and every 380 s, a temperature-table refresh after each shutter, and the range-change timings (PROTOCOL.md). Its device-name check includes "S0". |
| IRCAM Thermal Viewer (RMG Engineering, rmg-engineering.com) | closed, Windows 10/11, licensed (free trial) | Explicitly lists the InfiRay S0 series and the HTI HT-301. Not testable in this setup (no Windows PC); skip unless the owner gets access to one. |

### Other thermal viewers (different protocol — techniques only)

| Project | License | Take |
|---|---|---|
| InfiRayProPyCapture — github.com/saschaludwig/InfiRayProPyCapture @ `a038e88` | MIT | P2 Pro, PySide6. Preview interpolation Nearest / Linear / Cubic / Lanczos / Sharp, where Sharp = ESPCN ×4 (OpenCV dnn_superres) on the palette-mapped 8-bit image, then an unsharp mask and a cubic fit. Used for preview and exports, never for measurement. Manual range with "Set to current", a 50-bin histogram, 60 s min/max/avg/center history. |
| thermal-camera-android — github.com/fbreitwieser/thermal-camera-android @ `3284ca0` | none (ideas only) | P2 Pro / TC001 on Android via libusb + libuvc (`NO_DEVICE_DISCOVERY`, then `uvc_wrap`). Scale lock copies the current min/max; manual range dialog. Marker hysteresis: a min/max marker moves only when beaten by 0.1–0.5 °C. Pitfalls: readouts quantized to 8 bits, all processing inside the libuvc callback, zoom pivots on the view center. |
| p2pro-rs — github.com/mbuesch/p2pro-rs @ `745aa2f` | MIT | P2 Pro viewer in Rust/Dioxus: Linux via V4L2, Android 9+ via its own rusb UVC driver on the UsbManager fd (no libuvc). Auto range = frame min/max through a 30-frame moving average, 0.1 °C minimum span; min and max can be pinned separately. Good pinch/pan model: anchor under the fingers, re-baseline on finger changes, clamped pan. Never copy: it writes emissivity and other settings to the camera on every connect. |
| thermal-cat — github.com/alufers/thermal-cat @ `870c380` | GPL-3.0 (stated in README only) | Temperature histogram and marker-temperature-over-time charts (debug-overlay ideas). A hysteresis auto-range controller: persistence timer, eased retarget, 5 °C minimum span — with two bugs (the timer adds twice; the span is added only to the max). A user tone curve. |
| P2Pro-Viewer — github.com/LeoDJ/P2Pro-Viewer @ `2388728` | MIT | P2 Pro, Windows. Source of the "commands hang unless the stream is open" note (PROTOCOL.md). |
| GetThermal — github.com/groupgets/GetThermal @ `2a744e4` | MIT | FLIR Lepton / PureThermal. Exposes the Lepton AGC parameters (ROI, max gain, damping); its spotmeter ROI lives in sensor coordinates. |
| p3-ir-camera — github.com/jvdillon/p3-ir-camera @ `e3205dc`; thermal-camera-viewer — github.com/skywalker1905/thermal-camera-viewer @ `512b46e` | Apache-2.0 | 1st/99th-percentile AGC with an EMA and no minimum span. The viewer's drag-box ROI (min/max/avg) is stored in widget coordinates, so it slides off the scene on zoom — a pitfall to avoid. p3-ir-camera denoises before measuring, so its readouts lag. |
| OpenIRV — github.com/OVGN/OpenIRV @ `d076180` | Apache-2.0 (FPGA) | Histogram AGC that trims 1/64 of the span at each end and moves min/max only past a 3-count deadband "to prevent undesirable image flickering". |

### Image-processing sources (any device)

| Source | Take |
|---|---|
| FLIR Boson "Camera Adjustments" application note (102-2013-100-01) and Lepton software IDD (110-0144-04) | The closest model for box-driven AGC: only the ROI builds the histogram ("this does not mean the portion outside of the ROI is not displayed, just that the portion outside does not factor into the optimization"). "Max Gain" caps the mapping's slope (a minimum span); plateau value; tail rejection under 1 %; damping as an IIR on the transfer function. |

Pass 2 adds the strongest papers and code per pipeline stage here.

### UI references (box gestures)

| Project | License | Take |
|---|---|---|
| Compose-Cropper — github.com/SmartToolFactory/Compose-Cropper @ `e035b95` | MIT | Compose-native: corner and inside hit regions sized by the handle, minimum size of two handles, outside dimmed with a BlendMode layer. |
| Android-Image-Cropper — github.com/CanHub/Android-Image-Cropper @ `e10601a`; uCrop — github.com/Yalantis/uCrop @ `f788b53` | Apache-2.0 | Hit-test within a touch radius, keep the finger's offset from the edge, enforce a minimum size, dim the outside. |

## Compatibility matrix (M0)

| App | Version | Connects | Streams | Messages / errors | Image size on screen | Zoom / area tools | Notes |
|---|---|---|---|---|---|---|---|
| Hti Image (`com.hti.Xtherm`) | 6.4.20240702 (targetSdk 34) | Yes | Yes | None beyond the USB prompts | 1980×1508 at (290, 60); aspect 1.31, slightly off 4:3 | No zoom. Point, line and box tools; the box confines hottest/coldest/center but doesn't change the colors | **Reference app.** Rainbow-type palette by default, no readouts until a tool is picked. Its log shows libuvc negotiating 100352-byte frames, 524-byte payloads, 25 fps, with 192 packets per transfer, and OpenCL initialization |
| Xtherm (`com.infiRay.Xtherm`) | 7.0.260325 (targetSdk 34) | Yes | Yes | "T3 device doesn't support HD function" when its HD button is pressed | 2010×1508 at (120, 60); 4:3 | Pinch zoom, but it won't go below 2× once engaged. Point, line, box and grid tools; the box shows max/avg/min but doesn't change the colors. The scale bar's end markers drag to limit the range, and pixels beyond a marker turn grey | **Reference app.** Ironbow palette; hottest, coldest and center readouts by default. Its log is flooded with MediaPlayer errors. It asks for USB permission on launch if the camera is already attached |
| InfiCamPlus (`be.ntmn.inficam`) | 1.0.5 (targetSdk 34) | Yes | Yes, 25.1–25.2 fps received and delivered | None beyond the permission prompts (CAMERA first, then USB) | 2053×1540 at (254, 60), to the bottom edge; 4:3 | Pinch zoom from 1×. No box tool. The lock freezes the current color range (the owner found it useful) | **Reference app.** Takes its V1 path (`raw=0`). Its log decodes our metadata: cal constants a=0.2333, b=27.867, ka=0.00004, kb=0.0053, kc=0.5351 at Q+3…Q+11; shutter 3050 (K × 10) = 31.85 °C; FPA 32.26 °C. It writes its own defaults (emissivity 0.95, 20 °C, humidity 0.5, distance 1) into the user area; the camera was replugged afterwards. Render time 17–27 ms per frame. Shares the original InfiCam package name, so the two can't coexist |
| ThruTracker Recorder (`com.thrutracker.thermalrecorder`) | 1.1.0 (targetSdk 35) | Yes | Streams, but 0.0 fps shown | Status line "HTI HT-301 · Streaming · 0.0 fps" | — | — | **Doesn't work.** It identifies 1514:0001 as the HT-301 (384×292) and rejects every frame ("bad frame size 100352 (expected 224256)"). It sends `0x8004` through the camera terminal's Zoom (Absolute) (entity 1) before streaming, and runs 16 URBs × 32 packets × 524 bytes on alt 1 |
| 天眼热成像 (`com.infiRayX.Search`) | 4.4.250120 (targetSdk 34) | No | No | "No device detected"; "Please enable OTG and plug in hardware", even after "Device inserted" and granting permissions | — | — | **Doesn't work.** It isn't offered in Android's USB chooser, so its device filter omits this camera. It's built on saki4510t's UVCCamera library |
| Xtherm Power (`com.infiRay.XthermPower`) | — | — | — | — | — | — | Not tested (owner's call): its listing targets InfiRay's DX-series cameras |

On plug-in, Android's app chooser lists the apps whose USB filters match this camera: Hti Image, Xtherm infrared, InfiCam (InfiCamPlus) and ThruTracker Recorder. Tested 2026-09-24 with `adb exec-out screencap -p`, `adb logcat --pid`, and `tools/py/image_rect.py` for the image rectangles. Half-size screenshots are in [device/matrix/](device/matrix/).

## Pass 1 — capture and protocol (during M0)

Answer each with file/function pointers and a verdict. Answered under Findings → "Pass 1 (M0, 2026-09-24)".

1. InfiCam `UVCDevice.connect()` / `InfiCam.connect()`: how the `UsbManager` file descriptor reaches libusb/libuvc, which libusb options are set, and how the format, frame size, frame interval and isochronous alternate setting are chosen.
2. InfiCam's start-up command sequence and order. Anything outside our allowlist is noted, never adopted.
3. InfiCam's disconnect and replug handling, including the libuvc `stream.c` fixes its notes mention.
4. How InfiCam handles the metadata rows and width-dependent offsets, and any code paths specific to width 256 or to PID 0x0001.
5. Why InfiCamPlus crashed on the HT-301 (issue #5, PID 0x0001): which code path, and whether it would also hit a 256-wide 0x0001 device.
6. ThruTracker, from its README, CHANGELOG and release notes (behavior only): a native C loop reaping USB transfers for a steady 25 fps (v1.0.6), a field log and a stall-warning banner on Android (v1.0.5). Its stall/freeze watchdogs with auto-recovery are listed under Windows in the CHANGELOG. Decide what our capture layer adopts.

## Pass 2 — image and display (at the start of M4)

1. Black-box analysis of the M3 reference captures: for each scene, which app looks best and why — sharpness, noise, halos, contrast, AGC flicker, visible shutter hiccups (use the screen recordings for anything temporal).
2. InfiCam's display path. It colorizes to 8-bit on the CPU first, so its scaling and sharpening shaders work on colors — the opposite of our scalar-first design. Compare its shaders: the one its code calls "bicubic" (a smoothing cubic B-spline built from 4 bilinear fetches), the 5-tap and 9-tap Catmull-Rom (the 9-tap is TheRealMJP's, MIT), and the adaptive one. Its early note that Catmull-Rom "does not work" concerns only the 4-fetch trick, most likely because Catmull-Rom's outer weights are negative.
3. InfiCam applies the palette to computed temperatures. Its notes say that leaves regions where temperatures can't be calculated — in the 400 °C mode, before a few calibration cycles — and that "xtherm just scales the palette to the raw input data linearly". We tone-map in the raw domain; confirm that choice holds.
4. Known issues from InfiCam's notes: flicker when aimed at trees or grass (its auto range is the camera's per-frame min/max with no smoothing), and nearest-neighbor interpolation looking bad at small sizes. Our AGC and upscaler must not reproduce either; the `night` benchmark scene should include trees or grass.
5. Interpolation options from InfiRayProPyCapture, including its ESPCN ×4 "Sharp" mode (applied to the palette-mapped image there, but to the scalar here): build the shortlist for M5.
6. Manual-range UX: InfiCam's lock button and two-thumb slider (1 °C steps, lock icons on the scale bar) and its open questions (min + max vs. center + span; locking vs. limiting; locking min and max separately); ht301_hacklib's center + span keys; p2pro-rs pinning min and max separately; thermal-camera-android's lock plus dialog. Pick the simplest control that works one-handed on a tablet.
7. Box and zoom UX: the M0 matrix observations are under Findings. Add how Hti Image's and Xtherm's boxes are drawn, moved and resized, then pick the hit-testing model from the UI references.
8. Algorithm survey, any source: for each pipeline stage (shutter handling, bad pixels, destriping, temporal denoise, tone mapping, detail enhancement, upscaling), shortlist the strongest one to three approaches from papers and open-source code for any sensor, with license and expected cost within the frame budget. Add them to the catalog.

## Findings

### Design: box-driven color range (2026-09-24)

- **Source:** every open-source viewer checked (ht301_hacklib, IR-Py-Thermal, InfiCam/InfiCamPlus, thermal-cat, p3-ir-camera, thermal-camera-viewer, GetThermal, and others). None lets a user-drawn box drive the color range; boxes only feed markers or stats. FLIR's firmware AGC does — only ROI pixels build the histogram. InfiCam and InfiCamPlus, when zoomed and unlocked, take the auto range and min/max from the visible area.
- **Verdict:** adopt FLIR's model for the box and InfiCam's visible-area rule for zoom (PLAN M4 stage 5, M6). Store the box in camera coordinates (thermal-camera-viewer's widget-coordinate box is the pitfall).

### Reference apps' measurement UX (M0 matrix, 2026-09-24)

- **Box:** Hti Image (point, line, box) and Xtherm (point, line, box, grid) both have one, and neither changes the color range. They only confine readouts: Hti Image shows hottest, coldest and center; Xtherm shows max, avg and min. This confirms the design finding above.
- **Zoom:** Hti Image has none. Xtherm pinch-zooms but floors at 2× once engaged, and pinching can't bring it back to 1× — a pitfall. InfiCamPlus pinch-zooms from 1×.
- **Range:** Xtherm's draggable scale-bar markers limit the range and paint out-of-range pixels grey. InfiCamPlus's lock freezes the current range; the owner found it useful.
- **Capture internals, from their logs:** Hti Image runs libuvc with 192 packets per transfer (stock libuvc uses 32) and initializes OpenCL. ThruTracker queues 16 URBs × 32 packets. InfiCamPlus renders through a CPU canvas in 17–27 ms per frame. Compare transfer sizing in M1 if our capture drops frames.
- **Verdict:** keep our box-driven color range, and zoom 1×–8× with double-tap back to 1× (PLAN M6). Take "mark out-of-range pixels in Manual mode instead of clipping them silently" to pass 2 item 6.

### Reference apps' temperatures (M2 cross-check, 2026-09-24)

- On the same wall, Xtherm read 23.4 °C at the centre and Hti Image 25.6 °C, 2.2 °C apart. ThermalView read 24.4–24.5 °C, and it matches a contact thermometer within ±1 °C at 10 and 48 °C (DEVICE.md).
- Neither app writes the camera's user area; their environment settings and maths are app-side.
- **Verdict:** use them as image-quality references (M3), not as temperature truth.

### Over-temperature protection (2026-09-24)

- **InfiCam** (`app/.../MainActivity.java` `onFrame` and `overTempLockout`, setting "Overtemperature Protection", on by default):
  - Triggers when the frame's maximum exceeds the range's top (120 or 400 °C), after the first 50 frames.
  - Shows "Warning! Do not point at very hot objects!" and calls `calibrate()` (`0x8000`) every 250 ms for 5 s.
  - Its notes list "user setting for protection max temp" as a to-do.
- **The P2 Pro and HIKMICRO** close the shutter in firmware (PROTOCOL.md "Heat, hot scenes and the sun"). This module has no known hold command.
- **Verdict: adopt InfiCam's approach, adapted.**
  - Trigger on ≥ 4 pixels, so a single bad pixel can't hold it closed.
  - Repeat `0x8000` every 260 ms for 5 s, then peek: fresh frames only, at least 1.5 s after the last command.
  - Hold again if the view is still hot; resume after 3 clear frames.
  - The limits live in the command gate, not only in the app logic.
  - Owner decision; CLAUDE.md rule 1.
- **M1 findings.**
  - The hold works: the shutter stays shut, with one click each way.
  - The first trigger (140 °C) could never fire, because the normal range clips at ~120–123 °C.
  - A trigger at that clip would also freeze the view on harmless hot parts: the camera can't tell 130 °C from 1000 °C there.
  - Hence the owner's choice: automatic switching to the high range, with the lockout only at the high range's ceiling. Until M2, an interim trigger applies: the clip held for 10 s.
- **After M2** (high range parked): the trigger is the normal range's clip held for 10 s. It counts only pixels at the clip itself (raw ≥ 13700), not everything the readouts show as "> 120 °C", so measurable hot parts don't freeze the view (owner decision, 2026-09-24).
- **High-range maths.** The sources disagree:
  - ht301_hacklib scales the table by `1.17 × T − 40.9`.
  - InfiCam drops the `cal_00` correction and notes the vendor library does something else it didn't decode.

  M2 decides by measurement (PROTOCOL.md "Ranges").

### Pass 2 (M4, 2026-09-25)

**1. Black-box analysis of the M3 captures.** Numbers from `tools/py/bench_refs.py` (`bench/results/refs.json`), in 8-bit display levels per camera pixel. The owner's picks are in PIPELINE_LOG (M1 baseline).
- **Auto range:** Hti and Xtherm are very steady. Flicker on static scenes is 0.07–0.22 levels, against our baseline's 0.55–2.3.
  - Both hold quiet scenes at low contrast. On `flat`, Hti spans levels 108–145 and Xtherm 133–184, so the wall looks calm: display noise 0.6–0.8 levels, against our 6.2.
  - Xtherm widens its range on structured scenes (`keyboard` 17–204, `night` 23–210), like FLIR's max-gain cap.
  - Hti stays narrow everywhere (`keyboard` 79–175, `night` 75–177), which is its washed-out look.
  - InfiCamPlus stretches each frame's min…max like our baseline, with the same noise and flicker. When a car at 38.7 °C crossed the night view, its whole image dimmed by up to 97 levels until the car left (pass 2 item 4's warning).
- **Sharpness:** `keyboard` is the fair test: the camera and laptop stayed still, and every app lines up with our frame within 0.12 px. All three apps draw the screen edge at 1.26–1.30 px FWHM after averaging back to camera pixels. Ours, drawn bicubic and averaged back the same way, is 1.81 px, and 1.44 px at camera resolution. So they all sharpen.
- **Halo:** Hti's rim comes from detail enhancement over a heavily compressed base. It shrinks the screen edge's step to 25 levels (ours 73, Xtherm 45), and the halo on it is 14 %. Xtherm's is 1.4 %, InfiCamPlus's 2.2 %, ours 2.5 % (the scene's own dip beside the bezel).
- **Detail:** key-gap detail on `keyboard` is 10.3 levels for InfiCamPlus, 4.1 for ours, 3.5 for Xtherm and 2.6 for Hti. InfiCamPlus sharpens hard without adding halo, which fits the owner's night pick.
- **Shutter hiccups:** none of the 10 s recordings caught a cycle, so this needs a longer recording.
- **Hand edges:** each app's hand sat at a different distance, which changes its edge widths (1.0–1.4 px for the apps, ~1.9 px for ours), so they don't compare cleanly.
- **Verdict:** the bar is:
  - Xtherm's auto range: steady, and gain-capped on flat scenes;
  - at least InfiCamPlus's sharpness and detail, without halos;
  - none of Hti's base compression.

  That's M4 stage 5 (tone mapping: minimum span, damping) and stage 6 (detail enhancement with halo control). M5's upscaler must not blur.

**2. InfiCam's display path.** Pointers are into inficam @ `531aa81`. A full clone of its history is in the session scratchpad; the shallow clones stop at `baea0c2`.
- **Colour first, on the CPU.** It applies the palette to float °C, giving 8-bit RGBX at sensor size (`InfiCamJNI.cpp:375-397`, `InfiFrame.cpp:203-207`).
- **Two GL passes, both on 8-bit colour:**
  - a sharpen at sensor resolution (`fsharpen.glsl`: `9·px − 2·(N+S+E+W)`, mixed in by the "Sharpening" setting). That's a Laplacian boost with a white-noise gain of 5.4× at InfiCam's default of 0.5 and 2.7× at InfiCamPlus's 0.2;
  - then the chosen scaling mode (default Linear) to the screen.
- **No clamps.** Every surface is RGBA8888, so each pass re-quantises to 8 bits, and no shader has an anti-ringing clamp. This sharpen pass is how InfiCamPlus gets its sharp look (item 1).
- **Scaling shaders:**
  - "B-spline" (`fcubic.glsl`) is a uniform cubic B-spline from 4 bilinear fetches (GPU Gems 2, ch. 20). Its weights are all positive, so it never rings, but it blurs even at 1:1.
  - The 5-tap Catmull-Rom (`fcmrom.glsl`) drops the corner taps (Jimenez's trick). It's adapted from a Shadertoy with no stated license, which defaults to CC BY-NC-SA.
  - The 9-tap Catmull-Rom (`fcmrom2.glsl`, unused) is MJP's gist, MIT, credited in InfiCam's LICENSE.
  - The adaptive one (`fadaptive.glsl`, after IEEE 924383) warps a Catmull-Rom by Sobel gradients of the palette's luma. It samples 2×2 averages, and its author doubted it works.
- **Why Catmull-Rom "does not work" in 4 fetches** (`notes.txt:340-341`). Its negative outer weights push the merged fetch offsets outside [0, 1]. The outer texels are then never read, and the result is exactly bilinear: the agent re-ran the maths and got a maximum difference of 1.7e-15. It also divides 0 by 0 at texel centres.
- **InfiCamPlus** keeps the same shaders and passes. It moved the colouring to Java (still 8-bit, still on °C) and lowered the sharpening default to 0.2.
- **Verdict:**
  - Reject colour-first scaling and the 8-bit sharpen pass.
  - Adopt the 9-tap Catmull-Rom structure (MJP, MIT; credit in THIRD_PARTY.md when adapted) for M5's scalar upscale. Use highp coordinates, with an optional clamp to the nearest 2×2 texels' min/max against ringing.
  - Keep the 4-fetch B-spline as a cheap smooth option (Sigg & Hadwiger).
  - Any sharpening goes on the scalar, after denoising, and through the benchmark (stage 6).

**3. Palette on temperatures vs raw.** `notes.txt:166-172`: "400c mode looks a bit quirky before a few calibration cycles, while it doesn't on xtherm / the reason is that xtherm just scales the palette to the raw input data linearly". InfiCam first computes temperatures, and "below a certain point the temperatures can't be calculated at all". Its table refreshes only at stream start and on each calibration (`InfiCam.cpp:18-21, 182-193`). Pixels below the model's floor collapse to one colour (`InfiFrame.cpp:128-129`).
- **Verdict: confirmed, tone-map raw.** Raw is always defined and doesn't depend on the model. °C is only for the readouts, the scale-bar endpoints, and converting a manual °C range to raw bounds whenever the table updates (PLAN M4 stage 5).

**4. Known issues from InfiCam's notes.**
- **Auto range:** it's the camera's per-frame min/max, with no smoothing, percentiles or hysteresis (`InfiCamJNI.cpp:376-377`; InfiCamPlus recomputes it the same way).
- `notes.txt:128`, still open: "fix the flickering that occurs when aimed at say a tree or grass". The M3 night capture showed the same mechanism: a passing car dimmed InfiCamPlus's whole image by up to 97 levels.
- `notes.txt:124`: "the nearest neighbor interpolation sucks for smaller sizes".
- **Verdict:** robust percentiles, temporal smoothing and hysteresis for our auto range (stage 5). M5's upscaler replaces nearest; the benchmark sheets already preview bicubic. `night` has a shrub but no trees or grass; add a tree scene when convenient.

**5. Interpolation options (InfiRayProPyCapture @ `a038e88`).**
- **Plain modes:** each one resizes the palette-mapped 8-bit image with OpenCV. Its cubic is Keys a = −0.75, sharper than Catmull-Rom's −0.5; its Lanczos is 8×8.
- **"Sharp"** takes these steps:
  1. ESPCN ×4 on the palette image's luma. The model is `ESPCN_x4.pb` from TF-ESPCN: Apache-2.0 code, trained on DIV2K, whose data is for academic research only.
  2. An unsharp mask, 1.85·img − 0.85·blur(σ 1).
  3. A cubic fit to the window.
- **ESPCN ×4 cost** at 256×192: 24,752 parameters, 1.21 G multiply-adds per frame, ~60 GFLOP/s at 25 fps.
  - On the CPU it's implausible (~160 ms per frame at OpenCV's efficiency).
  - On the Adreno 650 it's plausible but unproven: a hand-written GLES network might run in ~4–20 ms.
- **M5 shortlist:**
  - 9-tap Catmull-Rom, with and without the 2×2 clamp;
  - Keys a = −0.75;
  - Lanczos-3 with the anti-ringing clamp.
- **Verdict on ESPCN:** only a Mac-harness experiment on our tone-mapped scalar. Keep it only if it beats Catmull-Rom on the benchmark without halos.

**6. Manual-range UX.** Pointers are into each repo at its catalog commit.
- **InfiCam:**
  - A lock toggle snapshots the frame's raw min/max (`MainActivity.java:537-538`).
  - A vertical RangeSlider appears only while locked, in 1 °C steps on a fixed −20…120 °C axis, with no value bubble.
  - The model can hold min and max separately (a NaN end means auto), but the UI locks both.
  - The scale bar shows lock icons at locked ends.
  - Its open questions (`notes.txt:115-118`): "maybe enter as center + span rather than min + max?", "separate locking for min and max of range", "option to just limit the range instead of locking it".
- **InfiCamPlus** snapshots the visible area instead. It adds a moving barber-pole over pixels above the range, and disables the slider during calibration.
- **ht301_hacklib:** 'a' toggles auto. The arrow keys move the centre by ±1 °C and the half-span by ±1 °C, and pressing one turns auto off.
- **p2pro-rs:** each end has a checkbox and a number field (0.5 °C steps); unpinned ends follow a 30-frame average.
- **thermal-camera-android:** a menu lock, plus an edit dialog.
- **thermal-cat:** unchecking Auto copies the *displayed* range, and the auto controller keeps running underneath, so Auto resumes already settled.
- **Pitfalls seen in the code:**
  - InfiCam decides which thumb moved by comparing values, so the thumbs swap when they meet.
  - Several projects never guard a zero or inverted span.
  - Most lock snapshots copy the raw min/max rather than what was shown.
  - A fixed 140 °C axis gives ~5 dp per °C on this tablet, so a 2 °C span can't be set.
  - InfiCam calls Material's RangeSlider fragile.
- **Verdict:**
  - One Lock toggle sits at the foot of the scale bar, and **the scale bar is the slider.**
    - In Manual, drag the top end for max, the bottom end for min, or the bar's body to shift both.
    - Starting an end drag in Auto locks first, so lock-and-adjust is one gesture.
  - **Locking** copies the displayed range, rounded outward to 0.5 °C (thermal-cat; PLAN M6 "locks whatever range is showing"). Keep the auto controller running underneath.
  - **Storage:** keep the range in °C and convert it to raw bounds every frame (item 3).
  - **Drag feel:**
    - Δ°C = Δy / bar height × the span at touch-down, with the axis frozen during the drag, so precision scales with the span.
    - Snap to 0.5 °C, and clamp at a minimum span of 1 °C rather than pushing the other end.
    - 48 dp targets, with a value callout beside the finger.
    - Disabled during a lockout or shutter cycle.
  - **Build:** a custom Compose Canvas widget, not Material's RangeSlider.
  - **InfiCam's questions:** min + max and centre + span both live on one widget. No separate end locks in v1, though the state stays two endpoints so one can come later. Lock, not limit.
  - **Open for M4 stage 5 and M6:**
    - Auto's HE blend and Manual's linear mapping differ, so locking changes the look. Fade the blend out over ~0.3 s, or accept it.
    - Manual's out-of-range marking could be a static hatch (not moving stripes, not grey); that's the owner's call.

**7. Box and zoom UX, code side.** Hti Image's and Xtherm's boxes, observed on the tablet, are still to add.
- **Compose-Cropper:**
  - Corner zones only (20 dp in its demo), with priority corners → inside.
  - It keeps the finger's offset for corners, but moves the inside by per-event deltas.
  - It overshoots bounds and animates back on release, and dims the outside with a `SrcOut` layer.
- **Android-Image-Cropper:**
  - A 24 dp touch radius and a 42 dp minimum.
  - "corner-handles take precedence, then side-handles, then center" (`CropWindowHandler.kt:193`).
  - It keeps the finger's offset, clamps live with a 3 dp snap, and maps far-away touches to the nearest handle.
- **uCrop:** 30 dp corners, no edges, a 100 dp minimum. A move that would cross a bound is dropped, and a pinch that starts on the box does nothing.
- **Both Compose-Cropper and Android-Image-Cropper trap small boxes:** at their own minimum size, the corner zones cover the whole box, so it can't be moved.
- **p2pro-rs's pinch:** it keeps the content point under the fingers' centroid fixed, re-baselines whenever a finger is added or lifted, and clamps pan so the image always fills the view. That matches PLAN M6.
- **Verdict:**
  - **Storage:** keep the box as a half-open integer rect in camera pixels (at least 4×4), with one `camToScreen`/`screenToCam` pair shared by the renderer, the overlay and the hit-test.
  - **Hit-test** in screen pixels with r = 24 dp:
    - On each axis, take the box's screen extent, grown to at least 2r.
    - The inner band is b = clamp((extent − 2r)/2, 0, r).
    - The near-low zone is [lo−r, lo+b], the near-high zone is [hi−b, hi+r], and anything between is inside.
    - Corner beats edge beats inside. The inside zone never vanishes, so a 4×4 box stays movable.
    - Outside touches never grab a handle; they stay free for pan and pinch.
  - **Drags:**
    - Start after touch slop.
    - Edges = touch-down snapshot + (finger − down) in camera px, which keeps the finger's offset without drift.
    - Clamp live, sliding moves along the walls; round to whole pixels.
    - Never overshoot and snap back, because readouts must come from exactly what's drawn.
  - **Gestures:** one `awaitEachGesture` handler. A second finger at any time restores the box snapshot and becomes a pinch (p2pro-rs's model), and two fingers down to one keeps panning.
  - **Drawing:** dimming goes in the shader (50 % outside, mirrored in `native/core`). The outline and handles go in a Compose Canvas at fixed dp.

### Pass 1 (M0, 2026-09-24)

Pointers confirmed in the clones at the commits in the catalog (InfiCam `531aa81`, InfiCamPlus `6fad1f3` and its v1.0.1 tag, libuvc upstream `4e9fc77`).

1. **Connect path** — InfiCam `UVCDevice.cpp:21-67`: `libusb_set_option(NULL, LIBUSB_OPTION_NO_DEVICE_DISCOVERY)` (:25, the renamed `WEAK_AUTHORITY`), `libusb_init` (:29) with its own event thread (`libusb_handle_events_timeout`, 50 ms, :13), `uvc_init(&ctx, usb_ctx)` (:38), then `uvc_wrap(fd, …)` (:42), which calls `libusb_wrap_sys_device`. Format: the first format descriptor, which must be uncompressed (:54), with the size of its first frame descriptor (:63-64). Then `uvc_get_stream_ctrl_format_size(…, format, w, h, 0)` with `format = UVC_FRAME_FORMAT_ANY` (`UVCDevice.h:27`), and `uvc_start_streaming(…, 0)` (:96-98). libuvc picks the isochronous alternate setting: the first whose wMaxPacketSize × mult covers the negotiated dwMaxPayloadTransferSize — on our camera that can only be alt 1 (DEVICE.md). thermal-camera-android does the same with libusb 1.0.27. **Verdict:** adopt the wrapping sequence, with our own context instead of the NULL default (PROTOCOL.md). Adapt the selection: open the M0-verified 256 × 196 YUYV format with an explicit 25 fps, since `fps = 0` can divide by zero in libuvc.
2. **Start-up sequence** — InfiCam `InfiCam.cpp`: `connect()` sends `0x8004` (:59), then calls `set_range()`, which sends `0x8020` (:104) — both before `stream_start()` (:74). `MainActivity.java:139-148`: `0x8000` after an initial delay, then every `shutterInterval` ("Xtherm does it 1 sec after connect and then every 380 sec", :140). InfiCamPlus v1.0.5 keeps the pre-stream order on purpose for V1 cameras and discards 300 ms of frames (`InfiCam.cpp:109-121`, :182). InfiRay's demo instead streams first and sends `0x8004` 300 ms later. Outside our allowlist: InfiCam writes the user area when a thermometry slider changes, and InfiCamPlus writes it on every connect. Neither ever sends `0x80FF`. **Verdict (owner, 2026-09-24):** stream first as InfiRay's demo does, then an explicit `0x8000`, with InfiCam's order as the fallback (PROTOCOL.md "Order"); reject every user-area write.
3. **Disconnect and replug** — `USB_DEVICE_DETACHED` broadcast, then teardown (`UVCDevice.cpp:69-89`): stop the stream, `uvc_exit` (which also closes the libusb handle, :73), join the event thread (:78), `libusb_exit` (:82), `close(fd)` (:86); `onStop` disconnects, `onStart` rescans. Java then closes the same fd again through `usbConnection.close()` (`MainActivity.java:820`). Both libuvc fixes from its notes are upstream: PR #209 (`e3f3690`, bounds check on garbage frames) and PR #210 (`d3318ae`, don't free in-flight transfers) are ancestors of libuvc's HEAD. **Verdict:** adopt the teardown order; pin libuvc at or after `d3318ae`; close the fd in one place only.
4. **Metadata and width** — width 256 only selects the `InfiFrame::init` constants; nothing is specific to PID 0x0001. InfiCam's only frame check is `data_bytes ≥ w*h*2`. InfiCamPlus also drops partial frames, frames with any pixel above `0x3FFF`, and frames with invalid settings or thermometry metadata (`InfiCam.cpp:401-490`). InfiCam reads distance as a float; the vendor software uses u16. **Verdict:** adopt InfiCamPlus's validation (PROTOCOL.md "Sanity checks"); keep distance as u16.
5. **InfiCamPlus on the HT-301** — root cause unconfirmed (the issue has no log). Static reading of v1.0.1: every 0x1514 device got `raw_sensor = true` (`InfiCamJNI.cpp:356`), and at 384×292 the raw-sensor path reads about 94 bytes past the frame buffer, which fits "immediately exits". A 256-wide device reads in bounds — no crash, but raw-sensor formulas and wrong temperatures (consistent with issue #2). HEAD rejects the HT-301 by product name and accepts "S0" names. **Verdict:** nothing to adopt; our camera shouldn't hit it.
6. **ThruTracker Recorder** (README, CHANGELOG and release notes only; closed source). On Android: transfers "reaped in a tight native (C) loop" for a steady 25 fps (v1.0.6), a field log, and a stall-warning banner (v1.0.5). Its stall/freeze watchdogs with auto-recovery are listed under Windows. **Verdict:** adopt all three, in our terms. (a) The reaping loop is the dedicated libusb event thread from item 1. (b) A stall watchdog: if no frame arrives for 0.5 s (tunable) while streaming, log it, show a banner, and restart the stream through the normal start sequence, so the `0x8000` rate limit still applies. (c) A field log: a small rotating on-device log of connects, commands, drops, stalls and restarts, pulled with adb. All three land in M1.
