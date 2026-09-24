// The camera's per-pixel drift between shutter calibrations (docs/PLAN.md M4 stage 3,
// PIPELINE_LOG): each pixel's offset moves at its own fixed rate as the focal plane warms, the
// same from one warm-up to the next. Built offline with tools/py/drift_map.py from our own dumps;
// software only, never written to the camera (CLAUDE.md rule 2).
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace tv {

struct DriftMap {
  std::vector<float> rate;  // counts per °C of drift, kImagePixels entries (row-major), zero mean
  bool empty() const { return rate.empty(); }
};

// From tools/py/drift_map.py's float32 little-endian file; empty if the size is wrong.
DriftMap driftMapFromBytes(const void* data, size_t bytes);
DriftMap loadDriftMap(const std::string& path);  // empty if missing

// The drift since the last calibration, from a frame's metadata: the shutter temperature (Q+1)
// keeps its value from the calibration, and right after one FPA - shutter is about c0.
inline double driftSinceCalibrationC(double fpaC, double shutterC, double c0 = 0.40) {
  return fpaC - shutterC - c0;
}

}  // namespace tv
