# ThermalView (working title)

Personal Android app for ONE specific USB thermal camera. The single goal: the best image quality of any app for this camera, behind a deliberately simple UI. Personal use only — never distributed, never multi-device.

Owner's use cases, in tuning priority: PCB work, underfloor heating, outdoor/night observation.

The owner vibe-codes this with you. They're an IT grad and comfortable with any approach: explain decisions briefly, don't lecture on basics.

## Hardware

Known facts (docs/DEVICE.md holds the verified record once M0 creates it):

- **Camera:** InfiRay Xmodule S0H, variant 212-40. USB product "S0H-40", manufacturer "Infiray", VID:PID `0x1514:0x0001`, USB 2.0 High Speed, requests 500 mA.
- **This unit:** bought second-hand on Xianyu, built into a 3D-printed housing with a focus ring. The seller's listing calls it an InfiRay S0 with "the same core as the T2L/T2S". It claims 256×192 at 25 Hz, −20 to 120 °C ("about 200 °C in practice"), and a replaceable 4 mm lens (M17×0.75 thread, same as T2/T3 lenses), for use with Xtherm. The seller notes cosmetic scratches on the lens.
- **Module spec:** 256×192, 12 µm VOx, 4.0 mm lens, FOV 42.0°×32.1°, 25/15 Hz (this unit offers 25 fps only — docs/DEVICE.md), NETD ≤ 60 mK, mechanical shutter, -20 to 120 °C. This is InfiRay's sheet for the Xmodule **S0** (212-40), and it matches the seller's claims. But the USB product string says S0H, and the only S0H listing found (a bbs.16rd.com reseller page) differs: NETD ≤ 50 mK, measurement range 30–45 °C at ±0.5–0.8 °C, operating ambient 10–40 °C. That reads like a body-temperature variant. Treat accuracy outside 30–45 °C, and use below 10 °C ambient, as unverified until M2's absolute check.
- It shares VID:PID with the HTI HT-301 (a 384×288 camera) because both speak InfiRay's "Xtherm" UVC protocol. That's why the Xtherm app calls it a "T3" device. Nothing 384×288-specific applies here.
- **Focus:** manual focus ring on the housing; the seller says it focuses near and far. Software can't control it; don't try.
- **Host:** Xiaomi Pad 5 Pro 12.4 (codename `dagu`): Snapdragon 870, Adreno 650 (GLES 3.2), 12.4" 2560×1600 IPS LCD at 244 ppi, 60/120 Hz, one USB-C port (USB 3.2 Gen 1). Launched on Android 12 (MIUI 13); it now runs HyperOS OS2.0.10.0.ULZCNXM (China ROM) on Android 14, API 34, its last update (verified in M0 — docs/DEVICE.md).
- **Dev machine:** MacBook Pro, Apple Silicon (M5 Pro).

## Hard rules

### 1. Camera command allowlist

Vendor commands are 16-bit values written with UVC SET_CUR to the Camera Terminal's CT_ZOOM_ABSOLUTE_CONTROL (libuvc: `uvc_set_zoom_abs`). These are the only values the app may ever send:

| Value | Effect | When |
| --- | --- | --- |
| `0x8004` | Select raw 16-bit output | Once per stream start, after streaming begins |
| `0x8020` | Select normal range (-20 to 120 °C) | Once, after `0x8004` has taken effect |
| `0x8000` | Shutter close + dark-frame (NUC) refresh — the runtime calibration Xtherm and InfiRay's demo trigger on connect and every 380 s; it does not touch factory calibration | Once ~0.5 s after `0x8020`; the Recalibrate button; a periodic policy once the owner approves its numbers (docs/PLAN.md M4). Max once per 10 s |

Start sequence: stream → `0x8004` → drop frames until they pass the sanity checks (docs/PROTOCOL.md) → `0x8020` → `0x8000` about 0.5 s later. If M1 shows trouble with streaming first, the owner-approved fallback is InfiCam's order: `0x8004` and `0x8020` before streaming starts, the rest unchanged.

Everything else is forbidden, including: `0x80FF` (writes the user area to non-volatile memory), `0xEC..`/`0xEE..` (write the camera's dead-pixel table), `0x8081` (asks raw-sensor cameras for per-pixel calibration data), `0x8001`, `0x8002`, `0x8003`, `0x8005`, `0x8021`, any value below `0x8000` (user-area byte writes), and `0xF0..`–`0xFB..` (measurement points).

Enforcement:

- One function owns this channel: `CameraCommands::send(uint16_t)`. It checks a compile-time allowlist, refuses and logs anything else, and rate-limits `0x8000`. Its logic lives in `native/core`, so its unit tests run on the Mac: one asserts the allowlist is exactly `{0x8000, 0x8004, 0x8020}`, another checks the rate limit. `native/android` supplies the single sender that calls `uvc_set_zoom_abs`.
- A test fails the build if our code (anything outside `native/third_party`) calls a `uvc_set_*` function or `libusb_control_transfer` anywhere but that sender.
- The only other SET_CUR requests allowed are libuvc's standard stream negotiation (VideoStreaming probe/commit). No SET_CUR on any other control.
- Never extend the allowlist or send a command "to see what happens". If a problem seems to need one, stop and ask the owner.
- Reads (GET_CUR, descriptors) are fine.
- This covers tools too. On the Mac, nothing talks to the camera beyond M0's standard descriptor reads: no OpenCV, pyusb or ht301_hacklib `Camera()` sessions against the real device. The oracle works from recorded frames only.

### 2. Never modify factory calibration

- Each frame's metadata rows carry the camera's calibration constants. Use them verbatim.
- Temperature readouts come from RAW frame values plus those constants — never from the enhanced display image.
- Environmental parameters (emissivity, reflected and ambient temperature, humidity, distance) are read from the metadata and used as-is. If they ever become adjustable, that happens app-side only.
- Bad-pixel handling is software-only; nothing is written to the camera.

### 3. Verify, don't assume

- Verify hardware and OS facts on the real devices before code depends on them, and record them in docs/DEVICE.md along with the command that verified them.
- If a verified fact contradicts this file or docs/PROTOCOL.md, stop and tell the owner.
- Protocol knowledge comes from docs/PROTOCOL.md. For anything not covered there, investigate with logging on the device — don't guess.

### 4. Measure every image-quality change

- Evaluate each pipeline change on the benchmark set with the Mac harness (docs/PLAN.md, M3): metrics before and after, plus side-by-side PNGs (and clips, for temporal effects) next to the reference captures from every app that works with the camera. The bar for each scene is the best of those apps.
- Keep a change only if it improves the benchmark without visible artifacts (halos, ghosting, flicker, banding). Log results in docs/PIPELINE_LOG.md.
- The owner makes the final visual call, so show before/after images.

## Scope

- **v1:** live view; palettes `white_hot` and `rainbow_hc`; auto range and manual (locked, adjustable) range; low, high and center-crosshair temperature readouts; palette scale bar with °C endpoints; a Recalibrate button; view-size presets (the image always stays 4:3); pinch zoom; a measurement box. docs/PLAN.md M6 specifies the UI.
- **Debug builds only:** raw frame dumper, dump replay, debug overlay, pipeline stage toggles and tuning sliders.
- **Out of scope — don't build:** photo capture, video recording, tablet-camera overlay, other cameras, other palettes, iOS, emissivity UI, high-temperature range, cropped or stretched aspect ratios.

## Fresh code, best-of-breed approaches

- This repo is written from scratch: no forks and no bulk-copied app code.
- Third-party code: the app's only native libraries are libusb and libuvc, on top of the Android SDK, AndroidX and Compose. Tests, the Mac harness and `tools/py` may also use a few pinned, well-known libraries (e.g. a C++ test framework, a PNG writer, a JSON library, numpy, pyusb) and the vendored Python oracle. List each in THIRD_PARTY.md.
- `docs/PRIOR_ART.md` catalogs every app that works with this camera or its protocol family. For each problem, study how they solve it in the scheduled review passes, adopt the best approach (or a better one), and record the verdict and evidence there.
- Image-quality algorithms aren't tied to a device, so research them anywhere: papers, and open-source code for any thermal sensor or for image processing in general. Experiments may run third-party code in the harness; what ships is adapted into our architecture. Tuning still happens on our benchmark, because it depends on this sensor's noise and on what the camera already corrects onboard. Temperature math is the exception: it stays this camera's own (docs/PROTOCOL.md).
- Open-source code: read freely; small, well-understood pieces may be adapted into our architecture with attribution in THIRD_PARTY.md, within each project's license (personal use; revisit licensing before ever publishing).
- Closed-source apps (Hti Image, Xtherm, 天眼/infiRaySearch, Xtherm Power, ThruTracker, IRCAM): learn only by observing their output and behavior. Never decompile them or extract their code or assets.
- Decompilation by others (owner decision, 2026-09-24): open-source code derived from decompiled vendor libraries (InfiCam, and ht301_hacklib's math through it) is usable like any open-source code, and InfiRay's SDK demo source in InfiCam's early history may be read for protocol facts such as timings. Never copy vendor code, and record provenance in docs/PRIOR_ART.md.

## Stack

- **UI:** Kotlin + Jetpack Compose — lifecycle, USB permission and controls only.
- **Native:** C++20 via NDK + CMake — owns capture, processing and rendering.
- **USB/UVC:** libusb + libuvc as pinned git submodules. InfiCam proves this stack on Android for this camera family; follow libusb's Android docs and InfiCam's `UVCDevice.connect()` for wrapping the file descriptor from `UsbManager.openDevice()`. Rejected: Android's external-camera Camera2 path (can't send the zoom-control commands and may alter the raw stream); `/dev/video*` (needs root).
- **Rendering:** OpenGL ES 3.2 from native code into the SurfaceView's `ANativeWindow`. Vulkan buys nothing for this workload.
- **Not Rust:** libuvc and every reference implementation are C/C++, and NDK + CMake is the smoothest path.

## Repo layout

```
app/                 Kotlin/Compose app + JNI bridge
native/core/         Portable C++: decode, temperature LUT, image pipeline, CPU reference of the
                     display path, command-gate logic. No Android/GL deps.
native/android/      JNI, UVC capture, the command sender, GLES renderer
native/third_party/  libusb, libuvc (submodules, pinned)
tools/harness/       Mac CLI: runs native/core on raw dumps -> PNGs, clips + metrics JSON
tools/py/            Descriptor dump, dump converters, temperature oracle, analysis
palettes/            Palette control points (JSON)
bench/               Benchmark dumps (raw files gitignored) + reference screenshots
docs/                PROTOCOL.md, PLAN.md, PRIOR_ART.md, DEVICE.md, PIPELINE_LOG.md
THIRD_PARTY.md       Every third-party library and adapted piece, with its license
```

`native/core` must build and pass its tests on macOS with plain CMake, so the pipeline can be iterated on the Mac against recorded frames.

## Working with the devices

- The camera lives on the tablet. It goes on the Mac only for M0's descriptor dump; after that, the Mac works from recorded dumps.
- The tablet's USB-C port is taken by the camera, so adb runs over Wi-Fi (Android 11+ wireless debugging: `adb pair`, then `adb connect`). Agents can build, install, launch, trigger the dumper, pull dumps, read logcat and take screenshots over it; the owner only plugs in, grants permission and aims. The emulator can't see the camera; test on the tablet. Debug replay needs no camera, so use it for on-device pipeline and display checks.
- With the camera plugged in, the tablet can't charge, so long runs (M1's 10- and 15-minute tests) drain it. A USB-C hub with power pass-through may work, but only if M1's stability check passes through the hub too.
- The owner already has APK installs working on the tablet. If an install fails, report the error to the owner rather than working around it.
- Android gives a USB device to one app at a time. If another thermal app holds the camera, ask the owner to close it and replug.
- For anything physical (plug/unplug, aim the camera, tap a dialog, turn the focus ring): tell the owner exactly what to do and wait.
- When asking for outdoor captures, remind the owner never to point the camera at the sun.

## Read before working

- `docs/PROTOCOL.md` — frame layout, metadata offsets, temperature math, command details, sources. Read before touching capture, decode or temperature code.
- `docs/PLAN.md` — milestones with acceptance criteria, pipeline design, benchmark, UI spec. Read at the start of each milestone.
- `docs/PRIOR_ART.md` — the other apps for this camera family, image-processing sources, what to learn from each, and the review questions. Read during M0 and at the start of M4.
- `docs/DEVICE.md` (from M0) — verified hardware and OS facts. Check it before code depends on one.
- `docs/PIPELINE_LOG.md` (from M3) — every pipeline experiment and its verdict. Read before changing a stage.

## Conventions

- One milestone at a time. When its acceptance criteria are met, tell the owner and show the evidence.
- Performance budget: sustained 25 fps, zero dropped frames over 10 minutes, app-side latency (frame callback to buffer swap) p95 ≤ 20 ms. Show these in the debug overlay.
- Never block the libuvc frame callback: copy the frame out and return.
- Survive attach, detach, replug and background/foreground without crashes or leaked handles.
