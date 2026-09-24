#include "tv/filters.h"

#include <algorithm>
#include <cmath>

namespace tv {
namespace {

constexpr int W = kFrameWidth, H = kImageRows;

// Running-sum box along rows, then columns (both shrink the window at the borders).
void boxRows(const float* src, float* dst, int r) {
  for (int y = 0; y < H; ++y) {
    const float* in = src + size_t(y) * W;
    float* out = dst + size_t(y) * W;
    float sum = 0;
    int lo = 0, hi = -1;
    for (int x = 0; x < W; ++x) {
      while (hi < std::min(W - 1, x + r)) sum += in[++hi];
      while (lo < x - r) sum -= in[lo++];
      out[x] = sum / float(hi - lo + 1);
    }
  }
}

void boxCols(const float* src, float* dst, int r) {
  std::vector<float> sum(size_t(W), 0.0f);
  int lo = 0, hi = -1;
  for (int y = 0; y < H; ++y) {
    while (hi < std::min(H - 1, y + r)) {
      const float* add = src + size_t(++hi) * W;
      for (int x = 0; x < W; ++x) sum[size_t(x)] += add[x];
    }
    while (lo < y - r) {
      const float* sub = src + size_t(lo++) * W;
      for (int x = 0; x < W; ++x) sum[size_t(x)] -= sub[x];
    }
    const float n = float(hi - lo + 1);
    float* out = dst + size_t(y) * W;
    for (int x = 0; x < W; ++x) out[x] = sum[size_t(x)] / n;
  }
}

}  // namespace

void boxFilter(const float* src, float* dst, int r) {
  std::vector<float> tmp(kImagePixels);
  boxRows(src, tmp.data(), r);
  boxCols(tmp.data(), dst, r);
}

void guidedFilterSelf(const float* src, float* dst, int r, float eps, std::vector<float>& scratch) {
  const size_t n = kImagePixels;
  scratch.resize(5 * n);
  float* meanI = scratch.data();
  float* meanII = meanI + n;
  float* a = meanII + n;
  float* b = a + n;
  float* tmp = b + n;
  // mean and mean of squares
  boxRows(src, tmp, r);
  boxCols(tmp, meanI, r);
  for (size_t i = 0; i < n; ++i) a[i] = src[i] * src[i];
  boxRows(a, tmp, r);
  boxCols(tmp, meanII, r);
  for (size_t i = 0; i < n; ++i) {
    const float var = std::max(meanII[i] - meanI[i] * meanI[i], 0.0f);
    a[i] = var / (var + eps);
    b[i] = meanI[i] * (1.0f - a[i]);
  }
  // average the coefficients, then apply
  boxRows(a, tmp, r);
  boxCols(tmp, meanI, r);  // mean of a
  boxRows(b, tmp, r);
  boxCols(tmp, meanII, r);  // mean of b
  for (size_t i = 0; i < n; ++i) dst[i] = meanI[i] * src[i] + meanII[i];
}

void localRange(const float* src, float* dst, int r, std::vector<float>& scratch) {
  // Row pass, then column pass, each as whole-row loops the compiler vectorizes; only the r pixels at
  // each end of a row clamp their index.
  r = std::clamp(r, 0, std::min(W, H) / 2 - 1);
  scratch.resize(2 * kImagePixels + 2 * size_t(W));
  float* mx = scratch.data();
  float* mn = mx + kImagePixels;
  float* hi = mn + kImagePixels;
  float* lo = hi + W;
  for (int y = 0; y < H; ++y) {
    const float* in = src + size_t(y) * W;
    float* a = mx + size_t(y) * W;
    float* b = mn + size_t(y) * W;
    std::copy(in, in + W, a);
    std::copy(in, in + W, b);
    for (int k = 1; k <= r; ++k) {
      for (int x = 0; x < W - k; ++x) {  // right neighbours
        a[x] = std::max(a[x], in[x + k]);
        b[x] = std::min(b[x], in[x + k]);
      }
      for (int x = k; x < W; ++x) {  // left neighbours
        a[x] = std::max(a[x], in[x - k]);
        b[x] = std::min(b[x], in[x - k]);
      }
    }
  }
  for (int y = 0; y < H; ++y) {
    std::copy(mx + size_t(y) * W, mx + size_t(y + 1) * W, hi);
    std::copy(mn + size_t(y) * W, mn + size_t(y + 1) * W, lo);
    for (int k = std::max(0, y - r); k <= std::min(H - 1, y + r); ++k) {
      const float* a = mx + size_t(k) * W;
      const float* b = mn + size_t(k) * W;
      for (int x = 0; x < W; ++x) {
        hi[x] = std::max(hi[x], a[x]);
        lo[x] = std::min(lo[x], b[x]);
      }
    }
    float* out = dst + size_t(y) * W;
    for (int x = 0; x < W; ++x) out[x] = hi[x] - lo[x];
  }
}

void gaussianBlur(const float* src, float* dst, float sigma, std::vector<float>& scratch) {
  const int r = std::clamp(int(std::ceil(3.0f * sigma)), 1, 7);  // 15 taps at most
  float k[15];
  float total = 0;
  for (int i = -r; i <= r; ++i) total += k[i + r] = std::exp(-0.5f * float(i * i) / (sigma * sigma));
  for (int i = 0; i <= 2 * r; ++i) k[i] /= total;
  scratch.resize(kImagePixels);
  float* tmp = scratch.data();
  // Rows: the interior as whole-row multiply-adds, the r pixels at each end with clamped indices.
  for (int y = 0; y < H; ++y) {
    const float* in = src + size_t(y) * W;
    float* out = tmp + size_t(y) * W;
    for (int x = r; x < W - r; ++x) out[x] = k[r] * in[x];
    for (int i = 1; i <= r; ++i) {
      const float w = k[r + i];
      for (int x = r; x < W - r; ++x) out[x] += w * (in[x - i] + in[x + i]);
    }
    for (int x : {0, 1, 2, 3, 4, 5, 6}) {
      if (x >= r) break;
      for (int e : {x, W - 1 - x}) {
        float s = 0;
        for (int i = -r; i <= r; ++i) s += k[i + r] * in[std::clamp(e + i, 0, W - 1)];
        out[e] = s;
      }
    }
  }
  // Columns: whole rows at a time, clamping only the row index.
  for (int y = 0; y < H; ++y) {
    float* out = dst + size_t(y) * W;
    const float* c = tmp + size_t(y) * W;
    for (int x = 0; x < W; ++x) out[x] = k[r] * c[x];
    for (int i = 1; i <= r; ++i) {
      const float w = k[r + i];
      const float* up = tmp + size_t(std::max(y - i, 0)) * W;
      const float* down = tmp + size_t(std::min(y + i, H - 1)) * W;
      for (int x = 0; x < W; ++x) out[x] += w * (up[x] + down[x]);
    }
  }
}

void localMinMax3(const float* src, float* lo, float* hi, std::vector<float>& scratch) {
  scratch.resize(2 * kImagePixels);
  float* a = scratch.data();  // row max
  float* b = a + kImagePixels;  // row min
  for (int y = 0; y < H; ++y) {
    const float* in = src + size_t(y) * W;
    float* ra = a + size_t(y) * W;
    float* rb = b + size_t(y) * W;
    ra[0] = std::max(in[0], in[1]);
    rb[0] = std::min(in[0], in[1]);
    for (int x = 1; x < W - 1; ++x) {
      ra[x] = std::max(std::max(in[x - 1], in[x]), in[x + 1]);
      rb[x] = std::min(std::min(in[x - 1], in[x]), in[x + 1]);
    }
    ra[W - 1] = std::max(in[W - 2], in[W - 1]);
    rb[W - 1] = std::min(in[W - 2], in[W - 1]);
  }
  for (int y = 0; y < H; ++y) {
    const size_t u = size_t(std::max(y - 1, 0)) * W, m = size_t(y) * W, d = size_t(std::min(y + 1, H - 1)) * W;
    for (int x = 0; x < W; ++x) {
      hi[m + x] = std::max(std::max(a[u + x], a[m + x]), a[d + x]);
      lo[m + x] = std::min(std::min(b[u + x], b[m + x]), b[d + x]);
    }
  }
}

}  // namespace tv
