// Low / high / center readouts over the measurement region (docs/PLAN.md M2), from raw values and
// the temperature table, never from the display image (CLAUDE.md rule 2).
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "tv/frame.h"
#include "tv/temperature.h"

namespace tv {

// Camera pixels [x0, x1) x [y0, y1). The whole frame until M6 adds zoom and the box.
struct Region {
  int x0 = 0, y0 = 0, x1 = kFrameWidth, y1 = kImageRows;
  bool contains(int x, int y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};

struct Spot {
  double tempC = NAN;      // NaN: no valid temperature, shown as "--"
  double x = 0, y = 0;     // camera pixel (the center spot sits between pixels)
  bool overRange = false;  // at the active range's clip: shown as "> ceiling"
  bool valid() const { return std::isfinite(tempC) && !overRange; }
};

struct Readouts {
  Spot low, high, center;
};

// The raw values low and high may come from (M6: a locked range's, so the markers point at the
// hottest and coldest parts of what the range shows; the owner, 2026-09-26). The whole scale by
// default.
struct RawWindow {
  uint16_t lo = 0, hi = UINT16_MAX;
  bool contains(uint16_t v) const { return v >= lo && v <= hi; }
};

// The normal range clips per pixel, at raw ~13835-14192 (docs/DEVICE.md), and the rated range ends
// at 120 °C. A pixel is over range at 120 °C through the table or at kClipFloorRaw, whichever comes
// first, so every clipped pixel counts at any camera temperature.
inline constexpr double kRatedTopC = 120.0;
inline constexpr uint16_t kClipFloorRaw = 13700;
inline uint16_t overRangeRaw(const TemperatureLut& lut) {
  const uint16_t t = lut.rawAtOrAbove(kRatedTopC);
  return t < kClipFloorRaw ? t : kClipFloorRaw;
}

// One frame, unsmoothed. Low and high are our own argmin/argmax over the region, skipping pixels
// marked in badPixels (width x rows, nonzero = bad; empty until M4 builds the map). The center is
// the mean of the 1-4 pixels nearest the region's center.
// Low and high only from pixels inside [window] (none there: no low or high).
Readouts computeReadouts(const uint16_t* image, const TemperatureLut& lut, const Region& region,
                         uint16_t clipRaw, const std::vector<uint8_t>* badPixels = nullptr,
                         RawWindow window = {});

// Display smoothing: a low/high marker moves only when a new extreme beats the marked pixel by
// hysteresisC, so it doesn't jitter between near-equal pixels, and shown values follow an EMA with
// time constant tauS. Invalid and over-range states show at once.
class ReadoutFilter {
 public:
  explicit ReadoutFilter(double hysteresisC = 0.2, double tauS = 0.3)
      : hysteresis_(hysteresisC), tau_(tauS) {}

  Readouts update(const Readouts& frame, const uint16_t* image, const TemperatureLut& lut,
                  const Region& region, uint16_t clipRaw, double dtS, RawWindow window = {});
  void reset() { primed_ = false; }

 private:
  Spot follow(const Spot& extreme, Spot& marked, bool higherWins, const uint16_t* image,
              const TemperatureLut& lut, const Region& region, uint16_t clipRaw, RawWindow window) const;
  static void smooth(Spot& shown, const Spot& target, double alpha);

  double hysteresis_, tau_;
  bool primed_ = false;
  Spot markedLow_, markedHigh_;   // the pixels the markers sit on
  Readouts shown_;                // what's displayed
};

}  // namespace tv
