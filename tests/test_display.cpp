#include <doctest/doctest.h>

#include "platform/DisplayMode.h"
#include "platform/Window.h"

using choco::platform::chooseScale;

TEST_CASE("scale fallback") {
  CHECK(chooseScale(2, 2560, 1440) == 2);
  CHECK(chooseScale(2, 1920, 1080) == 1);  // 1200 tall does not fit 1080
  CHECK(chooseScale(2, 1600, 1200) == 2);
  CHECK(chooseScale(2, 1599, 1200) == 1);
  CHECK(chooseScale(1, 1920, 1080) == 1);
  CHECK(chooseScale(2, 640, 480) == 1);  // never below 1
  CHECK(chooseScale(0, 4000, 4000) == 1);
  CHECK(chooseScale(1.5, 1920, 1080) == 1.5);  // 1200x900 fits
  CHECK(chooseScale(1.5, 1100, 1080) == 1);    // 1200 wide does not: the whole number below
  CHECK(chooseScale(3.5, 2560, 1440) == 2);    // 3.5 and 3 do not fit 1440 tall, 2 does
  CHECK(choco::platform::scaledWindowSize(800, 1.5) == 1200);
  CHECK(choco::platform::scaledWindowSize(600, 1.25) == 750);
  CHECK(choco::platform::scaledWindowSize(800, 1.37) == 1096);
}

TEST_CASE("the cursor colour key: pure magenta becomes transparent, everything else stays") {
  // The shipped cursor pictures (cursor/cursor, cursor/gun_aimer?simple, image/cursor, image/gun_aimer) are the only
  // pfp art without a tRNS chunk: 8-bit paletted PNGs whose transparency is the (255,0,255) key.
  choco::gfx::ImageData img;
  img.width = 3;
  img.height = 1;
  img.rgba = {
      255, 0, 255, 255,  // magenta: the key
      255, 0, 254, 200,  // near-magenta: not the key (the art's anti-aliased edges keep their own alpha)
      10, 20, 30, 255,   // ordinary art
  };
  CHECK(choco::platform::Window::applyCursorColourKey(img));
  CHECK(img.rgba[0] == 255);  // the magenta pixel keeps its colour, only the alpha zeroes
  CHECK(img.rgba[3] == 0);
  CHECK(img.rgba[4] == 255);  // the near-magenta pixel is untouched (rgb and alpha)
  CHECK(img.rgba[5] == 0);
  CHECK(img.rgba[7] == 200);
  CHECK(img.rgba[8] == 10);  // ordinary art
  CHECK(img.rgba[9] == 20);
  CHECK(img.rgba[11] == 255);
  choco::gfx::ImageData empty;
  CHECK_FALSE(choco::platform::Window::applyCursorColourKey(empty));  // a broken image: no key, no crash
}
