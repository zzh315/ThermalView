// M4 image pipeline (docs/PLAN.md M4, docs/PIPELINE_LOG.md): camera image in, display intensity out.
// Every stage can be switched off, and with all of them off the output equals renderBaseline. Stages
// shape the display only; readouts always come from raw values (CLAUDE.md rule 2).
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "tv/bad_pixels.h"
#include "tv/frame.h"

namespace tv {

struct PipelineOptions {
  // Defaults are the approved stages (docs/PIPELINE_LOG.md); the rest start off.
  //
  // Stage 1 (approved 2026-09-25): while the camera repeats one frame (a shutter cycle), hold the
  // last output without advancing any stage; when fresh frames return, crossfade from the held output
  // over this many frames (0 = jump). A motion gate is left out on purpose: the NUC's own correction
  // change is large and coherent (PIPELINE_LOG, stage 1), so it would read as motion.
  bool shutterHold = true;
  int shutterBlendFrames = 8;  // ~0.3 s at 25 fps

  // Stage 2 (approved 2026-09-25): replace the camera's known bad pixels (setBadPixels) for
  // display, from their good neighbours, before anything else sees them.
  bool badPixels = true;
};

// Stage settings as text, shared by the harness (--pipeline) and the app's debug options: a
// comma-separated list applied on top of the defaults. "default" changes nothing; "shutter" or
// "shutter=0" switches stage 1; "shutterBlend=N" sets its crossfade; "badPixels" / "badPixels=0"
// switches stage 2. False on an unknown item.
bool parseStages(const std::string& text, PipelineOptions* options);
std::string describeStages(const PipelineOptions& options);  // e.g. "shutter(8)", "none"

class Pipeline {
 public:
  explicit Pipeline(const PipelineOptions& options = {});

  void setOptions(const PipelineOptions& options);
  const PipelineOptions& options() const { return options_; }

  // Stage 2's map, for the camera in use (badPixelMapFor its serial).
  void setBadPixels(BadPixelMap map) { badPixels_ = std::move(map); }
  const BadPixelMap& badPixels() const { return badPixels_; }

  // A new stream, replay start or range switch: forget every frame seen so far.
  void reset();

  // One 256x192 camera image in; display intensity in [0, 1] out (kImagePixels floats). signal, if
  // given, receives the value tone mapping started from, in raw counts (for the harness's °C metrics).
  void process(const uint16_t* image, float* display, float* signal = nullptr);

  // The caller has stopped feeding frames through a shutter cycle it knows about (the app discards
  // frames while its own 0x8000 or a lockout runs): the next frame crossfades from the last output,
  // as after a cycle seen in the frames themselves (stage 1 on).
  void hold();

  // True while the camera repeats one frame, or after hold() until the next frame (stage 1 on).
  bool frozen() const { return frozen_; }
  bool blending() const { return blendLeft_ > 0; }

 private:
  PipelineOptions options_;
  BadPixelMap badPixels_;
  std::vector<float> work_;  // the signal, when the caller doesn't ask for it
  std::vector<uint16_t> previous_;
  std::vector<float> held_, heldSignal_;
  bool havePrevious_ = false;
  bool frozen_ = false;
  int blendLeft_ = 0, blendTotal_ = 0;
};

}  // namespace tv
