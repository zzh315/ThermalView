// BM3D, block-matching and 3D filtering (Dabov, Foi, Katkovnik and Egiazarian, "Image denoising by
// sparse 3D transform-domain collaborative filtering", IEEE TIP 2007), as Lebrun specifies it
// ("An analysis and implementation of the BM3D image denoising method", IPOL 2012): stage 4b's
// candidate for keeping more subtle texture than non-local means at the same noise reduction
// (owner, 2026-09-25; PIPELINE_LOG). This is the CPU reference, for the harness, the tests and the
// GPU version's check; every parameter is open for the real-time sweep.
//
// Step 1 (hard thresholding): for each reference block (block x block pixels, every stride pixels,
// the image's last row and column of blocks included), the most similar blocks within the search
// window (mean squared difference per pixel at most tau1 sigma^2, the best group1 at most, a power
// of two) are stacked into a group; its 3D transform (each block's orthonormal 2D DCT, then a
// normalized Walsh-Hadamard transform along the stack) is hard-thresholded at lambda sigma and
// inverted. Each block's estimate is aggregated, weighted by 1 / (the coefficients kept) and a Kaiser
// window, into the basic estimate. Step 2 (Wiener): the matching is redone on the basic estimate
// (tau2 sigma^2, group2); each group of noisy blocks is shrunk by the empirical Wiener factors
// B^2 / (B^2 + mu2 sigma^2) of the basic estimate's group B, and aggregated weighted by 1 / sum of
// the factors squared. lambda and mu2 default to the values Makinen, Azzari and Foi optimized for
// white noise ("Collaborative filtering of correlated noise", IEEE TIP 2020).
#pragma once

#include <vector>

namespace tv {

struct Bm3dOptions {
  int block = 8;        // block size, pixels
  int stride = 3;       // between reference blocks
  int search = 19;      // search radius, block positions: a (2 search + 1)^2 window
  int group1 = 16;      // step 1's largest group (rounded down to a power of two)
  int group2 = 32;      // step 2's
  float lambda = 3.0f;  // step 1's hard threshold, x sigma (Makinen, Azzari and Foi, IEEE TIP 2020: 3.0 for
                        // white noise; the 2007 paper's 2.7)
  float mu2 = 0.4f;     // step 2's Wiener factors B^2 / (B^2 + mu2 sigma^2) (the same: 0.4; 2007: 1)
  float tau1 = 4.0f;    // step 1's match threshold, x sigma^2 (IPOL: 2500 at sigma 25); <= 0: none
  float tau2 = 0.64f;   // step 2's, on the basic estimate (IPOL: 400 at sigma 25); <= 0: none
  bool wiener = true;   // step 2 on; off: the basic estimate is the result
  float kaiser = 2.0f;  // the aggregation window's beta
  // Every block of a group adds its estimate to the result (BM3D); off: only the group's reference
  // block does, the form a GPU can aggregate without float atomics (denser references make up for it).
  bool aggregateAll = true;
};

// src -> dst, kFrameWidth x kImageRows; sigma: the noise's std, in src's units (sigma <= 0: a copy).
void bm3d(const float* src, float* dst, float sigma, const Bm3dOptions& options);

}  // namespace tv
