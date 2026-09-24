#include "tv/command_gate.h"

#include <utility>

namespace tv {

const char* commandResultText(CommandResult result) {
  switch (result) {
    case CommandResult::Sent: return "sent";
    case CommandResult::RefusedNotAllowed: return "refused (not allowed)";
    case CommandResult::RefusedRateLimited: return "refused (rate limit)";
    case CommandResult::SendFailed: return "send failed";
  }
  return "?";
}

CameraCommands::CameraCommands(Sender sender, Clock clock)
    : sender_(std::move(sender)), clock_(std::move(clock)) {}

CommandResult CameraCommands::send(uint16_t value, CommandPurpose purpose) {
  const auto now = clock_();
  std::unique_lock lock(mutex_);
  if (!isAllowedCommand(value) || (purpose == CommandPurpose::Lockout && value != kCmdShutter)) {
    record(value, CommandResult::RefusedNotAllowed, now);
    return CommandResult::RefusedNotAllowed;
  }
  if (value == kCmdShutter) {
    if (purpose == CommandPurpose::Lockout) {
      const bool continuing = lockoutSent_ && now - lastLockout_ < kLockoutGap;
      if (continuing && (now - lastLockout_ < kLockoutSpacing || now - holdStart_ > kLockoutMaxHold)) {
        record(value, CommandResult::RefusedRateLimited, now);
        return CommandResult::RefusedRateLimited;
      }
      if (!continuing) holdStart_ = now;
      lockoutSent_ = true;
      lastLockout_ = now;
    } else if (shutterSent_ && now - lastShutter_ < kShutterMinInterval) {
      record(value, CommandResult::RefusedRateLimited, now);
      return CommandResult::RefusedRateLimited;
    }
    // Count the attempt even if the transfer fails: the camera may still have acted on it.
    shutterSent_ = true;
    lastShutter_ = now;
  } else if (value == kCmdRangeHigh) {
    if (rangeHighSent_ && now - lastRangeHigh_ < kRangeHighMinInterval) {
      record(value, CommandResult::RefusedRateLimited, now);
      return CommandResult::RefusedRateLimited;
    }
    rangeHighSent_ = true;
    lastRangeHigh_ = now;
  }
  const bool ok = sender_ && sender_(value);
  const auto result = ok ? CommandResult::Sent : CommandResult::SendFailed;
  record(value, result, now);
  return result;
}

bool CameraCommands::shutterReady() const {
  std::unique_lock lock(mutex_);
  return !shutterSent_ || clock_() - lastShutter_ >= kShutterMinInterval;
}

std::vector<CommandLogEntry> CameraCommands::history() const {
  std::unique_lock lock(mutex_);
  return {history_.begin(), history_.end()};
}

void CameraCommands::setSink(Sink sink) {
  std::unique_lock lock(mutex_);
  sink_ = std::move(sink);
}

void CameraCommands::record(uint16_t value, CommandResult result,
                            std::chrono::steady_clock::time_point now) {
  CommandLogEntry entry{now, value, result};
  history_.push_back(entry);
  if (history_.size() > kLogCapacity) history_.pop_front();
  if (sink_) sink_(entry);
}

}  // namespace tv
