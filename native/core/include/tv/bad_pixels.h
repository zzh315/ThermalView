// Software bad-pixel map (docs/PLAN.md M4 stage 2): pixels the camera's own correction leaves wrong.
// Found offline with tools/py/bad_pixels.py; used for display replacement and to keep readouts off
// them. Never written to the camera (CLAUDE.md rule 2).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tv/frame.h"

namespace tv {

struct BadPixelMap {
  std::vector<uint32_t> pixels;  // row-major indices into the 256x192 image
  std::vector<uint8_t> mask;     // kImagePixels entries, nonzero = bad (computeReadouts' format)
  bool empty() const { return pixels.empty(); }
};

// The map for a camera serial ("KA1213"); empty for unknown units.
BadPixelMap badPixelMapFor(const std::string& serial);

// Replaces each listed pixel of signal (kImagePixels values) with the median of the good pixels in
// its 3x3 neighbourhood, widening to 5x5 when none is good.
void replaceBadPixels(const BadPixelMap& map, float* signal);

}  // namespace tv
