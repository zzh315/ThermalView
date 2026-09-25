#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <functional>
#include <string>
#include <vector>

#include "doctest.h"
#include "tv/filters.h"
#include "tv/pipeline.h"

// Stage 4b: spatial noise reduction (non-local means), and its noise-sigma estimate.
namespace {
constexpr int W = tv::kFrameWidth, H = tv::kImageRows;

tv::PipelineOptions nrOnly() {  // stage 4b alone, the display as the baseline stretch (counts come back as signal)
  tv::PipelineOptions o;
  o.shutterHold = o.badPixels = o.drift = o.destripe = o.denoise = o.tone = o.detail = false;
  o.nr = true;
  return o;
}

// A scene: flat left third, a fine 6-count checkerboard in the middle, a 60-count lens-blurred step on
// the right; plus Gaussian noise.
std::vector<uint16_t> scene(std::mt19937& rng, float sigma) {
  std::normal_distribution<float> n(0.0f, sigma);
  std::vector<uint16_t> img(tv::kImagePixels);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      float v = 6000.0f;
      if (x >= 85 && x < 170) v += (x / 3 + y / 3) % 2 ? 6.0f : 0.0f;
      if (x >= 170) v += 60.0f / (1.0f + std::exp(-(float(x) - 213.0f)));
      img[size_t(y) * W + x] = uint16_t(std::lround(v + n(rng)));
    }
  return img;
}
}  // namespace

TEST_CASE("stage 4b cuts noise in flat areas and keeps edges and texture") {
  std::mt19937 rng(7);
  auto o = nrOnly();
  o.nrSigma = 1.5f;  // a fixed sigma: this test is about the filter, not the estimate
  tv::Pipeline p(o);
  tv::PipelineOptions off = nrOnly();
  off.nr = false;
  tv::Pipeline q(off);
  std::vector<float> d(tv::kImagePixels), withNr(tv::kImagePixels), without(tv::kImagePixels);
  const auto img = scene(rng, 1.5f);
  p.process(img.data(), d.data(), withNr.data());
  q.process(img.data(), d.data(), without.data());
  auto stdIn = [](const std::vector<float>& s, int x0, int x1) {  // spatial std in a column band, rows 10-180
    double m = 0, v = 0;
    int n = 0;
    for (int y = 10; y < 180; ++y)
      for (int x = x0; x < x1; ++x) m += s[size_t(y) * W + x], ++n;
    m /= n;
    for (int y = 10; y < 180; ++y)
      for (int x = x0; x < x1; ++x) v += std::pow(s[size_t(y) * W + x] - m, 2);
    return std::sqrt(v / n);
  };
  CHECK(stdIn(withNr, 10, 75) < 0.5 * stdIn(without, 10, 75));  // flat: noise more than halved
  // Texture: the checkerboard's contrast (mean of the high squares minus the low ones) is kept.
  auto contrast = [](const std::vector<float>& s) {
    double hi = 0, lo = 0;
    int nh = 0, nl = 0;
    for (int y = 12; y < 180; ++y)
      for (int x = 90; x < 165; ++x) {
        if ((x % 3) == 1 && (y % 3) == 1) {  // square centres
          ((x / 3 + y / 3) % 2 ? hi : lo) += s[size_t(y) * W + x];
          ++((x / 3 + y / 3) % 2 ? nh : nl);
        }
      }
    return hi / nh - lo / nl;
  };
  CHECK(contrast(withNr) > 0.85 * contrast(without));
  // The step's 10-90% rise, from the mean profile over rows: unchanged.
  auto rise = [](const std::vector<float>& s) {
    std::vector<double> prof(W, 0.0);
    for (int y = 10; y < 180; ++y)
      for (int x = 190; x < 236; ++x) prof[size_t(x)] += s[size_t(y) * W + x] / 170.0;
    const double lo = prof[195], hi = prof[231];
    double a = 0, b = 0;
    for (int x = 195; x < 231; ++x) {
      const double f = (prof[size_t(x)] - lo) / (hi - lo), g = (prof[size_t(x + 1)] - lo) / (hi - lo);
      if (f < 0.1 && g >= 0.1) a = x + (0.1 - f) / (g - f);
      if (f < 0.9 && g >= 0.9) b = x + (0.9 - f) / (g - f);
    }
    return b - a;
  };
  CHECK(std::fabs(rise(withNr) - rise(without)) < 0.2);
}

TEST_CASE("stage 4b's noise sigma follows the camera's noise and ignores motion") {
  // Temporally correlated noise, like the camera's own recursive filter (0.72): the estimate, 1.31 x
  // the trimmed RMS of frame differences, should read the per-pixel sigma.
  std::mt19937 rng(11);
  const float sigma = 1.3f, rho = 0.72f;
  std::normal_distribution<float> n(0.0f, sigma * std::sqrt(1.0f - rho * rho));
  std::vector<float> noise(tv::kImagePixels);
  for (float& v : noise) v = std::normal_distribution<float>(0.0f, sigma)(rng);
  auto o = nrOnly();
  tv::Pipeline p(o);
  std::vector<uint16_t> img(tv::kImagePixels);
  std::vector<float> d(tv::kImagePixels);
  auto frame = [&](float shift) {
    for (size_t i = 0; i < tv::kImagePixels; ++i) {
      noise[i] = rho * noise[i] + n(rng);
      const int x = int(i % W);
      img[i] = uint16_t(std::lround(6000.0f + 40.0f * std::sin(0.07f * (float(x) + shift)) + noise[i] + 0.5f));
    }
    p.process(img.data(), d.data());
  };
  for (int k = 0; k < 400; ++k) frame(0.0f);
  CHECK(p.noiseSigma() == doctest::Approx(sigma).epsilon(0.12));
  const float settled = p.noiseSigma();
  for (int k = 0; k < 20; ++k) frame(float(k) * 3.0f);  // a pan: every pixel changes a lot
  CHECK(p.noiseSigma() == doctest::Approx(settled).epsilon(0.05));
}

TEST_CASE("stage 4b uses the accelerator when it works, and the CPU when it doesn't") {
  std::mt19937 rng(13);
  auto o = nrOnly();
  o.nrSigma = 1.5f;
  tv::Pipeline p(o);
  std::vector<float> d(tv::kImagePixels), s(tv::kImagePixels);
  const auto img = scene(rng, 1.5f);
  int starts = 0, finishes = 0, alongside = 0;
  std::vector<std::string> order;
  tv::Pipeline::NoiseReducer gpu;
  gpu.start = [&](const float*, const tv::Pipeline::NoiseRequest& r) {
    ++starts;
    order.push_back("start");
    CHECK(r.method == 0);
    CHECK(r.searchRadius == o.nrSearch);
    CHECK(r.patchRadius == o.nrPatch);
    CHECK(r.h == doctest::Approx(o.nrStrength * 1.5f));
    return true;
  };
  gpu.finish = [&](float* dst) {
    ++finishes;
    order.push_back("finish");
    std::fill(dst, dst + tv::kImagePixels, 1234.0f);  // recognisable
    return true;
  };
  p.setNoiseReducer(gpu);
  const std::function<void()> work = [&] {
    ++alongside;
    order.push_back("alongside");
  };
  p.process(img.data(), d.data(), s.data(), {}, work);
  CHECK(starts == 1);
  CHECK(finishes == 1);
  CHECK(alongside == 1);
  CHECK(order == std::vector<std::string>{"start", "alongside", "finish"});  // the work overlaps the GPU
  CHECK(p.lastNoiseReductionAccelerated());
  CHECK(s[1000] == 1234.0f);
  // A failing accelerator: the CPU filters (the signal is no longer the fake value, and smoother).
  for (bool failAtStart : {true, false}) {
    CAPTURE(failAtStart);
    tv::Pipeline::NoiseReducer broken;
    broken.start = [&](const float*, const tv::Pipeline::NoiseRequest&) { return !failAtStart; };
    broken.finish = [&](float*) { return false; };
    p.setNoiseReducer(broken);
    alongside = 0;
    const auto next = scene(rng, 1.5f);  // (a fresh frame: a repeat would be a shutter cycle)
    p.process(next.data(), d.data(), s.data(), {}, work);
    CHECK(alongside == 1);  // exactly once either way
    CHECK_FALSE(p.lastNoiseReductionAccelerated());
    const size_t flat = size_t(10) * W + 20;  // in the flat third
    CHECK(s[flat] != 1234.0f);
    CHECK(std::fabs(s[flat] - 6000.0f) < 3.0f);
  }
}

TEST_CASE("the work alongside runs exactly once per frame: stage 4b off, and a repeated frame") {
  auto o = nrOnly();
  o.nr = false;
  o.shutterHold = true;
  tv::Pipeline p(o);
  std::mt19937 rng(5);
  std::vector<float> d(tv::kImagePixels);
  int alongside = 0;
  const std::function<void()> work = [&] { ++alongside; };
  const auto img = scene(rng, 1.0f);
  p.process(img.data(), d.data(), nullptr, {}, work);
  CHECK(alongside == 1);
  p.process(img.data(), d.data(), nullptr, {}, work);  // the same image: a shutter cycle's repeat
  CHECK(p.frozen());
  CHECK(alongside == 2);
}

TEST_CASE("with an accelerator, the output is exactly the CPU path's (stage 3b's estimate moves into its window)") {
  // Every approved stage on. A: no accelerator, the CPU filters. B: a fake accelerator running the
  // same CPU filter on what start() was given, with the work alongside (stage 3b's estimate update)
  // in between. Over many frames, with stripes for stage 3b to track, they must agree bit for bit.
  tv::PipelineOptions o;
  o.nrSearch = 2;  // (the fake runs on the CPU)
  tv::Pipeline a(o), b(o);
  std::vector<float> buffer(tv::kImagePixels), scratch;
  tv::NlmPadded padded;
  int sr = 0, pr = 0;
  float hh = 0.0f;
  tv::Pipeline::NoiseReducer fake;
  fake.start = [&](const float* src, const tv::Pipeline::NoiseRequest& r) {
    std::copy(src, src + tv::kImagePixels, buffer.begin());
    sr = r.searchRadius, pr = r.patchRadius, hh = r.h;
    return true;
  };
  fake.finish = [&](float* dst) {
    tv::nlmPad(buffer.data(), sr, pr, &padded);
    tv::nlmBand(padded, dst, 0, tv::kImageRows, sr, pr, hh, scratch);
    return true;
  };
  b.setNoiseReducer(fake);
  std::mt19937 rng(21);
  std::vector<float> da(tv::kImagePixels), db(tv::kImagePixels), sa(tv::kImagePixels), sb(tv::kImagePixels);
  for (int k = 0; k < 40; ++k) {
    auto img = scene(rng, 1.1f);
    for (int y = 0; y < H; ++y)  // a column and a row offset that stage 3b learns
      for (int x = 0; x < W; ++x) img[size_t(y) * W + x] = uint16_t(img[size_t(y) * W + x] + (x == 77 ? 2 : 0) + (y == 50 ? 1 : 0));
    a.process(img.data(), da.data(), sa.data(), {30.0, 25.0});
    b.process(img.data(), db.data(), sb.data(), {30.0, 25.0});
    REQUIRE(b.lastNoiseReductionAccelerated());
    CHECK(std::memcmp(da.data(), db.data(), da.size() * sizeof(float)) == 0);
    CHECK(std::memcmp(sa.data(), sb.data(), sa.size() * sizeof(float)) == 0);
  }
}
