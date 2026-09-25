#include <algorithm>
#include <cmath>
#include <vector>

#include "doctest.h"
#include "tv/tone.h"

namespace {

// Deterministic "noise" of roughly the given std.
std::vector<float> scene(float base, float noise, uint32_t seed, float (*content)(size_t) = nullptr) {
  std::vector<float> s(tv::kImagePixels);
  uint32_t r = seed;
  for (size_t i = 0; i < s.size(); ++i) {
    float n = 0;
    for (int k = 0; k < 4; ++k) {
      r = r * 1664525u + 1013904223u;
      n += float(r >> 8) / float(1 << 24) - 0.5f;
    }
    s[i] = base + noise * 1.73f * n + (content ? content(i) : 0.0f);
  }
  return s;
}

std::pair<float, float> spread(const std::vector<float>& out) {
  std::vector<float> v(out);
  std::sort(v.begin(), v.end());
  return {v[v.size() / 100], v[v.size() * 99 / 100]};
}

}  // namespace

TEST_CASE("a flat scene stays a calm, narrow band instead of stretched noise (the gain cap)") {
  tv::ToneOptions o;
  o.maxGain = 1.0f;
  tv::ToneMapper t(o);
  std::vector<float> out(tv::kImagePixels);
  for (uint32_t k = 0; k < 50; ++k) t.map(scene(5000, 1.2f, k).data(), out.data());
  const auto [lo, hi] = spread(out);
  CHECK(hi - lo < 0.1f);  // ~7 counts of noise at <= 1 level per count
  tv::ToneMapper t2;       // the default cap, 2 levels per count: still a band
  for (uint32_t k = 0; k < 50; ++k) t2.map(scene(5000, 1.2f, k).data(), out.data());
  const auto [lo2, hi2] = spread(out);
  CHECK(hi2 - lo2 < 0.2f);
  CHECK(std::abs(0.5f * (lo + hi) - 0.5f) < 0.05f);  // centred
}

TEST_CASE("two temperature zones are pulled well apart") {
  tv::ToneMapper t;
  std::vector<float> out(tv::kImagePixels);
  auto halves = [](size_t i) { return i < tv::kImagePixels / 2 ? 0.0f : 600.0f; };
  for (uint32_t k = 0; k < 50; ++k) t.map(scene(5000, 1.2f, k, halves).data(), out.data());
  CHECK(out[tv::kImagePixels - 1] - out[0] > 0.6f);
}

TEST_CASE("a calibration's step of the whole frame doesn't change the brightness") {
  tv::ToneMapper t;
  std::vector<float> out(tv::kImagePixels);
  auto ramp = [](size_t i) { return float(i % tv::kFrameWidth); };
  for (uint32_t k = 0; k < 50; ++k) t.map(scene(5000, 1.2f, k, ramp).data(), out.data());
  const float before = out[12345];
  t.map(scene(4962, 1.2f, 50, ramp).data(), out.data());  // -38 counts, like the shutter dump's NUC
  CHECK(std::abs(out[12345] - before) < 0.02f);
}

TEST_CASE("a hot object widens the range within a few frames; it contracts slowly after") {
  tv::ToneMapper t;
  std::vector<float> out(tv::kImagePixels);
  auto ramp = [](size_t i) { return float(i % tv::kFrameWidth); };
  auto hot = [](size_t i) { return float(i % tv::kFrameWidth) + (i < tv::kImagePixels / 10 ? 2000.0f : 0.0f); };
  for (uint32_t k = 0; k < 50; ++k) t.map(scene(5000, 1.2f, k, ramp).data(), out.data());
  const float narrow = t.highCounts() - t.lowCounts();
  for (uint32_t k = 0; k < 5; ++k) t.map(scene(5000, 1.2f, 100 + k, hot).data(), out.data());  // 0.2 s
  const float wide = t.highCounts() - t.lowCounts();
  CHECK(wide > narrow + 1500.0f);
  for (uint32_t k = 0; k < 5; ++k) t.map(scene(5000, 1.2f, 200 + k, ramp).data(), out.data());  // 0.2 s later
  CHECK(t.highCounts() - t.lowCounts() > wide * 0.6f);  // still mostly wide: contracting takes ~1.3 s
}

TEST_CASE("intensityAt is the mapping map() applies to a pixel of that value") {
  std::vector<float> sig(tv::kImagePixels), out(tv::kImagePixels);
  for (size_t i = 0; i < sig.size(); ++i) sig[i] = 5000.0f + float(i % 256) * 2.0f + float((i / 256) % 7);
  tv::ToneMapper tone;
  for (int f = 0; f < 5; ++f) tone.map(sig.data(), out.data());
  for (size_t i : {size_t(3), size_t(1000), size_t(20000), tv::kImagePixels - 1})
    CHECK(tone.intensityAt(sig[i]) == doctest::Approx(out[i]).epsilon(1e-5));
  CHECK(tone.intensityAt(1e6f) == doctest::Approx(tv::ToneOptions{}.outHi));  // clamped at the ends
  CHECK(tone.intensityAt(-1e6f) == doctest::Approx(tv::ToneOptions{}.outLo));
}
