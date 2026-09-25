#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

#include "doctest.h"
#include "tv/bm3d.h"
#include "tv/frame.h"
#include "tv/pipeline.h"

// Stage 4b's second method: BM3D, the CPU reference (tv/bm3d.h).
namespace {

constexpr int W = tv::kFrameWidth, H = tv::kImageRows;

// Counts: flat left third, a 6-count checkerboard of 3 px squares in the middle, a 60-count
// lens-blurred step on the right (stage 4b's NLM test scene).
std::vector<float> clean() {
  std::vector<float> s(tv::kImagePixels);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      float v = 6000.0f;
      if (x >= 85 && x < 170) v += (x / 3 + y / 3) % 2 ? 6.0f : 0.0f;
      if (x >= 170) v += 60.0f / (1.0f + std::exp(-(float(x) - 213.0f)));
      s[size_t(y) * W + x] = v;
    }
  return s;
}

std::vector<float> withNoise(std::vector<float> s, float sigma, uint32_t seed) {
  std::mt19937 rng(seed);
  std::normal_distribution<float> n(0.0f, sigma);
  for (float& v : s) v += n(rng);
  return s;
}

std::vector<float> filtered(const std::vector<float>& src, float sigma, const tv::Bm3dOptions& o) {
  std::vector<float> dst(tv::kImagePixels);
  tv::bm3d(src.data(), dst.data(), sigma, o);
  return dst;
}

double stdIn(const std::vector<float>& s, int x0, int x1) {  // spatial std in a column band, rows 10-180
  double m = 0, v = 0;
  int n = 0;
  for (int y = 10; y < 180; ++y)
    for (int x = x0; x < x1; ++x) m += s[size_t(y) * W + x], ++n;
  m /= n;
  for (int y = 10; y < 180; ++y)
    for (int x = x0; x < x1; ++x) v += std::pow(s[size_t(y) * W + x] - m, 2);
  return std::sqrt(v / n);
}

double contrast(const std::vector<float>& s) {  // the checkerboard's: its high squares' mean less the low ones'
  double hi = 0, lo = 0;
  int nh = 0, nl = 0;
  for (int y = 12; y < 180; ++y)
    for (int x = 90; x < 165; ++x)
      if ((x % 3) == 1 && (y % 3) == 1) {  // square centres
        ((x / 3 + y / 3) % 2 ? hi : lo) += s[size_t(y) * W + x];
        ++((x / 3 + y / 3) % 2 ? nh : nl);
      }
  return hi / nh - lo / nl;
}

double rise(const std::vector<float>& s) {  // the step's 10-90% rise, px, from the mean profile over rows
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
}

double fineColumns(const std::vector<float>& img, int x0 = 4, int x1 = W - 4) {  // column profile less its 9-wide mean
  std::vector<double> m(W, 0.0);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) m[size_t(x)] += img[size_t(y) * W + x] / H;
  double s = 0.0;
  for (int x = x0; x < x1; ++x) {
    double avg = 0.0;
    for (int k = -4; k <= 4; ++k) avg += m[size_t(x + k)] / 9.0;
    s += (m[size_t(x)] - avg) * (m[size_t(x)] - avg);
  }
  return std::sqrt(s / (x1 - x0));
}

tv::Bm3dOptions quick() {  // the reference's parameters, with a smaller search window (the tests' time)
  tv::Bm3dOptions o;
  o.search = 7;
  return o;
}

tv::Bm3dOptions realtime() {  // the real-time candidate (PIPELINE_LOG: BM3D's cost sweep)
  tv::Bm3dOptions o;
  o.stride = 6;
  o.search = 5;
  o.group1 = o.group2 = 8;
  return o;
}

}  // namespace

TEST_CASE("BM3D: sigma 0 is a copy") {
  const auto src = withNoise(clean(), 1.5f, 1);
  CHECK(filtered(src, 0.0f, quick()) == src);
}

TEST_CASE("BM3D cuts white noise and keeps a fine texture and an edge") {
  const auto truth = clean();
  const auto src = withNoise(truth, 1.5f, 3);
  for (const auto& [name, o] : {std::pair{std::string("reference"), quick()}, std::pair{std::string("real-time"), realtime()}}) {
    CAPTURE(name);
    const auto out = filtered(src, 1.5f, o);
    MESSAGE(name << ": flat noise " << stdIn(src, 10, 75) << " -> " << stdIn(out, 10, 75) << "; checkerboard "
                 << contrast(src) << " -> " << contrast(out) << " (true " << contrast(truth) << "); rise "
                 << rise(src) << " -> " << rise(out));
    CHECK(stdIn(out, 10, 75) < 0.2 * stdIn(src, 10, 75));
    CHECK(contrast(out) > 0.9 * contrast(truth));
    CHECK(std::fabs(rise(out) - rise(truth)) < 0.2);
  }
}

TEST_CASE("BM3D doesn't depend on the level") {
  auto src = withNoise(clean(), 1.5f, 5);
  for (float& v : src) v = std::round(v);  // counts, so the raised image is exact too
  auto raised = src;
  for (float& v : raised) v += 1000.0f;
  const auto a = filtered(src, 1.5f, realtime()), b = filtered(raised, 1.5f, realtime());
  double worst = 0.0;
  for (size_t i = 0; i < a.size(); ++i) worst = std::max(worst, double(std::fabs(b[i] - 1000.0f - a[i])));
  MESSAGE("worst difference, raised by 1000 counts: " << worst);
  CHECK(worst < 1e-3);
}

TEST_CASE("BM3D keeps a faint, wide level difference: each group's mean stays as it is") {
  // 0.2 counts between the halves, under the noise of a group's mean: thresholded like the other
  // coefficients, the group means pull both halves toward the image's mean (0.144-0.168 over five
  // seeds; kept, 0.199-0.214).
  std::vector<float> truth(tv::kImagePixels);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) truth[size_t(y) * W + x] = 6000.0f + (x < W / 2 ? 0.2f : 0.0f);
  const auto out = filtered(withNoise(truth, 1.07f, 13), 1.07f, realtime());
  double left = 0.0, right = 0.0;
  int n = 0;
  for (int y = 0; y < H; ++y)
    for (int x = 8; x < 110; ++x, ++n) {
      left += out[size_t(y) * W + x];
      right += out[size_t(y) * W + x + 138];
    }
  MESSAGE("the halves' difference: " << (left - right) / n << " (true 0.2)");
  CHECK((left - right) / n == doctest::Approx(0.2).epsilon(0.12));
}

TEST_CASE("BM3D with the noise's covariance removes the column noise the white form keeps") {
  // The sensor's per-frame column noise is the same down a column: the white form matches blocks that
  // share it and keeps it; the correlated form, told R, takes it off like the grain.
  const float white = 1.07f, column = 0.5f;
  std::mt19937 rng(9);
  std::normal_distribution<float> n(0.0f, 1.0f);
  std::vector<float> truth(tv::kImagePixels, 6000.0f), src(tv::kImagePixels), offsets(W);
  for (float& c : offsets) c = column * n(rng);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) src[size_t(y) * W + x] = truth[size_t(y) * W + x] + offsets[size_t(x)] + white * n(rng);
  tv::NoiseCovariance cov;
  cov.radius = 8;
  const int side = 2 * cov.radius + 1;
  cov.values.assign(size_t(side) * side, 0.0f);
  for (int dy = -cov.radius; dy <= cov.radius; ++dy) cov.values[size_t(dy + cov.radius) * side + cov.radius] = column * column;
  cov.values[size_t(cov.radius) * side + cov.radius] += white * white;
  tv::Bm3dOptions o = quick();
  std::vector<float> plain = filtered(src, std::sqrt(white * white + column * column), o), aware(tv::kImagePixels);
  tv::Bm3dOptions oc = o;
  oc.lambda = 3.2f;  // the correlated-noise values (Makinen, Azzari and Foi 2020; PIPELINE_LOG)
  oc.mu2 = 0.8f;
  tv::bm3d(src.data(), aware.data(), cov, 1.0f, oc);
  MESSAGE("column streaks " << fineColumns(src) << " -> white form " << fineColumns(plain) << ", correlated form "
                            << fineColumns(aware) << "; flat noise " << stdIn(src, 10, 246) << " -> "
                            << stdIn(plain, 10, 246) << ", " << stdIn(aware, 10, 246));
  CHECK(fineColumns(aware) < 0.5 * fineColumns(plain));
}

TEST_CASE("stage 4b runs BM3D on the CPU when asked, at bm3dStrength x sigma") {
  tv::PipelineOptions o;
  CHECK(tv::parseStages("nr,nrMethod=bm3d,bm3dStrength=1.2,bm3dStride=6,bm3dSearch=5,bm3dGroup1=8,bm3dGroup2=8", &o));
  CHECK(o.nrMethod == 1);
  CHECK(o.bm3dStrength == doctest::Approx(1.2f));
  CHECK(o.bm3d.stride == 6);
  CHECK(o.bm3d.group2 == 8);
  o.shutterHold = o.badPixels = o.drift = o.destripe = o.denoise = o.tone = o.detail = false;
  o.nrSigma = 1.5f;
  tv::Pipeline p(o);
  const auto src = withNoise(clean(), 1.5f, 7);
  std::vector<uint16_t> img(tv::kImagePixels);
  std::vector<float> asCounts(tv::kImagePixels);
  for (size_t i = 0; i < img.size(); ++i) asCounts[i] = float(img[i] = uint16_t(std::lround(src[i])));
  std::vector<float> d(tv::kImagePixels), sig(tv::kImagePixels);
  p.process(img.data(), d.data(), sig.data());
  const auto expected = filtered(asCounts, 1.2f * 1.5f, o.bm3d);
  double worst = 0.0;
  for (size_t i = 0; i < sig.size(); ++i) worst = std::max(worst, double(std::fabs(sig[i] - expected[i])));
  MESSAGE("worst difference from a direct call: " << worst);
  CHECK(worst < 1e-3);
}
