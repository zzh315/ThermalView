#include <cmath>
#include <vector>

#include "doctest.h"
#include "tv/drift.h"
#include "tv/pipeline.h"

TEST_CASE("the drift since calibration comes from FPA minus the shutter temperature") {
  CHECK(tv::driftSinceCalibrationC(31.77, 26.35) == doctest::Approx(5.02));
  CHECK(tv::driftSinceCalibrationC(36.37, 35.95) == doctest::Approx(0.02));  // just after a NUC
}

TEST_CASE("KA1213's drift map loads from the repo: zero mean, rates of a few counts per °C") {
  const tv::DriftMap map = tv::loadDriftMap(TV_DATA_DIR "/drift_KA1213.f32");
  REQUIRE(map.rate.size() == tv::kImagePixels);
  double sum = 0, sq = 0;
  for (float r : map.rate) {
    sum += r;
    sq += double(r) * r;
  }
  const double mean = sum / map.rate.size(), sd = std::sqrt(sq / map.rate.size() - mean * mean);
  CHECK(std::abs(mean) < 0.01);
  CHECK(sd > 3.0);
  CHECK(sd < 8.0);
  CHECK(tv::driftMapFromBytes(map.rate.data(), 10).empty());
}

TEST_CASE("stage 3 removes a pattern that grew by rate x drift, and does nothing when off") {
  tv::DriftMap map;
  map.rate.resize(tv::kImagePixels);
  for (size_t i = 0; i < map.rate.size(); ++i) map.rate[i] = (i % 2 ? 2.0f : -2.0f);  // zero mean
  std::vector<uint16_t> img(tv::kImagePixels);
  const double drift = 5.0;  // FPA 31.40, shutter 26.00, c0 0.40
  for (size_t i = 0; i < img.size(); ++i) img[i] = uint16_t(std::lround(5000 + map.rate[i] * drift));
  tv::PipelineOptions o;
  o.drift = true;
  o.driftScale = 1.0f;
  tv::Pipeline on(o);
  on.setDriftMap(map);
  std::vector<float> disp(tv::kImagePixels), sig(tv::kImagePixels);
  on.process(img.data(), disp.data(), sig.data(), {31.40, 26.00});
  CHECK(on.lastDriftC() == doctest::Approx(5.0));
  CHECK(sig[0] == doctest::Approx(5000.0f));
  CHECK(sig[1] == doctest::Approx(5000.0f));
  tv::Pipeline off;  // stage 3 is off by default
  off.setDriftMap(map);
  off.process(img.data(), disp.data(), sig.data(), {31.40, 26.00});
  CHECK(sig[1] == doctest::Approx(5010.0f));
}

TEST_CASE("stage 3b removes a column offset but only up to its clamp, and starts over after a cycle") {
  tv::PipelineOptions o;
  o.destripe = true;
  o.destripeClamp = 2.0f;
  tv::Pipeline p(o);
  std::vector<float> disp(tv::kImagePixels), sig(tv::kImagePixels);
  auto frame = [](uint32_t seed, float stripe) {
    std::vector<uint16_t> img(tv::kImagePixels);
    uint32_t s = seed;
    for (int y = 0; y < tv::kImageRows; ++y)
      for (int x = 0; x < tv::kFrameWidth; ++x) {
        s = s * 1664525u + 1013904223u;
        const float noise = float(s >> 30);  // 0..3 counts
        img[size_t(y) * tv::kFrameWidth + x] = uint16_t(5000.0f + noise + (x == 100 ? stripe : 0.0f));
      }
    return img;
  };
  auto columnMean = [&](int x) {
    double m = 0, all = 0;
    for (int y = 0; y < tv::kImageRows; ++y) m += sig[size_t(y) * tv::kFrameWidth + x];
    for (float v : sig) all += v;
    return m / tv::kImageRows - all / double(sig.size());
  };
  for (uint32_t k = 0; k < 200; ++k) {  // 8 s of a 1-count stripe: removed
    const auto img = frame(k, 1.0f);
    p.process(img.data(), disp.data(), sig.data());
  }
  CHECK(std::abs(columnMean(100)) < 0.2);
  for (uint32_t k = 0; k < 400; ++k) {  // a 4-count line: only the clamp's 2 counts come off
    const auto img = frame(1000 + k, 4.0f);
    p.process(img.data(), disp.data(), sig.data());
  }
  CHECK(columnMean(100) == doctest::Approx(2.0).epsilon(0.1));
}
