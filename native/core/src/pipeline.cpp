#include "tv/pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "tv/display.h"
#include "tv/filters.h"
#include "tv/readouts.h"

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
    } else if (key == "denoise") {
      o->denoise = on;
    } else if (key == "denoiseK" && !value.empty()) {
      o->denoiseKMin = float(std::atof(value.c_str()));
    } else if (key == "denoiseLo" && !value.empty()) {
      o->denoiseMotionLo = float(std::atof(value.c_str()));
    } else if (key == "denoiseHi" && !value.empty()) {
      o->denoiseMotionHi = float(std::atof(value.c_str()));
    } else if (key == "tone") {
      o->tone = on;
    } else if (key == "toneGain" && !value.empty()) {
      o->toneOptions.maxGain = float(std::atof(value.c_str()));
    } else if (key == "toneLinear" && !value.empty()) {
      o->toneOptions.linearShare = float(std::atof(value.c_str()));
    } else if (key == "toneLow" && !value.empty()) {
      o->toneOptions.lowPct = float(std::atof(value.c_str()));
    } else if (key == "toneHigh" && !value.empty()) {
      o->toneOptions.highPct = float(std::atof(value.c_str()));
    } else if (key == "toneExpand" && !value.empty()) {
      o->toneOptions.expandTauS = float(std::atof(value.c_str()));
    } else if (key == "toneContract" && !value.empty()) {
      o->toneOptions.contractTauS = float(std::atof(value.c_str()));
    } else if (key == "toneCurve" && !value.empty()) {
      o->toneOptions.curveTauS = float(std::atof(value.c_str()));
    } else if (key == "toneDeadband" && !value.empty()) {
      o->toneOptions.deadbandCounts = float(std::atof(value.c_str()));
    } else if (key == "detail") {
      o->detail = on;
    } else if (key == "detailRadius" && !value.empty()) {
      o->detailRadius = std::atoi(value.c_str());
    } else if (key == "detailEps" && !value.empty()) {
      o->detailEps = float(std::atof(value.c_str()));
    } else if (key == "detailGain" && !value.empty()) {
      o->detailGain = float(std::atof(value.c_str()));
    } else if (key == "detailLimit" && !value.empty()) {
      o->detailLimit = float(std::atof(value.c_str()));
    } else if (key == "detailNoiseLo" && !value.empty()) {
      o->detailNoiseLo = float(std::atof(value.c_str()));
    } else if (key == "detailNoiseHi" && !value.empty()) {
      o->detailNoiseHi = float(std::atof(value.c_str()));
    } else if (key == "detailMid" && !value.empty()) {
      o->detailMidGain = float(std::atof(value.c_str()));
    } else if (key == "detailMidRadius" && !value.empty()) {
      o->detailMidRadius = std::max(2, std::atoi(value.c_str()));
    } else if (key == "detailMidGate") {
      o->detailMidGate = on;
    } else if (key == "detailMidEps" && !value.empty()) {
      o->detailMidEps = float(std::atof(value.c_str()));
    } else if (key == "detailSmooth" && !value.empty()) {
      o->detailSmooth = float(std::atof(value.c_str()));
    } else if (key == "detailEdgeRadius" && !value.empty()) {
      o->detailEdgeRadius = std::max(1, std::atoi(value.c_str()));
    } else if (key == "detailEdgeLo" && !value.empty()) {
      o->detailEdgeLo = float(std::atof(value.c_str()));
    } else if (key == "detailEdgeHi" && !value.empty()) {
      o->detailEdgeHi = float(std::atof(value.c_str()));
    } else if (key == "unsharpEdgeLo" && !value.empty()) {
      o->unsharpEdgeLo = float(std::atof(value.c_str()));
    } else if (key == "unsharpEdgeHi" && !value.empty()) {
      o->unsharpEdgeHi = float(std::atof(value.c_str()));
    } else if (key == "unsharp" && !value.empty()) {
      o->unsharpAmount = float(std::atof(value.c_str()));
    } else if (key == "unsharpSigma" && !value.empty()) {
      o->unsharpSigma = float(std::atof(value.c_str()));
    } else if (key == "toneDeadbandPct" && !value.empty()) {
      o->toneOptions.deadbandPct = float(std::atof(value.c_str()));
    } else if (key == "toneOffset") {
      o->toneOptions.trackOffset = on;
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
  if (o.denoise) add("denoise(k" + std::to_string(o.denoiseKMin).substr(0, 4) + ")");
  if (o.tone) add("tone(g" + std::to_string(o.toneOptions.maxGain).substr(0, 4) + ")");
  if (o.detail && o.tone) add("detail(x" + std::to_string(o.detailGain).substr(0, 4) + ")");
  return s.empty() ? "none" : s;
}

Pipeline::Pipeline(const PipelineOptions& options)
    : options_(options),
      colOffset_(kFrameWidth),
      rowOffset_(kImageRows),
      colAcc_(kFrameWidth),
      colCount_(kFrameWidth),
      colSum_(kFrameWidth),
      filtered_(kImagePixels),
      diff_(kImagePixels),
      pooled_(kImagePixels),
      tone_(options.toneOptions),
      work_(kImagePixels),
      previous_(kImagePixels),
      held_(kImagePixels),
      heldSignal_(kImagePixels) {}

void Pipeline::setOptions(const PipelineOptions& options) {
  options_ = options;
  tone_.setOptions(options.toneOptions);
}

void Pipeline::reset() {
  havePrevious_ = false;
  frozen_ = false;
  blendLeft_ = blendTotal_ = 0;
  restartDestripe();
  haveFiltered_ = false;
  tone_.reset();
}

void Pipeline::restartDestripe() {
  std::fill(colOffset_.begin(), colOffset_.end(), 0.0f);
  std::fill(rowOffset_.begin(), rowOffset_.end(), 0.0f);
  destripeFrames_ = 0;
}

void Pipeline::destripe(float* sig) {
  const int w = kFrameWidth, h = kImageRows;
  for (int y = 0; y < h; ++y) {
    float* row = sig + size_t(y) * w;
    const float r = rowOffset_[size_t(y)];
    for (int x = 0; x < w; ++x) row[x] -= colOffset_[size_t(x)] + r;
  }

  // Each pixel's residual against its 8 nearest neighbours along one axis (fewer at the borders),
  // kept only when it and the steps to its two neighbours stay under the gate: real edges drop
  // out. Row-major passes with sliding sums, no per-pixel buffers.
  const float gate = options_.destripeGate;
  auto keep = [gate](float c, float r, float a, float b) {
    return std::fabs(r) < gate && std::fabs(c - a) < gate && std::fabs(c - b) < gate;
  };

  // Along rows (for column offsets): a 9-wide window sum slides across each row; each column
  // accumulates the mean of its gated residuals (the gate bounds every sample, so a mean is as
  // robust here as a median, at a fraction of the cost).
  std::vector<float>& colAcc = colAcc_;      // per-column sum of kept residuals
  std::vector<float>& colCount = colCount_;  // and how many were kept
  std::fill(colAcc.begin(), colAcc.end(), 0.0f);
  std::fill(colCount.begin(), colCount.end(), 0.0f);
  for (int y = 0; y < h; ++y) {
    const float* row = sig + size_t(y) * w;
    float sum = 0;
    int lo = 0, hi = -1;  // the window [lo, hi]
    for (int x = 0; x < w; ++x) {
      while (hi < std::min(w - 1, x + 4)) sum += row[++hi];
      while (lo < x - 4) sum -= row[lo++];
      const float c = row[x];
      const float r = c - (sum - c) / float(hi - lo);
      if (keep(c, r, row[std::max(0, x - 1)], row[std::min(w - 1, x + 1)])) {
        colAcc[size_t(x)] += r;
        colCount[size_t(x)] += 1.0f;
      }
    }
  }
  const float tau = destripeFrames_ < 75 ? 1.0f : options_.destripeTauS;  // 25 fps
  const float gain = 1.0f / (25.0f * tau);
  for (int x = 0; x < w; ++x)
    if (colCount[size_t(x)] >= float(h / 2)) colOffset_[size_t(x)] += gain * colAcc[size_t(x)] / colCount[size_t(x)];

  // Along columns (for row offsets): running 9-tall column sums, updated a row at a time.
  std::vector<float>& colSum = colSum_;
  std::fill(colSum.begin(), colSum.end(), 0.0f);
  int top = 0, bottom = -1;  // rows in colSum
  for (int y = 0; y < h; ++y) {
    while (bottom < std::min(h - 1, y + 4)) {
      const float* add = sig + size_t(++bottom) * w;
      for (int x = 0; x < w; ++x) colSum[size_t(x)] += add[x];
    }
    while (top < y - 4) {
      const float* sub = sig + size_t(top++) * w;
      for (int x = 0; x < w; ++x) colSum[size_t(x)] -= sub[x];
    }
    const float* row = sig + size_t(y) * w;
    const float* up = sig + size_t(std::max(0, y - 1)) * w;
    const float* down = sig + size_t(std::min(h - 1, y + 1)) * w;
    const float count = float(bottom - top);
    float acc = 0;
    int n = 0;
    for (int x = 0; x < w; ++x) {
      const float c = row[x];
      const float r = c - (colSum[size_t(x)] - c) / count;
      if (keep(c, r, up[x], down[x])) {
        acc += r;
        ++n;
      }
    }
    if (n >= w / 2) rowOffset_[size_t(y)] += gain * acc / float(n);
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

void Pipeline::denoise(float* sig) {
  const int w = kFrameWidth, h = kImageRows;
  float* y = filtered_.data();
  if (!haveFiltered_) {
    std::copy(sig, sig + kImagePixels, y);
    haveFiltered_ = true;
    sigmaD_ = 0.0f;
    return;
  }
  float* d = diff_.data();
  for (size_t i = 0; i < kImagePixels; ++i) d[i] = sig[i] - y[i];
  // 3x3 box of the difference: rows first, then columns (edge pixels average what they have).
  float* p = pooled_.data();
  for (int r = 0; r < h; ++r) {
    const float* in = d + size_t(r) * w;
    float* out = p + size_t(r) * w;
    for (int x = 0; x < w; ++x) {
      const int a = std::max(0, x - 1), b = std::min(w - 1, x + 1);
      float s = 0;
      for (int k = a; k <= b; ++k) s += in[k];
      out[x] = s / float(b - a + 1);
    }
  }
  for (int x = 0; x < w; ++x) {
    // in place, column by column, keeping the row above's original value
    float above = p[size_t(x)];
    for (int r = 0; r < h; ++r) {
      const float here = p[size_t(r) * w + x];
      const float below = r + 1 < h ? p[size_t(r + 1) * w + x] : here;
      const float s = (r > 0 ? above : here) + here + below;
      above = here;
      p[size_t(r) * w + x] = s / 3.0f;  // edges count themselves twice: close enough
    }
  }
  // Noise level of the pooled difference: a robust sigma from a subsample (most pixels don't move),
  // smoothed over ~1 s so one busy frame doesn't swing it.
  float* sample = colAcc_.data();  // 256 entries is plenty: every 191st pixel, so every column
  int n = 0;
  for (size_t i = 97; i < kImagePixels && n < kFrameWidth; i += 191) sample[n++] = std::fabs(p[i]);
  std::nth_element(sample, sample + n / 2, sample + n);
  const float sigma = 1.4826f * sample[n / 2];
  sigmaD_ = sigmaD_ > 0.0f ? sigmaD_ + 0.04f * (sigma - sigmaD_) : sigma;
  const float s = std::max(sigmaD_, 0.05f);
  const float lo = options_.denoiseMotionLo * s, hi = options_.denoiseMotionHi * s;
  const float kMin = options_.denoiseKMin;
  for (size_t i = 0; i < kImagePixels; ++i) {
    const float m = std::clamp((std::fabs(p[i]) - lo) / (hi - lo), 0.0f, 1.0f);
    const float motion = m * m * (3.0f - 2.0f * m);  // smoothstep
    const float k = kMin + (1.0f - kMin) * motion;
    y[i] += k * d[i];
    sig[i] = y[i];
  }
}

void Pipeline::enhance(const float* sig, float* display, const uint8_t* exclude) {
  base_.resize(kImagePixels);
  detailLayer_.resize(kImagePixels);
  energy_.resize(kImagePixels);
  gate_.resize(kImagePixels);
  float* base = base_.data();
  float* det = detailLayer_.data();
  float* e = energy_.data();
  float* gate = gate_.data();
  guidedFilterSelf(sig, base, options_.detailRadius, options_.detailEps, gfScratch_);
  for (size_t i = 0; i < kImagePixels; ++i) det[i] = sig[i] - base[i];
  auto smooth = [](float lo, float hi, float v) {
    const float x = std::clamp((v - lo) / std::max(hi - lo, 1e-6f), 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
  };

  // The noise gate: the detail's local energy (RMS over 3x3) against the frame's floor (its 10th
  // percentile, from a subsample). Closed where the detail is only noise, so neither the gain nor
  // the unsharp pass boosts it.
  for (size_t i = 0; i < kImagePixels; ++i) gate[i] = det[i] * det[i];
  boxFilter(gate, e, 1);  // the local energy, kept in energy_ for the halo guard
  std::copy(e, e + kImagePixels, gate);
  float* sample = colAcc_.data();
  int n = 0;
  for (size_t i = 5; i < kImagePixels && n < kFrameWidth; i += 191) sample[n++] = gate[i];  // every column
  std::nth_element(sample, sample + n / 10, sample + n);
  const float floorE = std::sqrt(std::max(sample[n / 10], 1e-6f));
  const float nLo = options_.detailNoiseLo * floorE, nHi = options_.detailNoiseHi * floorE;
  // (Running sums can leave a hair below zero where the energy is nil: a flat or clipped patch.)
  for (size_t i = 0; i < kImagePixels; ++i) gate[i] = smooth(nLo, nHi, std::sqrt(std::max(gate[i], 0.0f)));

  // The halo guard: beside a step, the filter leaves a faint rim along it (a residual of ~2% of the
  // step, up to ~5 pixels out), not texture. Where the nearest big step (the base's local range)
  // dwarfs the local detail (ratio detailEdgeLo..Hi), the gain fades out: scale-free, so it holds for
  // a 50-count edge and a 1000-count one alike, while texture, a good fraction of its own local
  // range, keeps its gain.
  range_.resize(kImagePixels);
  localRange(base, range_.data(), options_.detailEdgeRadius, gfScratch_);
  const float g = options_.detailGain, lim = options_.detailLimit;
  if (options_.detailSmooth > 0.0f) {
    // The added contrast, smoothed: texture (a few pixels across) keeps its gain, while pixel-scale
    // structure (noise, the stair-steps of a slanted edge) passes through at gain 1.
    float* extra = range_.data();  // (range_ is spent once each pixel's ratio is read)
    for (size_t i = 0; i < kImagePixels; ++i) {
      const float ratio = range_[i] / std::max(std::sqrt(std::max(e[i], 0.0f)), 1e-3f);
      extra[i] = (g - 1.0f) * gate[i] * (1.0f - smooth(options_.detailEdgeLo, options_.detailEdgeHi, ratio)) * det[i];
    }
    gaussianBlur(extra, e, options_.detailSmooth, gfScratch_);  // e: the local energy is spent too
    for (size_t i = 0; i < kImagePixels; ++i) det[i] = std::clamp(det[i] + e[i], -lim, lim);
  } else {
    for (size_t i = 0; i < kImagePixels; ++i) {
      const float ratio = range_[i] / std::max(std::sqrt(std::max(e[i], 0.0f)), 1e-3f);
      const float boost = gate[i] * (1.0f - smooth(options_.detailEdgeLo, options_.detailEdgeHi, ratio));
      det[i] = std::clamp((1.0f + (g - 1.0f) * boost) * det[i], -lim, lim);
    }
  }
  if (options_.detailMidGain > 1.0f) {
    // Experimental: a mid-scale layer (the base's own residual against a wider self-guided filter)
    // gets extra gain too, with the same scale-free halo guard over the wider filter's reach.
    midBase_.resize(kImagePixels);
    mid_.resize(kImagePixels);
    guidedFilterSelf(base, midBase_.data(), options_.detailMidRadius, options_.detailMidEps, gfScratch_);
    for (size_t i = 0; i < kImagePixels; ++i) mid_[i] = base[i] - midBase_[i];
    for (size_t i = 0; i < kImagePixels; ++i) e[i] = mid_[i] * mid_[i];
    boxFilter(e, gate, options_.detailMidRadius / 2);  // gate_ is free until the unsharp pass
    // Its own noise gate, like the fine layer's: against the mid layer's floor.
    int m = 0;
    for (size_t i = 5; i < kImagePixels && m < kFrameWidth; i += 191) sample[m++] = gate[i];
    std::nth_element(sample, sample + m / 10, sample + m);
    const float midFloor = std::sqrt(std::max(sample[m / 10], 1e-6f));
    const float mLo = options_.detailNoiseLo * midFloor, mHi = options_.detailNoiseHi * midFloor;
    localRange(midBase_.data(), range_.data(), 2 * options_.detailMidRadius, gfScratch_);
    const float gm = options_.detailMidGain - 1.0f;
    for (size_t i = 0; i < kImagePixels; ++i) {
      const float rms = std::sqrt(std::max(gate[i], 0.0f));
      const float ratio = range_[i] / std::max(rms, 1e-3f);
      const float open = options_.detailMidGate ? smooth(mLo, mHi, rms) : 1.0f;
      const float extra = gm * open * (1.0f - smooth(options_.detailEdgeLo, options_.detailEdgeHi, ratio)) * mid_[i];
      det[i] = std::clamp(det[i] + extra, -2.0f * lim, 2.0f * lim);
    }
    // The unsharp pass's gate below needs the fine layer's noise gate again.
    for (size_t i = 0; i < kImagePixels; ++i) {
      const float d = sig[i] - base[i];
      e[i] = d * d;
    }
    boxFilter(e, gate, 1);
    for (size_t i = 0; i < kImagePixels; ++i) gate[i] = smooth(nLo, nHi, std::sqrt(std::max(gate[i], 0.0f)));
  }
  tone_.map(base, display, exclude, 0.04f, det);

  // Unsharp on the display where there is texture (the gate) or an edge (the base's 3x3 range well
  // above the noise floor), clamped to each pixel's 3x3 min/max: sharper edges and texture with no
  // overshoot, and flat areas' noise left alone.
  if (options_.unsharpAmount > 0.0f) {
    localRange(base, e, 1, gfScratch_);
    const float eLo = options_.unsharpEdgeLo * floorE, eHi = options_.unsharpEdgeHi * floorE;
    for (size_t i = 0; i < kImagePixels; ++i) gate[i] = std::max(gate[i], smooth(eLo, eHi, e[i]));
    // (The base and the detail are spent: their buffers hold the 3x3 bounds and the unsharp input.)
    float* in = det;
    float* lo = base;
    float* hi = range_.data();
    std::copy(display, display + kImagePixels, in);
    gaussianBlur(in, e, options_.unsharpSigma, gfScratch_);
    localMinMax3(in, lo, hi, gfScratch_);
    const float amount = options_.unsharpAmount;
    for (size_t i = 0; i < kImagePixels; ++i)
      display[i] = std::clamp(in[i] + amount * gate[i] * (in[i] - e[i]), lo[i], hi[i]);
  }
}

void Pipeline::setRegion(const Region& region) {
  Region r = region;
  r.x0 = std::clamp(r.x0, 0, kFrameWidth);
  r.x1 = std::clamp(r.x1, r.x0, kFrameWidth);
  r.y0 = std::clamp(r.y0, 0, kImageRows);
  r.y1 = std::clamp(r.y1, r.y0, kImageRows);
  if (r.x0 == region_.x0 && r.x1 == region_.x1 && r.y0 == region_.y0 && r.y1 == region_.y1) return;
  region_ = r;
  tone_.retarget();
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
  if (resuming) {
    restartDestripe();
    haveFiltered_ = false;  // stage 4 starts over from the fresh frame
  }

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
  if (options_.denoise) denoise(sig);                         // stage 4
  if (options_.tone) {
    // Stage 5. Pixels at the camera's clip (too hot to measure) stay out of the statistics.
    // So do pixels outside the measurement region.
    const uint8_t* exclude = nullptr;
    const bool whole = region_.x0 <= 0 && region_.y0 <= 0 && region_.x1 >= kFrameWidth && region_.y1 >= kImageRows;
    if (!whole || *std::max_element(image, image + kImagePixels) >= kClipFloorRaw) {
      clipped_.resize(kImagePixels);
      for (int y = 0; y < kImageRows; ++y)
        for (int x = 0; x < kFrameWidth; ++x) {
          const size_t i = size_t(y) * kFrameWidth + size_t(x);
          clipped_[i] = image[i] >= kClipFloorRaw || !region_.contains(x, y);
        }
      exclude = clipped_.data();
    }
    if (options_.detail) enhance(sig, display, exclude);  // stage 6, with stage 5 on its base
    else tone_.map(sig, display, exclude);
  } else {
    renderBaseline(sig, display);
  }

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
