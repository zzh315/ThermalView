// Stage 3c: the per-frame column and row noise (owner, 2026-09-25: the stripe fix, before BM3D;
// PIPELINE_LOG). The sensor adds each frame a small offset to every column (~0.26 counts, fine
// part) and every row (~0.18), fixed to the sensor, correlated over ~10 frames (0.77 frame to
// frame). Stage 3b removes the persistent part; this removes what changes from frame to frame.
//
// Each frame is compared with a reference image of the scene: a slow average (tauFrames) of past
// frames, kept in scene coordinates and moved with the scene, so on a still scene or a pan the
// scene cancels and what's left is the sensor's pattern. Per column, the trimmed mean of frame -
// reference over the pixels that didn't change (|difference| under the gate) is that column's
// offset; the rows' come from what's left (each a mean refined once: a one-step M-estimate). Only
// their fine part (under ~highpass pixels) is taken off: the smooth part is drift, which the tone
// mapping follows. Nothing is blended: one offset per column and per row.
//
// The scene's motion on the sensor is a global translation, found by Lucas-Kanade between the
// reference and the frame (three pyramid levels, Tukey weights so a moving hand doesn't count), on
// both images less their column and row means: otherwise the stripes and the drifting fixed pattern
// read as motion (on a still, low-contrast scene, up to 6 px of it). A pyramid level moves only
// when its first step is significant (a score test): iterated on pure noise, Lucas-Kanade walks to
// a random peak of the noise's correlation, up to 20 px away on a scene with nothing left to track.
// Each frame enters the reference through one interpolation and the reference reaches the sensor
// through one more, so the blur can't pile up over a slow pan. A pixel that changed restarts its
// reference; when too little of the frame matches, the whole reference starts over (and a shutter
// cycle restarts it).
#pragma once

#include <array>
#include <vector>

namespace tv {

struct StripeOptions {
  float tauFrames = 50.0f;  // the reference's time constant (2 s at 25 fps)
  float gate = 3.0f;        // a pixel is unchanged when |frame - reference| < gate x 1.4 sigma
  float band = 2.5f;        // the offsets' second pass: values within band x 1.4 sigma of the first
  float lost = 0.7f;        // under this fraction of unchanged pixels: start over
  float clamp = 1.5f;       // counts, per pixel
  float highpass = 16.0f;   // pixels: the corrections' smooth part (a Gaussian of highpass / 2) stays
  float anchor = 8.0f;      // px of motion before the reference is moved into sensor coordinates
  // The reference also holds the sensor's own fixed pattern, which doesn't move with the scene: moved
  // with a pan, it no longer lines up and makes new stripes (a synthetic pan over the real fixed
  // pattern: worse than off, PIPELINE_LOG). So past maxMotion px since the reference began it starts
  // over, and compensate says whether smaller motions are followed at all.
  float maxMotion = 1e9f;
  bool compensate = true;
  int fresh = 8;            // frames a reference pixel needs before it counts
  float minShift = 0.02f;   // px: smaller motion estimates are taken as none
  // Motion counts only where the frame vouches for it: a pyramid level moves only when its first step
  // is this many standard errors long (a score test; with no motion it's passed ~4e-6 of the time).
  float significance = 5.0f;
  float edge = 10.0f;       // x sigma per pixel: pixels on steeper gradients don't vote
};

// estimateShift's buffers, kept between calls (fresh ones every frame cost page faults on Android).
struct ShiftScratch {
  static constexpr int kLevels = 3;
  std::vector<float> tmp, pa[kLevels], pb[kLevels], gx, gy, bw, sample;
};

class FrameStripes {
 public:
  explicit FrameStripes(StripeOptions options = {}) : options_(options) {}
  void setOptions(const StripeOptions& options) { options_ = options; }
  const StripeOptions& options() const { return options_; }
  void reset();

  // sig: kImagePixels counts (stages 1-3b), corrected in place; sigma: the noise, counts. Then, before
  // the next frame, updateReference() (it changes nothing in this frame: the pipeline runs it while
  // stage 4b's GPU works).
  void process(float* sig, float sigma);
  // The same, estimating from source (and keeping the reference of it) while correcting sig.
  void process(float* sig, const float* source, float sigma);
  void updateReference();
  // Stage 3b changed its column and row estimates by dcol / drow (this frame's signal had the old
  // ones taken off, the next will have the new): the reference takes the same change, so 3b's
  // learning is never mistaken for stripes (3c would undo it, and the older, stronger pattern would
  // linger: PIPELINE_LOG). The pattern is the sensor's: it goes where it sits in scene coordinates.
  void applyPatternChange(const std::vector<float>& dcol, const std::vector<float>& drow);

  // The last frame: the scene's motion on the sensor since the frame before (px), the fraction of
  // pixels that matched the reference, and whether a correction was applied.
  std::array<float, 2> lastShift() const { return lastShift_; }
  float lastMatched() const { return lastMatched_; }
  float lastStdError() const { return lastStdError_; }  // the last motion estimate's standard error, px
  bool lastCorrected() const { return lastCorrected_; }

 private:
  StripeOptions options_;
  bool started_ = false;
  std::vector<float> ref_, refSensor_, diff_, scene_, tmp_, colOff_, rowOff_, values_;
  std::vector<int> age_, ageSensor_, flags_, flagsScene_;
  std::vector<float> uncorrected_;
  ShiftScratch shift_;
  std::vector<float> colSum_;  // (scratch)
  std::vector<int> colCount_;
  std::vector<float> first_;
  float gateUsed_ = 0.0f;
  bool updatePending_ = false;
  std::vector<unsigned char> matched_;
  double cum_[2] = {0.0, 0.0};
  double moved_[2] = {0.0, 0.0};  // the motion since the reference began, px
  std::array<float, 2> lastShift_{};
  float lastMatched_ = 0.0f;
  float lastStdError_ = 0.0f;
  bool lastCorrected_ = false;
};

// Helpers, public for the tests. The image shifted by (dx, dy), out(p) = in(p - (dx, dy)): bicubic
// (a = -0.75, as OpenCV's), borders reflected (w x h floats).
void shiftImage(const float* in, float* out, int w, int h, double dx, double dy, std::vector<float>& scratch);
// The scene's translation from a to b (b(p) = a(p - d)), kFrameWidth x kImageRows images, as above;
// guess: where to start; noise: the images' noise std (the robust weights' floor); stderror (if
// given): the estimate's standard error at the finest level, px; significance: how many standard
// errors a pyramid level's first step needs before the level moves at all (0: always).
std::array<double, 2> estimateShift(const float* a, const float* b, std::vector<float>& scratch);
std::array<double, 2> estimateShift(const float* a, const float* b, std::vector<float>& scratch,
                                    std::array<double, 2> guess, double noise, double* stderror = nullptr,
                                    double significance = 5.0);
std::array<double, 2> estimateShift(const float* a, const float* b, ShiftScratch& scratch,
                                    std::array<double, 2> guess, double noise, double* stderror = nullptr,
                                    double significance = 5.0);

}  // namespace tv
