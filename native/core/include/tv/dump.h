// Raw frame dumps (docs/PLAN.md M1): <name>.raw holds consecutive little-endian uint16 frames
// of the full 256×196 (metadata included); <name>.json describes them.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tv/frame.h"

namespace tv {

struct DumpInfo {
  std::vector<int64_t> timestampsNs;  // CLOCK_MONOTONIC arrival time of each frame
  std::vector<uint32_t> sequence;     // source sequence numbers
  std::string wallClockStart;         // ISO 8601
  std::string manufacturer, product, serial, firmware;
  std::string appVersion;
  std::string source;      // "camera" or "replay"
  std::string startOrder;  // "stream-first" or "fallback"
  std::string range;       // "normal" or "high" (the range the frames were captured in)
  std::vector<std::string> commandLog;
};

// Writes base + ".raw" and base + ".json". frames holds info.timestampsNs.size() frames.
bool writeDump(const std::string& base, const std::vector<uint16_t>& frames, const DumpInfo& info,
               std::string* error);

struct LoadedDump {
  std::vector<uint16_t> frames;       // frameCount × kFramePixels
  size_t frameCount = 0;
  std::vector<int64_t> timestampsNs;  // from the sidecar; empty if it was missing or unreadable
};

// Loads base + ".raw" and, if present, the timestamps from base + ".json".
bool loadDump(const std::string& base, LoadedDump* out, std::string* error);

// The integers of a top-level JSON array field, e.g. "timestamps_ns": [1, 2, 3]. Only for the
// sidecars writeDump produces.
std::vector<int64_t> parseJsonIntArray(const std::string& json, const std::string& key);

std::string jsonEscape(const std::string& text);

}  // namespace tv
