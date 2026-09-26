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
  Easu,             // experimental: AMD FSR 1's edge-adaptive upsampling (MIT), a 12-tap Lanczos-2-like
                    // kernel stretched along the local edge, clamped to the 4 nearest pixels
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

// The cardinal B-spline's coefficients of a kFrameWidth x kImageRows image, into coeffs (same
// size; may not alias image): what the GPU's B-spline samples. Float, whole rows at a time;
// bsplineCoefficientsReference is the same filter line by line in double (tests).
void bsplineCoefficients(const float* image, float* coeffs);
void bsplineCoefficientsReference(const float* image, float* coeffs);

// Renders input (kernelInput of a kFrameWidth x kImageRows image) at dstW x dstH output pixels
// covering rect. Output pixel (i, j) samples camera coordinate rect.x + (i + 0.5) * rect.w / dstW
// - 0.5 (pixel centers at integers), borders clamped. With clamp, each output is limited to the
// min/max of the 2x2 image pixels around it (image: the unfiltered frame), so no kernel rings
// beyond its neighbors.
void upscale(const std::vector<float>& input, const float* image, Kernel kernel, bool clamp, const ViewRect& rect,
             int dstW, int dstH, float* dst);

// M7's edge sharpening, on the upscaled image (contour shaping). Where the image has an edge, each
// upscaled value is pushed away from the middle of its neighbourhood's range, toward the edge's two
// sides: the edge steepens along the upscaled image's own smooth contours, so a slanted edge gets no
// stair-steps (sharpening at camera resolution loses where an edge sits within its pixels), and no
// value passes its neighbourhood's min or max (no halo). The gate opens where the 3x3 range is most
// of the 7x7 one (a step, not a steady ramp) and several times the frame's noise floor (the 20th
// percentile of the 3x3 range), so neither gradients nor grain are touched.
struct ContourFields {
  std::vector<float> values;  // per camera pixel: the 3x3 min and max, the 7x7 min and max
  float floor = 0.0f;         // the frame's noise floor, in the image's units
};
void contourFields(const float* image, ContourFields* fields);  // image: kFrameWidth x kImageRows

// One output's value v at camera coordinate (cx, cy) (pixel centers at integers), shaped at strength
// k: the renderer's fragment shader does the same, with the fields sampled bilinearly.
float shapeContour(const ContourFields& fields, float k, float cx, float cy, float v);

// shapeContour over an upscale's output (its pixel (i, j) at upscale's camera coordinate).
void sharpenContours(const ContourFields& fields, float k, const ViewRect& rect, int dstW, int dstH, float* dst);

}  // namespace tv
