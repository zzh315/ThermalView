#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "tv/temperature.h"

namespace fs = std::filesystem;
using tv::HighRangeMath;
using tv::TemperatureLut;
using tv::TempRange;

namespace {

// Golden files from tools/py/oracle.py: NAME.meta (4 metadata rows) and NAME.lut (16384 float32).
template <typename T>
std::vector<T> readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::vector<T> v(fs::file_size(path) / sizeof(T));
  in.read(reinterpret_cast<char*>(v.data()), std::streamsize(v.size() * sizeof(T)));
  return v;
}

std::vector<uint16_t> frameFromMeta(const fs::path& meta) {
  std::vector<uint16_t> frame(tv::kFramePixels, 0);
  const auto rows = readFile<uint16_t>(meta);
  REQUIRE(rows.size() == tv::kFramePixels - tv::kImagePixels);
  std::copy(rows.begin(), rows.end(), frame.begin() + tv::kImagePixels);
  return frame;
}

std::vector<fs::path> goldenCases() {
  std::vector<fs::path> cases;
  for (const auto& e : fs::directory_iterator(TV_GOLDEN_DIR))
    if (e.path().extension() == ".meta") cases.push_back(e.path());
  std::sort(cases.begin(), cases.end());
  return cases;
}

// Max |ours - oracle| over the table; NaN must match NaN.
double compare(const TemperatureLut& lut, const std::vector<float>& oracle, size_t* nanMismatch) {
  double worst = 0;
  *nanMismatch = 0;
  for (size_t i = 0; i < TemperatureLut::kSize; ++i) {
    const double a = lut.table()[i], b = oracle[i];
    if (std::isnan(a) || std::isnan(b)) {
      *nanMismatch += std::isnan(a) != std::isnan(b);
      continue;
    }
    worst = std::max(worst, std::abs(a - b));
  }
  return worst;
}

}  // namespace

TEST_CASE("the normal-range table matches the oracle within 0.01 C on every golden frame") {
  const auto cases = goldenCases();
  REQUIRE(cases.size() >= 3);  // PLAN M2: at least three dumps
  for (const auto& meta : cases) {
    const auto frame = frameFromMeta(meta);
    TemperatureLut lut;
    lut.build(tv::temperatureInputs(tv::FrameView(frame.data())));
    fs::path lutPath = meta;
    const auto oracle = readFile<float>(lutPath.replace_extension(".lut"));
    REQUIRE(oracle.size() == TemperatureLut::kSize);
    size_t nanMismatch = 0;
    const double worst = compare(lut, oracle, &nanMismatch);
    MESSAGE(meta.filename().string(), ": worst difference ", worst, " C");
    CHECK(worst < 0.01);
    CHECK(nanMismatch == 0);
  }
}

TEST_CASE("the ht301 high-range table matches the oracle's high-range mode") {
  size_t checked = 0;
  for (const auto& meta : goldenCases()) {
    fs::path hi = meta;
    hi.replace_extension(".hi_ht301");
    if (!fs::exists(hi)) continue;
    const auto frame = frameFromMeta(meta);
    TemperatureLut lut;
    lut.build(tv::temperatureInputs(tv::FrameView(frame.data())), TempRange::High, HighRangeMath::Ht301);
    size_t nanMismatch = 0;
    const double worst = compare(lut, readFile<float>(hi), &nanMismatch);
    MESSAGE(hi.filename().string(), ": worst difference ", worst, " C");
    CHECK(worst < 0.01);
    CHECK(nanMismatch == 0);
    ++checked;
  }
  CHECK(checked >= 1);
}

TEST_CASE("the table folds at its vertex; only raw values above it are valid") {
  const auto frame = frameFromMeta(goldenCases().front());
  TemperatureLut lut;
  lut.build(tv::temperatureInputs(tv::FrameView(frame.data())));
  const uint16_t v = lut.vertex();
  REQUIRE(v > 0);
  REQUIRE(v + 1 < TemperatureLut::kSize);
  CHECK_FALSE(lut.valid(v));
  CHECK_FALSE(lut.valid(0));
  CHECK(lut.valid(uint16_t(v + 1)));
  CHECK(lut[uint16_t(v - 1)] > lut[v]);  // folded back up below the vertex
  for (size_t i = v + 1; i < TemperatureLut::kSize; ++i) CHECK(lut.table()[i] > lut.table()[i - 1]);
}

TEST_CASE("InfiCam's high range is the normal table shifted by the cal_00 correction") {
  const auto frame = frameFromMeta(goldenCases().front());
  const auto in = tv::temperatureInputs(tv::FrameView(frame.data()));
  TemperatureLut normal, infi;
  normal.build(in);
  infi.build(in, TempRange::High, HighRangeMath::InfiCam);
  const int shift = int(tv::kCal00Offset);  // 170 for width 256
  for (size_t i = shift; i < TemperatureLut::kSize; i += 97)
    CHECK(infi.table()[i] == doctest::Approx(normal.table()[i - shift]).epsilon(1e-12));
}
