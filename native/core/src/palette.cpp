#include "tv/palette.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace tv {
namespace {

float toLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float toGamma(float c) { return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }

bool inGamut(const Rgb& c) {
  constexpr float tol = 1e-4f;
  return std::all_of(c.begin(), c.end(), [](float v) { return v >= -tol && v <= 1.0f + tol; });
}

Rgb clip(const Rgb& c) { return {std::clamp(c[0], 0.0f, 1.0f), std::clamp(c[1], 0.0f, 1.0f), std::clamp(c[2], 0.0f, 1.0f)}; }

bool parseHex(const std::string& s, Rgb* out) {
  if (s.size() != 7 || s[0] != '#') return false;
  for (int k = 0; k < 3; ++k) {
    char* end = nullptr;
    const std::string byte = s.substr(size_t(1 + 2 * k), 2);
    const long v = std::strtol(byte.c_str(), &end, 16);
    if (end != byte.c_str() + 2) return false;
    (*out)[size_t(k)] = float(v) / 255.0f;
  }
  return true;
}

// Just enough JSON for the palette files: objects, arrays, strings, numbers; unknown keys skipped.
struct Cursor {
  const std::string& s;
  size_t i = 0;
  void ws() {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  }
  bool eat(char c) {
    ws();
    if (i < s.size() && s[i] == c) {
      ++i;
      return true;
    }
    return false;
  }
  bool string(std::string* out) {
    ws();
    if (i >= s.size() || s[i] != '"') return false;
    out->clear();
    for (++i; i < s.size() && s[i] != '"'; ++i) {
      if (s[i] == '\\' && i + 1 < s.size()) ++i;
      out->push_back(s[i]);
    }
    if (i >= s.size()) return false;
    ++i;
    return true;
  }
  bool number(double* out) {
    ws();
    const char* start = s.c_str() + i;
    char* end = nullptr;
    *out = std::strtod(start, &end);
    if (end == start) return false;
    i += size_t(end - start);
    return true;
  }
  bool skip() {
    ws();
    if (i >= s.size()) return false;
    const char c = s[i];
    if (c == '"') {
      std::string t;
      return string(&t);
    }
    if (c == '{' || c == '[') {
      const char close = c == '{' ? '}' : ']';
      ++i;
      if (eat(close)) return true;
      do {
        std::string key;
        if (c == '{' && (!string(&key) || !eat(':'))) return false;
        if (!skip()) return false;
      } while (eat(','));
      return eat(close);
    }
    const size_t start = i;
    while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+' || s[i] == '.')) ++i;
    return i > start;
  }
};

}  // namespace

Lab srgbToOklab(const Rgb& rgb) {
  const float r = toLinear(rgb[0]), g = toLinear(rgb[1]), b = toLinear(rgb[2]);
  const float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
  const float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
  const float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
  return {0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
          1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
          0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}

Rgb oklabToSrgb(const Lab& lab) {
  const float l_ = lab[0] + 0.3963377774f * lab[1] + 0.2158037573f * lab[2];
  const float m_ = lab[0] - 0.1055613458f * lab[1] - 0.0638541728f * lab[2];
  const float s_ = lab[0] - 0.0894841775f * lab[1] - 1.2914855480f * lab[2];
  const float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
  const float r = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
  const float g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
  const float b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
  auto encode = [](float c) { return c < 0.0f ? -toGamma(-c) : toGamma(c); };  // signed, for gamut checks
  return {encode(r), encode(g), encode(b)};
}

Rgb paletteColor(const PaletteSpec& spec, float t) {
  const auto& stops = spec.stops;
  if (stops.empty()) return {t, t, t};
  t = std::clamp(t, 0.0f, 1.0f);
  if (t <= stops.front().first) return stops.front().second;
  if (t >= stops.back().first) return stops.back().second;
  size_t k = 0;
  while (k + 2 < stops.size() && t > stops[k + 1].first) ++k;
  const float span = stops[k + 1].first - stops[k].first;
  const float u = span > 0.0f ? (t - stops[k].first) / span : 0.0f;
  const Lab a = srgbToOklab(stops[k].second), b = srgbToOklab(stops[k + 1].second);
  if (spec.space == PaletteSpec::Space::OkLab) {
    const Lab lab{a[0] + u * (b[0] - a[0]), a[1] + u * (b[1] - a[1]), a[2] + u * (b[2] - a[2])};
    return clip(oklabToSrgb(lab));
  }
  // OKLCh: the hue along the shorter arc, evenly in t between stops. With maxChroma (the default),
  // each hue gets the most vivid color sRGB can show (the gamut's cusp: for hues between the
  // primaries and secondaries, the cube's edges); otherwise lightness and chroma interpolate
  // linearly and chroma is cut back only as far as the gamut needs.
  constexpr float kPi = 3.14159265358979f;
  const float ca = std::hypot(a[1], a[2]), cb = std::hypot(b[1], b[2]);
  const float ha = std::atan2(a[2], a[1]);
  float dh = std::atan2(b[2], b[1]) - ha;
  if (dh > kPi) dh -= 2.0f * kPi;
  if (dh < -kPi) dh += 2.0f * kPi;
  const float h = ha + u * dh, ch = std::cos(h), sh = std::sin(h);
  auto at = [&](float L, float c) { return oklabToSrgb({L, c * ch, c * sh}); };
  auto maxChroma = [&](float L) {  // the most chroma in gamut at lightness L along this hue
    float lo = 0.0f, hi = 0.5f;
    for (int it = 0; it < 24; ++it) {
      const float mid = 0.5f * (lo + hi);
      (inGamut(at(L, mid)) ? lo : hi) = mid;
    }
    return lo;
  };
  if (spec.maxChroma) {
    // Chroma at the gamut's boundary is unimodal in lightness: golden-section search for its peak.
    float lo = 0.05f, hi = 0.999f;
    constexpr float g = 0.618034f;
    float x1 = hi - g * (hi - lo), x2 = lo + g * (hi - lo), f1 = maxChroma(x1), f2 = maxChroma(x2);
    for (int it = 0; it < 30; ++it) {
      if (f1 < f2) {
        lo = x1;
        x1 = x2;
        f1 = f2;
        x2 = lo + g * (hi - lo);
        f2 = maxChroma(x2);
      } else {
        hi = x2;
        x2 = x1;
        f2 = f1;
        x1 = hi - g * (hi - lo);
        f1 = maxChroma(x1);
      }
    }
    const float L = 0.5f * (lo + hi);
    return clip(at(L, maxChroma(L)));
  }
  const float L = a[0] + u * (b[0] - a[0]);
  const float c = ca + u * (cb - ca);
  if (inGamut(at(L, c))) return clip(at(L, c));
  return clip(at(L, maxChroma(L)));
}

std::vector<std::array<uint8_t, 3>> buildPaletteLut(const PaletteSpec& spec, int n) {
  std::vector<std::array<uint8_t, 3>> lut(size_t(std::max(n, 2)));
  for (size_t i = 0; i < lut.size(); ++i) {
    const Rgb c = paletteColor(spec, float(i) / float(lut.size() - 1));
    for (int k = 0; k < 3; ++k) lut[i][size_t(k)] = uint8_t(std::lround(255.0f * c[size_t(k)]));
  }
  return lut;
}

bool parsePalette(const std::string& json, PaletteSpec* out, std::string* error) {
  auto fail = [&](const std::string& why) {
    if (error) *error = why;
    return false;
  };
  PaletteSpec spec;
  Cursor c{json};
  if (!c.eat('{')) return fail("not a JSON object");
  if (!c.eat('}')) {
    do {
      std::string key, text;
      if (!c.string(&key) || !c.eat(':')) return fail("bad key");
      if (key == "name") {
        if (!c.string(&spec.name)) return fail("name: not a string");
      } else if (key == "space") {
        if (!c.string(&text)) return fail("space: not a string");
        if (text == "oklab") spec.space = PaletteSpec::Space::OkLab;
        else if (text == "oklch") spec.space = PaletteSpec::Space::OkLch;
        else return fail("space: \"" + text + "\" (oklab or oklch)");
      } else if (key == "chroma") {
        if (!c.string(&text) || (text != "max" && text != "interpolate")) return fail("chroma: \"max\" or \"interpolate\"");
        spec.maxChroma = text == "max";
      } else if (key == "saturation") {
        if (!c.string(&text) || !parseHex(text, &spec.saturation)) return fail("saturation: not #RRGGBB");
      } else if (key == "stops") {
        if (!c.eat('[')) return fail("stops: not an array");
        do {
          double pos;
          Rgb rgb;
          if (!c.eat('[') || !c.number(&pos) || !c.eat(',') || !c.string(&text) || !parseHex(text, &rgb) || !c.eat(']'))
            return fail("stops: each is [position, \"#RRGGBB\"]");
          spec.stops.emplace_back(float(pos), rgb);
        } while (c.eat(','));
        if (!c.eat(']')) return fail("stops: unterminated");
      } else if (!c.skip()) {
        return fail("bad value for " + key);
      }
    } while (c.eat(','));
    if (!c.eat('}')) return fail("unterminated object");
  }
  if (spec.stops.size() < 2) return fail("fewer than two stops");
  if (spec.stops.front().first != 0.0f || spec.stops.back().first != 1.0f) return fail("stops must run from 0 to 1");
  for (size_t k = 1; k < spec.stops.size(); ++k)
    if (!(spec.stops[k].first > spec.stops[k - 1].first)) return fail("stop positions must ascend");
  *out = std::move(spec);
  return true;
}

}  // namespace tv
