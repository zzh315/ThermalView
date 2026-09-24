#include "doctest.h"
#include "tv/recalibration.h"

namespace {
tv::RecalibrationSettings on() {
  tv::RecalibrationSettings s;
  s.enabled = true;
  return s;
}
// Frames at 25 fps from t0 to t1 with the FPA ramping from f0 to f1; the first time a request is due.
double firstDue(tv::RecalibrationPolicy& p, double t0, double t1, double f0, double f1, bool blocked = false) {
  for (double t = t0; t <= t1; t += 0.04)
    if (p.due(t, f0 + (f1 - f0) * (t - t0) / (t1 - t0), blocked)) return t;
  return -1;
}
}  // namespace

TEST_CASE("the recalibration policy is off by default") {
  tv::RecalibrationPolicy p;
  p.onCalibration(0, 30.0);
  CHECK(firstDue(p, 0, 1000, 30.0, 40.0) < 0);
}

TEST_CASE("a warming focal plane asks for a calibration, never within a minute of the last") {
  tv::RecalibrationPolicy p(on());
  p.onCalibration(0, 30.0);
  // +3 °C in 30 s (fast warm-up): still nothing before 60 s, then at once.
  const double t = firstDue(p, 0, 120, 30.0, 33.0);
  CHECK(t >= 60.0);
  CHECK(t < 60.1);
  CHECK(p.reason().find("FPA") != std::string::npos);
  // Slow warming: 0.5 °C (plus the smoothing's lag) takes about 100 s at 0.3 °C a minute.
  p.onCalibration(200, 40.0);
  const double slow = firstDue(p, 200, 500, 40.0, 41.5);
  CHECK(slow > 290);
  CHECK(slow < 310);
}

TEST_CASE("a steady focal plane still calibrates at the time cap; noise doesn't trigger it") {
  tv::RecalibrationPolicy p(on());
  p.onCalibration(0, 35.0);
  double t = -1;
  for (double s = 0; s <= 500 && t < 0; s += 0.04)  // +-0.1 °C of reading noise
    if (p.due(s, 35.0 + ((int(s * 25) % 3) - 1) * 0.1, false)) t = s;
  CHECK(t == doctest::Approx(380.0).epsilon(0.001));
  CHECK(p.reason().find("380 s") != std::string::npos);
}

TEST_CASE("the policy waits out lockouts, blocks and its own pending request") {
  tv::RecalibrationPolicy p(on());
  p.onCalibration(0, 30.0);
  CHECK(firstDue(p, 0, 100, 30.0, 32.0, true) < 0);  // blocked (power-up series, range switch, ...)
  p.onLockoutEnded(100);
  const double t = firstDue(p, 100.04, 400, 32.0, 32.0);
  CHECK(t >= 220.0);  // 120 s for the shutter to cool
  // Once asked, it doesn't ask again until a calibration shows, or retryS passes.
  CHECK_FALSE(p.due(t + 1, 32.0, false));
  CHECK(p.due(t + 15.1, 32.0, false));
  p.onCalibration(t + 17, 32.0);
  CHECK_FALSE(p.due(t + 30, 32.0, false));
}
