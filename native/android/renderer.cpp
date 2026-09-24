#include "renderer.h"

#include <ctime>

#include "log.h"

namespace tv {
namespace {

int64_t nowNs() {
  timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return int64_t(t.tv_sec) * 1'000'000'000 + t.tv_nsec;
}

constexpr const char* kVertexShader = R"(#version 300 es
uniform vec2 uMirror;
out vec2 vUV;
void main() {
  vec2 pos = vec2((gl_VertexID & 1) == 0 ? -1.0 : 1.0, (gl_VertexID & 2) == 0 ? -1.0 : 1.0);
  vUV = vec2(pos.x * 0.5 + 0.5, 0.5 - pos.y * 0.5);  // image row 0 at the top
  gl_Position = vec4(pos * uMirror, 0.0, 1.0);
}
)";

// Nearest-neighbor fetch of the pipeline's intensity (R32F: GLES can't filter it, texelFetch doesn't
// need to), drawn as gray.
constexpr const char* kFragmentShader = R"(#version 300 es
precision highp float;
precision highp sampler2D;
uniform sampler2D uImage;
in vec2 vUV;
out vec4 outColor;
void main() {
  ivec2 size = textureSize(uImage, 0);
  ivec2 p = clamp(ivec2(vUV * vec2(size)), ivec2(0), size - 1);
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

void Renderer::loop() {
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

    draw(frames_.readSlot(), haveFrame_);
    eglSwapBuffers(display_, surface_);
    if (haveNew) {
      const double ms = double(nowNs() - frames_.readSlot().arrivalNs) / 1e6;
      std::lock_guard lock(mutex_);
      latencyMs_.push(ms);
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
  const GLuint vs = compile(GL_VERTEX_SHADER, kVertexShader);
  const GLuint fs = compile(GL_FRAGMENT_SHADER, kFragmentShader);
  if (!vs || !fs) return false;
  program_ = glCreateProgram();
  glAttachShader(program_, vs);
  glAttachShader(program_, fs);
  glLinkProgram(program_);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(program_, GL_LINK_STATUS, &ok);
  if (!ok) {
    LOGE("renderer: program link failed");
    return false;
  }
  uMirror_ = glGetUniformLocation(program_, "uMirror");
  glUseProgram(program_);
  glUniform1i(glGetUniformLocation(program_, "uImage"), 0);

  glGenTextures(1, &texture_);
  glBindTexture(GL_TEXTURE_2D, texture_);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32F, kFrameWidth, kImageRows);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);  // R32F isn't filterable in GLES
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenVertexArrays(1, &vao_);
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

  // Letterbox the 4:3 image into the surface.
  int vw = w, vh = h;
  if (int64_t(w) * 3 > int64_t(h) * 4) vw = h * 4 / 3; else vh = w * 3 / 4;
  glViewport((w - vw) / 2, (h - vh) / 2, vw, vh);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kFrameWidth, kImageRows, GL_RED, GL_FLOAT, frame.intensity.data());
  glUseProgram(program_);
  glUniform2f(uMirror_, mirrorX_.load(), mirrorY_.load());
  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

}  // namespace tv
