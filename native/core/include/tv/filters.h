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

// localRange for wide windows at a quarter of the cost: 2x2 max/min pooling, the range over radius
// ceil(r/2) on the pooled image, each pixel then taking its 2x2 cell's value. Its window contains the
// full-resolution one (a pixel more at most), so a guard built on it errs on the side of caution.
void localRangeHalf(const float* src, float* dst, int r, std::vector<float>& scratch);

// Non-local means (Buades, Coll and Morel 2005), in the form OpenCV uses for 16-bit data: each pixel
// becomes the weighted mean of the pixels within searchRadius, each weighted by how alike the
// (2 patchRadius + 1)^2 patches around the two are: w = exp(-(mean |difference| / h)^2), the pixel
// itself at weight 1. Every offset pair is evaluated once for both of its pixels; borders reflect.
// h is in the image's units (a multiple of the noise sigma). dst may not alias src.
void nonLocalMeans(const float* src, float* dst, int searchRadius, int patchRadius, float h, std::vector<float>& scratch);

// The same filter, fast: the source padded once (nlmPad), then any horizontal band of output rows
// [y0, y1) computed on its own (nlmBand), each offset pair in one pass over the band's rows with a
// few rows of state, the weight exp() a vectorizable polynomial (relative error ~1e-6). Bands are
// independent, so they can run on separate threads (scratch per band). nonLocalMeans stays as the
// reference (tests hold the two within 1e-4).
struct NlmPadded {
  std::vector<float> data;
  int margin = 0, width = 0, height = 0;
};
void nlmPad(const float* src, int searchRadius, int patchRadius, NlmPadded* padded);
void nlmBand(const NlmPadded& padded, float* dst, int y0, int y1, int searchRadius, int patchRadius, float h,
             std::vector<float>& scratch);

// Separable Gaussian blur with the given sigma (pixels; at most 7 pixels of radius), borders
// clamped. dst may not alias src.
void gaussianBlur(const float* src, float* dst, float sigma, std::vector<float>& scratch);

// Each pixel's 3x3 minimum and maximum (borders clamped). lo and hi may not alias src.
void localMinMax3(const float* src, float* lo, float* hi, std::vector<float>& scratch);

}  // namespace tv
