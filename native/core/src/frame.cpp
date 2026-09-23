#include "tv/frame.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tv {

float FrameView::f32(size_t index) const {
  const uint32_t bits = uint32_t(d_[index]) | (uint32_t(d_[index + 1]) << 16);
  float value;
  std::memcpy(&value, &bits, sizeof value);
  return value;
}

std::string FrameView::string(size_t index, size_t maxBytes) const {
  const auto* bytes = reinterpret_cast<const unsigned char*>(d_ + index);
  std::string out;
  for (size_t i = 0; i < maxBytes && bytes[i] != 0; ++i)
    out += (bytes[i] >= 0x20 && bytes[i] < 0x7F) ? char(bytes[i]) : '?';
  return out;
}

ImageStats computeImageStats(const uint16_t* image, size_t pixels) {
  ImageStats s;
  if (pixels == 0) return s;
  uint16_t lo = 0xFFFF, hi = 0;
  double sum = 0, sumSq = 0;
  for (size_t i = 0; i < pixels; ++i) {
    const uint16_t v = image[i];
    lo = v < lo ? v : lo;
    hi = v > hi ? v : hi;
    sum += v;
    sumSq += double(v) * v;
    s.over14Bit += v > kMaxRaw;
  }
  s.min = lo;
  s.max = hi;
  s.mean = sum / double(pixels);
  s.stddev = std::sqrt(std::max(0.0, sumSq / double(pixels) - s.mean * s.mean));
  return s;
}

uint32_t checkFrame(const FrameView& f, const ImageStats& s, bool startup) {
  uint32_t flags = 0;
  if (s.over14Bit) flags |= kSanityOver14Bit;
  if (f.maxRaw() == 0 || f.minRaw() == 0 || f.centerRaw() == 0) flags |= kSanityBlockAZero;
  if (f.maxRaw() < f.minRaw()) flags |= kSanityBlockAOrder;
  if (startup) {
    // Block A should roughly agree with the image it describes.
    const double tolerance = std::max(64.0, 0.1 * double(s.max - s.min));
    if (std::abs(double(f.maxRaw()) - s.max) > tolerance ||
        std::abs(double(f.minRaw()) - s.min) > tolerance)
      flags |= kSanityBlockAExtremes;
  }
  const auto plausible = [](double c) { return c >= -20.0 && c <= 80.0; };
  if (!plausible(f.shutterC())) flags |= kSanityShutterTemp;
  if (!plausible(f.coreC())) flags |= kSanityCoreTemp;
  const float e = f.emissivity(), h = f.humidity();
  if (!(e > 0.0f && e <= 1.0f)) flags |= kSanityEmissivity;
  if (!(h >= 0.0f && h <= 1.0f)) flags |= kSanityHumidity;
  if (f.distance() == 0) flags |= kSanityDistance;
  return flags;
}

std::string describeSanity(uint32_t flags) {
  static constexpr struct { uint32_t flag; const char* name; } kNames[] = {
      {kSanityOver14Bit, "pixel>0x3FFF"},     {kSanityBlockAZero, "blockA zero"},
      {kSanityBlockAOrder, "blockA max<min"}, {kSanityBlockAExtremes, "blockA!=image"},
      {kSanityShutterTemp, "shutter temp"},   {kSanityCoreTemp, "core temp"},
      {kSanityEmissivity, "emissivity"},      {kSanityHumidity, "humidity"},
      {kSanityDistance, "distance"},
  };
  std::string out;
  for (const auto& n : kNames) {
    if (!(flags & n.flag)) continue;
    if (!out.empty()) out += ", ";
    out += n.name;
  }
  return out.empty() ? "ok" : out;
}

std::vector<TextRun> findTextRuns(const FrameView& f, size_t minLength) {
  const auto* meta = reinterpret_cast<const unsigned char*>(f.data() + kOffsetP);
  const size_t bytes = size_t(kMetaRows) * kFrameWidth * sizeof(uint16_t);
  std::vector<TextRun> runs;
  size_t start = 0;
  for (size_t i = 0; i <= bytes; ++i) {
    const bool printable = i < bytes && meta[i] >= 0x20 && meta[i] < 0x7F;
    if (printable) continue;
    if (i - start >= minLength)
      runs.push_back({start, std::string(reinterpret_cast<const char*>(meta + start), i - start)});
    start = i + 1;
  }
  return runs;
}

}  // namespace tv
