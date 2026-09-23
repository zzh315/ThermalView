// GLES render thread. M1 draws the baseline preview (docs/PLAN.md): a per-frame min/max linear
// stretch of the 14-bit image to grayscale, nearest-neighbor, letterboxed to 4:3.
#pragma once

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "tv/frame.h"
#include "tv/stats.h"
#include "tv/triple_buffer.h"

namespace tv {

struct DisplayFrame {
  std::array<uint16_t, kImagePixels> image;
  uint16_t min = 0;
  uint16_t max = 0;
  int64_t arrivalNs = 0;  // when the frame reached the capture callback
};

class Renderer {
 public:
  Renderer();
  ~Renderer();

  void start();
  void stop();

  // UI thread. Takes ownership of the window reference (nullptr releases the current one) and
  // returns only once the render thread has switched, because Android invalidates the surface
  // as soon as surfaceDestroyed() returns.
  void setWindow(ANativeWindow* window);

  // Processing thread: fill frameSlot(), then publishFrame().
  DisplayFrame& frameSlot() { return frames_.writeSlot(); }
  void publishFrame();
  void clear();  // back to black, e.g. on disconnect

  // Mirror the image horizontally/vertically (the M1 orientation check decides).
  void setMirror(bool x, bool y);

  double latencyP50Ms() const;
  double latencyP95Ms() const;
  uint64_t framesDrawn() const { return drawn_.load(); }

 private:
  void loop();
  bool initDisplay();
  void switchSurface(ANativeWindow* window);
  bool initGl();
  void draw(const DisplayFrame& frame, bool haveFrame);

  std::thread thread_;
  std::atomic<bool> running_{false};
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable switched_;
  ANativeWindow* pendingWindow_ = nullptr;
  bool windowPending_ = false;
  bool windowDone_ = false;
  bool clearPending_ = false;

  TripleBuffer<DisplayFrame> frames_;
  bool haveFrame_ = false;

  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLConfig config_ = nullptr;
  EGLContext context_ = EGL_NO_CONTEXT;
  EGLSurface surface_ = EGL_NO_SURFACE;
  ANativeWindow* window_ = nullptr;
  GLuint program_ = 0, texture_ = 0, vao_ = 0;
  GLint uMin_ = -1, uScale_ = -1, uMirror_ = -1;
  std::atomic<float> mirrorX_{1.0f}, mirrorY_{1.0f};

  RollingWindow latencyMs_{250};
  std::atomic<uint64_t> drawn_{0};
};

}  // namespace tv
