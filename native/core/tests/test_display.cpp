#include <vector>

#include "doctest.h"
#include "tv/display.h"

TEST_CASE("the baseline stretches each frame's min..max to 0..1") {
  std::vector<uint16_t> img(tv::kImagePixels, 5000);
  img[0] = 4000;
  img[1] = 6000;
  std::vector<float> out(tv::kImagePixels);
  tv::renderBaseline(img.data(), out.data());
  CHECK(out[0] == 0.0f);
  CHECK(out[1] == 1.0f);
  CHECK(out[2] == doctest::Approx(0.5f));
}

TEST_CASE("a flat frame renders black, like the shader's zero scale") {
  std::vector<uint16_t> img(tv::kImagePixels, 5000);
  std::vector<float> out(tv::kImagePixels, 1.0f);
  tv::renderBaseline(img.data(), out.data());
  CHECK(out[0] == 0.0f);
  CHECK(out[tv::kImagePixels - 1] == 0.0f);
}
