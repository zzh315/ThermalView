// Temperature table: ht301_hacklib's Camera.info() temperature section, wvc(), atmt() and
// get_temp_table(), ported line for line in double precision (docs/PROTOCOL.md "Temperature
// math"). The golden test against the original Python is the acceptance criterion (PLAN M2).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "tv/frame.h"

namespace tv {

enum class TempRange { Normal, High };

// The high range's maths is unsettled (PROTOCOL.md "Ranges"); M2 picks one by measurement.
enum class HighRangeMath {
  Ht301,    // ht301_hacklib: the normal table (cal_00 correction kept), then 1.17 * T - 40.9
  InfiCam,  // InfiCam: no cal_00 correction, no scaling
};

// Width-256 constants (PROTOCOL.md "Width-256 constants").
inline constexpr double kCal00Offset = 170.0;
inline constexpr double kCal00FpaMul = 0.0;

// Everything the table depends on, decoded verbatim from one frame's metadata (CLAUDE.md rule 2).
struct TemperatureInputs {
  double shutterC = 0;  // Q+1, kelvin x 10
  double fpaC = 0;      // P+1 with the width-256 FPA constants
  double cal00 = 0;     // Q+0
  float cal01 = 0, cal02 = 0, cal03 = 0, cal04 = 0, cal05 = 0;  // Q+3..Q+12, float32
  float correction = 0, reflectedC = 0, airC = 0, humidity = 0, emissivity = 0;  // user area
  uint16_t distance = 0;
};
TemperatureInputs temperatureInputs(const FrameView& frame);

// ht301_hacklib wvc() and atmt().
double waterVaporCoefficient(double humidity, double airC);
double atmosphericTransmittance(double humidity, double airC, double distance);

class TemperatureLut {
 public:
  static constexpr size_t kSize = 16384;  // 14-bit raw input

  void build(const TemperatureInputs& in, TempRange range = TempRange::Normal,
             HighRangeMath highMath = HighRangeMath::Ht301);

  double operator[](uint16_t raw) const { return table_[raw < kSize ? raw : kSize - 1]; }
  const std::array<double, kSize>& table() const { return table_; }

  // The table folds at its vertex (ht301_hacklib takes the square root of an absolute value), so
  // raw values at or below it, and non-finite entries, have no valid temperature: the app shows
  // "--" for them (PROTOCOL.md).
  uint16_t vertex() const { return vertex_; }
  bool valid(uint16_t raw) const;

 private:
  std::array<double, kSize> table_{};
  uint16_t vertex_ = 0;
};

}  // namespace tv
