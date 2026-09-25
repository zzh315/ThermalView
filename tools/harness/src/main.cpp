// ThermalView harness: runs native/core on recorded dumps on the Mac (docs/PLAN.md M2, M3).
//
//   harness temps DUMP [--range normal|high] [--math ht301|infi] [--disc X,Y,R ...]
//
// One CSV row per frame: the unsmoothed low/high/center readouts the app computes (whole frame,
// per-frame table), the camera's own Block A extremes through the same table, and the mean °C
// over each --disc (camera pixels within R of X,Y). DUMP is the dump path without .raw.
//
//   harness bench [--bench DIR] [--out DIR] [--pipeline STAGES] [--box X,Y,W,H] [SCENE ...]
//
// Renders every benchmark scene (DIR/<scene>/thermalview.raw; default: the repo's bench/) through
// the display path and writes OUT/<scene>/ (default DIR/out): baseline.f32, the display output
// (frames × 192 × 256 float32 in [0, 1]); baseline_c.f32, the signal the display path started
// from in °C, through the table of the dump's middle frame (NaN where it's undefined); and
// info.json, with that frame's environment inputs (the camera's user area, used as-is) and FPA.
// --pipeline also runs native/core's Pipeline, starting from its defaults (the approved stages) and
// changed by a comma-separated list ("default", "shutter=0", "shutterBlend=N", ...), and writes
// pipeline.f32 and pipeline_c.f32 the same way, plus pipeline_s.f32: that signal in raw counts. --box sets the pipeline's measurement region
// (camera pixels): stage 5's statistics come from it alone (docs/PLAN.md M4 stage 5).
// tools/py/bench.py turns these into metrics, contact sheets and clips.
//
//   harness render DUMP --frame N --size WxH [--pipeline STAGES] [--rect X,Y,W,H] [--kernel K]
//                  [--clamp] [--palette FILE.json] [--box X,Y,W,H [--dim F]] --out FILE.ppm
//
// The display path at on-screen size (docs/PLAN.md M5's CPU reference): the pipeline runs over
// frames 0..N (its filters need the history), then frame N's intensity is upscaled with kernel K
// (nearest, bilinear, catmullrom, lanczos3, bspline; --clamp: the 2x2 anti-ringing clamp) over the
// view rectangle (camera pixels; default the whole frame) and mapped through the palette (default
// grey), with pixels over range in the palette's saturation color. Writes a binary PPM. With
// FILE.f32 in place of DUMP (one frame's intensity, as the app's GPU readback saves it) the pipeline
// is skipped; --clip FILE.u8 gives its over-range mask and --mirror-x / --mirror-y its mirroring.
//
//   harness palette FILE.json --out FILE.ppm
//
// The palette's 1024-entry table as a 1024 x 64 gradient strip.
//
//   harness perf DUMP [--pipeline STAGES] [--drift PATH] [--passes N]
//
// Times Pipeline::process on every frame of DUMP (path without .raw), N passes (default 3), and
// prints the p50, p95 and max per frame in ms. Built with the NDK and run over adb, it measures the
// stages on the tablet's CPU without installing the app (the performance budget, CLAUDE.md).
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "tv/bad_pixels.h"
#include "tv/display.h"
#include "tv/drift.h"
#include "tv/dump.h"
#include "tv/palette.h"
#include "tv/pipeline.h"
#include "tv/readouts.h"
#include "tv/temperature.h"
#include "tv/upscale.h"

namespace {

struct Disc {
  int x, y, r;
};

int usage() {
  std::fprintf(stderr,
               "usage: harness temps DUMP [--range normal|high] [--math ht301|infi] [--disc X,Y,R ...]\n"
               "       harness bench [--bench DIR] [--out DIR] [--pipeline STAGES] [--box X,Y,W,H] [SCENE ...]\n"
               "       harness render DUMP --frame N --size WxH [--pipeline STAGES] [--rect X,Y,W,H]\n"
               "                      [--kernel K] [--clamp] [--palette FILE.json] [--box X,Y,W,H] --out FILE.ppm\n"
               "       harness render FILE.f32 [--clip FILE.u8] [--mirror-x] [--mirror-y] --size WxH ... --out FILE.ppm\n"
               "       harness palette FILE.json --out FILE.ppm\n"
               "       harness perf DUMP [--pipeline STAGES] [--drift PATH] [--passes N]\n");
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

bool benchScene(const std::filesystem::path& sceneDir, const std::filesystem::path& outDir,
                const tv::PipelineOptions* pipelineOptions, const std::string& stages, const tv::Region& box) {
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

  auto toCelsius = [&](float raw) {
    // Linear interpolation into the table, for the pipeline's fractional counts.
    const float r = std::clamp(raw, 0.0f, float(tv::TemperatureLut::kSize - 2));
    const size_t i = size_t(r);
    if (!lut.valid(uint16_t(i)) || !lut.valid(uint16_t(i + 1))) return float(NAN);
    return float(lut[uint16_t(i)] + (r - float(i)) * (lut[uint16_t(i + 1)] - lut[uint16_t(i)]));
  };
  std::vector<float> display(dump.frameCount * tv::kImagePixels), celsius(display.size());
  std::vector<float> pipeDisplay, pipeCelsius, pipeSignal, signal(tv::kImagePixels);
  if (pipelineOptions) {
    pipeDisplay.resize(display.size());
    pipeCelsius.resize(display.size());
    pipeSignal.resize(display.size());
  }
  tv::Pipeline pipeline(pipelineOptions ? *pipelineOptions : tv::PipelineOptions{});
  // Stage 2's map is the dump's camera's (its sidecar's serial).
  pipeline.setBadPixels(tv::badPixelMapFor(dump.serial));
  pipeline.setDriftMap(tv::loadDriftMap(TV_REPO_DIR "/native/core/data/drift_" + dump.serial + ".f32"));
  pipeline.setRegion(box);
  for (size_t f = 0; f < dump.frameCount; ++f) {
    const tv::FrameView frame(&dump.frames[f * tv::kFramePixels]);
    const uint16_t* image = frame.image();
    tv::renderBaseline(image, &display[f * tv::kImagePixels]);
    float* c = &celsius[f * tv::kImagePixels];
    for (size_t i = 0; i < tv::kImagePixels; ++i) c[i] = lut.valid(image[i]) ? float(lut[image[i]]) : NAN;
    if (pipelineOptions) {
      pipeline.process(image, &pipeDisplay[f * tv::kImagePixels], signal.data(), {frame.fpaC(), frame.shutterC()});
      float* pc = &pipeCelsius[f * tv::kImagePixels];
      for (size_t i = 0; i < tv::kImagePixels; ++i) pc[i] = toCelsius(signal[i]);
      std::copy(signal.begin(), signal.end(), pipeSignal.begin() + std::ptrdiff_t(f * tv::kImagePixels));
    }
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
  info += pipelineOptions ? "  \"stages\": [\"baseline\", \"pipeline\"],\n  \"pipeline\": \"" + tv::jsonEscape(stages) + "\",\n"
                         : "  \"stages\": [\"baseline\"],\n";
  info += "  \"lut_frame\": " + std::to_string(lutFrame) + ",\n";
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
  if (!writeFloats(outDir / "baseline.f32", display) || !writeFloats(outDir / "baseline_c.f32", celsius) ||
      (pipelineOptions && (!writeFloats(outDir / "pipeline.f32", pipeDisplay) ||
                           !writeFloats(outDir / "pipeline_c.f32", pipeCelsius) ||
                           !writeFloats(outDir / "pipeline_s.f32", pipeSignal)))) {
    std::fprintf(stderr, "harness: cannot write %s\n", outDir.c_str());
    return false;
  }
  std::printf("%s: %zu frames\n", sceneDir.filename().c_str(), dump.frameCount);
  return true;
}

int bench(int argc, char** argv) {
  std::filesystem::path benchDir = TV_REPO_DIR "/bench", outDir;
  std::vector<std::string> scenes;
  std::string stages;
  tv::PipelineOptions pipelineOptions;
  bool withPipeline = false;
  tv::Region box;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--box" && i + 1 < argc) {
      int x, y, w, h;
      if (std::sscanf(argv[++i], "%d,%d,%d,%d", &x, &y, &w, &h) != 4 || w <= 0 || h <= 0) return usage();
      box = {x, y, x + w, y + h};
    } else if (a == "--bench" && i + 1 < argc) {
      benchDir = argv[++i];
    } else if (a == "--out" && i + 1 < argc) {
      outDir = argv[++i];
    } else if (a == "--pipeline" && i + 1 < argc) {
      stages = argv[++i];
      if (!tv::parseStages(stages, &pipelineOptions)) {
        std::fprintf(stderr, "harness: unknown stage in \"%s\"\n", stages.c_str());
        return 2;
      }
      withPipeline = true;
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
  for (const std::string& scene : scenes)
    ok = benchScene(benchDir / scene, outDir / scene, withPipeline ? &pipelineOptions : nullptr, stages, box) && ok;
  return ok ? 0 : 1;
}

bool loadPaletteFile(const std::string& path, tv::PaletteSpec* spec) {
  std::ifstream in(path);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::string error;
  if (!in || !tv::parsePalette(text, spec, &error)) {
    std::fprintf(stderr, "harness: %s: %s\n", path.c_str(), error.empty() ? "cannot read" : error.c_str());
    return false;
  }
  return true;
}

bool writePpm(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << "P6\n" << w << " " << h << "\n255\n";
  out.write(reinterpret_cast<const char*>(rgb.data()), std::streamsize(rgb.size()));
  return bool(out);
}

int render(int argc, char** argv) {
  if (argc < 3) return usage();
  const std::string path = argv[2];
  std::string stages = "default", out, palettePath, kernelText = "bspline", clipPath, outsidePath;
  int frameIndex = 0, w = 0, h = 0;
  bool clamp = false, mirrorX = false, mirrorY = false;
  const bool fromIntensity = path.size() > 4 && path.compare(path.size() - 4, 4, ".f32") == 0;
  tv::ViewRect rect;
  tv::Region box;
  bool haveBox = false;
  float dim = 0.5f;
  for (int i = 3; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
    if (a == "--frame") frameIndex = std::atoi(next().c_str());
    else if (a == "--size") { if (std::sscanf(next().c_str(), "%dx%d", &w, &h) != 2) return usage(); }
    else if (a == "--pipeline") stages = next();
    else if (a == "--rect") { if (std::sscanf(next().c_str(), "%f,%f,%f,%f", &rect.x, &rect.y, &rect.w, &rect.h) != 4) return usage(); }
    else if (a == "--kernel") kernelText = next();
    else if (a == "--clamp") clamp = true;
    else if (a == "--palette") palettePath = next();
    else if (a == "--clip") clipPath = next();
    else if (a == "--outside") outsidePath = next();  // a locked range's marks: 1 above, 2 below (the readback's)
    else if (a == "--mirror-x") mirrorX = true;
    else if (a == "--mirror-y") mirrorY = true;
    else if (a == "--box") {
      int x, y, bw, bh;
      if (std::sscanf(next().c_str(), "%d,%d,%d,%d", &x, &y, &bw, &bh) != 4 || bw <= 0 || bh <= 0) return usage();
      box = {x, y, x + bw, y + bh};
      haveBox = true;
    }
    else if (a == "--dim") dim = float(std::atof(next().c_str()));
    else if (a == "--out") out = next();
    else return usage();
  }
  tv::PipelineOptions options;
  tv::Kernel kernel;
  if (w <= 0 || h <= 0 || out.empty() || !tv::parseStages(stages, &options) || !tv::parseKernel(kernelText, &kernel))
    return usage();
  tv::PaletteSpec spec;
  if (!palettePath.empty() && !loadPaletteFile(palettePath, &spec)) return 1;
  const auto lut = palettePath.empty() ? std::vector<std::array<uint8_t, 3>>{} : tv::buildPaletteLut(spec);
  std::vector<float> display(tv::kImagePixels), mask(tv::kImagePixels, 0.0f);
  if (fromIntensity) {
    std::ifstream in(path, std::ios::binary);
    in.read(reinterpret_cast<char*>(display.data()), std::streamsize(sizeof(float) * tv::kImagePixels));
    if (!in) {
      std::fprintf(stderr, "harness: %s: not one frame of intensity\n", path.c_str());
      return 1;
    }
    if (!clipPath.empty()) {
      std::vector<uint8_t> clip(tv::kImagePixels);
      std::ifstream c(clipPath, std::ios::binary);
      c.read(reinterpret_cast<char*>(clip.data()), std::streamsize(clip.size()));
      for (size_t i = 0; i < tv::kImagePixels; ++i) mask[i] = float(clip[i]) / 255.0f;
    }
  } else {
    tv::LoadedDump dump;
    std::string error;
    if (!tv::loadDump(path, &dump, &error) || dump.frameCount == 0) {
      std::fprintf(stderr, "harness: %s: %s\n", path.c_str(), error.empty() ? "no frames" : error.c_str());
      return 1;
    }
    tv::Pipeline pipeline(options);
    pipeline.setBadPixels(tv::badPixelMapFor(dump.serial));
    pipeline.setDriftMap(tv::loadDriftMap(TV_REPO_DIR "/native/core/data/drift_" + dump.serial + ".f32"));
    pipeline.setRegion(box);
    const size_t last = std::min(size_t(std::max(frameIndex, 0)), dump.frameCount - 1);
    for (size_t f = 0; f <= last; ++f) {
      const tv::FrameView frame(&dump.frames[f * tv::kFramePixels]);
      pipeline.process(frame.image(), display.data(), nullptr, {frame.fpaC(), frame.shutterC()});
    }
    // Pixels that read over range (> 120 °C through the frame's own table, the readouts' threshold).
    const tv::FrameView shown(&dump.frames[last * tv::kFramePixels]);
    tv::TemperatureLut table;
    table.build(tv::temperatureInputs(shown), tv::TempRange::Normal);
    const uint16_t clipRaw = tv::overRangeRaw(table);
    for (size_t i = 0; i < tv::kImagePixels; ++i) mask[i] = shown.image()[i] >= clipRaw ? 1.0f : 0.0f;
  }
  std::vector<float> up(size_t(w) * size_t(h));
  tv::upscale(tv::kernelInput(display.data(), kernel), display.data(), kernel, clamp, rect, w, h, up.data());
  // Over-range pixels take the palette's saturation color where their bilinear mask passes 0.5, as
  // the GPU does with a filtered R8 mask.
  std::vector<float> maskUp(up.size());
  tv::upscale(mask, mask.data(), tv::Kernel::Bilinear, false, rect, w, h, maskUp.data());
  // A locked range's marks, filtered like the over-range mask (the GPU's RG8 texture).
  std::vector<float> aboveUp, belowUp;
  if (!outsidePath.empty() && spec.marksLocked && !lut.empty()) {
    std::vector<uint8_t> marks(tv::kImagePixels);
    std::ifstream m(outsidePath, std::ios::binary);
    m.read(reinterpret_cast<char*>(marks.data()), std::streamsize(marks.size()));
    std::vector<float> above(tv::kImagePixels), below(tv::kImagePixels);
    for (size_t i = 0; i < tv::kImagePixels; ++i) {
      above[i] = marks[i] == 1 ? 1.0f : 0.0f;
      below[i] = marks[i] == 2 ? 1.0f : 0.0f;
    }
    aboveUp.resize(up.size());
    belowUp.resize(up.size());
    tv::upscale(above, above.data(), tv::Kernel::Bilinear, false, rect, w, h, aboveUp.data());
    tv::upscale(below, below.data(), tv::Kernel::Bilinear, false, rect, w, h, belowUp.data());
  }
  if (mirrorX || mirrorY) {
    std::vector<float> a = up, b = maskUp, c = aboveUp, d = belowUp;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const size_t from = size_t(mirrorY ? h - 1 - y : y) * size_t(w) + size_t(mirrorX ? w - 1 - x : x);
        const size_t to = size_t(y) * size_t(w) + size_t(x);
        up[to] = a[from];
        maskUp[to] = b[from];
        if (!c.empty()) {
          aboveUp[to] = c[from];
          belowUp[to] = d[from];
        }
      }
  }
  std::vector<uint8_t> rgb(up.size() * 3);
  for (size_t i = 0; i < up.size(); ++i) {
    const float v = std::clamp(up[i], 0.0f, 1.0f);
    float c[3];
    if (lut.empty()) {
      c[0] = c[1] = c[2] = v;
    } else if (maskUp[i] > 0.5f) {
      for (size_t k = 0; k < 3; ++k) c[k] = spec.saturation[k];
    } else if (!aboveUp.empty() && aboveUp[i] > 0.5f) {
      for (size_t k = 0; k < 3; ++k) c[k] = spec.lockedAbove[k];
    } else if (!belowUp.empty() && belowUp[i] > 0.5f) {
      for (size_t k = 0; k < 3; ++k) c[k] = spec.lockedBelow[k];
    } else {
      const auto& e = lut[size_t(std::lround(v * float(lut.size() - 1)))];
      for (size_t k = 0; k < 3; ++k) c[k] = float(e[k]) / 255.0f;
    }
    // M6's box, as the shader dims it: by the camera coordinate of the (unmirrored) pixel's center.
    if (haveBox) {
      const int x = int(i % size_t(w)), y = int(i / size_t(w));
      const float cx = rect.x + (float(mirrorX ? w - 1 - x : x) + 0.5f) * rect.w / float(w);
      const float cy = rect.y + (float(mirrorY ? h - 1 - y : y) + 0.5f) * rect.h / float(h);
      if (cx < float(box.x0) || cx >= float(box.x1) || cy < float(box.y0) || cy >= float(box.y1))
        for (float& k : c) k *= dim;
    }
    for (size_t k = 0; k < 3; ++k) rgb[3 * i + k] = uint8_t(std::lround(255.0f * std::clamp(c[k], 0.0f, 1.0f)));
  }
  if (!writePpm(out, w, h, rgb)) {
    std::fprintf(stderr, "harness: cannot write %s\n", out.c_str());
    return 1;
  }
  return 0;
}

int palette(int argc, char** argv) {
  if (argc != 5 || std::string(argv[3]) != "--out") return usage();
  tv::PaletteSpec spec;
  if (!loadPaletteFile(argv[2], &spec)) return 1;
  const auto lut = tv::buildPaletteLut(spec);
  const int w = int(lut.size()), h = 64;
  std::vector<uint8_t> rgb(size_t(w) * h * 3);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) std::copy(lut[size_t(x)].begin(), lut[size_t(x)].end(), rgb.begin() + 3 * (y * w + x));
  return writePpm(argv[4], w, h, rgb) ? 0 : 1;
}

int perf(int argc, char** argv) {
  if (argc < 3) return usage();
  const std::string path = argv[2];
  std::string stages, driftPath;
  int passes = 3;
  tv::PipelineOptions options;
  for (int i = 3; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--pipeline" && i + 1 < argc) {
      stages = argv[++i];
      if (!tv::parseStages(stages, &options)) {
        std::fprintf(stderr, "harness: unknown stage in \"%s\"\n", stages.c_str());
        return 2;
      }
    } else if (a == "--drift" && i + 1 < argc) {
      driftPath = argv[++i];
    } else if (a == "--passes" && i + 1 < argc) {
      passes = std::max(1, std::atoi(argv[++i]));
    } else {
      return usage();
    }
  }
  tv::LoadedDump dump;
  std::string error;
  if (!tv::loadDump(path, &dump, &error) || dump.frameCount == 0) {
    std::fprintf(stderr, "harness: %s: %s\n", path.c_str(), error.empty() ? "no frames" : error.c_str());
    return 1;
  }
  if (driftPath.empty()) driftPath = TV_REPO_DIR "/native/core/data/drift_" + dump.serial + ".f32";
  tv::Pipeline pipeline(options);
  pipeline.setBadPixels(tv::badPixelMapFor(dump.serial));
  pipeline.setDriftMap(tv::loadDriftMap(driftPath));
  std::vector<float> display(tv::kImagePixels);
  std::vector<double> ms, appMs;  // the pipeline; the app's other per-frame work (checks, table, readouts)
  std::vector<std::vector<double>> partMs(tv::Pipeline::kParts);  // the pipeline's parts
  tv::TemperatureLut lut;
  using Clock = std::chrono::steady_clock;
  auto since = [](Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); };
  for (int p = 0; p < passes; ++p) {
    pipeline.reset();
    for (size_t f = 0; f < dump.frameCount; ++f) {
      const tv::FrameView frame(&dump.frames[f * tv::kFramePixels]);
      auto t0 = Clock::now();
      const tv::ImageStats stats = tv::computeImageStats(frame.image());
      (void)tv::checkFrame(frame, stats, false);
      lut.build(tv::temperatureInputs(frame), tv::TempRange::Normal);
      const tv::Readouts r = tv::computeReadouts(frame.image(), lut, tv::Region{}, tv::overRangeRaw(lut));
      appMs.push_back(since(t0));
      t0 = Clock::now();
      pipeline.process(frame.image(), display.data(), nullptr, {frame.fpaC(), frame.shutterC()});
      ms.push_back(since(t0));
      for (int k = 0; k < tv::Pipeline::kParts; ++k) partMs[size_t(k)].push_back(pipeline.lastPartMs()[size_t(k)]);
      (void)r;
    }
  }
  std::sort(ms.begin(), ms.end());
  std::sort(appMs.begin(), appMs.end());
  auto at = [](const std::vector<double>& v, double q) { return v[std::min(v.size() - 1, size_t(q * double(v.size())))]; };
  std::printf("%s: %zu frames x %d, stages \"%s\" (drift map %s): p50 %.2f ms, p95 %.2f ms, max %.2f ms; "
              "checks + table + readouts p50 %.2f ms, p95 %.2f ms\n",
              path.c_str(), dump.frameCount, passes, tv::describeStages(pipeline.options()).c_str(),
              pipeline.driftMap().empty() ? "none" : "loaded", at(ms, 0.5), at(ms, 0.95), ms.back(),
              at(appMs, 0.5), at(appMs, 0.95));
  for (auto& v : partMs) std::sort(v.begin(), v.end());
  const auto part = [&](tv::Pipeline::Part k) { return at(partMs[size_t(k)], 0.5); };
  std::printf("  p50 by part: 1-3a %.2f  3b %.2f  3c %.2f  4b %.2f  alongside it: 3b learning %.2f, 3c reference %.2f"
              "  5-6 %.2f  rest %.2f ms\n",
              part(tv::Pipeline::kEarly), part(tv::Pipeline::kDestripe), part(tv::Pipeline::kStripes),
              part(tv::Pipeline::kNoise), part(tv::Pipeline::kLearn), part(tv::Pipeline::kReference),
              part(tv::Pipeline::kTone), part(tv::Pipeline::kRest));
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::string(argv[1]) == "temps") return temps(argc, argv);
  if (argc >= 2 && std::string(argv[1]) == "bench") return bench(argc, argv);
  if (argc >= 2 && std::string(argv[1]) == "perf") return perf(argc, argv);
  if (argc >= 2 && std::string(argv[1]) == "render") return render(argc, argv);
  if (argc >= 2 && std::string(argv[1]) == "palette") return palette(argc, argv);
  return usage();
}
