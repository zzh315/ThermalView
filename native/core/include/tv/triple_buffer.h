// Latest-value hand-off between one producer and one consumer: the producer never waits, the
// consumer always gets the newest published value, and intermediate values may be skipped.
#pragma once

#include <atomic>
#include <cstdint>

namespace tv {

template <class T>
class TripleBuffer {
 public:
  // Producer: fill writeSlot(), then publish().
  T& writeSlot() { return slots_[write_]; }
  void publish() {
    const uint8_t previous = middle_.exchange(uint8_t(write_ | kFresh), std::memory_order_acq_rel);
    write_ = previous & kIndexMask;
  }

  // Consumer: returns true (and makes readSlot() the newest value) if something new was published.
  bool acquire() {
    if (!(middle_.load(std::memory_order_acquire) & kFresh)) return false;
    const uint8_t previous = middle_.exchange(read_, std::memory_order_acq_rel);
    read_ = previous & kIndexMask;
    return true;
  }
  const T& readSlot() const { return slots_[read_]; }
  bool fresh() const { return middle_.load(std::memory_order_acquire) & kFresh; }

 private:
  static constexpr uint8_t kFresh = 0x4;
  static constexpr uint8_t kIndexMask = 0x3;
  T slots_[3]{};
  uint8_t write_ = 0;
  uint8_t read_ = 1;
  std::atomic<uint8_t> middle_{2};
};

}  // namespace tv
