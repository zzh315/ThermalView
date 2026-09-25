# Pipeline log

Every image-pipeline experiment and its verdict (CLAUDE.md rule 4, docs/PLAN.md M3–M4). Newest first. Each entry: what changed and which approach it adopts or improves on (PRIOR_ART.md), metrics before and after, contact sheets, clips where the effect is temporal, and the owner's verdict.

Run: `tools/py/.venv/bin/python tools/py/bench.py` (about 20 s; `--no-clips` for metrics and sheets only). It builds and runs `harness bench`, writes `bench/results/<label>.json` and the half-size sheets in `bench/results/<label>/`, and keeps full-size sheets and clips in `bench/out/` (local). The label is the last commit that changed the display path or the metrics code (`native/core/`, `tools/harness/`, `palettes/`, `tools/py/bench.py`: what the harness runs), suffixed `-dirty` while those have uncommitted changes. Metric definitions: PLAN.md M3 and `tools/py/bench.py`'s docstring.

## 2026-09-26 — Simple settings: noise reduction and texture Off / Low / High (owner)

**The owner, after trying BM3D and 3c on the tablet:** "could not tell big difference, maybe have simple presets that abstract settings into simple low and high effects so that it's more intuitive", and on the trade-offs, "do what you think is best for performance".

**Now** (the debug panel's first two buttons, and PLAN M6's settings):
- **Noise reduction:** Off / Low / High. Low (the default) is stage 3c plus non-local means at h 0.8; High is the same at h 1.1. So stage 3c is on by default now, as part of noise reduction.
- **Texture:** Off / Low (×1.5, the default) / High (×3).
- **The technical switches stay below:** a setting changed there shows as "custom".

**Why non-local means, not BM3D:** the owner couldn't see a big difference, and BM3D doesn't fit the latency budget yet. Live, with 3c: latency p50 / p95 28.4 / 30.9 ms with BM3D (after the faster shaders, `8043c8a`), 18.4 / 20.5 ms with non-local means. On known texture, BM3D's gain is largest at High's noise (+6–16 points at 1–2 px), so it would come in there first if it fits the budget.

**The budget with the new default:** p95 20.5 ms, just over. Stages 5–6 (~7 ms live) are next.

## 2026-09-25 — Stage 4b: BM3D against NLM at matched noise, on stage 3c's output (evidence; awaiting the owner)

**Why:** the owner chose BM3D for detail (after "stripe fix, then BM3D"). Before a GPU port (a large job), this checks what it buys with stage 3c on, at the owner's strengths.

**Known texture** (`tools/py/nr_detail.py --synthetic` on `flat` through stages 1–3c, `8a9f180`; as in the detail-loss entry below):

| Filter | Noise | 0.7 px ×0.5 / ×1 / ×2 | 1 px ×0.5 / ×1 / ×2 | 2 px ×0.5 / ×1 / ×2 | Cost a frame |
|---|---|---|---|---|---|
| NLM 11×11, 0.8 (Low, now) | 0.56 | 0.64 / 0.80 / 0.98 | 0.69 / 0.83 / 0.97 | 0.83 / 0.89 / 0.97 | GPU ~4 ms |
| NLM 11×11, 0.9 (Medium) | 0.42 | 0.48 / 0.65 / 0.94 | 0.56 / 0.71 / 0.94 | 0.76 / 0.83 / 0.94 | |
| NLM 11×11, 1.1 (High) | 0.28 | 0.31 / 0.42 / 0.78 | 0.40 / 0.53 / 0.81 | 0.66 / 0.73 / 0.88 | |
| BM3D real-time, σ ×1.0 | 0.59 | 0.67 / 0.81 / 0.93 | 0.77 / 0.88 / 0.96 | 0.92 / 0.96 / 0.99 | ~86 M operations |
| BM3D real-time, ×1.1 | 0.51 | 0.58 / 0.75 / 0.91 | 0.71 / 0.85 / 0.95 | 0.90 / 0.95 / 0.98 | |
| BM3D real-time, ×1.2 | 0.44 | 0.50 / 0.68 / 0.88 | 0.65 / 0.81 / 0.93 | 0.88 / 0.94 / 0.98 | |
| BM3D, stride 4, search 7, groups 16/16, ×1.0 | 0.54 | 0.66 / 0.80 / 0.93 | 0.76 / 0.88 / 0.96 | 0.91 / 0.96 / 0.99 | ~401 M |
| BM3D, the reference parameters, ×1.0 | 0.49 | 0.63 / 0.79 / 0.92 | 0.74 / 0.87 / 0.96 | 0.91 / 0.96 / 0.98 | ~1908 M |

"Real-time" is the cost sweep's candidate: block 8, stride 6, search radius 5, groups 8/8, full groups (no match thresholds), λ 3.0, μ² 0.4.

- **At Low's noise** (interpolated), BM3D keeps texture differently: 1 px +6 / +4 / −1 points, 2 px +8 / +7 / +2. The finest texture is 0 / −1 / −6: strong 0.7 px texture is where BM3D's thresholds cost.
- **At Medium's noise:** 1 px +9 / +10 / −1, 2 px +12 / +11 / +4, 0.7 px +2 / +3 / −6.
- **The costlier configurations** add only 3–5 points at the same noise, so the real-time one keeps most of the gain.
- **Stage 3c changes this little:** on stages 1–3b the same filters leave 6–10% more noise (the stripes) and keep the same texture, within 2 points.

**On the real scenes** (`harness`, BM3D on the CPU at ×1.04, the strength that matches Low's noise on `flat`), BM3D leaves 5–15% less temporal noise than NLM Low: `room` 0.65 vs 0.76 counts, `keyboard` 1.02 vs 1.13, `flat_aged` 0.65 vs 0.69. Non-local means backs off in textured areas; BM3D's thresholds don't. At 8×: no blocking or ringing. BM3D is smoother inside the keys and on walls, edges and the curtain folds hold, and it leaves a faint low-frequency mottle in flat areas.

**Review:** side-by-side clips (stage 3c on in both; NLM Low | BM3D ×1.04), local: `bench/out/clips/bm3d_low/` (full frames, 4×) and `bench/out/clips/bm3d_low_crops/` (8×).

**The owner, on the clips** (2026-09-26): "The bm3d one looks good on the clips you saved. I need to check on the device with camera plugged in later to make sure." So it went to the GPU.

**On the tablet, as a preview** (`78a55de`):
- **What runs:** the real-time shape as GLES compute shaders: step 1, step 2 and two gathers (`native/android/gpu_bm3d_shader.cpp`).
- **Checked on the Mac:** `tools/py/gpu_bm3d_emulate.py` runs the generated shaders on the Mac, each workgroup's 64 invocations as threads meeting at every barrier. Against `tv::bm3d`: max 0.001–0.004 counts, mean ~1e-5, on a synthetic frame and `room`, five shapes.
- **Checked on the tablet** (nrCheck, the camera's frames): max 0.0005 counts, mean 1e-5. One scene had 9 pixels up to 0.014 counts, where near-tied matches round the other way.
- **Too slow for use:** ~17–19 ms a frame, where the NLM takes ~4. Per pass: step 1 ~7.5 ms, step 2 ~9.5, each gather ~1.5. Live with 3c: 25 fps and no dropped frames, but latency p50 / p95 39 / 47 ms, against 20.
- **Where the time goes:** stubbing out block matching (distances and ranking) saves only ~2 ms, and nor are the uniform window lookups the cost. The transforms' and tiles' barrier-heavy phases at low occupancy are the bulk. Optimizing that is next.
- **To look at it:** in the debug panel, turn on "Stage 3c" and set "Noise reduction filter" to BM3D. The Low / Medium / High presets give BM3D the strength that leaves each preset's noise.

**Verdict:** pending the owner's look on the device.

## 2026-09-25 — Stage 3c: the per-frame column and row noise (preview, `0a19f38`; awaiting the owner's second look)

**Why:** the owner chose "stripe fix, then BM3D".
- **The white-noise BM3D keeps the stripes.** The sensor's per-frame column/row noise is the same down a column, so block matching stacks same-column blocks and keeps it as structure. On synthetic column noise, BM3D removed 45% of the streaks, NLM 83%.
- **The correlated-noise BM3D does remove them,** but spends its filtering on them, so at the owner's Low setting it's no better than NLM.
- **On perfectly destriped frames,** the plain BM3D keeps 6–12 points more 1–2 px texture than NLM at every strength. So the stripes come off first, at the source.

**The stripes** (`flat`, `flat_aged`, `room`, after stage 3b):
- **Size:** fine column part (< ~16 px) ~0.26 counts per frame, rows ~0.18.
- **Spectrum:** in the noise power spectrum, the column axis sits 11.9× the white level and the row axis 8.2×.
- **Over time:** correlated at 0.77 frame to frame, 0.44 at 5 frames, 0.25 at 10 (single pixels: 0.76, 0.30, 0.15).
- **Spatially:** 35% of the column profile's power is at 4–16 px periods, so neighbouring columns partly share it.

**Method** (`tv/stripes.h`, stage 3c, after 3b):
- **The reference:** each frame against a 2 s average of the scene, kept in scene coordinates.
- **The offsets:** per column, then per row, a one-step M-estimate of frame − reference over the unchanged pixels (under 3 × 1.4σ) that are off strong edges. Their fine part is subtracted, clamped at ±1.5 counts. Nothing is blended: one offset per column and per row.
- **Motion:** global Lucas–Kanade at half resolution (three levels, Tukey weights), on images with their column and row means removed. A level moves only when its first step passes a score test. Each frame enters the reference through one interpolation.
- **With stage 3b:** 3c works on 3b's output. 3b learns from that output before 3c's correction, and each change 3b makes to its estimate is made to 3c's reference too (in scene coordinates), so the two never disagree about the pattern.
- **Deferred work:** the reference update and 3b's learning run while stage 4b's GPU works.

**Found on the way** (each confirmed, then fixed):
- **A reference built from corrected frames locks in its first pattern** (a 1.5-count lasting change). It follows the frame as it came.
- **Stripes read as motion:** on a still, low-contrast scene, the drifting fixed pattern gave Lucas–Kanade up to 6 px of apparent motion. With the column/row means removed, it's within ±0.007 px.
- **Pans broke the version without motion compensation:** at 1–3 px/frame it made the streaks worse (0.250 → 0.30–0.34 counts).
- **Stage 3b fed 3c's output** missed the fast part of the pattern's drift, and the two chased each other (in the pipeline: `flat_aged` 0.292 → 0.238 only, a 1.15-count lasting change at one column).
- **Half-resolution motion is accurate to ~0.07 px** (exact Fourier shifts). Through pans, the fix does as well with it as with the true motion.

**The owner's first look** (the first clips): on `flat_aged`, "sometimes the stage 3c on is even a bit worse than the off one when it comes to vertical streaks". The cause, a fix tried and rejected, and a second problem the new regression test found:
- **3c undid 3b's learning.** After a calibration, 3b relearns the persistent pattern over seconds, so its output keeps changing. 3c's 2 s reference lagged behind that change, and 3c took the difference for stripes and put it back. So the output kept 3b's older, stronger correction. Fix: each change 3b makes goes into 3c's reference too.
- **Estimating before 3b** (tried first) keeps the full fixed pattern in the reference, and a pan drags it along. On synthetic pans over the real fixed pattern (below), that was worse than off. Rejected: 3c stays after 3b.
- **The motion estimate could wander** (found by the regression test, not in the owner's clip). On a scene with only column and row structure, the images are pure noise once their column and row means are gone. Iterated Lucas–Kanade then walks to a random peak of the noise's correlation, up to 20 px a frame, and the reference smears the pattern it holds. On the real scenes, the sensor's own per-pixel pattern anchors it (within 0.04 px on `flat_aged`). Fix: a score test on each level's first step, at 5 standard errors (with no motion, χ² with 2 degrees of freedom: a false start ~4·10⁻⁶ of the time). It changes no real scene or pan.

**Tests:**
- **"Stage 3c doesn't hold back stage 3b"** (unit test): a persistent pattern that 3b learns over 2 s, plus per-frame stripes, on a scene with only column and row structure. In every window, the output's lasting pattern with 3c must be no stronger than without, and its per-frame part under 0.4× off's. Per frame, off 0.26–0.28 counts: 0.30 / 0.18 / 0.10 while the motion wandered, now 0.085 / 0.048 / 0.028.
- **"estimateShift stays put when nothing is left to track"** (unit test): 20 trials, 0 px with the score test, up to 20 px without.
- **Synthetic pans over the real fixed pattern** (local, `build/nr/pan_bench`): `flat`'s raw frames (the real sensor's fixed pattern, drift, stripes and noise) plus `room`'s structure. The structure pans back and forth over 40 px and runs through the whole pipeline, scored against the known scene.

**Results** (`0a19f38`; counts; stages 1–3b, then 3c; frames 50+):
- **Per frame:** each frame's fine column profile (under 9 px) less the time-mean's, i.e. the flicker. Rows are in brackets.
- **Lasting:** the time-mean's fine column profile.

| | Per frame, off | Per frame, on | Lasting, off → on |
|---|---|---|---|
| `flat` | 0.250 (0.173) | 0.040 (0.023) | 0.103 → 0.066 |
| `flat_aged` | 0.277 (0.175) | 0.040 (0.024) | 0.158 → 0.148 |
| `room` | 0.253 (0.173) | 0.044 (0.026) | 0.413 → 0.403 (mostly the scene) |
| `keyboard` | 0.263 (0.181) | 0.061 (0.049) | 1.852 → 1.850 (the scene) |
| `night` (handheld) | 0.271 | 0.112 | — |
| `hand` (a hand moving) | 0.399 | 0.320 | — |

`night` and `hand` move, so their time-mean isn't the scene and their per-frame figure includes the motion.

| Synthetic pan over the real fixed pattern | Off | On |
|---|---|---|
| Still | 0.308 (0.254) | 0.175 (0.051) |
| 0.1 px/frame | 0.284 (0.253) | 0.108 (0.099) |
| 0.3 px/frame | 0.282 (0.253) | 0.100 (0.090) |
| 1 px/frame | 0.283 (0.254) | 0.132 (0.127) |

(Fine column error against the true scene per frame, counts; its per-frame part in brackets.)

**In the clips** (display levels, NLM Low; flickering / static / whole):
- **`flat_aged`:** 0.35 / 0.21 / 0.41 → 0.07 / 0.19 / 0.19.
- **The part of it the owner looked at** (x 64–192, y 48–144): whole 0.43 → 0.26, and the worst 5% of frames 0.50 → 0.34.

- **Real vertical structure is untouched:** e.g. `room`'s curtain folds.
- **Moving objects:** a warm block moving across a still scene adds no streaks (unit test).
- **Lasting change** (time-mean on − off, fine part): 0.06–0.09 counts on the real scenes.

**Cost** (the overlay now splits the pipeline by stage):
- **Live on the tablet** (camera, 2026-09-26): 3c costs 2.8 ms p50 (p95 3.8–4.3) before stage 4b, plus 0.45 ms alongside it. Latency p50 / p95: 15.5 / 17.7 ms off, 17.6 / 20.0–20.8 ms on. That's at the 20 ms budget, so 3c can't be approved as is.
- **Why so much:** the app's processing runs near the big cores' lowest clock (DEVICE.md "The processing thread's clock"). In `harness perf` on a big core, 3c is 1.10 ms and stages 5–6 2.4 ms; live they take ~2.5× that.
- **Made cheaper** (bit-identical output, then float sums that differ by one float step): 1.73 → 1.10 ms on the tablet at full clock, 0.34 → 0.22 ms on the Mac. What's left is mostly the motion estimate's pyramids (42%) and the voting passes.
- **The rest of the budget, live:** stages 5–6 take ~7 ms, the largest part; next are 3b's learning (1.7–2.1 ms) and the temperature table and readouts (1.4–1.6 ms), both alongside stage 4b's GPU, which they already outlast.

**Review:** side-by-side clips (NLM Low, 3c off | on), regenerated for `0a19f38`, local: `bench/out/clips/stage3c/` (full frames, 4×) and `bench/out/clips/stage3c_crops/` (8×). Made with `tools/py/compare_clips.py`.

**Verdict:** pending (second look). The debug panel has "Stage 3c: per-frame stripe fix (preview)", and the build on the tablet has the fixes.

## 2026-09-25 — Stage 4b loses subtle detail (owner); measured on known texture

**The owner, live on the keyboard:** "Stage 4B does reduce the noise, but it makes the objects loses some details. (Even at 1.1 strength)".

**Why the study missed it:**
- Its texture metric (`keys`: the time-mean's detail inside the keys ROI) kept 98%. But that's the key gaps, far stronger than the noise, which non-local means keeps.
- A band-wise version on the still scenes doesn't separate the two either (`tools/py/nr_detail.py`: the time-mean of one half of the frames against filtered frames from the other half, subtle pixels only). In the fine band, a still scene's time-mean is mostly the sensor's fixed pattern, which a filter within one frame can't tell from noise. Every variant keeps it in step with the noise it keeps.

**Measured on known texture** (`tools/py/nr_detail.py --synthetic`):
- **Method:** random texture (white noise blurred by 0.7, 1 or 2 px) at 0.5, 1 and 2× the noise amplitude, added to `flat`'s real frames (real noise and fixed pattern).
- **kept:** the fraction of the texture's contrast in the filtered time-mean.
- **noise:** the temporal noise left, relative to none.

| Filter | Noise | 0.7 px ×0.5 / ×1 / ×2 | 1 px ×0.5 / ×1 / ×2 | 2 px ×0.5 / ×1 / ×2 |
|---|---|---|---|---|
| NLM 11×11, strength 1.1 (the default) | 0.31 | 0.32 / 0.44 / 0.79 | 0.41 / 0.54 / 0.81 | 0.67 / 0.74 / 0.88 |
| NLM 11×11, 0.9 | 0.46 | 0.51 / 0.67 / 0.94 | 0.58 / 0.73 / 0.94 | 0.77 / 0.84 / 0.94 |
| NLM 11×11, 0.7 | 0.78 | 0.83 / 0.93 / 1.00 | 0.85 / 0.93 / 0.99 | 0.92 / 0.96 / 0.99 |
| NLM 5×5, 1.4 (the morning's preview) | 0.37 | 0.41 / 0.49 / 0.74 | 0.57 / 0.63 / 0.79 | 0.84 / 0.86 / 0.90 |
| NLM 11×11, squared distance less 2σ², 0.7 | 0.66 | 0.69 / 0.86 / 0.99 | 0.74 / 0.87 / 0.99 | 0.87 / 0.92 / 0.98 |
| BM3D, σ ×1.0 (reference implementation, offline) | 0.52 | 0.65 / 0.80 / 0.93 | 0.76 / 0.87 / 0.96 | 0.91 / 0.96 / 0.98 |
| BM3D, σ ×0.8 | 0.75 | 0.83 / 0.90 / 0.96 | 0.88 / 0.94 / 0.98 | 0.96 / 0.98 / 0.99 |

- **At the default:** texture as strong as the noise keeps only about half its contrast. That's what the owner sees.
- **The variants are no way out:** a weight cutoff, spatial weighting of the search, the squared distance, adding back the removed signal's smooth part, and a 5×5 or 7×7 search all fall on about the same noise-for-texture trade-off as lowering the strength.
- **Why:** at this signal-to-noise ratio, a 5×5 patch's distance barely tells texture of the noise's size from noise. Identical patches weigh only ~2× what patches 1σ apart do, and a large search gathers many of the latter.
- **BM3D** (the reference code, 0.43 s a frame on the Mac's CPU) does better at moderate strength: at about half the noise left, ~10 points more texture. At light strength it's about equal. A real-time GPU version would be a large project, and its simplifications would give some of that back.

**Now:** the debug panel sets the search size (5×5 / 7×7 / 11×11) and strengths 0.6–1.4 for the owner's live pick. Verdict pending.

## 2026-09-25 — Stage 4b: spatial noise reduction, a study, then non-local means (approved; on the GPU, `d25e430+default`)

**Why:** with stage 4 removed, the owner found tone mapping showed more noise. They asked for noise reduction within each frame, so nothing can ghost, but only after the best method was found (`tools/py/nr_study.py`).

**The noise:** the pipeline's signal after stages 1–3, from the still scenes.
- **Level:** 1.06–1.10 counts per pixel on `flat`, `flat_aged` and `room`. It doesn't depend on the scene level (1.04–1.08 counts in every level band).
- **Spatially:** nearly white once the frame's shared wander is removed (neighbours +0.07 horizontally, +0.11 vertically). The earlier +0.27 / +0.30 included the shared part.
- **A per-frame stripe part:**
  - column offsets 0.25 counts and row offsets 0.20, which change every frame;
  - 3.5× what random noise would give, and ~10% of the noise energy;
  - an isotropic filter can't remove them.
- **Excluded as references:** `keyboard` and `night` aren't truly still.

**Method:** each candidate runs per frame on the stages 1–3 signal, over a sweep of strengths.
- **Noise:** each pixel's temporal std, its trend and the frame's shared offset removed.
- **Bias:** time-mean of the output minus time-mean of the input, on `keyboard` and `room`.
- **Key texture:** the benchmark's `detail` on `keyboard`'s keys.
- **Edges:** 10–90% rise of `keyboard`'s screen edge and `hand`'s palm, per frame.

The methods scale by the camera's temporal noise. A single frame's own noise estimate reads 1.5–1.8× too high, since fixed pattern and texture inflate it.

**Results** (noise relative to the input; texture relative too):

| Method | Near 97% texture kept: `flat` / `room` noise | Notes |
|---|---|---|
| Guided-filter detail attenuation (the first idea) | ×0.71 / ×0.77 (97.4%) | Weakest of the adaptive ones |
| Guided filter as denoiser (eps = kσ²) | ×0.65 / ×0.81 (97.2%) | |
| Lee local Wiener (7×7) | ×0.67 / ×0.81 (97.0%) | |
| Bilateral (5×5) | ×0.65 / ×0.71 (96.9%) | Some blotches |
| Starlet wavelet shrinkage | hard: ×0.68 at 90%; soft: ×0.39 at 70% | Loses texture, softens edges (screen 2.8 → 3.4–3.9 px) |
| Non-local means, 5×5 patches, 5×5 search | ×0.45 / ×0.59 (98.4%) | |
| NLM, 7×7 search | ×0.36 / ×0.53 (98.2%) | |
| NLM, 11×11 search | ×0.29 / ×0.48 (97.9%) | |

- **Settings used in the table:** the NLM rows use strength 1.2; the 11×11 search has more at the same texture.
- **NLM's form:**
  - OpenCV's 16-bit form (mean |patch difference|, Gaussian weight, the pixel itself at weight 1) keeps ~2% more texture than squared differences with Buades' noise correction.
  - 5×5 patches beat 3×3.
  - A sparse 12-pair search pattern did no better than the dense 5×5.
- **Edges and halos:** strong edges are unchanged by every NLM variant.

**Stripes after noise reduction:** removing the grain leaves the per-frame column and row noise, 30–39% of what's left after NLM on `flat`, and stripes stand out more than grain. A per-frame, edge-gated column/row removal halves them, but its estimate took in real scene lines (`bench/results/nr_study/per_frame_destripe_removed_keyboard.png`: the screen edge, the keyboard's rows). So it's rejected; the stripes stay parked (owner, 2026-09-25) and need a frame-to-frame estimate.

**Built (stage 4b, off):** NLM with 5×5 patches in `native/core`.
- **Implementation:** `nonLocalMeans` is the reference; `nlmPad` / `nlmBand` are the fast version, in bands, with a polynomial exp. Tests: brute force, band equality, noise halved with texture and the step kept.
- **Strength:** h = strength × σ, where σ is the noise measured from consecutive raw frames: 1.31 × the trimmed RMS of their difference, since the camera's filter correlates frames at ~0.72.
  - Calibrated on the still scenes to read 1.06–1.10.
  - A frame's reading counts only within 0.5–2× of the current one, so pans and steps don't; it's smoothed over ~2 s. Frames themselves are never mixed.

**Full display path** (stages 1–3, 5, 6):

| | Now | NLM 5×5 search, strength 1.4 | NLM 11×11, strength 1.1 |
|---|---|---|---|
| `flat` noise | 2.09 levels | 0.82 | 0.68 |
| Key texture | 5.03 levels | 4.88 | 4.95 |
| Screen-edge halo | 2.9% | 2.0% | 1.9% |

- Edges and flicker are unchanged or better. Xtherm's `flat` measured 0.82 levels, a lower bound from its H.264 video.
- **Review:** `bench/results/nr_study/`: `methods_single_frame.png` (every method, one frame) and `pipeline_*_crop0.jpg` (now against both NLM settings).

**Cost:** NLM is ~40 M operations a frame at 5×5 search, near the CPU's arithmetic limit even when fused or banded (the Mac: 0.8–1.0 ms).
- **On the tablet's gold cluster, full clock:** +3.7 ms (5×5 search), +7–8 ms (7×7), +17 ms (11×11).
- **Live, at the cores' low clock:** latency p50 22.8 / p95 27.9 ms at 5×5 search (budget 20), still 25 fps.
- **Plan:** a GPU compute version (the Adreno 650 needs well under 1 ms even for 11×11), with this CPU code as its reference and fallback.

**Verdict:** approved (owner, 2026-09-25, after the review images and the live preview at a 5×5 search: "Sure they look ok, the stage 4b on tablet looks good as well"). It ships at the better setting the table shows: an 11×11 search at strength 1.1, on the GPU.

**The GPU version** (`native/android/gpu_nlm*`): a GLES 3.1 compute shader in its own EGL context on the processing thread, one dispatch a frame and a synchronous read-back. The CPU code is its reference and its fallback: on any GPU failure, the CPU runs at a 5×5 search with h scaled ×1.27, which is strength 1.4 there, the setting the owner previewed.
- **First version:** each pixel read both 5×5 patches from shared memory for all 121 offsets, ~300 M loads a frame. It took 22 ms live (the result matched the CPU reference to 7e-3 counts).
- **Now:** the shader source is generated for the radii, so every window index is a constant and the windows sit in registers.
  - Each thread keeps its own patches' pixels. For each dy, it slides the partner window along dx, loading one new column per step, about 25× fewer loads.
  - Adjacent pixels of a thread share the columns' difference sums.
  - Offset (0, 0) has distance 0, so the pixel itself gets weight 1, as on the CPU.
- **Checks:**
  - `tools/py/gpu_nlm_emulate.py` runs the generated shader on the Mac as C++ against the CPU reference: max 7e-3 counts at 1, 2 and 4 pixels a thread, search radius 1–7, patch radius 0–3. glslc validates the GLSL.
  - On the tablet, the debug extra `nrCheck` compares the GPU to the CPU reference on a live frame and times each variant (field log).
- **Timing of the generated shader on the tablet:** pending; wireless adb dropped before it could be installed.

**Final results** (`d25e430+default` against `d25e430+default_nr-0`, the same code with stage 4b off; stage 4 is gone from both):

| | Stage 4b off | On (11×11, strength 1.1) |
|---|---|---|
| `flat` noise | 2.09 levels (24.7 mK) | 0.68 (13.1 mK) |
| `flat_aged` noise | 2.11 levels | 0.80 |
| `flat` fixed pattern | 4.43 levels | 3.87 |
| `keyboard` key texture | 5.03 levels | 4.95 (98%) |
| `keyboard` screen-edge halo | 2.9% | 1.9% |
| Edge widths (`keyboard`, `hand`) | 2.09–2.72 px | 2.10–2.71 px |
| Flicker: `flat` / `flat_aged` / `night` / `keyboard` / `room` / keyboard box | 0.30 / 0.064 / 0.096 / 0.21 / 0.135 / 0.18 | 0.21 / 0.047 / 0.090 / 0.21 / 0.193 / 0.14 |
| `motion` step response (1 / 2 / 4 frames) | 0.79 / 0.94 / 0.98 | the same |

- **`room`'s flicker (+0.06 levels):** the auto range's own wander, not the filter.
  - The frame's mean signal wanders 0.440 counts with stage 4b on or off, and frame-to-frame changes are the same or smaller with it.
  - How the mapping settles over 8 s differs in detail, and it goes the other way on four of the six static cases. All of it is far below a visible level.
- **The `defects` metric** reads 0 → 33 pixels over 6σ on `flat` (0 → 42 on `flat_aged`), while the worst defect drops from 99 to 58 mK (140 → 88).
  - **Why it rises:** its σ is the frame's spread around each pixel's 5×5 ring, which non-local means cuts from 23 to 3.9 mK.
  - **What the pixels are:** small clusters that were already there (e.g. `flat` at x 251–255, rows 10–11; `flat_aged` around (201–209, 16–22) and on row 0). Each one is smaller than before: the 50 worst are at 0.6–0.7× their stage-4b-off size.
  - **Mechanism:** a rare patch has no good matches, so it's filtered less than its surroundings. Only 3 (`flat`) and 10 (`flat_aged`) pixels grew by more than 5 mK, all beside those clusters, where the ring median shifted.
  - **In display terms:** one frame's largest ring residual falls from 16 to 4.3 levels.

## 2026-09-25 — Stage 5: the measurement region (`039b6d7+default`, `keyboard_box`)

**What:** PLAN M4 stage 5's region, `Pipeline::setRegion` (M6 will pass the visible area, intersected with the box).
- **Statistics:** the tone mapping's statistics (range, histogram, global offset) come from the region alone. Every pixel still maps through the one curve.
- **Retarget:** a new region takes over at once, converging within ~0.3 s (time constants of 0.1 s, no deadband), then the usual damping resumes.
- **Nothing measurable:** a region that's all at the clip keeps the last mapping instead of greying the frame.
- **Tooling:** `harness bench/render --box`. bench.py runs any scene with a `bench/<scene>/box.json` a second time with that box and reports `<scene>_box`. `keyboard` has one: a few keys in the middle.

**Metrics** (inside the box, stages 1–5):

| | Whole-frame statistics | The box's statistics |
|---|---|---|
| Display levels used (1st–99th percentile) | 207–247 | 84–179 |
| Detail | 3.20 levels | 7.43 |
| Flicker of the box's mean | 0.09 | 0.17 |

The 2.0 gain cap is why the keys don't fill the whole output: their ~45 counts can use at most ~90 levels.

**Image:** `bench/results/039b6d7+default/keyboard_box.jpg`.

**Note for M6:** because of the gain cap, a region's range maps to a centred band, so pixels outside it sit at the band's ends (dark and light grey), not at the palette's ends as the plan says. Forcing black and white there would jump at the band's edges. Decide with the owner when the box and its dimming land in M6.

**Tests:** statistics from the region only, retarget within 0.3 s, and an all-clipped region holds its mapping.

## 2026-09-25 — Stage 6: detail enhancement (`b848b57+detail`, then `1ffd29c+default`: approved as the mid-scale texture layer)

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
- **Noise isn't boosted in flat areas:** the gate keeps `flat` at 1.41 levels. A looser gate (1.5–3×) gave 7.83 key detail at 1.67 levels of noise.
- **Temporal noise in textured areas does rise.** It's the noise riding on the texture, boosted with it. Per-pixel temporal std, frames 50–199, quadratic trend removed:
  - `keyboard` keys: 0.84 → 1.26 levels (mid layer: 1.35);
  - `room` (left part): 1.28 → 1.39 (1.46);
  - `night` (the car): 2.34 → 2.66 (2.81).
  After stage 4 that noise is slow (lag-1 ρ 0.95), so smoothing the boost over time wouldn't remove it. Judge it in the clips (`bench/out/clips/`, local, from `tools/py/bench.py --pipeline detail`).

**Cost** (`harness perf` over adb, `keyboard`, a big core at 2.42 GHz): 1.76 → 4.64 ms a frame. On a little core (A55, 1.8 GHz) it's 9.5 → 29.9 ms, over the budget. So the app now pins its processing thread to the big cores, not yet verified in the app (commit `def22ad`).

**Found on the way:**
- A NaN on very quiet fields: running sums dip a hair below zero before a sqrt.
- The noise floor's subsample, every 192nd pixel, only ever hit 4 columns (192 = ¾ × 256). Now it's every 191st.

**Review:** `bench/results/b848b57+detail/stage6_review/` shows stages 1–5, stage 6 as proposed, and the crisp variant side by side: `keyboard` (with the keys at 8× and the screen edge), `room`, `night` (the car at 8×), `hand`, `flat_aged`. Sheets against the baseline and the apps: `bench/results/b848b57+detail/<scene>.jpg`.

**Experiment, off by default: mid-scale texture** (`detailMid=2`, commit `6974da0`).
- **Idea:** the owner likes Hti's texture but not its rim, and Hti's 14% halo is the mark of large-scale local contrast. So a second layer, the base against a wider self-guided filter (radius 8, eps 100), gets ×2 under the same halo guard (over 16 px) and its own noise gate.
- **Results:** key detail 6.43 → 7.20 levels. The screen-edge halo stays 2.74%, and `flat` noise goes 1.41 → 1.46.
- **Pitfalls:** at eps 400 the layer drew an Hti-like rim (11.5%): the guard can't see a rim that wide. Without its noise gate, `flat` noise rose to 1.62.
- **Cost:** +3.1 ms a frame on the tablet's big cores (stages 1–6: 7.8 ms), mostly a 33-tap range filter that could be made ~10× cheaper.
- **Review:** `stage6_review/mid_*.jpg` show stages 1–5, stage 6 as proposed, and with the mid layer.

**Owner's review (2026-09-25):**
- **Crisp:** jagged.
- **Proposed:** less jagged, but still more than stages 1–5.
- **Mid-scale layer:** better contrast, less washed out.
- **The car:** little difference between any of them.
- **The keyboard:** with the mid layer added after the curve, the warm middle blew out and its key lines vanished.

**What changed after it:**
- **The mid layer goes into stage 5's input** (base + boosted mid), so the curve's range and histogram make room for it (commit `6f0da19`). The keys' share at the top of the output is 3.3% for stages 1–5, 4.6% at ×1.5 and 3.7% at ×2.5.
- **Only the enabled parts compute** (`21212af`, `1ffd29c`). The fine layer's gate and guard run only when its gain is above 1, the mid guard's 16 px range runs at half resolution (tested to contain the full one), and the energy box runs on one image.

**Final** (`1ffd29c+default`: stages 1–6, texture ×1.5, against stages 1–5 at `903d02a+default`):

| Metric | Stages 1–5 | Stage 6 (texture ×1.5) |
|---|---|---|
| `keyboard` key detail | 4.66 levels | 5.04 |
| `keyboard` screen edge: width / halo | 2.10 px / 2.66% | 2.08 px / 2.77% |
| `hand` edges | 2.73 / 2.10 / 2.59 px | 2.72 / 2.09 / 2.62 (unchanged: no sharpening, no stair-steps) |
| `flat` / `flat_aged` noise | 1.40 / 1.42 levels | 1.43 / 1.43 |
| `flat` / `flat_aged` fixed pattern | 4.26 / 4.62 levels | 4.46 / 4.69 |
| Flicker `flat` / `room` / `night` / `keyboard` | 0.15 / 0.09 / 0.13 / 0.24 | 0.23 / 0.23 / 0.11 / 0.22 (Xtherm 0.21 / 0.22 / 0.11 / 0.13) |
| `keyboard_box` detail inside the box | 7.43 | 7.93 |
| `motion`, `shutter` | — | unchanged |

- **Why flicker rose:** the curve now sees the noise-gated boost. It is still at Xtherm's level and far below visible.
- **Cost:** 1.64 ms a frame on the gold cluster. On the tablet (replay, the cores at their lowest clock) latency is p50 16.4 / p95 19.3 ms, the same at ×1.5 and ×3, against 12.6 / 13.4 with stage 6 off. That is inside the 20 ms budget but with little margin.

**Verdict:** approved (owner, 2026-09-25) as the mid-scale texture layer only.
- On by default at ×1.5 ("1.5× is enough").
- A setting turns it off if performance suffers, and sets the strength up to ×3. Today that's the debug panel's "Stage 6: texture" and "Texture strength" (×1.5, 2, 2.5, 3; adb extra `textureStrength`). M6 moves them into the settings.
- The fine-scale gain and the unsharp pass stay in the code, off; stage text still reaches them for experiments.

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

**Removed (owner, 2026-09-25, live with the camera):** moving the camera left after-images, and the stage gave no visible gain ("It does nothing to improve image quality"). The benchmark had only a hand moving in front of a still camera; in a pan, low-contrast structure moves without clearing the motion threshold, so the filter kept blending its old position in. Stage 4 is now off by default and gone from the app's debug panel; the code stays in `native/core`, off. Latency p95 on the tablet with stages 1–3, 5, 6: 16.8 ms (19.9 with stage 4).

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
