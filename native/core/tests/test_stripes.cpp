#include <cmath>
#include <random>
#include <vector>

#include "doctest.h"
#include "tv/frame.h"
#include "tv/stripes.h"

// Stage 3c: the per-frame column and row noise (tv/stripes.h).
namespace {

constexpr int W = tv::kFrameWidth, H = tv::kImageRows;

// A scene with structure at several scales (so motion can be found), in counts.
std::vector<float> scene(uint32_t seed) {
  std::mt19937 rng(seed);
  std::normal_distribution<float> n(0.0f, 1.0f);
  std::vector<float> s(tv::kImagePixels);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      float v = 5000.0f + 30.0f * std::sin(x * 0.11f) * std::cos(y * 0.07f) + 12.0f * std::sin(x * 0.37f + y * 0.23f);
      if ((x - 150) * (x - 150) + (y - 90) * (y - 90) < 400) v += 40.0f;  // a warm disc
      s[size_t(y) * W + x] = v;
    }
  // a little random texture, smoothed over a few pixels
  std::vector<float> t(tv::kImagePixels);
  for (float& v : t) v = n(rng);
  for (int y = 1; y < H - 1; ++y)
    for (int x = 1; x < W - 1; ++x) {
      float a = 0.0f;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) a += t[size_t(y + dy) * W + x + dx];
      s[size_t(y) * W + x] += 1.5f * a;
    }
  return s;
}

// The sensor's per-frame column and row offsets: AR(1) in time (0.77 frame to frame, as measured).
struct Stripes {
  std::mt19937 rng{11};
  std::normal_distribution<float> n{0.0f, 1.0f};
  std::vector<float> col = std::vector<float>(W, 0.0f), row = std::vector<float>(H, 0.0f);
  void step(float colSd = 0.3f, float rowSd = 0.2f) {
    const float r = 0.77f, k = std::sqrt(1.0f - r * r);
    for (float& c : col) c = r * c + k * colSd * n(rng);
    for (float& v : row) v = r * v + k * rowSd * n(rng);
  }
};

// The fine part of a column profile (less its 9-wide running mean): what reads as streaks.
double fineColumns(const std::vector<float>& img) {
  std::vector<double> m(W, 0.0);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) m[size_t(x)] += img[size_t(y) * W + x] / H;
  double s = 0.0;
  for (int x = 4; x < W - 4; ++x) {
    double avg = 0.0;
    for (int k = -4; k <= 4; ++k) avg += m[size_t(x + k)] / 9.0;
    s += (m[size_t(x)] - avg) * (m[size_t(x)] - avg);
  }
  return std::sqrt(s / (W - 8));
}

}  // namespace

TEST_CASE("shiftImage: whole-pixel shifts are exact, no shift is a copy") {
  const auto s = scene(1);
  std::vector<float> out(tv::kImagePixels), scratch;
  tv::shiftImage(s.data(), out.data(), W, H, 0.0, 0.0, scratch);
  CHECK(out == s);
  tv::shiftImage(s.data(), out.data(), W, H, 3.0, -2.0, scratch);
  for (int y = 10; y < H - 10; ++y)
    for (int x = 10; x < W - 10; ++x) CHECK(out[size_t(y) * W + x] == doctest::Approx(s[size_t(y + 2) * W + x - 3]).epsilon(1e-6));
}

TEST_CASE("estimateShift finds sub-pixel motion through noise, and stripes don't read as motion") {
  const auto s = scene(2);
  std::mt19937 rng(5);
  std::normal_distribution<float> n(0.0f, 1.07f);
  std::vector<float> scratch, a(tv::kImagePixels), b(tv::kImagePixels);
  Stripes st;
  for (auto [dx, dy] : {std::pair{0.0, 0.0}, std::pair{0.3, 0.0}, std::pair{-1.2, 0.6}, std::pair{2.5, -1.5}}) {
    CAPTURE(dx);
    CAPTURE(dy);
    tv::shiftImage(s.data(), b.data(), W, H, dx, dy, scratch);
    st.step();
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        a[size_t(y) * W + x] = s[size_t(y) * W + x] + n(rng);
        b[size_t(y) * W + x] += n(rng) + 5.0f * (st.col[size_t(x)] + st.row[size_t(y)]);  // strong stripes, fixed to the sensor
      }
    const auto d = tv::estimateShift(a.data(), b.data(), scratch);
    // Half resolution: to ~0.07 px at worst on the room scene (exact Fourier shifts; Lucas-Kanade's
    // own ~0.035 px at its finest level, which is half the frame), a little more on this scene's
    // 15 px periodic pattern, sharper than real ones. The stripe fix through a pan is as good with it
    // as with the true motion (the pan test below; PIPELINE_LOG).
    CHECK(std::fabs(d[0] - dx) < 0.12);
    CHECK(std::fabs(d[1] - dy) < 0.12);
  }
}

TEST_CASE("stage 3c removes the per-frame stripes of a still scene and leaves the scene alone") {
  const auto s = scene(3);
  std::mt19937 rng(7);
  std::normal_distribution<float> n(0.0f, 1.07f);
  Stripes st;
  tv::FrameStripes fs;
  std::vector<float> f(tv::kImagePixels), errIn(tv::kImagePixels), errOut(tv::kImagePixels), meanOut(tv::kImagePixels, 0.0f),
      meanIn(tv::kImagePixels, 0.0f);
  double inSum = 0.0, outSum = 0.0;
  int counted = 0;
  for (int t = 0; t < 150; ++t) {
    st.step();
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) f[size_t(y) * W + x] = s[size_t(y) * W + x] + st.col[size_t(x)] + st.row[size_t(y)] + n(rng);
    for (size_t i = 0; i < tv::kImagePixels; ++i) errIn[i] = f[i] - s[i];
    fs.process(f.data(), 1.07f);
    fs.updateReference();
    if (t < 50) continue;
    for (size_t i = 0; i < tv::kImagePixels; ++i) {
      errOut[i] = f[i] - s[i];
      meanOut[i] += (f[i] - s[i]) / 100.0f;
      meanIn[i] += errIn[i] / 100.0f;
    }
    inSum += fineColumns(errIn);
    outSum += fineColumns(errOut);
    ++counted;
  }
  CHECK(fs.lastCorrected());
  MESSAGE("fine column streaks " << inSum / counted << " -> " << outSum / counted);
  CHECK(outSum < 0.5 * inSum);  // at least half of the streaks gone
  // No lasting pattern of its own: over 100 frames the stripes' own slow part doesn't average out
  // (AR(1) at 0.77), so the output's lasting pattern is compared with the input's.
  MESSAGE("lasting fine column pattern " << fineColumns(meanIn) << " -> " << fineColumns(meanOut));
  CHECK(fineColumns(meanOut) <= fineColumns(meanIn) + 0.01);
}

TEST_CASE("stage 3c keeps working through a pan, and adds nothing of the scene") {
  const auto s = scene(4);
  std::mt19937 rng(9);
  std::normal_distribution<float> n(0.0f, 1.07f);
  Stripes st;
  tv::FrameStripes fs;
  std::vector<float> truth(tv::kImagePixels), f(tv::kImagePixels), errIn(tv::kImagePixels), errOut(tv::kImagePixels), scratch;
  double inSum = 0.0, outSum = 0.0;
  for (int t = 0; t < 120; ++t) {
    const double pos = 0.4 * t;  // 0.4 px a frame
    tv::shiftImage(s.data(), truth.data(), W, H, -pos, 0.0, scratch);
    st.step();
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x)
        f[size_t(y) * W + x] = truth[size_t(y) * W + x] + st.col[size_t(x)] + st.row[size_t(y)] + n(rng);
    for (size_t i = 0; i < tv::kImagePixels; ++i) errIn[i] = f[i] - truth[i];
    fs.process(f.data(), 1.07f);
    fs.updateReference();
    if (t < 40) continue;
    CHECK(fs.lastShift()[0] == doctest::Approx(-0.4).epsilon(0.2));
    for (size_t i = 0; i < tv::kImagePixels; ++i) errOut[i] = f[i] - truth[i];
    inSum += fineColumns(errIn);
    outSum += fineColumns(errOut);
  }
  MESSAGE("during the pan: fine column error " << inSum / 80 << " -> " << outSum / 80);
  CHECK(outSum < 0.6 * inSum);
}

TEST_CASE("stage 3c: a warm object moving across a still scene doesn't make stripes of its own") {
  const auto s = scene(5);
  std::mt19937 rng(13);
  std::normal_distribution<float> n(0.0f, 1.07f);
  tv::FrameStripes fs;
  std::vector<float> truth(tv::kImagePixels), f(tv::kImagePixels), err(tv::kImagePixels);
  double worst = 0.0;
  for (int t = 0; t < 120; ++t) {
    truth = s;
    const int left = 20 + t;  // a 30 x 60 warm block, 1 px a frame, no stripes at all
    for (int y = 60; y < 120; ++y)
      for (int x = left; x < left + 30 && x < W; ++x) truth[size_t(y) * W + x] += 25.0f;
    for (size_t i = 0; i < tv::kImagePixels; ++i) f[i] = truth[i] + n(rng);
    fs.process(f.data(), 1.07f);
    fs.updateReference();
    if (t < 40) continue;
    for (size_t i = 0; i < tv::kImagePixels; ++i) err[i] = f[i] - truth[i];
    worst = std::max(worst, fineColumns(err));
  }
  MESSAGE("worst fine column error with the moving block (no real stripes) " << worst);
  CHECK(worst < 0.12);  // (white noise alone: ~0.08)
}
