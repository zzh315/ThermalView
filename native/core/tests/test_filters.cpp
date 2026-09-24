#include <algorithm>
#include <cmath>
#include <vector>

#include "doctest.h"
#include "tv/filters.h"
#include "tv/pipeline.h"

namespace {
constexpr int W = tv::kFrameWidth, H = tv::kImageRows;
float noise(uint32_t& s) {
  s = s * 1664525u + 1013904223u;
  return float(s >> 8) / float(1 << 24) - 0.5f;
}
}  // namespace

TEST_CASE("box filter: a constant stays, a ramp stays in the interior") {
  std::vector<float> a(tv::kImagePixels), b(tv::kImagePixels);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) a[size_t(y) * W + x] = float(x);
  tv::boxFilter(a.data(), b.data(), 2);
  CHECK(b[size_t(50) * W + 100] == doctest::Approx(100.0f));
  std::fill(a.begin(), a.end(), 7.0f);
  tv::boxFilter(a.data(), b.data(), 2);
  CHECK(b[0] == doctest::Approx(7.0f));
}

TEST_CASE("the fast blur, local range and 3x3 bounds match brute force") {
  std::vector<float> a(tv::kImagePixels), b(tv::kImagePixels), c(tv::kImagePixels), scratch;
  uint32_t s = 11;
  for (float& v : a) v = 100.0f * noise(s);
  auto at = [&](int x, int y) { return a[size_t(std::clamp(y, 0, H - 1)) * W + size_t(std::clamp(x, 0, W - 1))]; };
  const int probes[][2] = {{0, 0}, {1, 0}, {5, 3}, {W - 1, H - 1}, {W - 2, 7}, {128, 96}, {3, H - 2}, {W - 7, 100}};
  for (float sigma : {0.7f, 1.0f, 2.0f}) {
    tv::gaussianBlur(a.data(), b.data(), sigma, scratch);
    const int r = std::clamp(int(std::ceil(3.0f * sigma)), 1, 7);
    for (const auto& p : probes) {
      double sum = 0, total = 0;
      for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
          const double w = std::exp(-0.5 * (dx * dx) / (sigma * sigma)) * std::exp(-0.5 * (dy * dy) / (sigma * sigma));
          sum += w * at(p[0] + dx, p[1] + dy);
          total += w;
        }
      CHECK(b[size_t(p[1]) * W + p[0]] == doctest::Approx(sum / total).epsilon(1e-4));
    }
  }
  for (int r : {1, 3, 6, 16}) {
    tv::localRange(a.data(), b.data(), r, scratch);
    for (const auto& p : probes) {
      float hi = -1e9f, lo = 1e9f;
      for (int y = std::max(0, p[1] - r); y <= std::min(H - 1, p[1] + r); ++y)
        for (int x = std::max(0, p[0] - r); x <= std::min(W - 1, p[0] + r); ++x) {
          hi = std::max(hi, a[size_t(y) * W + x]);
          lo = std::min(lo, a[size_t(y) * W + x]);
        }
      CHECK(b[size_t(p[1]) * W + p[0]] == doctest::Approx(hi - lo));
    }
  }
  tv::localMinMax3(a.data(), b.data(), c.data(), scratch);
  for (const auto& p : probes) {
    float hi = -1e9f, lo = 1e9f;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        hi = std::max(hi, at(p[0] + dx, p[1] + dy));
        lo = std::min(lo, at(p[0] + dx, p[1] + dy));
      }
    CHECK(b[size_t(p[1]) * W + p[0]] == lo);
    CHECK(c[size_t(p[1]) * W + p[0]] == hi);
  }
}

TEST_CASE("the self-guided filter keeps a strong edge and smooths flat noise") {
  std::vector<float> a(tv::kImagePixels), b(tv::kImagePixels), scratch;
  uint32_t s = 1;
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) a[size_t(y) * W + x] = (x < 128 ? 5000.0f : 5500.0f) + 4.0f * noise(s);
  tv::guidedFilterSelf(a.data(), b.data(), 2, 50.0f, scratch);
  CHECK(b[size_t(96) * W + 126] < 5010.0f);  // the edge survives: two pixels from it, still low
  CHECK(b[size_t(96) * W + 130] > 5490.0f);
  double va = 0, vb = 0;  // noise in a flat stretch
  for (int x = 20; x < 100; ++x) {
    va += std::pow(a[size_t(96) * W + x] - 5000.0f, 2);
    vb += std::pow(b[size_t(96) * W + x] - 5000.0f, 2);
  }
  CHECK(vb < 0.5 * va);
}

TEST_CASE("stage 6 adds contrast to fine texture and leaves no overshoot beyond a step's plateaus") {
  std::vector<uint16_t> img(tv::kImagePixels);
  uint32_t s = 3;
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const float texture = (x / 3 + y / 3) % 2 ? 8.0f : 0.0f;  // fine checkerboard on the left
      const float v = x < 128 ? 5000.0f + texture : 5400.0f;
      img[size_t(y) * W + x] = uint16_t(v + 2.0f * noise(s));
    }
  tv::PipelineOptions plain, enhanced;
  plain.detail = false;
  enhanced.detail = true;
  plain.denoise = enhanced.denoise = false;  // compare single frames
  tv::Pipeline a(plain), b(enhanced);
  std::vector<float> da(tv::kImagePixels), db(tv::kImagePixels);
  for (int k = 0; k < 3; ++k) {
    a.process(img.data(), da.data());
    b.process(img.data(), db.data());
  }
  auto contrast = [&](const std::vector<float>& d) {  // texture's std on the left
    double m = 0, v = 0;
    int n = 0;
    for (int y = 20; y < 170; ++y)
      for (int x = 20; x < 100; ++x) m += d[size_t(y) * W + x], ++n;
    m /= n;
    for (int y = 20; y < 170; ++y)
      for (int x = 20; x < 100; ++x) v += std::pow(d[size_t(y) * W + x] - m, 2);
    return std::sqrt(v / n);
  };
  CHECK(contrast(db) > 1.3 * contrast(da));
  // Across the step, the enhanced row never goes above its bright plateau or below the dark side's range.
  float brightPlateau = 0, darkMin = 1;
  for (int x = 140; x < 240; ++x) brightPlateau = std::max(brightPlateau, db[size_t(96) * W + x]);
  for (int x = 20; x < 120; ++x) darkMin = std::min(darkMin, db[size_t(96) * W + x]);
  for (int x = 120; x < 140; ++x) {
    CHECK(db[size_t(96) * W + x] <= brightPlateau + 1e-3f);
    CHECK(db[size_t(96) * W + x] >= darkMin - 1e-3f);
  }
}

TEST_CASE("stage 6 draws no rim beside a strong step") {
  // A 70-count step spread over a few pixels (a warm object's edge through the lens) between noisy
  // flat fields: the self-guided filter leaves a residual of about a count along both sides of it,
  // which the texture gain would turn into a dark line on the cold side and a bright one on the warm
  // side.
  auto run = [](const tv::PipelineOptions& o) {
    std::vector<uint16_t> img(tv::kImagePixels);
    uint32_t s = 5;
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        const float step = 70.0f / (1.0f + std::exp(-(float(x) - 128.0f)));
        img[size_t(y) * W + x] = uint16_t(5000.0f + step + noise(s) + 0.5f);  // quiet: the rim clears the noise gate
      }
    tv::Pipeline p(o);
    std::vector<float> d(tv::kImagePixels);
    for (int k = 0; k < 3; ++k) p.process(img.data(), d.data());
    CHECK(std::all_of(d.begin(), d.end(), [](float v) { return v >= 0.0f && v <= 1.0f; }));  // no NaN
    std::vector<float> profile(W, 0.0f);  // mean over rows: the noise averages out
    for (int y = 12; y < 180; ++y)
      for (int x = 0; x < W; ++x) profile[size_t(x)] += d[size_t(y) * W + x] / 168.0f;
    return profile;
  };
  auto mean = [](const std::vector<float>& p, int a, int b) {
    float m = 0;
    for (int x = a; x < b; ++x) m += p[size_t(x)] / float(b - a);
    return m;
  };
  auto rims = [&](const std::vector<float>& p) {  // the largest dip and bump beside the step, levels
    float dip = 0, bump = 0;
    for (int x = 116; x < 126; ++x) dip = std::max(dip, mean(p, 30, 100) - p[size_t(x)]);
    for (int x = 131; x < 141; ++x) bump = std::max(bump, p[size_t(x)] - mean(p, 160, 230));
    return std::pair{255.0f * dip, 255.0f * bump};
  };
  tv::PipelineOptions o;
  o.detail = true;
  o.denoise = false;
  o.destripe = false;      // a perfectly vertical step is all columns: leave it to stage 6
  o.unsharpAmount = 0.0f;  // the texture gain alone (the unsharp pass is clamped to the 3x3 range)
  const auto [dip, bump] = rims(run(o));
  CHECK(dip < 0.5f);
  CHECK(bump < 0.5f);
  // Without the guard the rims show, so the checks above mean something.
  o.detailEdgeLo = o.detailEdgeHi = 1e9f;
  const auto [bareDip, bareBump] = rims(run(o));
  CHECK(bareDip > 1.0f);
  CHECK(bareBump > 1.0f);
}
