#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "doctest.h"
#include "tv/frame.h"
#include "tv/pipeline.h"
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

TEST_CASE("estimateShift stays put when nothing is left to track, where plain iteration wanders") {
  // A scene of column and row structure only (a gradient, a vertical wave, a column pattern): after
  // the unstriping both images are pure noise. Iterated Lucas-Kanade walks to a random peak of the
  // noise's correlation, pixels away (and stage 3c's reference with it); the score test stays at zero.
  std::mt19937 rng(23);
  std::normal_distribution<float> n(0.0f, 1.0f);
  std::vector<float> a(tv::kImagePixels), b(tv::kImagePixels), pattern(W), scratch;
  double worstGuarded = 0.0, worstPlain = 0.0;
  for (int trial = 0; trial < 20; ++trial) {
    for (float& p : pattern) p = 0.8f * n(rng);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        const float s = 5000.0f + 0.1f * x + 4.0f * std::sin(y * 0.05f) + pattern[size_t(x)];
        a[size_t(y) * W + x] = s + 0.2f * n(rng);  // the reference: an average, less noisy
        b[size_t(y) * W + x] = s + 1.07f * n(rng);
      }
    const auto guarded = tv::estimateShift(a.data(), b.data(), scratch, {0.0, 0.0}, 1.07);
    const auto plain = tv::estimateShift(a.data(), b.data(), scratch, {0.0, 0.0}, 1.07, nullptr, 0.0);
    worstGuarded = std::max(worstGuarded, std::hypot(guarded[0], guarded[1]));
    worstPlain = std::max(worstPlain, std::hypot(plain[0], plain[1]));
  }
  MESSAGE("worst motion found in pure noise: " << worstGuarded << " px with the score test, " << worstPlain
                                                << " px without");
  CHECK(worstGuarded < 0.05);
  CHECK(worstPlain > 1.0);  // (so the scene is one that makes plain iteration wander)
}

TEST_CASE("the pipeline hands stage 3c its options, from the constructor and from setOptions") {
  tv::PipelineOptions o;
  CHECK(tv::parseStages("stripes,stripesMaxMotion=0.25,stripesCompensate=0,stripesTau=30", &o));
  tv::Pipeline p(o);
  CHECK(p.stripeOptionsInUse().maxMotion == doctest::Approx(0.25f));
  CHECK_FALSE(p.stripeOptionsInUse().compensate);
  CHECK(p.stripeOptionsInUse().tauFrames == doctest::Approx(30.0f));
  o.stripeOptions.maxMotion = 0.5f;
  p.setOptions(o);
  CHECK(p.stripeOptionsInUse().maxMotion == doctest::Approx(0.5f));
}

TEST_CASE("stage 3c doesn't hold back stage 3b: while 3b learns a persistent pattern, the output's pattern isn't stronger with 3c") {
  // The owner saw it (flat_aged, 2026-09-25): 3c took 3b's shrinking correction for stripes, undid
  // it, and the older, stronger pattern lingered. A persistent column pattern 3b learns over ~2 s
  // (3b starts from zero) plus per-frame stripes: in every window while 3b learns and after, the
  // output's lasting column pattern with 3c must not exceed the one without, and its per-frame part
  // must be much smaller. The scene has nothing but column and row structure, so this also fails if
  // 3c's motion estimate wanders (its reference then smears the pattern and the stripes return).
  std::mt19937 rng(17);
  std::normal_distribution<float> n(0.0f, 1.07f);
  std::vector<float> scene(tv::kImagePixels), persistent(W);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) scene[size_t(y) * W + x] = 5000.0f + 0.1f * x + 4.0f * std::sin(y * 0.05f);
  for (int x = 0; x < W; ++x) persistent[size_t(x)] = 0.8f * n(rng);  // ~0.86 counts, fixed to the sensor
  tv::PipelineOptions base;
  base.nr = base.tone = base.detail = base.badPixels = base.drift = false;
  tv::PipelineOptions with3c = base;
  with3c.stripes = true;
  tv::Pipeline a(base), b(with3c);
  Stripes st;
  std::vector<uint16_t> img(tv::kImagePixels);
  std::vector<float> d(tv::kImagePixels), sa(tv::kImagePixels), sb(tv::kImagePixels);
  struct Window {
    int from, to;
    std::vector<double> ma, mb;
    std::vector<std::vector<float>> fa, fb;
  };
  std::vector<Window> windows = {{10, 30, {}, {}, {}, {}}, {30, 60, {}, {}, {}, {}}, {60, 200, {}, {}, {}, {}}};
  for (auto& w : windows) w.ma.assign(tv::kImagePixels, 0.0), w.mb.assign(tv::kImagePixels, 0.0);
  for (int t = 0; t < 200; ++t) {
    st.step();
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x)
        img[size_t(y) * W + x] = uint16_t(std::lround(scene[size_t(y) * W + x] + persistent[size_t(x)] +
                                                      st.col[size_t(x)] + st.row[size_t(y)] + n(rng)));
    a.process(img.data(), d.data(), sa.data());
    b.process(img.data(), d.data(), sb.data());
    for (auto& w : windows)
      if (t >= w.from && t < w.to) {
        std::vector<float> ea(tv::kImagePixels), eb(tv::kImagePixels);
        for (size_t i = 0; i < tv::kImagePixels; ++i) {
          ea[i] = sa[i] - scene[i];
          eb[i] = sb[i] - scene[i];
          w.ma[i] += ea[i] / (w.to - w.from);
          w.mb[i] += eb[i] / (w.to - w.from);
        }
        w.fa.push_back(ea);
        w.fb.push_back(eb);
      }
  }
  for (auto& w : windows) {
    std::vector<float> ma(w.ma.begin(), w.ma.end()), mb(w.mb.begin(), w.mb.end());
    double flickA = 0.0, flickB = 0.0;
    for (size_t k = 0; k < w.fa.size(); ++k) {
      std::vector<float> ea(tv::kImagePixels), eb(tv::kImagePixels);
      for (size_t i = 0; i < tv::kImagePixels; ++i) {
        ea[i] = w.fa[k][i] - ma[i];
        eb[i] = w.fb[k][i] - mb[i];
      }
      flickA += fineColumns(ea) / double(w.fa.size());
      flickB += fineColumns(eb) / double(w.fb.size());
    }
    CAPTURE(w.from);
    MESSAGE("frames " << w.from << "-" << w.to << ": lasting fine column pattern " << fineColumns(ma) << " -> "
                      << fineColumns(mb) << "; per-frame " << flickA << " -> " << flickB);
    CHECK(fineColumns(mb) <= fineColumns(ma) * 1.05 + 0.01);
    CHECK(flickB < 0.4 * flickA);
  }
}
