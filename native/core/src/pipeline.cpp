#include "tv/pipeline.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "tv/display.h"

namespace tv {

bool parseStages(const std::string& text, PipelineOptions* o) {
  size_t start = 0;
  while (start <= text.size()) {
    const size_t end = std::min(text.find(',', start), text.size());
    const std::string item = text.substr(start, end - start);
    const size_t eq = item.find('=');
    const std::string key = item.substr(0, eq);
    const std::string value = eq == std::string::npos ? "" : item.substr(eq + 1);
    const bool on = value != "0";
    if (key == "default" || key.empty()) {
    } else if (key == "shutter") {
      o->shutterHold = on;
    } else if (key == "shutterBlend" && !value.empty()) {
      o->shutterBlendFrames = std::atoi(value.c_str());
    } else if (key == "badPixels") {
      o->badPixels = on;
    } else {
      return false;
    }
    start = end + 1;
  }
  return true;
}

std::string describeStages(const PipelineOptions& o) {
  std::string s;
  auto add = [&s](const std::string& item) { s += (s.empty() ? "" : ",") + item; };
  if (o.shutterHold) add("shutter(" + std::to_string(o.shutterBlendFrames) + ")");
  if (o.badPixels) add("badPixels");
  return s.empty() ? "none" : s;
}

Pipeline::Pipeline(const PipelineOptions& options)
    : options_(options), work_(kImagePixels), previous_(kImagePixels), held_(kImagePixels), heldSignal_(kImagePixels) {}

void Pipeline::setOptions(const PipelineOptions& options) { options_ = options; }

void Pipeline::reset() {
  havePrevious_ = false;
  frozen_ = false;
  blendLeft_ = blendTotal_ = 0;
}

void Pipeline::hold() {
  if (options_.shutterHold && havePrevious_) frozen_ = true;
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

  // The signal, in raw counts: what each stage passes on and tone mapping starts from.
  float* sig = signal ? signal : work_.data();
  for (size_t i = 0; i < kImagePixels; ++i) sig[i] = float(image[i]);
  if (options_.badPixels) replaceBadPixels(badPixels_, sig);  // stage 2
  renderBaseline(sig, display);

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
  std::copy(sig, sig + kImagePixels, heldSignal_.begin());
}

}  // namespace tv
