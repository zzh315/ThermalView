#!/usr/bin/env python3
"""Runs the GPU BM3D's generated shaders (native/android/gpu_bm3d_shader.cpp) on the Mac, against
native/core's CPU reference (tv/bm3d.h), without a GPU.

Each pass's GLSL is compile-checked with the NDK's glslc (GLSL ES 3.10), then rewritten as C++ (the
buffers and uniforms as globals, shared arrays as statics, barrier() as a std::barrier) and run: every
workgroup's 64 invocations as 64 threads that meet at each barrier, so the shaders' synchronization is
exercised as written. Step 1, the gather into the basic estimate, step 2 and the final gather, then
the result against tv::bm3d on the same frame: a synthetic one (a gradient, a step, fine texture, a
hot spot, the camera's noise) and, if present, a real one (build/nr/in3c: stages 1-3c's signal).

    tools/py/.venv/bin/python tools/py/gpu_bm3d_emulate.py [--search 5] [--group 8] [--stride 6]
"""

import argparse
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "build" / "gpu_bm3d"
GLSLC = pathlib.Path.home() / "Library/Android/sdk/ndk/28.1.13356709/shader-tools/darwin-x86_64/glslc"

PRELUDE = r"""
#include <algorithm>
#include <atomic>
#include <barrier>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <thread>
#include <vector>
#include "tv/bm3d.h"
#include "tv/frame.h"
struct UV3 { unsigned x, y, z; };
thread_local unsigned tLocal = 0, tGroupX = 0, tGroupY = 0;
#define gl_LocalInvocationIndex tLocal
#define gl_WorkGroupID (UV3{tGroupX, tGroupY, 0u})
#define gl_GlobalInvocationID (UV3{tGroupX * 64u + tLocal, 0u, 0u})
static std::barrier<>* gBarrier = nullptr;
inline void barrier() { gBarrier->arrive_and_wait(); }
using std::max;
using std::min;
inline int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
inline int atomicAdd(int& x, int v) { return std::atomic_ref<int>(x).fetch_add(v); }

template <class F> void runGroups(int gx, int gy, F body) {  // 64 invocations a group, as threads
  std::barrier<> sync(64);
  gBarrier = &sync;
  std::vector<std::thread> pool;
  for (unsigned t = 0; t < 64; ++t)
    pool.emplace_back([&, t] {
      tLocal = t;
      for (int y = 0; y < gy; ++y)
        for (int x = 0; x < gx; ++x) {
          tGroupX = unsigned(x);
          tGroupY = unsigned(y);
          sync.arrive_and_wait();  // (the previous group is done with the shared arrays)
          body();
          sync.arrive_and_wait();
        }
    });
  for (auto& p : pool) p.join();
}

template <class F> void runFlat(int groups, F body) {  // no barriers: one invocation at a time
  for (int g = 0; g < groups; ++g)
    for (unsigned t = 0; t < 64; ++t) {
      tGroupX = unsigned(g);
      tLocal = t;
      body();
    }
}
"""


def transpile(glsl, namespace):
    out = []
    for line in glsl.splitlines():
        if line.startswith(("#version", "precision", "layout(local_size")):
            continue
        m = re.match(r"layout\(std430, binding = \d+\) (?:readonly |writeonly )?buffer \w+ \{ float (\w+)\[\]; \};", line)
        if m:
            out.append(f"static float* {m.group(1)} = nullptr;")
            continue
        m = re.match(r"layout\(location = \d+\) uniform float (\w+);", line)
        if m:
            out.append(f"static float {m.group(1)} = 0.0f;")
            continue
        m = re.match(r"layout\(location = \d+\) uniform float (\w+)\[(\d+)\];", line)
        if m:
            out.append(f"static float {m.group(1)}[{m.group(2)}] = {{}};")
            continue
        line = re.sub(r"^shared ", "static ", line)
        line = re.sub(r"= float\[\d+\]\((.*)\);$", r"= {\1};", line)
        line = line.replace("void main()", "void shaderMain()")
        out.append(line)
    return f"namespace {namespace} {{\n" + "\n".join(out) + "\n}\n"


MAIN = r"""
int main(int argc, char** argv) {
  const int search = std::atoi(argv[1]), group = std::atoi(argv[2]), stride = std::atoi(argv[3]);
  const int rx = std::atoi(argv[4]), ry = std::atoi(argv[5]), tt = std::atoi(argv[6]);
  const char* real = argc > 7 ? argv[7] : nullptr;
  const int W = tv::kFrameWidth, H = tv::kImageRows;
  std::vector<float> src(tv::kImagePixels);
  if (real) {
    FILE* f = std::fopen(real, "rb");
    if (!f || std::fseek(f, long(100) * long(sizeof(float)) * long(tv::kImagePixels), SEEK_SET) != 0 ||
        std::fread(src.data(), sizeof(float), src.size(), f) != src.size()) { std::printf("cannot read %s\n", real); return 2; }
    std::fclose(f);
  } else {
    std::mt19937 rng(7);
    std::normal_distribution<float> noise(0.0f, 1.07f);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        float v = 5000.0f + 0.2f * x;
        if (x > 128) v += 40.0f;
        if (y > 100 && y < 140 && x > 20 && x < 90) v += ((x / 3 + y / 3) % 2) * 6.0f;
        const float dx = x - 200.0f, dy = y - 50.0f;
        v += 3000.0f * std::exp(-(dx * dx + dy * dy) / 50.0f);
        src[size_t(y) * W + x] = v + noise(rng);
      }
  }
  tv::Bm3dOptions o;
  o.block = 8;
  o.search = search;
  o.group1 = o.group2 = group;
  o.stride = stride;
  o.tau1 = o.tau2 = 0.0f;
  const float sigma = 1.04f * 1.07f;
  std::vector<float> cpu(src.size()), basic(src.size()), gpu(src.size()), tiles(size_t(rx) * ry * 2 * tt);
  tv::bm3d(src.data(), cpu.data(), sigma, o);
  double sum = 0.0;
  for (float v : src) sum += v;
  const float level = float(sum / double(tv::kImagePixels)), sigma2 = sigma * sigma;
  const std::vector<float> window = tv::bm3dKaiserWindow(8, o.kaiser);
  std::copy(window.begin(), window.end(), step1::uWin);
  std::copy(window.begin(), window.end(), step2::uWin);
  step1::src = src.data(); step1::tiles = tiles.data();
  step1::uLevel = level; step1::uThr = (o.lambda * o.lambda) * sigma2; step1::uSigma2 = sigma2;
  runGroups(rx, ry, step1::shaderMain);
  gather::tiles = tiles.data(); gather::outv = basic.data(); gather::uAdd = 0.0f;
  runFlat(tv::kImagePixels / 64, gather::shaderMain);
  step2::src = src.data(); step2::tiles = tiles.data(); step2::basic = basic.data();
  step2::uLevel = level; step2::uMuSigma2 = o.mu2 * sigma2; step2::uSigma2 = sigma2;
  runGroups(rx, ry, step2::shaderMain);
  gather::outv = gpu.data(); gather::uAdd = level;
  runFlat(tv::kImagePixels / 64, gather::shaderMain);
  double maxd = 0.0, total = 0.0, moved = 0.0;
  size_t worst = 0, over = 0;
  for (size_t i = 0; i < src.size(); ++i) {
    const double d = std::fabs(double(gpu[i]) - cpu[i]);
    if (d > maxd) { maxd = d; worst = i; }
    total += d;
    over += d > 0.01;
    moved += std::fabs(double(cpu[i]) - src[i]);
  }
  std::printf("|shaders - CPU reference| max %.2e counts at (%zu,%zu), mean %.2e, over 0.01: %zu pixels; "
              "the filter moved pixels %.3f on average\n",
              maxd, worst % size_t(W), worst / size_t(W), total / src.size(), over, moved / src.size());
  return maxd < 0.05 && total / src.size() < 1e-3 ? 0 : 1;
}
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--search", type=int, default=5)
    ap.add_argument("--group", type=int, default=8)
    ap.add_argument("--stride", type=int, default=6)
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    inc = ["-I", str(ROOT / "native" / "core" / "include"), "-I", str(ROOT / "native" / "android")]
    gen = OUT / "gen_sources.cpp"
    gen.write_text(r"""#include <cstdio>
#include <cstdlib>
#include "gpu_bm3d_shader.h"
int main(int, char** argv) {
  tv::GpuBm3dShape s;
  s.search = std::atoi(argv[2]); s.group = std::atoi(argv[3]); s.stride = std::atoi(argv[4]);
  const int pass = std::atoi(argv[1]);
  if (pass == 0) { std::printf("%d %d %d\n", tv::gpuBm3dRefsX(s), tv::gpuBm3dRefsY(s), tv::gpuBm3dTileSide(s)); return 0; }
  std::fputs(tv::gpuBm3dShaderSource(pass == 1 ? tv::GpuBm3dPass::Step1 : pass == 2 ? tv::GpuBm3dPass::Step2
                                                                        : tv::GpuBm3dPass::Gather, s).c_str(), stdout);
}
""")
    subprocess.run(["c++", "-std=c++20", "-O1", *inc, gen, ROOT / "native/android/gpu_bm3d_shader.cpp",
                    ROOT / "native/core/src/bm3d.cpp", "-o", OUT / "gen_sources"], check=True)
    shape = [str(args.search), str(args.group), str(args.stride)]
    rx, ry, side = subprocess.run([OUT / "gen_sources", "0", *shape], check=True, capture_output=True,
                                  text=True).stdout.split()
    code = PRELUDE
    for k, name in ((1, "step1"), (2, "step2"), (3, "gather")):
        glsl = subprocess.run([OUT / "gen_sources", str(k), *shape], check=True, capture_output=True, text=True).stdout
        path = OUT / f"{name}.comp"
        path.write_text(glsl)
        if GLSLC.exists():
            subprocess.run([GLSLC, "-fshader-stage=comp", "--target-env=opengl", "-std=310es", path, "-o",
                            OUT / f"{name}.spv"], check=True)
        code += transpile(glsl, name)
    emu = OUT / "emulate.cpp"
    emu.write_text(code + MAIN)
    exe = OUT / "emulate"
    subprocess.run(["c++", "-std=c++20", "-O2", "-w", *inc, emu, ROOT / "native/core/src/bm3d.cpp", "-o", exe],
                   check=True)
    tt = str(int(side) ** 2)
    ok = True
    real = ROOT / "build" / "nr" / "in3c" / "room" / "pipeline_s.f32"
    for label, extra in (("synthetic frame", []), ("room, stages 1-3c", [str(real)] if real.exists() else None)):
        if extra is None:
            continue
        r = subprocess.run([exe, *shape, rx, ry, tt, *extra], capture_output=True, text=True)
        print(f"search {args.search}, group {args.group}, stride {args.stride}, {label}: {r.stdout.strip()}")
        ok &= r.returncode == 0
    print("PASS" if ok else "FAIL")
    raise SystemExit(0 if ok else 1)


if __name__ == "__main__":
    main()
