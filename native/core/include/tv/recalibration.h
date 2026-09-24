// When to recalibrate (0x8000) on our own: docs/PLAN.md M4 stage 1, "Recalibration policy". The
// camera calibrates itself only in its first ~65 s after power-up; after that its readings drift
// about -1.2 °C per °C the focal plane warms between calibrations (DEVICE.md). Pure logic: the
// caller feeds each fresh frame's time and FPA temperature and every calibration it sees (its own
// or the camera's), and asks whether one is due; a due 0x8000 still goes through CameraCommands,
// which enforces the allowlist and the 10 s limit.
//
// OFF (enabled = false) and not wired into the app: the owner approves the numbers first (CLAUDE.md
// rule 1's table). The defaults below are the plan's starting points, except afterLockoutS, which
// M4 still has to measure (the shutter's cooling after a lockout).
#pragma once

#include <cmath>
#include <string>

namespace tv {

struct RecalibrationSettings {
  bool enabled = false;
  double fpaDeltaC = 0.5;        // due once the FPA has moved this far since the last calibration
  double minIntervalS = 60.0;    // never sooner: the shutter warms with use (back-to-back cycles read low)
  double maxIntervalS = 380.0;   // due after this long regardless (the vendor apps' period)
  double afterLockoutS = 120.0;  // after an over-range lockout, let the shutter cool this long (to measure)
  double fpaTauS = 2.0;          // the FPA reading is smoothed over this long before it's compared
  double retryS = 15.0;          // a request that no calibration followed may repeat after this long
};

class RecalibrationPolicy {
 public:
  explicit RecalibrationPolicy(const RecalibrationSettings& settings = {}) : s_(settings) {}
  void setSettings(const RecalibrationSettings& settings) { s_ = settings; }
  const RecalibrationSettings& settings() const { return s_; }

  void reset();                                // a new stream: no calibration seen yet
  void onCalibration(double tS, double fpaC);  // a cycle just ended (ours or the camera's)
  void onLockoutEnded(double tS);              // an over-range lockout released the shutter

  // Each fresh frame: its time (s, monotonic) and FPA (°C). blocked: the camera's power-up series, a
  // range switch, a lockout or our start-up sequence is in progress. True when a 0x8000 is due now.
  bool due(double tS, double fpaC, bool blocked);
  const std::string& reason() const { return reason_; }  // why the last request fired

 private:
  RecalibrationSettings s_;
  double lastCalS_ = NAN, lastCalFpaC_ = NAN, lockoutEndS_ = NAN, requestS_ = NAN;
  double fpaC_ = NAN, lastFrameS_ = NAN;  // the smoothed FPA and when it was last updated
  std::string reason_;
};

}  // namespace tv
