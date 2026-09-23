// Frame layout, metadata accessors and sanity checks (docs/PROTOCOL.md).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tv {

inline constexpr int kFrameWidth = 256;
inline constexpr int kImageRows = 192;
inline constexpr int kMetaRows = 4;
inline constexpr int kFrameRows = kImageRows + kMetaRows;  // 196
inline constexpr size_t kImagePixels = size_t(kFrameWidth) * kImageRows;
inline constexpr size_t kFramePixels = size_t(kFrameWidth) * kFrameRows;
inline constexpr size_t kFrameBytes = kFramePixels * sizeof(uint16_t);  // 100352
inline constexpr uint16_t kMaxRaw = 0x3FFF;                            // 14-bit values

// uint16 offsets: P = start of metadata, Q = Block B, U = user area.
inline constexpr size_t kOffsetP = kImagePixels;
inline constexpr size_t kOffsetQ = kOffsetP + kFrameWidth;  // amountPixels = 256 for width 256
inline constexpr size_t kOffsetU = kOffsetQ + 127;

// Width-256 FPA constants (PROTOCOL.md, ht301_hacklib init_parameters).
inline constexpr double kFpaOff = 8617.0;
inline constexpr double kFpaDiv = 37.682;

// Read-only view of one little-endian frame of kFramePixels uint16 values.
class FrameView {
 public:
  explicit FrameView(const uint16_t* data) : d_(data) {}

  const uint16_t* data() const { return d_; }
  const uint16_t* image() const { return d_; }
  uint16_t at(size_t index) const { return d_[index]; }

  // Block A, at P.
  uint16_t fpaAverage() const { return d_[kOffsetP + 0]; }
  uint16_t fpaRaw() const { return d_[kOffsetP + 1]; }
  uint16_t maxX() const { return d_[kOffsetP + 2]; }
  uint16_t maxY() const { return d_[kOffsetP + 3]; }
  uint16_t maxRaw() const { return d_[kOffsetP + 4]; }
  uint16_t minX() const { return d_[kOffsetP + 5]; }
  uint16_t minY() const { return d_[kOffsetP + 6]; }
  uint16_t minRaw() const { return d_[kOffsetP + 7]; }
  uint16_t avgRaw() const { return d_[kOffsetP + 8]; }
  uint16_t centerRaw() const { return d_[kOffsetP + 12]; }

  // Block B, at Q.
  uint16_t cal00() const { return d_[kOffsetQ + 0]; }
  uint16_t shutterK10() const { return d_[kOffsetQ + 1]; }
  uint16_t coreK10() const { return d_[kOffsetQ + 2]; }
  float cal(int n) const { return f32(kOffsetQ + 3 + 2 * (n - 1)); }  // n = 1..5
  std::string firmware() const { return string(kOffsetQ + 24, 16); }

  // User area, at U (read-only: the camera fills it).
  float correction() const { return f32(kOffsetU + 0); }
  float reflectedC() const { return f32(kOffsetU + 2); }
  float airC() const { return f32(kOffsetU + 4); }
  float humidity() const { return f32(kOffsetU + 6); }
  float emissivity() const { return f32(kOffsetU + 8); }
  uint16_t distance() const { return d_[kOffsetU + 10]; }

  double shutterC() const { return shutterK10() / 10.0 - 273.15; }
  double coreC() const { return coreK10() / 10.0 - 273.15; }
  double fpaC() const { return 20.0 - (fpaRaw() - kFpaOff) / kFpaDiv; }

  // Two consecutive uint16s forming a little-endian float32, low word first.
  float f32(size_t index) const;
  // NUL-terminated text of up to maxBytes bytes starting at a uint16 index; non-printable
  // bytes become '?'.
  std::string string(size_t index, size_t maxBytes) const;

 private:
  const uint16_t* d_;
};

struct ImageStats {
  uint16_t min = 0;
  uint16_t max = 0;
  double mean = 0;
  double stddev = 0;
  size_t over14Bit = 0;  // pixels above kMaxRaw
};
ImageStats computeImageStats(const uint16_t* image, size_t pixels = kImagePixels);

// Sanity checks from PROTOCOL.md. A frame that sets any flag is rejected.
enum SanityFlag : uint32_t {
  kSanityOver14Bit = 1u << 0,
  kSanityBlockAZero = 1u << 1,
  kSanityBlockAOrder = 1u << 2,
  kSanityBlockAExtremes = 1u << 3,  // start-up only
  kSanityShutterTemp = 1u << 4,
  kSanityCoreTemp = 1u << 5,
  kSanityEmissivity = 1u << 6,
  kSanityHumidity = 1u << 7,
};
uint32_t checkFrame(const FrameView& frame, const ImageStats& stats, bool startup);
std::string describeSanity(uint32_t flags);

// Printable ASCII runs of at least minLength bytes in the metadata rows, with their byte
// offset from P — used to locate the serial and product strings (PROTOCOL.md VERIFY).
struct TextRun {
  size_t byteOffsetFromP;
  std::string text;
};
std::vector<TextRun> findTextRuns(const FrameView& frame, size_t minLength = 4);

}  // namespace tv
