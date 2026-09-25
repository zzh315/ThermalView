#include "tv/range_lock.h"

#include <algorithm>
#include <cmath>

namespace tv {

double celsiusAt(const TemperatureLut& lut, double counts) {
  if (!std::isfinite(counts)) return NAN;
  const double c = std::clamp(counts, 0.0, double(TemperatureLut::kSize - 2));
  const auto i = uint16_t(c);
  if (!lut.valid(i) || !lut.valid(uint16_t(i + 1))) return NAN;
  return lut[i] + (c - i) * (lut[uint16_t(i + 1)] - lut[i]);
}

double countsAt(const TemperatureLut& lut, double tempC) {
  const uint16_t r = lut.rawAtOrAbove(tempC);
  if (r >= TemperatureLut::kSize) return double(TemperatureLut::kSize - 1);  // above the table
  if (r == 0 || !lut.valid(uint16_t(r - 1))) return double(r);               // at or below its start
  const double a = lut[uint16_t(r - 1)], b = lut[r];
  return double(r - 1) + (b > a ? std::clamp((tempC - a) / (b - a), 0.0, 1.0) : 1.0);
}

bool RangeLock::lock(const ToneMapper& tone, const TemperatureLut& lut) {
  release();
  if (!tone.ready()) return false;
  constexpr int K = ToneMapper::kCurve;
  const double lo = tone.lowCounts();
  const double hi = lo + std::max(double(tone.highCounts()) - lo, 1.0);  // (the span apply() uses)
  std::vector<double> e(K + 1);
  for (int k = 0; k <= K; ++k) {
    e[size_t(k)] = celsiusAt(lut, lo + (hi - lo) * k / K);
    if (!std::isfinite(e[size_t(k)])) return false;
    if (k > 0) e[size_t(k)] = std::max(e[size_t(k)], e[size_t(k - 1)]);
  }
  if (!(e.back() - e.front() > 0.0)) return false;
  edgesC_ = std::move(e);
  curve_ = tone.curve();
  loC_ = edgesC_.front();
  hiC_ = edgesC_.back();
  minSpanC_ = std::min(kMinSpanC, hiC_ - loC_);  // (a flat scene's lock may be narrower: no jump on the first drag)
  return true;
}

void RangeLock::setEnds(double loC, double hiC, double minC, double maxC) {
  if (!std::isfinite(loC) || !std::isfinite(hiC)) return;
  if (hiC - loC < minSpanC_) {
    const double mid = 0.5 * (loC + hiC);
    loC = mid - 0.5 * minSpanC_;
    hiC = mid + 0.5 * minSpanC_;
  }
  // Inside the table: a range dragged past its end slides back (keeping its span where it fits).
  if (maxC - minC > minSpanC_) {
    const double span = std::min(hiC - loC, maxC - minC);
    if (hiC > maxC) {
      hiC = maxC;
      loC = std::max(minC, hiC - span);
    }
    if (loC < minC) {
      loC = minC;
      hiC = std::min(maxC, loC + span);
    }
  }
  loC_ = loC;
  hiC_ = hiC;
}

bool RangeLock::mapping(const TemperatureLut& lut, FixedMapping* out) const {
  if (!held()) return false;
  constexpr int K = ToneMapper::kCurve;
  const double e0 = edgesC_.front();
  const double scale = (hiC_ - loC_) / (edgesC_.back() - e0);
  // The held edges, moved to the ends now, in this frame's counts.
  double counts[K + 1];
  for (int k = 0; k <= K; ++k) {
    counts[k] = countsAt(lut, loC_ + (edgesC_[size_t(k)] - e0) * scale);
    if (k > 0) counts[k] = std::max(counts[k], counts[k - 1]);
  }
  const double lo = counts[0], hi = counts[K];
  if (!(hi - lo >= 1.0)) return false;  // (the ends fell off the table)
  // The curve at even edges between them (ToneMapper's bins).
  out->lo = float(lo);
  out->hi = float(hi);
  out->curve.resize(K + 1);
  int j = 0;
  for (int e = 0; e <= K; ++e) {
    const double c = lo + (hi - lo) * e / K;
    while (j < K - 1 && counts[j + 1] < c) ++j;
    const double span = counts[j + 1] - counts[j];
    const double f = span > 0.0 ? std::clamp((c - counts[j]) / span, 0.0, 1.0) : 1.0;
    out->curve[size_t(e)] = float(curve_[size_t(j)] + f * (curve_[size_t(j + 1)] - curve_[size_t(j)]));
  }
  return true;
}

}  // namespace tv
