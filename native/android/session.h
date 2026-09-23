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
#include "tv/stats.h"

struct libusb_context;
struct uvc_context;
struct uvc_device_handle;
struct uvc_frame;

namespace tv {

enum class State { Idle, Starting, AwaitValid, RangeWait, ShutterHold, Running, Failed, Replay };
const char* stateName(State state);

struct Options {
  bool skipStartupShutter = false;  // debug: learn whether 0x8020 alone triggers a cycle
  bool statsCsv = false;            // debug: per-frame metadata and statistics to CSV
  bool fallbackOrder = false;       // debug: InfiCam's order (0x8004, 0x8020 before streaming)
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
  void enter(State state, int64_t now);
  void beginHold(int64_t now);  // withhold frames through a shutter cycle
  void trackFreezes(const RawFrame& frame, const FrameView& view, uint32_t flags, bool frozen,
                    State state);
  void fail(const std::string& reason);
  CommandResult command(uint16_t value);
  void captureForDump(const RawFrame& frame);
  void finishDump();
  void writeCsvRow(const RawFrame& frame, const FrameView& view, const ImageStats& stats,
                   uint32_t flags, bool frozen);
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
