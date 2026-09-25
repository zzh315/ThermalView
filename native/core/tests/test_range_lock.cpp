#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

#include "doctest.h"
#include "tv/range_lock.h"

namespace fs = std::filesystem;
using tv::FixedMapping;
using tv::RangeLock;
using tv::TemperatureLut;
using tv::ToneMapper;

namespace {

tv::TemperatureInputs goldenInputs() {
  std::vector<uint16_t> frame(tv::kFramePixels, 0);
  std::ifstream in(fs::path(TV_GOLDEN_DIR) / "water_201315_f0000.meta", std::ios::binary);
  in.read(reinterpret_cast<char*>(frame.data() + tv::kImagePixels),
          std::streamsize((tv::kFramePixels - tv::kImagePixels) * sizeof(uint16_t)));
  return tv::temperatureInputs(tv::FrameView(frame.data()));
}

TemperatureLut lutOf(const tv::TemperatureInputs& in) {
  TemperatureLut lut;
  lut.build(in);
  return lut;
}

// A scene in counts: a smooth ramp with two warm patches, so equalization bends the curve.
std::vector<float> scene(const TemperatureLut& lut) {
  std::vector<float> s(tv::kImagePixels);
  const double base = tv::countsAt(lut, 20.0), top = tv::countsAt(lut, 30.0);
  for (int y = 0; y < tv::kImageRows; ++y)
    for (int x = 0; x < tv::kFrameWidth; ++x) {
      double v = base + (top - base) * 0.3 * x / (tv::kFrameWidth - 1);
      if (x > 40 && x < 80 && y > 40 && y < 90) v = top;
      if (x > 150 && x < 170) v = base + 0.8 * (top - base);
      s[size_t(y) * tv::kFrameWidth + size_t(x)] = float(v);
    }
  return s;
}

// A fixed mapping's intensity at some counts (ToneMapper::apply's interpolation, before the squeeze).
double intensity(const FixedMapping& m, double counts) {
  const int K = ToneMapper::kCurve;
  const double t = std::clamp((counts - m.lo) / (m.hi - m.lo) * K, 0.0, double(K));
  const int k = std::min(int(t), K - 1);
  return m.curve[size_t(k)] + (t - k) * (m.curve[size_t(k + 1)] - m.curve[size_t(k)]);
}

}  // namespace

TEST_CASE("celsiusAt and countsAt invert each other where the table is valid") {
  const auto lut = lutOf(goldenInputs());
  for (double t : {-10.0, 0.0, 21.37, 55.5, 99.9}) {
    const double c = tv::countsAt(lut, t);
    CHECK(tv::celsiusAt(lut, c) == doctest::Approx(t).epsilon(1e-9));
  }
  CHECK(std::isnan(tv::celsiusAt(lut, lut.vertex())));  // no temperature at the fold
  CHECK(tv::countsAt(lut, 1e6) == double(TemperatureLut::kSize - 1));  // clamped above the table
}

TEST_CASE("locking keeps what's on screen") {
  const auto lut = lutOf(goldenInputs());
  const auto sig = scene(lut);
  ToneMapper tone;
  std::vector<float> autoOut(tv::kImagePixels), lockedOut(tv::kImagePixels);
  for (int f = 0; f < 60; ++f) tone.map(sig.data(), autoOut.data());  // settled
  RangeLock lock;
  REQUIRE(lock.lock(tone, lut));
  CHECK(lock.loC() == doctest::Approx(tv::celsiusAt(lut, tone.lowCounts())));
  CHECK(lock.hiC() == doctest::Approx(tv::celsiusAt(lut, tone.highCounts())));
  FixedMapping m;
  REQUIRE(lock.mapping(lut, &m));
  tone.setFixed(&m);
  CHECK(tone.fixed());
  tone.map(sig.data(), lockedOut.data());
  float worst = 0;
  for (size_t i = 0; i < tv::kImagePixels; ++i) worst = std::max(worst, std::abs(lockedOut[i] - autoOut[i]));
  CHECK(worst < 0.5f / 255.0f);  // below half a display level
}

TEST_CASE("a locked mapping holds temperatures, not counts") {
  const auto in = goldenInputs();
  const auto lut = lutOf(in);
  auto warmer = in;  // the camera warmed up: the same temperatures now read at other counts
  warmer.fpaC += 4.0;
  warmer.shutterC += 4.0;
  const auto lut2 = lutOf(warmer);
  REQUIRE(std::abs(tv::countsAt(lut2, 25.0) - tv::countsAt(lut, 25.0)) > 5.0);

  ToneMapper tone;
  std::vector<float> out(tv::kImagePixels);
  const auto sig = scene(lut);
  for (int f = 0; f < 60; ++f) tone.map(sig.data(), out.data());
  RangeLock lock;
  REQUIRE(lock.lock(tone, lut));
  FixedMapping a, b;
  REQUIRE(lock.mapping(lut, &a));
  REQUIRE(lock.mapping(lut2, &b));
  for (double t = lock.loC(); t <= lock.hiC(); t += (lock.hiC() - lock.loC()) / 17.0)
    CHECK(intensity(b, tv::countsAt(lut2, t)) == doctest::Approx(intensity(a, tv::countsAt(lut, t))).epsilon(0.003));
}

TEST_CASE("new ends scale the held curve between them, at least kMinSpanC apart") {
  const auto lut = lutOf(goldenInputs());
  ToneMapper tone;
  std::vector<float> out(tv::kImagePixels);
  const auto sig = scene(lut);
  for (int f = 0; f < 60; ++f) tone.map(sig.data(), out.data());
  RangeLock lock;
  REQUIRE(lock.lock(tone, lut));
  FixedMapping before, after;
  REQUIRE(lock.mapping(lut, &before));
  const double lo = lock.loC(), hi = lock.hiC();
  const double mid = tv::celsiusAt(lut, before.lo + 0.37 * (before.hi - before.lo));  // some temperature inside

  lock.setEnds(lo - 5.0, hi + 5.0);
  REQUIRE(lock.mapping(lut, &after));
  CHECK(tv::celsiusAt(lut, after.lo) == doctest::Approx(lo - 5.0).epsilon(1e-6));
  CHECK(tv::celsiusAt(lut, after.hi) == doctest::Approx(hi + 5.0).epsilon(1e-6));
  // The same point of the shape sits at the same fraction of the new span.
  const double moved = (lo - 5.0) + (mid - lo) * (hi - lo + 10.0) / (hi - lo);
  CHECK(intensity(after, tv::countsAt(lut, moved)) ==
        doctest::Approx(intensity(before, tv::countsAt(lut, mid))).epsilon(0.003));

  lock.setEnds(30.0, 30.1);
  CHECK(lock.hiC() - lock.loC() == doctest::Approx(RangeLock::kMinSpanC));
  CHECK(0.5 * (lock.loC() + lock.hiC()) == doctest::Approx(30.05));
}

TEST_CASE("after the lock, the scene's own mapping is back from the next frame") {
  const auto lut = lutOf(goldenInputs());
  const auto sig = scene(lut);
  ToneMapper tone;
  std::vector<float> out(tv::kImagePixels);
  for (int f = 0; f < 60; ++f) tone.map(sig.data(), out.data());
  RangeLock lock;
  REQUIRE(lock.lock(tone, lut));
  lock.setEnds(lock.loC() - 20.0, lock.hiC() + 20.0);  // a very different, wide range
  FixedMapping m;
  REQUIRE(lock.mapping(lut, &m));
  for (int f = 0; f < 10; ++f) {
    tone.setFixed(&m);
    tone.map(sig.data(), out.data());
  }
  const float lockedMean = [&] { double s = 0; for (float v : out) s += v; return float(s / out.size()); }();
  tone.setFixed(nullptr);
  CHECK_FALSE(tone.fixed());
  tone.map(sig.data(), out.data());
  CHECK(tone.globalOffset() == doctest::Approx(0.0f));  // carried on from the locked frames
  ToneMapper fresh;  // what a start on this scene shows
  std::vector<float> ref(tv::kImagePixels);
  fresh.map(sig.data(), ref.data());
  float worst = 0, change = 0;
  for (size_t i = 0; i < tv::kImagePixels; ++i) worst = std::max(worst, std::abs(out[i] - ref[i]));
  CHECK(worst < 0.5f / 255.0f);
  const float autoMean = [&] { double s = 0; for (float v : out) s += v; return float(s / out.size()); }();
  change = std::abs(autoMean - lockedMean);
  CHECK(change > 0.01f);  // (the lock's wide range did look different)
}

TEST_CASE("nothing to lock before a mapping exists") {
  const auto lut = lutOf(goldenInputs());
  ToneMapper tone;
  RangeLock lock;
  CHECK_FALSE(lock.lock(tone, lut));
  CHECK_FALSE(lock.held());
  FixedMapping m;
  CHECK_FALSE(lock.mapping(lut, &m));
}
