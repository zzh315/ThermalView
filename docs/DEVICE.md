# Device facts (verified)

The verified record CLAUDE.md points to. Every fact names the command that verified it and the date. Re-verify after OS updates or hardware changes, and raise any contradiction with CLAUDE.md or PROTOCOL.md with the owner (rule 3).

## Camera — "S0H-40" (verified 2026-09-24, on the Mac)

Commands, both read-only (cached descriptors and standard string reads; no class or vendor requests):

- `tools/py/.venv/bin/python tools/py/dump_descriptors.py` — full output in [device/descriptors-S0H-40.txt](device/descriptors-S0H-40.txt)
- `ioreg -p IOUSB -l -w0 -r -n "S0H-40"`

| Fact | Value | Against the docs |
|---|---|---|
| Identity | VID 0x1514, PID 0x0001, bcdDevice 0x0200, bcdUSB 0x0200; strings "Infiray" / "S0H-40" / "KA1213"; configuration string "High Speed" | Matches PROTOCOL.md "USB identity" |
| Speed and power | High Speed (480 Mb/s); bus-powered, MaxPower 500 mA | Matches |
| Topology | Device class 0xEF/0x02/0x01 (interface association). One configuration of 162 bytes: an association (video interface collection) over interface 0, VideoControl, and interface 1, VideoStreaming | — |
| UVC version | bcdUVC 1.00; dwClockFrequency 6 MHz (the payload-header timestamp clock) | — |
| VideoControl | Camera terminal (id 1) → output terminal (id 2, streaming). No processing unit, no extension unit, no interrupt endpoint | — |
| Camera-terminal controls | bmControls 0x000220 = Focus (Absolute) and Zoom (Absolute). Zoom (Absolute) is our command channel (CLAUDE.md rule 1). Focus (Absolute) is advertised too; we never touch it | Zoom matches; Focus isn't in the docs |
| Streaming format | One format: uncompressed, GUID YUY2 (`32595559-0000-0010-8000-00aa00389b71`), 16 bits per pixel | Matches ("declared as YUYV") |
| Frame size | One frame: 256 × 196; dwMaxVideoFrameBufferSize 100352 = 256 × 196 × 2 | Matches — width 256, so no stop |
| Frame rate | One discrete interval: 400000 × 100 ns = 25 fps. 15 Hz isn't offered | The S0 sheet says 25/15 Hz; later S0 pages say 25 Hz only |
| Streaming endpoint | Interface 1 alt 1, the only alternate setting with an endpoint: EP 0x81 IN, isochronous, async, 524 bytes × 1 per microframe, bInterval 1 → 4.19 MB/s against the 2.51 MB/s a 256 × 196 × 2 stream needs at 25 fps | Matches (isochronous, like the HT-301) |
| Quirks (harmless) | String indices 6 (camera terminal) and 9 (streaming interface) return invalid descriptors. The frame's dwMinBitRate/dwMaxBitRate (2508800) is in bytes/s, not bits/s. The camera terminal's bAssocTerminal is 2 | — |

For M1: open 256 × 196 with `UVC_FRAME_FORMAT_YUYV` (`ANY` also recognizes YUY2) and pass fps 25 explicitly; libuvc will pick alt 1.

### Seen through other apps on the tablet (2026-09-24; our own decoder verifies these in M1)

From `adb logcat --pid=<app>` during the M0 compatibility matrix:

- Hti Image's libuvc negotiated dwMaxVideoFrameSize 100352, dwMaxPayloadTransferSize 524, frame index 1, 25 fps.
- ThruTracker's `setZoomAbsolute value=0x8004 entity=1 vcIface=0` returned 2 bytes, so the camera terminal (id 1) accepts SET_CUR on Zoom (Absolute) as expected.
- InfiCamPlus streamed at 25.1–25.2 fps on its V1 path. Its decoder read, from our camera's metadata:
  - cal constants a = 0.2333, b = 27.867, ka = 0.00004, kb = 0.0053, kc = 0.5351, at uint16 offsets 259…267 = Q + 3 … Q + 11 with Q = P + 256, which is PROTOCOL.md's Block B layout;
  - shutter temperature 3050 (K × 10) = 31.85 °C;
  - FPA temperature 32.26 °C.

  All are plausible, which supports "V1 camera, compensated output" and PROTOCOL.md's offsets. InfiCamPlus then wrote its own defaults into the user area (emissivity 0.95, reflected and air temperature 20 °C, humidity 0.5, distance 1); the camera was unplugged afterwards. Our own decoder confirmed all of this in M1 (next section).

### Verified by our app on the tablet (M1, 2026-09-24)

From ThermalView's debug overlay and field log (`adb logcat -s 'ThermalView:*'`), camera plugged straight into the tablet:

| Fact | Value |
|---|---|
| Orientation | Correct as-is: the camera faces the same way as the tablet's rear camera, and the image looks like a window. No transform (checked with the owner) |
| Metadata strings | Firmware "1.00.201203" at Q+24. Serial "KA1213" at Q+32 and product "S0H-40" at Q+40 (InfiCam's layout). Both repeat as `KA1213\0S0H-40` at P+512 (metadata row 2), the layout seen on the T2S+ v2 |
| Calibration constants | cal_00 = 6000; cal_01…05 = 0.2333, 27.867, 4e-05, 0.0053, 0.5351 — the values InfiCamPlus decoded |
| Block A | max/min agree exactly with the image's own extremes; avg within a few counts of the image mean |
| User area after power-up (no app has written it) | correction 0, reflected 25.00 °C, air 25.00 °C, humidity 0.450, emissivity 0.980, **distance 0**. InfiCamPlus's writes were gone after a replug, so user-area writes are volatile |
| Temperatures | FPA 21.7 °C at plug-in, warming to ~31 °C over 20 minutes on the tablet; shutter and core temperatures track it within ~1–2 °C |
| Frame rate | 25.15 fps by the tablet's clock, jitter ~1 ms, max interval ~41 ms |
| Start sequence (stream first) | One 512-byte partial frame at stream start. `0x8004` accepted at +0.3 s; frames pass the checks within 3 frames. `0x8020` accepted, then one burst of short frames: 2 to 9 frames on a warm camera (40–55 KB down to ~460 bytes each), 6 and 88 after the two logged power-ups. After power-up, up to 50 partial frames also precede the first full one. All of this happens before Running. `0x8000` 0.5 s later, unless the camera is calibrating after power-up |
| Shutter cycle after `0x8000` (camera already warm) | Repeated frames start ~0.1 s after the command: the camera repeats one frame 30 times (1.23 s), then fresh frames resume. Q+1 (shutter temperature) updates during the cycle unless it moved less than 0.1 K. Four commands behaved alike: one at start-up and three 14.5 s apart |
| `0x8020` alone (camera already warm) | No cycle. In two reconnects with the start-up `0x8000` skipped, fresh frames continued throughout. One malformed frame follows the command |
| Warm camera, no commands | No cycle in 929 s (FPA 30.88 → 31.07 °C). While the camera stays powered, a reconnect's first frame arrives within 0.1 s of stream start, already in raw mode before `0x8004` is sent |
| After power-up | The camera runs its own calibration cycles for ~65 s, then none. See "Power-up calibration" below |
| Noise, flat wall (FPA ~31 °C) | Temporal 1.22–1.30 counts, ~26 mK at 20.8 mK/count (the oracle's LUT slope at the wall's level). Fixed pattern after removing a 2nd-order surface: 3.2–3.4 counts, mostly column stripes (2.0–2.2 counts; rows 0.4). It didn't grow over 14 minutes at a steady FPA |
| Reconnect and replug | Clean teardown and reopen. 178–180 fds (always one USB fd) and 33–34 threads before and after, so nothing leaks |
| Background and foreground | Home, then back after 35 s and after 69 s. The camera stays warm: no power-up series. Our start-up `0x8000` gave one cycle each time (the owner heard one pair of clicks), and Running began 0.36 s after the freeze. Afterwards 178 fds (one USB) and 34 threads |
| Performance (optimized native code) | Latency p50 3.2 / p95 4.3 ms (callback to buffer swap); processing p95 ~1 ms; no drops, overruns or rejects over 8700+ frames |

Commands: stats CSVs from the debug "Stats CSV" option, pulled with `adb pull /sdcard/Android/data/dev.thermalview/files/stats/…`, then `tools/py/shutter_stats.py`. Noise came from four dumps through `tools/py/flat_noise.py`. fds and threads came from `run-as dev.thermalview ls /proc/<pid>/fd` and `…/task`.

### Power-up calibration (M1, 2026-09-24)

After power-up the camera calibrates itself on a fixed schedule, then stops. Times below are from power-up: the Type-C attach in logcat (`android.hardware.usb@1.2-service-qti: partner added`). The owner listened to one power-up with the start-up `0x8000` skipped and heard pairs of clicks at about 0, 6, 8, 15, 30 and 60 s. Each pair is a close and an open; within counting accuracy they match:

| Time after power-up | Event | Seen as |
|---|---|---|
| 0 s | Clicks | Heard |
| 3.1 s | USB enumeration; the app starts streaming | logcat `UsbHostManager: USB device attached` |
| 5.1 s | First frame, 2 s after stream start (a warm camera takes under 0.1 s). One near-uniform image repeats (raw mean ~11350, sd ~120) | CSV |
| ~6 s | Clicks, during the repeated frames | Heard |
| 9.1 s | One fresh frame: raw mean ~11290, sd ~1040, apparently uncorrected | CSV |
| 9.15 s | Cycle: 31 repeated frames, 1.23–1.27 s | CSV, field log |
| 10.4 s | Fresh frames at the normal level (raw mean ~5340, sd ~9 on a wall) | CSV |
| 13.1 s, 37.0 s, 64.8 s | Cycles, same length | CSV, field log |
| after 65 s | No cycle in the following 392 s, although the FPA rose 30.91 → 31.65 °C | CSV |

- **Timer, not temperature.** Three power-ups gave the same gaps between the last three cycles to within 5 ms: 23.85 s and 27.83 s. Meanwhile the FPA moved by different amounts (+0.13, +0.61 and +0.24 °C).
- **Our `0x8000` has no visible effect during the calibration.** In the earlier power-up, streaming began 7.2 s after power-up and the start-up `0x8000` went out at 8.3 s. The timeline still matched the uncommanded run: the 9.15 s cycle ended at 10.35 s (10.44 s without the command), and the later cycles fell on the same times to the millisecond. That power-up's time comes from logcat's `USB_DEVICE_ATTACHED` event minus the 3.1 s enumeration delay, and the schedule itself puts it within 0.04 s of that.
- **We can't tell whether `0x8020` matters at power-up.** It went out at 5.3 s, during the repeated frames. The later level matches warm starts. Either the camera applied the range or it already starts in the normal range.
- **Third power-up (04:23), running the new start-up rule.** The app saw the repeated frames at 5.15 s and skipped its `0x8000`. It held through the camera's calibration: repeats until 9.05 s, then a cycle from 9.13 to 10.36 s. Running began at 10.71 s. The cycles at 13.10, 36.95 and 64.78 s followed, with gaps of 23.852 and 27.828 s, and none in the 115 s after. The command log shows only `0x8004` and `0x8020`. This time the owner heard 5 pairs rather than 6. The pairs at 0 s and ~6 s leave no trace in the frames, so the data can't say which one went unheard.
- **Cold vs warm starts are easy to tell apart.** After power-up the stream opens with repeated frames; a warm camera's first frames are fresh. The app logs which kind of start it saw ("stream began with repeated frames").

Commands: `adb logcat -d -b all` (plug-in and enumeration times), the field log, and `tools/py/shutter_stats.py STATS.csv --offset 3.13` on the power-up CSV.

## Mac toolchain (verified 2026-09-24)

| Component | Version | Verified by |
|---|---|---|
| macOS | 26.6.2 (25G83), Apple Silicon | `sw_vers` |
| Homebrew | 7.0.6 | `brew --version` |
| git | 2.54.0 (Apple Git-157); repo initialized on `main` | `git --version` |
| libusb | 1.0.30 (Homebrew) | `brew list --versions libusb` |
| Python for `tools/py` | CPython 3.13.13 (uv-managed), venv in `tools/py/.venv` | `tools/py/.venv/bin/python --version` |
| pyusb | 1.3.1 | `tools/py/requirements.txt` |
| CMake for Mac builds | 4.4.3 (Homebrew), with Ninja 1.13.2 | `cmake --version`, `ninja --version` |
| JDK | 17.0.20 (Homebrew `openjdk@17`), which AGP 8.x requires. It isn't on the default PATH: set `JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home` | `$JAVA_HOME/bin/java -version` |
| Android SDK | `~/Library/Android/sdk`, command-line only (no Android Studio). It's a symlink to `/opt/homebrew/share/android-commandlinetools`, the Homebrew `android-commandlinetools` cask's directory, so uninstalling or reinstalling that cask could wipe the NDK and platforms. Platforms android-35, android-36 and android-37.0 (added for compileSdk 37); build-tools 35.0.0, 36.0.0, 36.1.0; cmdline-tools 20.0; platform-tools 37.0.0 (adb 1.0.41) | each package's `source.properties`; `ls -la ~/Library/Android` |
| NDK (pinned) | 28.1.13356709 (AGP 9.4's default is 28.2.13676358; the pin builds fine) | `ndk/28.1.13356709/source.properties` |
| CMake for Android builds | 3.31.6 (SDK package, bundles Ninja) | `sdkmanager "cmake;3.31.6"`; the hello-world's `CMakeCache.txt` |
| Gradle, AGP, Kotlin | Gradle 9.6.0 (wrapper), AGP 9.4.1 (Kotlin built in, so no `kotlin-android` plugin), Kotlin 2.4.20 (through the Compose compiler plugin; AGP's own 2.2.10 resolves up to it). This is the current stable line; the owner's older cache held AGP 8.10.0 / Gradle 8.11.1 | hello-world build below; `./gradlew buildEnvironment` |
| Compose | BOM 2026.09.00 (Compose 1.12.1), activity-compose 1.13.0. Compose 1.12 requires compileSdk 37; targetSdk stays at the tablet's API level | hello-world build below |

### Hello-world build (M0 step 3, part 1)

2026-09-24: a throwaway project in the session scratchpad — Compose UI showing a string from a C++20 JNI library; minSdk 31, compileSdk 37, targetSdk 34, `arm64-v8a` only. `JAVA_HOME=… ./gradlew :app:assembleDebug` → BUILD SUCCESSFUL; the APK contains `lib/arm64-v8a/libhello.so`, built by NDK 28.1.13356709 and CMake 3.31.6 for `android-31`. On the tablet: `adb install` → Success; `adb shell am start -W` → cold launch in 991 ms; logcat shows `Hello from C++ (__cplusplus = 202002)`, and a screenshot shows Compose rendering it in landscape at 2560 × 1600. Uninstalled afterwards.

## Tablet — Xiaomi Pad 5 Pro 12.4 (verified 2026-09-24, over wireless adb)

Commands: `adb shell getprop <prop>`, `pm list features`, `wm size`, `wm density`, `dumpsys display`, `dumpsys SurfaceFlinger`, `dumpsys sensor_privacy`, `dumpsys package <pkg>`.

| Fact | Value | Against the docs |
|---|---|---|
| Model | "Xiaomi Pad 5 Pro 12.4", model 22081281AC, device `dagu` | Matches |
| OS | Android 14, API 34, security patch 2025-06-01. HyperOS OS2.0.10.0.ULZCNXM, the China ROM (region CN, locale zh-CN). Build UKQ1.240624.001 (`ro.build.display.id`) | Matches (HyperOS 2 on Android 14). M1's targetSdk = 34 |
| ABI | arm64-v8a (also lists armeabi-v7a, armeabi) | Matches |
| USB | `android.hardware.usb.host` and `android.hardware.usb.accessory` present | Matches |
| Display | Native 1600 × 2560 (portrait), so 2560 × 1600 in landscape. `wm density` 320 is the logical density; the panel reports 244.52 × 244.45 dpi (`dumpsys display`, and SurfaceFlinger x-dpi/y-dpi). The full 4:3 fit, 2133 × 1600 px, is therefore 10.9" diagonal | Matches Xiaomi's 244 ppi |
| Refresh modes | Exactly two: id 1 = 120 Hz, id 2 = 60 Hz. No 50 or 100 Hz mode, so M5 keeps the panel at 120 Hz. SurfaceFlinger showed 60 Hz while idle at check time | Matches (60/120 Hz) |
| GPU | Adreno 650, OpenGL ES 3.2, driver V@0502.0 dated 2021-10-04; `ro.opengles.version` 196610 = 0x30002 | Matches |
| Camera privacy toggle | Not set (`dumpsys sensor_privacy` lists no toggles) | — |
| App storage | The adb shell can read `/sdcard/Android/data`, M1's dump pull path | Confirms PLAN M1 |
| App stores | Xiaomi app store (`com.xiaomi.market`) and Google Play both installed | — |
| adb | Wireless debugging at 192.168.1.114; this Mac was already paired. The connect port changes whenever wireless debugging restarts (41373, then 44269). During camera testing it dropped twice: once briefly (reconnecting on the same port worked), and once for good — the tablet still showed it on, but `adb mdns services` advertised nothing until it was restarted. When the tablet vanishes, check `adb mdns services` first, then ask the owner to toggle wireless debugging | Workflow note for M1 |
| USB chooser | On plug-in, Android offers the apps whose USB filters match: Hti Image, Xtherm infrared, InfiCam (InfiCamPlus) and ThruTracker Recorder. Apps already running with the camera attached ask for permission directly | M0 compatibility matrix |
