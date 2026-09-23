#include <memory>
#include <thread>

#include "doctest.h"
#include "tv/frame_ring.h"
#include "tv/triple_buffer.h"

using namespace std::chrono_literals;

TEST_CASE("the frame ring keeps order and counts overruns instead of overwriting") {
  auto ring = std::make_unique<tv::FrameRing<4>>();
  for (uint32_t i = 1; i <= 4; ++i) {
    auto* slot = ring->beginWrite();
    REQUIRE(slot);
    slot->sequence = i;
    ring->commitWrite();
  }
  CHECK(ring->beginWrite() == nullptr);
  CHECK(ring->overruns() == 1);
  for (uint32_t i = 1; i <= 4; ++i) {
    auto* slot = ring->beginRead();
    REQUIRE(slot);
    CHECK(slot->sequence == i);
    ring->commitRead();
  }
  CHECK(ring->beginRead() == nullptr);
}

TEST_CASE("waitReadable times out when empty and wakes when a frame arrives") {
  auto ring = std::make_unique<tv::FrameRing<4>>();
  CHECK_FALSE(ring->waitReadable(10ms));
  std::thread producer([&] {
    std::this_thread::sleep_for(20ms);
    ring->beginWrite()->sequence = 7;
    ring->commitWrite();
  });
  CHECK(ring->waitReadable(2000ms));
  CHECK(ring->beginRead()->sequence == 7);
  producer.join();
}

TEST_CASE("the triple buffer hands over the latest value only") {
  tv::TripleBuffer<int> tb;
  CHECK_FALSE(tb.acquire());
  tb.writeSlot() = 1;
  tb.publish();
  tb.writeSlot() = 2;
  tb.publish();
  CHECK(tb.acquire());
  CHECK(tb.readSlot() == 2);
  CHECK_FALSE(tb.acquire());
  tb.writeSlot() = 3;
  tb.publish();
  CHECK(tb.acquire());
  CHECK(tb.readSlot() == 3);
}

TEST_CASE("the triple buffer survives a concurrent producer") {
  tv::TripleBuffer<int> tb;
  std::thread producer([&] {
    for (int i = 1; i <= 100000; ++i) {
      tb.writeSlot() = i;
      tb.publish();
    }
  });
  int last = 0;
  while (last < 100000) {
    if (tb.acquire()) {
      CHECK(tb.readSlot() >= last);  // never goes backwards
      last = tb.readSlot();
    }
  }
  producer.join();
}
