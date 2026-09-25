#include <filesystem>
#include <fstream>
#include <vector>

#include "doctest.h"
#include "tv/readouts.h"

namespace fs = std::filesystem;
using tv::Readouts;
using tv::ReadoutFilter;
using tv::Region;
using tv::TemperatureLut;

namespace {

// A real table: the metadata rows of a golden frame (tools/py/oracle.py output).
TemperatureLut realLut() {
  std::vector<uint16_t> frame(tv::kFramePixels, 0);
  std::ifstream in(fs::path(TV_GOLDEN_DIR) / "water_201315_f0000.meta", std::ios::binary);
  in.read(reinterpret_cast<char*>(frame.data() + tv::kImagePixels),
          std::streamsize((tv::kFramePixels - tv::kImagePixels) * sizeof(uint16_t)));
  TemperatureLut lut;
  lut.build(tv::temperatureInputs(tv::FrameView(frame.data())));
  return lut;
}

struct Image {
  std::vector<uint16_t> px = std::vector<uint16_t>(tv::kImagePixels, 5500);
  uint16_t& at(int x, int y) { return px[size_t(y) * tv::kFrameWidth + x]; }
};

}  // namespace

TEST_CASE("low and high are the region's extremes; center averages the 4 middle pixels") {
  const auto lut = realLut();
  Image img;
  img.at(10, 20) = 7000;
  img.at(200, 150) = 5000;
  img.at(127, 95) = 5600;  // one of the 4 center pixels of the full frame (127-128, 95-96)
  const Readouts r = tv::computeReadouts(img.px.data(), lut, Region{}, tv::overRangeRaw(lut));
  CHECK(r.high.x == 10);
  CHECK(r.high.y == 20);
  CHECK(r.high.tempC == doctest::Approx(lut[7000]));
  CHECK(r.low.x == 200);
  CHECK(r.low.y == 150);
  CHECK(r.low.tempC == doctest::Approx(lut[5000]));
  CHECK(r.center.x == doctest::Approx(127.5));
  CHECK(r.center.y == doctest::Approx(95.5));
  CHECK(r.center.tempC == doctest::Approx((lut[5600] + 3 * lut[5500]) / 4));
  CHECK(r.high.valid());
}

TEST_CASE("pixels at the clip read over range; at or below the vertex, no temperature") {
  const auto lut = realLut();
  Image img;
  img.at(5, 5) = 14192;             // the normal range's clip (docs/DEVICE.md)
  img.at(6, 6) = lut.vertex();      // bottom of the folded table
  const Readouts r = tv::computeReadouts(img.px.data(), lut, Region{}, tv::overRangeRaw(lut));
  CHECK(r.high.overRange);
  CHECK_FALSE(r.high.valid());
  CHECK(std::isnan(r.low.tempC));
  CHECK(r.low.x == 6);
}

TEST_CASE("bad pixels are skipped and the region limits the search") {
  const auto lut = realLut();
  Image img;
  img.at(10, 20) = 9000;
  img.at(40, 40) = 7000;
  std::vector<uint8_t> bad(tv::kImagePixels, 0);
  bad[size_t(20) * tv::kFrameWidth + 10] = 1;
  Readouts r = tv::computeReadouts(img.px.data(), lut, Region{}, tv::overRangeRaw(lut), &bad);
  CHECK(r.high.x == 40);
  r = tv::computeReadouts(img.px.data(), lut, Region{100, 100, 111, 121}, tv::overRangeRaw(lut));
  CHECK(r.high.x >= 100);
  CHECK(r.high.x < 111);
  CHECK(r.center.x == doctest::Approx(105));  // odd width: a single center column
  CHECK(r.center.y == doctest::Approx(110));
  CHECK(r.center.tempC == doctest::Approx(lut[5500]));
}

TEST_CASE("the high marker stays put unless a new maximum wins by the hysteresis margin") {
  const auto lut = realLut();
  Image img;
  img.at(10, 10) = 7000;
  ReadoutFilter filter(0.2, 0.3);
  Readouts shown = filter.update(tv::computeReadouts(img.px.data(), lut, Region{}, tv::overRangeRaw(lut)),
                                 img.px.data(), lut, Region{}, tv::overRangeRaw(lut), 0.04);
  CHECK(shown.high.x == 10);
  // A pixel slightly hotter (less than 0.2 °C) elsewhere: the marker stays on (10,10).
  uint16_t slightly = 7000;
  while (lut[uint16_t(slightly + 1)] - lut[7000] < 0.1) ++slightly;
  img.at(50, 50) = slightly;
  shown = filter.update(tv::computeReadouts(img.px.data(), lut, Region{}, tv::overRangeRaw(lut)),
                        img.px.data(), lut, Region{}, tv::overRangeRaw(lut), 0.04);
  CHECK(shown.high.x == 10);
  // Clearly hotter (> 0.2 °C): the marker moves.
  uint16_t hotter = 7000;
  while (lut[hotter] - lut[7000] < 0.5) ++hotter;
  img.at(60, 60) = hotter;
  shown = filter.update(tv::computeReadouts(img.px.data(), lut, Region{}, tv::overRangeRaw(lut)),
                        img.px.data(), lut, Region{}, tv::overRangeRaw(lut), 0.04);
  CHECK(shown.high.x == 60);
}

TEST_CASE("shown values ease toward a step change; over range shows at once") {
  const auto lut = realLut();
  Image img;
  ReadoutFilter filter(0.2, 0.3);
  auto step = [&](double dt) {
    return filter.update(tv::computeReadouts(img.px.data(), lut, Region{}, tv::overRangeRaw(lut)),
                         img.px.data(), lut, Region{}, tv::overRangeRaw(lut), dt);
  };
  step(0.04);
  const double before = lut[5500];
  for (auto& p : img.px) p = 6500;  // the whole scene warms
  const double after = lut[6500];
  const Readouts r = step(0.04);
  const double alpha = 1.0 - std::exp(-0.04 / 0.3);
  CHECK(r.center.tempC == doctest::Approx(before + alpha * (after - before)));
  for (int i = 0; i < 100; ++i) step(0.04);
  CHECK(step(0.04).center.tempC == doctest::Approx(after).epsilon(1e-4));
  img.at(127, 95) = 14192;
  CHECK(step(0.04).center.overRange);
}

TEST_CASE("over range starts at the rated 120 C or the clip floor, whichever is lower") {
  const auto lut = realLut();  // FPA ~36 °C: 120 °C sits well below the clip floor
  const uint16_t at120 = lut.rawAtOrAbove(tv::kRatedTopC);
  CHECK(lut[at120] >= tv::kRatedTopC);
  CHECK(lut[uint16_t(at120 - 1)] < tv::kRatedTopC);
  CHECK(tv::overRangeRaw(lut) == std::min<uint16_t>(at120, tv::kClipFloorRaw));
  Image img;
  img.at(3, 3) = 13841;  // a pixel clipped low (the 300 °C iron, docs/DEVICE.md)
  CHECK(tv::computeReadouts(img.px.data(), lut, Region{}, tv::overRangeRaw(lut)).high.overRange);
}

TEST_CASE("a window keeps low and high inside it (a locked range), the center regardless") {
  const auto lut = realLut();
  Image img;
  img.at(10, 20) = 7000;   // too hot for the window
  img.at(30, 40) = 6000;   // the hottest inside it
  img.at(200, 150) = 4000; // too cold
  img.at(100, 100) = 5200; // (the bulk is 5500)
  img.at(120, 60) = 5100;  // the coldest inside it
  const tv::RawWindow w{5000, 6500};
  const Readouts r = tv::computeReadouts(img.px.data(), lut, Region{}, tv::overRangeRaw(lut), nullptr, w);
  CHECK(r.high.x == 30);
  CHECK(r.high.y == 40);
  CHECK(r.low.x == 120);
  CHECK(r.low.y == 60);
  CHECK(std::isfinite(r.center.tempC));

  // Nothing inside: no low or high (the UI draws no marker), the center still.
  const Readouts none = tv::computeReadouts(img.px.data(), lut, Region{}, tv::overRangeRaw(lut), nullptr, {9000, 9100});
  CHECK(std::isnan(none.high.x));
  CHECK(std::isnan(none.low.x));
  CHECK(std::isfinite(none.center.tempC));
}

TEST_CASE("the filter drops a marked pixel that leaves the window") {
  const auto lut = realLut();
  Image img;
  img.at(30, 40) = 6000;
  ReadoutFilter f;
  const Region all{};
  const uint16_t clip = tv::overRangeRaw(lut);
  f.update(tv::computeReadouts(img.px.data(), lut, all, clip), img.px.data(), lut, all, clip, 0.04);
  // The window narrows below the marked pixel: the high moves to what's inside.
  img.at(50, 50) = 5800;
  const tv::RawWindow w{0, 5900};
  const Readouts r = f.update(tv::computeReadouts(img.px.data(), lut, all, clip, nullptr, w), img.px.data(), lut, all,
                              clip, 0.04, w);
  CHECK(r.high.x == 50);
  CHECK(r.high.y == 50);
}
