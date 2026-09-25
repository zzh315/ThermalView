// gpu_bm3d.h's shader sources (no GL here, so the Mac can generate, compile-check and emulate them:
// tools/py/gpu_bm3d_emulate.py).
#pragma once

#include <string>
#include <vector>

namespace tv {

// The shapes the GPU version supports: native/core's BM3D (tv/bm3d.h) with 8 x 8 blocks, full
// groups (no match thresholds) of one size for both steps, every block of a group aggregated.
struct GpuBm3dShape {
  int search = 5;       // radius, 1-7
  int group = 8;        // 2, 4 or 8
  int stride = 6;       // between reference blocks, 1-8
  float kaiser = 2.0f;  // the aggregation window's beta
  int ablate = 0;       // profiling only (wrong results): 1 no ranking (the first candidates), 2 no distances
  bool operator==(const GpuBm3dShape&) const = default;
};

enum class GpuBm3dPass {
  Step1,   // hard thresholding: each reference block's group into its own tile (num, den)
  Step2,   // Wiener, matched on the basic estimate: the same
  Gather,  // each pixel: the tiles that cover it, summed in a fixed order, then num / den + uAdd
};

std::string gpuBm3dShaderSource(GpuBm3dPass pass, const GpuBm3dShape& shape);

// The dispatch sizes: reference blocks across and down (tv/bm3d.h's positions), and a tile's side.
int gpuBm3dRefsX(const GpuBm3dShape& shape);
int gpuBm3dRefsY(const GpuBm3dShape& shape);
int gpuBm3dTileSide(const GpuBm3dShape& shape);
// The aggregation window the shaders take as their uWin uniform (location 3, 64 floats).
std::vector<float> gpuBm3dWindow(const GpuBm3dShape& shape);

}  // namespace tv
