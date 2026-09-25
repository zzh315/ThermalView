#include "tv/tone.h"

#include <algorithm>
#include <cmath>

namespace tv {
namespace {

float follow(float current, float target, float tauS, float dtS) {
  if (tauS <= 0.0f) return target;
  return current + (1.0f - std::exp(-dtS / tauS)) * (target - current);
}

}  // namespace

ToneMapper::ToneMapper(const ToneOptions& options)
    : options_(options), previous_(kImagePixels), work_(kImagePixels), curve_(kCurve + 1), target_(kCurve + 1) {
  reset();
}

void ToneMapper::reset() {
  havePrevious_ = haveRange_ = haveCurve_ = fixed_ = false;
  offset_ = 0.0f;
  for (int i = 0; i <= kCurve; ++i) curve_[size_t(i)] = float(i) / float(kCurve);
}

void ToneMapper::setFixed(const FixedMapping* m) {
  if (!m) {
    if (fixed_) {
      // The scene's own mapping from the next frame, as at a start (a clean cut: easing from a
      // locked range far from the scene's would take seconds). The offset tracking carries on.
      fixed_ = false;
      haveRange_ = haveCurve_ = false;
    }
    return;
  }
  if (m->curve.size() != size_t(kCurve + 1) || !(m->hi > m->lo)) return;
  fixed_ = true;
  offset_ = 0.0f;  // absolute counts
  lo_ = m->lo;
  hi_ = m->hi;
  std::copy(m->curve.begin(), m->curve.end(), curve_.begin());
  haveRange_ = haveCurve_ = true;
}

void ToneMapper::map(const float* signal, float* out, const uint8_t* exclude, float dtS, const float* detail) {
  if (fixed_) {
    // The range lock: the mapping as given. The frame still counts as the previous one, so the offset
    // tracking carries on from here when the lock ends.
    std::copy(signal, signal + kImagePixels, previous_.begin());
    havePrevious_ = true;
    apply(signal, out, detail);
    return;
  }
  // 1. The global offset: the median frame-to-frame change over a subsample (robust to anything
  //    covering less than half the frame), accumulated.
  if (!options_.trackOffset) {
    offset_ = 0.0f;
  } else if (havePrevious_) {
    float* d = work_.data();
    int n = 0;
    for (size_t i = 3; i < kImagePixels; i += 7) {
      if (exclude && exclude[i]) continue;
      d[n++] = signal[i] - previous_[i];
    }
    if (n > 0) {
      std::nth_element(d, d + n / 2, d + n);
      offset_ += d[n / 2];
    }
  } else if (haveRange_) {
    // After a skipped stretch (a calibration): carry the offset so the frame's median stays where
    // the mapping had it.
    float* d = work_.data();
    int n = 0;
    for (size_t i = 3; i < kImagePixels; i += 7)
      if (!(exclude && exclude[i])) d[n++] = signal[i];
    if (n > 0) {
      std::nth_element(d, d + n / 2, d + n);
      offset_ = d[n / 2] - 0.5f * (lo_ + hi_);
    }
  }
  std::copy(signal, signal + kImagePixels, previous_.begin());
  havePrevious_ = true;

  // 2. The robust range of (signal - offset): percentiles from a sorted subsample.
  float* v = work_.data();
  int n = 0;
  for (size_t i = 0; i < kImagePixels; i += 2)
    if (!(exclude && exclude[i])) v[n++] = signal[i] - offset_;
  if (n < 16) {
    if (!haveRange_) {
      for (size_t i = 0; i < kImagePixels; ++i) out[i] = 0.5f;
      return;
    }
    apply(signal, out, detail);  // nothing to measure: keep the current mapping
    return;
  }
  // (the high percentile is searched for only above the low one: nth_element leaves every value
  // from the low one's place on at least as large, so it's the same value either way)
  auto index = [&](float p) { return std::clamp(int(p / 100.0f * float(n - 1) + 0.5f), 0, n - 1); };
  const bool retargeting = retargetLeftS_ > 0.0f;
  const float fastTau = retargetS_ / 3.0f;
  if (retargeting) retargetLeftS_ -= dtS;
  const int kLo = index(options_.lowPct), kHi = index(options_.highPct);
  std::nth_element(v, v + kLo, v + n);
  float lo = v[kLo];
  if (kHi > kLo) std::nth_element(v + kLo + 1, v + kHi, v + n);
  else std::nth_element(v, v + kHi, v + n);
  float hi = v[kHi];
  if (hi - lo < 1.0f) {
    const float c = 0.5f * (lo + hi);
    lo = c - 0.5f;
    hi = c + 0.5f;
  }

  // 3. Damped range: expand fast, contract slowly, ignore small changes.
  if (!haveRange_) {
    lo_ = lo;
    hi_ = hi;
    haveRange_ = true;
  } else {
    const float db = retargeting ? 0.0f : std::max(options_.deadbandCounts, options_.deadbandPct / 100.0f * (hi_ - lo_));
    const float expand = retargeting ? fastTau : options_.expandTauS;
    const float contract = retargeting ? fastTau : options_.contractTauS;
    if (lo < lo_ - db) lo_ = follow(lo_, lo, expand, dtS);
    else if (lo > lo_ + db) lo_ = follow(lo_, lo, contract, dtS);
    if (hi > hi_ + db) hi_ = follow(hi_, hi, expand, dtS);
    else if (hi < hi_ - db) hi_ = follow(hi_, hi, contract, dtS);
  }
  const float span = std::max(hi_ - lo_, 1.0f);
  const float binWidth = span / float(kCurve);

  // 4. The curve: double-plateau histogram equalization blended with linear, each bin's rise capped
  //    at maxGain (levels per count): a flat scene gets a calm, centred band instead of stretched noise.
  hist_.assign(kCurve, 0);
  const float perBin = 1.0f / binWidth;  // (a multiply a pixel instead of a divide)
  for (size_t i = 0; i < kImagePixels; ++i) {
    if (exclude && exclude[i]) continue;
    const int b = int((signal[i] - offset_ - lo_) * perBin);  // (truncated: just under lo_ counts in bin 0)
    if (b >= 0 && b < kCurve) ++hist_[size_t(b)];
  }
  double occupiedSum = 0, peaksSum = 0;
  int occupied = 0, peaks = 0;
  for (int b = 0; b < kCurve; ++b) {
    const uint32_t c = hist_[size_t(b)];
    if (!c) continue;
    occupiedSum += c;
    ++occupied;
    const uint32_t l = b > 0 ? hist_[size_t(b - 1)] : 0, r = b + 1 < kCurve ? hist_[size_t(b + 1)] : 0;
    if (c >= l && c >= r) {
      peaksSum += c;
      ++peaks;
    }
  }
  const double up = peaks ? peaksSum / peaks : 1.0;
  const double down = occupied ? options_.plateauDown * occupiedSum / occupied : 0.0;
  // Per-bin rises: the plateau-clipped histogram, blended with a linear share, each capped.
  std::vector<float>& inc = work_;  // free again: the percentiles are done
  double total = 0;
  for (int b = 0; b < kCurve; ++b) {
    double c = hist_[size_t(b)];
    if (c > 0) c = std::clamp(c, std::min(down, up), up);
    inc[size_t(b)] = float(c);
    total += c;
  }
  const float lin = options_.linearShare / float(kCurve);
  const float maxRise = options_.maxGain * binWidth / 255.0f;  // fraction of the output per bin
  float rise = 0;
  for (int b = 0; b < kCurve; ++b) {
    const float he = total > 0 ? float(inc[size_t(b)] / total) : 1.0f / float(kCurve);
    inc[size_t(b)] = std::min(lin + (1.0f - options_.linearShare) * he, maxRise);
    rise += inc[size_t(b)];
  }
  // What the cap cut off goes, evenly, to bins still under it (the gaps between a scene's zones), so
  // separate zones spread apart; a flat scene's bins are all at the cap, so it stays a calm band.
  for (int pass = 0; pass < 8 && rise < 1.0f - 1e-4f; ++pass) {
    int room = 0;
    for (int b = 0; b < kCurve; ++b) room += inc[size_t(b)] < maxRise;
    if (!room) break;
    const float add = (1.0f - rise) / float(room);
    rise = 0;
    for (int b = 0; b < kCurve; ++b) {
      if (inc[size_t(b)] < maxRise) inc[size_t(b)] = std::min(inc[size_t(b)] + add, maxRise);
      rise += inc[size_t(b)];
    }
  }
  // The curve at the bin edges: cumulative, centred when the cap kept it below the full range.
  target_[0] = 0.5f * (1.0f - rise);
  for (int b = 0; b < kCurve; ++b) target_[size_t(b + 1)] = target_[size_t(b)] + inc[size_t(b)];
  // The first frame takes its curve as it is: easing in from a neutral one would show a second or two
  // of harsh contrast at every start.
  const float curveTau = retargeting ? fastTau : options_.curveTauS;
  const float a = haveCurve_ ? 1.0f - std::exp(-dtS / std::max(curveTau, 1e-3f)) : 1.0f;
  haveCurve_ = true;
  for (int e = 0; e <= kCurve; ++e) curve_[size_t(e)] += a * (target_[size_t(e)] - curve_[size_t(e)]);

  apply(signal, out, detail);
}

void ToneMapper::apply(const float* signal, float* out, const float* detail) const {
  // 5. Apply: interpolate between edges (clamped outside the range), squeeze into outLo..outHi. With a
  //    detail layer, add it at the curve's local slope (per count), so it keeps its size relative to
  //    the base's contrast there.
  const float span = std::max(hi_ - lo_, 1.0f);
  const float binWidth = span / float(kCurve);
  const float perBin = 1.0f / binWidth;  // (multiplies instead of two divides a pixel)
  const float scale = options_.outHi - options_.outLo;
  for (size_t i = 0; i < kImagePixels; ++i) {
    const float t = std::clamp((signal[i] - offset_ - lo_) * perBin, 0.0f, float(kCurve));
    const int k = std::min(int(t), kCurve - 1);
    const float step = curve_[size_t(k + 1)] - curve_[size_t(k)];
    float c = curve_[size_t(k)] + (t - float(k)) * step;
    if (detail) c += detail[i] * step * perBin;
    out[i] = options_.outLo + scale * std::clamp(c, 0.0f, 1.0f);
  }
}

}  // namespace tv
