#include <filesystem>

#include "doctest.h"
#include "tv/dump.h"

TEST_CASE("a dump round-trips frames and timestamps") {
  const auto dir = std::filesystem::temp_directory_path() / "thermalview_test_dump";
  std::filesystem::create_directories(dir);
  const std::string base = (dir / "d").string();

  std::vector<uint16_t> frames(3 * tv::kFramePixels);
  for (size_t i = 0; i < frames.size(); ++i) frames[i] = uint16_t(i * 7);
  tv::DumpInfo info;
  info.timestampsNs = {100, 40'000'100, 80'000'100};
  info.sequence = {5, 6, 7};
  info.product = "S0H-40";
  info.commandLog = {"0x8004 sent", "quote \" and backslash \\"};
  std::string error;
  REQUIRE(tv::writeDump(base, frames, info, &error));

  tv::LoadedDump loaded;
  REQUIRE(tv::loadDump(base, &loaded, &error));
  CHECK(loaded.frameCount == 3);
  CHECK(loaded.frames == frames);
  CHECK(loaded.timestampsNs == info.timestampsNs);
  std::filesystem::remove_all(dir);
}

TEST_CASE("mismatched sizes are refused") {
  tv::DumpInfo info;
  info.timestampsNs = {1, 2};
  std::string error;
  CHECK_FALSE(tv::writeDump("/tmp/never", std::vector<uint16_t>(tv::kFramePixels), info, &error));
  CHECK_FALSE(error.empty());
}

TEST_CASE("the sidecar parser reads integer arrays") {
  CHECK(tv::parseJsonIntArray(R"({"a": [1, -2, 30000000000], "b": []})", "a") ==
        std::vector<int64_t>{1, -2, 30000000000});
  CHECK(tv::parseJsonIntArray(R"({"b": []})", "b").empty());
  CHECK(tv::parseJsonIntArray(R"({"b": []})", "missing").empty());
}
