// Stage 4b's BM3D on the GPU (docs/PIPELINE_LOG.md, "BM3D against NLM at matched noise"): native/core's
// CPU reference (tv/bm3d.h) as GLES 3.1 compute shaders (gpu_bm3d_shader.h), four dispatches a frame
// (step 1, its gather into the basic estimate, step 2, the final gather) and a synchronous read-back.
// Like GpuNlm it keeps its own EGL context on the processing thread, made current whenever it runs
// (the two alternate when the method changes); release() there too. If anything fails it reports
// false, stays off, and the pipeline's CPU fallback takes over.
#pragma once

#include <EGL/egl.h>
#include <GLES3/gl31.h>

#include <string>
#include <vector>

#include "gpu_bm3d_shader.h"
#include "tv/bm3d.h"

namespace tv {

class GpuBm3d {
 public:
  GpuBm3d() = default;
  GpuBm3d(const GpuBm3d&) = delete;
  GpuBm3d& operator=(const GpuBm3d&) = delete;
  ~GpuBm3d() { release(); }

  // Whether these options have a GPU version: 8 x 8 blocks, one group size of 2, 4 or 8 for both
  // steps, no match thresholds, every block aggregated, no skipped columns or rows.
  static bool supports(const Bm3dOptions& o);

  // kImagePixels counts in and out, sigma the noise's std: start uploads src and dispatches (src can
  // change once it returns), finish waits and copies the result. False: not available.
  bool start(const float* src, float sigma, const Bm3dOptions& o);
  bool finish(float* dst);
  bool run(const float* src, float* dst, float sigma, const Bm3dOptions& o) {
    return start(src, sigma, o) && finish(dst);
  }
  void release();
  // Debug: the four passes one at a time, the GPU drained before and after each (ms: step 1, its
  // gather, step 2, the final gather). The result lands in dst as with run(); ablate (GpuBm3dShape's)
  // leaves parts out to time the rest, and makes the result wrong.
  bool profile(const float* src, float* dst, float sigma, const Bm3dOptions& o, double ms[4], int ablate = 0);

  bool failed() const { return failed_; }
  const std::string& status() const { return status_; }
  // The last run, ms: its own calls, and their phases: the level and upload, dispatch, wait, copy.
  double lastMs() const { return lastMs_; }
  const double* lastPhasesMs() const { return phasesMs_; }

 private:
  struct Programs {
    GpuBm3dShape shape;
    GLuint step1 = 0, step2 = 0, gather = 0;
  };
  bool init();
  bool current();
  bool dispatch(const float* src, float sigma, const Bm3dOptions& o, double* passMs, int ablate);
  const Programs* programs(const GpuBm3dShape& shape);
  GLuint compile(const std::string& source);
  bool fail(const std::string& why);
  bool failed_ = false;
  std::string status_ = "not started";
  double lastMs_ = 0;
  double phasesMs_[4] = {};
  bool started_ = false;
  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLContext context_ = EGL_NO_CONTEXT;
  EGLSurface surface_ = EGL_NO_SURFACE;
  std::vector<Programs> programs_;
  GLuint src_ = 0, tiles_ = 0, basic_ = 0, out_ = 0;
  GLsizeiptr tilesBytes_ = 0;
};

}  // namespace tv
