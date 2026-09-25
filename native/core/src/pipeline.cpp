#include "tv/pipeline.h"

#include <algorithm>
#include <chrono>
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
    } else if (key == "nr") {
      o->nr = on;
    } else if (key == "nrSearch" && !value.empty()) {
      o->nrSearch = std::clamp(std::atoi(value.c_str()), 1, 7);
    } else if (key == "nrPatch" && !value.empty()) {
      o->nrPatch = std::clamp(std::atoi(value.c_str()), 0, 3);
    } else if (key == "nrStrength" && !value.empty()) {
      o->nrStrength = float(std::atof(value.c_str()));
    } else if (key == "nrSigma" && !value.empty()) {
      o->nrSigma = float(std::atof(value.c_str()));
    } else if (key == "stripes") {
      o->stripes = on;
    } else if (key == "stripesTau" && !value.empty()) {
      o->stripeOptions.tauFrames = float(std::atof(value.c_str()));
    } else if (key == "stripesGate" && !value.empty()) {
      o->stripeOptions.gate = float(std::atof(value.c_str()));
    } else if (key == "stripesHighpass" && !value.empty()) {
      o->stripeOptions.highpass = float(std::atof(value.c_str()));
    } else if (key == "stripesClamp" && !value.empty()) {
      o->stripeOptions.clamp = float(std::atof(value.c_str()));
    } else if (key == "stripesEdge" && !value.empty()) {
      o->stripeOptions.edge = float(std::atof(value.c_str()));
    } else if (key == "stripesMaxMotion" && !value.empty()) {
      o->stripeOptions.maxMotion = float(std::atof(value.c_str()));
    } else if (key == "stripesCompensate") {
      o->stripeOptions.compensate = on;
    } else if (key == "stripesSignificance" && !value.empty()) {
      o->stripeOptions.significance = float(std::atof(value.c_str()));
    } else if (key == "nrMethod" && !value.empty()) {
      o->nrMethod = value == "bm3d" || value == "1" ? 1 : 0;
    } else if (key == "bm3dStrength" && !value.empty()) {
      o->bm3dStrength = float(std::atof(value.c_str()));
    } else if (key == "bm3dBlock" && !value.empty()) {
      o->bm3d.block = std::clamp(std::atoi(value.c_str()), 2, 16);
    } else if (key == "bm3dStride" && !value.empty()) {
      o->bm3d.stride = std::clamp(std::atoi(value.c_str()), 1, 16);
    } else if (key == "bm3dSearch" && !value.empty()) {
      o->bm3d.search = std::clamp(std::atoi(value.c_str()), 0, 32);
    } else if (key == "bm3dGroup1" && !value.empty()) {
      o->bm3d.group1 = std::clamp(std::atoi(value.c_str()), 1, 64);
    } else if (key == "bm3dGroup2" && !value.empty()) {
      o->bm3d.group2 = std::clamp(std::atoi(value.c_str()), 1, 64);
    } else if (key == "bm3dWiener") {
      o->bm3d.wiener = on;
    } else if (key == "bm3dAll") {
      o->bm3d.aggregateAll = on;
    } else if (key == "bm3dLambda" && !value.empty()) {
      o->bm3d.lambda = float(std::atof(value.c_str()));
    } else if (key == "bm3dMu2" && !value.empty()) {
      o->bm3d.mu2 = float(std::atof(value.c_str()));
    } else if (key == "bm3dTau1" && !value.empty()) {
      o->bm3d.tau1 = float(std::atof(value.c_str()));
    } else if (key == "bm3dTau2" && !value.empty()) {
      o->bm3d.tau2 = float(std::atof(value.c_str()));
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
  if (o.stripes) add("stripes");
  if (o.nr && o.nrMethod == 1) add("bm3d(s" + std::to_string(o.bm3dStrength).substr(0, 4) + ")");
  else if (o.nr) add("nr(h" + std::to_string(o.nrStrength).substr(0, 4) + ")");
  if (o.tone) add("tone(g" + std::to_string(o.toneOptions.maxGain).substr(0, 4) + ")");
  if (o.detail && o.tone) {
    if (o.detailMidGain > 1.0f) add("texture(x" + std::to_string(o.detailMidGain).substr(0, 4) + ")");
    if (o.detailGain > 1.0f) add("detail(x" + std::to_string(o.detailGain).substr(0, 4) + ")");
    if (o.unsharpAmount > 0.0f) add("unsharp(" + std::to_string(o.unsharpAmount).substr(0, 4) + ")");
    if (o.detailMidGain <= 1.0f && o.detailGain <= 1.0f && o.unsharpAmount <= 0.0f) add("detail(off)");
  }
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
      heldSignal_(kImagePixels) {
  stripes_.setOptions(options.stripeOptions);
}

void Pipeline::setOptions(const PipelineOptions& options) {
  options_ = options;
  tone_.setOptions(options.toneOptions);
  stripes_.setOptions(options.stripeOptions);
}

void Pipeline::updateNoiseSigma(const uint16_t* image) {
  // The trimmed RMS of the difference from the previous frame, over a subsample: still texture
  // cancels, and the few pixels that moved are trimmed.
  nrSample_.resize(kImagePixels / 7 + 1);
  size_t n = 0;
  double sumAbs = 0;
  for (size_t i = 3; i < kImagePixels; i += 7) {
    const float d = float(image[i]) - float(previous_[i]);
    nrSample_[n++] = d;
    sumAbs += std::fabs(d);
  }
  if (n == 0 || sumAbs == 0.0) return;  // a repeat: nothing to measure
  const float cut = 5.0f * std::max(float(sumAbs / double(n)), 0.5f);
  double sum2 = 0;
  size_t kept = 0;
  for (size_t k = 0; k < n; ++k)
    if (std::fabs(nrSample_[k]) < cut) {
      sum2 += double(nrSample_[k]) * nrSample_[k];
      ++kept;
    }
  if (kept < n / 2) return;
  const float reading = 1.31f * float(std::sqrt(sum2 / double(kept)));
  const float current = nrSigma_ > 0.0f ? nrSigma_ : options_.nrNominalSigma;
  if (reading < 0.5f * current || reading > 2.0f * current) return;  // motion, a step: not the noise
  nrSigma_ = current + 0.02f * (reading - current);  // ~2 s at 25 fps
}

void Pipeline::reduceNoise(float* sig, const std::function<void()>& alongside) {
  const float sigma = options_.nrSigma > 0.0f ? options_.nrSigma : noiseSigma();
  const float h = options_.nrStrength * sigma;
  const bool accelerator = reducer_.start && reducer_.finish;
  nrAccelerated_ = false;
  NoiseRequest request;
  request.method = options_.nrMethod;
  request.searchRadius = options_.nrSearch;
  request.patchRadius = options_.nrPatch;
  request.h = h;
  request.sigma = options_.bm3dStrength * sigma;
  request.bm3d = options_.bm3d;
  if (accelerator && reducer_.start(sig, request)) {
    alongside();  // while it runs
    nrOut_.resize(kImagePixels);
    nrAccelerated_ = reducer_.finish(nrOut_.data());
    if (nrAccelerated_) {
      std::copy(nrOut_.begin(), nrOut_.end(), sig);
      return;
    }
  }
  alongside();  // before the CPU filter changes sig in place
  if (options_.nrMethod == 1 && !accelerator) {  // BM3D on the CPU: the harness's reference
    nrOut_.resize(kImagePixels);
    bm3d(sig, nrOut_.data(), request.sigma, options_.bm3d);
    std::copy(nrOut_.begin(), nrOut_.end(), sig);
    return;
  }
  // The CPU: at most nrFallbackSearch when an accelerator failed (the full search would miss the
  // frame budget), h scaled to keep the noise reduction (nr_study: 5x5 search at 1.27x h ~ 11x11).
  int search = options_.nrSearch;
  float hh = h;
  if (accelerator && search > options_.nrFallbackSearch) {
    hh = h * (1.0f + 0.09f * float(search - options_.nrFallbackSearch));
    search = options_.nrFallbackSearch;
  }
  nlmPad(sig, search, options_.nrPatch, &nrPadded_);
  nlmBand(nrPadded_, sig, 0, kImageRows, search, options_.nrPatch, hh, nrScratch_);
}

void Pipeline::reset() {
  havePrevious_ = false;
  nrSigma_ = 0.0f;
  frozen_ = false;
  blendLeft_ = blendTotal_ = 0;
  restartDestripe();
  stripes_.reset();
  haveFiltered_ = false;
  tone_.reset();
}

void Pipeline::restartDestripe() {
  std::fill(colOffset_.begin(), colOffset_.end(), 0.0f);
  std::fill(rowOffset_.begin(), rowOffset_.end(), 0.0f);
  destripeFrames_ = 0;
}

void Pipeline::applyDestripe(float* sig) {
  const int w = kFrameWidth, h = kImageRows;
  for (int y = 0; y < h; ++y) {
    float* row = sig + size_t(y) * w;
    const float r = rowOffset_[size_t(y)];
    for (int x = 0; x < w; ++x) row[x] -= colOffset_[size_t(x)] + r;
  }
}

void Pipeline::updateDestripe(const float* sig) {
  const int w = kFrameWidth, h = kImageRows;
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
  // (the window is 9 wide from x = 4 to w - 5, where dividing by its other 8 is an exact * 0.125;
  // the sums are branchless, adding 0 where a residual isn't kept: the same values as the plain loop)
  for (int y = 0; y < h; ++y) {
    const float* row = sig + size_t(y) * w;
    float sum = 0;
    int lo = 0, hi = -1;  // the window [lo, hi]
    const auto edge = [&](int x) {
      while (hi < std::min(w - 1, x + 4)) sum += row[++hi];
      while (lo < x - 4) sum -= row[lo++];
      const float c = row[x];
      const float r = c - (sum - c) / float(hi - lo);
      const bool k = keep(c, r, row[std::max(0, x - 1)], row[std::min(w - 1, x + 1)]);
      colAcc[size_t(x)] += k ? r : 0.0f;
      colCount[size_t(x)] += k ? 1.0f : 0.0f;
    };
    for (int x = 0; x <= 4; ++x) edge(x);
    for (int x = 5; x <= w - 5; ++x) {
      sum += row[x + 4];
      sum -= row[x - 5];
      const float c = row[x];
      const float r = c - (sum - c) * 0.125f;
      const bool k = keep(c, r, row[x - 1], row[x + 1]);
      colAcc[size_t(x)] += k ? r : 0.0f;
      colCount[size_t(x)] += k ? 1.0f : 0.0f;
    }
    hi = w - 1;
    lo = w - 9;
    for (int x = w - 4; x < w; ++x) edge(x);
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
    const bool full = bottom - top == 8;  // (then * 0.125 is the same as / 8)
    float acc = 0;
    int n = 0;
    for (int x = 0; x < w; ++x) {
      const float c = row[x];
      const float r = full ? c - (colSum[size_t(x)] - c) * 0.125f : c - (colSum[size_t(x)] - c) / count;
      const bool k = keep(c, r, up[x], down[x]);
      acc += k ? r : 0.0f;
      n += k;
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
  range_.resize(kImagePixels);
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
  // A local-energy map's floor: its 10th percentile, from a subsample over every column.
  float* sample = colAcc_.data();
  auto floorOf = [&](const float* energy) {
    int n = 0;
    for (size_t i = 5; i < kImagePixels && n < kFrameWidth; i += 191) sample[n++] = energy[i];
    std::nth_element(sample, sample + n / 10, sample + n);
    return std::sqrt(std::max(sample[n / 10], 1e-6f));
  };
  // Only what's switched on is computed: the fine layer's gain, the unsharp pass, the mid layer.
  const bool fine = options_.detailGain > 1.0f, sharpen = options_.unsharpAmount > 0.0f,
             mid = options_.detailMidGain > 1.0f;
  const float lim = options_.detailLimit;

  // The fine layer's noise gate: its local energy (RMS over 3x3) against the frame's floor. Closed
  // where the detail is only noise, so neither the gain nor the unsharp pass boosts it. (Running
  // sums can leave a hair below zero where the energy is nil: a flat or clipped patch.)
  float floorE = 0.0f;
  if (fine || sharpen) {
    for (size_t i = 0; i < kImagePixels; ++i) gate[i] = det[i] * det[i];
    boxFilter(gate, e, 1);  // the local energy, kept for the halo guard
    floorE = floorOf(e);
    const float nLo = options_.detailNoiseLo * floorE, nHi = options_.detailNoiseHi * floorE;
    for (size_t i = 0; i < kImagePixels; ++i) gate[i] = smooth(nLo, nHi, std::sqrt(std::max(e[i], 0.0f)));
  }

  // The fine layer's gain, under the halo guard: beside a step, the filter leaves a faint rim along
  // it (a residual of ~2% of the step, up to ~5 pixels out), not texture. Where the nearest big step
  // (the base's local range) dwarfs the local detail (ratio detailEdgeLo..Hi), the gain fades out:
  // scale-free, so it holds for a 50-count edge and a 1000-count one alike, while texture, a good
  // fraction of its own local range, keeps its gain.
  if (fine) {
    localRange(base, range_.data(), options_.detailEdgeRadius, gfScratch_);
    const float g = options_.detailGain;
    if (options_.detailSmooth > 0.0f) {
      // The added contrast, smoothed: texture (a few pixels across) keeps its gain, while
      // pixel-scale structure (noise, the stair-steps of a slanted edge) passes through at gain 1.
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
  }

  // The mid-scale layer: the base against a wider self-guided filter (surfaces and shapes a few to
  // ~16 pixels across), with its own noise gate and the same halo guard over the wider filter's
  // reach (its local range at half resolution: 4x cheaper, a pixel more conservative). Its extra
  // contrast goes into the tone curve's input, not onto the display: the curve's range and
  // histogram then make room for it, where added after the curve it pushed warm areas past white
  // (the owner saw the keyboard's warm middle lose its key lines).
  const float* toneInput = base;
  if (mid) {
    midBase_.resize(kImagePixels);
    mid_.resize(kImagePixels);
    float* mb = midBase_.data();
    guidedFilterSelf(base, mb, options_.detailMidRadius, options_.detailMidEps, gfScratch_);
    for (size_t i = 0; i < kImagePixels; ++i) mid_[i] = base[i] - mb[i];
    localRangeHalf(mb, range_.data(), 2 * options_.detailMidRadius, gfScratch_);
    for (size_t i = 0; i < kImagePixels; ++i) mb[i] = mid_[i] * mid_[i];  // (the mid base is spent)
    boxFilter(mb, e, options_.detailMidRadius / 2);
    const float midFloor = floorOf(e);
    const float mLo = options_.detailNoiseLo * midFloor, mHi = options_.detailNoiseHi * midFloor;
    const float gm = options_.detailMidGain - 1.0f;
    for (size_t i = 0; i < kImagePixels; ++i) {
      const float rms = std::sqrt(std::max(e[i], 0.0f));
      const float ratio = range_[i] / std::max(rms, 1e-3f);
      const float open = options_.detailMidGate ? smooth(mLo, mHi, rms) : 1.0f;
      const float extra = gm * open * (1.0f - smooth(options_.detailEdgeLo, options_.detailEdgeHi, ratio)) * mid_[i];
      mb[i] = base[i] + std::clamp(extra, -2.0f * lim, 2.0f * lim);  // (mb: the squares are spent)
    }
    toneInput = mb;
  }
  tone_.map(toneInput, display, exclude, 0.04f, det);

  // Unsharp on the display where there is texture (the gate) or an edge (the base's 3x3 range well
  // above the noise floor), clamped to each pixel's 3x3 min/max: sharper edges and texture with no
  // overshoot, and flat areas' noise left alone.
  if (sharpen) {
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

void Pipeline::process(const uint16_t* image, float* display, float* signal, FrameMeta meta,
                       const std::function<void()>& alongside) {
  using Clock = std::chrono::steady_clock;
  const auto msSince = [](Clock::time_point t) {
    return std::chrono::duration<float, std::milli>(Clock::now() - t).count();
  };
  partMs_.fill(0.0f);
  auto t = Clock::now();
  float callerMs = 0.0f;  // (the caller's alongside work, inside stage 4b's call: not 4b's own time)
  bool alongsideRan = false;
  const std::function<void()> once = [&] {
    if (alongsideRan) return;
    alongsideRan = true;
    const auto t0 = Clock::now();
    if (alongside) alongside();
    callerMs = msSince(t0);
  };
  const size_t bytes = kImagePixels * sizeof(uint16_t);
  const bool repeat = havePrevious_ && std::memcmp(image, previous_.data(), bytes) == 0;
  if ((options_.nr || options_.stripes) && havePrevious_ && !repeat && !frozen_) updateNoiseSigma(image);  // (4b, 3c)
  std::memcpy(previous_.data(), image, bytes);
  havePrevious_ = true;

  if (options_.shutterHold && repeat) {
    // A shutter cycle (M1: 30-31 repeats of one image): the camera's noise makes an accidental exact
    // repeat impossible. Show what was last shown and leave every stage's state alone.
    frozen_ = true;
    std::copy(held_.begin(), held_.end(), display);
    if (signal) std::copy(heldSignal_.begin(), heldSignal_.end(), signal);
    once();
    partMs_[kEarly] = msSince(t) - callerMs;
    return;
  }

  // The first fresh frame after a cycle (or after hold()): a calibration reset the pattern stage 3b
  // tracks, so it starts over before it runs.
  const bool resuming = frozen_;
  if (resuming) {
    restartDestripe();
    stripes_.reset();  // (a calibration changed the pattern the reference holds)
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
  partMs_[kEarly] = msSince(t);
  t = Clock::now();
  // Stage 3b: this frame's correction now; its estimate for the next frames from this corrected
  // signal later, while stage 4b's GPU works (it changes nothing in this frame).
  if (options_.destripe) applyDestripe(sig);
  partMs_[kDestripe] = msSince(t);
  t = Clock::now();
  // Stage 3c: this frame's column and row noise now, on 3b's output (where the persistent pattern is
  // gone, so a reference moved with the scene doesn't drag it along); its reference's update later,
  // with 3b's (3b learns from its own output before 3c's correction, and each change it makes to its
  // estimate goes into 3c's reference too).
  const bool stripesRan = options_.stripes;
  if (stripesRan) {
    after3b_.assign(sig, sig + kImagePixels);
    stripes_.process(sig, noiseSigma());
  }
  partMs_[kStripes] = msSince(t);
  bool patternChanged = false;
  const auto learnDestripe = [&] {  // stage 3b's estimate update (and, with 3c on, what it changed)
    const auto t0 = Clock::now();
    if (stripesRan) {
      colBefore_ = colOffset_;
      rowBefore_ = rowOffset_;
      patternChanged = true;
    }
    updateDestripe(stripesRan ? after3b_.data() : sig);
    partMs_[kLearn] += msSince(t0);
  };
  bool destripePending = options_.destripe, stripesPending = stripesRan;
  const std::function<void()> sideWork = [&] {
    // (stage 3b learns the persistent pattern from the signal before 3c took this frame's part off:
    // fed 3c's output it would miss the fast share of the pattern's drift, and the two would chase
    // each other)
    if (destripePending) learnDestripe();
    destripePending = false;
    if (stripesPending) {
      const auto t0 = Clock::now();
      stripes_.updateReference();  // (toward this frame, which had 3b's old estimate taken off)
      if (patternChanged) {        // then the change 3b just made, as the next frame will have it
        for (size_t x = 0; x < colBefore_.size(); ++x) colBefore_[x] = colOffset_[x] - colBefore_[x];
        for (size_t y = 0; y < rowBefore_.size(); ++y) rowBefore_[y] = rowOffset_[y] - rowBefore_[y];
        stripes_.applyPatternChange(colBefore_, rowBefore_);
      }
      partMs_[kReference] += msSince(t0);
    }
    stripesPending = false;
    once();
  };
  if (options_.denoise) {  // stage 4 (removed: off) changes sig in place, so the estimate goes first
    if (destripePending) learnDestripe();
    destripePending = false;
    denoise(sig);
  }
  t = Clock::now();
  if (options_.nr) reduceNoise(sig, sideWork);  // stage 4b
  sideWork();  // (if stage 4b didn't run it: off)
  partMs_[kNoise] = std::max(0.0f, msSince(t) - partMs_[kLearn] - partMs_[kReference] - callerMs);
  t = Clock::now();
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
  partMs_[kTone] = msSince(t);
  t = Clock::now();
  struct RestTimer {  // (the blend and the copies below, however the function returns)
    float* out;
    Clock::time_point t0;
    ~RestTimer() { *out = std::chrono::duration<float, std::milli>(Clock::now() - t0).count(); }
  } rest{&partMs_[kRest], t};

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
