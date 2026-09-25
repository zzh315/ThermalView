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

tv::PipelineOptions allOff() {
  tv::PipelineOptions o;
  o.shutterHold = false;
  o.badPixels = false;
  o.drift = false;
  o.destripe = false;
  o.denoise = false;
  o.nr = false;
  o.tone = false;
  o.detail = false;
  return o;
}

}  // namespace

TEST_CASE("with every stage off the pipeline equals the baseline") {
  tv::Pipeline p(allOff());
  std::vector<float> out(tv::kImagePixels), ref(tv::kImagePixels), sig(tv::kImagePixels);
  for (uint16_t k = 0; k < 3; ++k) {
    const auto img = image(5000, k);
    p.process(img.data(), out.data(), sig.data());
    tv::renderBaseline(img.data(), ref.data());
    CHECK(out == ref);
    CHECK(sig[7] == float(img[7]));
  }
}

TEST_CASE("stage 1 holds through repeated frames and the first fresh one, then cuts to the next") {
  tv::PipelineOptions o = allOff();
  o.shutterHold = true;  // the defaults: no crossfade (owner, 2026-09-26: no afterimages), one frame skipped
  CHECK(o.shutterBlendFrames == 0);
  CHECK(o.shutterSkipFrames == 1);
  tv::Pipeline p(o);
  std::vector<float> out(tv::kImagePixels), held(tv::kImagePixels), live(tv::kImagePixels);

  const auto before = image(5000, 1);
  p.process(before.data(), out.data());
  held = out;
  for (int k = 0; k < 30; ++k) {  // the shutter cycle
    p.process(before.data(), out.data());
    CHECK(p.frozen());
    CHECK(out == held);
  }
  const auto streaky = image(4000, 2);  // the camera's first fresh frame: held over
  p.process(streaky.data(), out.data());
  CHECK(p.frozen());
  CHECK(out == held);
  const auto fresh = image(4960, 3);  // then straight to the next one, nothing of the held image left
  p.process(fresh.data(), out.data());
  CHECK_FALSE(p.frozen());
  CHECK_FALSE(p.blending());
  tv::renderBaseline(fresh.data(), live.data());
  CHECK(out == live);
}

TEST_CASE("stage 1's crossfade, if asked for: the held output fades out over its frames") {
  tv::PipelineOptions o = allOff();
  o.shutterHold = true;
  o.shutterBlendFrames = 4;
  o.shutterSkipFrames = 0;
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

TEST_CASE("the defaults are the approved stages") {
  const tv::PipelineOptions o;
  CHECK(o.shutterHold);
  CHECK(o.badPixels);
  CHECK(o.drift);
  CHECK(o.destripe);
  CHECK_FALSE(o.denoise);  // stage 4 removed (owner, 2026-09-25: after-images on camera motion)
  CHECK(o.nr);              // stage 4b as approved (2026-09-25): 11x11 search, 5x5 patches, the app's Low
  CHECK(o.nrSearch == 5);
  CHECK(o.nrPatch == 2);
  CHECK(o.nrStrength == doctest::Approx(0.8f));
  CHECK(o.tone);
  CHECK(o.detail);  // stage 6 as approved (2026-09-25): the mid-scale texture layer at x1.5 alone
  CHECK(o.detailMidGain == doctest::Approx(1.5f));
  CHECK(o.detailGain == doctest::Approx(1.0f));
  CHECK(o.unsharpAmount == 0.0f);
}

TEST_CASE("stage 1 off: repeated frames pass straight through") {
  tv::Pipeline p(allOff());
  std::vector<float> a(tv::kImagePixels), b(tv::kImagePixels);
  const auto img = image(5000, 3);
  p.process(img.data(), a.data());
  p.process(img.data(), b.data());
  CHECK_FALSE(p.frozen());
  CHECK(a == b);
}

TEST_CASE("reset forgets the previous frame, so a repeat right after it isn't a freeze") {
  tv::PipelineOptions o = allOff();
  o.shutterHold = true;
  tv::Pipeline p(o);
  std::vector<float> out(tv::kImagePixels);
  const auto img = image(5000, 4);
  p.process(img.data(), out.data());
  p.reset();
  p.process(img.data(), out.data());
  CHECK_FALSE(p.frozen());
}

TEST_CASE("hold(): the next frame crossfades from the last output, as after a cycle in the frames") {
  tv::PipelineOptions o = allOff();
  o.shutterHold = true;
  o.shutterBlendFrames = 2;  // (hold() skips nothing: the app has already waited out fresh frames)
  tv::Pipeline p(o);
  std::vector<float> out(tv::kImagePixels), held(tv::kImagePixels), live(tv::kImagePixels);
  const auto a = image(5000, 20), b = image(4900, 21);
  p.process(a.data(), out.data());
  held = out;
  p.hold();  // the app discarded the frames of its own 0x8000
  CHECK(p.frozen());
  p.process(b.data(), out.data());
  tv::renderBaseline(b.data(), live.data());
  CHECK(out[99] == doctest::Approx(live[99] + (2.0f / 3.0f) * (held[99] - live[99])).epsilon(1e-5));
  CHECK(p.blending());
}

TEST_CASE("stage settings as text") {
  tv::PipelineOptions o;
  CHECK(tv::parseStages("default", &o));
  CHECK(o.shutterHold);
  CHECK(tv::parseStages("shutter=0,badPixels=0,drift=0,destripe=0,denoise=0,nr=0,tone=0", &o));
  CHECK_FALSE(o.shutterHold);
  CHECK(tv::describeStages(o) == "none");
  CHECK(tv::parseStages("shutter,shutterBlend=12", &o));
  CHECK(tv::describeStages(o) == "shutter(12,skip1)");
  CHECK(tv::parseStages("badPixels", &o));
  CHECK(tv::describeStages(o) == "shutter(12,skip1),badPixels");
  CHECK(tv::parseStages("drift,destripe", &o));
  CHECK(tv::describeStages(o) == "shutter(12,skip1),drift(x0.90),badPixels,destripe");
  CHECK(tv::parseStages("denoise", &o));
  CHECK(tv::describeStages(o) == "shutter(12,skip1),drift(x0.90),badPixels,destripe,denoise(k0.25)");
  CHECK(tv::parseStages("denoise=0,nr", &o));
  CHECK(tv::describeStages(o) == "shutter(12,skip1),drift(x0.90),badPixels,destripe,nr(h0.80)");
  CHECK_FALSE(tv::parseStages("bogus", &o));
}

TEST_CASE("stage 5 measures only the region, and a new region takes over within ~0.3 s") {
  constexpr int W = tv::kFrameWidth, H = tv::kImageRows;
  std::vector<uint16_t> img(tv::kImagePixels);
  uint32_t seed = 9;
  bool hot = false;  // a clipped patch in the top-left corner
  auto frame = [&]() {  // two ramps, 0-200 counts each (the left cool, the right warm), a count of noise
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        seed = seed * 1664525u + 1013904223u;
        const int noise = int(seed >> 30);  // 0..3: frames never repeat, so stage 1 never holds
        img[size_t(y) * W + x] = hot && x < 20 && y < 20 ? uint16_t(14000)
                                                         : uint16_t((x < 128 ? 5000 : 5400) + (x % 128) * 200 / 127 + noise);
      }
    return img.data();
  };
  tv::PipelineOptions o;
  o.denoise = false;
  tv::Pipeline p(o);
  std::vector<float> d(tv::kImagePixels);
  auto spread = [&](int x0, int x1) {  // the display range a band of columns uses (row 96)
    float lo = 1, hi = 0;
    for (int x = x0; x < x1; ++x) {
      lo = std::min(lo, d[size_t(96) * W + x]);
      hi = std::max(hi, d[size_t(96) * W + x]);
    }
    return hi - lo;
  };
  for (int k = 0; k < 50; ++k) p.process(frame(), d.data());
  const float whole = spread(0, 128);
  p.setRegion({0, 0, 128, H});  // the cool half only
  for (int k = 0; k < 8; ++k) p.process(frame(), d.data());  // 0.32 s
  const float soon = spread(0, 128);
  for (int k = 0; k < 100; ++k) p.process(frame(), d.data());
  const float settled = spread(0, 128);
  CHECK(settled > 1.5f * whole);  // the region's range now fills the output
  CHECK(soon > 0.9f * settled);   // and it got there within ~0.3 s
  CHECK(d[size_t(96) * W + 200] >= 0.96f);  // outside, the warmer half clips to the top
  // A region with nothing measurable (all at the camera's clip) keeps the mapping it had.
  hot = true;
  p.process(frame(), d.data());
  const float before = d[size_t(96) * W + 60];
  p.setRegion({0, 0, 20, 20});
  for (int k = 0; k < 10; ++k) p.process(frame(), d.data());
  CHECK(d[size_t(96) * W + 60] == doctest::Approx(before).epsilon(0.03));
}
