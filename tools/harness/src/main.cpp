// ThermalView harness: runs native/core on recorded dumps on the Mac (docs/PLAN.md M2, M3).
//
//   harness temps DUMP [--range normal|high] [--math ht301|infi] [--disc X,Y,R ...]
//
// One CSV row per frame: the unsmoothed low/high/center readouts the app computes (whole frame,
// per-frame table), the camera's own Block A extremes through the same table, and the mean °C
// over each --disc (camera pixels within R of X,Y). DUMP is the dump path without .raw.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "tv/dump.h"
#include "tv/readouts.h"
#include "tv/temperature.h"

namespace {

struct Disc {
  int x, y, r;
};

int usage() {
  std::fprintf(stderr,
               "usage: harness temps DUMP [--range normal|high] [--math ht301|infi] [--disc X,Y,R ...]\n");
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

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::string(argv[1]) == "temps") return temps(argc, argv);
  return usage();
}
