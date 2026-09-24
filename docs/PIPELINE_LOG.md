# Pipeline log

Every image-pipeline experiment and its verdict (CLAUDE.md rule 4, docs/PLAN.md M3–M4). Newest first. Each entry: what changed and which approach it adopts or improves on (PRIOR_ART.md), metrics before and after, contact sheets, clips where the effect is temporal, and the owner's verdict.

Run: `tools/py/.venv/bin/python tools/py/bench.py` (about 20 s; `--no-clips` for metrics and sheets only). It builds and runs `harness bench`, writes `bench/results/<label>.json` and the half-size sheets in `bench/results/<label>/`, and keeps full-size sheets and clips in `bench/out/` (local). The label is the last commit that changed the display path or the metrics code (`native/core/`, `tools/harness/`, `palettes/`, `tools/py/bench.py`: what the harness runs), suffixed `-dirty` while those have uncommitted changes. Metric definitions: PLAN.md M3 and `tools/py/bench.py`'s docstring.

## 2026-09-25 — Stage 3: drift compensation and stripe cleanup (`7c8f0ba+drift_destripe`)

**What:** the owner approved this approach in place of PLAN's gated stripe tracker, after this evidence.
- **Between calibrations, every pixel drifts at its own fixed rate as the focal plane warms.**
- **The pattern repeats across sessions.** Maps from M1's warm-up on a wall (2026-09-24, 7 dumps, 1.1–8.3 °C of drift) and today's desk series (4 dumps, 2.3–5.2 °C) correlate at +0.99. That holds for all three parts: columns +0.91, rows +0.97, per-pixel +0.97.
- **The pattern is linear in the drift.** The spatial residual is 0.85 counts, ~2 % of the pattern.
- **The metadata gives the drift.** The shutter temperature (Q+1) keeps its calibration-time value, so dT = (FPA − shutter) − 0.40 °C, per frame, without tracking calibrations.

**Stages:**
- **3a** subtracts rate × dT × 0.9 before the other stages. The 0.9 fits the per-pixel part on `flat_aged` (0.86–0.91). The map is `native/core/data/drift_KA1213.f32`, built by `tools/py/drift_map.py`.
- **3b** tracks what's left in rows and columns:
  - gated medians of each pixel's residual against 8 neighbours;
  - integrated with τ 4 s;
  - clamped to ±2 counts, so a faint real line loses at most that much;
  - restarted at each calibration.

It's the survey's FPA-drift model (PRIOR_ART pass 2 item 8, stage 3 #2), taken per pixel, with the drift read from metadata.

**Metrics** (`bench/results/7c8f0ba+drift_destripe.json`; stages 1–2 on in both columns):

| Metric | Baseline | Stage 3 |
|---|---|---|
| `flat_aged` fixed pattern | 188.8 mK | 49.1 mK (a fresh `flat`: 47) |
| `flat_aged` column / row stripes | 45.9 / 65.9 mK | 12.4 / 23.7 mK |
| `flat` column / row stripes | 11.6 / 11.3 mK | 8.8 / 9.4 mK |
| `keyboard` key-gap detail | 81.7 mK | 80.7 mK (−1.2 %) |
| `keyboard` screen edge | 1.44 px, halo 2.0 % | 1.42 px, 1.5 % |
| `hand` edges, all flicker | — | unchanged or slightly better |
| `shutter`: total change at the calibration | 49.1 levels | 36.9 levels (the aged image before it is already mostly corrected) |

Drift alone (3a) gave 54.3 mK of fixed pattern and 21.5 / 28.2 mK of column/row stripes on `flat_aged`.

**Review:** `bench/results/7c8f0ba+drift_destripe/stage3_review.jpg`. On `flat_aged` the aged banding is gone and the desk's real structure shows; `keyboard` looks unchanged.

**Limits:**
- The map comes from two sessions at FPA 26–37 °C.
- Cooling (a negative dT), much colder ambients and the 6 % rate difference between sessions are untested. Refining the scale from each calibration's jump is the planned next step.
- Rows keep ~2× the fresh level.

**Verdict:** pending the owner.

## 2026-09-25 — Stage 2: software bad-pixel map (`27894b7+badPixels`)

**What:** PLAN M4 stage 2, as a diagnostic plus a guard (PRIOR_ART pass 2 item 8). `tools/py/bad_pixels.py` looks for pixels that are, on uniform dumps:
- stuck;
- noisy;
- blinking;
- offset from their 5×5 ring.

A pixel counts only if it's flagged in at least two dumps. Pixels on the list are replaced for display with the median of their good neighbours, and readouts skip them. Nothing is written to the camera.

**Finding:** KA1213 has no stuck, noisy or blinking pixels; the camera corrects its own. Two neighbours at the left edge, (2, 115) and (3, 115), drift as the FPA warms after a NUC:

| Pixel | Drift per °C of FPA change | After 6 min (~5 °C) | Right after a NUC |
|---|---|---|---|
| (2, 115) | −17.6 counts | −86 counts, a cold dot ~1.7 °C deep | within ~3 counts |
| (3, 115) | −8.7 counts | −43 counts | within ~3 counts |

Replacing them for display all the time costs nothing, because their neighbours carry the same scene.

**Metrics** (`bench/results/27894b7+badPixels.json`; stage 1 is on in both runs):

| `flat_aged` | Baseline | Stage 2 |
|---|---|---|
| Worst pixel against its 5×5 ring | 12.5σ, 1.74 °C | 5.5σ, 0.77 °C (just the aged pattern) |
| Pixels beyond 6σ | 2 | 0 |

`flat` (right after a NUC) and every other metric are unchanged.

**Zoom:** `bench/results/27894b7+badPixels/flat_aged_zoom.png`.

**Verdict:** approved by the owner (2026-09-25). Stage 2 is on by default.

## 2026-09-25 — Stage 1: shutter-cycle hold and crossfade (`f3d169c+shutter`)

**What:** PLAN M4 stage 1, as refined in pass 2 (PRIOR_ART item 8). An exact repeat of the image rows means a shutter cycle. The pipeline holds the last output without advancing any stage, and when fresh frames return it crossfades from the held output over 8 frames (0.3 s).
- **Freeze:** the same as FLIR's cameras, which freeze video during flat-field correction (Lepton, Boson).
- **Blend back:** no source describes one.
- **Left out:** the survey's motion-gated blend. After 6 minutes of drift, the NUC's own correction is a coherent row structure of ~25 counts, which a motion gate would take for motion.

**Scene:** `shutter`, a 250-frame dump through one `0x8000` (2026-09-25).
- Frames 60–89 repeat frame 59.
- The first fresh frame reads 38 counts lower and differs from the held one by a spatial std of ~29 counts: the aged pattern (9.7 counts, mostly rows) goes, leaving 2.2.
- That first fresh frame also carries strong column streaks that the following frames don't. The crossfade dilutes it to 1/9.

**Metrics** (`bench/results/f3d169c+shutter.json`):

| `shutter` | Baseline | Stage 1 |
|---|---|---|
| Freeze | 31 frames | 31 frames |
| Biggest frame-to-frame change of the display in the second after the freeze | 43.7 levels | 11.0 levels |
| Total change the NUC brings | 49.1 levels | 49.1 levels |

The other seven scenes are identical to the baseline, since none of them contains a cycle.

**Clip:** `bench/out/clips/shutter.mp4` (local): baseline left, stage 1 right; the switch comes at ~3.6 s.

**Verdict:** approved by the owner (2026-09-25): keep the 0.3 s crossfade. Stage 1 is on by default (`PipelineOptions`), and later "pipeline" columns include it.

## 2026-09-24 — M1 baseline (`31051f5`)

**What:** the M1 display path, unchanged. Each frame's raw min…max is stretched linearly to grey and drawn nearest-neighbour (`renderer.cpp`); `native/core`'s `renderBaseline` is its CPU reference.

**Captures:** `bench/<scene>/`, 2026-09-24 22:10–22:40 indoors and 23:28 for `night` (a driveway: a car, houses across the street, a shrub; 15–21 °C, no sky in view; handheld but held steady; air 15–20 °C), with `tools/bench_scene.sh`. The camera was warm (FPA 35–37 °C), and each dump starts 3 s after a NUC. Each reference app was captured a few minutes after our dump, so moving content (hand, reflections in the laptop screen) differs between panels. Environment inputs, used as-is: `flat` has the camera's power-up defaults (emissivity 0.98, reflected and air 25 °C, humidity 0.45, distance 0). The later scenes have InfiCamPlus's writes from the scene before (0.95, 20 °C, 20 °C, 0.50, 1), which persist until a replug; the results record them per scene.

**Metrics** (`bench/results/31051f5.json`; display levels are 8-bit):

| Metric | Scene | Baseline |
|---|---|---|
| Temporal noise | `flat` | 6.20 levels; 24.7 mK, of which 11.5 mK is shared by all pixels; lag-1 ρ 0.72 |
| Stripes, columns / rows | `flat` | 3.00 / 2.92 levels; 11.6 / 11.3 mK |
| Flicker | `flat` / `room` / `keyboard` / `night` | 2.42 / 0.98 / 0.57 / 0.67 levels |
| Sharpness (edge FWHM) | `hand` wrist, palm, thumb / `keyboard` screen edge | 2.66, 1.94, 2.52 / 1.44 px |
| Halo | `hand` / `keyboard` | 0, 0, 0.2 % / 2.0 % (the scene's own: the background is slightly darker right beside the bezel) |
| Detail (key gaps) | `keyboard` | 4.09 levels; 81.7 mK |
| On device | — | 25.15 fps, no drops. M1: latency p95 4.3 ms, processing p95 ~1 ms (DEVICE.md). With M2's per-frame table and readouts, and the stats CSV on: processing p95 3–7 ms, latency p95 6–10 ms (debug overlay, M2 sessions) |

**Reading:** a per-frame stretch amplifies whatever range the frame has. `flat`'s wall spans only ~1 °C, so its 24.7 mK of noise becomes 6.2 levels. The frame's extremes are noisy too, so the whole image pumps by 2.4 levels. The camera already filters in time and space (DEVICE.md "Onboard filtering"), so what's left is low-frequency: noise that crawls rather than sparkles, plus the shared slow wander.

**Contact sheets:** `bench/results/31051f5/<scene>.jpg`. Each row is ours at a reference app's on-screen size next to that app: Hti Image, then Xtherm, then InfiCamPlus.

**Clips:** `bench/out/clips/<scene>.mp4` (local), with the same layout. Ours is the whole dump (8 s); the app's is seconds 1–9 of its recording.

**Owner's review** (2026-09-24, review page and clips):
- **Hti Image is first.** It has the best texture, but draws a bright rim around warm objects (a halo) and looks a bit washed out.
- **Xtherm is second.** It's smooth and clean.
- **Ours is good on `room`, and shows more detail than Xtherm.** But it's jagged (the M1 renderer's nearest-neighbour upscale) and not as smooth as Xtherm.

**Best reference per scene:** Hti Image for the five indoor scenes, with Xtherm second; the owner didn't split them by scene. `night`: InfiCamPlus, for sharpness. The owner finds Xtherm blurry there, and Hti sharper and clearer than Xtherm but washed out. The bar is Hti's texture and sharpness without its rim or washed-out look, and free of our blockiness without Xtherm's blur. The owner values sharpness: ours already shows more detail than Xtherm, and that detail must stay. In plan terms:
- the texture is stage 6 (detail enhancement), with `halo` held at the baseline's;
- the washed-out look is stage 5 (tone mapping);
- the blockiness is M5's upscaler, which has to stay sharp (no blur like Xtherm's).
