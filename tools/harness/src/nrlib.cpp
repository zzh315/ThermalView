// A C interface to native/core's noise filters for the Python analysis tools (ctypes):
// tools/py/nr_detail.py measures them on the known-texture test and the benchmark scenes.
#include "tv/bm3d.h"
#include "tv/filters.h"
#include "tv/frame.h"

extern "C" {

// BM3D (tv/bm3d.h): src -> dst, kImagePixels floats each.
void tv_bm3d(const float* src, float* dst, float sigma, int block, int stride, int search, int group1, int group2,
             float lambda, float tau1, float tau2, int wiener, float kaiser, int aggregateAll, float mu2) {
  tv::Bm3dOptions o;
  o.block = block;
  o.stride = stride;
  o.search = search;
  o.group1 = group1;
  o.group2 = group2;
  o.lambda = lambda;
  o.tau1 = tau1;
  o.tau2 = tau2;
  o.wiener = wiener != 0;
  o.kaiser = kaiser;
  o.aggregateAll = aggregateAll != 0;
  o.mu2 = mu2;
  tv::bm3d(src, dst, sigma, o);
}

// Stage 4b's shipped non-local means (tv/filters.h's fast version), h in src's units.
void tv_nlm(const float* src, float* dst, int searchRadius, int patchRadius, float h) {
  tv::NlmPadded padded;
  std::vector<float> scratch;
  tv::nlmPad(src, searchRadius, patchRadius, &padded);
  tv::nlmBand(padded, dst, 0, tv::kImageRows, searchRadius, patchRadius, h, scratch);
}

}
