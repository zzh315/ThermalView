#include "tv/drift.h"

#include <cstring>
#include <fstream>
#include <iterator>

#include "tv/frame.h"

namespace tv {

DriftMap driftMapFromBytes(const void* data, size_t bytes) {
  DriftMap map;
  if (!data || bytes != kImagePixels * sizeof(float)) return map;
  map.rate.resize(kImagePixels);
  std::memcpy(map.rate.data(), data, bytes);  // little-endian float32, as on every target we build for
  return map;
}

DriftMap loadDriftMap(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return driftMapFromBytes(bytes.data(), bytes.size());
}

}  // namespace tv
