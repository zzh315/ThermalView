// Single-producer, single-consumer ring of raw frames: the capture callback copies in and returns;
// the processing thread takes every frame in order. It never overwrites: when full, the producer
// counts an overrun and drops the new frame.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

#include "tv/frame.h"

namespace tv {

struct RawFrame {
  alignas(64) std::array<uint16_t, kFramePixels> data;
  uint32_t bytes = 0;      // bytes the source delivered (may differ from kFrameBytes)
  int64_t arrivalNs = 0;   // CLOCK_MONOTONIC when the source handed it over
  uint32_t sequence = 0;   // source sequence number (libuvc frame->sequence); 0 if unknown
  bool replay = false;
};

template <size_t Capacity>
class FrameRing {
  static_assert(Capacity >= 2, "need at least two slots");

 public:
  // Producer side. Returns nullptr (and counts an overrun) when the ring is full.
  RawFrame* beginWrite() {
    const size_t head = head_.load(std::memory_order_relaxed);
    if (head - tail_.load(std::memory_order_acquire) == Capacity) {
      overruns_.fetch_add(1, std::memory_order_relaxed);
      return nullptr;
    }
    return &slots_[head % Capacity];
  }
  void commitWrite() {
    {
      // Publishing under the mutex means a consumer between its check and its wait can't miss
      // the wake-up. Uncontended, this costs nanoseconds.
      std::lock_guard lock(mutex_);
      head_.store(head_.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    }
    cv_.notify_one();
  }

  // Consumer side.
  RawFrame* beginRead() {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return nullptr;
    return &slots_[tail % Capacity];
  }
  void commitRead() { tail_.store(tail_.load(std::memory_order_relaxed) + 1, std::memory_order_release); }

  // Waits until a frame is readable or the timeout passes; returns true if one is readable.
  bool waitReadable(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, timeout, [this] {
      return tail_.load(std::memory_order_relaxed) != head_.load(std::memory_order_acquire);
    });
  }
  void wake() { cv_.notify_all(); }

  // Only while neither side is active.
  void clear() { tail_.store(head_.load()); }

  uint64_t overruns() const { return overruns_.load(std::memory_order_relaxed); }
  size_t size() const { return head_.load() - tail_.load(); }

 private:
  std::array<RawFrame, Capacity> slots_{};
  std::atomic<size_t> head_{0};
  std::atomic<size_t> tail_{0};
  std::atomic<uint64_t> overruns_{0};
  std::mutex mutex_;
  std::condition_variable cv_;
};

}  // namespace tv
