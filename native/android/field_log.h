// Small rotating on-device log of connects, commands, drops, stalls and restarts (docs/PLAN.md
// M1), mirrored to logcat. Lives in app-specific external storage, so `adb pull` can fetch it.
#pragma once

#include <cstdio>
#include <mutex>
#include <string>

namespace tv {

class FieldLog {
 public:
  static FieldLog& get();

  void open(const std::string& dir);
  void log(const char* format, ...) __attribute__((format(printf, 2, 3)));

 private:
  void rotateLocked();

  static constexpr long kMaxBytes = 1 << 20;
  std::mutex mutex_;
  std::string dir_;
  FILE* file_ = nullptr;
};

}  // namespace tv

#define FLOG(...) ::tv::FieldLog::get().log(__VA_ARGS__)
