#include "tv/readouts.h"

#include <algorithm>

namespace tv {
namespace {

Spot spotAt(const uint16_t* image, const TemperatureLut& lut, int x, int y, uint16_t clipRaw) {
  const uint16_t raw = image[size_t(y) * kFrameWidth + x];
  Spot s;
  s.x = x;
  s.y = y;
  s.overRange = raw >= clipRaw;
  s.tempC = lut.valid(raw) ? lut[raw] : NAN;
  return s;
}

}  // namespace

Readouts computeReadouts(const uint16_t* image, const TemperatureLut& lut, const Region& r,
                         uint16_t clipRaw, const std::vector<uint8_t>* bad) {
  const bool useBad = bad && bad->size() == kImagePixels;
  int loX = -1, loY = -1, hiX = -1, hiY = -1;
  uint16_t lo = UINT16_MAX, hi = 0;
  for (int y = r.y0; y < r.y1; ++y) {
    const uint16_t* row = image + size_t(y) * kFrameWidth;
    for (int x = r.x0; x < r.x1; ++x) {
      if (useBad && (*bad)[size_t(y) * kFrameWidth + x]) continue;
      const uint16_t v = row[x];
      if (v < lo) { lo = v; loX = x; loY = y; }
      if (v > hi) { hi = v; hiX = x; hiY = y; }
    }
  }
  Readouts out;
  if (loX < 0) return out;  // empty region or every pixel bad
  out.low = spotAt(image, lut, loX, loY, clipRaw);
  out.high = spotAt(image, lut, hiX, hiY, clipRaw);

  // Center: 1 or 2 pixels per axis around the region's midpoint, averaged in °C.
  const double cx = (r.x0 + r.x1 - 1) / 2.0, cy = (r.y0 + r.y1 - 1) / 2.0;
  const int xs[2] = {int(std::floor(cx)), int(std::ceil(cx))};
  const int ys[2] = {int(std::floor(cy)), int(std::ceil(cy))};
  double sum = 0;
  int n = 0;
  bool invalid = false, over = false;
  for (int j = 0; j < (ys[0] == ys[1] ? 1 : 2); ++j)
    for (int i = 0; i < (xs[0] == xs[1] ? 1 : 2); ++i) {
      const Spot s = spotAt(image, lut, xs[i], ys[j], clipRaw);
      over |= s.overRange;
      if (std::isfinite(s.tempC)) { sum += s.tempC; ++n; } else { invalid = true; }
    }
  out.center.x = cx;
  out.center.y = cy;
  out.center.overRange = over;
  out.center.tempC = (invalid || n == 0) ? NAN : sum / n;
  return out;
}

Spot ReadoutFilter::follow(const Spot& extreme, Spot& marked, bool higherWins, const uint16_t* image,
                           const TemperatureLut& lut, const Region& region, uint16_t clipRaw) const {
  // The marked pixel's current reading; move to the new extreme only if it wins by the margin.
  const bool markedUsable = region.contains(int(marked.x), int(marked.y));
  Spot now = markedUsable ? spotAt(image, lut, int(marked.x), int(marked.y), clipRaw) : Spot{};
  const bool move = !markedUsable || !std::isfinite(now.tempC) || !std::isfinite(extreme.tempC) ||
                    extreme.overRange ||
                    (higherWins ? extreme.tempC > now.tempC + hysteresis_
                                : extreme.tempC < now.tempC - hysteresis_);
  if (move) now = extreme;
  marked = now;
  return now;
}

void ReadoutFilter::smooth(Spot& shown, const Spot& target, double alpha) {
  const bool jump = !std::isfinite(shown.tempC) || !std::isfinite(target.tempC) || target.overRange ||
                    shown.overRange;
  const double t = jump ? target.tempC : shown.tempC + alpha * (target.tempC - shown.tempC);
  shown = target;
  shown.tempC = t;
}

Readouts ReadoutFilter::update(const Readouts& frame, const uint16_t* image, const TemperatureLut& lut,
                               const Region& region, uint16_t clipRaw, double dtS) {
  if (!primed_) {
    markedLow_ = frame.low;
    markedHigh_ = frame.high;
    shown_ = frame;
    primed_ = true;
    return shown_;
  }
  const Spot high = follow(frame.high, markedHigh_, true, image, lut, region, clipRaw);
  const Spot low = follow(frame.low, markedLow_, false, image, lut, region, clipRaw);
  const double alpha = 1.0 - std::exp(-std::max(dtS, 0.0) / tau_);
  smooth(shown_.high, high, alpha);
  smooth(shown_.low, low, alpha);
  smooth(shown_.center, frame.center, alpha);
  return shown_;
}

}  // namespace tv
