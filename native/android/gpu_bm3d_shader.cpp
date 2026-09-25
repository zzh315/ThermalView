// gpu_bm3d_shader.h: the GPU BM3D's shaders, generated for one shape. They follow native/core's CPU
// reference (bm3d.cpp) step by step and in its order of operations: the same candidates, ranked by
// (distance, position), the same separable DCT passes, Walsh-Hadamard butterflies, thresholds and
// Wiener factors, the same weights and window. Only the aggregation differs: each workgroup (one
// reference block) sums its group over its search region into a tile of its own, and a gather pass
// adds the tiles over each pixel in a fixed order, so no atomics are needed and the result is
// deterministic.
//
// Written for the Adreno: every transform is unrolled with its coefficients as literals, one
// invocation per block row or column (8 values in registers), and every per-coefficient array is a
// set of registers, so nothing is indexed at run time but shared memory and one uniform array (the
// aggregation window). The text is also valid C++ after a few rewrites, so the Mac can run it.
#include "gpu_bm3d_shader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "tv/bm3d.h"
#include "tv/frame.h"

namespace tv {
namespace {

constexpr int K = 8;

std::string literal(float v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.9g", double(v));
  std::string s = buf;
  if (s.find_first_of(".en") == std::string::npos) s += ".0";
  return s;
}

std::string replaceAll(std::string text, const std::string& key, const std::string& value) {
  for (size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + value.size()))
    text.replace(at, key.size(), value);
  return text;
}

int positions(int n, int stride) {  // tv/bm3d.cpp's referencePositions: 0, stride, ... < n, and n - 1
  int count = 0, last = -1;
  for (int i = 0; i < n; i += std::max(stride, 1)) ++count, last = i;
  return last == n - 1 ? count : count + 1;
}

GpuBm3dShape clamped(GpuBm3dShape s) {
  s.search = std::clamp(s.search, 1, 7);
  s.group = s.group >= 8 ? 8 : s.group >= 4 ? 4 : 2;
  s.stride = std::clamp(s.stride, 1, 8);
  return s;
}

std::string var(const char* name, int i) { return name + std::to_string(i); }

// out_o = sum_i m(o, i) in_i for o, i in 0..7, each a chain of additions in i's order (as the CPU's
// loops), the coefficients as literals. in: the registers' prefix; store(o): where output o goes.
template <class Coef, class Store>
std::string transform8(const std::string& in, Coef m, Store store) {
  std::string s;
  for (int o = 0; o < K; ++o) {
    s += "    s = 0.0;";
    for (int i = 0; i < K; ++i) s += " s += " + literal(m(o, i)) + " * " + in + std::to_string(i) + ";";
    s += "\n    " + store(o) + " = s;\n";
  }
  return s;
}

// The normalized Walsh-Hadamard transform of registers name0..name(n-1), as bm3d.cpp's
// walshHadamard: the butterflies stage by stage, then the scale.
std::string wht(const char* name, int n) {
  std::string s;
  for (int len = 1; len < n; len <<= 1)
    for (int i = 0; i < n; i += len << 1)
      for (int j = i; j < i + len; ++j)
        s += "    { float wa = " + var(name, j) + ", wb = " + var(name, j + len) + "; " + var(name, j) + " = wa + wb; " +
             var(name, j + len) + " = wa - wb; }\n";
  for (int j = 0; j < n; ++j) s += "    " + var(name, j) + " *= WS;\n";
  return s;
}

const char* kHeader = R"(#version 310 es
precision highp float;
precision highp int;
layout(local_size_x = 64) in;
const int W = $W, H = $H, K = 8, KK = 64, S = $S, SPAN = $SPAN, NC = $NC, NCP = $NCP, N = $N;
const int STRIDE = $STRIDE, NX = $NX, NY = $NY, T = $T, TT = $TT, RX = $RX, RY = $RY;
const float WS = $WS;
const float INVALID = 3.0e38;
int refPos(int i, int n) {
  return min(i * STRIDE, n - 1);
}
)";

// The group's members: the reference, then the N - 1 nearest candidates by (distance, window order;
// window order is the image's scan order, as the CPU's partial sort on (distance, index)).
const char* kMatch = R"(
  for (int c = t; c < NCP; c += 64) {
    float d = INVALID;
    if (c < NC) {
      int dy = c / SPAN - S;
      int dx = c - (c / SPAN) * SPAN - S;
      int cx = rx + dx, cy = ry + dy;
      if (cx >= 0 && cx < NX && cy >= 0 && cy < NY && (dx != 0 || dy != 0)) {
        d = 0.0;
        for (int i = 0; i < K; ++i)
          for (int j = 0; j < K; ++j) {
            float e = $M[(S + i) * T + S + j] - $M[(S + dy + i) * T + S + dx + j];
            d += e * e;
          }
      }
    }
    dist[c] = d;
  }
  barrier();
  for (int c = t; c < NC; c += 64) {
    float d = dist[c];
    if (d < INVALID) {
      int rank = 0;
      for (int o = 0; o < NC; ++o) {
        float e = dist[o];
        rank += (e < d || (e == d && o < c)) ? 1 : 0;
      }
      if (rank < N - 1) pick[rank + 1] = c;
    }
  }
  if (t == 0) pick[0] = S * SPAN + S;
  barrier();
)";

// The per-cell tile sums: every block of the group that covers the cell, in the group's order (as the
// CPU adds them), weighted by weight and the window; the blocks' pixels in g.
const char* kTile = R"(
  int tile = (iy * RX + ix) * 2 * TT;
  for (int c = t; c < TT; c += 64) {
    int cy = c / T, cx = c - (c / T) * T;
    float num = 0.0, den = 0.0;
    for (int j = 0; j < N; ++j) {
      int pc = pick[j];
      int yy = cy - pc / SPAN, xx = cx - (pc - (pc / SPAN) * SPAN);
      if (yy >= 0 && yy < K && xx >= 0 && xx < K) {
        float w = weight * win[yy * K + xx];
        num += w * g[j * KK + yy * K + xx];
        den += w;
      }
    }
    tiles[tile + c] = num;
    tiles[tile + TT + c] = den;
  }
}
)";

const char* kGather = R"(layout(std430, binding = 0) readonly buffer Tiles { float tiles[]; };
layout(std430, binding = 1) writeonly buffer Out { float outv[]; };
layout(location = 0) uniform float uAdd;
void main() {
  int p = int(gl_GlobalInvocationID.x);
  if (p >= W * H) return;
  int py = p / W, px = p - (p / W) * W;
  float num = 0.0, den = 0.0;
  int ix0 = max(0, max(0, px - S - K + 1) / STRIDE - 1), ix1 = min(RX - 1, (px + S) / STRIDE + 1);
  int iy0 = max(0, max(0, py - S - K + 1) / STRIDE - 1), iy1 = min(RY - 1, (py + S) / STRIDE + 1);
  for (int iy = iy0; iy <= iy1; ++iy) {
    int ry = refPos(iy, NY);
    if (py < ry - S || py > ry + S + K - 1) continue;
    for (int ix = ix0; ix <= ix1; ++ix) {
      int rx = refPos(ix, NX);
      if (px < rx - S || px > rx + S + K - 1) continue;
      int base = (iy * RX + ix) * 2 * TT;
      int cell = (py - ry + S) * T + px - rx + S;
      num += tiles[base + cell];
      den += tiles[base + TT + cell];
    }
  }
  outv[p] = num / den + uAdd;
}
)";

const char* kStep1Head = R"(layout(std430, binding = 0) readonly buffer Src { float src[]; };
layout(std430, binding = 1) writeonly buffer Tiles { float tiles[]; };
layout(location = 0) uniform float uLevel;
layout(location = 1) uniform float uThr;
layout(location = 2) uniform float uSigma2;
layout(location = 3) uniform float uWin[64];
shared float region[TT];
shared float dist[NCP];
shared int pick[N];
shared float g[N * KK];
shared float tmp[N * KK];
shared int kept[64];
shared int keptAll;
shared float win[64];
void main() {
  int t = int(gl_LocalInvocationIndex);
  win[t] = uWin[t];  // (read once: the tile loop's lookups differ between invocations, and those would
                     // be serialized from uniform memory)
  int ix = int(gl_WorkGroupID.x), iy = int(gl_WorkGroupID.y);
  int rx = refPos(ix, NX), ry = refPos(iy, NY);
  for (int i = t; i < TT; i += 64) {
    int yy = i / T, xx = i - (i / T) * T;
    int gy = clamp(ry - S + yy, 0, H - 1), gx = clamp(rx - S + xx, 0, W - 1);
    region[i] = src[gy * W + gx] - uLevel;
  }
  barrier();
)";

const char* kStep2Head = R"(layout(std430, binding = 0) readonly buffer Src { float src[]; };
layout(std430, binding = 1) writeonly buffer Tiles { float tiles[]; };
layout(std430, binding = 2) readonly buffer Basic { float basic[]; };
layout(location = 0) uniform float uLevel;
layout(location = 1) uniform float uMuSigma2;
layout(location = 2) uniform float uSigma2;
layout(location = 3) uniform float uWin[64];
shared float regionB[TT];
shared float regionN[TT];
shared float dist[NCP];
shared int pick[N];
shared float g[N * KK];
shared float gb[N * KK];
shared float tmp[N * KK];
shared float part[64];
shared float energyAll;
shared float win[64];
void main() {
  int t = int(gl_LocalInvocationIndex);
  win[t] = uWin[t];  // (read once: the tile loop's lookups differ between invocations, and those would
                     // be serialized from uniform memory)
  int ix = int(gl_WorkGroupID.x), iy = int(gl_WorkGroupID.y);
  int rx = refPos(ix, NX), ry = refPos(iy, NY);
  for (int i = t; i < TT; i += 64) {
    int yy = i / T, xx = i - (i / T) * T;
    int gy = clamp(ry - S + yy, 0, H - 1), gx = clamp(rx - S + xx, 0, W - 1);
    regionB[i] = basic[gy * W + gx];
    regionN[i] = src[gy * W + gx] - uLevel;
  }
  barrier();
)";

class Generator {
 public:
  explicit Generator(const GpuBm3dShape& s) : s_(s), c_(bm3dDctMatrix(K)) {}

  // The group's blocks' 2D DCTs from `region` into `dst` (through tmp), as bm3d.cpp's blockDcts: each
  // block row's horizontal transform, then each column's vertical one.
  std::string forward(const std::string& region, const std::string& dst) const {
    std::string s = "  if (t < N * 8) {\n    int j = t >> 3, r = t & 7;\n    int pc = pick[j];\n";
    s += "    int base = (pc / SPAN + r) * T + (pc - (pc / SPAN) * SPAN);\n    float s;\n";
    for (int i = 0; i < K; ++i) s += "    float p" + std::to_string(i) + " = " + region + "[base + " + std::to_string(i) + "];\n";
    s += transform8("p", [&](int u, int x) { return c_[size_t(u * K + x)]; },
                    [](int u) { return "tmp[j * KK + r * K + " + std::to_string(u) + "]"; });
    s += "  }\n  barrier();\n  if (t < N * 8) {\n    int j = t >> 3, u = t & 7;\n    float s;\n";
    for (int i = 0; i < K; ++i) s += "    float h" + std::to_string(i) + " = tmp[j * KK + " + std::to_string(i * K) + " + u];\n";
    s += transform8("h", [&](int v, int i) { return c_[size_t(v * K + i)]; },
                    [&](int v) { return dst + "[j * KK + " + std::to_string(v * K) + " + u]"; });
    s += "  }\n  barrier();\n";
    return s;
  }

  // Each block's inverse DCT, g's coefficients to g's pixels (through tmp), as bm3d.cpp's aggregate:
  // along u first (tmp[v][x] = sum_u coef[v][u] c[u][x]), then along v (sum_v c[v][y] tmp[v][x]).
  std::string inverse() const {
    std::string s = "  if (t < N * 8) {\n    int j = t >> 3, v = t & 7;\n    float s;\n";
    for (int u = 0; u < K; ++u) s += "    float q" + std::to_string(u) + " = g[j * KK + v * K + " + std::to_string(u) + "];\n";
    s += transform8("q", [&](int x, int u) { return c_[size_t(u * K + x)]; },
                    [](int x) { return "tmp[j * KK + v * K + " + std::to_string(x) + "]"; });
    s += "  }\n  barrier();\n  if (t < N * 8) {\n    int j = t >> 3, x = t & 7;\n    float s;\n";
    for (int v = 0; v < K; ++v) s += "    float m" + std::to_string(v) + " = tmp[j * KK + " + std::to_string(v * K) + " + x];\n";
    s += transform8("m", [&](int y, int v) { return c_[size_t(v * K + y)]; },
                    [](int y) { return "g[j * KK + " + std::to_string(y * K) + " + x]"; });
    s += "  }\n  barrier();\n";
    return s;
  }

  std::string loadGroup(const char* name, const char* from) const {
    std::string s;
    for (int j = 0; j < s_.group; ++j)
      s += "    float " + var(name, j) + " = " + from + "[" + std::to_string(j * 64) + " + t];\n";
    return s;
  }
  std::string storeGroup(const char* name) const {
    std::string s;
    for (int j = 0; j < s_.group; ++j) s += "    g[" + std::to_string(j * 64) + " + t] = " + var(name, j) + ";\n";
    return s;
  }

  std::string match(const char* region) const {
    std::string m = replaceAll(kMatch, "$M", region);
    if (s_.ablate & 2) m = replaceAll(m, "d += e * e;", "d = float(c);");
    if (s_.ablate & 1)
      m = replaceAll(m, "      int rank = 0;\n      for (int o = 0; o < NC; ++o) {\n        float e = dist[o];\n"
                        "        rank += (e < d || (e == d && o < c)) ? 1 : 0;\n      }\n",
                     "      int rank = c < N - 1 ? c : N;\n");
    return m;
  }

  std::string step1() const {
    std::string s = kStep1Head;
    s += match("region");
    s += forward("region", "g");
    s += "  {\n" + loadGroup("v", "g") + wht("v", s_.group) + "    int k = 0;\n";
    for (int j = 0; j < s_.group; ++j) {  // (the group's mean, coefficient 0 of block 0, always stays)
      const std::string v = var("v", j);
      s += "    if (" + v + " * " + v + " < uThr" + (j == 0 ? " && t != 0" : "") + ") " + v + " = 0.0; else k += 1;\n";
    }
    s += "    kept[t] = k;\n" + wht("v", s_.group) + storeGroup("v") + "  }\n  barrier();\n";
    s += "  if (t == 0) {\n    int sum = 0;\n    for (int q = 0; q < 64; ++q) sum += kept[q];\n    keptAll = sum;\n  }\n";
    s += "  barrier();\n  float weight = 1.0 / (float(keptAll) * uSigma2);\n";
    s += inverse();
    s += kTile;
    return s;
  }

  std::string step2() const {
    std::string s = kStep2Head;
    s += match("regionB");
    s += forward("regionN", "g");
    s += forward("regionB", "gb");
    s += "  {\n" + loadGroup("v", "g") + loadGroup("b", "gb") + wht("v", s_.group) + wht("b", s_.group);
    s += "    float e = 0.0;\n    float wf;\n";
    for (int j = 0; j < s_.group; ++j) {  // (the group's mean keeps factor 1)
      const std::string v = var("v", j), b = var("b", j);
      const std::string f = b + " * " + b + " / (" + b + " * " + b + " + uMuSigma2)";
      s += "    wf = " + (j == 0 ? "t != 0 ? " + f + " : 1.0" : f) + ";\n";
      s += "    " + v + " *= wf;\n    e += wf * wf * uSigma2;\n";
    }
    s += "    part[t] = e;\n" + wht("v", s_.group) + storeGroup("v") + "  }\n  barrier();\n";
    s += "  if (t == 0) {\n    float sum = 0.0;\n    for (int q = 0; q < 64; ++q) sum += part[q];\n    energyAll = sum;\n  }\n";
    s += "  barrier();\n  float weight = energyAll > 1.0e-20 ? 1.0 / energyAll : 1.0 / uSigma2;\n";
    s += inverse();
    s += kTile;
    return s;
  }

 private:
  GpuBm3dShape s_;
  std::vector<float> c_;
};

}  // namespace

int gpuBm3dRefsX(const GpuBm3dShape& shape) { return positions(kFrameWidth - K + 1, clamped(shape).stride); }
int gpuBm3dRefsY(const GpuBm3dShape& shape) { return positions(kImageRows - K + 1, clamped(shape).stride); }
int gpuBm3dTileSide(const GpuBm3dShape& shape) { return K + 2 * clamped(shape).search; }
std::vector<float> gpuBm3dWindow(const GpuBm3dShape& shape) { return bm3dKaiserWindow(K, clamped(shape).kaiser); }

std::string gpuBm3dShaderSource(GpuBm3dPass pass, const GpuBm3dShape& shape0) {
  const GpuBm3dShape s = clamped(shape0);
  const int span = 2 * s.search + 1, nc = span * span, side = gpuBm3dTileSide(s);
  const Generator gen(s);
  std::string text = kHeader;
  text += pass == GpuBm3dPass::Step1 ? gen.step1() : pass == GpuBm3dPass::Step2 ? gen.step2() : std::string(kGather);
  const auto num = [](int v) { return std::to_string(v); };
  text = replaceAll(text, "$WS", literal(1.0f / std::sqrt(float(s.group))));
  text = replaceAll(text, "$W", num(kFrameWidth));
  text = replaceAll(text, "$H", num(kImageRows));
  text = replaceAll(text, "$SPAN", num(span));
  text = replaceAll(text, "$NCP", num((nc + 63) / 64 * 64));
  text = replaceAll(text, "$NC", num(nc));
  text = replaceAll(text, "$NX", num(kFrameWidth - K + 1));
  text = replaceAll(text, "$NY", num(kImageRows - K + 1));
  text = replaceAll(text, "$N", num(s.group));
  text = replaceAll(text, "$STRIDE", num(s.stride));
  text = replaceAll(text, "$S", num(s.search));
  text = replaceAll(text, "$TT", num(side * side));
  text = replaceAll(text, "$T", num(side));
  text = replaceAll(text, "$RX", num(gpuBm3dRefsX(s)));
  text = replaceAll(text, "$RY", num(gpuBm3dRefsY(s)));
  return text;
}

}  // namespace tv
