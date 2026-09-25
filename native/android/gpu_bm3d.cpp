#include "gpu_bm3d.h"

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

GpuBm3dShape shapeOf(const Bm3dOptions& o) {
  GpuBm3dShape s;
  s.search = o.search;
  s.group = o.group1;
  s.stride = o.stride;
  s.kaiser = o.kaiser;
  return s;
}

}  // namespace

bool GpuBm3d::supports(const Bm3dOptions& o) {
  const bool groupOk = o.group1 == o.group2 && (o.group1 == 2 || o.group1 == 4 || o.group1 == 8);
  return o.block == 8 && groupOk && o.search >= 1 && o.search <= 7 && o.stride >= 1 && o.stride <= 8 &&
         !(o.tau1 > 0.0f) && !(o.tau2 > 0.0f) && o.aggregateAll && !o.skipSameColumn && !o.skipSameRow;
}

bool GpuBm3d::fail(const std::string& why) {
  status_ = why;
  failed_ = true;
  LOGE("gpu bm3d: %s", why.c_str());
  release();
  return false;
}

bool GpuBm3d::init() {
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
  auto buffer = [bytes](GLuint* id, GLenum usage) {
    glGenBuffers(1, id);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, *id);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, usage);
  };
  buffer(&src_, GL_DYNAMIC_DRAW);
  buffer(&basic_, GL_DYNAMIC_COPY);
  buffer(&out_, GL_DYNAMIC_READ);
  glGenBuffers(1, &tiles_);
  if (glGetError() != GL_NO_ERROR) return fail("buffer setup failed");
  status_ = "GPU (" + std::string(reinterpret_cast<const char*>(glGetString(GL_RENDERER))) + ")";
  LOGI("gpu bm3d: ready, %s, GLES %d.%d", status_.c_str(), major, minor);
  return true;
}

bool GpuBm3d::current() {
  if (context_ == EGL_NO_CONTEXT) return init();
  if (eglGetCurrentContext() == context_) return true;
  return eglMakeCurrent(display_, surface_, surface_, context_) ? true : fail("cannot make the context current");
}

GLuint GpuBm3d::compile(const std::string& source) {
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
    return 0;
  }
  const GLuint id = glCreateProgram();
  glAttachShader(id, shader);
  glLinkProgram(id);
  glDeleteShader(shader);
  glGetProgramiv(id, GL_LINK_STATUS, &ok);
  if (!ok) {
    glDeleteProgram(id);
    fail("compute program link failed");
    return 0;
  }
  return id;
}

const GpuBm3d::Programs* GpuBm3d::programs(const GpuBm3dShape& shape) {
  for (const Programs& p : programs_)
    if (p.shape == shape) return &p;
  const double t0 = nowMs();
  Programs p;
  p.shape = shape;
  if (!(p.step1 = compile(gpuBm3dShaderSource(GpuBm3dPass::Step1, shape)))) return nullptr;
  if (!(p.step2 = compile(gpuBm3dShaderSource(GpuBm3dPass::Step2, shape)))) return nullptr;
  if (!(p.gather = compile(gpuBm3dShaderSource(GpuBm3dPass::Gather, shape)))) return nullptr;
  programs_.push_back(p);
  LOGI("gpu bm3d: compiled search %d, groups of %d, stride %d in %.0f ms", shape.search, shape.group, shape.stride,
       nowMs() - t0);
  return &programs_.back();
}

bool GpuBm3d::start(const float* src, float sigma, const Bm3dOptions& o) {
  started_ = false;
  if (!dispatch(src, sigma, o, nullptr, 0)) return false;
  started_ = true;
  return true;
}

bool GpuBm3d::profile(const float* src, float* dst, float sigma, const Bm3dOptions& o, double ms[4], int ablate) {
  started_ = false;
  if (!dispatch(src, sigma, o, ms, ablate)) return false;
  started_ = true;
  return finish(dst);
}

bool GpuBm3d::dispatch(const float* src, float sigma, const Bm3dOptions& o, double* passMs, int ablate) {
  if (failed_ || !supports(o) || !(sigma > 0.0f)) return false;
  if (!current()) return false;
  GpuBm3dShape shape = shapeOf(o);
  shape.ablate = ablate;
  const Programs* p = programs(shape);
  if (!p) return false;
  const double t0 = nowMs();
  // Around the image's mean, as the CPU reference (bm3d.cpp): the same level, the same thresholds.
  double sum = 0.0;
  for (size_t i = 0; i < kImagePixels; ++i) sum += src[i];
  const float level = float(sum / double(kImagePixels));
  const float sigma2 = sigma * sigma;
  const GLsizeiptr bytes = GLsizeiptr(kImagePixels * sizeof(float));
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, src_);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bytes, src);
  const int rx = gpuBm3dRefsX(shape), ry = gpuBm3dRefsY(shape), side = gpuBm3dTileSide(shape);
  const GLsizeiptr tiles = GLsizeiptr(size_t(rx) * size_t(ry) * 2 * size_t(side) * size_t(side) * sizeof(float));
  if (tiles != tilesBytes_) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, tiles_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, tiles, nullptr, GL_DYNAMIC_COPY);
    tilesBytes_ = tiles;
  }
  const double t1 = nowMs();
  const GLuint groups = GLuint(kImagePixels / 64);
  const std::vector<float> window = gpuBm3dWindow(shape);
  double mark = 0.0;
  const auto lap = [&](int k) {  // (profiling: drain the GPU and take the pass's time)
    if (!passMs) return;
    glFinish();
    const double now = nowMs();
    passMs[k] = now - mark;
    mark = now;
  };
  if (passMs) {
    glFinish();
    mark = nowMs();
    for (int k = 0; k < 4; ++k) passMs[k] = 0.0;
  }
  // Step 1 and the basic estimate.
  glUseProgram(p->step1);
  glUniform1f(0, level);
  glUniform1f(1, (o.lambda * o.lambda) * sigma2);
  glUniform1f(2, sigma2);
  glUniform1fv(3, 64, window.data());
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, src_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, tiles_);
  glDispatchCompute(GLuint(rx), GLuint(ry), 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  lap(0);
  glUseProgram(p->gather);
  glUniform1f(0, o.wiener ? 0.0f : level);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, tiles_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, o.wiener ? basic_ : out_);
  glDispatchCompute(groups, 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  lap(1);
  if (o.wiener) {  // step 2 and the result
    glUseProgram(p->step2);
    glUniform1f(0, level);
    glUniform1f(1, o.mu2 * sigma2);
    glUniform1f(2, sigma2);
    glUniform1fv(3, 64, window.data());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, src_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, tiles_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, basic_);
    glDispatchCompute(GLuint(rx), GLuint(ry), 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    lap(2);
    glUseProgram(p->gather);
    glUniform1f(0, level);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, tiles_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, out_);
    glDispatchCompute(groups, 1, 1);
  }
  glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
  glFlush();  // the GPU starts now, while the caller works
  lap(3);
  const double t2 = nowMs();
  if (glGetError() != GL_NO_ERROR) return fail("dispatch failed");
  phasesMs_[0] = t1 - t0;
  phasesMs_[1] = t2 - t1;
  return true;
}

bool GpuBm3d::finish(float* dst) {
  if (!started_ || failed_) return false;
  started_ = false;
  if (!current()) return false;
  const double t2 = nowMs();
  const GLsizeiptr bytes = GLsizeiptr(kImagePixels * sizeof(float));
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, out_);
  const void* mapped = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, bytes, GL_MAP_READ_BIT);
  const double t3 = nowMs();
  if (!mapped) {
    char hex[16];
    std::snprintf(hex, sizeof hex, "0x%x", glGetError());
    return fail(std::string("read-back failed (") + hex + ")");
  }
  std::memcpy(dst, mapped, size_t(bytes));
  glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
  const double t4 = nowMs();
  phasesMs_[2] = t3 - t2;
  phasesMs_[3] = t4 - t3;
  lastMs_ = phasesMs_[0] + phasesMs_[1] + phasesMs_[2] + phasesMs_[3];
  return true;
}

void GpuBm3d::release() {
  if (display_ == EGL_NO_DISPLAY) return;
  if (context_ != EGL_NO_CONTEXT && (eglGetCurrentContext() == context_ ||
                                     eglMakeCurrent(display_, surface_, surface_, context_))) {
    for (const Programs& p : programs_) {
      glDeleteProgram(p.step1);
      glDeleteProgram(p.step2);
      glDeleteProgram(p.gather);
    }
    const GLuint buffers[] = {src_, tiles_, basic_, out_};
    glDeleteBuffers(4, buffers);
  }
  programs_.clear();
  src_ = tiles_ = basic_ = out_ = 0;
  tilesBytes_ = 0;
  eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
  if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
  surface_ = EGL_NO_SURFACE;
  context_ = EGL_NO_CONTEXT;
  eglTerminate(display_);  // Android counts initializations: this ends ours, not the others'
  display_ = EGL_NO_DISPLAY;
}

}  // namespace tv
