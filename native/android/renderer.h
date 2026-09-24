// GLES render thread: draws the pipeline's display intensity (native/core Pipeline, docs/PLAN.md M4)
// letterboxed to 4:3, upscaled nearest-neighbor or with the cardinal B-spline (M5's pick so far; the
// owner reviews it), gray or through a palette.
#pragma once

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "tv/frame.h"
#include "tv/stats.h"
#include "tv/triple_buffer.h"

namespace tv {

struct DisplayFrame {
  std::array<float, kImagePixels> intensity;  // [0, 1], from Pipeline::process
  std::array<uint8_t, kImagePixels> clipped;  // 255 where the pixel reads over range (> 120 °C)
  int64_t arrivalNs = 0;                      // when the frame reached the capture callback
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

  // Any thread: the upscaler (0 nearest, 1 cardinal B-spline with the 2x2 clamp), the palette's
  // 1024-entry table (anything else: gray) and its saturation color, which marks pixels too hot to
  // measure (PLAN M4 stage 5; not drawn in gray), from the next frame drawn.
  void setDisplay(int upscaler, std::vector<std::array<uint8_t, 3>> lut, std::array<float, 3> saturation);

  // The visible part of the frame in camera pixels (zoom and pan; the whole frame by default).
  void setViewRect(float x, float y, float w, float h);

  // The view's width in panel pixels (4:3; M6's view-size presets, a debug setting until then); 0 or
  // anything larger than the screen allows: the largest 4:3 fit.
  void setViewWidth(int px) { viewWidthPx_ = px; }

  // Debug (M5's GPU-vs-CPU check): after the next frame drawn, save what the GPU drew in the view
  // (<prefix>.ppm), the intensity and over-range mask it drew from (<prefix>.f32, <prefix>_clip.u8)
  // and how (<prefix>.json: size, upscaler, palette, mirroring).
  void requestReadback(std::string prefix, std::string paletteName);

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
  GLuint program_ = 0, texture_ = 0, coeffTexture_ = 0, lutTexture_ = 0, clipTexture_ = 0, vao_ = 0;
  GLint uMirror_ = -1, uMode_ = -1, uPalette_ = -1, uSaturation_ = -1, uRect_ = -1;
  std::array<float, 4> rect_{0.0f, 0.0f, float(kFrameWidth), float(kImageRows)};  // under displayMutex_
  std::array<float, 3> saturation_{0.5f, 0.5f, 0.5f};
  std::string readbackPrefix_, readbackPalette_;  // under displayMutex_
  int viewX_ = 0, viewY_ = 0, viewW_ = 0, viewH_ = 0;  // the last draw's view rectangle
  std::atomic<int> viewWidthPx_{0};
  void saveReadback(const DisplayFrame& frame, const std::string& prefix, const std::string& palette, int upscaler);
  std::vector<float> coeffs_;  // the B-spline's coefficients of the frame being drawn
  std::mutex displayMutex_;
  int upscaler_ = 1;
  std::vector<std::array<uint8_t, 3>> pendingLut_;
  bool lutPending_ = false, havePalette_ = false;
  std::atomic<float> mirrorX_{1.0f}, mirrorY_{1.0f};

  RollingWindow latencyMs_{250};
  std::atomic<uint64_t> drawn_{0};
};

}  // namespace tv
