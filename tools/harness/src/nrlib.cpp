// A C interface to native/core's noise filters for the Python analysis tools (ctypes):
// tools/py/nr_detail.py measures them on the known-texture test and the benchmark scenes.
#include "tv/bm3d.h"
#include "tv/filters.h"
#include "tv/frame.h"
#include "tv/stripes.h"

extern "C" {

// BM3D (tv/bm3d.h): src -> dst, kImagePixels floats each.
void tv_bm3d(const float* src, float* dst, float sigma, int block, int stride, int search, int group1, int group2,
             float lambda, float tau1, float tau2, int wiener, float kaiser, int aggregateAll, float mu2,
             int skipSameColumn, int skipSameRow) {
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
  o.skipSameColumn = skipSameColumn != 0;
  o.skipSameRow = skipSameRow != 0;
  tv::bm3d(src, dst, sigma, o);
}

// BM3D for correlated noise: the covariance values (2 radius + 1)^2, scaled by scale.
void tv_bm3d_cov(const float* src, float* dst, const float* cov, int radius, float scale, int block, int stride,
                 int search, int group1, int group2, float lambda, float tau1, float tau2, int wiener, float kaiser,
                 int aggregateAll, float mu2) {
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
  tv::NoiseCovariance noise;
  noise.radius = radius;
  noise.values.assign(cov, cov + size_t(2 * radius + 1) * size_t(2 * radius + 1));
  tv::bm3d(src, dst, noise, scale, o);
}

// Stage 3c over a sequence: frames (n x kImagePixels) corrected in place; per frame, the shift found
// and the fraction matched go into shifts (2 n) and matched (n).
void tv_stripes(float* frames, int n, float sigma, float tauFrames, float gate, float keep /* band */, float lost, float clamp,
                float highpass, float* shifts, float* matched) {
  tv::StripeOptions o;
  o.tauFrames = tauFrames;
  o.gate = gate;
  o.band = keep;
  o.lost = lost;
  o.clamp = clamp;
  o.highpass = highpass;
  tv::FrameStripes stripes(o);
  for (int t = 0; t < n; ++t) {
    stripes.process(frames + size_t(t) * tv::kImagePixels, sigma);
    stripes.updateReference();
    shifts[2 * t] = stripes.lastShift()[0];
    shifts[2 * t + 1] = stripes.lastShift()[1];
    matched[t] = stripes.lastMatched();
  }
}

// Stage 4b's shipped non-local means (tv/filters.h's fast version), h in src's units.
void tv_nlm(const float* src, float* dst, int searchRadius, int patchRadius, float h) {
  tv::NlmPadded padded;
  std::vector<float> scratch;
  tv::nlmPad(src, searchRadius, patchRadius, &padded);
  tv::nlmBand(padded, dst, 0, tv::kImageRows, searchRadius, patchRadius, h, scratch);
}

}
