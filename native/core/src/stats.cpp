#include "tv/stats.h"

#include <algorithm>
#include <cmath>

namespace tv {

void RollingWindow::push(double value) {
  if (buf_.empty()) return;
  buf_[head_] = value;
  head_ = (head_ + 1) % buf_.size();
  count_ = std::min(count_ + 1, buf_.size());
}

double RollingWindow::mean() const {
  if (!count_) return 0;
  double sum = 0;
  for (size_t i = 0; i < count_; ++i) sum += buf_[i];
  return sum / double(count_);
}

double RollingWindow::stddev() const {
  if (count_ < 2) return 0;
  const double m = mean();
  double sum = 0;
  for (size_t i = 0; i < count_; ++i) sum += (buf_[i] - m) * (buf_[i] - m);
  return std::sqrt(sum / double(count_ - 1));
}

double RollingWindow::max() const {
  if (!count_) return 0;
  return *std::max_element(buf_.begin(), buf_.begin() + long(count_));
}

double RollingWindow::percentile(double p) const {
  if (!count_) return 0;
  std::vector<double> v(buf_.begin(), buf_.begin() + long(count_));
  const double rank = std::clamp(p, 0.0, 100.0) / 100.0 * double(count_ - 1);
  const size_t lo = size_t(std::floor(rank));
  std::nth_element(v.begin(), v.begin() + long(lo), v.end());
  const double low = v[lo];
  if (lo + 1 >= count_) return low;
  const double high = *std::min_element(v.begin() + long(lo) + 1, v.end());
  return low + (high - low) * (rank - double(lo));
}

void ArrivalTracker::onFrame(int64_t arrivalNs, uint32_t sequence) {
  if (frames_ > 0) {
    const double ms = double(arrivalNs - lastNs_) / 1e6;
    intervals_.push(ms);
    if (ms > 1.5 * nominalMs_) ++arrivalGaps_;
    if (sequence && lastSeq_ && sequence > lastSeq_ + 1) sequenceGaps_ += sequence - lastSeq_ - 1;
  }
  ++frames_;
  lastNs_ = arrivalNs;
  lastSeq_ = sequence;
}

void ArrivalTracker::reset() {
  intervals_.clear();
  frames_ = sequenceGaps_ = arrivalGaps_ = 0;
  lastNs_ = 0;
  lastSeq_ = 0;
}

double ArrivalTracker::fps() const {
  const double m = intervals_.mean();
  return m > 0 ? 1000.0 / m : 0;
}

}  // namespace tv
