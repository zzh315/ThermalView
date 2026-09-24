// Camera session: USB/UVC lifecycle, capture, the start sequence, frame checks, watchdog,
// dumper and replay (docs/PLAN.md M1). One instance per process.
#pragma once

#include <android/native_window.h>

#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "renderer.h"
#include "tv/command_gate.h"
#include "tv/dump.h"
#include "tv/frame.h"
#include "tv/frame_ring.h"
#include "tv/readouts.h"
#include "tv/temperature.h"
#include "tv/stats.h"

struct libusb_context;
struct uvc_context;
struct uvc_device_handle;
struct uvc_frame;

namespace tv {

enum class State { Idle, Starting, AwaitValid, RangeWait, ShutterHold, Running, Lockout, Failed, Replay };
const char* stateName(State state);

struct Options {
  bool skipStartupShutter = false;  // debug: learn whether 0x8020 alone triggers a cycle
  bool statsCsv = false;            // debug: per-frame metadata and statistics to CSV
  bool fallbackOrder = false;       // debug: InfiCam's order (0x8004, 0x8020 before streaming)
  bool dumpOnLockout = false;       // debug: save the frames that trigger an over-range lockout
  bool autoRange = false;           // automatic range switching (off until the M2 iron session)
  bool highMathInfiCam = false;     // debug: InfiCam's high-range math instead of ht301_hacklib's
};

class Session {
 public:
  static Session& get();

  void init(const std::string& storageDir, const std::string& appVersion);

  // UI thread. The Java side keeps ownership of the file descriptor and closes it after
  // closeCamera() (docs/PROTOCOL.md: one owner).
  bool openCamera(int fd, const std::string& manufacturer, const std::string& product,
                  const std::string& serial);
  void closeCamera();

  void setWindow(ANativeWindow* window) { renderer_.setWindow(window); }

  std::string overlayText();
  std::string statusLine();  // "key=value;..." for the UI
  std::string startDump(int frames);
  std::string startReplay(const std::string& base);  // "" on success, else the reason
  void stopReplay();
  std::string sendShutter();  // debug / Recalibrate
  std::string triggerLockout();  // debug: run one over-range lockout without a hot scene
  // debug: recalibrate, then dump 200 frames; with rangePair, again in the high range, then back.
  std::string requestCapture(const std::string& label, bool rangePair = false);
  // Shown readouts for the UI: {temp, x, y, flags} for high, low and center, then 1 if the high
  // range is active. flags: 1 = a valid temperature, 2 = over range. temp is NaN when invalid.
  std::vector<float> readouts();
  std::string requestRange(bool high);  // manual range switch (debug)
  void setOptions(const Options& options);

 private:
  struct Snapshot;  // latest metadata and frame statistics, for the overlay

  static void frameCallback(uvc_frame* frame, void* user);

  bool startStreaming();  // per the current start order; streamMutex_ held
  void stopStreaming();   // streamMutex_ held
  void restartStream(const char* reason);
  void startProcessing();
  void stopProcessing();
  void processLoop();
  void handleFrame(const RawFrame& frame);
  void tick(int64_t now);
  void tickCapture(int64_t now);
  void enter(State state, int64_t now);
  void beginHold(int64_t now, int noFreezeMs = 1000);  // withhold frames through a shutter cycle
  void switchRange(TempRange target, int64_t now, const char* why);
  void trackFreezes(const RawFrame& frame, const FrameView& view, uint32_t flags, bool frozen,
                    State state);
  void fail(const std::string& reason);
  CommandResult command(uint16_t value, CommandPurpose purpose = CommandPurpose::Normal);
  void beginLockout(int64_t now, int hotPixels, uint16_t maxRaw, bool manual);
  void endLockout(int64_t now);
  void captureForDump(const RawFrame& frame);
  void finishDump();
  void writeCsvRow(const RawFrame& frame, const FrameView& view, const ImageStats& stats,
                   uint32_t flags, bool frozen, int hotPixels, const Readouts* readouts);
  bool bannerIs(const std::string& text);
  void openCsv();
  void closeCsv();
  void replayLoop();
  void setBanner(const std::string& text);

  // Paths and identity.
  std::string storageDir_, dumpsDir_, statsDir_, appVersion_;
  std::string manufacturer_, product_, serial_;

  // USB / UVC.
  libusb_context* usb_ = nullptr;
  uvc_context* uvc_ = nullptr;
  uvc_device_handle* devh_ = nullptr;
  std::unique_ptr<unsigned char[]> ctrl_;  // uvc_stream_ctrl_t, opaque here
  std::thread eventThread_;
  std::atomic<bool> eventRun_{false};
  std::mutex streamMutex_;
  bool streaming_ = false;
  bool cameraOpen_ = false;
  std::unique_ptr<CameraCommands> gate_;

  // Processing.
  std::unique_ptr<FrameRing<8>> ring_;
  std::thread procThread_;
  std::atomic<bool> procRun_{false};
  Renderer renderer_;
  Options options_;
  std::mutex optionsMutex_;

  // Start sequence and state (processing thread, except where atomic).
  std::atomic<State> state_{State::Idle};
  int64_t stateSinceNs_ = 0;
  int64_t streamStartNs_ = 0;
  int64_t rangeSentNs_ = 0;
  int64_t lastShutterNs_ = 0;
  int validStreak_ = 0;
  bool countersResetPending_ = false;  // reset drop statistics when Running begins
  int liveStreak_ = 0;          // consecutive fresh frames that passed the checks
  bool holdSawFreeze_ = false;  // a freeze began during the current hold
  bool coldStart_ = false;      // the stream began with repeated frames
  uint64_t overrunBase_ = 0;
  uint64_t seqGapsLogged_ = 0;
  bool fallbackOrder_ = false;
  bool fallbackTried_ = false;
  std::atomic<int64_t> manualShutterNs_{0};
  std::atomic<bool> manualLockout_{false};
  std::atomic<bool> captureRequested_{false};
  enum class CapturePhase { None, WaitGap, Recalibrating, Settle, Recording, Done, SwitchHigh, SwitchBack };
  std::atomic<bool> capturePairRequested_{false};
  bool capturePair_ = false;
  int captureGapMs_ = 60000;
  CapturePhase capturePhase_ = CapturePhase::None;  // processing thread
  int64_t capturePhaseNs_ = 0;
  int64_t lastFreezeEndNs_ = 0;  // end of the latest shutter cycle, ours or the camera's

  // Temperatures (processing thread; the snapshot carries copies to the UI).
  TemperatureLut lut_;
  ReadoutFilter readoutFilter_;
  Readouts rawReadouts_{}, shownReadouts_{};
  int64_t lastReadoutNs_ = 0;
  TempRange range_ = TempRange::Normal;
  HighRangeMath highMath_ = HighRangeMath::Ht301;
  std::string cameraHotText_;  // the camera-hot banner while it shows

  // Range switching (processing thread, except the atomic request).
  std::atomic<int> requestedRange_{-1};  // -1 none, 0 normal, 1 high
  int autoRangeRequest_ = -1;
  int clipStreak_ = 0, coolStreak_ = 0;
  int64_t nextRangeAttemptNs_ = 0;
  int holdNoFreezeMs_ = 1000;
  bool rangeLogPending_ = false;
  bool autoRange_ = false;
  std::vector<uint16_t> lastMeta_;  // the latest usable frame, for logging constants at a switch

  // Over-range lockout (processing thread).
  int hotStreak_ = 0;
  int64_t lockoutSinceNs_ = 0, lockoutHoldStartNs_ = 0, lockoutLastCmdNs_ = 0, lockoutPeekStartNs_ = 0;
  bool lockoutPeekHot_ = false;
  int lockoutClear_ = 0;
  uint64_t lockouts_ = 0, lockoutCommands_ = 0;
  uint32_t lastHotPixels_ = 0;
  uint16_t lastHotMax_ = 0;

  // Statistics.
  ArrivalTracker arrivals_;
  RollingWindow procMs_{250};
  RollingWindow rangeWindow_{125};
  uint64_t rejectedSize_ = 0, rejectedChecks_ = 0, startupDiscarded_ = 0, restarts_ = 0;
  uint64_t frozenFrames_ = 0, shutterCommanded_ = 0, shutterDetected_ = 0, shutterUncommanded_ = 0;
  int sanityStreak_ = 0;
  uint32_t lastFlags_ = 0;
  uint64_t lastHash_ = 0;
  bool inFreeze_ = false;
  int64_t freezeStartNs_ = 0;
  int64_t lastFreshNs_ = 0;  // arrival of the last fresh frame that passed the checks
  int freezeRepeats_ = 0;
  double lastCycleMs_ = 0;
  std::unique_ptr<Snapshot> snapshot_;
  std::mutex snapshotMutex_;
  std::string banner_;
  std::string startOrderNote_;

  // Dump capture (processing thread fills, a writer thread saves).
  std::atomic<int> dumpWanted_{0};
  std::vector<uint16_t> dumpFrames_;
  DumpInfo dumpInfo_;
  std::string dumpBase_;
  std::thread dumpWriter_;
  std::string dumpStatus_;

  // Stats CSV.
  FILE* csv_ = nullptr;

  // Replay.
  std::thread replayThread_;
  std::atomic<bool> replayRun_{false};
  LoadedDump replay_;
  std::string replayName_;
};

}  // namespace tv
