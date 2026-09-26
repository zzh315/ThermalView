#include <algorithm>
#include <cmath>
#include <vector>

#include "doctest.h"
#include "tv/upscale.h"

namespace {
constexpr int W = tv::kFrameWidth, H = tv::kImageRows;
std::vector<float> randomImage(uint32_t s) {
  std::vector<float> a(tv::kImagePixels);
  for (float& v : a) {
    s = s * 1664525u + 1013904223u;
    v = float(s >> 8) / float(1 << 24);
  }
  return a;
}
}  // namespace

TEST_CASE("every kernel reproduces the frame at 1:1") {
  const auto img = randomImage(7);
  for (tv::Kernel k : {tv::Kernel::Nearest, tv::Kernel::Bilinear, tv::Kernel::CatmullRom, tv::Kernel::Lanczos3,
                       tv::Kernel::CardinalBSpline}) {  // (EASU resamples: it needn't pass through the pixels)
    CAPTURE(tv::kernelName(k));
    std::vector<float> out(tv::kImagePixels);
    tv::upscale(tv::kernelInput(img.data(), k), img.data(), k, false, {}, W, H, out.data());
    float worst = 0;
    for (size_t i = 0; i < out.size(); ++i) worst = std::max(worst, std::fabs(out[i] - img[i]));
    CHECK(worst < 2e-5f);  // the B-spline interpolates too: its prefilter makes it pass through the pixels
  }
}

TEST_CASE("kernel names round-trip") {
  for (tv::Kernel k : {tv::Kernel::Nearest, tv::Kernel::Bilinear, tv::Kernel::CatmullRom, tv::Kernel::Lanczos3,
                       tv::Kernel::CardinalBSpline, tv::Kernel::Easu}) {
    tv::Kernel parsed = tv::Kernel::Nearest;
    CHECK(tv::parseKernel(tv::kernelName(k), &parsed));
    CHECK(parsed == k);
  }
  tv::Kernel k;
  CHECK_FALSE(tv::parseKernel("sinc", &k));
}

TEST_CASE("sharp kernels ring at a step; the 2x2 clamp stops it") {
  std::vector<float> img(tv::kImagePixels);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) img[size_t(y) * W + x] = x < 128 ? 0.2f : 0.8f;
  const int dw = 8 * W, dh = 32;
  const tv::ViewRect rect{0.0f, 90.0f, float(W), 4.0f};
  for (tv::Kernel k : {tv::Kernel::CatmullRom, tv::Kernel::Lanczos3, tv::Kernel::CardinalBSpline}) {
    CAPTURE(tv::kernelName(k));
    std::vector<float> out(size_t(dw) * dh);
    const auto in = tv::kernelInput(img.data(), k);
    tv::upscale(in, img.data(), k, false, rect, dw, dh, out.data());
    const auto [lo, hi] = std::minmax_element(out.begin(), out.end());
    CHECK((*lo < 0.2f - 1e-3f || *hi > 0.8f + 1e-3f));  // overshoot beside the step
    tv::upscale(in, img.data(), k, true, rect, dw, dh, out.data());
    const auto [clo, chi] = std::minmax_element(out.begin(), out.end());
    CHECK(*clo >= 0.2f - 1e-6f);
    CHECK(*chi <= 0.8f + 1e-6f);
  }
}

TEST_CASE("a zoomed view samples the rectangle it names") {
  std::vector<float> img(tv::kImagePixels);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) img[size_t(y) * W + x] = float(x) + 1000.0f * float(y);
  std::vector<float> out(64 * 48);
  // 64 x 48 camera pixels starting at (100, 50), 1:1: exactly those pixels, for every kernel.
  for (tv::Kernel k : {tv::Kernel::Nearest, tv::Kernel::CatmullRom, tv::Kernel::CardinalBSpline}) {
    tv::upscale(tv::kernelInput(img.data(), k), img.data(), k, true, {100.0f, 50.0f, 64.0f, 48.0f}, 64, 48, out.data());
    CHECK(out[0] == doctest::Approx(100.0f + 50000.0f).epsilon(1e-5));
    CHECK(out[size_t(47) * 64 + 63] == doctest::Approx(163.0f + 97000.0f).epsilon(1e-5));
  }
}

TEST_CASE("EASU stays within each sample's 4 nearest pixels and keeps a flat field flat") {
  const auto img = randomImage(21);
  const int dw = 3 * W, dh = 3 * H;
  std::vector<float> out(size_t(dw) * dh);
  tv::upscale(tv::kernelInput(img.data(), tv::Kernel::Easu), img.data(), tv::Kernel::Easu, false, {}, dw, dh, out.data());
  int outside = 0;
  for (int y = 0; y < dh; ++y)
    for (int x = 0; x < dw; ++x) {
      const float u = (float(x) + 0.5f) / 3.0f - 0.5f, v = (float(y) + 0.5f) / 3.0f - 0.5f;
      const int x0 = std::clamp(int(std::floor(u)), 0, W - 1), x1 = std::clamp(int(std::floor(u)) + 1, 0, W - 1);
      const int y0 = std::clamp(int(std::floor(v)), 0, H - 1), y1 = std::clamp(int(std::floor(v)) + 1, 0, H - 1);
      const float a = img[size_t(y0) * W + x0], b = img[size_t(y0) * W + x1], c = img[size_t(y1) * W + x0],
                  d = img[size_t(y1) * W + x1];
      const float o = out[size_t(y) * dw + x];
      outside += o < std::min({a, b, c, d}) - 1e-6f || o > std::max({a, b, c, d}) + 1e-6f;
    }
  CHECK(outside == 0);
  std::vector<float> flat(tv::kImagePixels, 0.4f);
  tv::upscale(tv::kernelInput(flat.data(), tv::Kernel::Easu), flat.data(), tv::Kernel::Easu, false, {}, dw, dh, out.data());
  CHECK(*std::max_element(out.begin(), out.end()) == doctest::Approx(0.4f));
  CHECK(*std::min_element(out.begin(), out.end()) == doctest::Approx(0.4f));
}

TEST_CASE("the fast B-spline prefilter matches the line-by-line double version") {
  std::vector<float> img(tv::kImagePixels), fast(tv::kImagePixels), ref(tv::kImagePixels);
  uint32_t seed = 3;
  for (size_t i = 0; i < img.size(); ++i) {  // display intensities: noise, a step and a ramp
    seed = seed * 1664525u + 1013904223u;
    const int x = int(i % tv::kFrameWidth), y = int(i / tv::kFrameWidth);
    img[i] = 0.02f * float(seed >> 24) / 255.0f + (x > 100 ? 0.6f : 0.1f) + 0.001f * float(y);
  }
  tv::bsplineCoefficients(img.data(), fast.data());
  tv::bsplineCoefficientsReference(img.data(), ref.data());
  float worst = 0.0f;
  for (size_t i = 0; i < img.size(); ++i) worst = std::max(worst, std::fabs(fast[i] - ref[i]));
  CHECK(worst < 1e-5f);
}

namespace {
// An 8x upscale of image through the cardinal B-spline (the app's), then the contour shaping at k.
std::vector<float> shaped(const std::vector<float>& img, float k, int dw = 8 * W, int dh = 8 * H) {
  std::vector<float> out(size_t(dw) * size_t(dh));
  tv::upscale(tv::kernelInput(img.data(), tv::Kernel::CardinalBSpline), img.data(), tv::Kernel::CardinalBSpline, true, {},
              dw, dh, out.data());
  tv::ContourFields f;
  tv::contourFields(img.data(), &f);
  tv::sharpenContours(f, k, {}, dw, dh, out.data());
  return out;
}
// A soft vertical step at x = 128 (a 2 px ramp, like a real edge through the lens) with a little noise.
std::vector<float> softStep(uint32_t seed, float noise) {
  std::vector<float> a(tv::kImagePixels);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      seed = seed * 1664525u + 1013904223u;
      const float t = std::clamp((float(x) - 127.0f) / 2.0f, 0.0f, 1.0f);
      a[size_t(y) * W + size_t(x)] = 0.3f + 0.4f * t + noise * (float(seed >> 8) / float(1 << 24) - 0.5f);
    }
  return a;
}
float width1090(const std::vector<float>& out, int dw, int row) {  // the step's 10-90% width, output pixels
  const float* p = &out[size_t(row) * size_t(dw)];
  int a = 0, b = 0;
  while (a < dw && p[a] < 0.3f + 0.04f) ++a;
  while (b < dw && p[b] < 0.7f - 0.04f) ++b;
  return float(b - a);
}
}  // namespace

TEST_CASE("contour sharpening steepens an edge without passing its neighbourhood's range") {
  const auto img = softStep(3, 0.004f);
  const auto plain = shaped(img, 0.0f), sharp = shaped(img, 1.0f);
  const int dw = 8 * W, row = 8 * 96 + 3;
  CHECK(width1090(sharp, dw, row) < 0.8f * width1090(plain, dw, row));
  // No halo: nothing goes outside the step's two levels (and a little noise).
  float lo = 1, hi = 0;
  for (int x = 8 * 110; x < 8 * 146; ++x) {
    lo = std::min(lo, sharp[size_t(row) * size_t(dw) + size_t(x)]);
    hi = std::max(hi, sharp[size_t(row) * size_t(dw) + size_t(x)]);
  }
  CHECK(lo > 0.3f - 0.01f);
  CHECK(hi < 0.7f + 0.01f);
  // Its position stays: the 50% crossing moves by under a camera pixel's tenth.
  const auto cross = [&](const std::vector<float>& o) {
    int x = 0;
    while (x < dw && o[size_t(row) * size_t(dw) + size_t(x)] < 0.5f) ++x;
    return x;
  };
  CHECK(std::abs(cross(sharp) - cross(plain)) <= 1);
}

TEST_CASE("contour sharpening leaves noise and smooth gradients alone") {
  // Flat grain: the gate stays shut (its floor is the frame's own).
  std::vector<float> grain(tv::kImagePixels);
  uint32_t s = 11;
  for (float& v : grain) {
    s = s * 1664525u + 1013904223u;
    v = 0.5f + 0.01f * (float(s >> 8) / float(1 << 24) - 0.5f);
  }
  const auto g0 = shaped(grain, 0.0f, 4 * W, 4 * H), g1 = shaped(grain, 1.0f, 4 * W, 4 * H);
  float worst = 0;
  for (size_t i = 0; i < g0.size(); ++i) worst = std::max(worst, std::fabs(g1[i] - g0[i]));
  CHECK(worst < 0.004f);  // under a display level
  // A steady ramp across the frame: no terracing.
  std::vector<float> ramp(tv::kImagePixels);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) ramp[size_t(y) * W + size_t(x)] = 0.2f + 0.6f * float(x) / float(W - 1);
  const auto r0 = shaped(ramp, 0.0f, 4 * W, 4 * H), r1 = shaped(ramp, 1.0f, 4 * W, 4 * H);
  worst = 0;
  for (size_t i = 0; i < r0.size(); ++i) worst = std::max(worst, std::fabs(r1[i] - r0[i]));
  CHECK(worst < 1e-4f);
}
