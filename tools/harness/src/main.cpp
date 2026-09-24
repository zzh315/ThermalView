// ThermalView harness: runs native/core on recorded dumps on the Mac (docs/PLAN.md M2, M3).
//
//   harness temps DUMP [--range normal|high] [--math ht301|infi] [--disc X,Y,R ...]
//
// One CSV row per frame: the unsmoothed low/high/center readouts the app computes (whole frame,
// per-frame table), the camera's own Block A extremes through the same table, and the mean °C
// over each --disc (camera pixels within R of X,Y). DUMP is the dump path without .raw.
//
//   harness bench [--bench DIR] [--out DIR] [SCENE ...]
//
// Renders every benchmark scene (DIR/<scene>/thermalview.raw; default: the repo's bench/) through
// the display path and writes OUT/<scene>/ (default DIR/out): baseline.f32, the display output
// (frames × 192 × 256 float32 in [0, 1]); baseline_c.f32, the signal the display path started
// from in °C, through the table of the dump's middle frame (NaN where it's undefined); and
// info.json, with that frame's environment inputs (the camera's user area, used as-is) and FPA.
// tools/py/bench.py turns these into metrics, contact sheets and clips.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "tv/display.h"
#include "tv/dump.h"
#include "tv/readouts.h"
#include "tv/temperature.h"

namespace {

struct Disc {
  int x, y, r;
};

int usage() {
  std::fprintf(stderr,
               "usage: harness temps DUMP [--range normal|high] [--math ht301|infi] [--disc X,Y,R ...]\n"
               "       harness bench [--bench DIR] [--out DIR] [SCENE ...]\n");
  return 2;
}

std::string num(double v) {
  if (!std::isfinite(v)) return "";
  char b[32];
  std::snprintf(b, sizeof b, "%.3f", v);
  return b;
}

int temps(int argc, char** argv) {
  if (argc < 3) return usage();
  const std::string base = argv[2];
  tv::TempRange range = tv::TempRange::Normal;
  tv::HighRangeMath math = tv::HighRangeMath::Ht301;
  std::vector<Disc> discs;
  for (int i = 3; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--range" && i + 1 < argc) {
      range = std::string(argv[++i]) == "high" ? tv::TempRange::High : tv::TempRange::Normal;
    } else if (a == "--math" && i + 1 < argc) {
      math = std::string(argv[++i]) == "infi" ? tv::HighRangeMath::InfiCam : tv::HighRangeMath::Ht301;
    } else if (a == "--disc" && i + 1 < argc) {
      Disc d{};
      if (std::sscanf(argv[++i], "%d,%d,%d", &d.x, &d.y, &d.r) != 3) return usage();
      discs.push_back(d);
    } else {
      return usage();
    }
  }

  tv::LoadedDump dump;
  std::string error;
  if (!tv::loadDump(base, &dump, &error)) {
    std::fprintf(stderr, "harness: %s\n", error.c_str());
    return 1;
  }
  std::printf("frame,t_ms,fpa_c,shutter_c,low_c,low_x,low_y,high_c,high_x,high_y,high_over,center_c,"
              "cam_max_c,cam_min_c,cam_center_c");
  for (size_t d = 0; d < discs.size(); ++d) std::printf(",disc%zu_c", d);
  std::printf("\n");

  tv::TemperatureLut lut;
  const tv::Region region;
  for (size_t f = 0; f < dump.frameCount; ++f) {
    const uint16_t* data = &dump.frames[f * tv::kFramePixels];
    const tv::FrameView view(data);
    lut.build(tv::temperatureInputs(view), range, math);
    const tv::Readouts r = tv::computeReadouts(view.image(), lut, region, tv::overRangeRaw(lut));
    const double tMs = dump.timestampsNs.size() == dump.frameCount
                           ? double(dump.timestampsNs[f] - dump.timestampsNs[0]) / 1e6
                           : double(f) * 40.0;
    auto camT = [&](uint16_t raw) { return lut.valid(raw) ? lut[raw] : NAN; };
    std::printf("%zu,%.1f,%.3f,%.2f,%s,%d,%d,%s,%d,%d,%d,%s,%s,%s,%s", f, tMs, view.fpaC(), view.shutterC(),
                num(r.low.tempC).c_str(), int(r.low.x), int(r.low.y), num(r.high.tempC).c_str(),
                int(r.high.x), int(r.high.y), r.high.overRange ? 1 : 0, num(r.center.tempC).c_str(),
                num(camT(view.maxRaw())).c_str(), num(camT(view.minRaw())).c_str(),
                num(camT(view.centerRaw())).c_str());
    for (const Disc& d : discs) {
      double sum = 0;
      int n = 0;
      for (int y = std::max(0, d.y - d.r); y <= std::min(tv::kImageRows - 1, d.y + d.r); ++y)
        for (int x = std::max(0, d.x - d.r); x <= std::min(tv::kFrameWidth - 1, d.x + d.r); ++x) {
          if ((x - d.x) * (x - d.x) + (y - d.y) * (y - d.y) > d.r * d.r) continue;
          const uint16_t raw = view.image()[size_t(y) * tv::kFrameWidth + x];
          if (lut.valid(raw)) {
            sum += lut[raw];
            ++n;
          }
        }
      std::printf(",%s", num(n ? sum / n : NAN).c_str());
    }
    std::printf("\n");
  }
  return 0;
}

bool writeFloats(const std::filesystem::path& path, const std::vector<float>& data) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size() * sizeof(float)));
  return bool(out);
}

bool benchScene(const std::filesystem::path& sceneDir, const std::filesystem::path& outDir) {
  tv::LoadedDump dump;
  std::string error;
  if (!tv::loadDump((sceneDir / "thermalview").string(), &dump, &error) || dump.frameCount == 0) {
    std::fprintf(stderr, "harness: %s: %s\n", sceneDir.c_str(), error.empty() ? "no frames" : error.c_str());
    return false;
  }
  // One table for the whole dump, so °C noise isn't mixed with the table's frame-to-frame changes.
  const size_t lutFrame = dump.frameCount / 2;
  const tv::TemperatureInputs in = tv::temperatureInputs(tv::FrameView(&dump.frames[lutFrame * tv::kFramePixels]));
  tv::TemperatureLut lut;
  lut.build(in, tv::TempRange::Normal);

  std::vector<float> display(dump.frameCount * tv::kImagePixels), celsius(display.size());
  for (size_t f = 0; f < dump.frameCount; ++f) {
    const uint16_t* image = tv::FrameView(&dump.frames[f * tv::kFramePixels]).image();
    tv::renderBaseline(image, &display[f * tv::kImagePixels]);
    float* c = &celsius[f * tv::kImagePixels];
    for (size_t i = 0; i < tv::kImagePixels; ++i) c[i] = lut.valid(image[i]) ? float(lut[image[i]]) : NAN;
  }

  std::filesystem::create_directories(outDir);
  std::string info = "{\n  \"scene\": \"" + tv::jsonEscape(sceneDir.filename().string()) + "\",\n";
  info += "  \"frames\": " + std::to_string(dump.frameCount) + ",\n  \"width\": " +
          std::to_string(tv::kFrameWidth) + ",\n  \"height\": " + std::to_string(tv::kImageRows) + ",\n";
  char env[256];
  std::snprintf(env, sizeof env,
                "  \"environment\": {\"emissivity\": %.3f, \"reflected_c\": %.2f, \"air_c\": %.2f, "
                "\"humidity\": %.3f, \"distance\": %u, \"fpa_c\": %.2f},\n",
                in.emissivity, in.reflectedC, in.airC, in.humidity, unsigned(in.distance), in.fpaC);
  info += "  \"stages\": [\"baseline\"],\n  \"lut_frame\": " + std::to_string(lutFrame) + ",\n";
  info += env;
  info += "  \"t_ms\": [";
  for (size_t f = 0; f < dump.frameCount; ++f) {
    const double tMs = dump.timestampsNs.size() == dump.frameCount
                           ? double(dump.timestampsNs[f] - dump.timestampsNs[0]) / 1e6
                           : double(f) * 40.0;
    char b[32];
    std::snprintf(b, sizeof b, "%s%.1f", f ? ", " : "", tMs);
    info += b;
  }
  info += "]\n}\n";
  std::ofstream(outDir / "info.json", std::ios::trunc) << info;
  if (!writeFloats(outDir / "baseline.f32", display) || !writeFloats(outDir / "baseline_c.f32", celsius)) {
    std::fprintf(stderr, "harness: cannot write %s\n", outDir.c_str());
    return false;
  }
  std::printf("%s: %zu frames\n", sceneDir.filename().c_str(), dump.frameCount);
  return true;
}

int bench(int argc, char** argv) {
  std::filesystem::path benchDir = TV_REPO_DIR "/bench", outDir;
  std::vector<std::string> scenes;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--bench" && i + 1 < argc) {
      benchDir = argv[++i];
    } else if (a == "--out" && i + 1 < argc) {
      outDir = argv[++i];
    } else if (a.starts_with("--")) {
      return usage();
    } else {
      scenes.push_back(a);
    }
  }
  if (outDir.empty()) outDir = benchDir / "out";
  if (scenes.empty()) {
    for (const auto& e : std::filesystem::directory_iterator(benchDir))
      if (e.is_directory() && std::filesystem::exists(e.path() / "thermalview.raw"))
        scenes.push_back(e.path().filename().string());
    std::sort(scenes.begin(), scenes.end());
  }
  bool ok = !scenes.empty();
  for (const std::string& scene : scenes) ok = benchScene(benchDir / scene, outDir / scene) && ok;
  return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::string(argv[1]) == "temps") return temps(argc, argv);
  if (argc >= 2 && std::string(argv[1]) == "bench") return bench(argc, argv);
  return usage();
}
