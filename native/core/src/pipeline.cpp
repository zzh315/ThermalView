#include "tv/pipeline.h"

#include <algorithm>
#include <cmath>
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
    } else if (key == "drift") {
      o->drift = on;
    } else if (key == "driftScale" && !value.empty()) {
      o->driftScale = float(std::atof(value.c_str()));
    } else if (key == "driftC0" && !value.empty()) {
      o->driftC0 = float(std::atof(value.c_str()));
    } else if (key == "destripe") {
      o->destripe = on;
    } else if (key == "destripeTau" && !value.empty()) {
      o->destripeTauS = float(std::atof(value.c_str()));
    } else if (key == "destripeGate" && !value.empty()) {
      o->destripeGate = float(std::atof(value.c_str()));
    } else if (key == "destripeClamp" && !value.empty()) {
      o->destripeClamp = float(std::atof(value.c_str()));
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
  if (o.drift) add("drift(x" + std::to_string(o.driftScale).substr(0, 4) + ")");
  if (o.badPixels) add("badPixels");
  if (o.destripe) add("destripe");
  return s.empty() ? "none" : s;
}

Pipeline::Pipeline(const PipelineOptions& options)
    : options_(options),
      colOffset_(kFrameWidth),
      rowOffset_(kImageRows),
      scratch_(kImagePixels),
      work_(kImagePixels),
      previous_(kImagePixels),
      held_(kImagePixels),
      heldSignal_(kImagePixels) {}

void Pipeline::setOptions(const PipelineOptions& options) { options_ = options; }

void Pipeline::reset() {
  havePrevious_ = false;
  frozen_ = false;
  blendLeft_ = blendTotal_ = 0;
  restartDestripe();
}

void Pipeline::restartDestripe() {
  std::fill(colOffset_.begin(), colOffset_.end(), 0.0f);
  std::fill(rowOffset_.begin(), rowOffset_.end(), 0.0f);
  destripeFrames_ = 0;
}

void Pipeline::destripe(float* sig) {
  const int w = kFrameWidth, h = kImageRows;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) sig[y * w + x] -= colOffset_[size_t(x)] + rowOffset_[size_t(y)];

  // Gated residuals against 8 neighbours along one axis; each column's (row's) median is its
  // remaining offset. Returns false if too few pixels passed the gate.
  const float gate = options_.destripeGate;
  auto median = [](float* v, int n) {
    std::nth_element(v, v + n / 2, v + n);
    return v[n / 2];
  };
  auto residual = [&](int x, int y, int dx, int dy, float* out) {
    const float c = sig[y * w + x];
    float sum = 0;
    int n = 0;
    for (int k = -4; k <= 4; ++k) {
      const int xx = x + k * dx, yy = y + k * dy;
      if (k == 0 || xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
      sum += sig[yy * w + xx];
      ++n;
    }
    const float r = c - sum / float(n);
    const int x0 = std::max(0, x - dx), y0 = std::max(0, y - dy);
    const int x1 = std::min(w - 1, x + dx), y1 = std::min(h - 1, y + dy);
    if (std::fabs(r) >= gate || std::fabs(c - sig[y0 * w + x0]) >= gate || std::fabs(c - sig[y1 * w + x1]) >= gate)
      return false;
    *out = r;
    return true;
  };

  const float tau = destripeFrames_ < 75 ? 1.0f : options_.destripeTauS;  // 25 fps
  const float gain = 1.0f / (25.0f * tau);
  float* v = scratch_.data();
  for (int x = 0; x < w; ++x) {
    int n = 0;
    for (int y = 0; y < h; ++y) n += residual(x, y, 1, 0, &v[n]);
    if (n >= h / 2) colOffset_[size_t(x)] += gain * median(v, n);
  }
  for (int y = 0; y < h; ++y) {
    int n = 0;
    for (int x = 0; x < w; ++x) n += residual(x, y, 0, 1, &v[n]);
    if (n >= w / 2) rowOffset_[size_t(y)] += gain * median(v, n);
  }
  // Zero mean (a global offset is tone mapping's business), then the clamp.
  auto finish = [this](std::vector<float>& o) {
    float mean = 0;
    for (float f : o) mean += f;
    mean /= float(o.size());
    for (float& f : o) f = std::clamp(f - mean, -options_.destripeClamp, options_.destripeClamp);
  };
  finish(colOffset_);
  finish(rowOffset_);
  ++destripeFrames_;
}

void Pipeline::hold() {
  if (options_.shutterHold && havePrevious_) frozen_ = true;
}

void Pipeline::process(const uint16_t* image, float* display, float* signal, FrameMeta meta) {
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

  // The first fresh frame after a cycle (or after hold()): a calibration reset the pattern stage 3b
  // tracks, so it starts over before it runs.
  const bool resuming = frozen_;
  if (resuming) restartDestripe();

  // The signal, in raw counts: what each stage passes on and tone mapping starts from.
  float* sig = signal ? signal : work_.data();
  for (size_t i = 0; i < kImagePixels; ++i) sig[i] = float(image[i]);
  lastDriftC_ = 0.0;
  if (options_.drift && !drift_.empty() && std::isfinite(meta.fpaC) && std::isfinite(meta.shutterC)) {
    // Stage 3: the pattern that grew since the last calibration, predicted from the metadata.
    lastDriftC_ = driftSinceCalibrationC(meta.fpaC, meta.shutterC, options_.driftC0);
    const float k = float(lastDriftC_) * options_.driftScale;
    const float* r = drift_.rate.data();
    for (size_t i = 0; i < kImagePixels; ++i) sig[i] -= k * r[i];
  }
  if (options_.badPixels) replaceBadPixels(badPixels_, sig);  // stage 2
  if (options_.destripe) destripe(sig);                       // stage 3b
  renderBaseline(sig, display);

  if (!options_.shutterHold) return;
  if (resuming) {  // the first fresh frame after a cycle
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
