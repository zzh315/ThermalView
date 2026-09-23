# Third-party code

Everything here is used for personal use only. Revisit these licenses before ever publishing (CLAUDE.md) — in particular libusb's LGPL, which is statically linked into `libthermalview.so`.

## In the app

| Component | Version | License | Where | Use |
|---|---|---|---|---|
| libusb | v1.0.30 (`87a5563`) | LGPL-2.1-or-later | `native/third_party/libusb` (submodule) | USB access on the wrapped `UsbManager` file descriptor; built from its sources like `android/jni/libusb.mk` |
| libuvc | v0.0.8 (`4e9fc77`) | BSD-3-Clause | `native/third_party/libuvc` (submodule) | UVC stream negotiation, isochronous capture, the Zoom (Absolute) control write; built without JPEG |
| AndroidX Activity, Core, Compose (BOM 2026.09.00) | see `gradle/libs.versions.toml` | Apache-2.0 | Gradle dependencies | UI and lifecycle |
| Kotlin standard library | 2.4.20 | Apache-2.0 | Gradle dependency | — |

## In tests and tools only

| Component | Version | License | Where | Use |
|---|---|---|---|---|
| doctest | v2.5.3 (`2d0a935`) | MIT | `native/third_party/doctest` (submodule) | `native/core` unit tests |
| pyusb | 1.3.1 | BSD-3-Clause | `tools/py/requirements.txt` | Read-only descriptor dump |
| numpy | 2.5.3 | BSD-3-Clause | `tools/py/requirements.txt` | Analysis tools |
| Pillow | 12.3.0 | MIT-CMU (HPND) | `tools/py/requirements.txt` | Screenshot analysis |

## Approaches adapted (ideas, no code copied)

| Source | License | What we took |
|---|---|---|
| InfiCam (gitlab.com/netman69/inficam @ `531aa81`) | MIT | The Android connect sequence (no-device-discovery option, own libusb context and event thread, `uvc_wrap` on the `UsbManager` fd) and the teardown order — docs/PRIOR_ART.md pass 1 items 1 and 3 |
| InfiCamPlus (github.com/diminDDL/InfiCamPlus @ `6fad1f3`) | MIT | The frame-validation rules behind docs/PROTOCOL.md's sanity checks (pass 1 item 4) |
| ThruTracker Recorder (behavior described in its release notes) | proprietary | A stall watchdog, stall banner and field log (pass 1 item 6) |
