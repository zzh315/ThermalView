#include <vector>

#include "doctest.h"
#include "tv/display.h"
#include "tv/pipeline.h"

namespace {

std::vector<uint16_t> image(uint16_t base, uint16_t seed) {
  std::vector<uint16_t> img(tv::kImagePixels);
  uint32_t s = seed;
  for (auto& v : img) {
    s = s * 1664525u + 1013904223u;
    v = uint16_t(base + (s >> 28));  // 0..15 counts of "noise", different per seed
  }
  return img;
}

}  // namespace

TEST_CASE("with every stage off the pipeline equals the baseline") {
  tv::Pipeline p;
  std::vector<float> out(tv::kImagePixels), ref(tv::kImagePixels), sig(tv::kImagePixels);
  for (uint16_t k = 0; k < 3; ++k) {
    const auto img = image(5000, k);
    p.process(img.data(), out.data(), sig.data());
    tv::renderBaseline(img.data(), ref.data());
    CHECK(out == ref);
    CHECK(sig[7] == float(img[7]));
  }
}

TEST_CASE("stage 1 holds the last output through repeated frames, then crossfades back") {
  tv::PipelineOptions o;
  o.shutterHold = true;
  o.shutterBlendFrames = 4;
  tv::Pipeline p(o);
  std::vector<float> out(tv::kImagePixels), held(tv::kImagePixels), live(tv::kImagePixels);

  const auto before = image(5000, 1);
  p.process(before.data(), out.data());
  held = out;
  CHECK_FALSE(p.frozen());

  for (int k = 0; k < 30; ++k) {  // the shutter cycle: one image repeated
    p.process(before.data(), out.data());
    CHECK(p.frozen());
    CHECK(out == held);
  }

  // Fresh frames: the held output's weight goes 4/5, 3/5, 2/5, 1/5, then 0.
  for (int k = 0; k < 6; ++k) {
    const auto fresh = image(4960, uint16_t(10 + k));
    p.process(fresh.data(), out.data());
    CHECK_FALSE(p.frozen());
    tv::renderBaseline(fresh.data(), live.data());
    const float w = k < 4 ? float(4 - k) / 5.0f : 0.0f;
    for (size_t i : {size_t(0), size_t(1234), tv::kImagePixels - 1})
      CHECK(out[i] == doctest::Approx(live[i] + w * (held[i] - live[i])).epsilon(1e-5));
    held = out;  // the next frame blends from what was shown
  }
}

TEST_CASE("stage 1 off: repeated frames pass straight through") {
  tv::Pipeline p;
  std::vector<float> a(tv::kImagePixels), b(tv::kImagePixels);
  const auto img = image(5000, 3);
  p.process(img.data(), a.data());
  p.process(img.data(), b.data());
  CHECK_FALSE(p.frozen());
  CHECK(a == b);
}

TEST_CASE("reset forgets the previous frame, so a repeat right after it isn't a freeze") {
  tv::PipelineOptions o;
  o.shutterHold = true;
  tv::Pipeline p(o);
  std::vector<float> out(tv::kImagePixels);
  const auto img = image(5000, 4);
  p.process(img.data(), out.data());
  p.reset();
  p.process(img.data(), out.data());
  CHECK_FALSE(p.frozen());
}
