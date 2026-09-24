#include "perf_hint.h"

#include <dlfcn.h>
#include <unistd.h>

namespace tv {

std::string PerfHint::start(int64_t targetNs) {
  stop();
  void* lib = dlopen("libandroid.so", RTLD_NOW);
  if (!lib) return "ADPF: libandroid.so not found";
  using GetManager = void* (*)();
  using CreateSession = void* (*)(void*, const int32_t*, size_t, int64_t);
  const auto getManager = reinterpret_cast<GetManager>(dlsym(lib, "APerformanceHint_getManager"));
  const auto create = reinterpret_cast<CreateSession>(dlsym(lib, "APerformanceHint_createSession"));
  report_ = reinterpret_cast<int (*)(void*, int64_t)>(dlsym(lib, "APerformanceHint_reportActualWorkDuration"));
  close_ = reinterpret_cast<void (*)(void*)>(dlsym(lib, "APerformanceHint_closeSession"));
  if (!getManager || !create || !report_ || !close_) return "ADPF: not in this Android (API 33+)";
  void* manager = getManager();
  if (!manager) return "ADPF: no performance hint manager";
  const int32_t tid = int32_t(gettid());
  session_ = create(manager, &tid, 1, targetNs);
  if (!session_) return "ADPF: the device declined a session (no hint support in its power HAL?)";
  targetNs_ = targetNs;
  return "ADPF: session for the processing thread, target " + std::to_string(targetNs / 1000000) + " ms";
}

void PerfHint::report(int64_t actualNs) {
  if (session_ && report_ && actualNs > 0) report_(session_, actualNs);
}

void PerfHint::stop() {
  if (session_ && close_) close_(session_);
  session_ = nullptr;
  targetNs_ = 0;
}

}  // namespace tv
