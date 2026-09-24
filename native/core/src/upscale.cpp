#include "tv/upscale.h"

#include <algorithm>
#include <cmath>

namespace tv {
namespace {

constexpr int W = kFrameWidth, H = kImageRows;

struct Taps {
  int first = 0, n = 0;  // taps at first .. first + n - 1 (border-handled when used)
  float w[6] = {};
};

float sinc(float x) {
  if (std::fabs(x) < 1e-6f) return 1.0f;
  const float px = 3.14159265358979f * x;
  return std::sin(px) / px;
}

Taps tapsFor(Kernel kernel, float u) {
  Taps t;
  const float fl = std::floor(u), f = u - fl;
  const int i = int(fl);
  switch (kernel) {
    case Kernel::Nearest:
      t.first = int(std::floor(u + 0.5f));
      t.n = 1;
      t.w[0] = 1.0f;
      break;
    case Kernel::Bilinear:
      t.first = i;
      t.n = 2;
      t.w[0] = 1.0f - f;
      t.w[1] = f;
      break;
    case Kernel::CatmullRom:  // Keys (1981), a = -0.5
      t.first = i - 1;
      t.n = 4;
      t.w[0] = ((-0.5f * f + 1.0f) * f - 0.5f) * f;
      t.w[1] = (1.5f * f - 2.5f) * f * f + 1.0f;
      t.w[2] = ((-1.5f * f + 2.0f) * f + 0.5f) * f;
      t.w[3] = (0.5f * f - 0.5f) * f * f;
      break;
    case Kernel::Lanczos3: {
      t.first = i - 2;
      t.n = 6;
      float total = 0.0f;
      for (int k = 0; k < 6; ++k) {
        const float d = f + 2.0f - float(k);  // from tap i - 2 + k to u
        total += t.w[k] = sinc(d) * sinc(d / 3.0f);
      }
      for (int k = 0; k < 6; ++k) t.w[k] /= total;
      break;
    }
    case Kernel::CardinalBSpline: {  // the cubic B-spline basis, on prefiltered coefficients
      const float g = 1.0f - f;
      t.first = i - 1;
      t.n = 4;
      t.w[0] = g * g * g / 6.0f;
      t.w[1] = ((3.0f * f - 6.0f) * f * f + 4.0f) / 6.0f;
      t.w[2] = (((-3.0f * f + 3.0f) * f + 3.0f) * f + 1.0f) / 6.0f;
      t.w[3] = f * f * f / 6.0f;
      break;
    }
  }
  return t;
}

int clampIndex(int k, int n) { return std::clamp(k, 0, n - 1); }

int mirrorIndex(int k, int n) {  // whole-sample symmetric: -1 -> 1, n -> n - 2
  if (n == 1) return 0;
  while (k < 0 || k >= n) k = k < 0 ? -k : 2 * (n - 1) - k;
  return k;
}

// Unser (1999), Thévenaz et al. (2000): the cubic B-spline's interpolation coefficients along one
// line, a causal and an anticausal recursive filter with pole sqrt(3) - 2, mirror boundaries.
void prefilterLine(float* c, int n, int stride) {
  if (n < 2) return;
  const double z = std::sqrt(3.0) - 2.0;
  const double gain = (1.0 - z) * (1.0 - 1.0 / z);  // 6
  std::vector<double> line(static_cast<size_t>(n));
  for (int k = 0; k < n; ++k) line[size_t(k)] = gain * c[size_t(k) * stride];
  // Causal start: the mirrored signal's sum, truncated where z^k falls below 1e-9 (~16 terms).
  const int horizon = std::min(n, int(std::ceil(std::log(1e-9) / std::log(std::fabs(z)))));
  double sum = line[0], zn = z;
  for (int k = 1; k < horizon; ++k) {
    sum += zn * line[size_t(k)];
    zn *= z;
  }
  line[0] = sum;
  for (int k = 1; k < n; ++k) line[size_t(k)] += z * line[size_t(k - 1)];
  line[size_t(n - 1)] = (z / (z * z - 1.0)) * (z * line[size_t(n - 2)] + line[size_t(n - 1)]);
  for (int k = n - 2; k >= 0; --k) line[size_t(k)] = z * (line[size_t(k + 1)] - line[size_t(k)]);
  for (int k = 0; k < n; ++k) c[size_t(k) * stride] = float(line[size_t(k)]);
}

}  // namespace

bool parseKernel(const std::string& name, Kernel* kernel) {
  for (Kernel k : {Kernel::Nearest, Kernel::Bilinear, Kernel::CatmullRom, Kernel::Lanczos3, Kernel::CardinalBSpline})
    if (name == kernelName(k)) {
      *kernel = k;
      return true;
    }
  return false;
}

const char* kernelName(Kernel kernel) {
  switch (kernel) {
    case Kernel::Nearest: return "nearest";
    case Kernel::Bilinear: return "bilinear";
    case Kernel::CatmullRom: return "catmullrom";
    case Kernel::Lanczos3: return "lanczos3";
    case Kernel::CardinalBSpline: return "bspline";
  }
  return "?";
}

void bsplineCoefficients(const float* image, float* coeffs) {
  std::copy(image, image + kImagePixels, coeffs);
  for (int y = 0; y < H; ++y) prefilterLine(coeffs + size_t(y) * W, W, 1);
  for (int x = 0; x < W; ++x) prefilterLine(coeffs + x, H, W);
}

std::vector<float> kernelInput(const float* image, Kernel kernel) {
  std::vector<float> c(image, image + kImagePixels);
  if (kernel == Kernel::CardinalBSpline) bsplineCoefficients(image, c.data());
  return c;
}

void upscale(const std::vector<float>& input, const float* image, Kernel kernel, bool clamp, const ViewRect& rect,
             int dstW, int dstH, float* dst) {
  if (dstW <= 0 || dstH <= 0 || input.size() < kImagePixels) return;
  std::vector<Taps> tx(static_cast<size_t>(dstW)), ty(static_cast<size_t>(dstH));
  std::vector<int> x0(static_cast<size_t>(dstW)), y0(static_cast<size_t>(dstH));
  for (int i = 0; i < dstW; ++i) {
    const float u = rect.x + (float(i) + 0.5f) * rect.w / float(dstW) - 0.5f;
    tx[size_t(i)] = tapsFor(kernel, u);
    x0[size_t(i)] = int(std::floor(u));
  }
  for (int j = 0; j < dstH; ++j) {
    const float v = rect.y + (float(j) + 0.5f) * rect.h / float(dstH) - 0.5f;
    ty[size_t(j)] = tapsFor(kernel, v);
    y0[size_t(j)] = int(std::floor(v));
  }
  auto index = kernel == Kernel::CardinalBSpline ? mirrorIndex : clampIndex;
  std::vector<float> row(static_cast<size_t>(W));
  for (int j = 0; j < dstH; ++j) {
    // Down the columns first (whole source rows at a time), then along the row.
    const Taps& t = ty[size_t(j)];
    std::fill(row.begin(), row.end(), 0.0f);
    for (int k = 0; k < t.n; ++k) {
      const float* src = input.data() + size_t(index(t.first + k, H)) * W;
      const float w = t.w[k];
      for (int x = 0; x < W; ++x) row[size_t(x)] += w * src[x];
    }
    float* out = dst + size_t(j) * dstW;
    for (int i = 0; i < dstW; ++i) {
      const Taps& s = tx[size_t(i)];
      float v = 0.0f;
      for (int k = 0; k < s.n; ++k) v += s.w[k] * row[size_t(index(s.first + k, W))];
      out[i] = v;
    }
    if (!clamp) continue;
    const float* ra = image + size_t(clampIndex(y0[size_t(j)], H)) * W;
    const float* rb = image + size_t(clampIndex(y0[size_t(j)] + 1, H)) * W;
    for (int i = 0; i < dstW; ++i) {
      const int xa = clampIndex(x0[size_t(i)], W), xb = clampIndex(x0[size_t(i)] + 1, W);
      const float lo = std::min(std::min(ra[xa], ra[xb]), std::min(rb[xa], rb[xb]));
      const float hi = std::max(std::max(ra[xa], ra[xb]), std::max(rb[xa], rb[xb]));
      out[i] = std::clamp(out[i], lo, hi);
    }
  }
}

}  // namespace tv
