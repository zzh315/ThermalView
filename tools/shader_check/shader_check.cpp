// tools/shader_check.sh: compiles and links the renderer's shaders in an off-screen GLES 3 context on
// the device (a pbuffer, so it works with the screen locked) and prints the logs.
//   shader_check VERTEX.glsl FRAGMENT.glsl [FRAGMENT.glsl ...]
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
static std::string slurp(const char* p) { std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str(); }
static GLuint compile(GLenum type, const std::string& src, const char* name) {
  GLuint sh = glCreateShader(type);
  const char* c = src.c_str();
  glShaderSource(sh, 1, &c, nullptr);
  glCompileShader(sh);
  GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  char log[4096] = {0}; glGetShaderInfoLog(sh, sizeof log, nullptr, log);
  std::printf("%s: %s %s\n", name, ok ? "compiled" : "FAILED", log);
  return ok ? sh : 0;
}
int main(int argc, char** argv) {
  EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!eglInitialize(d, nullptr, nullptr)) { std::printf("eglInitialize failed\n"); return 1; }
  const EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
  EGLConfig cfg; EGLint n = 0;
  eglChooseConfig(d, cfgAttr, &cfg, 1, &n);
  const EGLint pb[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
  EGLSurface surf = eglCreatePbufferSurface(d, cfg, pb);
  const EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ctxAttr);
  if (!eglMakeCurrent(d, surf, surf, ctx)) { std::printf("eglMakeCurrent failed\n"); return 1; }
  std::printf("GL: %s / %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
  GLuint v = compile(GL_VERTEX_SHADER, slurp(argv[1]), "vertex");
  for (int i = 2; i < argc; ++i) {
    GLuint f = compile(GL_FRAGMENT_SHADER, slurp(argv[i]), argv[i]);
    if (!v || !f) continue;
    GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    char log[4096] = {0}; glGetProgramInfoLog(p, sizeof log, nullptr, log);
    std::printf("link %s: %s %s\n", argv[i], ok ? "ok" : "FAILED", log);
    if (ok) {
      glUseProgram(p);
      std::printf("  uFields %d uSharpen %d uFloor %d uRot %d\n", glGetUniformLocation(p, "uFields"),
                  glGetUniformLocation(p, "uSharpen"), glGetUniformLocation(p, "uFloor"), glGetUniformLocation(p, "uRot"));
    }
  }
  return 0;
}
