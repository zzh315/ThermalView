#include "tv/temperature.h"

#include <cmath>
#include <limits>

namespace tv {
namespace {
constexpr double kZeroC = 273.15;
}

TemperatureInputs temperatureInputs(const FrameView& f) {
  TemperatureInputs in;
  in.shutterC = f.shutterK10() / 10.0 - kZeroC;
  in.fpaC = 20.0 - (double(f.fpaRaw()) - kFpaOff) / kFpaDiv;
  in.cal00 = f.cal00();
  in.cal01 = f.cal(1);
  in.cal02 = f.cal(2);
  in.cal03 = f.cal(3);
  in.cal04 = f.cal(4);
  in.cal05 = f.cal(5);
  in.correction = f.correction();
  in.reflectedC = f.reflectedC();
  in.airC = f.airC();
  in.humidity = f.humidity();
  in.emissivity = f.emissivity();
  in.distance = f.distance();
  return in;
}

// Water vapor coefficient from humidity and ambient temperature.
double waterVaporCoefficient(double h, double tAtm) {
  const double h1 = 1.5587, h2 = 0.06939, h3 = -2.7816e-4, h4 = 6.8455e-7;
  return h * std::exp(h1 + h2 * tAtm + h3 * std::pow(tAtm, 2) + h4 * std::pow(tAtm, 3));
}

// Transmittance of the atmosphere from humidity, ambient temperature and distance.
double atmosphericTransmittance(double h, double tAtm, double d) {
  const double kAtm = 1.9;
  const double nsqd = -std::sqrt(d);
  const double sqw = std::sqrt(waterVaporCoefficient(h, tAtm));
  const double a1 = 0.006569, a2 = 0.01262;   // attenuation without water vapor
  const double b1 = -0.002276, b2 = -0.00667;  // attenuation for water vapor
  return kAtm * std::exp(nsqd * (a1 + b1 * sqw)) + (1.0 - kAtm) * std::exp(nsqd * (a2 + b2 * sqw));
}

void TemperatureLut::build(const TemperatureInputs& in, TempRange range, HighRangeMath highMath) {
  // Camera.info(): distance clamped to 20, multiplier 1.0.
  const double distanceAdjusted = (in.distance >= 20 ? 20.0 : double(in.distance)) * 1.0;
  const double emiss = in.emissivity, refl = in.reflectedC, air = in.airC;
  const double atm = atmosphericTransmittance(in.humidity, air, distanceAdjusted);
  const double numeratorSub = (1.0 - emiss) * atm * std::pow(refl + kZeroC, 4) +
                              (1.0 - atm) * std::pow(air + kZeroC, 4);
  const double denominator = emiss * atm;

  const double ts = in.shutterC;  // + offset_temp_shutter (0)
  const double tfpa = in.fpaC;    // + offset_temp_fpa (0)
  const double cal01 = in.cal01, cal02 = in.cal02, cal03 = in.cal03, cal04 = in.cal04,
               cal05 = in.cal05;
  const double calA = cal02 / (cal01 + cal01);
  const double calB = cal02 * cal02 / (cal01 * cal01 * 4.0);
  const double calC = cal01 * std::pow(ts, 2) + ts * cal02;
  const double calD = cal03 * std::pow(tfpa, 2) + cal04 * tfpa + cal05;

  // ht301_hacklib applies the cal_00 correction while its range attribute is 120, which its
  // high-range switch never changes; InfiCam drops it in the 400 range.
  const bool high = range == TempRange::High;
  int cal00Corr = 0;
  if (!high || highMath == HighRangeMath::Ht301)
    cal00Corr = int(kCal00Offset - tfpa * kCal00FpaMul);  // Python int(): truncates toward zero
  const double tableOffset = in.cal00 - (cal00Corr > 0 ? cal00Corr : 0);
  const bool scale = high && highMath == HighRangeMath::Ht301;
  const double m = scale ? 1.17 : 1.0, b = scale ? -40.9 : 0.0;

  // get_temp_table()
  double lowest = std::numeric_limits<double>::infinity();
  vertex_ = 0;
  for (size_t i = 0; i < kSize; ++i) {
    double n = std::sqrt(std::abs(((double(i) - tableOffset) * calD + calC) / cal01 + calB));
    if (std::isnan(n)) n = 0.0;
    // pow(x, 4) and pow(x, 0.25) as multiplies and square roots: the same values (the golden
    // test holds), several times faster on the tablet, which rebuilds the table every frame.
    const double k = n - calA + kZeroC, k2 = k * k;
    const double wtot = k2 * k2;
    const double ttot = std::sqrt(std::sqrt((wtot - numeratorSub) / denominator)) - kZeroC;
    const double t = ttot + (distanceAdjusted * 0.85 - 1.125) * (ttot - air) / 100.0 + in.correction;
    table_[i] = m * t + b;
    if (table_[i] < lowest) {  // NaN never compares less, so it can't become the vertex
      lowest = table_[i];
      vertex_ = uint16_t(i);
    }
  }
}

bool TemperatureLut::valid(uint16_t raw) const {
  return raw > vertex_ && raw < kSize && std::isfinite(table_[raw]);
}

}  // namespace tv
