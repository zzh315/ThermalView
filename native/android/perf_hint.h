// ADPF performance hints (Android 13+, APerformanceHint): the processing thread tells the system how
// long each frame's work may take and how long it took, so the CPU governor raises the big cores'
// clock instead of leaving them at their lowest. By load alone it doesn't: the thread works a few
// ms every 40 ms, so schedutil keeps cpus 4-6 at 710 MHz of 2.42 GHz, and stage 6 then misses the
// latency budget (M4, 2026-09-25: processing p95 20.4 ms). Loaded at run time, since minSdk is 31;
// where the device lacks it, start() says so and report() does nothing.
#pragma once

#include <cstdint>
#include <string>

namespace tv {

class PerfHint {
 public:
  ~PerfHint() { stop(); }
  // For the calling thread. Returns a line for the log: whether the session exists.
  std::string start(int64_t targetNs);
  void report(int64_t actualNs);
  void stop();
  bool active() const { return session_ != nullptr; }
  int64_t targetNs() const { return targetNs_; }

 private:
  void* session_ = nullptr;
  int (*report_)(void*, int64_t) = nullptr;
  void (*close_)(void*) = nullptr;
  int64_t targetNs_ = 0;
};

}  // namespace tv
