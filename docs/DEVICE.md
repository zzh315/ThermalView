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
| adb | Wireless debugging at 192.168.1.114; this Mac was already paired. The connect port changes whenever wireless debugging restarts | — |
