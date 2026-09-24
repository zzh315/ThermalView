#include "gpu_nlm.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "log.h"
#include "tv/frame.h"

namespace tv {
namespace {

double nowMs() {
  timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return double(t.tv_sec) * 1e3 + double(t.tv_nsec) / 1e6;
}

}  // namespace

bool GpuNlm::fail(const std::string& why) {
  status_ = why;
  failed_ = true;
  LOGE("gpu nlm: %s", why.c_str());
  release();
  return false;
}

bool GpuNlm::init() {
  display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr)) return fail("no EGL display");
  const EGLint configAttribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
  EGLConfig config = nullptr;
  EGLint count = 0;
  if (!eglChooseConfig(display_, configAttribs, &config, 1, &count) || count < 1) return fail("no EGL config");
  const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, contextAttribs);
  if (context_ == EGL_NO_CONTEXT) return fail("no GLES 3 context");
  const EGLint pbufferAttribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
  surface_ = eglCreatePbufferSurface(display_, config, pbufferAttribs);
  if (surface_ == EGL_NO_SURFACE || !eglMakeCurrent(display_, surface_, surface_, context_))
    return fail("cannot make the context current");
  GLint major = 0, minor = 0;
  glGetIntegerv(GL_MAJOR_VERSION, &major);
  glGetIntegerv(GL_MINOR_VERSION, &minor);
  if (major < 3 || (major == 3 && minor < 1)) return fail("GLES " + std::to_string(major) + "." + std::to_string(minor) + ": no compute");
  const GLsizeiptr bytes = GLsizeiptr(kImagePixels * sizeof(float));
  glGenBuffers(1, &in_);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, in_);
  glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_DRAW);
  glGenBuffers(1, &out_);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, out_);
  glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_READ);
  if (glGetError() != GL_NO_ERROR) return fail("buffer setup failed");
  status_ = "GPU (" + std::string(reinterpret_cast<const char*>(glGetString(GL_RENDERER))) + ")";
  LOGI("gpu nlm: ready, %s, GLES %d.%d", status_.c_str(), major, minor);
  return true;
}

const GpuNlm::Program* GpuNlm::program(int searchRadius, int patchRadius, int pixelsPerThread) {
  for (const Program& p : programs_)
    if (p.searchRadius == searchRadius && p.patchRadius == patchRadius && p.pixelsPerThread == pixelsPerThread) return &p;
  const double t0 = nowMs();
  const std::string source = gpuNlmShaderSource(searchRadius, patchRadius, pixelsPerThread);
  const char* text = source.c_str();
  const GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(shader, 1, &text, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char info[1024] = {};
    glGetShaderInfoLog(shader, sizeof info, nullptr, info);
    glDeleteShader(shader);
    fail(std::string("compute shader: ") + info);
    return nullptr;
  }
  const GLuint id = glCreateProgram();
  glAttachShader(id, shader);
  glLinkProgram(id);
  glDeleteShader(shader);
  glGetProgramiv(id, GL_LINK_STATUS, &ok);
  if (!ok) {
    glDeleteProgram(id);
    fail("compute program link failed");
    return nullptr;
  }
  programs_.push_back({searchRadius, patchRadius, pixelsPerThread, id, glGetUniformLocation(id, "uK")});
  LOGI("gpu nlm: compiled search %d, patch %d, %d pixel(s) a thread in %.0f ms", searchRadius, patchRadius,
       pixelsPerThread, nowMs() - t0);
  return &programs_.back();
}

bool GpuNlm::run(const float* src, float* dst, int searchRadius, int patchRadius, float h, int pixelsPerThread) {
  if (failed_) return false;
  if (context_ == EGL_NO_CONTEXT && !init()) return false;
  searchRadius = std::clamp(searchRadius, 1, 7);  // as the CPU's
  patchRadius = std::clamp(patchRadius, 0, 3);
  if (pixelsPerThread != 1 && pixelsPerThread != 2 && pixelsPerThread != 4) pixelsPerThread = kDefaultPixelsPerThread;
  const Program* p = program(searchRadius, patchRadius, pixelsPerThread);
  if (!p) return false;
  const double t0 = nowMs();
  const GLsizeiptr bytes = GLsizeiptr(kImagePixels * sizeof(float));
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, in_);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bytes, src);
  glUseProgram(p->id);
  // w = exp(-(mean |difference| / h)^2) = exp2(-S^2 k), S the patch's sum of |differences|.
  const float area = float((2 * patchRadius + 1) * (2 * patchRadius + 1));
  const float hs = std::max(h, 1e-6f) * area;
  glUniform1f(p->uK, 1.44269504f / (hs * hs));
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, in_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, out_);
  glDispatchCompute(GLuint(kFrameWidth / (16 * pixelsPerThread)), GLuint(kImageRows / 16), 1);
  glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, out_);
  const void* mapped = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, bytes, GL_MAP_READ_BIT);
  if (!mapped) {
    const GLenum error = glGetError();
    char hex[16];
    std::snprintf(hex, sizeof hex, "0x%x", error);
    return fail(std::string("read-back failed (") + hex + ")");
  }
  std::memcpy(dst, mapped, size_t(bytes));
  glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
  lastMs_ = nowMs() - t0;
  return true;
}

void GpuNlm::release() {
  if (display_ == EGL_NO_DISPLAY) return;
  if (context_ != EGL_NO_CONTEXT && eglGetCurrentContext() == context_) {
    for (const Program& p : programs_) glDeleteProgram(p.id);
    if (in_) glDeleteBuffers(1, &in_);
    if (out_) glDeleteBuffers(1, &out_);
  }
  programs_.clear();
  in_ = out_ = 0;
  eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
  if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
  surface_ = EGL_NO_SURFACE;
  context_ = EGL_NO_CONTEXT;
  eglTerminate(display_);  // Android counts initializations: this ends ours, not the renderer's
  eglReleaseThread();
  display_ = EGL_NO_DISPLAY;
}

}  // namespace tv
