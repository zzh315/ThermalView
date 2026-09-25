// M6's range lock (docs/PLAN.md: "lock current range", then the ends adjusted in °C): the mapping
// stage 5 shows at that moment, held in °C, so a color keeps meaning one temperature. The camera's
// own warming moves the counts a temperature reads at, so each frame's table converts it back.
#pragma once

#include <cmath>
#include <vector>

#include "tv/temperature.h"
#include "tv/tone.h"

namespace tv {

// The table at fractional counts (linear between entries), NaN where it has no valid temperature.
double celsiusAt(const TemperatureLut& lut, double counts);

// Its inverse: the fractional counts reading tempC, clamped to the table's valid, rising part.
double countsAt(const TemperatureLut& lut, double tempC);

class RangeLock {
 public:
  static constexpr double kMinSpanC = 0.5;  // the ends stay at least this far apart (or the span locked, if less)

  // Holds the mapping [tone] shows now, through [lut]. False, holding nothing, without a mapping or
  // valid temperatures at its ends.
  bool lock(const ToneMapper& tone, const TemperatureLut& lut);
  void release() { edgesC_.clear(); }
  bool held() const { return !edgesC_.empty(); }

  // The ends, in °C: the held curve keeps its shape, scaled linearly between them. Clamped to
  // [minC, maxC] when given (the table's valid temperatures), keeping the span.
  void setEnds(double loC, double hiC, double minC = -INFINITY, double maxC = INFINITY);
  double loC() const { return loC_; }
  double hiC() const { return hiC_; }

  // This frame's mapping in counts, through its table, for ToneMapper::setFixed. False if nothing is
  // held or the table can't express it.
  bool mapping(const TemperatureLut& lut, FixedMapping* out) const;

 private:
  std::vector<double> edgesC_;  // the curve's kCurve + 1 edges in °C when locked, ascending
  std::vector<float> curve_;    // the curve's values there
  double loC_ = 0, hiC_ = 0;    // the ends now
  double minSpanC_ = kMinSpanC;
};

}  // namespace tv
