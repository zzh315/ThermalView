// M4 image pipeline (docs/PLAN.md M4, docs/PIPELINE_LOG.md): camera image in, display intensity out.
// Every stage can be switched off, and with all of them off the output equals renderBaseline. Stages
// shape the display only; readouts always come from raw values (CLAUDE.md rule 2).
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "tv/bad_pixels.h"
#include "tv/drift.h"
#include "tv/frame.h"
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
  bool denoise = true;  // approved 2026-09-25 (owner: "do what you think is best"), at 0.25
  float denoiseKMin = 0.25f;
  float denoiseMotionLo = 2.0f, denoiseMotionHi = 4.0f;  // in sigmas of the pooled difference

  // Stage 5 (approved 2026-09-25, gain cap 2.0): automatic tone mapping (tone.h) in place of the
  // baseline's per-frame min/max stretch. Pixels at the camera's clip stay out of its statistics.
  bool tone = true;
  ToneOptions toneOptions;

  // Stage 6 (needs stage 5): split the signal into a base (a self-guided filter, radius
  // detailRadius, eps detailEps counts^2) and detail; stage 5 maps the base, and the detail comes back
  // at the curve's slope with gain detailGain, limited to +-detailLimit counts. The gain applies only
  // where a noise gate is open (the detail's local energy above detailNoiseLo..detailNoiseHi x the
  // frame's noise floor) and fades out beside steps that dwarf the local detail (the base's local
  // range within detailEdgeRadius pixels, over detailEdgeLo..Hi x the detail's RMS), where the
  // filter's residual is a faint rim that the gain would turn into a halo. Then an unsharp pass on the display (unsharpAmount, sigma unsharpSigma) where the gate is
  // open, clamped to each pixel's 3x3 min/max so it can't overshoot.
  bool detail = false;
  int detailRadius = 2;
  float detailEps = 50.0f;
  float detailGain = 2.5f;
  float detailLimit = 7.0f;
  float detailNoiseLo = 2.0f, detailNoiseHi = 4.0f;  // the noise gate's ramp, in x the detail noise floor
  float detailSmooth = 1.0f;  // > 0: the added contrast is blurred with this sigma (px) first
  int detailEdgeRadius = 6;                          // the halo guard looks this far for a step...
  float detailEdgeLo = 12.0f, detailEdgeHi = 25.0f;  // ...and fades the gain out where the step is
                                                     // this many times the local detail (RMS)
  float unsharpAmount = 1.0f;
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
// "denoiseK=X", "denoiseLo=X" and "denoiseHi=X" tune it; "tone" / "tone=0" switches stage 5,
// "toneGain=X" (max gain), "toneLinear=X", "toneLow=X", "toneHigh=X" (percentiles), "toneExpand=X",
// "toneContract=X" and "toneCurve=X" (time constants) tune it; "detail" / "detail=0" switches stage
// 6, "detailRadius=N", "detailEps=X", "detailGain=X", "detailLimit=X", "detailNoiseLo=X",
// "detailNoiseHi=X", "detailSmooth=X", "detailEdgeRadius=N", "detailEdgeLo=X", "detailEdgeHi=X", "unsharp=X" (amount),
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

  // A new stream, replay start or range switch: forget every frame seen so far.
  void reset();

  // One 256x192 camera image in; display intensity in [0, 1] out (kImagePixels floats). signal, if
  // given, receives the value tone mapping started from, in raw counts (for the harness's °C metrics).
  void process(const uint16_t* image, float* display, float* signal = nullptr, FrameMeta meta = {});

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
  std::vector<float> base_, detailLayer_, energy_, gate_, range_, gfScratch_, detailScratch_;  // stage 6
  void enhance(const float* sig, float* display, const uint8_t* exclude);
  bool haveFiltered_ = false;
  float sigmaD_ = 0.0f;  // stage 4's noise level of the pooled difference, counts
  void denoise(float* sig);
  int destripeFrames_ = 0;                    // frames since stage 3b last started over
  void destripe(float* sig);
  void restartDestripe();
  std::vector<float> work_;  // the signal, when the caller doesn't ask for it
  std::vector<uint16_t> previous_;
  std::vector<float> held_, heldSignal_;
  bool havePrevious_ = false;
  bool frozen_ = false;
  int blendLeft_ = 0, blendTotal_ = 0;
};

}  // namespace tv
