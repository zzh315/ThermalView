#include "renderer.h"

#include <pthread.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>

#include "log.h"
#include "tv/upscale.h"

namespace tv {
namespace {

int64_t nowNs() {
  timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return int64_t(t.tv_sec) * 1'000'000'000 + t.tv_nsec;
}

// vUV: the camera's UV at each corner of the view. uRot turns the image clockwise by quarter turns on
// the screen (the camera turns with the tablet: M6's orientation), so a screen UV (s, t) samples the
// camera at the turned-back UV.
constexpr const char* kVertexShader = R"(#version 300 es
uniform vec2 uMirror;
uniform int uRot;
out vec2 vUV;
void main() {
  vec2 pos = vec2((gl_VertexID & 1) == 0 ? -1.0 : 1.0, (gl_VertexID & 2) == 0 ? -1.0 : 1.0);
  vec2 s = vec2(pos.x * 0.5 + 0.5, 0.5 - pos.y * 0.5);  // the screen's UV, row 0 at the top
  vUV = uRot == 1 ? vec2(s.y, 1.0 - s.x) : uRot == 2 ? vec2(1.0 - s.x, 1.0 - s.y) : uRot == 3 ? vec2(1.0 - s.y, s.x) : s;
  gl_Position = vec4(pos * uMirror, 0.0, 1.0);
}
)";

// The pipeline's intensity (R32F: GLES can't filter it, so every tap is a texelFetch), upscaled
// either nearest-neighbor or with the cardinal cubic B-spline (native/core upscale.h: 4x4 taps on
// its prefiltered coefficients, mirror borders, then clamped to the 2x2 image pixels around, so it
// never rings past its neighbors), then gray or through the palette's 1024-entry table.
constexpr const char* kFragmentShader = R"(#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
uniform sampler2D uImage;
uniform sampler2D uCoeffs;
uniform sampler2D uLut;
uniform sampler2D uClip;   // R8, 1 where a pixel reads over range; filtered, so its edge stays smooth
uniform vec3 uSaturation;  // the palette's color for those pixels
uniform vec4 uRect;        // the visible part of the frame, camera pixels: x, y, w, h (zoom and pan)
uniform int uMode;         // 0 nearest, 1 cardinal B-spline
uniform int uPalette;      // 0 gray, 1 uLut (and uSaturation)
uniform sampler2D uOutside;  // RG8: 1 above (R) or below (G) a locked range (M6); filtered like uClip
uniform int uLocked;         // 1: mark those with uAbove / uBelow (the palette's; the rainbow's grey)
uniform vec3 uAbove;
uniform vec3 uBelow;
uniform vec4 uBox;         // M6's box, camera pixels x0, y0, x1, y1 (x1 <= x0: none)
uniform float uDim;        // outside it, this much of the brightness
in vec2 vUV;
out vec4 outColor;
int mirror(int k, int n) {  // whole-sample symmetric; one reflection covers the taps' reach
  k = k < 0 ? -k : k;
  return k >= n ? 2 * (n - 1) - k : k;
}
void main() {
  ivec2 size = textureSize(uImage, 0);
  float g;
  vec2 cam = uRect.xy + vUV * uRect.zw;  // camera coordinates, pixel p spanning [p, p + 1)
  if (uMode == 0) {
    g = texelFetch(uImage, clamp(ivec2(floor(cam)), ivec2(0), size - 1), 0).r;
  } else {
    vec2 u = cam - 0.5;  // pixel centers at integers
    vec2 fl = floor(u);
    vec2 f = u - fl;
    ivec2 i0 = ivec2(fl);
    vec2 r = 1.0 - f;
    vec4 wx = vec4(r.x * r.x * r.x, ((3.0 * f.x - 6.0) * f.x * f.x + 4.0),
                   (((-3.0 * f.x + 3.0) * f.x + 3.0) * f.x + 1.0), f.x * f.x * f.x) / 6.0;
    vec4 wy = vec4(r.y * r.y * r.y, ((3.0 * f.y - 6.0) * f.y * f.y + 4.0),
                   (((-3.0 * f.y + 3.0) * f.y + 3.0) * f.y + 1.0), f.y * f.y * f.y) / 6.0;
    int x0 = mirror(i0.x - 1, size.x), x1 = mirror(i0.x, size.x), x2 = mirror(i0.x + 1, size.x),
        x3 = mirror(i0.x + 2, size.x);
    float sum = 0.0;
    for (int j = 0; j < 4; ++j) {
      int y = mirror(i0.y - 1 + j, size.y);
      vec4 c = vec4(texelFetch(uCoeffs, ivec2(x0, y), 0).r, texelFetch(uCoeffs, ivec2(x1, y), 0).r,
                    texelFetch(uCoeffs, ivec2(x2, y), 0).r, texelFetch(uCoeffs, ivec2(x3, y), 0).r);
      sum += wy[j] * dot(wx, c);
    }
    ivec2 a = clamp(i0, ivec2(0), size - 1), b = clamp(i0 + 1, ivec2(0), size - 1);
    float p00 = texelFetch(uImage, a, 0).r, p10 = texelFetch(uImage, ivec2(b.x, a.y), 0).r;
    float p01 = texelFetch(uImage, ivec2(a.x, b.y), 0).r, p11 = texelFetch(uImage, b, 0).r;
    g = clamp(sum, min(min(p00, p10), min(p01, p11)), max(max(p00, p10), max(p01, p11)));
  }
  g = clamp(g, 0.0, 1.0);
  if (uPalette == 1) {
    vec3 c = texture(uLut, vec2((g * 1023.0 + 0.5) / 1024.0, 0.5)).rgb;
    if (uLocked == 1) {
      vec2 o = texture(uOutside, cam / vec2(size)).rg;
      if (o.r > 0.5) c = uAbove;
      else if (o.g > 0.5) c = uBelow;
    }
    outColor = vec4(texture(uClip, cam / vec2(size)).r > 0.5 ? uSaturation : c, 1.0);
  } else {
    outColor = vec4(vec3(g), 1.0);
  }
  if (uBox.z > uBox.x && (cam.x < uBox.x || cam.x >= uBox.z || cam.y < uBox.y || cam.y >= uBox.w))
    outColor.rgb *= uDim;
}
)";

// If the upscaling shader fails to build on some driver: M1's nearest-neighbor gray, so the image
// still shows.
constexpr const char* kFallbackFragmentShader = R"(#version 300 es
precision highp float;
precision highp sampler2D;
uniform sampler2D uImage;
uniform vec4 uRect;
in vec2 vUV;
out vec4 outColor;
void main() {
  ivec2 size = textureSize(uImage, 0);
  ivec2 p = clamp(ivec2(floor(uRect.xy + vUV * uRect.zw)), ivec2(0), size - 1);
  float g = clamp(texelFetch(uImage, p, 0).r, 0.0, 1.0);
  outColor = vec4(vec3(g), 1.0);
}
)";

GLuint compile(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char info[1024];
    glGetShaderInfoLog(shader, sizeof info, nullptr, info);
    LOGE("shader compile failed: %s", info);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

}  // namespace

Renderer::Renderer() = default;

Renderer::~Renderer() { stop(); }

void Renderer::start() {
  if (running_.exchange(true)) return;
  thread_ = std::thread(&Renderer::loop, this);
}

void Renderer::stop() {
  if (!running_.exchange(false)) return;
  wake_.notify_all();
  switched_.notify_all();
  thread_.join();
}

void Renderer::setWindow(ANativeWindow* window) {
  std::unique_lock lock(mutex_);
  if (!running_) {
    if (window) ANativeWindow_release(window);
    return;
  }
  if (windowPending_ && pendingWindow_) ANativeWindow_release(pendingWindow_);
  pendingWindow_ = window;
  windowPending_ = true;
  windowDone_ = false;
  wake_.notify_all();
  switched_.wait(lock, [this] { return windowDone_ || !running_; });
}

void Renderer::publishFrame() {
  {
    std::lock_guard lock(mutex_);  // so the render thread can't miss the wake-up
    frames_.publish();
    publishNs_ = nowNs();
  }
  wake_.notify_one();
}

void Renderer::clear() {
  {
    std::lock_guard lock(mutex_);
    clearPending_ = true;
  }
  wake_.notify_one();
}

void Renderer::setDisplay(int upscaler, std::vector<std::array<uint8_t, 3>> lut, std::array<float, 3> saturation,
                          bool marksLocked, std::array<float, 3> above, std::array<float, 3> below) {
  std::lock_guard lock(displayMutex_);
  upscaler_ = upscaler;
  pendingLut_ = std::move(lut);
  lutPending_ = true;
  saturation_ = saturation;
  marksLocked_ = marksLocked;
  above_ = above;
  below_ = below;
}

void Renderer::setViewRect(float x, float y, float w, float h) {
  std::lock_guard lock(displayMutex_);
  rect_ = {x, y, w, h};
}

void Renderer::setBox(float x0, float y0, float x1, float y1, float dim) {
  std::lock_guard lock(displayMutex_);
  box_ = {x0, y0, x1, y1};
  dim_ = dim;
}

void Renderer::requestReadback(std::string prefix, std::string paletteName) {
  std::lock_guard lock(displayMutex_);
  readbackPrefix_ = std::move(prefix);
  readbackPalette_ = std::move(paletteName);
}

void Renderer::saveReadback(const DisplayFrame& frame, const std::string& prefix, const std::string& palette,
                            int upscaler) {
  const int w = viewW_, h = viewH_;
  std::vector<uint8_t> rgba(size_t(w) * size_t(h) * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(viewX_, viewY_, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  std::ofstream ppm(prefix + ".ppm", std::ios::binary | std::ios::trunc);
  ppm << "P6\n" << w << " " << h << "\n255\n";
  for (int y = h - 1; y >= 0; --y)  // GL rows run bottom-up
    for (int x = 0; x < w; ++x) ppm.write(reinterpret_cast<const char*>(&rgba[(size_t(y) * w + x) * 4]), 3);
  std::ofstream(prefix + ".f32", std::ios::binary | std::ios::trunc)
      .write(reinterpret_cast<const char*>(frame.intensity.data()), std::streamsize(sizeof(float) * kImagePixels));
  std::ofstream(prefix + "_clip.u8", std::ios::binary | std::ios::trunc)
      .write(reinterpret_cast<const char*>(frame.clipped.data()), std::streamsize(kImagePixels));
  bool marksLocked;
  {
    std::lock_guard lock(displayMutex_);
    marksLocked = marksLocked_;
  }
  const bool markLocked = marksLocked && frame.locked;
  if (markLocked) {  // (1 above, 2 below, per pixel: the harness's --outside)
    std::vector<uint8_t> marks(kImagePixels);
    for (size_t i = 0; i < kImagePixels; ++i) marks[i] = frame.outside[2 * i] ? 1 : frame.outside[2 * i + 1] ? 2 : 0;
    std::ofstream(prefix + "_outside.u8", std::ios::binary | std::ios::trunc)
        .write(reinterpret_cast<const char*>(marks.data()), std::streamsize(kImagePixels));
  }
  std::array<float, 4> rect, box;
  float dim;
  {
    std::lock_guard lock(displayMutex_);
    rect = rect_;
    box = box_;
    dim = dim_;
  }
  char json[512];
  std::snprintf(json, sizeof json,
                "{\"width\": %d, \"height\": %d, \"upscaler\": \"%s\", \"palette\": \"%s\", \"mirror_x\": %s, "
                "\"mirror_y\": %s, \"rect\": [%.4f, %.4f, %.4f, %.4f], \"box\": [%.0f, %.0f, %.0f, %.0f], \"dim\": %.3f, "
                "\"locked\": %s, \"rot\": %d}\n",
                w, h, upscaler == 1 ? "bspline" : "nearest", palette.c_str(), mirrorX_.load() < 0 ? "true" : "false",
                mirrorY_.load() < 0 ? "true" : "false", rect[0], rect[1], rect[2], rect[3], box[0], box[1], box[2],
                box[3], dim, markLocked ? "true" : "false", rotation_.load());
  std::ofstream(prefix + ".json", std::ios::trunc) << json;
  LOGI("renderer: readback saved to %s (%dx%d)", prefix.c_str(), w, h);
}

void Renderer::setMirror(bool x, bool y) {
  mirrorX_ = x ? -1.0f : 1.0f;
  mirrorY_ = y ? -1.0f : 1.0f;
}

double Renderer::latencyP50Ms() const {
  std::lock_guard lock(mutex_);
  return latencyMs_.percentile(50);
}

double Renderer::latencyP95Ms() const {
  std::lock_guard lock(mutex_);
  return latencyMs_.percentile(95);
}

void Renderer::renderPartsP50Ms(double* wake, double* draw, double* swap, double* prefilter) const {
  std::lock_guard lock(mutex_);
  *prefilter = prefilterWindowMs_.percentile(50);
  *wake = wakeMs_.percentile(50);
  *draw = drawMs_.percentile(50);
  *swap = swapMs_.percentile(50);
}

void Renderer::loop() {
  pthread_setname_np(pthread_self(), "tv-render");
  if (!initDisplay()) {
    LOGE("renderer: EGL init failed");
  }
  while (running_) {
    ANativeWindow* newWindow = nullptr;
    bool doSwitch = false, doClear = false, haveNew = false;
    {
      std::unique_lock lock(mutex_);
      wake_.wait_for(lock, std::chrono::milliseconds(100), [this] {
        return !running_ || windowPending_ || clearPending_ || frames_.fresh();
      });
      if (!running_) break;
      if (windowPending_) {
        doSwitch = true;
        newWindow = pendingWindow_;
        pendingWindow_ = nullptr;
        windowPending_ = false;
      }
      doClear = clearPending_;
      clearPending_ = false;
      haveNew = frames_.acquire();
      if (haveNew) wakeMs_.push(double(nowNs() - publishNs_) / 1e6);
    }
    if (doSwitch) {
      switchSurface(newWindow);
      {
        std::lock_guard lock(mutex_);
        windowDone_ = true;
      }
      switched_.notify_all();
    }
    if (doClear) haveFrame_ = false;
    if (haveNew) haveFrame_ = true;
    if (surface_ == EGL_NO_SURFACE || !(doSwitch || doClear || haveNew)) continue;

    const int64_t tDraw = nowNs();
    draw(frames_.readSlot(), haveFrame_);
    const int64_t tDrawn = nowNs();
    std::string readback, readbackPalette;
    int upscaler = 0;
    if (haveFrame_ && program_) {
      std::lock_guard lock(displayMutex_);
      readback.swap(readbackPrefix_);
      readbackPalette = readbackPalette_;
      upscaler = upscaler_;
    }
    if (!readback.empty()) saveReadback(frames_.readSlot(), readback, readbackPalette, upscaler);
    const int64_t tSwap = nowNs();
    eglSwapBuffers(display_, surface_);
    if (haveNew) {
      const int64_t tSwapped = nowNs();
      const double ms = double(tSwapped - frames_.readSlot().arrivalNs) / 1e6;
      std::lock_guard lock(mutex_);
      latencyMs_.push(ms);
      drawMs_.push(double(tDrawn - tDraw) / 1e6);
      prefilterWindowMs_.push(prefilterMs_);
      swapMs_.push(double(tSwapped - tSwap) / 1e6);
      ++drawn_;
    }
  }
  switchSurface(nullptr);
  if (display_ != EGL_NO_DISPLAY) {
    if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
    eglTerminate(display_);
  }
  context_ = EGL_NO_CONTEXT;
  display_ = EGL_NO_DISPLAY;
  std::lock_guard lock(mutex_);
  windowDone_ = true;
  switched_.notify_all();
}

bool Renderer::initDisplay() {
  display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr)) return false;
  const EGLint attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
  EGLint count = 0;
  if (!eglChooseConfig(display_, attribs, &config_, 1, &count) || count < 1) return false;
  const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, contextAttribs);
  return context_ != EGL_NO_CONTEXT;
}

void Renderer::switchSurface(ANativeWindow* window) {
  if (display_ == EGL_NO_DISPLAY) {
    if (window) ANativeWindow_release(window);
    return;
  }
  if (surface_ != EGL_NO_SURFACE) {
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display_, surface_);
    surface_ = EGL_NO_SURFACE;
  }
  if (window_) ANativeWindow_release(window_);
  window_ = window;
  if (!window_ || context_ == EGL_NO_CONTEXT) return;
  surface_ = eglCreateWindowSurface(display_, config_, window_, nullptr);
  if (surface_ == EGL_NO_SURFACE || !eglMakeCurrent(display_, surface_, surface_, context_)) {
    LOGE("renderer: cannot make the window surface current (0x%x)", eglGetError());
    surface_ = EGL_NO_SURFACE;
    return;
  }
  eglSwapInterval(display_, 1);
  if (!program_ && !initGl()) LOGE("renderer: GL init failed");
}

bool Renderer::initGl() {
  auto build = [](const char* fragment) -> GLuint {
    const GLuint vs = compile(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, fragment);
    if (!vs || !fs) {
      if (vs) glDeleteShader(vs);
      if (fs) glDeleteShader(fs);
      return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
      glDeleteProgram(program);
      return 0;
    }
    return program;
  };
  program_ = build(kFragmentShader);
  if (!program_) {
    LOGE("renderer: the upscaling shader failed; falling back to nearest-neighbor gray");
    program_ = build(kFallbackFragmentShader);
  }
  if (!program_) {
    LOGE("renderer: program link failed");
    return false;
  }
  uMirror_ = glGetUniformLocation(program_, "uMirror");
  uRot_ = glGetUniformLocation(program_, "uRot");
  uMode_ = glGetUniformLocation(program_, "uMode");
  uPalette_ = glGetUniformLocation(program_, "uPalette");
  glUseProgram(program_);
  glUniform1i(glGetUniformLocation(program_, "uImage"), 0);
  glUniform1i(glGetUniformLocation(program_, "uCoeffs"), 1);
  glUniform1i(glGetUniformLocation(program_, "uLut"), 2);
  glUniform1i(glGetUniformLocation(program_, "uClip"), 3);
  glUniform1i(glGetUniformLocation(program_, "uOutside"), 4);
  uLocked_ = glGetUniformLocation(program_, "uLocked");
  uAbove_ = glGetUniformLocation(program_, "uAbove");
  uBelow_ = glGetUniformLocation(program_, "uBelow");
  uSaturation_ = glGetUniformLocation(program_, "uSaturation");
  uRect_ = glGetUniformLocation(program_, "uRect");
  uBox_ = glGetUniformLocation(program_, "uBox");
  uDim_ = glGetUniformLocation(program_, "uDim");

  for (GLuint* t : {&texture_, &coeffTexture_}) {
    glGenTextures(1, t);
    glBindTexture(GL_TEXTURE_2D, *t);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32F, kFrameWidth, kImageRows);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);  // R32F isn't filterable in GLES
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  // The palette: 1024 x 1 RGBA8 (GLES has no 1D textures), linear between entries.
  glGenTextures(1, &lutTexture_);
  glBindTexture(GL_TEXTURE_2D, lutTexture_);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 1024, 1);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenTextures(1, &clipTexture_);
  glBindTexture(GL_TEXTURE_2D, clipTexture_);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_R8, kFrameWidth, kImageRows);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenTextures(1, &outsideTexture_);
  glBindTexture(GL_TEXTURE_2D, outsideTexture_);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RG8, kFrameWidth, kImageRows);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenVertexArrays(1, &vao_);
  coeffs_.resize(kImagePixels);
  return true;
}

void Renderer::draw(const DisplayFrame& frame, bool haveFrame) {
  EGLint w = 0, h = 0;
  eglQuerySurface(display_, surface_, EGL_WIDTH, &w);
  eglQuerySurface(display_, surface_, EGL_HEIGHT, &h);
  glViewport(0, 0, w, h);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  if (!haveFrame || !program_) return;

  // Letterbox the 4:3 image into the surface (3:4 when it's turned a quarter). A view-size preset gives
  // the image's long side.
  const int rot = rotation_.load();
  const bool upright = rot % 2 == 0;
  const int aw = upright ? 4 : 3, ah = upright ? 3 : 4;
  int vw = w, vh = h;
  if (int64_t(w) * ah > int64_t(h) * aw) vw = h * aw / ah; else vh = w * ah / aw;
  const int want = viewWidthPx_.load();
  if (want > 0 && want < std::max(vw, vh)) {  // a smaller preset, centered
    vw = upright ? want : want * 3 / 4;
    vh = upright ? want * 3 / 4 : want;
  }
  viewX_ = (w - vw) / 2;
  viewY_ = (h - vh) / 2;
  viewW_ = vw;
  viewH_ = vh;
  glViewport(viewX_, viewY_, vw, vh);

  int upscaler;
  bool palette;
  std::array<float, 3> saturation;
  std::array<float, 4> rect, box;
  float dim;
  bool marksLocked;
  std::array<float, 3> above, below;
  {
    std::lock_guard lock(displayMutex_);
    upscaler = upscaler_;
    saturation = saturation_;
    rect = rect_;
    box = box_;
    dim = dim_;
    marksLocked = marksLocked_;
    above = above_;
    below = below_;
    if (lutPending_) {
      havePalette_ = pendingLut_.size() == 1024;
      if (havePalette_) {
        std::vector<uint8_t> rgba(1024 * 4, 255);
        for (size_t i = 0; i < 1024; ++i)
          for (size_t k = 0; k < 3; ++k) rgba[4 * i + k] = pendingLut_[i][k];
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, lutTexture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
      }
      lutPending_ = false;
    }
    palette = havePalette_;
  }

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kFrameWidth, kImageRows, GL_RED, GL_FLOAT, frame.intensity.data());
  if (upscaler == 1) {
    const int64_t t0 = nowNs();
    bsplineCoefficients(frame.intensity.data(), coeffs_.data());
    prefilterMs_ = double(nowNs() - t0) / 1e6;
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, coeffTexture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kFrameWidth, kImageRows, GL_RED, GL_FLOAT, coeffs_.data());
  }
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, lutTexture_);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, clipTexture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kFrameWidth, kImageRows, GL_RED, GL_UNSIGNED_BYTE, frame.clipped.data());
  const bool markLocked = marksLocked && palette && frame.locked;
  if (markLocked) {
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, outsideTexture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kFrameWidth, kImageRows, GL_RG, GL_UNSIGNED_BYTE, frame.outside.data());
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glUseProgram(program_);
  glUniform3f(uSaturation_, saturation[0], saturation[1], saturation[2]);
  glUniform4f(uRect_, rect[0], rect[1], rect[2], rect[3]);
  glUniform4f(uBox_, box[0], box[1], box[2], box[3]);  // (the fallback shader has neither: -1, ignored)
  glUniform1f(uDim_, dim);
  glUniform2f(uMirror_, mirrorX_.load(), mirrorY_.load());
  glUniform1i(uRot_, rot);
  glUniform1i(uMode_, upscaler == 1 ? 1 : 0);
  glUniform1i(uPalette_, palette ? 1 : 0);
  glUniform1i(uLocked_, markLocked ? 1 : 0);
  glUniform3f(uAbove_, above[0], above[1], above[2]);
  glUniform3f(uBelow_, below[0], below[1], below[2]);
  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

}  // namespace tv
