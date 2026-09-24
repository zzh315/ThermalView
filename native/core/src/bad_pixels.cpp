#include "tv/bad_pixels.h"

#include <algorithm>

namespace tv {
namespace {

struct Known {
  const char* serial;
  std::vector<std::pair<int, int>> xy;
};

// tools/py/bad_pixels.py on bench/flat and bench/flat_aged (+2/+4/+6 min dumps), 2026-09-25.
// KA1213 has no stuck, noisy or blinking pixels. Two neighbours at the left edge drift away
// from their surroundings as the FPA warms after a NUC: (2, 115) by about -17.6 counts per °C
// (-86 counts after 6 minutes and ~5 °C) and (3, 115) by about -8.7 counts per °C, while both
// sit within ~3 counts right after a NUC. Replaced for display at all times, which costs nothing.
const Known kKnown[] = {
    {"KA1213", {{2, 115}, {3, 115}}},
};

}  // namespace

BadPixelMap badPixelMapFor(const std::string& serial) {
  BadPixelMap map;
  for (const Known& k : kKnown) {
    if (serial != k.serial) continue;
    map.mask.assign(kImagePixels, 0);
    for (const auto& [x, y] : k.xy) {
      const uint32_t i = uint32_t(y) * kFrameWidth + uint32_t(x);
      map.pixels.push_back(i);
      map.mask[i] = 1;
    }
  }
  return map;
}

void replaceBadPixels(const BadPixelMap& map, float* signal) {
  if (map.empty()) return;
  float values[25];
  for (const uint32_t i : map.pixels) {
    const int x = int(i % kFrameWidth), y = int(i / kFrameWidth);
    for (int radius = 1; radius <= 2; ++radius) {
      int n = 0;
      for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
          const int xx = x + dx, yy = y + dy;
          if (xx < 0 || yy < 0 || xx >= kFrameWidth || yy >= kImageRows) continue;
          const size_t j = size_t(yy) * kFrameWidth + size_t(xx);
          if (map.mask[j]) continue;
          values[n++] = signal[j];
        }
      if (n == 0) continue;
      std::nth_element(values, values + n / 2, values + n);
      float m = values[n / 2];
      if (n % 2 == 0) m = 0.5f * (m + *std::max_element(values, values + n / 2));
      signal[i] = m;
      break;
    }
  }
}

}  // namespace tv
