# Pipeline log

Every image-pipeline experiment and its verdict (CLAUDE.md rule 4, docs/PLAN.md M3–M4). Newest first. Each entry: what changed and which approach it adopts or improves on (PRIOR_ART.md), metrics before and after, contact sheets, clips where the effect is temporal, and the owner's verdict.

Run: `tools/py/.venv/bin/python tools/py/bench.py` (about 20 s; `--no-clips` for metrics and sheets only). It builds and runs `harness bench`, writes `bench/results/<label>.json` and the half-size sheets in `bench/results/<label>/`, and keeps full-size sheets and clips in `bench/out/` (local). The label is the commit, suffixed `-dirty` for uncommitted pipeline code. Metric definitions: PLAN.md M3 and `tools/py/bench.py`'s docstring.

## 2026-09-24 — M1 baseline (`d9a2ffe`)

**What:** the M1 display path, unchanged. Each frame's raw min…max is stretched linearly to grey and drawn nearest-neighbour (`renderer.cpp`); `native/core`'s `renderBaseline` is its CPU reference.

**Captures:** `bench/<scene>/`, 2026-09-24 22:10–22:40, with `tools/bench_scene.sh`. The camera was warm (FPA ~36 °C), and each dump starts 3 s after a NUC. Each reference app was captured a few minutes after our dump, so moving content (hand, reflections in the laptop screen) differs between panels.

**Metrics** (`bench/results/d9a2ffe.json`; display levels are 8-bit):

| Metric | Scene | Baseline |
|---|---|---|
| Temporal noise | `flat` | 6.20 levels; 24.7 mK, of which 11.5 mK is shared by all pixels; lag-1 ρ 0.72 |
| Stripes, columns / rows | `flat` | 3.00 / 2.92 levels; 11.6 / 11.3 mK |
| Flicker | `flat` / `room` / `keyboard` | 2.42 / 0.98 / 0.57 levels |
| Sharpness (edge FWHM) | `hand` wrist, palm, thumb / `keyboard` screen edge | 2.66, 1.94, 2.52 / 1.44 px |
| Halo | `hand` / `keyboard` | 0, 0, 0.2 % / 2.0 % (the scene's own: the background is slightly darker right beside the bezel) |
| Detail (key gaps) | `keyboard` | 4.09 levels; 81.7 mK |
| On device | — | 25.15 fps, no drops. M1: latency p95 4.3 ms, processing p95 ~1 ms (DEVICE.md). With M2's per-frame table and readouts, and the stats CSV on: processing p95 3–7 ms, latency p95 6–10 ms (debug overlay, M2 sessions) |

**Reading:** a per-frame stretch amplifies whatever range the frame has. `flat`'s wall spans only ~1 °C, so its 24.7 mK of noise becomes 6.2 levels. The frame's extremes are noisy too, so the whole image pumps by 2.4 levels. The camera already filters in time and space (DEVICE.md "Onboard filtering"), so what's left is low-frequency: noise that crawls rather than sparkles, plus the shared slow wander.

**Contact sheets:** `bench/results/d9a2ffe/<scene>.jpg`. Each row is ours at a reference app's on-screen size next to that app: Hti Image, then Xtherm, then InfiCamPlus.

**Clips:** `bench/out/clips/<scene>.mp4` (local), with the same layout. Ours is the whole dump (8 s); the app's is seconds 1–9 of its recording.

**Best reference per scene** (the bar for M4; the owner's pick): pending review.
