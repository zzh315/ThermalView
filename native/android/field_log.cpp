#include "field_log.h"

#include <sys/stat.h>

#include <cstdarg>
#include <ctime>

#include "log.h"

namespace tv {

FieldLog& FieldLog::get() {
  static FieldLog instance;
  return instance;
}

void FieldLog::open(const std::string& dir) {
  std::lock_guard lock(mutex_);
  if (file_) return;
  dir_ = dir;
  mkdir(dir_.c_str(), 0770);
  file_ = std::fopen((dir_ + "/field.log").c_str(), "a");
  if (!file_) LOGE("field log: cannot open %s/field.log", dir_.c_str());
}

void FieldLog::log(const char* format, ...) {
  char message[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(message, sizeof message, format, args);
  va_end(args);
  LOGI("%s", message);

  timespec wall{};
  clock_gettime(CLOCK_REALTIME, &wall);
  tm local{};
  localtime_r(&wall.tv_sec, &local);
  char stamp[40];
  std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &local);

  std::lock_guard lock(mutex_);
  if (!file_) return;
  std::fprintf(file_, "%s.%03ld %s\n", stamp, wall.tv_nsec / 1000000, message);
  std::fflush(file_);
  if (std::ftell(file_) > kMaxBytes) rotateLocked();
}

void FieldLog::rotateLocked() {
  std::fclose(file_);
  const std::string current = dir_ + "/field.log";
  std::rename(current.c_str(), (dir_ + "/field.1.log").c_str());
  file_ = std::fopen(current.c_str(), "a");
}

}  // namespace tv
