#include "session.h"

#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "command_sender.h"
#include "field_log.h"
#include "libusb.h"
#include "libuvc/libuvc.h"
#include "log.h"
#include "tv/palette.h"

namespace tv {
namespace {

constexpr int64_t kMs = 1'000'000;
constexpr int kRawModeDelayMs = 300;   // InfiRay's demo sends 0x8004 300 ms after the stream starts
constexpr int kRangeToShutterMs = 500; // and 0x8000 about 0.5 s after a range command
constexpr int kNoFreezeMs = 1000;      // a hold ends if no freeze starts this long after 0x8000
constexpr int kLiveStreak = 10;        // fresh frames after a freeze that end a hold (docs/DEVICE.md:
                                       // after power-up a single fresh frame separates two freezes)
constexpr int kHoldMaxMs = 8000;       // a hold ends after this long regardless (after power-up the
                                       // camera repeats frames until ~10.4 s after plug-in)
constexpr int kAwaitValidMs = 5000;    // start-up gives up after this long without valid frames
constexpr int kValidStreak = 3;        // consecutive valid frames before 0x8020
constexpr int kStallMs = 500;          // watchdog (docs/PLAN.md M1, tunable)
constexpr int kFailStreak = 50;        // 2 s of failing frames after start-up -> stop

// Over-range lockout (owner decision, 2026-09-24; CLAUDE.md rule 1): pixels over range (readouts.h
// overRangeRaw: 120 °C or the per-pixel clip floor) held for 10 s trigger it, so brief looks at a
// hot part or the iron never freeze the view.
constexpr int kLockoutPixels = 4;        // pixels at or above it, in...
constexpr int kLockoutFrames = 250;      // ...this many consecutive frames (10 s), trigger a lockout
constexpr int kLockoutRepeatMs = 260;    // 0x8000 spacing that keeps the shutter closed (gate: >= 250)
constexpr int kLockoutHoldMs = 5000;     // hold, then let the shutter reopen and look again
constexpr int kLockoutClearFrames = 3;   // fresh frames with no hot pixels end the lockout
constexpr int kLockoutPeekMaxMs = 4000;  // resume anyway if no fresh frame arrives after a hold
constexpr int kLockoutDumpFrames = 25;   // debug: frames saved when a lockout triggers

// Range switching (owner decision, 2026-09-24; CLAUDE.md rule 1; PLAN M2). What the camera does on
// a switch, and the high range's clip, are unverified: automatic switching stays behind a debug
// option until the iron session confirms them.
constexpr int kRangeUpFrames = 50;     // 2 s of clipping in the normal range
constexpr int kRangeDownFrames = 125;  // 5 s with nothing above kRangeDownC in the high range
constexpr double kRangeDownC = 110.0;
constexpr int kLockoutFramesHigh = 2;       // in the high range the lockout acts at once

// Debug capture on the owner's Ready tap: recalibrate, settle, record. Runs on the tablet, so a
// dropped adb link can't lose it. Recalibrations stay >= 60 s apart: the shutter warms with use
// and back-to-back cycles read low (docs/DEVICE.md).
constexpr int kCaptureGapMs = 60000;
// A range test is short enough to hold the iron by hand (~20 s): only the gate's 10 s between
// its two recalibrations, a 1 s settle and 2 s per capture. The shutter's self-heating bias
// (~0.2-0.8 C, docs/DEVICE.md) is small next to the difference between the high-range maths.
constexpr int kPairGapMs = 10000;
constexpr int kPairSettleMs = 1000;
constexpr int kPairFrames = 50;
constexpr int kCaptureSettleMs = 3000;
constexpr int kCaptureFrames = 200;

int64_t nowNs() {
  timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return int64_t(t.tv_sec) * 1'000'000'000 + t.tv_nsec;
}

std::string wallClock(const char* format) {
  timespec wall{};
  clock_gettime(CLOCK_REALTIME, &wall);
  tm local{};
  localtime_r(&wall.tv_sec, &local);
  char buf[64];
  std::strftime(buf, sizeof buf, format, &local);
  return buf;
}

uint64_t hashImage(const uint16_t* image) {
  const auto* words = reinterpret_cast<const uint64_t*>(image);  // RawFrame::data is 64-byte aligned
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < kImagePixels / 4; ++i) {
    h ^= words[i];
    h *= 1099511628211ull;
  }
  return h;
}

std::string format(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::string format(const char* fmt, ...) {
  char buf[512];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  return buf;
}

uvc_stream_ctrl_t* asCtrl(const std::unique_ptr<unsigned char[]>& p) {
  return reinterpret_cast<uvc_stream_ctrl_t*>(p.get());
}

std::string constantsText(const FrameView& v) {
  std::string o = format("shutter %.2f C, cal00 %u, cal01..05 %.6g %.6g %.6g %.6g %.6g, Q13..23",
                         v.shutterC(), v.cal00(), v.cal(1), v.cal(2), v.cal(3), v.cal(4), v.cal(5));
  for (int i = 13; i < 24; ++i) o += format(" %u", v.at(kOffsetQ + i));
  return o;
}

// The cores of every cluster faster than the slowest (this tablet: cpus 4-7, the A77s, against the
// A55s at 1.8 GHz), from each core's top frequency; empty if cpufreq can't be read.
cpu_set_t bigCpuSet(std::string* text) {
  cpu_set_t set;
  CPU_ZERO(&set);
  long freq[32] = {};
  long lowest = 0;
  for (int c = 0; c < 32; ++c) {
    FILE* f = std::fopen(("/sys/devices/system/cpu/cpu" + std::to_string(c) + "/cpufreq/cpuinfo_max_freq").c_str(), "r");
    if (!f) continue;
    if (std::fscanf(f, "%ld", &freq[c]) != 1) freq[c] = 0;
    std::fclose(f);
    if (freq[c] > 0 && (lowest == 0 || freq[c] < lowest)) lowest = freq[c];
  }
  for (int c = 0; c < 32; ++c)
    if (freq[c] > lowest) {
      CPU_SET(c, &set);
      *text += (text->empty() ? "" : ",") + std::to_string(c);
    }
  return set;
}

}  // namespace

const char* stateName(State state) {
  switch (state) {
    case State::Idle: return "Idle";
    case State::Starting: return "Starting";
    case State::AwaitValid: return "AwaitValid";
    case State::RangeWait: return "RangeWait";
    case State::ShutterHold: return "ShutterHold";
    case State::RangeSwitch: return "RangeSwitch";
    case State::Lockout: return "Lockout";
    case State::Running: return "Running";
    case State::Failed: return "Failed";
    case State::Replay: return "Replay";
  }
  return "?";
}

struct Session::Snapshot {
  State state = State::Idle;
  bool fallbackOrder = false;
  bool coldStart = false;  // the stream began with repeated frames (camera calibrating)
  std::string startOrderNote;
  double fps = 0, jitterMs = 0, maxIntervalMs = 0, procP95Ms = 0;
  int cpu = -1;          // the processing thread's core for the last frame
  std::string perfHint;  // "ADPF 8 ms" or "no ADPF"
  double bigShare = 0;   // share of processed frames that ran on a big core
  uint64_t frames = 0, seqGaps = 0, arrivalGaps = 0, rejectedSize = 0, rejectedChecks = 0;
  uint64_t startupDiscarded = 0, overruns = 0, restarts = 0, frozen = 0;
  uint64_t shutterCommanded = 0, shutterDetected = 0, shutterUncommanded = 0;
  uint64_t lockouts = 0, lockoutCommands = 0;
  uint32_t lastHotPixels = 0;
  uint16_t lastHotMax = 0;
  Readouts raw, shown;  // this frame's readouts, unsmoothed and as displayed
  uint16_t vertex = 0, clipRaw = 0, lockoutRaw = 0;
  bool rangeHigh = false, autoRange = false, highMathInfiCam = false;
  std::string pipeline;
  bool pipelineFrozen = false, pipelineBlending = false, driftMap = false;
  double driftC = 0;
  double camMaxC = NAN, camMinC = NAN, camCenterC = NAN;  // Block A through our table
  double lastCycleMs = 0;
  uint64_t recalDue = 0;   // the recalibration policy's dry run: how often it would have asked
  double recalAgoS = -1;   // and how long ago it last would have
  std::string recalReason;
  uint32_t bytes = 0, flags = 0, lastBadFlags = 0;
  // Metadata of the latest frame.
  uint16_t fpaAvg = 0, fpaRaw = 0, maxRaw = 0, minRaw = 0, avgRaw = 0, centerRaw = 0, cal00 = 0;
  uint16_t maxX = 0, maxY = 0, minX = 0, minY = 0, distance = 0;
  double fpaC = 0, shutterC = 0, coreC = 0;
  float cal[5] = {};
  float correction = 0, reflectedC = 0, airC = 0, humidity = 0, emissivity = 0;
  std::string firmware;
  std::vector<TextRun> texts;
  ImageStats image;
};

Session& Session::get() {
  static Session instance;
  return instance;
}

void Session::init(const std::string& storageDir, const std::string& appVersion) {
  if (!storageDir_.empty()) return;
  storageDir_ = storageDir;
  appVersion_ = appVersion;
  dumpsDir_ = storageDir + "/dumps";
  statsDir_ = storageDir + "/stats";
  mkdir(dumpsDir_.c_str(), 0770);
  mkdir(statsDir_.c_str(), 0770);
  FieldLog::get().open(storageDir + "/fieldlog");
  FLOG("app start %s", appVersion.c_str());
  ring_ = std::make_unique<FrameRing<8>>();
  snapshot_ = std::make_unique<Snapshot>();
  ctrl_ = std::make_unique<unsigned char[]>(sizeof(uvc_stream_ctrl_t));
  renderer_.start();
}

// --- Camera lifecycle (UI thread) ----------------------------------------------------------------

bool Session::openCamera(int fd, const std::string& manufacturer, const std::string& product,
                         const std::string& serial) {
  stopReplay();
  std::unique_lock lock(streamMutex_);
  if (cameraOpen_) return true;
  manufacturer_ = manufacturer;
  product_ = product;
  serial_ = serial;
  FLOG("open camera fd=%d (%s / %s / %s)", fd, manufacturer.c_str(), product.c_str(), serial.c_str());

  // Android forbids device discovery; wrap the UsbManager file descriptor (docs/PROTOCOL.md).
  libusb_init_option option{};
  option.option = LIBUSB_OPTION_NO_DEVICE_DISCOVERY;
  int r = libusb_init_context(&usb_, &option, 1);
  if (r != LIBUSB_SUCCESS) {
    setBanner(std::string("libusb init failed: ") + libusb_error_name(r));
    return false;
  }
  eventRun_ = true;
  eventThread_ = std::thread([this] {
    while (eventRun_) {
      timeval timeout{0, 100'000};
      libusb_handle_events_timeout_completed(usb_, &timeout, nullptr);
    }
  });

  const auto bail = [&](const std::string& why) {
    FLOG("open failed: %s", why.c_str());
    setBanner("Camera open failed: " + why);
    if (devh_) uvc_close(devh_);
    devh_ = nullptr;
    if (uvc_) uvc_exit(uvc_);
    uvc_ = nullptr;
    eventRun_ = false;
    libusb_interrupt_event_handler(usb_);
    eventThread_.join();
    libusb_exit(usb_);
    usb_ = nullptr;
    return false;
  };

  uvc_error_t u = uvc_init(&uvc_, usb_);
  if (u != UVC_SUCCESS) return bail(std::string("uvc_init: ") + uvc_strerror(u));
  u = uvc_wrap(fd, uvc_, &devh_);
  if (u != UVC_SUCCESS) return bail(std::string("uvc_wrap: ") + uvc_strerror(u));

  // The descriptors M0 verified: one uncompressed format, one 256×196 frame (docs/DEVICE.md).
  bool found = false;
  for (const uvc_format_desc_t* f = uvc_get_format_descs(devh_); f && !found; f = f->next)
    for (const uvc_frame_desc_t* fr = f->frame_descs; fr && !found; fr = fr->next)
      found = f->bDescriptorSubtype == UVC_VS_FORMAT_UNCOMPRESSED && fr->wWidth == kFrameWidth &&
              fr->wHeight == kFrameRows;
  if (!found) return bail("no uncompressed 256x196 frame descriptor");

  gate_ = std::make_unique<CameraCommands>(makeCameraSender(devh_));
  gate_->setSink([](const CommandLogEntry& e) {
    FLOG("command 0x%04x: %s", e.value, commandResultText(e.result));
  });
  {
    std::lock_guard o(optionsMutex_);
    fallbackOrder_ = options_.fallbackOrder;
  }
  fallbackTried_ = false;
  startOrderNote_.clear();
  cameraOpen_ = true;
  setBanner("");
  ring_->clear();
  if (!startStreaming()) {
    cameraOpen_ = false;
    gate_.reset();
    return bail("could not start streaming");
  }
  lock.unlock();
  startProcessing();
  return true;
}

void Session::closeCamera() {
  stopProcessing();  // first: the processing thread may take streamMutex_ to restart a stream
  std::lock_guard lock(streamMutex_);
  if (!cameraOpen_) return;
  stopStreaming();
  if (dumpWriter_.joinable()) dumpWriter_.join();
  dumpWanted_ = 0;
  dumpFrames_.clear();
  closeCsv();
  uvc_close(devh_);  // stop and close before the event thread stops: cancellations need events
  devh_ = nullptr;
  uvc_exit(uvc_);
  uvc_ = nullptr;
  eventRun_ = false;
  libusb_interrupt_event_handler(usb_);
  eventThread_.join();
  libusb_exit(usb_);
  usb_ = nullptr;
  cameraOpen_ = false;
  enter(State::Idle, nowNs());
  renderer_.clear();
  FLOG("camera closed");
}

bool Session::startStreaming() {
  uvc_error_t r = uvc_get_stream_ctrl_format_size(devh_, asCtrl(ctrl_), UVC_FRAME_FORMAT_YUYV,
                                                  kFrameWidth, kFrameRows, 25);
  if (r != UVC_SUCCESS) {
    FLOG("stream negotiation failed: %s", uvc_strerror(r));
    return false;
  }
  validStreak_ = 0;
  lastHash_ = 0;
  inFreeze_ = false;
  lastFreshNs_ = 0;
  liveStreak_ = 0;
  coldStart_ = false;
  readoutFilter_.reset();
  rawReadouts_ = shownReadouts_ = Readouts{};
  lastReadoutNs_ = 0;
  pipeline_.reset();
  pipeline_.setBadPixels(badPixelMapFor(serial_));
  pipeline_.setDriftMap(driftMapFor(serial_));
  pipelineFed_ = false;
  range_ = TempRange::Normal;  // the start sequence always selects the normal range
  recoveryNucRequested_ = recoveryNucSent_ = false;
  blockAZeroStreak_ = 0;
  clipStreak_ = coolStreak_ = 0;
  autoRangeRequest_ = -1;
  hotStreak_ = 0;
  lockoutHoldStartNs_ = 0;
  lockoutPeekHot_ = false;
  lastFreezeEndNs_ = 0;
  capturePhase_ = CapturePhase::None;
  lastShutterNs_ = 0;
  rangeWindow_.clear();
  arrivals_.reset();
  countersResetPending_ = true;
  if (fallbackOrder_) {
    // Approved fallback: InfiCam's order, mode and range before streaming (CLAUDE.md rule 1).
    command(kCmdRawOutput);
    command(kCmdRangeNormal);
  }
  r = uvc_start_streaming(devh_, asCtrl(ctrl_), &Session::frameCallback, this, 0);
  if (r != UVC_SUCCESS) {
    FLOG("start streaming failed: %s", uvc_strerror(r));
    return false;
  }
  streaming_ = true;
  const int64_t now = nowNs();
  streamStartNs_ = now;
  FLOG("streaming started (%s order)", fallbackOrder_ ? "fallback" : "stream-first");
  enter(fallbackOrder_ ? State::AwaitValid : State::Starting, now);
  return true;
}

void Session::stopStreaming() {
  if (!streaming_) return;
  uvc_stop_streaming(devh_);
  streaming_ = false;
  FLOG("streaming stopped");
}

void Session::restartStream(const char* reason) {
  FLOG("restarting stream: %s", reason);
  ++restarts_;
  setBanner(std::string("Restarting stream: ") + reason);
  std::lock_guard lock(streamMutex_);
  if (!cameraOpen_) return;
  stopStreaming();
  ring_->clear();  // safe: the capture callback has stopped and this is the consumer
  if (!startStreaming()) {
    enter(State::Failed, nowNs());
    setBanner("Stream restart failed — replug the camera");
  }
}

// --- Capture callback (libuvc's thread): copy and return ----------------------------------------

void Session::frameCallback(uvc_frame* frame, void* user) {
  auto* self = static_cast<Session*>(user);
  RawFrame* slot = self->ring_->beginWrite();
  if (!slot) return;  // ring full: counted as an overrun
  std::memcpy(slot->data.data(), frame->data, std::min(frame->data_bytes, kFrameBytes));
  slot->bytes = uint32_t(frame->data_bytes);
  slot->arrivalNs = nowNs();
  slot->sequence = frame->sequence;
  slot->replay = false;
  self->ring_->commitWrite();
}

// --- Processing thread ---------------------------------------------------------------------------

void Session::startProcessing() {
  if (procRun_.exchange(true)) return;
  procThread_ = std::thread(&Session::processLoop, this);
}

void Session::stopProcessing() {
  if (!procRun_.exchange(false)) return;
  ring_->wake();
  procThread_.join();
}

void Session::applyAffinity(bool big) {
  if (bigCpusText_.empty()) bigCpus_ = bigCpuSet(&bigCpusText_);
  cpu_set_t set;
  if (big && CPU_COUNT(&bigCpus_) > 0) {
    set = bigCpus_;
  } else {
    CPU_ZERO(&set);
    for (int c = 0; c < 32; ++c) CPU_SET(c, &set);
  }
  // pid 0: the calling thread only (the processing thread).
  if (sched_setaffinity(0, sizeof set, &set) != 0)
    FLOG("processing thread affinity (%s): %s", big ? "big cores" : "any core", std::strerror(errno));
  else
    FLOG("processing thread on %s", big && CPU_COUNT(&bigCpus_) > 0 ? ("cpus " + bigCpusText_).c_str() : "any core");
  affinityBig_ = big;
  affinitySet_ = true;
  cpuFrames_ = cpuFramesBig_ = 0;  // the overlay's share starts over with the new setting
}

void Session::processLoop() {
  while (procRun_) {
    bool big;
    {
      std::lock_guard o(optionsMutex_);
      big = options_.bigCores;
    }
    if (!affinitySet_ || big != affinityBig_) applyAffinity(big);
    bool hint;
    {
      std::lock_guard o(optionsMutex_);
      hint = options_.perfHint;
    }
    if (!perfHintSet_ || hint != perfHintOn_) {
      if (hint) {
        perfHintText_ = perfHint_.start(8 * 1000000LL);  // 8 ms: stages 1-6 take ~6 ms at full clock
      } else {
        perfHint_.stop();
        perfHintText_ = "ADPF: off";
      }
      FLOG("%s", perfHintText_.c_str());
      perfHintSet_ = true;
      perfHintOn_ = hint;
    }
    if (ring_->waitReadable(std::chrono::milliseconds(50))) {
      while (RawFrame* frame = ring_->beginRead()) {
        handleFrame(*frame);
        ring_->commitRead();
        if (!procRun_) break;
      }
    }
    tick(nowNs());
  }
}

CommandResult Session::command(uint16_t value, CommandPurpose purpose) {
  const CommandResult result = gate_ ? gate_->send(value, purpose) : CommandResult::SendFailed;
  if (value == kCmdShutter && result == CommandResult::Sent) {
    if (purpose == CommandPurpose::Lockout)
      ++lockoutCommands_;
    else
      ++shutterCommanded_;
    lastShutterNs_ = nowNs();
  }
  return result;
}

void Session::beginLockout(int64_t now, int hotPixels, uint16_t maxRaw, bool manual) {
  ++lockouts_;
  hotStreak_ = 0;
  lockoutSinceNs_ = now;
  lockoutHoldStartNs_ = now;
  lockoutLastCmdNs_ = 0;
  lockoutPeekHot_ = false;
  lockoutClear_ = 0;
  bool dump;
  {
    std::lock_guard o(optionsMutex_);
    dump = options_.dumpOnLockout;
  }
  if (dump && !manual) startDump(kLockoutDumpFrames);  // before leaving Running
  enter(State::Lockout, now);
  if (command(kCmdShutter, CommandPurpose::Lockout) == CommandResult::Sent) lockoutLastCmdNs_ = nowNs();
  setBanner("Too hot to measure for 10 s: shutter closed to protect the sensor");
  if (manual)
    FLOG("lockout #%" PRIu64 " (manual)", lockouts_);
  else
    FLOG("lockout #%" PRIu64 ": %d pixels >= %u (max %u)", lockouts_, hotPixels, lockoutRaw_, maxRaw);
}

void Session::endLockout(int64_t now) {
  FLOG("lockout ended after %.1f s", double(now - lockoutSinceNs_) / 1e9);
  recal_.onLockoutEnded(double(now) / 1e9);
  enter(State::Running, now);
  setBanner("");
}

void Session::beginHold(int64_t now, int noFreezeMs) {
  holdSawFreeze_ = false;
  liveStreak_ = 0;
  holdNoFreezeMs_ = noFreezeMs;
  enter(State::ShutterHold, now);
}

void Session::switchRange(TempRange target, int64_t now, const char* why) {
  if (target == range_ || now < nextRangeAttemptNs_) return;
  const bool high = target == TempRange::High;
  const CommandResult r = command(high ? kCmdRangeHigh : kCmdRangeNormal);
  if (r != CommandResult::Sent) {
    nextRangeAttemptNs_ = now + 1000 * kMs;
    FLOG("range switch to %s refused: %s", high ? "high" : "normal", commandResultText(r));
    return;
  }
  FLOG("range -> %s (%s); constants before: %s", high ? "high" : "normal", why,
       lastMeta_.empty() ? "?" : constantsText(FrameView(lastMeta_.data())).c_str());
  range_ = target;
  hotStreak_ = clipStreak_ = coolStreak_ = 0;
  rangeLogPending_ = true;
  readoutFilter_.reset();
  rangeSwitchNs_ = now;
  if (capturePhase_ == CapturePhase::None) {
    setBanner("Switching range…");
    rangeBanner_ = true;
  }
  enter(State::RangeSwitch, now);
}

void Session::enter(State state, int64_t now) {
  const State previous = state_.exchange(state);
  stateSinceNs_ = now;
  if (state == State::Running && rangeBanner_) {
    rangeBanner_ = false;
    setBanner("");
  }
  if (previous != state) FLOG("state %s -> %s", stateName(previous), stateName(state));
  if (state == State::Running && countersResetPending_) {
    // Drops count from here: start-up bursts (e.g. the short frames after 0x8020) aren't drops.
    countersResetPending_ = false;
    arrivals_.reset();
    rejectedSize_ = rejectedChecks_ = 0;
    overrunBase_ = ring_->overruns();
    procMs_.clear();
    cpuFrames_ = cpuFramesBig_ = 0;
    recal_.reset();
    recalDue_ = 0;
    recalDueNs_ = 0;
    recalReason_.clear();
    setBanner("");
  }
}

void Session::fail(const std::string& reason) {
  FLOG("FAILED: %s", reason.c_str());
  setBanner(reason + " — streaming stopped. Tell the owner.");
  enter(State::Failed, nowNs());
  std::lock_guard lock(streamMutex_);
  stopStreaming();
}

void Session::tickCapture(int64_t now) {
  const auto since = [now](int64_t t) { return (now - t) / kMs; };
  const State state = state_.load();
  if (capturePhase_ == CapturePhase::None) {
    if (!captureRequested_.exchange(false)) return;
    if (state != State::Running) {
      FLOG("capture: not running, ignored");
      return;
    }
    capturePhase_ = CapturePhase::WaitGap;
    capturePair_ = capturePairRequested_.load();
    captureGapMs_ = capturePair_ ? kPairGapMs : kCaptureGapMs;
    FLOG("capture: requested%s", capturePair_ ? " (range test: normal, then high)" : "");
  }
  const char* leg = !capturePair_ ? "Capture" : range_ == TempRange::High ? "Range test 2/2 (high)"
                                                                          : "Range test 1/2 (normal)";
  if (state != State::Running && state != State::ShutterHold && state != State::RangeSwitch) {
    FLOG("capture: aborted (%s)", stateName(state));
    capturePhase_ = CapturePhase::None;
    // Only clear our own banner: a lockout that aborts the capture has just set its own (M2).
    if (bannerIs(captureBanner_)) setBanner("");
    return;
  }
  switch (capturePhase_) {
    case CapturePhase::WaitGap: {
      if (state != State::Running) break;
      const int64_t gap = since(std::max(lastShutterNs_, lastFreezeEndNs_));
      if (gap < captureGapMs_) {
        captureSetBanner(format("%s: waiting %" PRId64 " s for the shutter to cool…", leg,
                                (captureGapMs_ - gap) / 1000 + 1));
        break;
      }
      if (command(kCmdShutter) != CommandResult::Sent) break;  // retried on the next tick
      FLOG("capture: recalibrating");
      captureSetBanner(format("%s: recalibrating…", leg));
      capturePhase_ = CapturePhase::Recalibrating;
      beginHold(now);
      break;
    }
    case CapturePhase::Recalibrating:
      if (state == State::Running) {
        capturePhase_ = CapturePhase::Settle;
        capturePhaseNs_ = now;
        captureSetBanner(format("%s: recording, keep still…", leg));
      }
      break;
    case CapturePhase::Settle:
      if (since(capturePhaseNs_) >= (capturePair_ ? kPairSettleMs : kCaptureSettleMs)) {
        FLOG("capture: recording %s", startDump(capturePair_ ? kPairFrames : kCaptureFrames).c_str());
        capturePhase_ = CapturePhase::Recording;
      }
      break;
    case CapturePhase::Recording:
      if (dumpWanted_.load() != 0) break;
      if (capturePair_ && range_ == TempRange::Normal) {
        FLOG("capture: normal leg done, switching to the high range");
        captureSetBanner("Range test: switching to the high range…");
        capturePhase_ = CapturePhase::SwitchHigh;
        break;
      }
      if (capturePair_) {
        FLOG("capture: high leg done, switching back");
        captureSetBanner("Range test: switching back…");
        capturePhase_ = CapturePhase::SwitchBack;
        break;
      }
      FLOG("capture: done");
      captureSetBanner("Capture done: measure again now");
      capturePhase_ = CapturePhase::Done;
      capturePhaseNs_ = now;
      break;
    case CapturePhase::SwitchHigh:
      // The gate may refuse 0x8021 for up to 10 s; switchRange retries once a second.
      if (range_ != TempRange::High) {
        if (state == State::Running) switchRange(TempRange::High, now, "range test");
        break;
      }
      if (state != State::Running) break;  // still switching (the switch recalibrates)
      capturePhase_ = CapturePhase::Settle;
      capturePhaseNs_ = now;
      captureSetBanner(format("%s: recording, keep still…", leg));
      break;
    case CapturePhase::SwitchBack:
      if (range_ != TempRange::Normal) {
        if (state == State::Running) switchRange(TempRange::Normal, now, "range test done");
        break;
      }
      if (state != State::Running) break;
      FLOG("capture: range test done");
      captureSetBanner("Range test done");
      capturePhase_ = CapturePhase::Done;
      capturePhaseNs_ = now;
      break;
    case CapturePhase::Done:
      if (since(capturePhaseNs_) >= 5000) {
        if (bannerIs(captureBanner_)) setBanner("");
        capturePhase_ = CapturePhase::None;
      }
      break;
    default:
      break;
  }
}

void Session::tick(int64_t now) {
  tickCapture(now);
  const auto since = [now](int64_t t) { return (now - t) / kMs; };
  // Frames keep arriving (repeated) through a shutter cycle, so a silent stream is a stall.
  const auto stalled = [&] {
    const int64_t last = arrivals_.lastArrivalNs();
    if (!last || since(last) <= kStallMs || since(lastShutterNs_) <= 2000) return false;
    restartStream(format("stall: no frame for %" PRId64 " ms", since(last)).c_str());
    return true;
  };
  switch (state_.load()) {
    case State::Starting:
      if (since(streamStartNs_) >= kRawModeDelayMs) {
        command(kCmdRawOutput);
        validStreak_ = 0;
        enter(State::AwaitValid, now);
      }
      break;
    case State::AwaitValid:
      // Block A reads zero after a range switch until the camera recalibrates (M2), and the
      // start sequence only recalibrates once frames are valid: break that deadlock once.
      if (recoveryNucRequested_ && !recoveryNucSent_) {
        recoveryNucRequested_ = false;
        if (command(kCmdShutter) == CommandResult::Sent) {
          recoveryNucSent_ = true;
          stateSinceNs_ = now;  // give the cycle its own 5 s
          FLOG("recovery: frames fail only on Block A zero; sent 0x8000");
        }
        break;
      }
      if (since(stateSinceNs_) >= kAwaitValidMs) {
        const std::string why =
            "no valid frame within " + std::to_string(kAwaitValidMs / 1000) + " s (" +
            describeSanity(lastFlags_) + ")";
        if (!fallbackOrder_ && !fallbackTried_) {
          fallbackTried_ = true;
          fallbackOrder_ = true;
          startOrderNote_ = "fallback, because stream-first gave " + why;
          FLOG("switching to the fallback start order: %s", why.c_str());
          restartStream("switching to the fallback start order");
        } else {
          fail(why);
        }
      }
      break;
    case State::RangeWait:
      if (since(rangeSentNs_) >= kRangeToShutterMs) {
        bool skip;
        {
          std::lock_guard o(optionsMutex_);
          skip = options_.skipStartupShutter;
        }
        // After power-up the camera calibrates itself for ~65 s, and a 0x8000 sent meanwhile has
        // no visible effect (docs/DEVICE.md), so only a warm camera gets one (owner decision).
        if (skip)
          FLOG("start-up 0x8000 skipped (debug option)");
        else if (coldStart_)
          FLOG("start-up 0x8000 skipped: the camera is calibrating after power-up");
        else
          command(kCmdShutter);
        beginHold(now);  // the hold also covers a camera still calibrating after power-up
      }
      break;
    case State::RangeSwitch:
      // Like the start sequence: the range command, then 0x8000 once the gate allows it (>= 10 s
      // after the last one), then a hold through the cycle. Without it, Block A reads zero after
      // leaving the high range (M2).
      if (stalled()) break;
      if ((since(rangeSwitchNs_) >= std::max(kRangeToShutterMs, rangeSettleMs_) && gate_ &&
           gate_->shutterReady()) ||
          since(rangeSwitchNs_) >= std::max(15000, rangeSettleMs_ + 15000)) {
        if (command(kCmdShutter) == CommandResult::Sent) {
          FLOG("range switch: recalibrating");
          beginHold(now);
        } else if (since(rangeSwitchNs_) >= std::max(15000, rangeSettleMs_ + 15000)) {
          FLOG("range switch: 0x8000 refused; continuing without it");
          beginHold(now);
        }
      }
      break;
    case State::ShutterHold: {
      // A shutter cycle repeats the last frame for ~1.2 s (M1). After power-up the camera runs
      // its own calibration first: repeated frames, a single fresh one, then another cycle
      // (docs/DEVICE.md). So the hold ends with a run of fresh frames after a freeze.
      if (stalled()) break;
      const int64_t held = since(stateSinceNs_);
      if (holdSawFreeze_ ? liveStreak_ >= kLiveStreak : held >= holdNoFreezeMs_) {
        enter(State::Running, now);
      } else if (held >= kHoldMaxMs) {
        FLOG("shutter hold ends after %d ms without a run of fresh frames", kHoldMaxMs);
        enter(State::Running, now);
      }
      break;
    }
    case State::Running: {
      if (manualLockout_.exchange(false)) {
        beginLockout(now, 0, 0, true);
        break;
      }
      if (const int r = requestedRange_.exchange(-1); r >= 0) {
        switchRange(r ? TempRange::High : TempRange::Normal, now, "manual");
        break;
      }
      if (rangeFallback_) {
        rangeFallback_ = false;
        switchRange(TempRange::Normal, now, "high range unstable: frames fail the checks");
        break;
      }
      if (autoRangeRequest_ >= 0) {
        const int r = autoRangeRequest_;
        autoRangeRequest_ = -1;
        switchRange(r ? TempRange::High : TempRange::Normal, now,
                    r ? "auto: clipping for 2 s" : "auto: nothing above 110 C for 5 s");
        break;
      }
      if (const int64_t sent = manualShutterNs_.exchange(0)) {
        ++shutterCommanded_;
        lastShutterNs_ = sent;
        beginHold(now);
        break;
      }
      stalled();
      break;
    }
    case State::Lockout: {
      if (stalled()) break;
      if (lockoutHoldStartNs_) {
        // Holding: repeat 0x8000 before the cycle ends so the shutter stays closed.
        if (since(lockoutHoldStartNs_) >= kLockoutHoldMs) {
          lockoutHoldStartNs_ = 0;
          lockoutPeekStartNs_ = now;
          lockoutPeekHot_ = false;
          lockoutClear_ = 0;
          FLOG("lockout: hold over, waiting for the shutter to reopen");
        } else if (!lockoutLastCmdNs_ || since(lockoutLastCmdNs_) >= kLockoutRepeatMs) {
          if (command(kCmdShutter, CommandPurpose::Lockout) == CommandResult::Sent)
            lockoutLastCmdNs_ = nowNs();
        }
      } else if (lockoutPeekHot_) {
        // Still too hot after the shutter reopened: hold again once the gate's quiet gap is over.
        if ((nowNs() - lockoutLastCmdNs_) / kMs < 1510) break;
        if (command(kCmdShutter, CommandPurpose::Lockout) == CommandResult::Sent) {
          lockoutHoldStartNs_ = now;
          lockoutLastCmdNs_ = nowNs();
          lockoutPeekHot_ = false;
          FLOG("lockout: still too hot, holding again");
        }
      } else if (since(lockoutPeekStartNs_) >= kLockoutPeekMaxMs) {
        FLOG("lockout: no fresh frame %d ms after the hold", kLockoutPeekMaxMs);
        endLockout(now);
      }
      break;
    }
    default:
      break;
  }
}

// Shutter cycles: the camera repeats its last frame while the shutter is closed (M1), so a run of
// identical frames marks one. Tracked in every state, so the log also shows the start-up and
// power-up cycles. (Comparing contrast with recent frames misfired on scene changes.)
void Session::trackFreezes(const RawFrame& frame, const FrameView& view, uint32_t flags,
                           bool frozen, State state) {
  if (flags) {
    liveStreak_ = 0;
  } else if (frozen) {
    liveStreak_ = 0;
    ++frozenFrames_;
    ++freezeRepeats_;
    if (state == State::ShutterHold) holdSawFreeze_ = true;
    if (!inFreeze_) {
      inFreeze_ = true;
      freezeStartNs_ = lastFreshNs_ ? lastFreshNs_ : frame.arrivalNs;  // the repeated frame's own
      freezeRepeats_ = 1;
      ++shutterDetected_;
      const int64_t sinceCommand = frame.arrivalNs - lastShutterNs_;
      const bool commanded = lastShutterNs_ && sinceCommand >= 0 && sinceCommand < 3000 * kMs;
      if (!commanded) ++shutterUncommanded_;
      FLOG("frozen frames start (%s, %s)", commanded ? "after our 0x8000" : "NOT commanded",
           stateName(state));
    }
  } else {
    ++liveStreak_;
    if (inFreeze_) {
      inFreeze_ = false;
      lastFreezeEndNs_ = frame.arrivalNs;
      // Measured like tools/py/shutter_stats.py: from the repeated frame to the next fresh one.
      lastCycleMs_ = double(frame.arrivalNs - freezeStartNs_) / 1e6;
      FLOG("frozen frames end: %d repeats, one image for %.0f ms; FPA %.2f C, shutter %.2f C",
           freezeRepeats_, lastCycleMs_, view.fpaC(), view.shutterC());
      if (range_ == TempRange::Normal) recal_.onCalibration(double(frame.arrivalNs) / 1e9, view.fpaC());
    }
    lastFreshNs_ = frame.arrivalNs;
  }
}

void Session::handleFrame(const RawFrame& frame) {
  const int64_t t0 = nowNs();
  const State state = state_.load();
  const uint64_t gapsBefore = arrivals_.sequenceGaps();
  arrivals_.onFrame(frame.arrivalNs, frame.sequence);
  if (state == State::Running && arrivals_.sequenceGaps() > gapsBefore) {
    ++seqGapsLogged_;
    if (seqGapsLogged_ <= 20 || seqGapsLogged_ % 100 == 0)
      FLOG("sequence gap: %" PRIu64 " frame(s) missed before seq %u (%u bytes)",
           arrivals_.sequenceGaps() - gapsBefore, frame.sequence, frame.bytes);
  }
  if (frame.bytes != kFrameBytes) {
    ++rejectedSize_;
    if (rejectedSize_ <= 10 || rejectedSize_ % 100 == 0)
      FLOG("rejected frame #%" PRIu64 ": %u bytes, expected %zu", rejectedSize_, frame.bytes, kFrameBytes);
    return;
  }

  const FrameView view(frame.data.data());
  const ImageStats stats = computeImageStats(view.image());
  const bool steady = state == State::Running || state == State::Replay;
  const uint32_t flags = checkFrame(view, stats, !steady);
  // Frozen = identical to the last frame that passed the checks, so a stray bad frame (one
  // follows 0x8020) doesn't split a freeze in two.
  const uint64_t hash = hashImage(view.image());
  const bool frozen = !flags && hash == lastHash_;
  if (!flags) lastHash_ = hash;
  if (flags) lastFlags_ = flags;
  trackFreezes(frame, view, flags, frozen, state);


  // Temperatures: a fresh table from this frame's own metadata (PROTOCOL.md "Temperature math"),
  // readouts from raw values (CLAUDE.md rule 2).
  {
    std::lock_guard o(optionsMutex_);
    autoRange_ = options_.autoRange;
    rangeSettleMs_ = options_.rangeSettleMs;
    if (lockoutEnabled_ != options_.lockoutEnabled)
      FLOG("over-range lockout %s (debug option)", options_.lockoutEnabled ? "enabled" : "DISABLED");
    lockoutEnabled_ = options_.lockoutEnabled;
    highMath_ = options_.highMathInfiCam ? HighRangeMath::InfiCam : HighRangeMath::Ht301;
    if (stagesPending_) {
      PipelineOptions p;
      parseStages(pendingStages_, &p);  // checked in setPipeline()
      pipeline_.setOptions(p);
      stagesPending_ = false;
      FLOG("pipeline: %s (debug option \"%s\")", describeStages(p).c_str(), pendingStages_.c_str());
    }
  }
  // A frame whose only failure is values above 14 bits counts too, in case saturated pixels ever
  // read that way (M2: they clip per pixel at raw ~13835-14192 instead).
  const bool usable = !flags || flags == kSanityOver14Bit;
  int hotPixels = 0;      // over range: what the readouts show as "> 120 °C"
  int clippedPixels = 0;  // at the camera's clip: the lockout and the (parked) range switching
  if (usable) {
    lastMeta_.assign(frame.data.begin(), frame.data.end());
    lut_.build(temperatureInputs(view), range_, highMath_);
    clipRaw_ = overRangeRaw(lut_);
    // Owner decision, 2026-09-24: lock out only at the real clip, so hot parts the camera can still
    // measure (up to ~131-134 °C when it's warm) don't freeze the view. The parked high range keeps
    // its own threshold.
    lockoutRaw_ = range_ == TempRange::Normal ? kClipFloorRaw : clipRaw_;
    // Stage 2 on: readouts also stay off the known bad pixels (PLAN M4).
    const BadPixelMap& bad = pipeline_.badPixels();
    const bool exclude = pipeline_.options().badPixels && !bad.empty();
    rawReadouts_ = computeReadouts(view.image(), lut_, Region{}, clipRaw_, exclude ? &bad.mask : nullptr);
    if (stats.max >= std::min(clipRaw_, lockoutRaw_)) {
      const uint16_t* img = view.image();
      for (size_t i = 0; i < kImagePixels; ++i) {
        hotPixels += img[i] >= clipRaw_;
        clippedPixels += img[i] >= lockoutRaw_;
      }
    }
  }
  if (hotPixels) {
    lastHotPixels_ = hotPixels;
    lastHotMax_ = stats.max;
  }

  {
    bool wantCsv;
    {
      std::lock_guard o(optionsMutex_);
      wantCsv = options_.statsCsv;
    }
    if (wantCsv && !csv_) openCsv();
    if (!wantCsv && csv_) closeCsv();
    if (csv_) writeCsvRow(frame, view, stats, flags, frozen, hotPixels, usable ? &rawReadouts_ : nullptr);
  }

  if (state == State::Running) {
    hotStreak_ = clippedPixels >= kLockoutPixels ? hotStreak_ + 1 : 0;
    const int lockoutFrames = range_ == TempRange::High ? kLockoutFramesHigh : kLockoutFrames;
    if (lockoutEnabled_ && hotStreak_ >= lockoutFrames) beginLockout(frame.arrivalNs, clippedPixels, stats.max, false);
    if (autoRange_ && usable && state_.load() == State::Running) {
      if (range_ == TempRange::Normal) {
        clipStreak_ = clippedPixels >= kLockoutPixels ? clipStreak_ + 1 : 0;
        if (clipStreak_ >= kRangeUpFrames) autoRangeRequest_ = 1;
      } else {
        const bool cool = std::isfinite(rawReadouts_.high.tempC) && !rawReadouts_.high.overRange &&
                          rawReadouts_.high.tempC < kRangeDownC;
        coolStreak_ = cool ? coolStreak_ + 1 : 0;
        if (coolStreak_ >= kRangeDownFrames) autoRangeRequest_ = 0;
      }
    }
    if (rangeLogPending_ && usable && !frozen) {
      rangeLogPending_ = false;
      FLOG("range %s in effect: %s", range_ == TempRange::High ? "high" : "normal", constantsText(view).c_str());
    }
  } else if (state == State::Lockout && !lockoutHoldStartNs_ && usable && !frozen) {
    // Peeking: judge fresh frames only (repeated ones still show the scene before the shutter).
    if (clippedPixels >= kLockoutPixels)
      lockoutPeekHot_ = true;
    else if (!lockoutPeekHot_ && ++lockoutClear_ >= kLockoutClearFrames)
      endLockout(frame.arrivalNs);
  }
  captureForDump(frame);  // every full frame, so a dump also shows cycles and lockouts

  bool accepted = false;
  switch (state) {
    case State::Starting:
    case State::RangeWait:
    case State::ShutterHold:
    case State::RangeSwitch:
      ++startupDiscarded_;
      break;
    case State::Lockout:
      break;  // the display keeps the last frame from before the lockout
    case State::AwaitValid:
      ++startupDiscarded_;
      if (flags) {
        validStreak_ = 0;
        blockAZeroStreak_ = flags == kSanityBlockAZero ? blockAZeroStreak_ + 1 : 0;
        if (blockAZeroStreak_ == 25) recoveryNucRequested_ = true;  // 1 s of nothing else wrong
      } else if (++validStreak_ >= kValidStreak) {
        // Repeated frames right at the start: the camera is calibrating, as after power-up.
        coldStart_ = inFreeze_;
        if (coldStart_) {
          FLOG("stream began with repeated frames: the camera is calibrating (power-up)");
          setBanner("Camera calibrating after power-up…");
        }
        if (!fallbackOrder_) command(kCmdRangeNormal);  // the fallback sent it before streaming
        rangeSentNs_ = nowNs();
        enter(State::RangeWait, rangeSentNs_);
      }
      break;
    case State::Running:
    case State::Replay:
      if (flags) {
        ++rejectedChecks_;
        if (rejectedChecks_ <= 10 || rejectedChecks_ % 100 == 0)
          FLOG("frame failed checks: %s", describeSanity(flags).c_str());
        ++sanityStreak_;
        if (range_ == TempRange::High && sanityStreak_ >= 10 && state == State::Running) {
          // M2: the high range's output can slide to the floor after a switch; fall back.
          FLOG("high range: frames fail the checks (%s); back to the normal range", describeSanity(flags).c_str());
          sanityStreak_ = 0;
          rangeFallback_ = true;
        } else if (sanityStreak_ >= kFailStreak && state == State::Running) {
          fail("Frames keep failing the sanity checks (" + describeSanity(flags) + ")");
        }
        break;
      }
      sanityStreak_ = 0;
      accepted = true;
      break;
    default:
      break;
  }

  // Frames stop reaching the display (our own 0x8000, a range switch, a lockout, a rejected frame):
  // stage 1 holds and crossfades back from what was last shown.
  if (!accepted && pipelineFed_) {
    pipeline_.hold();
    pipelineFed_ = false;
  }

  if (accepted) {
    const double dt = lastReadoutNs_ ? double(frame.arrivalNs - lastReadoutNs_) / 1e9 : 0.04;
    lastReadoutNs_ = frame.arrivalNs;
    shownReadouts_ = readoutFilter_.update(rawReadouts_, view.image(), lut_, Region{}, clipRaw_, dt);

    // Camera-hot banner (PLAN M2): the module is rated to ~60 °C ambient and runs ~11-13 °C above it.
    // The FPA word means something else in the high range (M2: it decodes to ~70 °C there).
    const double fpa = range_ == TempRange::Normal ? view.fpaC() : 0.0;
    if (cameraHotText_.empty() && fpa > 55.0 && bannerIs("")) {
      cameraHotText_ = format("Camera is hot (%.0f °C): readings may drift; let it cool", fpa);
      setBanner(cameraHotText_);
      FLOG("camera hot: FPA %.1f C", fpa);
    } else if (!cameraHotText_.empty() && fpa < 53.0) {
      if (bannerIs(cameraHotText_)) setBanner("");  // leave any other banner alone
      cameraHotText_.clear();
    }

    // The recalibration policy, dry run: when it would ask, log it and count it as if the
    // calibration had happened, so the overlay shows the cadence it would keep. Nothing is sent.
    const bool recalBlocked = state != State::Running || range_ != TempRange::Normal || inFreeze_;
    if (recal_.due(double(frame.arrivalNs) / 1e9, view.fpaC(), recalBlocked)) {
      ++recalDue_;
      recalDueNs_ = frame.arrivalNs;
      recalReason_ = recal_.reason();
      FLOG("recalibration policy (dry run, nothing sent): would ask now: %s", recalReason_.c_str());
      recal_.onCalibration(double(frame.arrivalNs) / 1e9, view.fpaC());
    }

    DisplayFrame& out = renderer_.frameSlot();
    pipeline_.process(view.image(), out.intensity.data(), nullptr, {view.fpaC(), view.shutterC()});
    for (size_t i = 0; i < kImagePixels; ++i) out.clipped[i] = view.image()[i] >= clipRaw_ ? 255 : 0;
    pipelineFed_ = true;
    out.arrivalNs = frame.arrivalNs;
    renderer_.publishFrame();
    const int64_t workNs = nowNs() - t0;
    procMs_.push(double(workNs) / 1e6);
    perfHint_.report(workNs);
    lastCpu_ = sched_getcpu();
    ++cpuFrames_;
    if (lastCpu_ >= 0 && CPU_ISSET(lastCpu_, &bigCpus_)) ++cpuFramesBig_;
  }

  // Overlay snapshot.
  std::lock_guard lock(snapshotMutex_);
  Snapshot& s = *snapshot_;
  s.state = state_.load();
  s.fallbackOrder = fallbackOrder_;
  s.coldStart = coldStart_;
  s.startOrderNote = startOrderNote_;
  s.fps = arrivals_.fps();
  s.jitterMs = arrivals_.jitterMs();
  s.maxIntervalMs = arrivals_.maxIntervalMs();
  s.frames = arrivals_.frames();
  s.seqGaps = arrivals_.sequenceGaps();
  s.arrivalGaps = arrivals_.arrivalGaps();
  s.rejectedSize = rejectedSize_;
  s.rejectedChecks = rejectedChecks_;
  s.startupDiscarded = startupDiscarded_;
  s.overruns = ring_->overruns() - overrunBase_;
  s.restarts = restarts_;
  s.frozen = frozenFrames_;
  s.shutterCommanded = shutterCommanded_;
  s.shutterDetected = shutterDetected_;
  s.shutterUncommanded = shutterUncommanded_;
  s.lockouts = lockouts_;
  s.lockoutCommands = lockoutCommands_;
  s.lastHotPixels = lastHotPixels_;
  s.lastHotMax = lastHotMax_;
  s.raw = rawReadouts_;
  s.shown = shownReadouts_;
  s.vertex = lut_.vertex();
  s.clipRaw = clipRaw_;
  s.lockoutRaw = lockoutRaw_;
  s.rangeHigh = range_ == TempRange::High;
  s.autoRange = autoRange_;
  s.pipeline = describeStages(pipeline_.options());
  s.driftC = pipeline_.lastDriftC();
  s.driftMap = !pipeline_.driftMap().empty();
  s.pipelineFrozen = pipeline_.frozen();
  s.pipelineBlending = pipeline_.blending();
  s.highMathInfiCam = highMath_ == HighRangeMath::InfiCam;
  s.camMaxC = lut_.valid(view.maxRaw()) ? lut_[view.maxRaw()] : NAN;
  s.camMinC = lut_.valid(view.minRaw()) ? lut_[view.minRaw()] : NAN;
  s.camCenterC = lut_.valid(view.centerRaw()) ? lut_[view.centerRaw()] : NAN;
  s.lastCycleMs = lastCycleMs_;
  s.recalDue = recalDue_;
  s.recalAgoS = recalDueNs_ ? double(frame.arrivalNs - recalDueNs_) / 1e9 : -1.0;
  s.recalReason = recalReason_;
  s.procP95Ms = procMs_.percentile(95);
  s.cpu = lastCpu_;
  s.perfHint = perfHint_.active() ? "ADPF " + std::to_string(perfHint_.targetNs() / 1000000) + " ms" : "no ADPF";
  s.bigShare = cpuFrames_ ? double(cpuFramesBig_) / double(cpuFrames_) : 0.0;
  s.bytes = frame.bytes;
  s.flags = flags;
  s.lastBadFlags = lastFlags_;
  s.fpaAvg = view.fpaAverage();
  s.fpaRaw = view.fpaRaw();
  s.maxRaw = view.maxRaw();
  s.minRaw = view.minRaw();
  s.avgRaw = view.avgRaw();
  s.centerRaw = view.centerRaw();
  s.maxX = view.maxX();
  s.maxY = view.maxY();
  s.minX = view.minX();
  s.minY = view.minY();
  s.cal00 = view.cal00();
  s.fpaC = view.fpaC();
  s.shutterC = view.shutterC();
  s.coreC = view.coreC();
  for (int i = 0; i < 5; ++i) s.cal[i] = view.cal(i + 1);
  s.correction = view.correction();
  s.reflectedC = view.reflectedC();
  s.airC = view.airC();
  s.humidity = view.humidity();
  s.emissivity = view.emissivity();
  s.distance = view.distance();
  s.image = stats;
  if (arrivals_.frames() % 25 == 1) {
    s.firmware = view.firmware();
    s.texts = findTextRuns(view);
  }
}

// --- Dumps ---------------------------------------------------------------------------------------

std::string Session::startDump(int frames) {
  const State state = state_.load();
  if (state != State::Running && state != State::Replay) return "not streaming";
  if (dumpWanted_.load() > 0) return "a dump is already in progress";
  dumpBase_ = dumpsDir_ + "/dump_" + wallClock("%Y%m%d_%H%M%S");
  {
    std::lock_guard lock(snapshotMutex_);
    dumpStatus_ = "capturing " + std::to_string(frames) + " frames";
  }
  dumpWanted_.store(std::max(1, frames), std::memory_order_release);
  FLOG("dump requested: %d frames -> %s", frames, dumpBase_.c_str());
  return dumpBase_.substr(dumpBase_.find_last_of('/') + 1);
}

void Session::captureForDump(const RawFrame& frame) {
  const int wanted = dumpWanted_.load(std::memory_order_acquire);
  if (wanted <= 0) return;
  if (dumpInfo_.timestampsNs.empty()) {
    dumpFrames_.clear();
    dumpFrames_.reserve(size_t(wanted) * kFramePixels);
    dumpInfo_ = DumpInfo{};
    dumpInfo_.wallClockStart = wallClock("%Y-%m-%dT%H:%M:%S%z");
  }
  dumpFrames_.insert(dumpFrames_.end(), frame.data.begin(), frame.data.end());
  dumpInfo_.timestampsNs.push_back(frame.arrivalNs);
  dumpInfo_.sequence.push_back(frame.sequence);
  if (int(dumpInfo_.timestampsNs.size()) >= wanted) finishDump();
}

void Session::finishDump() {
  dumpWanted_ = 0;
  dumpInfo_.manufacturer = manufacturer_;
  dumpInfo_.product = product_;
  dumpInfo_.serial = serial_;
  dumpInfo_.appVersion = appVersion_;
  dumpInfo_.source = state_.load() == State::Replay ? "replay" : "camera";
  dumpInfo_.startOrder = fallbackOrder_ ? "fallback" : "stream-first";
  dumpInfo_.range = range_ == TempRange::High ? "high" : "normal";
  {
    std::lock_guard lock(snapshotMutex_);
    dumpInfo_.firmware = snapshot_->firmware;
  }
  if (gate_) {
    const auto start = gate_->history().empty() ? std::chrono::steady_clock::time_point{}
                                                : gate_->history().front().time;
    for (const auto& e : gate_->history()) {
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(e.time - start).count();
      dumpInfo_.commandLog.push_back(format("+%lld ms 0x%04x %s", (long long)ms, e.value,
                                            commandResultText(e.result)));
    }
  }
  if (dumpWriter_.joinable()) dumpWriter_.join();
  dumpWriter_ = std::thread([this, frames = std::move(dumpFrames_), info = std::move(dumpInfo_),
                             base = dumpBase_] {
    std::string error;
    const bool ok = writeDump(base, frames, info, &error);
    const std::string name = base.substr(base.find_last_of('/') + 1);
    FLOG("dump %s: %s", name.c_str(), ok ? "saved" : error.c_str());
    std::lock_guard lock(snapshotMutex_);
    dumpStatus_ = ok ? "saved " + name + " (" + std::to_string(info.timestampsNs.size()) + " frames)"
                     : "dump failed: " + error;
  });
  dumpFrames_ = {};
  dumpInfo_ = DumpInfo{};
}

// --- Stats CSV (debug) ---------------------------------------------------------------------------

void Session::openCsv() {
  const std::string path = statsDir_ + "/stats_" + wallClock("%Y%m%d_%H%M%S") + ".csv";
  csv_ = std::fopen(path.c_str(), "w");
  if (!csv_) {
    FLOG("stats CSV: cannot open %s", path.c_str());
    return;
  }
  std::fprintf(csv_, "t_ms,seq,state,bytes,flags,img_min,img_max,img_mean,img_sd,frozen,hot");
  for (int i = 0; i < 16; ++i) std::fprintf(csv_, ",P%d", i);
  for (int i : {0, 1, 2}) std::fprintf(csv_, ",Q%d", i);
  for (int i = 13; i < 24; ++i) std::fprintf(csv_, ",Q%d", i);
  std::fprintf(csv_, ",lo_c,hi_c,hi_over,ce_c\n");
  FLOG("stats CSV started: %s", path.c_str());
}

void Session::closeCsv() {
  if (!csv_) return;
  std::fclose(csv_);
  csv_ = nullptr;
  FLOG("stats CSV closed");
}

void Session::writeCsvRow(const RawFrame& frame, const FrameView& view, const ImageStats& stats,
                          uint32_t flags, bool frozen, int hotPixels, const Readouts* r) {
  std::fprintf(csv_, "%.3f,%u,%s,%u,%u,%u,%u,%.2f,%.2f,%d,%d",
               double(frame.arrivalNs - streamStartNs_) / 1e6, frame.sequence, stateName(state_.load()),
               frame.bytes, flags, stats.min, stats.max, stats.mean, stats.stddev, frozen ? 1 : 0,
               hotPixels);
  for (int i = 0; i < 16; ++i) std::fprintf(csv_, ",%u", view.at(kOffsetP + i));
  for (int i : {0, 1, 2}) std::fprintf(csv_, ",%u", view.at(kOffsetQ + i));
  for (int i = 13; i < 24; ++i) std::fprintf(csv_, ",%u", view.at(kOffsetQ + i));
  // Unsmoothed readouts (the M2 device check compares them with tools/harness temps).
  if (r)
    std::fprintf(csv_, ",%.3f,%.3f,%d,%.3f", r->low.tempC, r->high.tempC, r->high.overRange ? 1 : 0,
                 r->center.tempC);
  else
    std::fprintf(csv_, ",,,,");
  std::fprintf(csv_, "\n");
}

// --- Replay (debug) ------------------------------------------------------------------------------

std::string Session::startReplay(const std::string& base) {
  {
    std::lock_guard lock(streamMutex_);
    if (cameraOpen_) return "a camera is connected; unplug it first";
  }
  stopReplay();
  LoadedDump dump;
  std::string error;
  if (!loadDump(base, &dump, &error)) return error;
  pipeline_.setBadPixels(badPixelMapFor(dump.serial));  // the dump's camera, not the connected one
  pipeline_.setDriftMap(driftMapFor(dump.serial));
  replay_ = std::move(dump);
  replayName_ = base.substr(base.find_last_of('/') + 1);
  arrivals_.reset();
  rangeWindow_.clear();
  lastHash_ = 0;
  inFreeze_ = false;
  lastFreshNs_ = 0;
  liveStreak_ = 0;
  readoutFilter_.reset();
  rawReadouts_ = shownReadouts_ = Readouts{};
  lastReadoutNs_ = 0;
  pipeline_.reset();
  pipelineFed_ = false;
  enter(State::Replay, nowNs());
  startProcessing();
  replayRun_ = true;
  replayThread_ = std::thread(&Session::replayLoop, this);
  FLOG("replay started: %s (%zu frames%s)", replayName_.c_str(), replay_.frameCount,
       replay_.timestampsNs.empty() ? ", nominal 40 ms spacing" : "");
  return "";
}

void Session::stopReplay() {
  if (!replayRun_.exchange(false)) return;
  replayThread_.join();
  stopProcessing();
  enter(State::Idle, nowNs());
  renderer_.clear();
  FLOG("replay stopped");
}

void Session::replayLoop() {
  const auto& ts = replay_.timestampsNs;
  uint32_t sequence = 1;
  size_t i = 0;
  int64_t loopStart = nowNs();
  while (replayRun_) {
    const int64_t offset = ts.empty() ? int64_t(i) * 40 * kMs : ts[i] - ts[0];
    const int64_t due = loopStart + offset;
    while (replayRun_ && nowNs() < due) {
      const int64_t wait = std::min<int64_t>(due - nowNs(), 20 * kMs);
      if (wait > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(wait));
    }
    if (!replayRun_) break;
    if (RawFrame* slot = ring_->beginWrite()) {
      std::memcpy(slot->data.data(), &replay_.frames[i * kFramePixels], kFrameBytes);
      slot->bytes = uint32_t(kFrameBytes);
      slot->arrivalNs = nowNs();
      slot->sequence = sequence;
      slot->replay = true;
      ring_->commitWrite();
    }
    ++sequence;
    if (++i == replay_.frameCount) {
      i = 0;
      loopStart = nowNs() + 40 * kMs;
    }
  }
}

// --- UI queries and commands ---------------------------------------------------------------------

std::string Session::sendShutter() {
  std::lock_guard lock(streamMutex_);
  if (!cameraOpen_ || !gate_) return "no camera";
  const CommandResult result = gate_->send(kCmdShutter);
  if (result == CommandResult::Sent) manualShutterNs_ = nowNs();
  return commandResultText(result);
}

std::string Session::requestRange(bool high) {
  if (state_.load() != State::Running) return "not running";
  requestedRange_ = high ? 1 : 0;
  return high ? "high range requested" : "normal range requested";
}

std::string Session::requestCapture(const std::string& label, bool rangePair) {
  FLOG("owner mark: %s", label.c_str());
  if (state_.load() != State::Running) return "not running";
  capturePairRequested_ = rangePair;
  captureRequested_ = true;
  return rangePair ? "range test started" : "capture started";
}

std::string Session::triggerLockout() {
  if (state_.load() != State::Running) return "not running";
  manualLockout_ = true;
  return "lockout requested";
}

void Session::setOptions(const Options& options) {
  std::lock_guard lock(optionsMutex_);
  options_ = options;
}

std::string Session::setDisplay(int upscaler, const std::string& paletteJson) {
  std::vector<std::array<uint8_t, 3>> lut;
  PaletteSpec spec;
  if (!paletteJson.empty()) {
    std::string error;
    if (!parsePalette(paletteJson, &spec, &error)) return "palette: " + error;
    lut = buildPaletteLut(spec);
  }
  renderer_.setDisplay(upscaler, std::move(lut), spec.saturation);
  return "";
}

void Session::registerDriftMap(const std::string& serial, DriftMap map) {
  std::lock_guard lock(driftMutex_);
  if (map.empty()) {
    FLOG("drift map for %s: wrong size, ignored", serial.c_str());
    return;
  }
  driftMaps_[serial] = std::move(map);
  FLOG("drift map for %s registered", serial.c_str());
}

DriftMap Session::driftMapFor(const std::string& serial) {
  std::lock_guard lock(driftMutex_);
  const auto it = driftMaps_.find(serial);
  return it == driftMaps_.end() ? DriftMap{} : it->second;
}

std::string Session::setPipeline(const std::string& stages) {
  PipelineOptions p;
  if (!parseStages(stages, &p)) return "unknown stage in \"" + stages + "\"";
  std::lock_guard lock(optionsMutex_);
  pendingStages_ = stages;
  stagesPending_ = true;
  return "";
}

void Session::captureSetBanner(const std::string& text) {
  captureBanner_ = text;
  setBanner(text);
}

bool Session::bannerIs(const std::string& text) {
  std::lock_guard lock(snapshotMutex_);
  return banner_ == text;
}

std::vector<float> Session::readouts() {
  std::lock_guard lock(snapshotMutex_);
  std::vector<float> v;
  for (const Spot* spot : {&snapshot_->shown.high, &snapshot_->shown.low, &snapshot_->shown.center}) {
    v.push_back(float(spot->tempC));
    v.push_back(float(spot->x));
    v.push_back(float(spot->y));
    v.push_back(float((std::isfinite(spot->tempC) ? 1 : 0) | (spot->overRange ? 2 : 0)));
  }
  v.push_back(snapshot_->rangeHigh ? 1.0f : 0.0f);
  return v;
}

void Session::setBanner(const std::string& text) {
  std::lock_guard lock(snapshotMutex_);
  banner_ = text;
}

std::string Session::statusLine() {
  std::lock_guard lock(snapshotMutex_);
  const State state = state_.load();
  const bool streaming = state == State::Starting || state == State::AwaitValid ||
                         state == State::RangeWait || state == State::ShutterHold ||
                         state == State::RangeSwitch || state == State::Lockout ||
                         state == State::Running || state == State::Replay;
  std::string banner = banner_;
  std::replace(banner.begin(), banner.end(), ';', ',');
  std::string dump = dumpStatus_;
  std::replace(dump.begin(), dump.end(), ';', ',');
  return std::string("state=") + stateName(state) + ";streaming=" + (streaming ? "1" : "0") +
         ";banner=" + banner + ";dump=" + dump + ";replay=" + (replayRun_ ? replayName_ : "");
}

std::string Session::overlayText() {
  std::vector<CommandLogEntry> commands;
  if (gate_) commands = gate_->history();
  const double p50 = renderer_.latencyP50Ms(), p95 = renderer_.latencyP95Ms();

  std::lock_guard lock(snapshotMutex_);
  const Snapshot& s = *snapshot_;
  std::string o;
  const std::string note = s.startOrderNote.empty() ? "" : " (" + s.startOrderNote + ")";
  const State state = state_.load();
  const char* start = state == State::Replay ? ""
                      : s.coldStart          ? "  power-up start (camera calibrating)"
                                             : "  warm start";
  o += format("ThermalView %s  %s  %s order%s%s\n", appVersion_.c_str(), stateName(state),
              s.fallbackOrder ? "fallback" : "stream-first", note.c_str(), start);
  o += format("fps %.2f  jitter %.2f ms  max %.1f ms  latency p50 %.1f / p95 %.1f ms  proc p95 %.2f ms  "
              "cpu %d (big %.0f%%)  %s\n",
              s.fps, s.jitterMs, s.maxIntervalMs, p50, p95, s.procP95Ms, s.cpu, 100.0 * s.bigShare,
              s.perfHint.c_str());
  o += format("frames %" PRIu64 "  drops: seq %" PRIu64 "  bus %" PRIu64 "  rejected %" PRIu64
              " (size %" PRIu64 ")  overrun %" PRIu64 "  start-up %" PRIu64 "  restarts %" PRIu64 "\n",
              s.frames, s.seqGaps, s.arrivalGaps, s.rejectedSize + s.rejectedChecks, s.rejectedSize,
              s.overruns, s.startupDiscarded, s.restarts);
  o += format("frame 256x196 (%u B)  checks: %s  last failure: %s\n", s.bytes,
              describeSanity(s.flags).c_str(), describeSanity(s.lastBadFlags).c_str());
  o += s.recalDue ? format("recalibration policy (dry run, nothing sent): would have asked %" PRIu64
                            " times, last %.0f s ago (%s)\n",
                            s.recalDue, s.recalAgoS, s.recalReason.c_str())
                  : std::string("recalibration policy (dry run, nothing sent): not due yet\n");
  o += format("FPA raw %u (%.2f C)  avg %u   shutter %.2f C   core %.2f C   cal_00 %u\n", s.fpaRaw,
              s.fpaC, s.fpaAvg, s.shutterC, s.coreC, s.cal00);
  o += format("cal_01..05  %.6g  %.6g  %.6g  %.6g  %.6g\n", s.cal[0], s.cal[1], s.cal[2], s.cal[3],
              s.cal[4]);
  o += format("user area: corr %.3f  refl %.2f C  air %.2f C  hum %.3f  emis %.3f  dist %u\n",
              s.correction, s.reflectedC, s.airC, s.humidity, s.emissivity, s.distance);
  o += format("block A: max %u @(%u,%u)  min %u @(%u,%u)  avg %u  center %u   image: min %u max %u "
              "mean %.0f sd %.1f\n",
              s.maxRaw, s.maxX, s.maxY, s.minRaw, s.minX, s.minY, s.avgRaw, s.centerRaw, s.image.min,
              s.image.max, s.image.mean, s.image.stddev);
  o += "firmware \"" + s.firmware + "\"  text:";
  for (const auto& t : s.texts) o += format(" [P+%zu \"%s\"]", t.byteOffsetFromP, t.text.c_str());
  o += "\n";
  o += format("shutter: commanded %" PRIu64 "  shutter-like %" PRIu64 " (not commanded %" PRIu64
              ")  last %.0f ms  frozen frames %" PRIu64 "\n",
              s.shutterCommanded, s.shutterDetected, s.shutterUncommanded, s.lastCycleMs, s.frozen);
  o += format("lockout: %" PRIu64 " (%" PRIu64 " commands)  trigger %d px >= raw %u  last hot: %u px, max %u\n",
              s.lockouts, s.lockoutCommands, kLockoutPixels, s.lockoutRaw, s.lastHotPixels, s.lastHotMax);
  auto t = [](const Spot& sp) {
    if (sp.overRange) return std::string("> 120");
    return std::isfinite(sp.tempC) ? format("%.2f", sp.tempC) : std::string("--");
  };
  auto c = [](double v) { return std::isfinite(v) ? format("%.2f", v) : std::string("--"); };
  o += format("range: %s, auto %s, high-range math %s\n", s.rangeHigh ? "HIGH" : "normal",
              s.autoRange ? "on" : "off", s.highMathInfiCam ? "InfiCam" : "ht301");
  o += format("pipeline: %s%s   drift since calibration %+.2f C%s\n", s.pipeline.c_str(),
              s.pipelineFrozen ? "  (holding)" : s.pipelineBlending ? "  (blending back)" : "", s.driftC,
              s.driftMap ? "" : " (no drift map for this camera)");
  o += format("temps (%s range, per-frame table, vertex raw %u): high %s @(%.0f,%.0f)  low %s @(%.0f,%.0f)  "
              "center %s   camera's own: max %s min %s center %s\n",
              s.rangeHigh ? "high" : "normal", s.vertex, t(s.raw.high).c_str(), s.raw.high.x, s.raw.high.y,
              t(s.raw.low).c_str(), s.raw.low.x, s.raw.low.y, t(s.raw.center).c_str(), c(s.camMaxC).c_str(),
              c(s.camMinC).c_str(), c(s.camCenterC).c_str());
  o += "commands:";
  const size_t first = commands.size() > 6 ? commands.size() - 6 : 0;
  const auto origin = commands.empty() ? std::chrono::steady_clock::time_point{} : commands.front().time;
  for (size_t i = first; i < commands.size(); ++i) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(commands[i].time - origin).count();
    o += format("  +%lld ms 0x%04x %s", (long long)ms, commands[i].value, commandResultText(commands[i].result));
  }
  if (!dumpStatus_.empty()) o += "\ndump: " + dumpStatus_;
  return o;
}

}  // namespace tv
