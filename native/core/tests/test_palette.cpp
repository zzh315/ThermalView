#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include "doctest.h"
#include "tv/palette.h"

namespace {
tv::PaletteSpec load(const std::string& name) {
  std::ifstream in(std::string(TV_PALETTE_DIR) + "/" + name + ".json");
  std::stringstream text;
  text << in.rdbuf();
  tv::PaletteSpec spec;
  std::string error;
  REQUIRE_MESSAGE(tv::parsePalette(text.str(), &spec, &error), error);
  return spec;
}
float hueDeg(const tv::Rgb& c) {
  const tv::Lab lab = tv::srgbToOklab(c);
  return std::atan2(lab[2], lab[1]) * 180.0f / 3.14159265f;
}
}  // namespace

TEST_CASE("OKLab round-trips sRGB and puts white at L = 1") {
  for (const tv::Rgb c : {tv::Rgb{0.0f, 0.0f, 0.0f}, tv::Rgb{1.0f, 1.0f, 1.0f}, tv::Rgb{1.0f, 0.5f, 0.0f},
                          tv::Rgb{0.2f, 0.7f, 0.9f}}) {
    const tv::Rgb back = tv::oklabToSrgb(tv::srgbToOklab(c));
    for (int k = 0; k < 3; ++k) CHECK(back[size_t(k)] == doctest::Approx(c[size_t(k)]).epsilon(1e-3));
  }
  CHECK(tv::srgbToOklab({1.0f, 1.0f, 1.0f})[0] == doctest::Approx(1.0f).epsilon(1e-3));
}

TEST_CASE("white_hot: black to white, lightness linear in intensity") {
  const tv::PaletteSpec spec = load("white_hot");
  const auto lut = tv::buildPaletteLut(spec);
  REQUIRE(lut.size() == 1024);
  CHECK(lut.front() == std::array<uint8_t, 3>{0, 0, 0});
  CHECK(lut.back() == std::array<uint8_t, 3>{255, 255, 255});
  for (float t : {0.1f, 0.25f, 0.5f, 0.75f, 0.9f}) {
    const tv::Rgb c = tv::paletteColor(spec, t);
    CHECK(tv::srgbToOklab(c)[0] == doctest::Approx(t).epsilon(2e-3));
    CHECK(c[0] == doctest::Approx(c[1]));  // grey
  }
  int drops = 0;
  for (size_t i = 1; i < lut.size(); ++i) drops += lut[i][0] < lut[i - 1][0];
  CHECK(drops == 0);
}

TEST_CASE("rainbow_hc: green through yellow to red, smoothly, as vivid as sRGB allows") {
  const tv::PaletteSpec spec = load("rainbow_hc");
  const auto lut = tv::buildPaletteLut(spec);
  CHECK(lut.front() == std::array<uint8_t, 3>{0, 255, 0});
  CHECK(lut[512][0] >= 250);  // yellow in the middle
  CHECK(lut[512][1] >= 250);
  CHECK(lut.back() == std::array<uint8_t, 3>{255, 0, 0});
  // The hue falls steadily from green to red: no band, no detour through other hues.
  float previous = hueDeg(tv::paletteColor(spec, 0.0f));
  for (int i = 1; i <= 100; ++i) {
    const float h = hueDeg(tv::paletteColor(spec, float(i) / 100.0f));
    CHECK(h <= previous + 0.05f);
    CHECK(previous - h < 5.0f);
    previous = h;
  }
  // Vivid: every entry has at least one channel at full and one near zero (a saturated sRGB color).
  int dull = 0;
  for (const auto& c : lut) dull += std::max({c[0], c[1], c[2]}) < 250 || std::min({c[0], c[1], c[2]}) > 20;
  CHECK(dull == 0);
}

TEST_CASE("rainbow_deep and rainbow_soft: rainbow_hc's hues at their stops' lightness, less chroma") {
  for (const char* name : {"rainbow_deep", "rainbow_soft"}) {
    const tv::PaletteSpec spec = load(name);
    CHECK(spec.stopLightness);
    // The lightness follows the stops': a darker cold end, the brightest at the yellow middle.
    const float l0 = tv::srgbToOklab(tv::paletteColor(spec, 0.0f))[0];
    const float l5 = tv::srgbToOklab(tv::paletteColor(spec, 0.5f))[0];
    const float l1 = tv::srgbToOklab(tv::paletteColor(spec, 1.0f))[0];
    CHECK(l0 < 0.6f);
    CHECK(l5 > l0 + 0.25f);
    CHECK(l5 > l1);
    // The same hue order as rainbow_hc: falling steadily from green to red.
    float previous = hueDeg(tv::paletteColor(spec, 0.0f));
    for (int i = 1; i <= 100; ++i) {
      const float h = hueDeg(tv::paletteColor(spec, float(i) / 100.0f));
      CHECK(h <= previous + 0.05f);
      previous = h;
    }
    // Less vivid than rainbow_hc: some chroma held back everywhere.
    for (int i = 0; i <= 10; ++i) {
      const tv::Lab lab = tv::srgbToOklab(tv::paletteColor(spec, float(i) / 10.0f));
      tv::PaletteSpec full = spec;
      full.chromaScale = 1.0f;
      const tv::Lab max = tv::srgbToOklab(tv::paletteColor(full, float(i) / 10.0f));
      CHECK(std::hypot(lab[1], lab[2]) <= std::hypot(max[1], max[2]) * (spec.chromaScale + 0.02f));
    }
  }
}

TEST_CASE("rainbow_hti (Rainbow HC): magenta to red and a softer red, round the hue wheel one way") {
  const tv::PaletteSpec spec = load("rainbow_hti");
  CHECK(spec.marksLocked);
  const auto lut = tv::buildPaletteLut(spec);
  // Its ends are its stops: HTi's cold magenta and its hot, lighter red.
  CHECK(lut.front() == std::array<uint8_t, 3>{0x82, 0x04, 0xA1});
  CHECK(lut.back() == std::array<uint8_t, 3>{0xCB, 0x4E, 0x52});
  // The hue falls steadily from magenta (~317 degrees) through blue, green and yellow to red
  // (~22): unwrapped, it only falls, and never by a jump.
  float previous = hueDeg(tv::paletteColor(spec, 0.0f));
  float total = 0.0f;
  for (int i = 1; i <= 400; ++i) {
    const float h = hueDeg(tv::paletteColor(spec, float(i) / 400.0f));
    float d = h - previous;
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    CHECK(d <= 0.05f);
    CHECK(d > -8.0f);
    total += d;
    previous = h;
  }
  CHECK(total == doctest::Approx(-295.0f).epsilon(0.02));
  // Lightness: darkest in the blue-teal stretch, brightest at the yellow, the red darker again.
  float lMin = 1.0f, lMax = 0.0f, tMin = 0.0f, tMax = 0.0f;
  for (int i = 0; i <= 100; ++i) {
    const float t = float(i) / 100.0f, l = tv::srgbToOklab(tv::paletteColor(spec, t))[0];
    if (l < lMin) lMin = l, tMin = t;
    if (l > lMax) lMax = l, tMax = t;
  }
  CHECK(tMin > 0.1f);
  CHECK(tMin < 0.4f);
  CHECK(tMax > 0.55f);
  CHECK(tMax < 0.75f);
  CHECK(lMax - lMin > 0.45f);
  // Smooth: neighbouring entries never differ by more than a few levels.
  int jumps = 0;
  for (size_t i = 1; i < lut.size(); ++i)
    for (size_t k = 0; k < 3; ++k) jumps += std::abs(int(lut[i][k]) - int(lut[i - 1][k])) > 4;
  CHECK(jumps == 0);
}

TEST_CASE("palette files are checked") {
  tv::PaletteSpec spec;
  std::string error;
  CHECK_FALSE(tv::parsePalette("[]", &spec, &error));
  CHECK_FALSE(tv::parsePalette(R"({"stops": [[0, "#000000"]]})", &spec, &error));
  CHECK_FALSE(tv::parsePalette(R"({"stops": [[0, "#000000"], [0.5, "#FFFFFF"]]})", &spec, &error));
  CHECK_FALSE(tv::parsePalette(R"({"space": "hsv", "stops": [[0, "#000000"], [1, "#FFFFFF"]]})", &spec, &error));
  CHECK(tv::parsePalette(R"({"name": "x", "note": {"a": [1, 2]}, "stops": [[0, "#000000"], [1, "#ffffff"]]})", &spec,
                         &error));
  CHECK(spec.name == "x");
}
