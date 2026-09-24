// Display upscaling (docs/PLAN.md M5): the scalar intensity is interpolated to screen pixels, then
// the palette applies. This is the CPU reference of the GLES shaders, so the harness renders what
// the tablet will show at any on-screen size and zoom.
#pragma once

#include <string>
#include <vector>

#include "tv/frame.h"

namespace tv {

enum class Kernel {
  Nearest,
  Bilinear,
  CatmullRom,       // Keys cubic, a = -0.5 (4x4 taps)
  Lanczos3,         // windowed sinc (6x6 taps), weights normalized
  CardinalBSpline,  // cubic B-spline on prefiltered coefficients (Unser 1999): interpolating, C2
};

bool parseKernel(const std::string& name, Kernel* kernel);  // "nearest", "bilinear", "catmullrom", ...
const char* kernelName(Kernel kernel);

// The frame's visible part, in camera pixels (the whole frame at 1x zoom).
struct ViewRect {
  float x = 0.0f, y = 0.0f, w = float(kFrameWidth), h = float(kImageRows);
};

// Scalar coefficients the kernel samples: the image itself, or for the cardinal B-spline, its
// prefiltered coefficients (a recursive filter along rows and columns, mirror boundaries).
std::vector<float> kernelInput(const float* image, Kernel kernel);

// Renders input (kernelInput of a kFrameWidth x kImageRows image) at dstW x dstH output pixels
// covering rect. Output pixel (i, j) samples camera coordinate rect.x + (i + 0.5) * rect.w / dstW
// - 0.5 (pixel centers at integers), borders clamped. With clamp, each output is limited to the
// min/max of the 2x2 image pixels around it (image: the unfiltered frame), so no kernel rings
// beyond its neighbors.
void upscale(const std::vector<float>& input, const float* image, Kernel kernel, bool clamp, const ViewRect& rect,
             int dstW, int dstH, float* dst);

}  // namespace tv
