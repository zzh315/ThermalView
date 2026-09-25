#include "tv/bm3d.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "tv/frame.h"

namespace tv {
namespace {

constexpr int W = kFrameWidth, H = kImageRows;

// The orthonormal DCT-II matrix, c[u * k + x].
std::vector<float> dctMatrix(int k) {
  std::vector<float> c(size_t(k) * k);
  for (int u = 0; u < k; ++u)
    for (int x = 0; x < k; ++x)
      c[size_t(u) * k + x] = float((u == 0 ? std::sqrt(1.0 / k) : std::sqrt(2.0 / k)) *
                                   std::cos(3.14159265358979323846 * (2.0 * x + 1.0) * u / (2.0 * k)));
  return c;
}

double besselI0(double x) {
  double sum = 1.0, term = 1.0;
  for (int m = 1; m < 50; ++m) {
    term *= (x / (2.0 * m)) * (x / (2.0 * m));
    sum += term;
    if (term < 1e-12 * sum) break;
  }
  return sum;
}

// The aggregation window: a separable Kaiser window, k x k.
std::vector<float> kaiserWindow(int k, float beta) {
  std::vector<double> w1(static_cast<size_t>(k));
  for (int i = 0; i < k; ++i) {
    const double r = k > 1 ? 2.0 * i / (k - 1) - 1.0 : 0.0;
    w1[size_t(i)] = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / besselI0(beta);
  }
  std::vector<float> w(size_t(k) * k);
  for (int i = 0; i < k; ++i)
    for (int j = 0; j < k; ++j) w[size_t(i) * k + j] = float(w1[size_t(i)] * w1[size_t(j)]);
  return w;
}

// Every block position's 2D DCT, [(y * nx + x) * k * k + v * k + u]: each row segment's horizontal
// transform once, then the vertical one per block.
void blockDcts(const float* img, int k, const std::vector<float>& c, std::vector<float>& out) {
  const int nx = W - k + 1, ny = H - k + 1;
  std::vector<float> horiz(size_t(H) * nx * k);  // [(y * nx + x) * k + u]
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < nx; ++x) {
      const float* row = img + size_t(y) * W + x;
      float* hz = horiz.data() + (size_t(y) * nx + x) * k;
      for (int u = 0; u < k; ++u) {
        float s = 0.0f;
        for (int j = 0; j < k; ++j) s += c[size_t(u) * k + j] * row[j];
        hz[u] = s;
      }
    }
  out.assign(size_t(nx) * ny * k * k, 0.0f);
  for (int y = 0; y < ny; ++y)
    for (int x = 0; x < nx; ++x) {
      float* blk = out.data() + (size_t(y) * nx + x) * k * k;
      for (int i = 0; i < k; ++i) {
        const float* hz = horiz.data() + (size_t(y + i) * nx + x) * k;
        for (int v = 0; v < k; ++v) {
          const float cv = c[size_t(v) * k + i];
          float* dst = blk + size_t(v) * k;
          for (int u = 0; u < k; ++u) dst[u] += cv * hz[u];
        }
      }
    }
}

// In place, the normalized Walsh-Hadamard transform of n (a power of two) values spaced stride apart:
// orthonormal and its own inverse.
void walshHadamard(float* v, int n, int stride) {
  for (int len = 1; len < n; len <<= 1)
    for (int i = 0; i < n; i += len << 1)
      for (int j = i; j < i + len; ++j) {
        const float a = v[size_t(j) * stride], b = v[size_t(j + len) * stride];
        v[size_t(j) * stride] = a + b;
        v[size_t(j + len) * stride] = a - b;
      }
  const float scale = 1.0f / std::sqrt(float(n));
  for (int j = 0; j < n; ++j) v[size_t(j) * stride] *= scale;
}

std::vector<int> referencePositions(int n, int stride) {
  std::vector<int> p;
  for (int i = 0; i < n; i += std::max(stride, 1)) p.push_back(i);
  if (p.back() != n - 1) p.push_back(n - 1);  // the last blocks too, so every pixel is covered
  return p;
}

// The reference block's group: itself, then the most similar blocks within the window (mean squared
// difference per pixel at most tau), up to maxGroup, rounded down to a power of two.
void matchBlocks(const float* img, int k, int rx, int ry, int search, float tau, int maxGroup, bool skipColumn,
                 bool skipRow, const float* bias, std::vector<std::pair<float, int>>& candidates,
                 std::vector<int>& group) {
  const int nx = W - k + 1, ny = H - k + 1;
  const float limit = tau > 0.0f ? tau * float(k * k) : 3.0e38f;  // as a sum over the block
  const int span = 2 * search + 1;
  candidates.clear();
  const float* ref = img + size_t(ry) * W + rx;
  for (int y = std::max(0, ry - search); y <= std::min(ny - 1, ry + search); ++y)
    for (int x = std::max(0, rx - search); x <= std::min(nx - 1, rx + search); ++x) {
      if ((x == rx && y == ry) || (skipColumn && x == rx) || (skipRow && y == ry)) continue;
      const float* cand = img + size_t(y) * W + x;
      // The noise's expected share of the distance at this displacement, taken off (correlated noise:
      // blocks sharing columns or rows share part of it, so they would look closer than they are).
      const float offset = bias ? bias[size_t(y - ry + search) * span + size_t(x - rx + search)] : 0.0f;
      float d = -offset;
      for (int i = 0; i < k && d <= limit; ++i) {
        const float* a = ref + size_t(i) * W;
        const float* b = cand + size_t(i) * W;
        for (int j = 0; j < k; ++j) {
          const float e = a[j] - b[j];
          d += e * e;
        }
      }
      if (d <= limit) candidates.emplace_back(d, y * nx + x);
    }
  const int want = std::min(int(candidates.size()) + 1, std::max(maxGroup, 1));
  int n = 1;
  while (n * 2 <= want) n *= 2;
  std::partial_sort(candidates.begin(), candidates.begin() + (n - 1), candidates.end());
  group.assign(1, ry * nx + rx);
  for (int j = 0; j < n - 1; ++j) group.push_back(candidates[size_t(j)].second);
}

// Adds a group's estimate (coefficients g[j * k * k + c], transformed back along the group already)
// into num/den: each block's inverse DCT, weighted by weight and the window.
void aggregate(const float* g, const std::vector<int>& group, int k, float weight, const std::vector<float>& c,
               const std::vector<float>& window, float* num, float* den, float* tmp, bool all) {
  const int nx = W - k + 1;
  const int kk = k * k;
  for (size_t j = 0; j < (all ? group.size() : 1); ++j) {
    const float* coef = g + j * kk;
    // pixels = C^T coef C: first t = coef C (along u), then C^T t (along v).
    for (int v = 0; v < k; ++v)
      for (int x = 0; x < k; ++x) {
        float s = 0.0f;
        for (int u = 0; u < k; ++u) s += coef[v * k + u] * c[size_t(u) * k + x];
        tmp[v * k + x] = s;
      }
    const int bx = group[j] % nx, by = group[j] / nx;
    for (int y = 0; y < k; ++y) {
      float* nr = num + size_t(by + y) * W + bx;
      float* dr = den + size_t(by + y) * W + bx;
      for (int x = 0; x < k; ++x) {
        float s = 0.0f;
        for (int v = 0; v < k; ++v) s += c[size_t(v) * k + y] * tmp[v * k + x];
        const float w = weight * window[size_t(y) * k + x];
        nr[x] += w * s;
        dr[x] += w;
      }
    }
  }
}

// The correlated model's tables: each DCT coefficient's noise covariance between two blocks at every
// displacement up to reach (cq), and the matching's bias at every displacement up to search.
struct NoiseTables {
  int reach = 0;
  std::vector<float> cq;    // [q * span^2 + (dy + reach) * span + dx + reach], span = 2 reach + 1
  std::vector<float> bias;  // [(dy + search) * (2 search + 1) + dx + search]: 2 (R(0) - R(d)) k^2
  float r0 = 0.0f;
};

NoiseTables noiseTables(const NoiseCovariance& cov, int k, int search, const std::vector<float>& c) {
  NoiseTables t;
  t.reach = 2 * search;  // the members of a group lie within search of its reference
  const int span = 2 * t.reach + 1, e = k - 1;
  t.r0 = cov.at(0, 0);
  // a_u(d) = sum_x c[u][x + d] c[u][x]: the 1D basis functions' autocorrelations (the 2D basis is
  // separable, so its autocorrelation is a_v(ey) a_u(ex)).
  std::vector<float> a(size_t(k) * (2 * e + 1), 0.0f);
  for (int u = 0; u < k; ++u)
    for (int d = -e; d <= e; ++d) {
      float sum = 0.0f;
      for (int x = 0; x < k; ++x)
        if (x + d >= 0 && x + d < k) sum += c[size_t(u) * k + x + d] * c[size_t(u) * k + x];
      a[size_t(u) * (2 * e + 1) + d + e] = sum;
    }
  // C_q(d) = sum_{ey, ex} a_v(ey) a_u(ex) R(dy + ey, dx + ex): along x first, then along y.
  const int tall = span + 2 * e;
  std::vector<float> tx(size_t(k) * tall * span);  // [u][dy' + reach + e][dx + reach]
  for (int u = 0; u < k; ++u)
    for (int dy = -t.reach - e; dy <= t.reach + e; ++dy)
      for (int dx = -t.reach; dx <= t.reach; ++dx) {
        float sum = 0.0f;
        for (int ex = -e; ex <= e; ++ex) sum += a[size_t(u) * (2 * e + 1) + ex + e] * cov.at(dx + ex, dy);
        tx[(size_t(u) * tall + size_t(dy + t.reach + e)) * span + size_t(dx + t.reach)] = sum;
      }
  t.cq.assign(size_t(k) * k * span * span, 0.0f);
  for (int v = 0; v < k; ++v)
    for (int u = 0; u < k; ++u) {
      float* out = t.cq.data() + size_t(v * k + u) * span * span;
      for (int dy = -t.reach; dy <= t.reach; ++dy)
        for (int dx = -t.reach; dx <= t.reach; ++dx) {
          float sum = 0.0f;
          for (int ey = -e; ey <= e; ++ey)
            sum += a[size_t(v) * (2 * e + 1) + ey + e] *
                   tx[(size_t(u) * tall + size_t(dy + ey + t.reach + e)) * span + size_t(dx + t.reach)];
          out[size_t(dy + t.reach) * span + size_t(dx + t.reach)] = sum;
        }
    }
  const int ms = 2 * search + 1;
  t.bias.resize(size_t(ms) * ms);
  for (int dy = -search; dy <= search; ++dy)
    for (int dx = -search; dx <= search; ++dx)
      t.bias[size_t(dy + search) * ms + size_t(dx + search)] = 2.0f * (t.r0 - cov.at(dx, dy)) * float(k * k);
  return t;
}

// The noise variance of each coefficient of a group's 3D transform, var[j * kk + q]: from the
// members' pairwise covariances (their displacements), through the Walsh-Hadamard transform on both
// sides, diag(H K H^T).
void groupVariances(const NoiseTables& t, const std::vector<int>& group, int k, float scale, float* var, float* km) {
  const int nx = W - k + 1, kk = k * k, n = int(group.size());
  const int span = 2 * t.reach + 1;
  for (int q = 0; q < kk; ++q) {
    const float* cq = t.cq.data() + size_t(q) * span * span;
    for (int a = 0; a < n; ++a)
      for (int b = 0; b < n; ++b) {
        const int dx = group[size_t(a)] % nx - group[size_t(b)] % nx, dy = group[size_t(a)] / nx - group[size_t(b)] / nx;
        km[a * n + b] = cq[size_t(dy + t.reach) * span + size_t(dx + t.reach)];
      }
    for (int a = 0; a < n; ++a) walshHadamard(km + a * n, n, 1);  // K H^T, row by row
    for (int b = 0; b < n; ++b) walshHadamard(km + b, n, n);      // H (K H^T), column by column
    for (int j = 0; j < n; ++j) var[size_t(j) * kk + q] = std::max(km[j * n + j], 1e-12f) * scale;
  }
}

// Both noise models: white (tables null: every coefficient's variance sigma2) or correlated.
void bm3dCore(const float* image, float* dst, float sigma2, const NoiseTables* tables, float scale, const Bm3dOptions& o) {
  // Around the image's mean: the transforms' float rounding then scales with the scene's contrast,
  // not its level. (At ~6000 counts a block's DC is ~48000, and its rounding flipped threshold
  // decisions: a scene and the same scene 1000 counts warmer differed by up to 0.17 counts.) Each
  // group's mean (its 3D DC) is kept as it is, neither thresholded nor shrunk, as it always was at
  // the camera's levels (and in the reference package's use); centred, it would be thresholded
  // against the image's mean and flat areas near it would snap to it.
  double sum = 0.0;
  for (size_t i = 0; i < kImagePixels; ++i) sum += image[i];
  const float level = float(sum / double(kImagePixels));
  std::vector<float> centred(kImagePixels);
  for (size_t i = 0; i < kImagePixels; ++i) centred[i] = image[i] - level;
  const float* src = centred.data();
  const int k = std::clamp(o.block, 2, 16);
  const int kk = k * k;
  const std::vector<float> c = dctMatrix(k), window = kaiserWindow(k, o.kaiser);
  const std::vector<int> xs = referencePositions(W - k + 1, o.stride), ys = referencePositions(H - k + 1, o.stride);
  std::vector<float> noisyDct, basicDct;
  blockDcts(src, k, c, noisyDct);
  std::vector<float> num(kImagePixels), den(kImagePixels), basic(kImagePixels);
  std::vector<std::pair<float, int>> candidates;
  std::vector<int> group;
  const int maxGroup = std::max(o.group1, o.group2);
  std::vector<float> g(static_cast<size_t>(maxGroup) * kk), gb(static_cast<size_t>(maxGroup) * kk),
      tmp(static_cast<size_t>(kk)), var(static_cast<size_t>(maxGroup) * kk, sigma2),
      km(static_cast<size_t>(maxGroup) * maxGroup);
  std::vector<float> bias;
  if (tables)
    for (float b : tables->bias) bias.push_back(b * scale);
  const float* matchBias = tables ? bias.data() : nullptr;
  const float pixelVar = tables ? tables->r0 * scale : sigma2;  // the thresholds' unit

  // Step 1: hard thresholding of each group's 3D transform, each coefficient against its own noise.
  const float lambda2 = o.lambda * o.lambda;
  for (int ry : ys)
    for (int rx : xs) {
      matchBlocks(src, k, rx, ry, o.search, o.tau1 * pixelVar, o.group1, o.skipSameColumn, o.skipSameRow, matchBias,
                  candidates, group);
      const int n = int(group.size());
      if (tables) groupVariances(*tables, group, k, scale, var.data(), km.data());
      for (int j = 0; j < n; ++j)
        std::copy_n(noisyDct.data() + size_t(group[size_t(j)]) * kk, kk, g.data() + size_t(j) * kk);
      double keptVar = 0.0;  // the noise the group keeps: its weight is the inverse
      for (int q = 0; q < kk; ++q) {
        walshHadamard(g.data() + q, n, kk);
        for (int j = 0; j < n; ++j) {
          float& v = g[size_t(j) * kk + q];
          const float vr = var[size_t(j) * kk + q];
          if (v * v < lambda2 * vr && (q | j)) v = 0.0f;  // (the group's mean, q = j = 0, always stays: below)
          else keptVar += vr;
        }
        walshHadamard(g.data() + q, n, kk);
      }
      const float weight = keptVar > 0.0 ? float(1.0 / keptVar) : 1.0f / pixelVar;
      aggregate(g.data(), group, k, weight, c, window, num.data(), den.data(), tmp.data(), o.aggregateAll);
    }
  for (size_t i = 0; i < kImagePixels; ++i) basic[i] = num[i] / den[i];
  if (!o.wiener) {
    std::copy(basic.begin(), basic.end(), dst);
    return;
  }

  // Step 2: the basic estimate's groups give the Wiener factors for the noisy ones.
  blockDcts(basic.data(), k, c, basicDct);
  std::fill(num.begin(), num.end(), 0.0f);
  std::fill(den.begin(), den.end(), 0.0f);
  for (int ry : ys)
    for (int rx : xs) {
      // (on the basic estimate, the noise left is small and hardly correlated: no bias)
      matchBlocks(basic.data(), k, rx, ry, o.search, o.tau2 * pixelVar, o.group2, o.skipSameColumn, o.skipSameRow,
                  nullptr, candidates, group);
      const int n = int(group.size());
      if (tables) groupVariances(*tables, group, k, scale, var.data(), km.data());
      for (int j = 0; j < n; ++j) {
        std::copy_n(noisyDct.data() + size_t(group[size_t(j)]) * kk, kk, g.data() + size_t(j) * kk);
        std::copy_n(basicDct.data() + size_t(group[size_t(j)]) * kk, kk, gb.data() + size_t(j) * kk);
      }
      double energy = 0.0;  // the noise left after shrinking: sum of W^2 var
      for (int q = 0; q < kk; ++q) {
        walshHadamard(g.data() + q, n, kk);
        walshHadamard(gb.data() + q, n, kk);
        for (int j = 0; j < n; ++j) {
          const float b = gb[size_t(j) * kk + q];
          const float vr = var[size_t(j) * kk + q];
          const float wf = (q | j) ? b * b / (b * b + o.mu2 * vr) : 1.0f;
          g[size_t(j) * kk + q] *= wf;
          energy += double(wf) * wf * vr;
        }
        walshHadamard(g.data() + q, n, kk);
      }
      aggregate(g.data(), group, k, energy > 1e-20 ? float(1.0 / energy) : 1.0f / pixelVar, c, window, num.data(),
                den.data(), tmp.data(), o.aggregateAll);
    }
  for (size_t i = 0; i < kImagePixels; ++i) dst[i] = num[i] / den[i] + level;
}

}  // namespace

std::vector<float> bm3dDctMatrix(int k) { return dctMatrix(k); }
std::vector<float> bm3dKaiserWindow(int k, float beta) { return kaiserWindow(k, beta); }

float NoiseCovariance::at(int dx, int dy) const {
  if (radius <= 0 || values.empty()) return 0.0f;
  dx = std::clamp(dx, -radius, radius);
  dy = std::clamp(dy, -radius, radius);
  return values[size_t(dy + radius) * size_t(2 * radius + 1) + size_t(dx + radius)];
}

void bm3d(const float* src, float* dst, float sigma, const Bm3dOptions& o) {
  if (!(sigma > 0.0f)) {
    std::copy(src, src + kImagePixels, dst);
    return;
  }
  bm3dCore(src, dst, sigma * sigma, nullptr, 1.0f, o);
}

void bm3d(const float* src, float* dst, const NoiseCovariance& noise, float scale, const Bm3dOptions& o) {
  if (!(scale > 0.0f) || noise.radius <= 0 || !(noise.at(0, 0) > 0.0f)) {
    std::copy(src, src + kImagePixels, dst);
    return;
  }
  const int k = std::clamp(o.block, 2, 16);
  const NoiseTables tables = noiseTables(noise, k, std::max(o.search, 0), dctMatrix(k));
  bm3dCore(src, dst, 0.0f, &tables, scale, o);
}

}  // namespace tv
