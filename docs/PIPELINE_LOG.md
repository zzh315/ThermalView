# Pipeline log

Every image-pipeline experiment and its verdict (CLAUDE.md rule 4, docs/PLAN.md M3–M4). Newest first. Each entry: what changed and which approach it adopts or improves on (PRIOR_ART.md), metrics before and after, contact sheets, clips where the effect is temporal, and the owner's verdict.

Run: `tools/py/.venv/bin/python tools/py/bench.py` (about 20 s; `--no-clips` for metrics and sheets only). It builds and runs `harness bench`, writes `bench/results/<label>.json` and the half-size sheets in `bench/results/<label>/`, and keeps full-size sheets and clips in `bench/out/` (local). The label is the last commit that changed the display path or the metrics code (`native/core/`, `tools/harness/`, `palettes/`, `tools/py/bench.py`: what the harness runs), suffixed `-dirty` while those have uncommitted changes. Metric definitions: PLAN.md M3 and `tools/py/bench.py`'s docstring.

## 2026-09-25 — Stage 6: detail enhancement (`b848b57+detail`: stages 1–6; awaiting the owner's verdict)

**What:** PLAN M4 stage 6, as the approved plan change (PRIOR_ART pass 2 item 8): a guided-filter base/detail split before tone mapping (`native/core` `filters.h`, `Pipeline::enhance`).
- **Split:** He et al.'s guided filter with the signal as its own guide (radius 2, eps 50 counts²) gives the base; detail = signal − base.
- **Tone:** stage 5's curve comes from the base, so noise and fine texture don't bend it. The detail comes back at the curve's local slope: out = T(base) + T′(base) × detail.
- **Gain:** 2.5 on the detail, limited to ±7 counts, applied only where:
  - a **noise gate** is open: the detail's 3×3 RMS clears 2–4× the frame's noise floor (its 10th percentile, sampled over every column);
  - the **halo guard** allows it: the guided filter leaves a residual of about 2% of every step, up to ~5 px out, along both sides. Boosted, that residual is a rim (the first version drew one: `keyboard` screen-edge halo 2.66 → 5.77%). The gain fades out where the base's local range within 6 px is 12–25× the local detail's RMS. The rule is scale-free, so it holds for a 50-count edge and a 1000-count one alike, while texture, a good fraction of its own local range, keeps its gain. A unit test builds a lens-blurred step and checks the rim stays under half a level (over one without the guard).
- **Smoothing:** the added contrast is blurred (σ 1 px) before it's added. Texture a few pixels across keeps its gain; pixel-scale structure (noise, the stair-steps of a slanted edge) passes at gain 1.
- **Unsharp:** 1.0 (σ 0.7 px) on the display where there is texture or an edge, clamped to each pixel's 3×3 min/max so it can't overshoot.

**Why unsharp 1.0 and the smoothing:** at 1.5 with no smoothing (the "crisp" variant), each edge shrank to about a pixel, losing where it sits within the pixel, and slanted key edges came out as stair-steps at 8× (`stage6_review/keyboard_crop0.jpg`, right). That is the "jagged" look the owner dislikes. The proposed settings keep most of the gain without it.

**Metrics** (stages 1–5 = `903d02a+default`; the crisp variant from a `--no-clips` run):

| Metric | Stages 1–5 | Stage 6 (proposed) | Crisp (unsharp 1.5, no smoothing) |
|---|---|---|---|
| `keyboard` key detail | 4.66 levels | 6.42 | 7.29 |
| `keyboard` screen edge: width / halo | 2.10 px / 2.66% | 1.77 px / 2.73% | 1.15 px / 2.71% |
| `hand` edges: wrist / palm / thumb width | 2.73 / 2.10 / 2.59 px | 2.23 / 1.56 / 2.39 | 2.07 / 1.47 / 2.27 |
| `hand` halos | 0 / 0 / 0.46% | 0 / 0 / 0.50% | 0 / 0 / 0.5% |
| `flat` / `flat_aged` display noise | 1.40 / 1.42 levels | 1.41 / 1.44 | 1.43 / — |
| Fixed pattern, stripes, `motion` step response | — | unchanged | unchanged |
| Flicker `flat` / `room` / `keyboard` / `night` | 0.15 / 0.09 / 0.24 / 0.13 | 0.22 / 0.13 / 0.24 / 0.11 | 0.22 / 0.13 / 0.23 / 0.11 |

- **Against the apps** (captures, `refs.json`): key detail Hti 2.60, Xtherm 3.49, InfiCamPlus 10.3. Screen-edge halo Hti 14.2%, Xtherm 1.37%, InfiCamPlus 2.17%. Our edge widths are at camera resolution, the apps' through their capture, so the two don't compare directly.
- **Flicker** on `flat` and `room` rises by 0.04–0.07 levels. That comes from mapping the base instead of the signal (it's there at gain 1 with no unsharp), not from the enhancement. It stays at or below Xtherm's 0.21–0.22 and far below visible.
- **Noise isn't boosted:** the gate keeps `flat` at 1.41 levels. A looser gate (1.5–3×) gave 7.83 key detail at 1.67 levels of noise.

**Cost** (`harness perf` over adb, `keyboard`, a big core at 2.42 GHz): 1.76 → 4.64 ms a frame. On a little core (A55, 1.8 GHz) it's 9.5 → 29.9 ms, over the budget. So the app now pins its processing thread to the big cores, not yet verified in the app (commit `def22ad`).

**Found on the way:**
- A NaN on very quiet fields: running sums dip a hair below zero before a sqrt.
- The noise floor's subsample, every 192nd pixel, only ever hit 4 columns (192 = ¾ × 256). Now it's every 191st.

**Review:** `bench/results/b848b57+detail/stage6_review/` shows stages 1–5, stage 6 as proposed, and the crisp variant side by side: `keyboard` (with the keys at 8× and the screen edge), `room`, `night` (the car at 8×), `hand`, `flat_aged`. Sheets against the baseline and the apps: `bench/results/b848b57+detail/<scene>.jpg`.

**Verdict:** pending the owner's review (proposed vs crisp vs off). Stage 6 stays off by default until then (debug toggle "Stage 6: detail + sharpening").

## 2026-09-25 — Stage 5: automatic tone mapping (`903d02a+default`: stages 1–5)

**What:** PLAN M4 stage 5 as FLIR-style plateau equalization (PRIOR_ART pass 2 item 8; `native/core` `tone.h`):
- **Range:** robust percentiles 0.3 / 99.7 %. The survey's 99.9 % let the small hot spot in `room` jitter the range.
- **Curve:** a double-plateau histogram equalization, blended 20 % with linear.
- **Gain cap:** each bin's rise is capped at max gain. What the cap cuts off is redistributed to bins still under it (like CLAHE's clip limit), so a flat scene stays a calm band while separate zones still spread apart.
- **Damping:** the range expands in 0.1 s and contracts in 1.3 s, with a deadband; the curve eases over 2 s (0.3 s made a moving hand pump).
- **Offset tracking:** the curve works on the signal minus a tracked global offset (the median frame-to-frame change), so calibration steps and the camera's wander don't show.
- **Clipped pixels** stay out of the statistics.
- **Start-up:** the first frame takes its curve directly. Easing in from a neutral curve looked like a second or two of harsh contrast at every start, and it had inflated the first measurements.

**The gain cap was the owner's call** (`bench/results/903d02a+default/stage5_gain_caps.jpg`). At 1 level per count, `room` came out washed out (levels 81–172), like Hti. At 2.0 it has real blacks and whites (35–216), while `flat` stays a band (96–160). The owner chose 2.0.

**Metrics** (stages 1–5 against the baseline; the reference apps from `bench/results/refs.json`):

| Scene | Levels used (baseline → now) | Xtherm | Flicker (baseline → now) | Xtherm |
|---|---|---|---|---|
| `flat` | 27–210 → 95–161 (a calm band) | 133–184 | 2.41 → 0.15 | 0.21 |
| `room` | 25–152 → 34–216 | 41–152 | 0.98 → 0.09 | 0.22 |
| `keyboard` | 12–239 → 11–244 | 17–204 | 0.54 → 0.13 | 0.13 |
| `night` | 26–238 → 14–243 | 23–210 | 0.67 → 0.17 | 0.11 |
| `hand` | 3–246 → 9–244 | 9–242 | 0.19 → 0.13 | 0.09 |

- **`flat` display noise:** 6.2 → 1.4 levels (Xtherm 0.82 at its lower gain).
- **`keyboard` key detail:** 4.09 → 4.66 levels (Xtherm 3.49, InfiCamPlus 10.3).
- **`shutter`:** the biggest frame-to-frame step at the calibration is 2.0 levels (43.7 at baseline), and the total visible change 9.5 (49.1).
- **`motion`:** step response unchanged.
- **Caveat:** the `keyboard` screen edge's half-slope width reads 1.42 → 2.10 px. The signal's edge is identical; equalization gives the dense screen and background more output and the sparse in-between values less, which flattens the transition's middle in display space. On tone-mapped output, edge metrics partly measure the curve's shape.

**Cost on the tablet** (a replay of `shutter`): processing p95 8.9 ms with stages 1–5, latency p95 11.2 ms.

**Verdict:** approved by the owner (2026-09-25) at gain cap 2.0, and on by default.

## 2026-09-25 — Stage 4: motion-adaptive temporal filter (`fb23aa7+denoise`, `fb23aa7+denoise_denoiseK-0.1`)

**What:** PLAN M4 stage 4, as the survey's top pick (PRIOR_ART pass 2 item 8): a per-pixel recursive filter, y += K (x − y).
- **K** is k_min where nothing moves and 1 where something does.
- **Motion** is the 3×3 box of (x − y) measured against its own noise level, a robust estimate over the frame smoothed over ~1 s: smoothstep between 2 and 4 σ.
- **After a calibration** the filter starts over, and stage 1 holds it frozen through the cycle.

The camera already runs its own motion-adaptive filter (DEVICE.md "Onboard filtering"), so this one gains less than it would on white noise.

**Metrics** (stages 1–3 on in all columns):

| Metric | Baseline | k_min 0.25 | k_min 0.10 |
|---|---|---|---|
| `flat` temporal noise | 24.7 mK | 17.1 mK (×0.69) | 11.9 mK (×0.48) |
| `flat` temporal noise, display | 6.2 levels | 4.5 | 3.5 |
| Lag-1 correlation of the noise | 0.72 | 0.95 | 0.98 |
| `motion`: step done 1 / 2 / 4 frames later | 0.79 / 0.94 / 0.98 | 0.80 / 0.95 / 0.98 | 0.81 / 0.96 / 0.98 |
| Flicker, `flat` / `room` / `night` | 2.42 / 0.98 / 0.67 | 2.23 / 0.78 / 0.58 | 2.17 / 0.62 / 0.62 |
| `keyboard` detail, `hand` and `keyboard` edges | — | unchanged | unchanged |

- **No trails:** the moving hand's edges respond as fast as the camera's own.
- **The survey's caution** (synthetic, not in our scenes): faint objects moving slowly keep 83–94 % of their contrast at k_min 0.25 and 64–85 % at 0.10.

**Clips:** `bench/out/stage4_{flat,motion,night,room}.mp4` (local), with three panels: baseline, k_min 0.25, k_min 0.10.

**Verdict:** the owner (2026-09-25) "could not tell any difference" and left it to me, provided it doesn't hog performance. It's on by default at k_min 0.25, for two reasons:
- It's the safest setting for faint moving detail.
- Its gain shows through stage 5: the tone curve's gain cap scales with the residual noise, so lower noise buys contrast on low-ΔT scenes.

**Cost on the tablet:** ~1.3 ms. Processing p95 is 7.0 ms and latency p95 9.7 ms with stages 1–4.

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

**Verdict:** approved by the owner (2026-09-25). Stages 3a and 3b are on by default.

**Cost on the tablet:** processing p95 went 3.9 → 12.2 ms with 3b's first version (per-pixel neighbour loops and 448 medians a frame). Two rewrites brought it to 5.7 ms, with latency p95 8.2 ms:
1. Sliding sums, row-major.
2. Gated means instead of medians. The gate already bounds every sample, and the metrics are unchanged to within 0.1 mK.

The map is bundled as an APK asset from `native/core/data`, and the overlay shows the drift being compensated.

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
