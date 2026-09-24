// Stage 4b on the GPU (docs/PIPELINE_LOG.md stage 4b): the same non-local means as native/core's CPU
// reference (filters.h nonLocalMeans), as a GLES 3.1 compute shader, one dispatch a frame and a
// synchronous read-back. The CPU at the processing thread's low clock needs ~12 ms for a 5x5 search;
// this does the 11x11 one on the Adreno 650. It keeps its own EGL context, created on the first
// run() and current on that thread only (the processing thread); release() there too. If anything
// fails it reports false, stays off, and the pipeline's CPU version takes over.
#pragma once

#include <EGL/egl.h>
#include <GLES3/gl31.h>

#include <string>
#include <vector>

namespace tv {

// The shader's source for a search radius, a patch radius and pixels per thread (a row of adjacent
// pixels: they share the patch columns' difference sums). Generated so that every window and loop
// index is a constant and the windows live in registers. Portable, so the Mac can check it compiles.
std::string gpuNlmShaderSource(int searchRadius, int patchRadius, int pixelsPerThread);

class GpuNlm {
 public:
  GpuNlm() = default;
  GpuNlm(const GpuNlm&) = delete;
  GpuNlm& operator=(const GpuNlm&) = delete;
  ~GpuNlm() { release(); }

  // src -> dst, kImagePixels counts each (the pipeline's NoiseReducer; radii clamped as the CPU's).
  // pixelsPerThread 0: the default. False: not available.
  bool run(const float* src, float* dst, int searchRadius, int patchRadius, float h, int pixelsPerThread = 0);
  void release();

  bool failed() const { return failed_; }                 // it won't run again (status() says why)
  const std::string& status() const { return status_; }  // "GPU (renderer)" once working, else why not
  double lastMs() const { return lastMs_; }              // the last run: upload to read-back

  static constexpr int kDefaultPixelsPerThread = 2;

 private:
  struct Program {
    int searchRadius, patchRadius, pixelsPerThread;
    GLuint id;
    GLint uK;
  };
  bool init();
  const Program* program(int searchRadius, int patchRadius, int pixelsPerThread);
  bool fail(const std::string& why);
  bool failed_ = false;
  std::string status_ = "not started";
  double lastMs_ = 0;
  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLContext context_ = EGL_NO_CONTEXT;
  EGLSurface surface_ = EGL_NO_SURFACE;
  std::vector<Program> programs_;
  GLuint in_ = 0, out_ = 0;
};

}  // namespace tv
