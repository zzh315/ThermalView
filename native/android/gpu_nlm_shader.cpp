// gpu_nlm.h's shader source (no GL here, so the Mac can generate and validate it).
#include <algorithm>
#include <string>

#include "tv/frame.h"

namespace tv {
namespace {

std::string name(const char* prefix, int a, int b) {
  auto n = [](int v) { return v < 0 ? "m" + std::to_string(-v) : std::to_string(v); };
  return std::string(prefix) + n(a) + "_" + n(b);
}

}  // namespace

// Each thread filters n adjacent pixels of a row. Its own patches' pixels (rows -pr..pr, columns
// -pr..n-1+pr around the first pixel) stay in registers. For every vertical offset dy, the partner
// pixels come in one column at a time as the unrolled dx steps slide right, so each step loads
// 2pr + 1 values. Per step: the columns' |difference| sums, then each pixel's patch sum by sliding
// along the row, its weight exp(-(mean |difference| / h)^2) and the running sums. Offset (0, 0)
// has distance 0, so it gives the pixel itself at weight 1, as the CPU version.
std::string gpuNlmShaderSource(int searchRadius, int patchRadius, int pixelsPerThread) {
  const int sr = std::clamp(searchRadius, 1, 7), pr = std::clamp(patchRadius, 0, 3);
  const int n = pixelsPerThread;
  const int r = sr + pr;
  const int tw = 16 * n + 2 * r, th = 16 + 2 * r;
  std::string s;
  auto line = [&s](const std::string& text) { s += text + "\n"; };
  line("#version 310 es");
  line("layout(local_size_x = 16, local_size_y = 16) in;");
  line("layout(std430, binding = 0) readonly buffer Src { float src[]; };");
  line("layout(std430, binding = 1) writeonly buffer Dst { float dst[]; };");
  line("layout(location = 0) uniform float uK;  // log2(e) / (h * patch area)^2");
  line("const int TW = " + std::to_string(tw) + ";");
  line("shared float tile[" + std::to_string(tw * th) + "];");
  line("int reflectIndex(int k, int n) {");
  line("  k = abs(k);");
  line("  return k >= n ? 2 * (n - 1) - k : k;");
  line("}");
  line("void main() {");
  line("  ivec2 origin = ivec2(gl_WorkGroupID.xy) * ivec2(" + std::to_string(16 * n) + ", 16) - " + std::to_string(r) + ";");
  line("  for (int i = int(gl_LocalInvocationIndex); i < " + std::to_string(tw * th) + "; i += 256) {");
  line("    int ty = i / TW;");
  line("    int tx = i - ty * TW;");
  line("    tile[i] = src[reflectIndex(origin.y + ty, " + std::to_string(kImageRows) + ") * " + std::to_string(kFrameWidth) +
       " + reflectIndex(origin.x + tx, " + std::to_string(kFrameWidth) + ")];");
  line("  }");
  line("  barrier();");
  line("  int base = (int(gl_LocalInvocationID.y) + " + std::to_string(r) + ") * TW + int(gl_LocalInvocationID.x) * " +
       std::to_string(n) + " + " + std::to_string(r) + ";");
  for (int ky = -pr; ky <= pr; ++ky)
    for (int c = -pr; c <= n - 1 + pr; ++c)
      line("  float " + name("o", ky, c) + " = tile[base + " + std::to_string(ky * tw + c) + "];");
  for (int i = 0; i < n; ++i) line("  float n" + std::to_string(i) + " = 0.0, d" + std::to_string(i) + " = 0.0;");
  line("  for (int dy = -" + std::to_string(sr) + "; dy <= " + std::to_string(sr) + "; ++dy) {");
  line("    int b = base + dy * TW;");
  int loaded = -sr - pr - 1;  // partner columns a (relative to the first pixel) loaded so far: up to here
  for (int dx = -sr; dx <= sr; ++dx) {
    for (int a = loaded + 1; a <= dx + n - 1 + pr; ++a)
      for (int ky = -pr; ky <= pr; ++ky)
        line("    float " + name("q", ky, a) + " = tile[b + " + std::to_string(ky * tw + a) + "];");
    loaded = dx + n - 1 + pr;
    line("    {  // dx " + std::to_string(dx));
    for (int c = -pr; c <= n - 1 + pr; ++c) {
      std::string d = "      float " + name("D", 0, c) + " = ";
      for (int ky = -pr; ky <= pr; ++ky) {
        if (ky > -pr) d += " + ";
        d += "abs(" + name("o", ky, c) + " - " + name("q", ky, dx + c) + ")";
      }
      line(d + ";");
    }
    std::string sum = "      float S = ";
    for (int c = -pr; c <= pr; ++c) sum += (c > -pr ? " + " : "") + name("D", 0, c);
    line(sum + ";");
    for (int i = 0; i < n; ++i) {
      if (i > 0) line("      S += " + name("D", 0, i + pr) + " - " + name("D", 0, i - 1 - pr) + ";");
      line("      { float w = exp2(-S * S * uK); n" + std::to_string(i) + " += w * " + name("q", 0, dx + i) + "; d" +
           std::to_string(i) + " += w; }");
    }
    line("    }");
  }
  line("  }");
  line("  int g = int(gl_GlobalInvocationID.y) * " + std::to_string(kFrameWidth) + " + int(gl_WorkGroupID.x) * " +
       std::to_string(16 * n) + " + int(gl_LocalInvocationID.x) * " + std::to_string(n) + ";");
  for (int i = 0; i < n; ++i)
    line("  dst[g + " + std::to_string(i) + "] = n" + std::to_string(i) + " / d" + std::to_string(i) + ";");
  line("}");
  return s;
}

}  // namespace tv
