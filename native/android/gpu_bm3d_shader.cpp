// gpu_bm3d_shader.h: the GPU BM3D's shaders, generated for one shape. They follow native/core's CPU
// reference (bm3d.cpp) step by step and in its order of operations: the same candidates, ranked by
// (distance, position), the same separable DCT passes, Walsh-Hadamard butterflies, thresholds and
// Wiener factors, the same weights and window. Only the aggregation differs: each workgroup (one
// reference block) sums its group over its search region into a tile of its own, and a gather pass
// adds the tiles over each pixel in a fixed order, so no atomics are needed and the result is
// deterministic.
//
// Written for the Adreno, where a workgroup's shared memory limits how many run at once: every
// transform is unrolled with its coefficients as literals and done in place (one invocation per block
// row or column, 8 values in registers), the candidates' selection borrows the group's array before
// the group needs it, and step 2 reads the noisy blocks straight from the image. Nothing is indexed at
// run time but shared memory. The text is also valid C++ after a few rewrites, so the Mac can run it.
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

std::string num(int v) { return std::to_string(v); }
std::string var(const char* name, int i) { return name + num(i); }

// out_o = sum_i m(o, i) in_i for o, i in 0..7, each a chain of additions in i's order (as the CPU's
// loops), the coefficients as literals. in: the registers' prefix; store(o): where output o goes.
template <class Coef, class Store>
std::string transform8(const std::string& in, Coef m, Store store) {
  std::string s;
  for (int o = 0; o < K; ++o) {
    s += "    s = 0.0;";
    for (int i = 0; i < K; ++i) s += " s += " + literal(m(o, i)) + " * " + in + num(i) + ";";
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
const int W = $W, H = $H, K = 8, KK = 64, S = $S, SPAN = $SPAN, NC = $NC, N = $N, GS = $GS;
const int STRIDE = $STRIDE, NX = $NX, NY = $NY, T = $T, TT = $TT, RX = $RX, RY = $RY;
const float WS = $WS;
const float INVALID = 3.0e38;
int refPos(int i, int n) {
  return min(i * STRIDE, n - 1);
}
)";

// The selection, after each invocation's candidates' distances (dK, cK) and its nearest (bd, bc):
// the (N-1)-th nearest of the 64 invocations' nearest (at least N - 1 candidates are at or under it,
// so the N - 1 nearest overall are too), then only the candidates at or under it ranked among
// themselves, by (distance, window order: the image's scan order, as the CPU's partial sort on
// (distance, index)). Its scratch is g's first 256 values, which the group doesn't need yet.
const char* kSelect = R"(
  g[t] = bd;
  g[64 + t] = float(bc);
  if (t == 0) finals = 0;
  barrier();
  {
    int r = 0;
    for (int o = 0; o < 64; ++o) {
      float e = g[o], ec = g[64 + o];
      r += (e < bd || (e == bd && ec < float(bc))) ? 1 : 0;
    }
    if (r == N - 2) {
      tauD = bd;
      tauC = bc;
    }
  }
  barrier();
  {
    float td = tauD;
    int tc = tauC;
$FINALIST
  }
  barrier();
  {
    int nf = finals;
    if (t < nf) {
      float d = g[128 + t], c = g[192 + t];
      int rank = 0;
      for (int o = 0; o < nf; ++o) rank += (g[128 + o] < d || (g[128 + o] == d && g[192 + o] < c)) ? 1 : 0;
      if (rank < N - 1) pick[rank + 1] = int(c);
    }
  }
  if (t == 0) pick[0] = S * SPAN + S;
  barrier();
  if (t < N) {
    offY[t] = pick[t] / SPAN;
    offX[t] = pick[t] - (pick[t] / SPAN) * SPAN;
  }
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
      int yy = cy - offY[j], xx = cx - offX[j];
      if (yy >= 0 && yy < K && xx >= 0 && xx < K) {
        float w = weight * win[yy * K + xx];
        num += w * g[j * KK + yy * K + xx];
        den += w;
      }
    }
    tiles[tile + 2 * c] = num;
    tiles[tile + 2 * c + 1] = den;
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
      int at = (iy * RX + ix) * 2 * TT + 2 * ((py - ry + S) * T + px - rx + S);
      num += tiles[at];
      den += tiles[at + 1];
    }
  }
  outv[p] = num / den + uAdd;
}
)";

const char* kCommonShared = R"(shared float g[GS];
shared int pick[N];
shared int offY[N];
shared int offX[N];
shared float tauD;
shared int tauC;
shared int finals;
shared float win[64];
)";

const char* kPrologue = R"(void main() {
  int t = int(gl_LocalInvocationIndex);
  int ix = int(gl_WorkGroupID.x), iy = int(gl_WorkGroupID.y);
  int rx = refPos(ix, NX), ry = refPos(iy, NY);
  win[t] = uWin[t];  // (the tile loop's lookups differ between invocations: from shared memory, not uniforms)
)";

class Generator {
 public:
  explicit Generator(const GpuBm3dShape& s) : s_(s), c_(bm3dDctMatrix(K)) {}

  int perThread() const {  // candidates per invocation: the window's positions over 64 invocations
    const int span = 2 * s_.search + 1;
    return (span * span + 63) / 64;
  }

  // Each invocation's candidates' distances to the reference block in `m`, row by row (the
  // reference row in registers), each candidate's sum in the CPU's order; then its nearest.
  std::string match(const char* m) const {
    const int cpt = perThread();
    std::string s;
    for (int k = 0; k < cpt; ++k) {
      const std::string K_ = num(k);
      s += "  int c" + K_ + " = t + " + num(64 * k) + ";\n";
      s += "  int q" + K_ + " = min(c" + K_ + ", NC - 1);\n";
      s += "  int dy" + K_ + " = q" + K_ + " / SPAN - S, dx" + K_ + " = q" + K_ + " - (q" + K_ + " / SPAN) * SPAN - S;\n";
      s += "  bool ok" + K_ + " = c" + K_ + " < NC && rx + dx" + K_ + " >= 0 && rx + dx" + K_ + " < NX && ry + dy" + K_ +
           " >= 0 && ry + dy" + K_ + " < NY && (dx" + K_ + " != 0 || dy" + K_ + " != 0);\n";
      s += "  int base" + K_ + " = (S + dy" + K_ + ") * T + S + dx" + K_ + ";\n";
      s += "  float d" + K_ + " = 0.0;\n";
    }
    s += "  for (int i = 0; i < K; ++i) {\n    int ra = (S + i) * T + S;\n";
    for (int j = 0; j < K; ++j) s += "    float r" + num(j) + " = " + std::string(m) + "[ra + " + num(j) + "];\n";
    for (int k = 0; k < cpt; ++k) {
      const std::string K_ = num(k);
      s += "    {\n      int rb = base" + K_ + " + i * T;\n      float e;\n";
      for (int j = 0; j < K; ++j)
        s += "      e = r" + num(j) + " - " + std::string(m) + "[rb + " + num(j) + "]; d" + K_ + " += e * e;\n";
      s += "    }\n";
    }
    s += "  }\n";
    for (int k = 0; k < cpt; ++k) {
      const std::string K_ = num(k);
      if (s_.ablate & 2) s += "  d" + K_ + " = float(c" + K_ + ");\n";  // (profiling: no real distances)
      s += "  if (!ok" + K_ + ") d" + K_ + " = INVALID;\n";
    }
    s += "  float bd = d0;\n  int bc = c0;\n";
    for (int k = 1; k < cpt; ++k) {
      const std::string d = "d" + num(k), c = "c" + num(k);
      s += "  if (" + d + " < bd || (" + d + " == bd && " + c + " < bc)) {\n    bd = " + d + ";\n    bc = " + c + ";\n  }\n";
    }
    std::string finalist;
    for (int k = 0; k < cpt; ++k) {
      const std::string d = "d" + num(k), c = "c" + num(k);
      finalist += "    if (" + d + " < INVALID && (" + d + " < td || (" + d + " == td && " + c + " <= tc))) {\n"
                  "      int slot = atomicAdd(finals, 1);\n      g[128 + slot] = " + d + ";\n      g[192 + slot] = float(" +
                  c + ");\n    }\n";
    }
    std::string sel = replaceAll(kSelect, "$FINALIST", finalist);
    if (s_.ablate & 1)  // (profiling: the first candidates, unranked)
      sel = replaceAll(sel, "      if (rank < N - 1) pick[rank + 1] = int(c);", "      if (t < N - 1) pick[t + 1] = int(c);");
    return s + sel;
  }

  // The group's blocks' 2D DCTs into `dst`, as bm3d.cpp's blockDcts: each block row's horizontal
  // transform from `pixel(row, column)`, then in place each column's vertical one.
  template <class Pixel>
  std::string forward(Pixel pixel, const std::string& dst) const {
    std::string s = "  if (t < N * 8) {\n    int j = t >> 3, r = t & 7;\n    float s;\n";
    for (int x = 0; x < K; ++x) s += "    float p" + num(x) + " = " + pixel(x) + ";\n";
    s += transform8("p", [&](int u, int x) { return c_[size_t(u * K + x)]; },
                    [&](int u) { return dst + "[j * KK + r * K + " + num(u) + "]"; });
    s += "  }\n  barrier();\n  if (t < N * 8) {\n    int j = t >> 3, u = t & 7;\n    float s;\n";
    for (int i = 0; i < K; ++i) s += "    float h" + num(i) + " = " + dst + "[j * KK + " + num(i * K) + " + u];\n";
    s += transform8("h", [&](int v, int i) { return c_[size_t(v * K + i)]; },
                    [&](int v) { return dst + "[j * KK + " + num(v * K) + " + u]"; });
    s += "  }\n  barrier();\n";
    return s;
  }
  std::string forwardFromRegion(const std::string& region, const std::string& dst) const {
    return forward([&](int x) { return region + "[(offY[j] + r) * T + offX[j] + " + num(x) + "]"; }, dst);
  }

  // Each block's inverse DCT in place in g, as bm3d.cpp's aggregate: along u first
  // (tmp[v][x] = sum_u coef[v][u] c[u][x]), then along v (sum_v c[v][y] tmp[v][x]).
  std::string inverse() const {
    std::string s = "  if (t < N * 8) {\n    int j = t >> 3, v = t & 7;\n    float s;\n";
    for (int u = 0; u < K; ++u) s += "    float q" + num(u) + " = g[j * KK + v * K + " + num(u) + "];\n";
    s += transform8("q", [&](int x, int u) { return c_[size_t(u * K + x)]; },
                    [](int x) { return "g[j * KK + v * K + " + num(x) + "]"; });
    s += "  }\n  barrier();\n  if (t < N * 8) {\n    int j = t >> 3, x = t & 7;\n    float s;\n";
    for (int v = 0; v < K; ++v) s += "    float m" + num(v) + " = g[j * KK + " + num(v * K) + " + x];\n";
    s += transform8("m", [&](int y, int v) { return c_[size_t(v * K + y)]; },
                    [](int y) { return "g[j * KK + " + num(y * K) + " + x]"; });
    s += "  }\n  barrier();\n";
    return s;
  }

  std::string loadGroup(const char* name, const char* from) const {
    std::string s;
    for (int j = 0; j < s_.group; ++j) s += "    float " + var(name, j) + " = " + from + "[" + num(j * 64) + " + t];\n";
    return s;
  }
  std::string storeGroup(const char* name) const {
    std::string s;
    for (int j = 0; j < s_.group; ++j) s += "    g[" + num(j * 64) + " + t] = " + var(name, j) + ";\n";
    return s;
  }

  // (profiling: 4 leaves the tile sums out)
  std::string tileCode() const {
    if (!(s_.ablate & 4)) return kTile;
    return "  int tile = (iy * RX + ix) * 2 * TT;\n  for (int c = t; c < TT; c += 64) {\n    tiles[tile + 2 * c] = weight;\n"
           "    tiles[tile + 2 * c + 1] = 1.0;\n  }\n}\n";
  }

  std::string step1() const {
    std::string s = R"(layout(std430, binding = 0) readonly buffer Src { float src[]; };
layout(std430, binding = 1) writeonly buffer Tiles { float tiles[]; };
layout(location = 0) uniform float uLevel;
layout(location = 1) uniform float uThr;
layout(location = 2) uniform float uSigma2;
layout(location = 3) uniform float uWin[64];
shared float region[TT];
shared int keptAll;
)";
    s += kCommonShared;
    s += kPrologue;
    s += R"(  for (int i = t; i < TT; i += 64) {
    int yy = i / T, xx = i - (i / T) * T;
    int gy = clamp(ry - S + yy, 0, H - 1), gx = clamp(rx - S + xx, 0, W - 1);
    region[i] = src[gy * W + gx] - uLevel;
  }
  if (t == 0) keptAll = 0;
  barrier();
)";
    s += match("region");
    s += forwardFromRegion("region", "g");
    s += "  {\n" + loadGroup("v", "g") + wht("v", s_.group) + "    int k = 0;\n";
    for (int j = 0; j < s_.group; ++j) {  // (the group's mean, coefficient 0 of block 0, always stays)
      const std::string v = var("v", j);
      s += "    if (" + v + " * " + v + " < uThr" + (j == 0 ? " && t != 0" : "") + ") " + v + " = 0.0; else k += 1;\n";
    }
    s += "    atomicAdd(keptAll, k);\n" + wht("v", s_.group) + storeGroup("v") + "  }\n  barrier();\n";
    s += "  float weight = 1.0 / (float(keptAll) * uSigma2);\n";
    s += inverse();
    s += tileCode();
    return s;
  }

  std::string step2() const {
    std::string s = R"(layout(std430, binding = 0) readonly buffer Src { float src[]; };
layout(std430, binding = 1) writeonly buffer Tiles { float tiles[]; };
layout(std430, binding = 2) readonly buffer Basic { float basic[]; };
layout(location = 0) uniform float uLevel;
layout(location = 1) uniform float uMuSigma2;
layout(location = 2) uniform float uSigma2;
layout(location = 3) uniform float uWin[64];
shared float regionB[TT];
shared float gb[N * KK];
shared float part[64];
shared float energyAll;
)";
    s += kCommonShared;
    s += kPrologue;
    s += R"(  for (int i = t; i < TT; i += 64) {
    int yy = i / T, xx = i - (i / T) * T;
    int gy = clamp(ry - S + yy, 0, H - 1), gx = clamp(rx - S + xx, 0, W - 1);
    regionB[i] = basic[gy * W + gx];
  }
  barrier();
)";
    s += match("regionB");
    // (the noisy blocks straight from the image: they lie inside it, as their region cells)
    s += forward([&](int x) { return "(src[(ry - S + offY[j] + r) * W + rx - S + offX[j] + " + num(x) + "] - uLevel)"; }, "g");
    s += forwardFromRegion("regionB", "gb");
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
    s += tileCode();
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
  text = replaceAll(text, "$WS", literal(1.0f / std::sqrt(float(s.group))));
  text = replaceAll(text, "$W", num(kFrameWidth));
  text = replaceAll(text, "$H", num(kImageRows));
  text = replaceAll(text, "$SPAN", num(span));
  text = replaceAll(text, "$NC", num(nc));
  text = replaceAll(text, "$NX", num(kFrameWidth - K + 1));
  text = replaceAll(text, "$NY", num(kImageRows - K + 1));
  text = replaceAll(text, "$N", num(s.group));
  text = replaceAll(text, "$GS", num(std::max(s.group * 64, 256)));
  text = replaceAll(text, "$STRIDE", num(s.stride));
  text = replaceAll(text, "$S", num(s.search));
  text = replaceAll(text, "$TT", num(side * side));
  text = replaceAll(text, "$T", num(side));
  text = replaceAll(text, "$RX", num(gpuBm3dRefsX(s)));
  text = replaceAll(text, "$RY", num(gpuBm3dRefsY(s)));
  return text;
}

}  // namespace tv
