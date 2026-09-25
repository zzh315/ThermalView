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
| Pillow | 12.3.0 | MIT-CMU (HPND) | `tools/py/requirements.txt` | Screenshot analysis, benchmark crops and contact sheets |
| ht301_hacklib (github.com/stawel/ht301_hacklib @ `2f1498d`) | GPL-3.0 | `tools/py/vendor/ht301_hacklib.py` (vendored unmodified, with `LICENSE-ht301_hacklib`) | The M2 temperature oracle, `CameraEmulator` only (`tools/py/oracle.py`); golden tables in `native/core/tests/golden` |
| opencv-python-headless | 5.0.0.93 | Apache-2.0 | `tools/py/requirements.txt` | Imported by the vendored oracle; the Gaussian blur behind `tools/py/bench.py`'s `detail` metric |
| FFmpeg | 8.1.2 (Homebrew build on the dev Mac) | GPL-3.0-or-later (this build: `--enable-gpl --enable-version3`) | External command, not vendored or linked | `tools/py/bench.py` renders the side-by-side benchmark clips with it |
| bm3d (Tampere University, Mäkinen et al.), with bm4d | 4.0.3 | Free for non-commercial use | A throwaway venv under `build/` for one experiment; not in `tools/py/requirements.txt`, nothing vendored or shipped | The reference BM3D against stage 4b's non-local means on known texture (PIPELINE_LOG, 2026-09-25) |

## Approaches adapted (ideas, no code copied)

| Source | License | What we took |
|---|---|---|
| InfiCam (gitlab.com/netman69/inficam @ `531aa81`) | MIT | The Android connect sequence (no-device-discovery option, own libusb context and event thread, `uvc_wrap` on the `UsbManager` fd) and the teardown order — docs/PRIOR_ART.md pass 1 items 1 and 3 |
| InfiCamPlus (github.com/diminDDL/InfiCamPlus @ `6fad1f3`) | MIT | The frame-validation rules behind docs/PROTOCOL.md's sanity checks (pass 1 item 4) |
| ht301_hacklib (`2f1498d`) | GPL-3.0 | The temperature math in `native/core/src/temperature.cpp`, ported line for line from `Camera.info()`, `wvc()`, `atmt()` and `get_temp_table()` (docs/PROTOCOL.md: provenance traces to decompiled vendor libraries via xtherm-python). A port of GPL code: revisit before ever publishing |
| InfiCam (`531aa81`) | MIT | The over-range lockout idea (repeat `0x8000` to hold the shutter) and the high-range variant without the `cal_00` correction (`InfiFrame::update`) |
| ThruTracker Recorder (behavior described in its release notes) | proprietary | A stall watchdog, stall banner and field log (pass 1 item 6) |
| He, Sun and Tang, "Guided Image Filtering" (ECCV 2010 / TPAMI 2013) | paper | Stage 6's self-guided base/detail split (`native/core/src/filters.cpp`), written from the paper's box-filter formulation |
| Björn Ottosson, "A perceptual color space for image processing" (2020, bottosson.github.io) | published formulas (public domain) | OKLab and OKLCh conversions in `native/core/src/palette.cpp`: the paper's matrices, our code |
| Buades, Coll and Morel, "A non-local algorithm for image denoising" (CVPR 2005) | paper | Stage 4b's non-local means (`native/core/src/filters.cpp`), written from the paper; its weighting (mean absolute patch difference, Gaussian weight, the pixel itself at weight 1) follows what OpenCV's `fastNlMeansDenoising` does for 16-bit data, which `tools/py/nr_study.py` uses as a reference (OpenCV is listed above) |
| Lee, "Digital image enhancement and noise filtering by use of local statistics" (IEEE TPAMI 2(2), 1980) | paper | The local Wiener filter compared in `tools/py/nr_study.py` (not shipped) |
| Unser, IEEE Signal Processing Magazine 16(6), 1999; Thévenaz, Blu and Unser, IEEE TMI 19(7), 2000 | papers | The cardinal cubic B-spline's recursive prefilter (pole √3 − 2, mirror boundaries) in `native/core/src/upscale.cpp`, written from the papers' algorithm |

## Adapted code

| Source | License | Where | What |
|---|---|---|---|
| AMD FidelityFX Super Resolution 1.0, `ffx_fsr1.h` (EASU), Copyright (c) 2021 Advanced Micro Devices, Inc. | MIT (notice below) | `native/core/src/upscale.cpp`, `easuSample` | EASU's edge-adaptive 12-tap filter, adapted to one scalar channel with exact arithmetic. Experimental: in the harness only, for M5/M6's high-zoom comparison |

AMD's notice for the adapted EASU code:

> Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
> Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
