#include "tv/display.h"

#include <algorithm>

namespace tv {

void renderBaseline(const uint16_t* image, float* out) {
  const auto [lo, hi] = std::minmax_element(image, image + kImagePixels);
  const float scale = *hi > *lo ? 1.0f / float(*hi - *lo) : 0.0f;
  const float min = float(*lo);
  for (size_t i = 0; i < kImagePixels; ++i) out[i] = std::clamp((float(image[i]) - min) * scale, 0.0f, 1.0f);
}

void renderBaseline(const float* signal, float* out) {
  const auto [lo, hi] = std::minmax_element(signal, signal + kImagePixels);
  const float scale = *hi > *lo ? 1.0f / (*hi - *lo) : 0.0f;
  const float min = *lo;
  for (size_t i = 0; i < kImagePixels; ++i) out[i] = std::clamp((signal[i] - min) * scale, 0.0f, 1.0f);
}

}  // namespace tv
