// The one channel for vendor commands (CLAUDE.md rule 1).
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace tv {

inline constexpr uint16_t kCmdShutter = 0x8000;      // shutter close + dark-frame refresh
inline constexpr uint16_t kCmdRawOutput = 0x8004;    // raw 16-bit output
inline constexpr uint16_t kCmdRangeNormal = 0x8020;  // -20 to 120 °C

// Compile-time allowlist: the only values the app may ever send.
inline constexpr std::array<uint16_t, 3> kAllowedCommands = {kCmdShutter, kCmdRawOutput,
                                                             kCmdRangeNormal};
inline constexpr std::chrono::seconds kShutterMinInterval{10};

constexpr bool isAllowedCommand(uint16_t value) {
  for (uint16_t allowed : kAllowedCommands)
    if (value == allowed) return true;
  return false;
}

enum class CommandResult { Sent, RefusedNotAllowed, RefusedRateLimited, SendFailed };
const char* commandResultText(CommandResult result);

struct CommandLogEntry {
  std::chrono::steady_clock::time_point time;
  uint16_t value;
  CommandResult result;
};

class CameraCommands {
 public:
  // The sender is the single piece of code that talks to the camera's Zoom (Absolute) control.
  using Sender = std::function<bool(uint16_t value)>;
  using Clock = std::function<std::chrono::steady_clock::time_point()>;
  using Sink = std::function<void(const CommandLogEntry&)>;

  explicit CameraCommands(Sender sender, Clock clock = std::chrono::steady_clock::now);

  // Refuses anything outside the allowlist, rate-limits 0x8000, and logs every attempt.
  CommandResult send(uint16_t value);

  // Most recent last; bounded to the last kLogCapacity attempts.
  std::vector<CommandLogEntry> history() const;
  void setSink(Sink sink);

  // True if a 0x8000 sent now would pass the rate limit.
  bool shutterReady() const;

  static constexpr size_t kLogCapacity = 64;

 private:
  void record(uint16_t value, CommandResult result, std::chrono::steady_clock::time_point now);

  Sender sender_;
  Clock clock_;
  Sink sink_;
  mutable std::mutex mutex_;
  std::deque<CommandLogEntry> history_;
  bool shutterSent_ = false;
  std::chrono::steady_clock::time_point lastShutter_{};
};

}  // namespace tv
