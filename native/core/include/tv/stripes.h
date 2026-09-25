// Stage 3c: the per-frame column and row noise (owner, 2026-09-25: the stripe fix, before BM3D;
// PIPELINE_LOG). The sensor adds each frame a small offset to every column (~0.26 counts, fine part)
// and every row (~0.18), fixed to the sensor, correlated over ~10 frames (0.77 frame to frame).
// Stage 3b removes the persistent part; this removes what changes from frame to frame.
//
// Each frame is compared with a reference image of the scene: a slow average (tauFrames) of past
// frames, kept in scene coordinates and moved with the scene, so on a still scene or a pan the scene
// cancels and what's left is the sensor's pattern. Per column, the trimmed mean of frame - reference
// over the pixels that didn't change (|difference| under the gate) is that column's offset; the rows'
// come from what's left (each a mean refined once: a one-step M-estimate). Only their fine part (under ~highpass pixels) is taken off: the smooth part
// is drift, which the tone mapping follows. Nothing is blended: one offset per column and per row.
//
// The scene's motion on the sensor is a global translation, found by Lucas-Kanade between the
// reference and the frame (three pyramid levels, Tukey weights so a moving hand doesn't count), on
// both images less their column and row means: otherwise the stripes and the drifting fixed pattern
// read as motion (on a still, low-contrast scene, up to 6 px of it). Each frame enters the
// reference through one interpolation and the reference reaches the sensor through one more, so the
// blur can't pile up over a slow pan. A pixel that changed restarts its reference; when too little
// of the frame matches, the whole reference starts over (and a shutter cycle restarts it).
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
  int fresh = 8;            // frames a reference pixel needs before it counts
  float minShift = 0.02f;   // px: smaller motion estimates are taken as none
  float edge = 10.0f;       // x sigma per pixel: pixels on steeper gradients don't vote
};

class FrameStripes {
 public:
  explicit FrameStripes(StripeOptions options = {}) : options_(options) {}
  void setOptions(const StripeOptions& options) { options_ = options; }
  void reset();

  // sig: kImagePixels counts (stages 1-3b), corrected in place; sigma: the noise, counts. Then, before
  // the next frame, updateReference() (it changes nothing in this frame: the pipeline runs it while
  // stage 4b's GPU works).
  void process(float* sig, float sigma);
  void updateReference();
  // The last frame as it came in, before this stage's correction (what stage 3b learns from).
  const float* uncorrected() const { return uncorrected_.data(); }

  // The last frame: the scene's motion on the sensor since the frame before (px), the fraction of
  // pixels that matched the reference, and whether a correction was applied.
  std::array<float, 2> lastShift() const { return lastShift_; }
  float lastMatched() const { return lastMatched_; }
  bool lastCorrected() const { return lastCorrected_; }

 private:
  StripeOptions options_;
  bool started_ = false;
  std::vector<float> ref_, refSensor_, diff_, scene_, tmp_, colOff_, rowOff_, values_;
  std::vector<int> age_, ageSensor_, flags_, flagsScene_;
  std::vector<float> uncorrected_;
  float gateUsed_ = 0.0f;
  bool updatePending_ = false;
  std::vector<unsigned char> matched_;
  double cum_[2] = {0.0, 0.0};
  std::array<float, 2> lastShift_{};
  float lastMatched_ = 0.0f;
  bool lastCorrected_ = false;
};

// Helpers, public for the tests. The image shifted by (dx, dy), out(p) = in(p - (dx, dy)): bicubic
// (a = -0.75, as OpenCV's), borders reflected (w x h floats).
void shiftImage(const float* in, float* out, int w, int h, double dx, double dy, std::vector<float>& scratch);
// The scene's translation from a to b (b(p) = a(p - d)), kFrameWidth x kImageRows images, as above;
// guess: where to start; noise: the images' noise std (the robust weights' floor).
std::array<double, 2> estimateShift(const float* a, const float* b, std::vector<float>& scratch);
std::array<double, 2> estimateShift(const float* a, const float* b, std::vector<float>& scratch,
                                    std::array<double, 2> guess, double noise);

}  // namespace tv
