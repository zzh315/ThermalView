#!/usr/bin/env python3
"""Runs stage 4b's generated GPU shader (native/android/gpu_nlm_shader.cpp) on the Mac, against
native/core's CPU reference (filters.h nonLocalMeans), without a GPU.

The shader's code is plain enough to compile as C++ with a few stand-ins (ivec2, the gl_ built-ins),
so this generates it, turns it into a C++ function that runs every workgroup (the tile load for all
threads, then each thread's filter), and compares the two on a synthetic frame: flat noise, steps,
fine texture and a hot spot, at the camera's noise level (1.07 counts).

    tools/py/.venv/bin/python tools/py/gpu_nlm_emulate.py [--search 5] [--patch 2] [--pixels 1 2 4]
"""

import argparse
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "build" / "gpu_nlm"

PRELUDE = r"""
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>
#include "tv/filters.h"
#include "tv/frame.h"
using std::abs;
using std::exp2;
struct UV2 { unsigned x, y; };
struct U3 { unsigned x, y, z; UV2 xy; };
struct ivec2 {
  int x, y;
  ivec2(int a, int b) : x(a), y(b) {}
  explicit ivec2(UV2 v) : x(int(v.x)), y(int(v.y)) {}
};
ivec2 operator*(ivec2 a, ivec2 b) { return {a.x * b.x, a.y * b.y}; }
ivec2 operator-(ivec2 a, int b) { return {a.x - b, a.y - b}; }
"""


def transpile(glsl, n):
    lines = glsl.splitlines()
    head, load, body = [], [], []
    part = head
    for line in lines:
        if line.startswith(("#version", "layout(")):
            continue
        if line.startswith("void main() {"):
            part = load
            continue
        if line.strip() == "barrier();":
            part = body
            continue
        part.append(line)
    assert body and body[-1] == "}", "unexpected shader layout"
    body = body[:-1]
    head = [re.sub(r"^shared float", "static float", l) for l in head]
    wx = 256 // (16 * n)
    return PRELUDE + "\n".join(head) + f"""
void runShader(const float* src, float* dst, float uK) {{
  for (unsigned wy = 0; wy < 12; ++wy)
    for (unsigned wx = 0; wx < {wx}; ++wx) {{
      U3 gl_WorkGroupID{{wx, wy, 0, {{wx, wy}}}};
      for (unsigned t = 0; t < 256; ++t) {{
        unsigned gl_LocalInvocationIndex = t;
        (void)gl_LocalInvocationIndex;
""" + "\n".join(load) + """
      }
      for (unsigned t = 0; t < 256; ++t) {
        U3 gl_LocalInvocationID{t % 16, t / 16, 0, {t % 16, t / 16}};
        U3 gl_GlobalInvocationID{gl_WorkGroupID.x * 16 + t % 16, gl_WorkGroupID.y * 16 + t / 16, 0, {0, 0}};
        (void)gl_GlobalInvocationID;
""" + "\n".join(body) + """
      }
    }
}
"""


MAIN = r"""
int main(int argc, char** argv) {
  const int sr = std::atoi(argv[1]), pr = std::atoi(argv[2]);
  const float h = 1.1f * 1.07f;
  std::vector<float> src(tv::kImagePixels), gpu(src.size()), cpu(src.size()), scratch;
  std::mt19937 rng(7);
  std::normal_distribution<float> noise(0.0f, 1.07f);
  for (int y = 0; y < 192; ++y)
    for (int x = 0; x < 256; ++x) {
      float v = 5000.0f + 0.2f * x;                                  // a gentle gradient
      if (x > 128) v += 40.0f;                                       // a step
      if (y > 100 && y < 140 && x > 20 && x < 90) v += ((x / 3 + y / 3) % 2) * 6.0f;  // fine texture
      const float dx = x - 200.0f, dy = y - 50.0f;
      v += 3000.0f * std::exp(-(dx * dx + dy * dy) / 50.0f);        // a hot spot
      src[size_t(y) * 256 + x] = v + noise(rng);
    }
  const float area = float((2 * pr + 1) * (2 * pr + 1));
  const float k = 1.44269504f / ((h * area) * (h * area));
  runShader(src.data(), gpu.data(), k);
  tv::nonLocalMeans(src.data(), cpu.data(), sr, pr, h, scratch);
  double maxd = 0, sum = 0, moved = 0;
  size_t worst = 0;
  for (size_t i = 0; i < src.size(); ++i) {
    const double d = std::fabs(double(gpu[i]) - cpu[i]);
    if (d > maxd) { maxd = d; worst = i; }
    sum += d;
    moved += std::fabs(double(cpu[i]) - src[i]);
  }
  std::printf("|shader - CPU reference| max %.2e counts at (%zu,%zu), mean %.2e; the filter moved pixels %.3f on average\n",
              maxd, worst % 256, worst / 256, sum / src.size(), moved / src.size());
  return maxd < 0.02 ? 0 : 1;
}
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--search", type=int, default=5)
    ap.add_argument("--patch", type=int, default=2)
    ap.add_argument("--pixels", type=int, nargs="+", default=[1, 2, 4])
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    gen_src = OUT / "gen.cpp"
    gen_src.write_text('#include <cstdio>\n#include <cstdlib>\n#include <string>\n'
                       'namespace tv { std::string gpuNlmShaderSource(int, int, int); }\n'
                       'int main(int, char** argv) { std::fputs(tv::gpuNlmShaderSource(std::atoi(argv[1]), '
                       'std::atoi(argv[2]), std::atoi(argv[3])).c_str(), stdout); }\n')
    inc = ["-I", str(ROOT / "native" / "core" / "include")]
    subprocess.run(["c++", "-std=c++20", "-O1", *inc, gen_src, ROOT / "native/android/gpu_nlm_shader.cpp",
                    "-o", OUT / "gen"], check=True)
    ok = True
    for n in args.pixels:
        glsl = subprocess.run([OUT / "gen", str(args.search), str(args.patch), str(n)], check=True,
                              capture_output=True, text=True).stdout
        emu = OUT / f"emulate_{n}.cpp"
        emu.write_text(transpile(glsl, n) + MAIN)
        exe = OUT / f"emulate_{n}"
        subprocess.run(["c++", "-std=c++20", "-O2", "-w", *inc, emu, ROOT / "native/core/src/filters.cpp",
                        "-o", exe], check=True)
        r = subprocess.run([exe, str(args.search), str(args.patch)], capture_output=True, text=True)
        print(f"search {args.search}, patch {args.patch}, {n} pixel(s) a thread: {r.stdout.strip()}")
        ok &= r.returncode == 0
    print("PASS" if ok else "FAIL")
    raise SystemExit(0 if ok else 1)


if __name__ == "__main__":
    main()
