// M4 image pipeline (docs/PLAN.md M4, docs/PIPELINE_LOG.md): camera image in, display intensity out.
// Every stage can be switched off, and with all of them off the output equals renderBaseline. Stages
// shape the display only; readouts always come from raw values (CLAUDE.md rule 2).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "tv/bad_pixels.h"
#include "tv/drift.h"
#include "tv/filters.h"
#include "tv/frame.h"
#include "tv/readouts.h"
#include "tv/tone.h"

namespace tv {

struct PipelineOptions {
  // Defaults are the approved stages (docs/PIPELINE_LOG.md); the rest start off.
  //
  // Stage 1 (approved 2026-09-25): while the camera repeats one frame (a shutter cycle), hold the
  // last output without advancing any stage; when fresh frames return, crossfade from the held output
  // over this many frames (0 = jump). A motion gate is left out on purpose: the NUC's own correction
  // change is large and coherent (PIPELINE_LOG, stage 1), so it would read as motion.
  bool shutterHold = true;
  int shutterBlendFrames = 8;  // ~0.3 s at 25 fps

  // Stage 2 (approved 2026-09-25): replace the camera's known bad pixels (setBadPixels) for
  // display, from their good neighbours, before anything else sees them.
  bool badPixels = true;

  // Stage 3 (approved 2026-09-25): subtract each pixel's drift since the last calibration
  // (setDriftMap), scaled by the drift the frame's metadata reports. It runs first, like a
  // calibration of our own.
  bool drift = true;
  float driftScale = 0.9f;  // on the map's rates (M4: 0.86-0.91 fits flat_aged's per-pixel part)
  float driftC0 = 0.40f;    // FPA - shutter right after a calibration, °C

  // Stage 3b: a light tracker for the row and column offsets the drift map leaves. Per frame, each
  // pixel's residual against its 8 in-row (or in-column) neighbours counts only when it and the
  // steps to its neighbours stay under the gate (real edges don't); per-column (per-row) means of
  // what's kept are integrated with time constant destripeTauS (1 s for the first 3 s after a reset or calibration)
  // and clamped to +-destripeClamp counts, so a faint real line loses at most that much.
  bool destripe = true;  // approved with stage 3a, 2026-09-25
  float destripeTauS = 4.0f;
  float destripeGate = 5.0f;   // counts
  float destripeClamp = 2.0f;  // counts

  // Stage 4: a per-pixel recursive filter, y += K (x - y), with K = kMin where nothing moves and 1
  // where it does. Motion is the 3x3 box of (x - y) against its own noise level (sigma, a robust
  // estimate over the frame, smoothed over time): smoothstep(motionLo, motionHi, |box| / sigma).
  // Starts over (K = 1) after a calibration. The camera already filters in time (DEVICE.md
  // "Onboard filtering"), so this gains less than it would on white noise.
  // Removed from the pipeline (owner, 2026-09-25): moving the camera left after-images of
  // low-contrast structure, and it gave no visible gain. The code stays, off; the app can't enable it.
  bool denoise = false;
  float denoiseKMin = 0.25f;
  float denoiseMotionLo = 2.0f, denoiseMotionHi = 4.0f;  // in sigmas of the pooled difference

  // Stage 4b, spatial noise reduction (owner request, 2026-09-25: less of the noise tone mapping
  // shows, within each frame so nothing can ghost; PIPELINE_LOG, tools/py/nr_study.py): non-local
  // means (filters.h), search radius nrSearch, patch radius nrPatch, strength h = nrStrength x the
  // noise sigma. Sigma (counts) is measured from consecutive raw frames, 1.31 x the trimmed RMS of
  // their difference (the camera's own temporal filter correlates them at ~0.72), a frame's reading
  // accepted only within 0.5-2x of the current one (so motion doesn't count) and smoothed over ~2 s;
  // nrSigma > 0 fixes it instead. The frames themselves are never mixed. Approved (owner,
  // 2026-09-25) at an 11x11 search (the tablet's GPU; gpu_nlm.h); strength 0.8, the app's Low: at
  // 1.1 the owner saw objects lose detail (PIPELINE_LOG), and the app's setting goes Off / Low 0.8 /
  // Medium 0.9 / High 1.1.
  bool nr = true;
  int nrSearch = 5, nrPatch = 2;
  float nrStrength = 0.8f;
  float nrSigma = 0.0f;
  float nrNominalSigma = 1.07f;  // the start value (M4: 1.06-1.10 counts on the still benchmark scenes)
  int nrFallbackSearch = 2;      // the CPU's search radius when an accelerator is set but fails

  // Stage 5 (approved 2026-09-25, gain cap 2.0): automatic tone mapping (tone.h) in place of the
  // baseline's per-frame min/max stretch. Pixels at the camera's clip stay out of its statistics.
  bool tone = true;
  ToneOptions toneOptions;

  // Stage 6 (needs stage 5; approved 2026-09-25 as the mid-scale texture layer, on at x1.5, the
  // strength a user setting up to x3): a self-guided filter (radius detailRadius, eps detailEps
  // counts^2) splits the signal into a base and fine detail; a wider one (detailMidRadius,
  // detailMidEps) splits the base into a smoother base and a mid-scale layer: surfaces and shapes a
  // few to ~16 pixels across. The mid layer gets gain detailMidGain where its own noise gate is open
  // (its local energy over detailNoiseLo..Hi x its floor) and fades out beside steps that dwarf it
  // (the halo guard: the local range within 2 x detailMidRadius pixels, over detailEdgeLo..Hi x its
  // RMS), and goes into stage 5's input, so the curve makes room for it and nothing blows out. Fine
  // detail comes back at the curve's slope, at gain 1 by default.
  //
  // Also available, off by default (the owner found them jagged): gain detailGain on the fine layer
  // (its own gate and guard over detailEdgeRadius pixels, limited to +-detailLimit counts, smoothed
  // over detailSmooth px), and an unsharp pass on the display (unsharpAmount, sigma unsharpSigma)
  // clamped to each pixel's 3x3 min/max.
  bool detail = true;
  int detailRadius = 2;
  float detailEps = 50.0f;
  float detailGain = 1.0f;
  float detailLimit = 7.0f;
  float detailNoiseLo = 2.0f, detailNoiseHi = 4.0f;  // the noise gates' ramp, in x each layer's floor
  float detailSmooth = 1.0f;  // > 0: the fine layer's added contrast is blurred with this sigma (px)
  float detailMidGain = 1.5f;  // the texture strength (owner: 1.5 by default, a setting up to 3)
  int detailMidRadius = 8;
  float detailMidEps = 100.0f;
  bool detailMidGate = true;  // the mid layer's own noise gate
  int detailEdgeRadius = 6;                          // the fine layer's halo guard looks this far...
  float detailEdgeLo = 12.0f, detailEdgeHi = 25.0f;  // ...and both guards fade the gain out where the
                                                     // step is this many times the local detail (RMS)
  float unsharpAmount = 0.0f;
  float unsharpSigma = 0.7f;
  float unsharpEdgeLo = 3.0f, unsharpEdgeHi = 6.0f;  // its edge gate: the base's 3x3 range, x the floor
};

// What the pipeline needs from a frame's metadata (FrameView fpaC(), shutterC()).
struct FrameMeta {
  double fpaC = 0.0 / 0.0;
  double shutterC = 0.0 / 0.0;
};

// Stage settings as text, shared by the harness (--pipeline) and the app's debug options: a
// comma-separated list applied on top of the defaults. "default" changes nothing; "shutter" or
// "shutter=0" switches stage 1; "shutterBlend=N" sets its crossfade; "badPixels" / "badPixels=0"
// switches stage 2; "drift" / "drift=0" switches stage 3's compensation, "driftScale=X" and
// "driftC0=X" tune it; "destripe" / "destripe=0" switches stage 3b, "destripeTau=X",
// "destripeGate=X" and "destripeClamp=X" tune it; "denoise" / "denoise=0" switches stage 4,
// "denoiseK=X", "denoiseLo=X" and "denoiseHi=X" tune it; "nr" / "nr=0" switches stage 4b,
// "nrSearch=N", "nrPatch=N", "nrStrength=X" and "nrSigma=X" tune it; "tone" / "tone=0" switches stage 5,
// "toneGain=X" (max gain), "toneLinear=X", "toneLow=X", "toneHigh=X" (percentiles), "toneExpand=X",
// "toneContract=X" and "toneCurve=X" (time constants) tune it; "detail" / "detail=0" switches stage
// 6, "detailRadius=N", "detailEps=X", "detailGain=X", "detailLimit=X", "detailNoiseLo=X",
// "detailNoiseHi=X", "detailSmooth=X", "detailMid=X" (experimental gain), "detailMidRadius=N",
// "detailMidEps=X", "detailMidGate" / "detailMidGate=0", "detailEdgeRadius=N", "detailEdgeLo=X", "detailEdgeHi=X", "unsharp=X" (amount),
// "unsharpSigma=X", "unsharpEdgeLo=X" and "unsharpEdgeHi=X" tune it. False on an unknown item.
bool parseStages(const std::string& text, PipelineOptions* options);
std::string describeStages(const PipelineOptions& options);  // e.g. "shutter(8)", "none"

class Pipeline {
 public:
  explicit Pipeline(const PipelineOptions& options = {});

  void setOptions(const PipelineOptions& options);
  const PipelineOptions& options() const { return options_; }

  // Stage 2's map, for the camera in use (badPixelMapFor its serial).
  void setBadPixels(BadPixelMap map) { badPixels_ = std::move(map); }
  const BadPixelMap& badPixels() const { return badPixels_; }

  // Stage 3's drift map, for the camera in use (tools/py/drift_map.py).
  void setDriftMap(DriftMap map) { drift_ = std::move(map); }
  const DriftMap& driftMap() const { return drift_; }
  double lastDriftC() const { return lastDriftC_; }  // the drift the last frame was compensated for
  float noiseSigma() const { return nrSigma_ > 0.0f ? nrSigma_ : options_.nrNominalSigma; }  // stage 4b's, counts

  // Stage 4b's accelerator (the app's GPU version), in two steps so the caller can work while it
  // runs: start begins filtering src (kImagePixels counts, which stays untouched until finish) with
  // the same non-local means, finish waits for the result. Either returning false means it isn't
  // available, and this CPU version runs instead, at a search radius of at most nrFallbackSearch
  // (h scaled to keep the strength, as measured).
  struct NoiseReducer {
    std::function<bool(const float* src, int searchRadius, int patchRadius, float h)> start;
    std::function<bool(float* dst)> finish;
  };
  void setNoiseReducer(NoiseReducer reducer) { reducer_ = std::move(reducer); }
  bool lastNoiseReductionAccelerated() const { return nrAccelerated_; }

  // A new stream, replay start or range switch: forget every frame seen so far.
  void reset();

  // The measurement region (M6: the visible area, intersected with the box): stage 5's statistics
  // come from it alone, and pixels outside still map through the same curve, so they clip to the
  // palette ends. A change retargets the mapping at once, easing over ~0.3 s (PLAN M4 stage 5).
  void setRegion(const Region& region);
  const Region& region() const { return region_; }

  // One 256x192 camera image in; display intensity in [0, 1] out (kImagePixels floats). signal, if
  // given, receives the value tone mapping started from, in raw counts (for the harness's °C metrics).
  // alongside, if given, runs exactly once during the call: while the accelerator filters (the app's
  // temperature work overlaps the GPU, as does stage 3b's estimate update), else before stage 4b.
  void process(const uint16_t* image, float* display, float* signal = nullptr, FrameMeta meta = {},
               const std::function<void()>& alongside = {});

  // The caller has stopped feeding frames through a shutter cycle it knows about (the app discards
  // frames while its own 0x8000 or a lockout runs): the next frame crossfades from the last output,
  // as after a cycle seen in the frames themselves (stage 1 on).
  void hold();

  // True while the camera repeats one frame, or after hold() until the next frame (stage 1 on).
  bool frozen() const { return frozen_; }
  bool blending() const { return blendLeft_ > 0; }

 private:
  PipelineOptions options_;
  BadPixelMap badPixels_;
  DriftMap drift_;
  double lastDriftC_ = 0.0;
  std::vector<float> colOffset_, rowOffset_;  // stage 3b's corrections, counts
  std::vector<float> colAcc_, colCount_, colSum_;  // stage 3b's per-column scratch
  std::vector<float> filtered_, diff_, pooled_;    // stage 4's state and scratch
  ToneMapper tone_;                                // stage 5
  std::vector<uint8_t> clipped_;                   // stage 5's exclusion mask
  Region region_;                                  // stage 5's measurement region
  std::vector<float> base_, detailLayer_, energy_, gate_, range_, gfScratch_, detailScratch_, midBase_, mid_;  // stage 6
  void enhance(const float* sig, float* display, const uint8_t* exclude);
  bool haveFiltered_ = false;
  float nrSigma_ = 0.0f;                     // stage 4b's measured noise sigma (0: none yet)
  std::vector<float> nrScratch_, nrSample_, nrOut_;
  NlmPadded nrPadded_;
  NoiseReducer reducer_;
  bool nrAccelerated_ = false;
  void updateNoiseSigma(const uint16_t* image);  // before previous_ is overwritten
  void reduceNoise(float* sig, const std::function<void()>& alongside);
  float sigmaD_ = 0.0f;  // stage 4's noise level of the pooled difference, counts
  void denoise(float* sig);
  int destripeFrames_ = 0;                    // frames since stage 3b last started over
  void applyDestripe(float* sig);          // this frame's correction
  void updateDestripe(const float* sig);   // the estimate for the next frames, from the corrected signal
  void restartDestripe();
  std::vector<float> work_;  // the signal, when the caller doesn't ask for it
  std::vector<uint16_t> previous_;
  std::vector<float> held_, heldSignal_;
  bool havePrevious_ = false;
  bool frozen_ = false;
  int blendLeft_ = 0, blendTotal_ = 0;
};

}  // namespace tv
