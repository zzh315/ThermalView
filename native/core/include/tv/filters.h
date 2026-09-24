// Small spatial filters on 256x192 float images (row-major, kFrameWidth x kImageRows), for M4 stage 6.
#pragma once

#include <vector>

#include "tv/frame.h"

namespace tv {

// Mean over the (2r+1)^2 window, with the window shrunk at the borders. dst may not alias src.
void boxFilter(const float* src, float* dst, int r);

// He, Sun and Tang's guided filter with the image as its own guide: an edge-preserving smoothing.
// Where local variance is well above eps the output follows the image (edges survive); where it's
// below, the output is the local mean. scratch is resized as needed and reused between calls.
void guidedFilterSelf(const float* src, float* dst, int r, float eps, std::vector<float>& scratch);

// Max minus min over the (2r+1)^2 window (shrunk at the borders): the size of any step within r
// pixels. dst may not alias src.
void localRange(const float* src, float* dst, int r, std::vector<float>& scratch);

// Separable Gaussian blur with the given sigma (pixels; at most 7 pixels of radius), borders
// clamped. dst may not alias src.
void gaussianBlur(const float* src, float* dst, float sigma, std::vector<float>& scratch);

// Each pixel's 3x3 minimum and maximum (borders clamped). lo and hi may not alias src.
void localMinMax3(const float* src, float* lo, float* hi, std::vector<float>& scratch);

}  // namespace tv
