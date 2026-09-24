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
inline constexpr uint16_t kCmdRangeHigh = 0x8021;    // high range, ~120 to 400+ °C (owner, 2026-09-24)

// Compile-time allowlist: the only values the app may ever send.
inline constexpr std::array<uint16_t, 4> kAllowedCommands = {kCmdShutter, kCmdRawOutput,
                                                             kCmdRangeNormal, kCmdRangeHigh};
inline constexpr std::chrono::seconds kShutterMinInterval{10};
inline constexpr std::chrono::seconds kRangeHighMinInterval{10};  // bounds range switching

// Over-range lockout (owner decision, 2026-09-24): while the scene is hotter than the camera can
// measure, the app holds the shutter closed by repeating 0x8000 before each cycle ends, then lets it
// reopen so the view can be checked again. These limits hold even if the caller misbehaves.
inline constexpr std::chrono::milliseconds kLockoutSpacing{250};  // between commands of one hold
inline constexpr std::chrono::seconds kLockoutMaxHold{5};         // first to last command of a hold
inline constexpr std::chrono::milliseconds kLockoutGap{1500};     // quiet time that ends a hold

constexpr bool isAllowedCommand(uint16_t value) {
  for (uint16_t allowed : kAllowedCommands)
    if (value == allowed) return true;
  return false;
}

enum class CommandResult { Sent, RefusedNotAllowed, RefusedRateLimited, SendFailed };
enum class CommandPurpose { Normal, Lockout };  // Lockout: 0x8000 only, under the lockout limits
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

  // Refuses anything outside the allowlist, rate-limits 0x8000 and 0x8021 (once per 10 s; 0x8020,
  // the safe default, is never limited), and logs every attempt. A normal
  // 0x8000 needs 10 s since the last one of either purpose; a lockout 0x8000 either continues the
  // current hold (>= 250 ms after the previous command, <= 5 s after the hold's first) or, after a
  // 1.5 s quiet gap, starts a new one.
  CommandResult send(uint16_t value, CommandPurpose purpose = CommandPurpose::Normal);

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
  bool rangeHighSent_ = false;
  std::chrono::steady_clock::time_point lastRangeHigh_{};
  bool lockoutSent_ = false;
  std::chrono::steady_clock::time_point holdStart_{}, lastLockout_{};
};

}  // namespace tv
