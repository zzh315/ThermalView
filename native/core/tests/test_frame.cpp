#include <cstring>
#include <vector>

#include "doctest.h"
#include "tv/frame.h"

namespace {

void putF32(std::vector<uint16_t>& f, size_t index, float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof bits);
  f[index] = uint16_t(bits & 0xFFFF);  // low word first
  f[index + 1] = uint16_t(bits >> 16);
}

void putText(std::vector<uint16_t>& f, size_t index, const char* text) {
  std::memcpy(reinterpret_cast<char*>(f.data() + index), text, std::strlen(text));
}

// A frame that passes every sanity check: a gradient image with matching Block A, plausible
// temperatures, and a sensible user area.
std::vector<uint16_t> goodFrame() {
  std::vector<uint16_t> f(tv::kFramePixels, 0);
  for (size_t i = 0; i < tv::kImagePixels; ++i) f[i] = uint16_t(6000 + i % 1000);
  f[tv::kOffsetP + 0] = 6500;      // FPA average
  f[tv::kOffsetP + 1] = 8617;      // FPA raw -> 20 °C
  f[tv::kOffsetP + 4] = 6999;      // max raw
  f[tv::kOffsetP + 7] = 6000;      // min raw
  f[tv::kOffsetP + 12] = 6500;     // center raw
  f[tv::kOffsetQ + 0] = 1234;      // cal_00
  f[tv::kOffsetQ + 1] = 3050;      // shutter 305.0 K = 31.85 °C
  f[tv::kOffsetQ + 2] = 3100;      // core 310.0 K = 36.85 °C
  putF32(f, tv::kOffsetQ + 3, 0.2333f);
  putF32(f, tv::kOffsetQ + 5, 27.867f);
  putF32(f, tv::kOffsetQ + 11, 0.5351f);
  putText(f, tv::kOffsetQ + 24, "1.10.2023052515");
  putF32(f, tv::kOffsetU + 2, 20.0f);   // reflected
  putF32(f, tv::kOffsetU + 8, 0.95f);   // emissivity
  putF32(f, tv::kOffsetU + 6, 0.5f);    // humidity
  f[tv::kOffsetU + 10] = 1;             // distance
  return f;
}

}  // namespace

TEST_CASE("layout constants match PROTOCOL.md") {
  CHECK(tv::kFrameBytes == 100352);
  CHECK(tv::kOffsetP == 256 * 192);
  CHECK(tv::kOffsetQ == tv::kOffsetP + 256);
  CHECK(tv::kOffsetU == tv::kOffsetQ + 127);
}

TEST_CASE("metadata accessors decode Block A, Block B and the user area") {
  const auto data = goodFrame();
  const tv::FrameView f(data.data());
  CHECK(f.fpaRaw() == 8617);
  CHECK(f.fpaC() == doctest::Approx(20.0));
  CHECK(f.maxRaw() == 6999);
  CHECK(f.minRaw() == 6000);
  CHECK(f.centerRaw() == 6500);
  CHECK(f.cal00() == 1234);
  CHECK(f.shutterC() == doctest::Approx(31.85));
  CHECK(f.coreC() == doctest::Approx(36.85));
  CHECK(f.cal(1) == doctest::Approx(0.2333f));
  CHECK(f.cal(2) == doctest::Approx(27.867f));
  CHECK(f.cal(5) == doctest::Approx(0.5351f));
  CHECK(f.firmware() == "1.10.2023052515");
  CHECK(f.reflectedC() == doctest::Approx(20.0f));
  CHECK(f.emissivity() == doctest::Approx(0.95f));
  CHECK(f.distance() == 1);
}

TEST_CASE("f32 fields are low word first") {
  auto data = goodFrame();
  data[tv::kOffsetQ + 3] = 0x5532;  // the T2S+ v2 test frame's cal_01 words
  data[tv::kOffsetQ + 4] = 0x3E70;
  CHECK(tv::FrameView(data.data()).cal(1) == doctest::Approx(0.2347f).epsilon(1e-4));
}

TEST_CASE("strings stop at NUL and mask non-printable bytes") {
  auto data = goodFrame();
  putText(data, tv::kOffsetQ + 24, "AB\x01" "C");
  CHECK(tv::FrameView(data.data()).firmware().substr(0, 4) == "AB?C");
}

TEST_CASE("a good frame passes the sanity checks, at start-up too") {
  const auto data = goodFrame();
  const tv::FrameView f(data.data());
  const auto s = tv::computeImageStats(f.image());
  CHECK(s.min == 6000);
  CHECK(s.max == 6999);
  CHECK(tv::checkFrame(f, s, true) == 0);
  CHECK(tv::describeSanity(0) == "ok");
}

TEST_CASE("each sanity check fires on its own fault") {
  const auto check = [](auto mutate, uint32_t expected) {
    auto data = goodFrame();
    mutate(data);
    const tv::FrameView f(data.data());
    const uint32_t flags = tv::checkFrame(f, tv::computeImageStats(f.image()), true);
    CHECK((flags & expected) == expected);
  };
  check([](auto& d) { d[100] = 0x4000; }, tv::kSanityOver14Bit);
  check([](auto& d) { d[tv::kOffsetP + 4] = 0; }, tv::kSanityBlockAZero);
  check([](auto& d) { d[tv::kOffsetP + 7] = 7500; }, tv::kSanityBlockAOrder);
  check([](auto& d) { d[tv::kOffsetP + 4] = 9000; }, tv::kSanityBlockAExtremes);
  // The T2S+ v2 test frame, which streamed uncompensated data: −219.95 °C and −273.15 °C.
  check([](auto& d) { d[tv::kOffsetQ + 1] = 532; }, tv::kSanityShutterTemp);
  check([](auto& d) { d[tv::kOffsetQ + 2] = 0; }, tv::kSanityCoreTemp);
  check([](auto& d) { putF32(d, tv::kOffsetU + 8, 0.0f); }, tv::kSanityEmissivity);
  check([](auto& d) { putF32(d, tv::kOffsetU + 6, 1.5f); }, tv::kSanityHumidity);
}

TEST_CASE("distance 0 is valid: the camera reports it after power-up") {
  auto data = goodFrame();
  data[tv::kOffsetU + 10] = 0;
  const tv::FrameView f(data.data());
  CHECK(tv::checkFrame(f, tv::computeImageStats(f.image()), true) == 0);
}

TEST_CASE("Block A may disagree with the image after start-up") {
  auto data = goodFrame();
  data[tv::kOffsetP + 4] = 9000;
  const tv::FrameView f(data.data());
  CHECK(tv::checkFrame(f, tv::computeImageStats(f.image()), false) == 0);
}

TEST_CASE("text runs in the metadata are found with their offset from P") {
  auto data = goodFrame();
  putText(data, tv::kOffsetP + 512, "KA1213");
  const auto runs = tv::findTextRuns(tv::FrameView(data.data()));
  bool found = false;
  for (const auto& r : runs) found |= r.text == "KA1213" && r.byteOffsetFromP == 1024;
  CHECK(found);
}
