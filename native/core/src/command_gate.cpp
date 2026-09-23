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

CommandResult CameraCommands::send(uint16_t value) {
  const auto now = clock_();
  std::unique_lock lock(mutex_);
  if (!isAllowedCommand(value)) {
    record(value, CommandResult::RefusedNotAllowed, now);
    return CommandResult::RefusedNotAllowed;
  }
  if (value == kCmdShutter) {
    if (shutterSent_ && now - lastShutter_ < kShutterMinInterval) {
      record(value, CommandResult::RefusedRateLimited, now);
      return CommandResult::RefusedRateLimited;
    }
    // Count the attempt even if the transfer fails: the camera may still have acted on it.
    shutterSent_ = true;
    lastShutter_ = now;
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
