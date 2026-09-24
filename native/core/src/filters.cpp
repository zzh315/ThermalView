#include "tv/filters.h"

#include <algorithm>
#include <cmath>

namespace tv {
namespace {

constexpr int W = kFrameWidth, H = kImageRows;

// Box means along rows, then columns, with the window shrunk at the borders. Each pass handles two
// images at once (the guided filter always needs pairs), multiplies by precomputed reciprocals of
// the window sizes and keeps its branches out of the inner loops. b may be null: then the second
// output is the box mean of a squared.
void boxPairRows(const float* a, const float* b, float* outA, float* outB, int r) {
  float inv[W];
  for (int x = 0; x < W; ++x) inv[x] = 1.0f / float(std::min(W - 1, x + r) - std::max(0, x - r) + 1);
  for (int y = 0; y < H; ++y) {
    const float* ia = a + size_t(y) * W;
    const float* ib = b ? b + size_t(y) * W : nullptr;
    float* oa = outA + size_t(y) * W;
    float* ob = outB + size_t(y) * W;
    auto vb = [&](int k) { return ib ? ib[k] : ia[k] * ia[k]; };
    float sa = 0, sb = 0;
    for (int k = 0; k <= std::min(r, W - 1); ++k) {  // the window of x = 0
      sa += ia[k];
      sb += vb(k);
    }
    oa[0] = sa * inv[0];
    ob[0] = sb * inv[0];
    int x = 1;
    for (; x <= r && x + r < W; ++x) {  // growing: only the right end comes in
      sa += ia[x + r];
      sb += vb(x + r);
      oa[x] = sa * inv[x];
      ob[x] = sb * inv[x];
    }
    for (; x + r < W; ++x) {  // sliding
      sa += ia[x + r] - ia[x - r - 1];
      sb += vb(x + r) - vb(x - r - 1);
      oa[x] = sa * inv[x];
      ob[x] = sb * inv[x];
    }
    for (; x < W; ++x) {  // shrinking: only the left end goes out
      if (x - r - 1 >= 0) {
        sa -= ia[x - r - 1];
        sb -= vb(x - r - 1);
      }
      oa[x] = sa * inv[x];
      ob[x] = sb * inv[x];
    }
  }
}

void boxPairCols(const float* a, const float* b, float* outA, float* outB, int r) {
  float sa[W], sb[W];
  std::fill(sa, sa + W, 0.0f);
  std::fill(sb, sb + W, 0.0f);
  int lo = 0, hi = -1;
  for (int y = 0; y < H; ++y) {
    while (hi < std::min(H - 1, y + r)) {
      ++hi;
      const float* pa = a + size_t(hi) * W;
      const float* pb = b + size_t(hi) * W;
      for (int x = 0; x < W; ++x) {
        sa[x] += pa[x];
        sb[x] += pb[x];
      }
    }
    while (lo < y - r) {
      const float* pa = a + size_t(lo) * W;
      const float* pb = b + size_t(lo) * W;
      for (int x = 0; x < W; ++x) {
        sa[x] -= pa[x];
        sb[x] -= pb[x];
      }
      ++lo;
    }
    const float inv = 1.0f / float(hi - lo + 1);
    float* oa = outA + size_t(y) * W;
    float* ob = outB + size_t(y) * W;
    for (int x = 0; x < W; ++x) {
      oa[x] = sa[x] * inv;
      ob[x] = sb[x] * inv;
    }
  }
}

}  // namespace

void boxFilter(const float* src, float* dst, int r) {
  std::vector<float> tmp(2 * kImagePixels), unused(kImagePixels);
  boxPairRows(src, src, tmp.data(), tmp.data() + kImagePixels, r);
  boxPairCols(tmp.data(), tmp.data(), dst, unused.data(), r);
}

void guidedFilterSelf(const float* src, float* dst, int r, float eps, std::vector<float>& scratch) {
  const size_t n = kImagePixels;
  scratch.resize(6 * n);
  float* meanI = scratch.data();
  float* meanII = meanI + n;
  float* a = meanII + n;
  float* b = a + n;
  float* t1 = b + n;
  float* t2 = t1 + n;
  // The mean and the mean of squares, in one pair of passes.
  boxPairRows(src, nullptr, t1, t2, r);
  boxPairCols(t1, t2, meanI, meanII, r);
  for (size_t i = 0; i < n; ++i) {
    const float var = std::max(meanII[i] - meanI[i] * meanI[i], 0.0f);
    a[i] = var / (var + eps);
    b[i] = meanI[i] * (1.0f - a[i]);
  }
  // Average the coefficients (another pair), then apply.
  boxPairRows(a, b, t1, t2, r);
  boxPairCols(t1, t2, meanI, meanII, r);  // mean of a, mean of b
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

void localRangeHalf(const float* src, float* dst, int r, std::vector<float>& scratch) {
  constexpr int w = W / 2, h = H / 2;
  constexpr size_t n = size_t(w) * h;
  const int rr = std::clamp((r + 1) / 2, 0, h / 2 - 1);
  scratch.resize(6 * n);
  float* mx = scratch.data();  // pooled
  float* mn = mx + n;
  float* ax = mn + n;  // along rows
  float* an = ax + n;
  float* bx = an + n;  // then columns
  float* bn = bx + n;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float* p = src + size_t(2 * y) * W + size_t(2 * x);
      const float a = p[0], b = p[1], c = p[W], d = p[W + 1];
      mx[size_t(y) * w + x] = std::max(std::max(a, b), std::max(c, d));
      mn[size_t(y) * w + x] = std::min(std::min(a, b), std::min(c, d));
    }
  for (int y = 0; y < h; ++y) {
    const float* ix = mx + size_t(y) * w;
    const float* in = mn + size_t(y) * w;
    float* ox = ax + size_t(y) * w;
    float* on = an + size_t(y) * w;
    std::copy(ix, ix + w, ox);
    std::copy(in, in + w, on);
    for (int k = 1; k <= rr; ++k) {
      for (int x = 0; x < w - k; ++x) {
        ox[x] = std::max(ox[x], ix[x + k]);
        on[x] = std::min(on[x], in[x + k]);
      }
      for (int x = k; x < w; ++x) {
        ox[x] = std::max(ox[x], ix[x - k]);
        on[x] = std::min(on[x], in[x - k]);
      }
    }
  }
  for (int y = 0; y < h; ++y) {
    float* ox = bx + size_t(y) * w;
    float* on = bn + size_t(y) * w;
    std::copy(ax + size_t(y) * w, ax + size_t(y + 1) * w, ox);
    std::copy(an + size_t(y) * w, an + size_t(y + 1) * w, on);
    for (int k = std::max(0, y - rr); k <= std::min(h - 1, y + rr); ++k) {
      const float* px = ax + size_t(k) * w;
      const float* pn = an + size_t(k) * w;
      for (int x = 0; x < w; ++x) {
        ox[x] = std::max(ox[x], px[x]);
        on[x] = std::min(on[x], pn[x]);
      }
    }
  }
  for (int y = 0; y < H; ++y) {
    const float* ox = bx + size_t(y / 2) * w;
    const float* on = bn + size_t(y / 2) * w;
    float* out = dst + size_t(y) * W;
    for (int x = 0; x < W; ++x) out[x] = ox[x / 2] - on[x / 2];
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
