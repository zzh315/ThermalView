#include <algorithm>
#include <vector>

#include "doctest.h"
#include "tv/command_gate.h"

using namespace std::chrono_literals;
using tv::CameraCommands;
using tv::CommandPurpose;
using tv::CommandResult;

namespace {

struct FakeCamera {
  std::vector<uint16_t> received;
  bool fail = false;
  CameraCommands::Sender sender() {
    return [this](uint16_t v) {
      received.push_back(v);
      return !fail;
    };
  }
};

struct FakeClock {
  std::chrono::steady_clock::time_point now{};
  CameraCommands::Clock clock() {
    return [this] { return now; };
  }
};

}  // namespace

TEST_CASE("the allowlist is exactly {0x8000, 0x8004, 0x8020}") {
  std::vector<uint16_t> allowed(tv::kAllowedCommands.begin(), tv::kAllowedCommands.end());
  std::sort(allowed.begin(), allowed.end());
  CHECK(allowed == std::vector<uint16_t>{0x8000, 0x8004, 0x8020});
}

TEST_CASE("every other 16-bit value is refused and never reaches the camera") {
  FakeCamera camera;
  FakeClock clock;
  CameraCommands gate(camera.sender(), clock.clock());
  size_t refused = 0;
  for (uint32_t v = 0; v <= 0xFFFF; ++v) {
    if (tv::isAllowedCommand(uint16_t(v))) continue;
    refused += gate.send(uint16_t(v)) == CommandResult::RefusedNotAllowed;
  }
  CHECK(refused == 0x10000 - 3);
  CHECK(camera.received.empty());
}

TEST_CASE("forbidden values named in CLAUDE.md are refused") {
  FakeCamera camera;
  CameraCommands gate(camera.sender());
  for (uint16_t v : {0x80FF, 0xEC00, 0xEE12, 0x8001, 0x8002, 0x8003, 0x8005, 0x8021, 0x8081,
                     0x7FFF, 0x0000, 0x1400, 0xF000, 0xFB00}) {
    CHECK(gate.send(v) == CommandResult::RefusedNotAllowed);
  }
  CHECK(camera.received.empty());
}

TEST_CASE("allowed values reach the camera") {
  FakeCamera camera;
  CameraCommands gate(camera.sender());
  CHECK(gate.send(0x8004) == CommandResult::Sent);
  CHECK(gate.send(0x8020) == CommandResult::Sent);
  CHECK(gate.send(0x8000) == CommandResult::Sent);
  CHECK(camera.received == std::vector<uint16_t>{0x8004, 0x8020, 0x8000});
}

TEST_CASE("0x8000 is limited to once per 10 s; the others are not rate-limited") {
  FakeCamera camera;
  FakeClock clock;
  CameraCommands gate(camera.sender(), clock.clock());
  CHECK(gate.shutterReady());
  CHECK(gate.send(0x8000) == CommandResult::Sent);
  CHECK_FALSE(gate.shutterReady());
  clock.now += 9999ms;
  CHECK(gate.send(0x8000) == CommandResult::RefusedRateLimited);
  CHECK(gate.send(0x8004) == CommandResult::Sent);
  CHECK(gate.send(0x8020) == CommandResult::Sent);
  clock.now += 1ms;
  CHECK(gate.shutterReady());
  CHECK(gate.send(0x8000) == CommandResult::Sent);
  CHECK(std::count(camera.received.begin(), camera.received.end(), 0x8000) == 2);
}

TEST_CASE("a failed 0x8000 still counts toward the rate limit") {
  FakeCamera camera;
  FakeClock clock;
  CameraCommands gate(camera.sender(), clock.clock());
  camera.fail = true;
  CHECK(gate.send(0x8000) == CommandResult::SendFailed);
  camera.fail = false;
  clock.now += 5s;
  CHECK(gate.send(0x8000) == CommandResult::RefusedRateLimited);
}

TEST_CASE("every attempt is logged, bounded, and passed to the sink") {
  FakeCamera camera;
  CameraCommands gate(camera.sender());
  std::vector<uint16_t> sunk;
  gate.setSink([&](const tv::CommandLogEntry& e) { sunk.push_back(e.value); });
  gate.send(0x8004);
  gate.send(0x80FF);
  const auto log = gate.history();
  REQUIRE(log.size() == 2);
  CHECK(log[0].result == CommandResult::Sent);
  CHECK(log[1].result == CommandResult::RefusedNotAllowed);
  CHECK(sunk == std::vector<uint16_t>{0x8004, 0x80FF});
  for (int i = 0; i < 100; ++i) gate.send(0x1234);
  CHECK(gate.history().size() == CameraCommands::kLogCapacity);
}

TEST_CASE("a lockout holds 0x8000 at >= 250 ms spacing for at most 5 s") {
  FakeCamera camera;
  FakeClock clock;
  CameraCommands gate(camera.sender(), clock.clock());
  const auto lockout = [&] { return gate.send(0x8000, CommandPurpose::Lockout); };
  CHECK(lockout() == CommandResult::Sent);
  clock.now += 249ms;
  CHECK(lockout() == CommandResult::RefusedRateLimited);
  clock.now += 1ms;
  CHECK(lockout() == CommandResult::Sent);
  for (int i = 0; i < 19; ++i) {  // up to 5.0 s after the first command
    clock.now += 250ms;
    CHECK(lockout() == CommandResult::Sent);
  }
  clock.now += 250ms;  // 5.25 s: the hold is over
  CHECK(lockout() == CommandResult::RefusedRateLimited);
  CHECK(std::count(camera.received.begin(), camera.received.end(), 0x8000) == 21);
}

TEST_CASE("a lockout must go quiet for 1.5 s before the next hold") {
  FakeCamera camera;
  FakeClock clock;
  CameraCommands gate(camera.sender(), clock.clock());
  const auto lockout = [&] { return gate.send(0x8000, CommandPurpose::Lockout); };
  for (int i = 0; i <= 20; ++i) {
    CHECK(lockout() == CommandResult::Sent);
    clock.now += 250ms;
  }
  clock.now -= 250ms;  // at the last command (5 s)
  clock.now += 1499ms;
  CHECK(lockout() == CommandResult::RefusedRateLimited);  // still within the gap: refused
  clock.now += 1ms;  // a refused attempt doesn't restart the gap
  CHECK(lockout() == CommandResult::Sent);  // a new hold
  clock.now += 250ms;
  CHECK(lockout() == CommandResult::Sent);
}

TEST_CASE("a normal 0x8000 still waits 10 s after a lockout command") {
  FakeCamera camera;
  FakeClock clock;
  CameraCommands gate(camera.sender(), clock.clock());
  CHECK(gate.send(0x8000, CommandPurpose::Lockout) == CommandResult::Sent);
  clock.now += 9s;
  CHECK(gate.send(0x8000) == CommandResult::RefusedRateLimited);
  clock.now += 1s;
  CHECK(gate.send(0x8000) == CommandResult::Sent);
}

TEST_CASE("the lockout purpose carries only 0x8000") {
  FakeCamera camera;
  CameraCommands gate(camera.sender());
  CHECK(gate.send(0x8004, CommandPurpose::Lockout) == CommandResult::RefusedNotAllowed);
  CHECK(gate.send(0x8020, CommandPurpose::Lockout) == CommandResult::RefusedNotAllowed);
  CHECK(gate.send(0x80FF, CommandPurpose::Lockout) == CommandResult::RefusedNotAllowed);
  CHECK(camera.received.empty());
}
