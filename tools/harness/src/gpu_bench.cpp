// The GPU BM3D (native/android/gpu_bm3d.h) on its own, from the adb shell, so its shaders can be
// timed without the app: a frame (kImagePixels floats, e.g. the app's nrcheck/bm3d_input.f32) through
// a shape, back to back and pass by pass, and against native/core's CPU reference.
//
//   adb shell /data/local/tmp/tv/gpu_bench FRAME.f32 [search group stride [sigma [ablate]]]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "gpu_bm3d.h"
#include "tv/bm3d.h"
#include "tv/frame.h"

namespace {

double median(std::vector<double> v) {
  if (v.empty()) return -1.0;
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: gpu_bench FRAME.f32 [search group stride [sigma [ablate]]]\n");
    return 2;
  }
  std::vector<float> src(tv::kImagePixels), gpu(src.size()), cpu(src.size());
  FILE* f = std::fopen(argv[1], "rb");
  if (!f || std::fread(src.data(), sizeof(float), src.size(), f) != src.size()) {
    std::fprintf(stderr, "gpu_bench: cannot read %s\n", argv[1]);
    return 1;
  }
  std::fclose(f);
  tv::Bm3dOptions o;
  o.search = argc > 2 ? std::atoi(argv[2]) : 5;
  o.group1 = o.group2 = argc > 3 ? std::atoi(argv[3]) : 8;
  o.stride = argc > 4 ? std::atoi(argv[4]) : 6;
  o.tau1 = o.tau2 = 0.0f;
  const float sigma = argc > 5 ? float(std::atof(argv[5])) : 1.04f * 1.07f;
  const int ablate = argc > 6 ? std::atoi(argv[6]) : 0;
  tv::GpuBm3d bm3d;
  if (!bm3d.run(src.data(), gpu.data(), sigma, o)) {  // (the first run compiles)
    std::fprintf(stderr, "gpu_bench: %s\n", bm3d.status().c_str());
    return 1;
  }
  std::vector<double> total, pass[4];
  for (int k = 0; k < 20 && bm3d.run(src.data(), gpu.data(), sigma, o); ++k) total.push_back(bm3d.lastMs());
  for (int k = 0; k < 10; ++k) {
    double t[4];
    if (!bm3d.profile(src.data(), gpu.data(), sigma, o, t, ablate)) break;
    for (int j = 0; j < 4; ++j) pass[j].push_back(t[j]);
  }
  // The GPU at a sustained clock: each pass 50 times back to back, twice (the second counts).
  double sus[3] = {};
  for (int rep = 0; rep < 2; ++rep)
    for (int k = 0; k < 3; ++k) sus[k] = bm3d.sustained(o, k, 50, ablate);
  std::printf("sustained, a dispatch: step 1 %.2f, gather %.2f, step 2 %.2f ms\n", sus[0], sus[1], sus[2]);
  std::printf("search %d, groups %d, stride %d%s: upload to read-back %.2f ms (median of %zu); passes alone: step 1 "
              "%.2f, gather %.2f, step 2 %.2f, gather %.2f ms\n",
              o.search, o.group1, o.stride, ablate ? " (ablated)" : "", median(total), total.size(), median(pass[0]),
              median(pass[1]), median(pass[2]), median(pass[3]));
  if (ablate) return 0;
  bm3d.run(src.data(), gpu.data(), sigma, o);
  tv::bm3d(src.data(), cpu.data(), sigma, o);
  double maxd = 0.0, sum = 0.0;
  for (size_t i = 0; i < src.size(); ++i) {
    const double d = std::fabs(double(gpu[i]) - cpu[i]);
    maxd = std::max(maxd, d);
    sum += d;
  }
  std::printf("|GPU - CPU| max %.2e counts, mean %.2e\n", maxd, sum / double(src.size()));
  return 0;
}
