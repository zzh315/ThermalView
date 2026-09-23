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

### Works with this exact camera (owner-verified)

| App | Type | Notes |
|---|---|---|
| Hti Image (`com.hti.Xtherm`) | closed, Android | The owner's current best image. Play Store: v6.4.20240702 (July 2024) by WIFI-VIEW (hti-meter.com), "HT301 thermal imager supporting software". Its package name and versioning suggest it's derived from Xtherm's code base and tuned for the HT-301; a 2020 Xtherm review calls the two apps identical. Two 2022 reviews of v6.3 say it has no manual or locked range; check whether 6.4 added one. Primary image-quality reference. |
| Xtherm (`com.infiRay.Xtherm`) | closed, Android | Official InfiRay app; its listing says "APP for T3s/T3pro". The "T3" label for our camera is the owner's observation; no public source confirms it. Point/line/surface measurement and 6 environment corrections (listing). Removed from Google Play (404 since 2025); Xiaomi's store carries v7.0.260325 (March 2026). The owner finds its image weaker than Hti Image. Temperature cross-check reference (M2). Per InfiCam's notes and InfiRay's demo, it sends `0x8000` 1 s after connect and then every 380 s, and rebuilds its temperature table after each shutter. |

### Plausibly compatible — test in the M0 compatibility matrix

| App | Type | Why it might work |
|---|---|---|
| 天眼热成像 (formerly 艾睿天眼) / Xinfrared (`com.infiRayX.Search`, English name infiRaySearch) | closed, Android | InfiRay's app for the Search / Eagle Eye series, with the MATRIX IV algorithm and an "X-Clear" outdoor enhancement mode (listings). Not on Google Play; Xiaomi's store has v4.4.250120. It has an HD mode, so it's the likely source of the "T3 device doesn't support HD" message the owner has seen; note which app shows it. Night-scene reference if it connects. |
| 专业级热像仪 / Xtherm Power (`com.infiRay.XthermPower`) | closed, Android | Same developer as Xtherm. Its listing targets InfiRay's industrial DX-series cameras (point/line/box measurement, −20–550 °C), so support for ours is unlikely, but it's cheap to try. |
| ThruTracker Recorder — github.com/ThruTrackerAnalytics/thrutracker-recorder (README and CHANGELOG only) | closed (free through 2027), Android 10+ | v1.1.0 (2026-07-25) added experimental support for exactly VID:PID 1514:0001 (the HT-301), with a native isochronous capture path and a raw 16-bit radiometric mode. So far only the release notes say so; the README and website still list only the TopDon for Android. Unknown whether it handles 256-wide frames. |
| InfiCamPlus — github.com/diminDDL/InfiCamPlus @ `6fad1f3` | open (MIT), Android, Java + C++ | Its device list (taken from Xtherm's, per PR #17) accepts product names containing "S0" or "Xmodule", and "S0H-40" from "Infiray" takes its V1 (onboard-processed) path. Its V2 support comes from Ghidra work on Xtherm's libraries (PR #17). The project targets "V2" raw-sensor cameras: V1 T2S+/T2L users saw readings ~40–45 °C too high on 1.0.x, and the maintainer points them to the original InfiCam (issue #2); v1.0.4 crashed on a T2S+ V1 (#19). HEAD rejects the HT-301; older builds crashed on it (issue #5). |
| InfiCam — gitlab.com/netman69/inficam @ `531aa81` | open (MIT), Android, Java + C++ | No published APK, so it needs a source build (old Gradle, targetSdk 27). Built for onboard-processed ("V1") cameras. Try it if InfiCamPlus misbehaves. |

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
| Hti Image (`com.hti.Xtherm`) | 6.4.20240702 (targetSdk 34) | | | | | | Installed by the owner |
| Xtherm (`com.infiRay.Xtherm`) | 7.0.260325 (targetSdk 34) | | | | | | Installed by the owner (Xiaomi store build) |
| InfiCamPlus (`be.ntmn.inficam`) | 1.0.5 (targetSdk 34) | | | | | | From GitHub releases. Uses the original InfiCam package name, so the two can't coexist. Writes its environment settings to the camera's user area on every connect (volatile — it never sends `0x80FF`); replug after testing it |
| ThruTracker Recorder (`com.thrutracker.thermalrecorder`) | 1.1.0 (targetSdk 35) | | | | | | From GitHub releases |
| 天眼热成像 (`com.infiRayX.Search`) | — | | | | | | Not installed; available in the tablet's Xiaomi store |
| Xtherm Power (`com.infiRay.XthermPower`) | — | | | | | | Not installed; available in the tablet's Xiaomi store |

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
7. Box and zoom UX: black-box observation of Xtherm's area measurement and of any zoom or area tools other reference apps showed in the M0 matrix — how the box is drawn, moved and resized, what it shows, whether it changes the color mapping. Then pick the hit-testing model from the UI references.
8. Algorithm survey, any source: for each pipeline stage (shutter handling, bad pixels, destriping, temporal denoise, tone mapping, detail enhancement, upscaling), shortlist the strongest one to three approaches from papers and open-source code for any sensor, with license and expected cost within the frame budget. Add them to the catalog.

## Findings

### Design: box-driven color range (2026-09-24)

- **Source:** every open-source viewer checked (ht301_hacklib, IR-Py-Thermal, InfiCam/InfiCamPlus, thermal-cat, p3-ir-camera, thermal-camera-viewer, GetThermal, and others). None lets a user-drawn box drive the color range; boxes only feed markers or stats. FLIR's firmware AGC does — only ROI pixels build the histogram. InfiCam and InfiCamPlus, when zoomed and unlocked, take the auto range and min/max from the visible area.
- **Verdict:** adopt FLIR's model for the box and InfiCam's visible-area rule for zoom (PLAN M4 stage 5, M6). Store the box in camera coordinates (thermal-camera-viewer's widget-coordinate box is the pitfall).

### Pass 1 (M0, 2026-09-24)

Pointers confirmed in the clones at the commits in the catalog (InfiCam `531aa81`, InfiCamPlus `6fad1f3` and its v1.0.1 tag, libuvc upstream `4e9fc77`).

1. **Connect path** — InfiCam `UVCDevice.cpp:21-67`: `libusb_set_option(NULL, LIBUSB_OPTION_NO_DEVICE_DISCOVERY)` (:25, the renamed `WEAK_AUTHORITY`), `libusb_init` (:29) with its own event thread (`libusb_handle_events_timeout`, 50 ms, :13), `uvc_init(&ctx, usb_ctx)` (:38), then `uvc_wrap(fd, …)` (:42), which calls `libusb_wrap_sys_device`. Format: the first format descriptor, which must be uncompressed (:54), with the size of its first frame descriptor (:63-64). Then `uvc_get_stream_ctrl_format_size(…, format, w, h, 0)` with `format = UVC_FRAME_FORMAT_ANY` (`UVCDevice.h:27`), and `uvc_start_streaming(…, 0)` (:96-98). libuvc picks the isochronous alternate setting: the first whose wMaxPacketSize × mult covers the negotiated dwMaxPayloadTransferSize — on our camera that can only be alt 1 (DEVICE.md). thermal-camera-android does the same with libusb 1.0.27. **Verdict:** adopt the wrapping sequence, with our own context instead of the NULL default (PROTOCOL.md). Adapt the selection: open the M0-verified 256 × 196 YUYV format with an explicit 25 fps, since `fps = 0` can divide by zero in libuvc.
2. **Start-up sequence** — InfiCam `InfiCam.cpp`: `connect()` sends `0x8004` (:59), then calls `set_range()`, which sends `0x8020` (:104) — both before `stream_start()` (:74). `MainActivity.java:139-148`: `0x8000` after an initial delay, then every `shutterInterval` ("Xtherm does it 1 sec after connect and then every 380 sec", :140). InfiCamPlus v1.0.5 keeps the pre-stream order on purpose for V1 cameras and discards 300 ms of frames (`InfiCam.cpp:109-121`, :182). InfiRay's demo instead streams first and sends `0x8004` 300 ms later. Outside our allowlist: InfiCam writes the user area when a thermometry slider changes, and InfiCamPlus writes it on every connect. Neither ever sends `0x80FF`. **Verdict (owner, 2026-09-24):** stream first as InfiRay's demo does, then an explicit `0x8000`, with InfiCam's order as the fallback (PROTOCOL.md "Order"); reject every user-area write.
3. **Disconnect and replug** — `USB_DEVICE_DETACHED` broadcast, then teardown (`UVCDevice.cpp:69-89`): stop the stream, `uvc_exit` (which also closes the libusb handle, :73), join the event thread (:78), `libusb_exit` (:82), `close(fd)` (:86); `onStop` disconnects, `onStart` rescans. Java then closes the same fd again through `usbConnection.close()` (`MainActivity.java:820`). Both libuvc fixes from its notes are upstream: PR #209 (`e3f3690`, bounds check on garbage frames) and PR #210 (`d3318ae`, don't free in-flight transfers) are ancestors of libuvc's HEAD. **Verdict:** adopt the teardown order; pin libuvc at or after `d3318ae`; close the fd in one place only.
4. **Metadata and width** — width 256 only selects the `InfiFrame::init` constants; nothing is specific to PID 0x0001. InfiCam's only frame check is `data_bytes ≥ w*h*2`. InfiCamPlus also drops partial frames, frames with any pixel above `0x3FFF`, and frames with invalid settings or thermometry metadata (`InfiCam.cpp:401-490`). InfiCam reads distance as a float; the vendor software uses u16. **Verdict:** adopt InfiCamPlus's validation (PROTOCOL.md "Sanity checks"); keep distance as u16.
5. **InfiCamPlus on the HT-301** — root cause unconfirmed (the issue has no log). Static reading of v1.0.1: every 0x1514 device got `raw_sensor = true` (`InfiCamJNI.cpp:356`), and at 384×292 the raw-sensor path reads about 94 bytes past the frame buffer, which fits "immediately exits". A 256-wide device reads in bounds — no crash, but raw-sensor formulas and wrong temperatures (consistent with issue #2). HEAD rejects the HT-301 by product name and accepts "S0" names. **Verdict:** nothing to adopt; our camera shouldn't hit it.
6. **ThruTracker Recorder** (README, CHANGELOG and release notes only; closed source). On Android: transfers "reaped in a tight native (C) loop" for a steady 25 fps (v1.0.6), a field log, and a stall-warning banner (v1.0.5). Its stall/freeze watchdogs with auto-recovery are listed under Windows. **Verdict:** adopt all three, in our terms. (a) The reaping loop is the dedicated libusb event thread from item 1. (b) A stall watchdog: if no frame arrives for 0.5 s (tunable) while streaming, log it, show a banner, and restart the stream through the normal start sequence, so the `0x8000` rate limit still applies. (c) A field log: a small rotating on-device log of connects, commands, drops, stalls and restarts, pulled with adb. All three land in M1.
