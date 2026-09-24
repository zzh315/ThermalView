#include "tv/recalibration.h"

#include <algorithm>
#include <cstdio>

namespace tv {

void RecalibrationPolicy::reset() {
  lastCalS_ = lastCalFpaC_ = lockoutEndS_ = requestS_ = fpaC_ = lastFrameS_ = NAN;
  reason_.clear();
}

void RecalibrationPolicy::onCalibration(double tS, double fpaC) {
  lastCalS_ = tS;
  lastCalFpaC_ = fpaC;
  fpaC_ = fpaC;  // restart the smoothing from the fresh reading
  lastFrameS_ = tS;
  requestS_ = NAN;
}

void RecalibrationPolicy::onLockoutEnded(double tS) { lockoutEndS_ = tS; }

bool RecalibrationPolicy::due(double tS, double fpaC, bool blocked) {
  if (std::isfinite(fpaC)) {
    if (!std::isfinite(fpaC_) || !std::isfinite(lastFrameS_)) {
      fpaC_ = fpaC;
    } else {
      const double dt = std::max(0.0, tS - lastFrameS_);
      fpaC_ += (1.0 - std::exp(-dt / std::max(s_.fpaTauS, 1e-3))) * (fpaC - fpaC_);
    }
    lastFrameS_ = tS;
  }
  if (!s_.enabled || blocked || !std::isfinite(lastCalS_) || !std::isfinite(fpaC_)) return false;
  const double since = tS - lastCalS_;
  if (since < s_.minIntervalS) return false;
  if (std::isfinite(lockoutEndS_) && tS - lockoutEndS_ < s_.afterLockoutS) return false;
  if (std::isfinite(requestS_) && tS - requestS_ < s_.retryS) return false;  // one is on its way
  const double drift = fpaC_ - lastCalFpaC_;
  char text[64];
  if (std::fabs(drift) >= s_.fpaDeltaC) {
    std::snprintf(text, sizeof text, "FPA %+.2f °C since the last calibration", drift);
  } else if (since >= s_.maxIntervalS) {
    std::snprintf(text, sizeof text, "%.0f s since the last calibration", since);
  } else {
    return false;
  }
  reason_ = text;
  requestS_ = tS;
  return true;
}

}  // namespace tv
