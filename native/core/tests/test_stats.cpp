#include "doctest.h"
#include "tv/stats.h"

TEST_CASE("rolling window percentiles, mean and max") {
  tv::RollingWindow w(5);
  for (double v : {1.0, 2.0, 3.0, 4.0, 5.0}) w.push(v);
  CHECK(w.mean() == doctest::Approx(3.0));
  CHECK(w.percentile(50) == doctest::Approx(3.0));
  CHECK(w.percentile(95) == doctest::Approx(4.8));
  CHECK(w.max() == doctest::Approx(5.0));
  w.push(10.0);  // evicts 1.0
  CHECK(w.size() == 5);
  CHECK(w.max() == doctest::Approx(10.0));
  CHECK(w.percentile(0) == doctest::Approx(2.0));
}

TEST_CASE("arrival tracker: rate, sequence gaps, arrival gaps") {
  tv::ArrivalTracker t(40.0);
  int64_t ns = 1'000'000'000;
  uint32_t seq = 10;
  for (int i = 0; i < 50; ++i) {
    t.onFrame(ns, seq++);
    ns += 40'000'000;
  }
  CHECK(t.fps() == doctest::Approx(25.0));
  CHECK(t.sequenceGaps() == 0);
  CHECK(t.arrivalGaps() == 0);
  seq += 2;               // the callback missed two frames
  ns += 40'000'000;       // and one interval came late on the bus
  t.onFrame(ns, seq);
  CHECK(t.sequenceGaps() == 2);
  CHECK(t.arrivalGaps() == 1);
}
