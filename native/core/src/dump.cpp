#include "tv/dump.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace tv {

std::string jsonEscape(const std::string& text) {
  std::string out;
  for (unsigned char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += char(c);
        }
    }
  }
  return out;
}

bool writeDump(const std::string& base, const std::vector<uint16_t>& frames, const DumpInfo& info,
               std::string* error) {
  const size_t count = info.timestampsNs.size();
  if (frames.size() != count * kFramePixels) {
    if (error) *error = "frame buffer size does not match the timestamp count";
    return false;
  }
  {
    std::ofstream raw(base + ".raw", std::ios::binary | std::ios::trunc);
    raw.write(reinterpret_cast<const char*>(frames.data()), std::streamsize(frames.size() * 2));
    if (!raw) {
      if (error) *error = "cannot write " + base + ".raw";
      return false;
    }
  }
  std::ostringstream j;
  const auto str = [](const std::string& s) { return "\"" + jsonEscape(s) + "\""; };
  j << "{\n";
  j << "  \"format\": \"thermalview-dump/1\",\n";
  j << "  \"width\": " << kFrameWidth << ",\n";
  j << "  \"height\": " << kFrameRows << ",\n";
  j << "  \"metadata_rows\": " << kMetaRows << ",\n";
  j << "  \"bytes_per_frame\": " << kFrameBytes << ",\n";
  j << "  \"frame_count\": " << count << ",\n";
  j << "  \"wall_clock_start\": " << str(info.wallClockStart) << ",\n";
  j << "  \"source\": " << str(info.source) << ",\n";
  j << "  \"start_order\": " << str(info.startOrder) << ",\n";
  j << "  \"range\": " << str(info.range.empty() ? "normal" : info.range) << ",\n";
  j << "  \"app_version\": " << str(info.appVersion) << ",\n";
  j << "  \"device\": {\"manufacturer\": " << str(info.manufacturer)
    << ", \"product\": " << str(info.product) << ", \"serial\": " << str(info.serial)
    << ", \"firmware\": " << str(info.firmware) << "},\n";
  j << "  \"commands\": [";
  for (size_t i = 0; i < info.commandLog.size(); ++i) j << (i ? ", " : "") << str(info.commandLog[i]);
  j << "],\n";
  j << "  \"sequence\": [";
  for (size_t i = 0; i < info.sequence.size(); ++i) j << (i ? ", " : "") << info.sequence[i];
  j << "],\n";
  j << "  \"timestamps_ns\": [";
  for (size_t i = 0; i < count; ++i) j << (i ? ", " : "") << info.timestampsNs[i];
  j << "]\n}\n";

  std::ofstream json(base + ".json", std::ios::trunc);
  json << j.str();
  if (!json) {
    if (error) *error = "cannot write " + base + ".json";
    return false;
  }
  return true;
}

std::vector<int64_t> parseJsonIntArray(const std::string& json, const std::string& key) {
  std::vector<int64_t> out;
  const size_t k = json.find("\"" + key + "\"");
  if (k == std::string::npos) return out;
  const size_t open = json.find('[', k);
  const size_t close = open == std::string::npos ? open : json.find(']', open);
  if (close == std::string::npos) return out;
  const char* p = json.c_str() + open + 1;
  const char* end = json.c_str() + close;
  while (p < end) {
    char* next = nullptr;
    const long long v = std::strtoll(p, &next, 10);
    if (next == p) {
      ++p;  // skip separators and whitespace
      continue;
    }
    out.push_back(v);
    p = next;
  }
  return out;
}

bool loadDump(const std::string& base, LoadedDump* out, std::string* error) {
  std::ifstream raw(base + ".raw", std::ios::binary | std::ios::ate);
  if (!raw) {
    if (error) *error = "cannot open " + base + ".raw";
    return false;
  }
  const std::streamsize bytes = raw.tellg();
  if (bytes <= 0 || bytes % std::streamsize(kFrameBytes) != 0) {
    if (error) *error = base + ".raw is not a whole number of 256x196 frames";
    return false;
  }
  out->frameCount = size_t(bytes) / kFrameBytes;
  out->frames.resize(out->frameCount * kFramePixels);
  raw.seekg(0);
  raw.read(reinterpret_cast<char*>(out->frames.data()), bytes);
  if (!raw) {
    if (error) *error = "cannot read " + base + ".raw";
    return false;
  }
  std::ifstream json(base + ".json");
  if (json) {
    std::stringstream s;
    s << json.rdbuf();
    out->timestampsNs = parseJsonIntArray(s.str(), "timestamps_ns");
    if (out->timestampsNs.size() != out->frameCount) out->timestampsNs.clear();
    const std::string text = s.str(), key = "\"serial\": \"";
    const size_t k = text.find(key);
    if (k != std::string::npos) {
      const size_t from = k + key.size(), to = text.find('"', from);
      if (to != std::string::npos) out->serial = text.substr(from, to - from);
    }
  }
  return true;
}

}  // namespace tv
