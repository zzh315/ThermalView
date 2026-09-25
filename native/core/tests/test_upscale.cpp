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
