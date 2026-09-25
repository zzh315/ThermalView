// Palettes (docs/PLAN.md M5): control points in palettes/*.json, baked into a 1024-entry sRGB table
// that the display samples after upscaling the scalar intensity (so interpolation never mixes
// colors). white_hot interpolates in OKLab, with lightness linear in intensity so equal steps look
// equal; rainbow_hc follows the hue arc in OKLCh with chroma kept as high as sRGB allows.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace tv {

using Rgb = std::array<float, 3>;  // sRGB components in [0, 1]
using Lab = std::array<float, 3>;  // OKLab: L in [0, 1], a, b

struct PaletteSpec {
  enum class Space { OkLab, OkLch };
  std::string name;
  Space space = Space::OkLab;
  std::vector<std::pair<float, Rgb>> stops;  // positions ascending from 0 to 1
  bool maxChroma = true;                     // OKLCh: each hue at sRGB's most vivid ("chroma": "max")
  bool stopLightness = false;                // OKLCh: lightness from the stops, chroma as vivid as sRGB allows there ("chroma": "lightness")
  float chromaScale = 1.0f;                  // OKLCh: this share of that chroma ("chromaScale", 0..1)
  Rgb saturation{0.5f, 0.5f, 0.5f};          // pixels at the camera's clip (too hot to measure)
  // With the range locked (M6), pixels above and below it, for palettes that mark them ("lockedAbove",
  // "lockedBelow"; the owner, 2026-09-26: grey in the rainbow, as Xtherm; white hot just clips).
  bool marksLocked = false;
  Rgb lockedAbove{0.78f, 0.78f, 0.78f}, lockedBelow{0.33f, 0.33f, 0.33f};
};

// Reads a palette file's text: {"name": ..., "space": "oklab" | "oklch", "chroma": "max" |
// "interpolate" | "lightness", "chromaScale": 0..1, "stops": [[0.0, "#RRGGBB"], ...], "saturation":
// "#RRGGBB", "lockedAbove": "#RRGGBB", "lockedBelow": "#RRGGBB"}. Other keys are ignored. False, with a
// reason, on anything else.
bool parsePalette(const std::string& json, PaletteSpec* out, std::string* error);

// The table: entry i is the color at intensity i / (n - 1), in 8-bit sRGB.
std::vector<std::array<uint8_t, 3>> buildPaletteLut(const PaletteSpec& spec, int n = 1024);

// The color at intensity t in [0, 1], before quantization.
Rgb paletteColor(const PaletteSpec& spec, float t);

// sRGB <-> OKLab (Björn Ottosson, 2020). oklabToSrgb doesn't clip: out-of-gamut colors come back
// with components outside [0, 1].
Lab srgbToOklab(const Rgb& rgb);
Rgb oklabToSrgb(const Lab& lab);

}  // namespace tv
