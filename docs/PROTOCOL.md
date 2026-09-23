# Protocol reference — InfiRay Xmodule S0H (VID:PID 0x1514:0x0001)

Everything here comes from open-source reverse engineering of InfiRay's "Xtherm" USB protocol — which this camera shares with the HTI HT-301 / InfiRay T3 family and the T2L/T2S+ — plus facts read from InfiRay's SDK demo. Items marked **VERIFY** must be confirmed on the real device (M0/M1) before code relies on them. Source claims were checked against the code on 2026-09-24 at the commits listed below.

## Sources

| Source | License | What it gives us |
|---|---|---|
| ht301_hacklib — github.com/stawel/ht301_hacklib (`ht301_hacklib.py`) @ `2f1498d` | GPL-3.0 | Frame layout, per-width constants, full temperature math, command values, and `CameraEmulator` (the M2 oracle). Supports HT-301, T3S, T2L, T2S+. Tested on Ubuntu and Windows only. Unmaintained since 2024. |
| IR-Py-Thermal — github.com/diminDDL/IR-Py-Thermal @ `edf7d56` | GPL-3.0 | ht301_hacklib's maintained fork. Its history (commit `75ebbde`) holds a real 256×196 frame from a T2S+ v2 plus the vendor app's log for that camera — a ready-made test vector. |
| InfiCam — gitlab.com/netman69/inficam (`notes.txt`, `libinficam/`) @ `531aa81` | MIT (own code); bundled libusb LGPL-2.1, libuvc BSD | Protocol table including the persistent-write commands; Android libusb + libuvc capture (`UVCDevice`, `InfiCam` classes); GLES 2.0 display. |
| InfiCamPlus — github.com/diminDDL/InfiCamPlus @ `6fad1f3` (v1.0.5) | MIT | InfiCam fork aimed at "V2" (raw-sensor) cameras. Frame validation, the V1 command-order fix, `0x8081`, how it tells V1 from V2. |
| InfiRay XthermDemo SDK — InfiCam's early history, commit `7028129` ("original SDK from infiray") | vendor source, license unknown — facts only | The official app flow: command timings, table refresh after each shutter, the device-name check (which includes "S0"). Never copy from it. |
| ThruTracker Recorder release notes (v1.1.0, 2026-07-25) | proprietary (notes only) | The HT-301 (same VID:PID) streams over USB isochronous transfers, switched into a raw 16-bit radiometric mode. |
| Xmodule S0 spec — InfiRay's product page (archived 2020) and its bbs.16rd.com repost | — | 256×192, 12 µm; variant 212-40 = 42.0°×32.1° FOV (4.0 mm by geometry); 25/15 Hz; NETD ≤ 60 mK; shutter correction; -20 to 120 °C, expandable. Later English pages list 25 Hz only. |
| Xmodule S0H listing — bbs.16rd.com reseller shop | — | The only S0H-specific source: the same 212-40 / 212-68 variants at 256×192, but NETD ≤ 50 mK, measurement range 30–45 °C, ±0.5–0.8 °C, operating ambient 10–40 °C; USB 2.0 with UVC, image format YUV422. It looks like a body-temperature variant (CLAUDE.md). |

Provenance: the temperature math and much of the protocol trace back to decompiled vendor libraries. InfiCam's notes credit Ghidra on `libthermometry.so` and `XthermDLL.DLL`, and the math reached ht301_hacklib through xtherm-python (github.com/lamnguyenvu98/xtherm-python). The owner decided (2026-09-24) that we may use such open-source derivatives and read the vendor demo for facts. We never decompile anything ourselves and never copy vendor code (CLAUDE.md).

When in doubt, read the source files directly — clone them into a scratch directory outside this repo.

## USB identity (observed on the Mac)

Product "S0H-40" · manufacturer "Infiray" · serial "KA1213" · VID 0x1514 · PID 0x0001 · bcdDevice 0x0200 · 480 Mb/s · 500 mA.

**VERIFY (M0):** full descriptors — VideoStreaming format GUID, frame sizes, frame intervals, and endpoint transfer type (isochronous expected, like the HT-301), with wMaxPacketSize per alternate setting.

## Frame layout

**VERIFY (M0):** the stream advertises 256 × 196 (192 image rows + 4 metadata rows) at 2 bytes per pixel — likely declared as YUYV (the S0H listing says YUV422) while actually carrying uint16 data. If the width isn't 256, stop: every constant below is width-specific.

Treat each frame as little-endian `uint16 buf[256 * 196]`. All offsets below are **uint16 indices** (the sources mix byte and uint16 offsets; these are normalized). A `f32` field is two consecutive uint16s forming an ordinary little-endian float32, low word first (confirmed on a real frame against the vendor app's log).

- **Image:** `buf[0 .. 256*192)`, row-major — raw 14-bit values (0–16383). Nothing in ht301_hacklib masks them; the 16384-entry LUT just assumes it, and InfiCamPlus drops any frame with a value above `0x3FFF`. Whether `0x8004` output is dark-frame compensated is an inference: InfiCam's notes say `0x8002` is raw output *without* compensation, and of `0x8004` only "called on start, not sure what it does". The sanity checks below catch a camera that streams uncompensated data.
- `P = 256*192` — start of the metadata rows.

Block A, at `P`:

| Offset | Type | Field |
|---|---|---|
| +0 | u16 | FPA average |
| +1 | u16 | FPA temperature (raw; see conversion) |
| +2, +3 | u16 | max point x, y |
| +4 | u16 | max raw value |
| +5, +6 | u16 | min point x, y |
| +7 | u16 | min raw value |
| +8 | u16 | average raw value |
| +12 | u16 | center raw value |
| +13..+15 | u16 | user points 1–3 raw values |

Block B, at `Q = P + amountPixels`, where `amountPixels = 256` for width 256 (it's `width * 3` for widths 384/640 — not ours):

| Offset | Type | Field |
|---|---|---|
| +0 | u16 | cal_00 |
| +1 | u16 | shutter temperature, kelvin × 10 — on our camera it changes only during a shutter cycle (M1) |
| +2 | u16 | core temperature, kelvin × 10 |
| +3 | f32 (2 × u16) | cal_01 |
| +5 | f32 | cal_02 |
| +7 | f32 | cal_03 |
| +9 | f32 | cal_04 |
| +11 | f32 | cal_05 |
| +13..+23 | f32 × 5, u16 | not in any source: a second environment block with the user area's layout (correction, reflected, air, humidity, emissivity, distance). Our camera: 0.0, 25.0, 25.0, 0.45, 0.98, **1** — distance 1 where the user area says 0 (M1) |
| +24..+31 | 16-byte ASCII | firmware version |
| +32..+39 | 16-byte ASCII | serial number (verified on our camera — see below) |
| +40..+47 | 16-byte ASCII | product name (verified on our camera — see below) |

**Open question for M2:** which environment block the vendor's math uses. ht301_hacklib (our reference) reads the user area. The final correction term `(distance × 0.85 − 1.125) × (T − air) / 100` makes distance 0 vs 1 worth about 0.5 °C at 80 °C, so the M2 cross-check against Xtherm may show exactly that gap.

The firmware string at Q+24 is solid in every source. The 16-byte serial and product fields at Q+32/Q+40 are InfiCam's layout. ht301_hacklib reads only 6 bytes of serial at Q+32 and no product name, and on the T2S+ v2 test frame, serial and product (`ME1299\0T2S+`) sat at `P + 512` (metadata row 2) instead. **Verified on our camera (M1):** firmware "1.00.201203" at Q+24, serial "KA1213" at Q+32, product "S0H-40" at Q+40 — and the pair repeats as `KA1213\0S0H-40` at P+512 (docs/DEVICE.md). Parse all strings as NUL-terminated and tolerate non-ASCII bytes.

User area, at `U = Q + 127`. The camera fills it; we only ever read it:

| Offset | Type | Field |
|---|---|---|
| +0 | f32 | correction |
| +2 | f32 | reflected temperature (°C) |
| +4 | f32 | air temperature (°C) |
| +6 | f32 | humidity |
| +8 | f32 | emissivity |
| +10 | u16 | distance |

Distance is a u16 in the vendor software; InfiCam alone reads it as a float. On the test frame the user area read 0.0 / 25.0 / 25.0 / 0.45 / 0.98 / 1, matching the vendor log. Our camera, freshly powered, reads 0.0 / 25.0 / 25.0 / 0.45 / 0.98 / **0** (M1). InfiCamPlus's writes didn't survive a replug, which fits its source never sending `0x80FF`. Whether the closed apps save their settings to the camera is unknown.

## Sanity checks (every frame)

Reject any frame that fails one of these. If failures persist once start-up is over, stop and tell the owner.

- The frame is exactly 256 × 196 × 2 bytes, and every image value is ≤ `0x3FFF`.
- Block A's max/min/center fields are non-zero and max ≥ min. At start-up, also check that they roughly match the image's own extremes.
- Shutter and core temperatures decode to plausible values (between −20 and 80 °C).
- User-area values are in range: emissivity in (0, 1], humidity in [0, 1]. (Distance isn't checked: 0 is what this camera reports after power-up.)
- The LUT built from this frame is finite and increasing above its vertex (from M2, when the LUT exists).

Partial frames are normal at two moments: one short frame when streaming starts (512 bytes), and a few when `0x8020` switches the range mid-frame (seen: 2 frames of 40–55 KB, or 9 of ~460 bytes). The size check drops them.

A camera streaming uncompensated ("V2") data fails them: on the T2S+ v2 test frame Block A +2..+15 were all zero, and the shutter and core fields decoded to −219.95 °C and −273.15 °C. We don't expect that here. InfiCamPlus decides V2 purely from USB strings — manufacturer "Xinfrared", or product names such as "T2S+_V2" or ones containing "_R", "_A" or "_C" — so "S0H-40" from "Infiray" takes its V1 path, and its older builds treated every VID 0x1514 device as V1. A V2 camera would need `0x8081` (download the per-pixel gain), held-shutter `0x8000` bursts and a libuvc patched to keep payload headers — all outside our rules, so that's an owner decision, not a code fix.

## Width-256 constants (ht301_hacklib `init_parameters`)

`fpa_off = 8617`, `fpa_div = 37.682`, `cal_00_offset = 170.0`, `cal_00_fpamul = 0.0`.

They are InfiCam's constants (`InfiFrame.cpp`), carried into ht301_hacklib via xtherm-python during its T2S+ work (commits `521c7d2`, `189586a`, 2023). They are reported to work on one T2S+ and one T2L, and InfiCam and InfiCamPlus use the same values. They're plausible for the S0H (same protocol, same width) but unproven — M2 cross-checks readouts against Xtherm and Hti Image.

## Temperature math

Port ht301_hacklib's `Camera.info()` temperature section, `wvc()`, `atmt()` and `get_temp_table()` line for line into C++, in double precision. Don't re-derive or "simplify" anything; M2's golden test against the original Python is the acceptance criterion. Orientation:

- `Tshutter = shutter / 10 − 273.15`; `Tfpa = 20.0 − (fpaTmp − fpa_off) / fpa_div` (both extra offsets are 0).
- Normal range: `cal_00_corr = int(cal_00_offset − Tfpa · cal_00_fpamul)` (= 170 for width 256), and `table_offset = cal_00 − max(cal_00_corr, 0)`.
- Distance is clamped to 20 before use (multiplier 1.0); the clamped value feeds both `atmt()` and the final correction term.
- The LUT has 16384 entries (14-bit input). The square root is taken of `abs(...)`: below the curve's vertex (raw ≈ 400 on the test frame) the argument goes negative, and `abs` folds the curve back up instead of producing NaN. The NaN→0 replacement only fires when the argument itself is NaN, e.g. when cal_01 = 0. InfiCam instead maps non-finite values to 0, which gives a flat floor below the vertex. Replicate ht301_hacklib exactly for the golden test, fold included. In the app, raw values below the vertex (the raw value where the LUT bottoms out) and any NaN mean "no valid temperature": show "--".
- Normal-range correction coefficients: m = 1, b = 0. (High range would use m = 1.17, b = −40.9, an empirical fit; we never enter high range.)
- Precision: the oracle's LUT is float32 under numpy 1.x and float64 under numpy 2.x. They differ by at most 0.001 °C, and a float64 reimplementation matches both within 0.0014 °C, so pin numpy.
- The LUT depends on per-frame shutter and FPA temperatures. ht301_hacklib rebuilds it every frame. InfiRay's demo, InfiCam and InfiCamPlus rebuild it only at stream start, after each shutter, and on a settings change; the demo also rebuilds it 100 ms before a measurement-mode shutter. Rebuilding 16384 entries is cheap, so per-frame is our default; M2 compares both policies against the reference apps.
- Lens-dependent branches: InfiCamPlus's port of the vendor function special-cases 13 mm and 6.8 mm lenses (an extra term from Q+13; distance × 3, clamped at 60) and uses the unclamped distance for atmospheric transmission. ht301_hacklib has no such branches. Which branch a 4.0 mm lens would take is unknown; ht301_hacklib stays our reference, and M2's cross-check would expose a difference.
- Environmental inputs come from the user area, verbatim.

## Commands

Transport: UVC SET_CUR on CT_ZOOM_ABSOLUTE_CONTROL with a 16-bit value (`uvc_set_zoom_abs` in libuvc; `cv2.CAP_PROP_ZOOM` in ht301_hacklib).

**Order.** The sources disagree:

- InfiRay's own demo starts the stream, sends `0x8004` 300 ms later, and sends no range command at connect. It sends `0x8000` 1 s after connect and every 380 s after that. For a range change it sends the range command at +100 ms, `0x8000` at +600 ms, and refreshes the temperature table at +1500 ms.
- InfiCam sends `0x8004` then `0x8020` in `connect()`, before `startStream()`, then `0x8000` 1 s after connect ("Xtherm does it 1 sec after connect and then every 380 sec").
- InfiCamPlus sends `0x8004` before streaming, deliberately, for V1 cameras: "otherwise their first callbacks still contain the previous UVC mode and are neither valid 14-bit pixels nor valid settings metadata". It then discards 300 ms of frames. Its v1.0.4 sent `0x8004` after streaming started, and the app crashed on V1 units (issue #19) — it had no frame validation.
- ht301_hacklib and IR-Py-Thermal send `0x8004` before OpenCV starts streaming (V4L2 starts on the first read). ht301_hacklib waits 50 frames after a range change before `0x8000` ("some delay is needed before calibration").
- P2Pro-Viewer reports that libusb commands hang unless the video stream is open. That's on Windows with the libusb-win32 filter driver, for a P2 Pro using a different transport — a hint, not evidence for this camera.

No source shows a range command triggering a shutter cycle by itself, and on our camera it doesn't, at least when the camera is warm (M1). InfiRay's demo, InfiCam and ht301_hacklib all follow it with `0x8000`; IR-Py-Thermal sends nothing and just waits for a non-uniform frame.

Our sequence (owner decision, 2026-09-24; CLAUDE.md rule 1): stream → `0x8004` → drop frames until they pass the sanity checks → `0x8020` → `0x8000` about 0.5 s later → drop frames through the shutter cycle. If the stream began with repeated frames, the camera is calibrating after power-up, so the start-up `0x8000` is skipped and the hold waits the calibration out (owner decision after M1's power-up finding). Fallback, if M1 shows trouble with streaming first: InfiCam's order, with `0x8004` and `0x8020` before streaming.

| Value | Meaning (source) | Status |
|---|---|---|
| 0x8004 | Raw 16-bit output; sent at start. The original SDK comment, quoted in InfiCam's notes: "切换数据输出8004原始8005yuv,80ff保存" (8004 = raw, 8005 = YUV, 80ff = save) | **Allowed** — once at start |
| 0x8020 | Range −20 to 120 °C, "(followed by shutter)" per InfiCam's notes. On a warm camera it triggers no cycle (M1), so the host sends `0x8000` | **Allowed** — once at start |
| 0x8000 | Click shutter + dark-frame calibration | **Allowed** — at start, on Recalibrate, and by an owner-approved policy; max once per 10 s |
| 0x8001 | Dark-frame calibration without closing the shutter; effect on the image unclear | Forbidden |
| 0x8002 | Raw output without dark-frame compensation | Forbidden |
| 0x8003 | Unknown mode | Forbidden |
| 0x8005 | YUV output | Forbidden |
| 0x8021 | High temperature range (120 to 400 °C) | Forbidden |
| 0x8081 | Asks a raw-sensor camera to send its per-pixel calibration data (InfiCamPlus `CMD_DOWNLOAD_CAL`) | Forbidden |
| 0x80FF | Saves the user area to non-volatile storage | **Forbidden — persistent write** |
| 0xEC.. / 0xEE.. | Appear to mark dead pixels in the camera's own dead-pixel table ("用户盲元表", per InfiCam's reading of the original SDK) | **Forbidden — treat as persistent write** |
| < 0x8000 | High byte = user-area address (0x00–0x15: correction, reflected, ambient, humidity, emissivity at 4-byte steps, then 2-byte distance), low byte = value | Forbidden |
| 0xF0..–0xFB.. | Set custom measurement points (ht301_hacklib `set_point`: `0xF000+x`, `0xF200+y`, `0xF400`/`0xF600`, `0xF800`/`0xFA00`) | Forbidden (everything is computed host-side) |

- Sending `0x8000` faster than about once a second keeps the shutter closed (IR-Py-Thermal). InfiCam and InfiCamPlus use bursts of it as over-temperature protection; our 10 s rate limit rules that out.
- InfiCamPlus masks user-area addresses with `& 0xFF` (InfiCam: `& 0x7F`), so an out-of-range address silently turns into an `0x80xx` command. That's why our gate allowlists exact values rather than ranges.
- Mode persistence is uncertain: InfiCam's author suspected the camera may remember its last mode, possibly via `0x80FF`. That's why the start sequence sends `0x8004` and `0x8020` explicitly rather than trusting defaults.

## Runtime behavior to expect

- **Shutter cycles (verified in M1; details in DEVICE.md).**
  - After power-up, our camera calibrates itself on a fixed timer. Frames repeat until ~9.1 s, and cycles start at ~9.15, 13.1, 37.0 and 64.8 s. After that it never cycles on its own: none in 929 s warm, and none in 392 s after the power-up series while the FPA rose 0.74 °C.
  - So once warm-up is over, only the host corrects drift. The apps do this: InfiRay's demo and Xtherm send `0x8000` 1 s after connect and then every 380 s. InfiCamPlus ramps its interval from 7.5 s to 180 s during warm-up; that ramp resembles the camera's own power-up schedule.
  - The demo refreshes the temperature table from the next frame after each shutter ("打快门更新表", shutter → update table).
  - During a cycle the camera repeats its last frame. Each cycle is 30–31 identical frames (1.23–1.27 s), commanded or not, starting ~0.1 s after `0x8000`. Identical frames are the detector. Q+1 (shutter temperature) also updates at each cycle, but not when it moved less than 0.1 K, so it isn't a reliable signal on its own.
  - InfiCamPlus treats 350–800 ms after `0x8000` as shutter-closed and withholds 800 ms of frames. On our camera the freeze is longer, so its numbers don't transfer.
  - Detect cycles and hold the last good image (PLAN M4). Count the camera's own cycles as calibrations too.
- **`0x8020`** triggers no cycle on a warm camera; one malformed frame follows it. At power-up its effect can't be separated from the camera's own calibration.
- **Start-up.** After power-up the stream opens with repeated frames, and a `0x8000` sent then has no visible effect (DEVICE.md), so we skip it. The start-up hold ends after 10 fresh frames following a freeze, and gives up after 8 s. A warm camera streams fresh raw frames within 0.1 s. It stays warm while the app is in the background: the camera remains powered, so no power-up series follows. Frame rate: 25.15 fps by the tablet's clock.
- **Dropped frames.** libuvc hands the callback only the newest completed frame, so frames the callback missed show up as gaps in `frame->sequence`; frames lost on the bus show up as arrival gaps.
- **libuvc quirks.** Request an explicit frame interval (25 fps): with `fps = 0` libuvc divides by zero on a continuous-interval descriptor. `UVC_FRAME_FORMAT_ANY` only matches the YUYV, UYVY, GRAY8, GRAY16, NV12 and BGR GUIDs, so pass the format M0 verifies.

## Android USB notes

- Request the CAMERA runtime permission before USB permission (InfiCam does the same). Since targetSdk 28, `UsbManager` grants USB permission for video-class devices only to apps holding CAMERA; ThruTracker's README says the same. The app never uses the tablet's own cameras. On Android 12+, the camera privacy toggle also blocks it: with the toggle on, permission for video-class devices is denied, so detect that and tell the owner.
- USB permission: an intent filter for `USB_DEVICE_ATTACHED` with `res/xml/device_filter.xml` → `vendor-id="5396" product-id="1"` (decimal for 0x1514 / 0x0001), so the app auto-launches on plug-in and Android can remember the grant. (InfiCam matches any IAD-class device and checks the vendor at runtime; our exact filter is stricter.)
- `requestPermission`'s PendingIntent must be `FLAG_MUTABLE` — the system adds `EXTRA_DEVICE` and `EXTRA_PERMISSION_GRANTED` as fill-in extras, which an immutable intent silently drops. At targetSdk 34 a mutable PendingIntent must also be explicit (`setPackage`). Register the result receiver with `RECEIVER_NOT_EXPORTED`. The sample in Android's USB host guide uses `FLAG_IMMUTABLE` and an unflagged receiver, so don't copy it.
- libusb can't enumerate devices on Android; the app opens the device through `UsbManager` and native code wraps that file descriptor. InfiCam's sequence (`UVCDevice.cpp`): `libusb_set_option(NULL, LIBUSB_OPTION_NO_DEVICE_DISCOVERY)`, `libusb_init`, its own libusb event thread, `uvc_init` with that context, then `uvc_wrap(fd, …)`, which calls `libusb_wrap_sys_device`. libuvc picks the isochronous alternate setting itself. Versions: `libusb_wrap_sys_device` since libusb 1.0.23; the option since 1.0.24 as `WEAK_AUTHORITY`, renamed `NO_DEVICE_DISCOVERY` in 1.0.25; per-context options and `libusb_init_context` since 1.0.27. Pin 1.0.27 or later and set the option on our own context; the NULL-context form in libusb's Android README logs "API misuse!". Follow libusb's Android documentation and this sequence rather than improvising.
- Give the file descriptor one owner: InfiCam closes it both natively and through `UsbDeviceConnection.close()`.
- Launch the activity `singleTop`, with `configChanges`, so a replug's attach intent doesn't recreate it.
- Restarting the app through adb while the camera stays attached can leave streaming unable to start (InfiCam's notes); replug if it happens.
- InfiCam notes an Android 10 bug that forces targetSdk ≤ 27 for UVC access on some devices. Not applicable: AOSP fixed it in android-10.0.0_r30 (the March 2020 patch), Android 11 and later keep the fix, and this tablet runs Android 12 or later.
