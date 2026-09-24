#include "tv/filters.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

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

// One image: the same passes as the pair above.
void boxRows1(const float* a, float* out, int r) {
  float inv[W];
  for (int x = 0; x < W; ++x) inv[x] = 1.0f / float(std::min(W - 1, x + r) - std::max(0, x - r) + 1);
  for (int y = 0; y < H; ++y) {
    const float* ia = a + size_t(y) * W;
    float* oa = out + size_t(y) * W;
    float sa = 0;
    for (int k = 0; k <= std::min(r, W - 1); ++k) sa += ia[k];
    oa[0] = sa * inv[0];
    int x = 1;
    for (; x <= r && x + r < W; ++x) {
      sa += ia[x + r];
      oa[x] = sa * inv[x];
    }
    for (; x + r < W; ++x) {
      sa += ia[x + r] - ia[x - r - 1];
      oa[x] = sa * inv[x];
    }
    for (; x < W; ++x) {
      if (x - r - 1 >= 0) sa -= ia[x - r - 1];
      oa[x] = sa * inv[x];
    }
  }
}

void boxCols1(const float* a, float* out, int r) {
  float sa[W];
  std::fill(sa, sa + W, 0.0f);
  int lo = 0, hi = -1;
  for (int y = 0; y < H; ++y) {
    while (hi < std::min(H - 1, y + r)) {
      const float* pa = a + size_t(++hi) * W;
      for (int x = 0; x < W; ++x) sa[x] += pa[x];
    }
    while (lo < y - r) {
      const float* pa = a + size_t(lo++) * W;
      for (int x = 0; x < W; ++x) sa[x] -= pa[x];
    }
    const float inv = 1.0f / float(hi - lo + 1);
    float* oa = out + size_t(y) * W;
    for (int x = 0; x < W; ++x) oa[x] = sa[x] * inv;
  }
}

}  // namespace

void boxFilter(const float* src, float* dst, int r) {
  std::vector<float> tmp(kImagePixels);
  boxRows1(src, tmp.data(), r);
  boxCols1(tmp.data(), dst, r);
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

void nonLocalMeans(const float* src, float* dst, int searchRadius, int patchRadius, float h, std::vector<float>& scratch) {
  const int sr = std::clamp(searchRadius, 1, 7), pr = std::clamp(patchRadius, 0, 3);
  // The source, reflected into a margin wide enough for every offset and patch.
  const int m = sr + pr + sr;       // a pair's weight is also needed at p - d for p inside
  const int pw = W + 2 * m, ph = H + 2 * m;
  // The grid the weights are computed on: every p with p or p + d inside, so (W + 2sr) x (H + 2sr).
  const int gw = W + 2 * sr, gh = H + 2 * sr;
  const int dw = gw + 2 * pr, dh = gh + 2 * pr;  // the grid plus the patch margin
  const size_t pn = size_t(pw) * ph, gn = size_t(gw) * gh, dn = size_t(dw) * dh;
  scratch.resize(pn + dn + size_t(gw) * dh + gn + 2 * kImagePixels + size_t(gw));
  float* xp = scratch.data();
  float* dist = xp + pn;               // |x(p) - x(p + d)| over the grid plus the patch margin
  float* rows = dist + dn;             // its row sums, dh rows of gw
  float* w = rows + size_t(gw) * dh;   // the pair weights on the grid
  float* num = w + gn;
  float* den = num + kImagePixels;
  float* colSum = den + kImagePixels;
  auto reflect = [](int k, int n) {
    while (k < 0 || k >= n) k = k < 0 ? -k : 2 * (n - 1) - k;
    return k;
  };
  for (int y = 0; y < ph; ++y) {
    const float* in = src + size_t(reflect(y - m, H)) * W;
    float* out = xp + size_t(y) * pw;
    for (int x = 0; x < pw; ++x) out[x] = in[reflect(x - m, W)];
  }
  // exp(-t^2) on [0, 4] (beyond: 0), linear between entries.
  constexpr int kLut = 1024;
  constexpr float kLutMax = 4.0f;
  static const std::vector<float> lut = [] {
    std::vector<float> v(kLut + 2);
    for (int i = 0; i <= kLut + 1; ++i) {
      const float t = kLutMax * float(i) / float(kLut);
      v[size_t(i)] = std::exp(-t * t);
    }
    v[kLut + 1] = v[kLut] = 0.0f;
    return v;
  }();
  const float patchArea = float((2 * pr + 1) * (2 * pr + 1));
  const float toLut = float(kLut) / (kLutMax * std::max(h, 1e-6f) * patchArea);  // patch sum -> LUT position
  for (size_t i = 0; i < kImagePixels; ++i) {
    num[i] = src[i];  // the pixel itself, at weight 1
    den[i] = 1.0f;
  }
  // Grid point (0, 0) is image pixel (-sr, -sr); the distance grid starts pr further out, at padded
  // coordinate o0 = m - sr - pr.
  const int o0 = m - sr - pr;
  for (int dy = 0; dy <= sr; ++dy)
    for (int dx = -sr; dx <= sr; ++dx) {
      if (dy == 0 && dx <= 0) continue;  // one of each +- pair
      // |x(p) - x(p + d)| on the distance grid
      for (int y = 0; y < dh; ++y) {
        const float* a = xp + size_t(o0 + y) * pw + o0;
        const float* b = xp + size_t(o0 + y + dy) * pw + o0 + dx;
        float* out = dist + size_t(y) * dw;
        for (int x = 0; x < dw; ++x) out[x] = std::fabs(a[x] - b[x]);
      }
      // patch sums: rows (a sliding window of 2pr + 1), then columns
      for (int y = 0; y < dh; ++y) {
        const float* in = dist + size_t(y) * dw;
        float* out = rows + size_t(y) * gw;
        float s = 0;
        for (int k = 0; k < 2 * pr + 1; ++k) s += in[k];
        out[0] = s;
        for (int x = 1; x < gw; ++x) {
          s += in[x + 2 * pr] - in[x - 1];
          out[x] = s;
        }
      }
      std::fill(colSum, colSum + gw, 0.0f);
      for (int k = 0; k < 2 * pr + 1; ++k) {
        const float* r = rows + size_t(k) * gw;
        for (int x = 0; x < gw; ++x) colSum[x] += r[x];
      }
      for (int y = 0; y < gh; ++y) {
        if (y > 0) {
          const float* add = rows + size_t(y + 2 * pr) * gw;
          const float* sub = rows + size_t(y - 1) * gw;
          for (int x = 0; x < gw; ++x) colSum[x] += add[x] - sub[x];
        }
        float* wr = w + size_t(y) * gw;
        for (int x = 0; x < gw; ++x) {
          const float t = std::min(colSum[x] * toLut, float(kLut));
          const int i = int(t);
          const float f = t - float(i);
          wr[x] = lut[size_t(i)] + f * (lut[size_t(i) + 1] - lut[size_t(i)]);
        }
      }
      // Pixel p gets x(p + d) at w(p); pixel q gets x(q - d) at w(q - d).
      for (int y = 0; y < H; ++y) {
        const float* wp = w + size_t(y + sr) * gw + sr;             // w(p), p = (x, y)
        const float* wq = w + size_t(y + sr - dy) * gw + sr - dx;   // w(q - d)
        const float* xf = xp + size_t(y + m + dy) * pw + m + dx;    // x(p + d)
        const float* xb = xp + size_t(y + m - dy) * pw + m - dx;    // x(q - d)
        float* nr = num + size_t(y) * W;
        float* dr = den + size_t(y) * W;
        for (int x = 0; x < W; ++x) {
          nr[x] += wp[x] * xf[x] + wq[x] * xb[x];
          dr[x] += wp[x] + wq[x];
        }
      }
    }
  for (size_t i = 0; i < kImagePixels; ++i) dst[i] = num[i] / den[i];
}

namespace {

// exp(-v) for v >= 0 (v clamped at 80): 2^-a with a = v log2(e), split into 2^-i (the exponent bits)
// and 2^-f (a degree-6 polynomial on [0, 1)). Plain arithmetic, so loops over it vectorize.
inline float expNeg(float v) {
  const float a = std::min(v, 80.0f) * 1.44269504f;
  const int i = int(a);
  const float f = a - float(i);
  // 2^-f = e^(-f ln 2), Taylor to degree 6 (error < 2e-6 on [0, 1))
  const float g = f * 0.693147181f;
  const float p = 1.0f - g * (1.0f - g * (0.5f - g * (0.166666667f - g * (0.0416666667f - g * (0.00833333333f - g * 0.00138888889f)))));
  uint32_t bits = uint32_t(127 - i) << 23;  // 2^-i, i < 127
  float scale;
  std::memcpy(&scale, &bits, sizeof scale);
  return p * scale;
}

}  // namespace

void nlmPad(const float* src, int searchRadius, int patchRadius, NlmPadded* padded) {
  const int sr = std::clamp(searchRadius, 1, 7), pr = std::clamp(patchRadius, 0, 3);
  const int m = 2 * sr + pr;
  padded->margin = m;
  padded->width = W + 2 * m;
  padded->height = H + 2 * m;
  padded->data.resize(size_t(padded->width) * size_t(padded->height));
  auto reflect = [](int k, int n) {
    while (k < 0 || k >= n) k = k < 0 ? -k : 2 * (n - 1) - k;
    return k;
  };
  for (int y = 0; y < padded->height; ++y) {
    const float* in = src + size_t(reflect(y - m, H)) * W;
    float* out = padded->data.data() + size_t(y) * size_t(padded->width);
    for (int x = 0; x < padded->width; ++x) out[x] = in[reflect(x - m, W)];
  }
}

void nlmBand(const NlmPadded& padded, float* dst, int y0, int y1, int searchRadius, int patchRadius, float h,
             std::vector<float>& scratch) {
  const int sr = std::clamp(searchRadius, 1, 7), pr = std::clamp(patchRadius, 0, 3);
  const int m = padded.margin, pw = padded.width;
  const float* xp = padded.data.data();
  y0 = std::clamp(y0, 0, H);
  y1 = std::clamp(y1, y0, H);
  const int rows = y1 - y0;
  if (rows == 0) return;
  // Grid column g covers image column g - sr (so p and p + d are both reachable); a patch row is the
  // distance row's horizontal sum over 2pr + 1 columns.
  const int gw = W + 2 * sr, dw = gw + 2 * pr, taps = 2 * pr + 1;
  scratch.resize(2 * size_t(rows) * W + size_t(taps) * gw + 3 * size_t(dw));
  float* num = scratch.data();
  float* den = num + size_t(rows) * W;
  float* ring = den + size_t(rows) * W;  // the last 2pr + 1 patch rows
  float* dist = ring + size_t(taps) * gw;
  float* colSum = dist + dw;
  float* wrow = colSum + dw;
  for (int y = 0; y < rows; ++y) {
    const float* in = xp + size_t(y0 + y + m) * pw + m;
    std::copy(in, in + W, num + size_t(y) * W);  // the pixel itself, at weight 1
    std::fill(den + size_t(y) * W, den + size_t(y + 1) * W, 1.0f);
  }
  const float patchArea = float(taps * taps);
  const float invH = 1.0f / (std::max(h, 1e-6f) * patchArea);  // patch sum -> mean |difference| / h
  const int o0 = m - sr - pr;  // padded coordinate of distance-grid (0, 0), image pixel (-sr - pr, -sr - pr)
  // Distance-grid row r's horizontal patch sums, into out (gw entries).
  auto patchRow = [&](int r, int dx, int dy, float* out) {
    const float* a = xp + size_t(o0 + r) * pw + o0;
    const float* b = xp + size_t(o0 + r + dy) * pw + o0 + dx;
    for (int x = 0; x < dw; ++x) dist[x] = std::fabs(a[x] - b[x]);
    std::copy(dist, dist + gw, out);
    for (int k = 1; k < taps; ++k) {  // shifted whole-row adds: these vectorize, a per-pixel tap loop doesn't
      const float* d = dist + k;
      for (int x = 0; x < gw; ++x) out[x] += d[x];
    }
  };
  for (int dy = 0; dy <= sr; ++dy)
    for (int dx = -sr; dx <= sr; ++dx) {
      if (dy == 0 && dx <= 0) continue;  // one of each +- pair
      // Grid rows (image row + sr) whose weights the band needs: its own rows (p) and those d above
      // them (p = q - d for its q).
      const int g0 = y0 + sr - dy, g1 = y1 + sr;
      for (int k = 0; k < taps; ++k) patchRow(g0 + k, dx, dy, ring + size_t(k) * gw);
      std::fill(colSum, colSum + gw, 0.0f);
      for (int k = 0; k < taps; ++k) {
        const float* r = ring + size_t(k) * gw;
        for (int x = 0; x < gw; ++x) colSum[x] += r[x];
      }
      for (int g = g0; g < g1; ++g) {
        if (g > g0) {  // slide: the row leaving goes out, the next one comes in (it takes its slot)
          float* slot = ring + size_t((g - g0 - 1) % taps) * gw;
          for (int x = 0; x < gw; ++x) colSum[x] -= slot[x];
          patchRow(g + 2 * pr, dx, dy, slot);
          for (int x = 0; x < gw; ++x) colSum[x] += slot[x];
        }
        for (int x = 0; x < gw; ++x) {
          const float u = colSum[x] * invH;
          wrow[x] = expNeg(u * u);
        }
        // p in image row yp = g - sr gets x(p + d); q in row yq = yp + dy gets x(q - d) = x(p).
        const int yp = g - sr, yq = yp + dy;
        if (yp >= y0 && yp < y1) {
          const float* wp = wrow + sr;
          const float* xf = xp + size_t(yp + m + dy) * pw + m + dx;
          float* nr = num + size_t(yp - y0) * W;
          float* dr = den + size_t(yp - y0) * W;
          for (int x = 0; x < W; ++x) {
            nr[x] += wp[x] * xf[x];
            dr[x] += wp[x];
          }
        }
        if (yq >= y0 && yq < y1) {
          const float* wq = wrow + sr - dx;  // w at q - d
          const float* xb = xp + size_t(yp + m) * pw + m - dx;
          float* nr = num + size_t(yq - y0) * W;
          float* dr = den + size_t(yq - y0) * W;
          for (int x = 0; x < W; ++x) {
            nr[x] += wq[x] * xb[x];
            dr[x] += wq[x];
          }
        }
      }
    }
  for (int y = 0; y < rows; ++y)
    for (int x = 0; x < W; ++x) dst[size_t(y0 + y) * W + x] = num[size_t(y) * W + x] / den[size_t(y) * W + x];
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
