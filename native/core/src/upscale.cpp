#include "tv/upscale.h"

#include <algorithm>
#include <cmath>
#include <vector>

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
    case Kernel::Easu:  // not separable: upscale() evaluates it per pixel (easuSample)
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

// AMD FidelityFX Super Resolution 1.0's EASU (Copyright (c) 2021 Advanced Micro Devices, MIT; see
// THIRD_PARTY.md), adapted to one scalar channel and exact arithmetic. Around the sample's 2x2
// neighborhood it measures the local gradient's direction and how edge-like it is, then filters the
// 12 nearest pixels with a Lanczos-2-like kernel rotated to the edge and stretched along it, and
// clamps to the 4 nearest pixels (no ringing).
float easuSample(const float* img, float u, float v) {
  const float fx = std::floor(u), fy = std::floor(v);
  const float px = u - fx, py = v - fy;
  const int x0 = int(fx), y0 = int(fy);
  auto at = [&](int dx, int dy) { return img[size_t(clampIndex(y0 + dy, H)) * W + size_t(clampIndex(x0 + dx, W))]; };
  //    b c
  //  e f g h
  //  i j k l
  //    n o
  const float b = at(0, -1), c = at(1, -1), e = at(-1, 0), f = at(0, 0), g = at(1, 0), hh = at(2, 0), i = at(-1, 1),
              j = at(0, 1), k = at(1, 1), l = at(2, 1), n = at(0, 2), o = at(1, 2);
  float dirX = 0, dirY = 0, len = 0;
  auto accumulate = [&](float w, float lA, float lB, float lC, float lD, float lE) {
    // '+' around C: direction from the outer differences, "edginess" from how one-sided it is.
    const float dx = lD - lB, mx = std::max(std::fabs(lD - lC), std::fabs(lC - lB));
    const float dy = lE - lA, my = std::max(std::fabs(lE - lC), std::fabs(lC - lA));
    dirX += dx * w;
    dirY += dy * w;
    const float lx = mx > 0 ? std::min(std::fabs(dx) / mx, 1.0f) : 0.0f;
    const float ly = my > 0 ? std::min(std::fabs(dy) / my, 1.0f) : 0.0f;
    len += (lx * lx + ly * ly) * w;
  };
  accumulate((1 - px) * (1 - py), b, e, f, g, j);
  accumulate(px * (1 - py), c, f, g, hh, k);
  accumulate((1 - px) * py, f, i, j, k, n);
  accumulate(px * py, g, j, k, l, o);
  const float r2 = dirX * dirX + dirY * dirY;
  if (r2 < 1.0f / 32768.0f) {
    dirX = 1.0f;
    dirY = 0.0f;
  } else {
    const float r = 1.0f / std::sqrt(r2);
    dirX *= r;
    dirY *= r;
  }
  len = 0.25f * len * len;  // {0..2} to {0..1}, squared
  const float stretch = 1.0f / std::max(std::fabs(dirX), std::fabs(dirY));  // 1 on axes, sqrt(2) diagonally
  const float lenX = 1.0f + (stretch - 1.0f) * len, lenY = 1.0f - 0.5f * len;
  const float lob = 0.5f + ((0.25f - 0.04f) - 0.5f) * len, clip = 1.0f / lob;
  float sum = 0, weight = 0;
  auto tap = [&](float ox, float oy, float value) {
    ox -= px;
    oy -= py;
    const float vx = (ox * dirX + oy * dirY) * lenX, vy = (-ox * dirY + oy * dirX) * lenY;
    const float d2 = std::min(vx * vx + vy * vy, clip);
    float wb = 0.4f * d2 - 1.0f, wa = lob * d2 - 1.0f;  // Lanczos-2's shape without sin(): base x window
    wb = (25.0f / 16.0f) * wb * wb - (25.0f / 16.0f - 1.0f);
    const float w = wb * wa * wa;
    sum += value * w;
    weight += w;
  };
  tap(0, -1, b);
  tap(1, -1, c);
  tap(-1, 1, i);
  tap(0, 1, j);
  tap(0, 0, f);
  tap(-1, 0, e);
  tap(1, 1, k);
  tap(2, 1, l);
  tap(2, 0, hh);
  tap(1, 0, g);
  tap(1, 2, o);
  tap(0, 2, n);
  const float lo = std::min(std::min(f, g), std::min(j, k)), hi = std::max(std::max(f, g), std::max(j, k));
  return std::clamp(weight != 0.0f ? sum / weight : f, lo, hi);
}

}  // namespace

bool parseKernel(const std::string& name, Kernel* kernel) {
  for (Kernel k : {Kernel::Nearest, Kernel::Bilinear, Kernel::CatmullRom, Kernel::Lanczos3, Kernel::CardinalBSpline,
                   Kernel::Easu})
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
    case Kernel::Easu: return "easu";
  }
  return "?";
}

namespace {

// prefilterLine's recursion on every column of a rows x cols block at once: each step is a whole
// row, contiguous and independent across columns, so it vectorizes (the renderer runs this per
// frame; line by line with a stride, in double, it took 2.4 ms on the tablet's render thread).
// Floats: the pole's filters are stable and the result stays within ~1e-6 of the double version.
void prefilterColumns(float* c, int rows, int cols) {
  if (rows < 2) return;
  const float z = float(std::sqrt(3.0) - 2.0);
  const float gain = float((1.0 - (std::sqrt(3.0) - 2.0)) * (1.0 - 1.0 / (std::sqrt(3.0) - 2.0)));  // 6
  const int horizon = std::min(rows, int(std::ceil(std::log(1e-9) / std::log(std::fabs(double(z))))));
  const size_t n = size_t(cols);
  for (size_t i = 0; i < n * size_t(rows); ++i) c[i] *= gain;
  // Causal start: the mirrored signal's sum over the first rows (as prefilterLine), into a scratch row.
  std::vector<float> start(c, c + n);
  float zn = z;
  for (int k = 1; k < horizon; ++k) {
    const float* row = c + size_t(k) * n;
    for (size_t x = 0; x < n; ++x) start[x] += zn * row[x];
    zn *= z;
  }
  std::copy(start.begin(), start.end(), c);
  for (int k = 1; k < rows; ++k) {
    float* row = c + size_t(k) * n;
    const float* prev = row - n;
    for (size_t x = 0; x < n; ++x) row[x] += z * prev[x];
  }
  const float endGain = z / (z * z - 1.0f);
  float* last = c + size_t(rows - 1) * n;
  const float* beforeLast = last - n;
  for (size_t x = 0; x < n; ++x) last[x] = endGain * (z * beforeLast[x] + last[x]);
  for (int k = rows - 2; k >= 0; --k) {
    float* row = c + size_t(k) * n;
    const float* next = row + n;
    for (size_t x = 0; x < n; ++x) row[x] = z * (next[x] - row[x]);
  }
}

// The same recursion along every row, 8 rows interleaved so their dependency chains overlap.
void prefilterRows(float* c) {
  constexpr int kRows = 8;
  static_assert(H % kRows == 0);
  const float z = float(std::sqrt(3.0) - 2.0);
  const float gain = float((1.0 - (std::sqrt(3.0) - 2.0)) * (1.0 - 1.0 / (std::sqrt(3.0) - 2.0)));  // 6
  const int horizon = std::min(W, int(std::ceil(std::log(1e-9) / std::log(std::fabs(double(z))))));
  const float endGain = z / (z * z - 1.0f);
  for (int y0 = 0; y0 < H; y0 += kRows) {
    float* block = c + size_t(y0) * W;
    for (size_t i = 0; i < size_t(kRows) * W; ++i) block[i] *= gain;
    float* r[kRows];
    for (int j = 0; j < kRows; ++j) r[j] = block + size_t(j) * W;
    float start[kRows];
    for (int j = 0; j < kRows; ++j) start[j] = r[j][0];
    float zn = z;
    for (int k = 1; k < horizon; ++k) {
      for (int j = 0; j < kRows; ++j) start[j] += zn * r[j][k];
      zn *= z;
    }
    for (int j = 0; j < kRows; ++j) r[j][0] = start[j];
    for (int x = 1; x < W; ++x)
      for (int j = 0; j < kRows; ++j) r[j][x] += z * r[j][x - 1];
    for (int j = 0; j < kRows; ++j) r[j][W - 1] = endGain * (z * r[j][W - 2] + r[j][W - 1]);
    for (int x = W - 2; x >= 0; --x)
      for (int j = 0; j < kRows; ++j) r[j][x] = z * (r[j][x + 1] - r[j][x]);
  }
}

}  // namespace

void bsplineCoefficients(const float* image, float* coeffs) {
  std::copy(image, image + kImagePixels, coeffs);
  prefilterRows(coeffs);
  prefilterColumns(coeffs, H, W);
}

void bsplineCoefficientsReference(const float* image, float* coeffs) {
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
  if (kernel == Kernel::Easu) {  // not separable: per output pixel (it clamps to its 4 nearest itself)
    for (int y = 0; y < dstH; ++y) {
      const float v = rect.y + (float(y) + 0.5f) * rect.h / float(dstH) - 0.5f;
      for (int x = 0; x < dstW; ++x)
        dst[size_t(y) * dstW + x] = easuSample(input.data(), rect.x + (float(x) + 0.5f) * rect.w / float(dstW) - 0.5f, v);
    }
    return;
  }
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

namespace {

// Separable running min and max over (2r + 1) x (2r + 1), borders clamped.
void minMax(const float* image, int r, float* lo, float* hi) {
  const int w = kFrameWidth, h = kImageRows;
  std::vector<float> rowLo(size_t(w) * size_t(h)), rowHi(rowLo.size());
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      float a = image[size_t(y) * size_t(w) + size_t(x)], b = a;
      for (int d = -r; d <= r; ++d) {
        const float v = image[size_t(y) * size_t(w) + size_t(std::clamp(x + d, 0, w - 1))];
        a = std::min(a, v);
        b = std::max(b, v);
      }
      rowLo[size_t(y) * size_t(w) + size_t(x)] = a;
      rowHi[size_t(y) * size_t(w) + size_t(x)] = b;
    }
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      float a = rowLo[size_t(y) * size_t(w) + size_t(x)], b = rowHi[size_t(y) * size_t(w) + size_t(x)];
      for (int d = -r; d <= r; ++d) {
        const size_t i = size_t(std::clamp(y + d, 0, h - 1)) * size_t(w) + size_t(x);
        a = std::min(a, rowLo[i]);
        b = std::max(b, rowHi[i]);
      }
      lo[size_t(y) * size_t(w) + size_t(x)] = a;
      hi[size_t(y) * size_t(w) + size_t(x)] = b;
    }
}

float smoothstep(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

}  // namespace

void contourFields(const float* image, ContourFields* fields) {
  std::vector<float> lo3(kImagePixels), hi3(kImagePixels), lo7(kImagePixels), hi7(kImagePixels);
  minMax(image, 1, lo3.data(), hi3.data());
  minMax(image, 3, lo7.data(), hi7.data());
  fields->values.resize(4 * kImagePixels);
  for (size_t i = 0; i < kImagePixels; ++i) {
    fields->values[4 * i] = lo3[i];
    fields->values[4 * i + 1] = hi3[i];
    fields->values[4 * i + 2] = lo7[i];
    fields->values[4 * i + 3] = hi7[i];
  }
  // The noise floor: the 20th percentile of the 3x3 range, from every 7th pixel.
  std::vector<float> r;
  r.reserve(kImagePixels / 7 + 1);
  for (size_t i = 3; i < kImagePixels; i += 7) r.push_back(hi3[i] - lo3[i]);
  std::nth_element(r.begin(), r.begin() + std::ptrdiff_t(r.size() / 5), r.end());
  fields->floor = std::max(r[r.size() / 5], 1e-4f);
}

float shapeContour(const ContourFields& fields, float k, float cx, float cy, float v) {
  const float fx = std::floor(cx), fy = std::floor(cy);
  const float tx = cx - fx, ty = cy - fy;
  const int x0 = std::clamp(int(fx), 0, kFrameWidth - 1), x1 = std::clamp(int(fx) + 1, 0, kFrameWidth - 1);
  const int y0 = std::clamp(int(fy), 0, kImageRows - 1), y1 = std::clamp(int(fy) + 1, 0, kImageRows - 1);
  float f[4];
  for (int c = 0; c < 4; ++c) {
    const auto at = [&](int x, int y) { return fields.values[4 * (size_t(y) * kFrameWidth + size_t(x)) + size_t(c)]; };
    const float top = at(x0, y0) + tx * (at(x1, y0) - at(x0, y0));
    const float bottom = at(x0, y1) + tx * (at(x1, y1) - at(x0, y1));
    f[c] = top + ty * (bottom - top);
  }
  const float range = f[1] - f[0];
  const float gate = smoothstep(0.45f, 0.75f, range / std::max(f[3] - f[2], 1e-5f)) *
                     smoothstep(3.0f * fields.floor, 6.0f * fields.floor, range);
  const float mid = 0.5f * (f[0] + f[1]);
  return std::clamp(mid + (v - mid) * (1.0f + k * gate), f[0], f[1]);
}

void sharpenContours(const ContourFields& fields, float k, const ViewRect& rect, int dstW, int dstH, float* dst) {
  if (k <= 0.0f) return;
  for (int j = 0; j < dstH; ++j) {
    const float cy = rect.y + (float(j) + 0.5f) * rect.h / float(dstH) - 0.5f;
    for (int i = 0; i < dstW; ++i) {
      const float cx = rect.x + (float(i) + 0.5f) * rect.w / float(dstW) - 0.5f;
      float& v = dst[size_t(j) * size_t(dstW) + size_t(i)];
      v = shapeContour(fields, k, cx, cy, std::clamp(v, 0.0f, 1.0f));
    }
  }
}

}  // namespace tv
