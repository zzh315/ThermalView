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
void matchBlocks(const float* img, int k, int rx, int ry, int search, float tau, int maxGroup,
                 std::vector<std::pair<float, int>>& candidates, std::vector<int>& group) {
  const int nx = W - k + 1, ny = H - k + 1;
  const float limit = tau > 0.0f ? tau * float(k * k) : 3.0e38f;  // as a sum over the block
  candidates.clear();
  const float* ref = img + size_t(ry) * W + rx;
  for (int y = std::max(0, ry - search); y <= std::min(ny - 1, ry + search); ++y)
    for (int x = std::max(0, rx - search); x <= std::min(nx - 1, rx + search); ++x) {
      if (x == rx && y == ry) continue;
      const float* cand = img + size_t(y) * W + x;
      float d = 0.0f;
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

}  // namespace

void bm3d(const float* src, float* dst, float sigma, const Bm3dOptions& o) {
  const int k = std::clamp(o.block, 2, 16);
  if (!(sigma > 0.0f)) {
    std::copy(src, src + kImagePixels, dst);
    return;
  }
  const int kk = k * k;
  const std::vector<float> c = dctMatrix(k), window = kaiserWindow(k, o.kaiser);
  const std::vector<int> xs = referencePositions(W - k + 1, o.stride), ys = referencePositions(H - k + 1, o.stride);
  std::vector<float> noisyDct, basicDct;
  blockDcts(src, k, c, noisyDct);
  std::vector<float> num(kImagePixels), den(kImagePixels), basic(kImagePixels);
  std::vector<std::pair<float, int>> candidates;
  std::vector<int> group;
  const int maxGroup = std::max(o.group1, o.group2);
  std::vector<float> g(static_cast<size_t>(maxGroup) * kk), gb(static_cast<size_t>(maxGroup) * kk), tmp(static_cast<size_t>(kk));
  const float sigma2 = sigma * sigma;

  // Step 1: hard thresholding of each group's 3D transform.
  const float threshold = o.lambda * sigma;
  for (int ry : ys)
    for (int rx : xs) {
      matchBlocks(src, k, rx, ry, o.search, o.tau1 * sigma2, o.group1, candidates, group);
      const int n = int(group.size());
      for (int j = 0; j < n; ++j)
        std::copy_n(noisyDct.data() + size_t(group[size_t(j)]) * kk, kk, g.data() + size_t(j) * kk);
      int kept = 0;
      for (int q = 0; q < kk; ++q) {
        walshHadamard(g.data() + q, n, kk);
        for (int j = 0; j < n; ++j) {
          float& v = g[size_t(j) * kk + q];
          if (std::fabs(v) < threshold) v = 0.0f;
          else ++kept;
        }
        walshHadamard(g.data() + q, n, kk);
      }
      aggregate(g.data(), group, k, 1.0f / float(std::max(kept, 1)), c, window, num.data(), den.data(), tmp.data(),
                o.aggregateAll);
    }
  for (size_t i = 0; i < kImagePixels; ++i) basic[i] = num[i] / den[i];
  if (!o.wiener) {
    std::copy(basic.begin(), basic.end(), dst);
    return;
  }

  // Step 2: the basic estimate's groups give the Wiener factors for the noisy ones.
  const float noiseW = o.mu2 * sigma2;
  blockDcts(basic.data(), k, c, basicDct);
  std::fill(num.begin(), num.end(), 0.0f);
  std::fill(den.begin(), den.end(), 0.0f);
  for (int ry : ys)
    for (int rx : xs) {
      matchBlocks(basic.data(), k, rx, ry, o.search, o.tau2 * sigma2, o.group2, candidates, group);
      const int n = int(group.size());
      for (int j = 0; j < n; ++j) {
        std::copy_n(noisyDct.data() + size_t(group[size_t(j)]) * kk, kk, g.data() + size_t(j) * kk);
        std::copy_n(basicDct.data() + size_t(group[size_t(j)]) * kk, kk, gb.data() + size_t(j) * kk);
      }
      float energy = 0.0f;  // the Wiener factors' sum of squares
      for (int q = 0; q < kk; ++q) {
        walshHadamard(g.data() + q, n, kk);
        walshHadamard(gb.data() + q, n, kk);
        for (int j = 0; j < n; ++j) {
          const float b = gb[size_t(j) * kk + q];
          const float wf = b * b / (b * b + noiseW);
          g[size_t(j) * kk + q] *= wf;
          energy += wf * wf;
        }
        walshHadamard(g.data() + q, n, kk);
      }
      aggregate(g.data(), group, k, 1.0f / std::max(energy, 1e-6f), c, window, num.data(), den.data(), tmp.data(),
                o.aggregateAll);
    }
  for (size_t i = 0; i < kImagePixels; ++i) dst[i] = num[i] / den[i];
}

}  // namespace tv
