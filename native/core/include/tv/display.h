// CPU reference of the display path (docs/PLAN.md M3-M5): what the renderer draws, so the Mac
// harness can measure it on recorded dumps. Output is float intensity in [0, 1] per camera pixel.
#pragma once

#include <cstdint>

#include "tv/frame.h"

namespace tv {

// M1 baseline: per-frame min/max linear stretch of the raw image to gray, exactly like
// renderer.cpp's shader: (v - min) / (max - min), clamped, black when the frame is flat.
// out holds kImagePixels values.
void renderBaseline(const uint16_t* image, float* out);

// The same stretch of a float signal (the pipeline's, after stages that change values).
void renderBaseline(const float* signal, float* out);

}  // namespace tv
