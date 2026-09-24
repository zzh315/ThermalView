#include "tv/pipeline.h"

#include <algorithm>
#include <cstring>

#include "tv/display.h"

namespace tv {

Pipeline::Pipeline(const PipelineOptions& options)
    : options_(options), previous_(kImagePixels), held_(kImagePixels), heldSignal_(kImagePixels) {}

void Pipeline::setOptions(const PipelineOptions& options) { options_ = options; }

void Pipeline::reset() {
  havePrevious_ = false;
  frozen_ = false;
  blendLeft_ = blendTotal_ = 0;
}

void Pipeline::process(const uint16_t* image, float* display, float* signal) {
  const size_t bytes = kImagePixels * sizeof(uint16_t);
  const bool repeat = havePrevious_ && std::memcmp(image, previous_.data(), bytes) == 0;
  std::memcpy(previous_.data(), image, bytes);
  havePrevious_ = true;

  if (options_.shutterHold && repeat) {
    // A shutter cycle (M1: 30-31 repeats of one image): the camera's noise makes an accidental exact
    // repeat impossible. Show what was last shown and leave every stage's state alone.
    frozen_ = true;
    std::copy(held_.begin(), held_.end(), display);
    if (signal) std::copy(heldSignal_.begin(), heldSignal_.end(), signal);
    return;
  }

  renderBaseline(image, display);
  if (signal)
    for (size_t i = 0; i < kImagePixels; ++i) signal[i] = float(image[i]);

  if (!options_.shutterHold) return;
  if (frozen_) {  // the first fresh frame after a cycle
    frozen_ = false;
    blendLeft_ = blendTotal_ = std::max(0, options_.shutterBlendFrames);
  }
  if (blendLeft_ > 0) {
    // The held output's weight falls linearly to zero: n/(n+1), ..., 1/(n+1) over n frames.
    const float w = float(blendLeft_) / float(blendTotal_ + 1);
    for (size_t i = 0; i < kImagePixels; ++i) display[i] += w * (held_[i] - display[i]);
    --blendLeft_;
  }
  std::copy(display, display + kImagePixels, held_.begin());
  if (signal) std::copy(signal, signal + kImagePixels, heldSignal_.begin());
}

}  // namespace tv
