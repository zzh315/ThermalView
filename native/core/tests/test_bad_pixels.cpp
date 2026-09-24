#include <vector>

#include "doctest.h"
#include "tv/bad_pixels.h"
#include "tv/pipeline.h"

TEST_CASE("KA1213's map holds its two drifting edge pixels; unknown units get none") {
  const tv::BadPixelMap map = tv::badPixelMapFor("KA1213");
  REQUIRE(map.pixels.size() == 2);
  CHECK(map.mask[115 * tv::kFrameWidth + 2]);
  CHECK(map.mask[115 * tv::kFrameWidth + 3]);
  CHECK(tv::badPixelMapFor("other").empty());
}

TEST_CASE("a bad pixel takes the median of its good neighbours; a bad neighbour is skipped") {
  const tv::BadPixelMap map = tv::badPixelMapFor("KA1213");
  std::vector<float> s(tv::kImagePixels, 100.0f);
  s[115 * tv::kFrameWidth + 2] = 14.0f;   // drifted cold
  s[115 * tv::kFrameWidth + 3] = 57.0f;   // its drifting neighbour, not a valid source
  s[114 * tv::kFrameWidth + 1] = 104.0f;  // one warmer good neighbour
  tv::replaceBadPixels(map, s.data());
  CHECK(s[115 * tv::kFrameWidth + 2] == 100.0f);
  CHECK(s[115 * tv::kFrameWidth + 3] == 100.0f);
}

TEST_CASE("stage 2 in the pipeline changes only the mapped pixels' display, and only when on") {
  std::vector<uint16_t> img(tv::kImagePixels);
  for (size_t i = 0; i < img.size(); ++i) img[i] = uint16_t(5000 + (i * 7) % 13);
  img[115 * tv::kFrameWidth + 2] = 4900;  // a cold dot that would stretch the whole image
  tv::PipelineOptions o, none;
  o.badPixels = true;
  o.destripe = none.destripe = false;  // stage 3b would also touch these pixels
  none.badPixels = false;
  tv::Pipeline on(o), off(none);
  on.setBadPixels(tv::badPixelMapFor("KA1213"));
  off.setBadPixels(tv::badPixelMapFor("KA1213"));
  std::vector<float> a(tv::kImagePixels), b(tv::kImagePixels), sig(tv::kImagePixels);
  on.process(img.data(), a.data(), sig.data());
  off.process(img.data(), b.data());
  CHECK(sig[115 * tv::kFrameWidth + 2] >= 5000.0f);  // replaced from its neighbours
  CHECK(b[115 * tv::kFrameWidth + 2] == 0.0f);      // off: the dot is the image minimum
  CHECK(a[115 * tv::kFrameWidth + 2] > 0.0f);
}
