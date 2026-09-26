// Stage 5, automatic tone mapping (docs/PLAN.md M4 stage 5; PRIOR_ART pass 2 item 8): the signal in
// counts to display intensity. FLIR-style plateau equalization: a robust range from percentiles, a
// double-plateau histogram equalization blended with a linear share, and a cap on the curve's slope
// (max gain) that keeps a flat scene calm instead of stretching its noise, all damped over time
// (expand fast, contract slowly, a deadband) and computed on the signal minus a tracked global
// offset, so calibration steps and the camera's slow wander don't show as brightness changes.
#pragma once

#include <cstdint>
#include <vector>

#include "tv/frame.h"

namespace tv {

struct ToneOptions {
  // The robust range: every object's pixels, leaving out only a few stray ones (owner, 2026-09-26: at
  // 0.3 / 99.7 a 60 °C bulb of 69 pixels fell above the range and a 30 °C hand took its color; small hot
  // parts are what PCB work looks for). The cost, flicker a little up on flat scenes (PIPELINE_LOG).
  float lowPct = 0.01f, highPct = 99.99f;
  float maxGain = 2.0f;          // display levels (of 255) per count at most: the gain cap (owner: 2.0)
  float linearShare = 0.2f;      // the curve's linear part; the rest is plateau equalization
  float plateauDown = 0.25f;     // occupied bins count at least this x the mean occupied bin...
  // ...and at most the mean of the histogram's local maxima (the upper plateau) times this:
  float plateauUp = 1.0f;
  bool balance = false;          // what the cap leaves goes half below the scene's median, half above it
  // Without the balance, the median stays where the curve puts it, but within [medianLo, medianHi] of
  // the output (M7 experiment: a hot object in view mustn't crush the scene's bulk into black).
  float medianLo = 0.0f, medianHi = 1.0f;
  float expandTauS = 0.1f;       // the range follows a wider scene this fast
  float contractTauS = 1.3f;     // and a narrower one this slowly
  float curveTauS = 2.0f;        // the curve's shape, smoothed over time (0.3 made a moving hand pump)
  float deadbandCounts = 1.5f;   // range changes smaller than this are ignored...
  float deadbandPct = 0.0f;      // ...or than this % of the current span, whichever is larger
  float outLo = 0.03f, outHi = 0.97f;  // the display range the curve maps into
  bool trackOffset = true;       // follow whole-frame steps (calibrations, the camera's wander)
};

// A mapping given from outside (M6's range lock: RangeLock builds one each frame from °C): the range
// in absolute counts and the curve at its ToneMapper::kCurve + 1 even edges, in [0, 1].
struct FixedMapping {
  float lo = 0.0f, hi = 0.0f;
  std::vector<float> curve;
};

class ToneMapper {
 public:
  static constexpr int kCurve = 256;  // bins across the smoothed range; the curve has kCurve + 1 edges

  explicit ToneMapper(const ToneOptions& options = {});
  void setOptions(const ToneOptions& options) { options_ = options; }
  void reset();

  // signal: kImagePixels counts (stages 1-4 done); exclude: optional mask, nonzero = leave out of the
  // statistics (clipped pixels, outside the measurement region); out: intensity in [0, 1]. With
  // too few pixels left to measure (a region that is all clipped), the current mapping holds.
  // With detail (stage 6), the curve comes from signal (the base layer) and each pixel gets its
  // detail back scaled by the curve's slope there: out = T(base) + T'(base) x detail.
  void map(const float* signal, float* out, const uint8_t* exclude = nullptr, float dtS = 0.04f,
           const float* detail = nullptr);

  // A calibration or another step change of the whole frame just happened: the next frame re-reads
  // the global offset from itself instead of from the frame before (the caller skipped frames).
  void resync() { havePrevious_ = false; }

  // M6's range lock: map through [m] (no statistics, no smoothing, no offset tracking) until called
  // with null, which goes back to the automatic mapping: the scene's own from the next frame.
  void setFixed(const FixedMapping* m);
  bool fixed() const { return fixed_; }
  // With a fixed mapping, each pixel of the last frame mapped: 1 above its range, 2 below it, 0 in it
  // (the display marks them for palettes that ask: PaletteSpec::lockedAbove / lockedBelow).
  const std::vector<uint8_t>& outside() const { return outside_; }

  // The statistics' region changed (the box moved or resized, zoom, pan): for the next seconds the
  // range and the curve follow the new statistics at once, with no deadband, converging within
  // about that time (time constants of a third of it), then the usual damping resumes.
  void retarget(float seconds = 0.3f) { retargetLeftS_ = retargetS_ = seconds; }

  // The current mapping, for the scale bar (M5): the smoothed range in counts and the curve at its
  // kCurve + 1 bin edges (values in [0, 1] before the outLo..outHi squeeze).
  bool ready() const { return haveRange_; }  // a mapping exists (after the first frame)
  // Where a value in counts lands on the display (the scale bar's marks): the current mapping's
  // intensity for it, as map() gives a pixel of that value without detail, in outLo..outHi.
  float intensityAt(float counts) const;
  float lowCounts() const { return lo_ + offset_; }
  float highCounts() const { return hi_ + offset_; }
  const std::vector<float>& curve() const { return curve_; }
  float globalOffset() const { return offset_; }

 private:
  ToneOptions options_;
  std::vector<float> previous_, work_, curve_, target_;
  std::vector<float> guardInc_ = std::vector<float>(kCurve);  // the median guard's copy of the rises
  std::vector<uint32_t> hist_;
  std::vector<uint8_t> outside_;
  bool havePrevious_ = false, haveRange_ = false, haveCurve_ = false;
  bool fixed_ = false;  // setFixed()
  float offset_ = 0.0f;  // tracked global offset, counts
  float lo_ = 0.0f, hi_ = 0.0f;  // smoothed range of (signal - offset), counts
  float retargetS_ = 0.0f, retargetLeftS_ = 0.0f;  // retarget(): its length and what's left of it
  void apply(const float* signal, float* out, const float* detail) const;  // step 5, the current mapping
};

}  // namespace tv
