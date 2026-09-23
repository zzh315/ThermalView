// Rolling statistics for the debug overlay and the performance budget (CLAUDE.md conventions).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tv {

// Fixed-capacity window of the most recent samples.
class RollingWindow {
 public:
  explicit RollingWindow(size_t capacity) : buf_(capacity) {}
  void push(double value);
  void clear() { count_ = head_ = 0; }
  size_t size() const { return count_; }
  double mean() const;
  double stddev() const;
  double max() const;
  double percentile(double p) const;  // p in [0, 100]; 0 when empty

 private:
  std::vector<double> buf_;
  size_t head_ = 0;
  size_t count_ = 0;
};

// Frame arrivals: rate, jitter, and the two kinds of drops the overlay reports.
class ArrivalTracker {
 public:
  explicit ArrivalTracker(double nominalIntervalMs = 40.0, size_t window = 250)
      : nominalMs_(nominalIntervalMs), intervals_(window) {}

  // sequence 0 means "unknown" (replay) and skips the sequence-gap check.
  void onFrame(int64_t arrivalNs, uint32_t sequence);
  void reset();

  uint64_t frames() const { return frames_; }
  uint64_t sequenceGaps() const { return sequenceGaps_; }  // frames the callback never saw
  uint64_t arrivalGaps() const { return arrivalGaps_; }    // intervals > 1.5x nominal
  double fps() const;
  double jitterMs() const { return intervals_.stddev(); }
  double maxIntervalMs() const { return intervals_.max(); }
  int64_t lastArrivalNs() const { return lastNs_; }

 private:
  double nominalMs_;
  RollingWindow intervals_;
  uint64_t frames_ = 0;
  uint64_t sequenceGaps_ = 0;
  uint64_t arrivalGaps_ = 0;
  int64_t lastNs_ = 0;
  uint32_t lastSeq_ = 0;
};

}  // namespace tv
