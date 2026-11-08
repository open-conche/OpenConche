// Chocolatier 1's Logo window (TLogo, docs/re/choc1-logo-and-hiscore.md): the canvas maths of Init 00415a90 and
// the composite of FUN_004155f0 (background stretch, graphic placement, the text with its drop passes).
#include <doctest/doctest.h>

#include <map>

#include "ScriptHarness.h"
#include "games/Registry.h"
#include "games/choc1/Logo.h"
#include "gfx/RecordingRenderer.h"
#include "gfx/Texture.h"

using namespace choco;
namespace f1 = choco::game::choc1;

namespace {

gfx::ImageData solid(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
  gfx::ImageData img;
  img.width = w;
  img.height = h;
  img.rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
  for (std::size_t i = 0; i < img.rgba.size(); i += 4)
    img.rgba[i] = r, img.rgba[i + 1] = g, img.rgba[i + 2] = b, img.rgba[i + 3] = a;
  return img;
}

const gfx::ImageData& red() {
  static const gfx::ImageData img = solid(100, 50, 200, 40, 40);
  return img;
}
const gfx::ImageData& blue() {
  static const gfx::ImageData img = solid(200, 200, 40, 60, 220);
  return img;
}

gfx::ImageData renderCanvas(int windowW, const f1::LogoWindow::Keys& k, const f1::LogoWindow::Assets& a) {
  gfx::ImageData canvas;
  canvas.width = f1::LogoWindow::canvasW(windowW);
  canvas.height = f1::LogoWindow::canvasH(windowW);
  canvas.rgba.assign(static_cast<std::size_t>(canvas.width) * canvas.height * 4, 0);
  f1::LogoWindow::render(k, a, canvas);
  return canvas;
}

bool pixelIs(const gfx::ImageData& img, int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  const std::size_t o = (static_cast<std::size_t>(y) * img.width + x) * 4;
  return img.rgba[o + 3] >= 20 && std::abs(img.rgba[o] - r) <= 8 && std::abs(img.rgba[o + 1] - g) <= 8 &&
         std::abs(img.rgba[o + 2] - b) <= 8;
}

int countColor(const gfx::ImageData& canvas, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  int n = 0;
  for (std::size_t i = 0; i < canvas.rgba.size(); i += 4)
    if (canvas.rgba[i + 3] >= 20 && std::abs(canvas.rgba[i] - r) <= 8 && std::abs(canvas.rgba[i + 1] - g) <= 8 &&
        std::abs(canvas.rgba[i + 2] - b) <= 8)
      ++n;
  return n;
}

// A font cache that resolves everything to the built-in font (no asset files in the unit environment).
struct Fonts {
  gfx::text::FontCache cache;
  Fonts() : cache(nullptr) {}
};

f1::LogoWindow::Keys textKeys() {
  f1::LogoWindow::Keys k;
  k.text = "Bob";  // short: the built-in font's blocky glyphs make stable pockets of colour
  k.fontColor = {1, 0, 0, 1}, k.dropColor = {0, 1, 0, 1};
  return k;
}

struct LogoEnv : test::ScriptEnv {
  LogoEnv()
      : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ui/modal"),
        btnFs(std::string(CHOCO_TEST_ASSETS) + "/button", ""), cache(btnFs, nullptr), host(ctx) {
    ctx.module = games::findModule("choc1");
    ctx.module->attach(ctx);
    wm.setTextures(&cache);
    booted = host.boot();
  }
  assets::AssetFS btnFs;
  gfx::TextureCache cache;
  script::ScriptHost host;
  bool booted = false;
};

}  // namespace

TEST_CASE("Logo: the canvas maths of Init 00415a90 (truncation of size*s + 0.5)") {
  CHECK(f1::LogoWindow::canvasW(262) == 262);  // the builder preview of ui/logo.lua
  CHECK(f1::LogoWindow::canvasH(262) == 150);
  CHECK(f1::LogoWindow::canvasW(106) == 106);  // the port sign: PlayerLogo over the factory's 106x60 button
  CHECK(f1::LogoWindow::canvasH(106) == 61);  // 106/350*200 = 60.57; +0.5 truncates to 61
  CHECK(f1::LogoWindow::canvasW(350) == 350);  // the full-size default
  const float s = f1::LogoWindow::scaleOf(262);
  CHECK(s == doctest::Approx(262.0 / 350.0));
  CHECK(f1::LogoWindow::scaled(310, s) == 232);  // textW 310 of ui/logo.lua
  CHECK(f1::LogoWindow::scaled(160, s) == 120);  // textH 160
  CHECK(f1::LogoWindow::scaled(55, s) == 41);     // the default font size (kLogoFontSizeMedium)
}

TEST_CASE("Logo: the background covers the canvas and the graphic follows (gx, gy) scaled") {
  const f1::LogoWindow::Assets empty;
  f1::LogoWindow::Keys k;
  gfx::ImageData canvas = renderCanvas(262, k, empty);
  CHECK(countColor(canvas, 0, 0, 0) == 0);  // transparent

  {
    // No graphic: the red background is stretched over the whole canvas.
    f1::LogoWindow::Assets a;
    a.background = &red();
    canvas = renderCanvas(262, k, a);
    CHECK(countColor(canvas, 200, 40, 40) == 262 * 150);
  }
  {
    // The default pos index 4 hides the graphic: LogoGraphicsPositions[4] = (-800, -800).
    f1::LogoWindow::Assets a;
    a.background = &red();
    a.graphic = &blue();
    k.gx = -800;
    k.gy = -800;
    canvas = renderCanvas(262, k, a);
    CHECK(countColor(canvas, 200, 40, 40) == 262 * 150);           // the blue never lands
    CHECK(countColor(canvas, 40, 60, 220) == 0);
  }
  {
    // pos 1 = (0, 0): the graphic's centre is ((200/2+0)*s, ...), so it covers the left of the canvas: pixel
    // (75, 75) per the placement rule, while the far right stays background.
    f1::LogoWindow::Assets a;
    a.background = &red();
    a.graphic = &blue();
    k.gx = 0;
    k.gy = 0;
    canvas = renderCanvas(262, k, a);
    CHECK(pixelIs(canvas, 75, 75, 40, 60, 220));  // inside the graphic
    CHECK(pixelIs(canvas, 240, 75, 200, 40, 40));  // right of it: still the background
  }
  {
    // gx = 150 (pos 3): the centre moves to ((100+150)*s) = 187, so the left edge of the canvas keeps the
    // background and (240, 75) is now the graphic.
    f1::LogoWindow::Assets a;
    a.background = &red();
    a.graphic = &blue();
    k.gx = 150;
    canvas = renderCanvas(262, k, a);
    CHECK(pixelIs(canvas, 10, 75, 200, 40, 40));
    CHECK(pixelIs(canvas, 240, 75, 40, 60, 220));
  }
}

TEST_CASE("Logo: a small canvas centres the text - the factory sign reading (the user's 24 px report)") {
  // ui/logoinfo.lua's PlayerLogo gives textX=20, textY=20, textW=310, textH=160, and the port's factory sign is a
  // 106-wide Logo (s = 106/350, canvas 106x61). Init OVERWRITES the raw keys with trunc(v*s + 0.5) (the stores at
  // 0x415f84..0x416016, fistp at 0x0c00 = truncate), so the box's origin is ((int)(20*0.3029+0.5), ...) = (6, 6),
  // NOT the raw (20, 20) - a first reading drew the origin unscaled and the sign's text landed bottom-right of the
  // sign (the user's report; the raw reading puts the drawn centre at (67,44) of the canvas).
  Fonts fonts;
  f1::LogoWindow::Keys k = textKeys();
  k.textX = 20;
  k.textY = 20;
  const f1::LogoWindow::Assets a{nullptr, nullptr, &fonts.cache};
  gfx::ImageData sign = renderCanvas(106, k, a);  // the factory sign's 106x61 canvas
  CHECK(sign.width == 106);
  CHECK(sign.height == 61);  // canvasH: trunc(200*106/350 + 0.5) = 61
  // The drawn pixels' bounding box is centred on the canvas, off by at most a quarter of its own size (the flag
  // alignment centres within the box; the raw-(20,20) bug put the centre at (67,44) of (106,60)).
  long minX = sign.width, maxX = -1, minY = sign.height, maxY = -1;
  for (int y = 0; y < sign.height; ++y)
    for (int x = 0; x < sign.width; ++x)
      if (pixelIs(sign, x, y, 255, 0, 0) || pixelIs(sign, x, y, 0, 255, 0)) {
        minX = std::min(minX, static_cast<long>(x));
        maxX = std::max(maxX, static_cast<long>(x));
        minY = std::min(minY, static_cast<long>(y));
        maxY = std::max(maxY, static_cast<long>(y));
      }
  REQUIRE(maxX > minX);
  REQUIRE(maxY > minY);
  const double cx = (minX + maxX) / 2.0, cy = (minY + maxY) / 2.0;
  // Centred on the canvas within 13 px - the raw-reading bug put the centre 14+ px right and down.
  CHECK(cx < 106.0 / 2 + 13);
  CHECK(cx > 106.0 / 2 - 13);
  CHECK(cy < 61.0 / 2 + 13);
  CHECK(cy > 61.0 / 2 - 13);
  // And the origin itself: scaled() of textX at the sign's s is 6 (not the raw 20).
  CHECK(f1::LogoWindow::scaled(20, 106.0f / 350.0f) == 6);
}

TEST_CASE("Logo: the text draws in fontColor with drop passes in dropColor (+2,+2 only when s > 0.5)") {
  Fonts fonts;
  f1::LogoWindow::Keys k = textKeys();

  // The default 350-wide canvas: s = 1, so both drop passes run.
  const f1::LogoWindow::Assets a{nullptr, nullptr, &fonts.cache};
  gfx::ImageData big = renderCanvas(350, k, a);
  CHECK(countColor(big, 255, 0, 0) > 20);  // the fill
  CHECK(countColor(big, 0, 255, 0) > 20);  // the drop

  // s <= 0.5 (a 175-wide canvas) drops the +2,+2 pass: strictly fewer drop pixels than the +1,+1 shared with the
  // large canvas leaves. The fill keeps a comparable count (the same text, a smaller box).
  gfx::ImageData small = renderCanvas(175, k, a);
  CHECK(countColor(small, 255, 0, 0) > 10);
  CHECK(countColor(small, 0, 255, 0) > 10);
  CHECK(countColor(small, 0, 255, 0) < countColor(big, 0, 255, 0));
}

TEST_CASE("Logo: the Lua type reads its keys (TLogo Init) and draws its canvas texture over the rect") {
  LogoEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('m')") == 0);
  REQUIRE(e.run("do local t = { typename = 'Logo', name = 'sign', x = 10, y = 20, w = 262, h = 150, background = 'image/bt_up', "
                "graphic = 'image/bt_down', text = 'Evangeline', flags = 5, textX = 20, textY = 20, textW = 310, "
                "textH = 160, gx = 75, gy = 0, font = 'fonts/bolton.mvec', fontSize = 55, "
                "fontColor = {0.94, 0.77, 0.08, 1}, dropColor = {0.35, 0.24, 0.07, 1} }; "
                "GenericCreate(t); PostCreate(t, 'Logo'); PopWindow() end") == 0);
  auto* logo = dynamic_cast<f1::LogoWindow*>(e.wm.topModal()->find("sign"));
  REQUIRE(logo != nullptr);
  CHECK(logo->rect() == gfx::RectI{10, 20, 262, 150});
  const f1::LogoWindow::Keys& k = logo->keys();
  CHECK(k.background == "image/bt_up");
  CHECK(k.graphic == "image/bt_down");
  CHECK(k.text == "Evangeline");
  CHECK(k.flags == 5);
  CHECK(k.textX == 20);
  CHECK(k.textW == 310);
  CHECK(k.gx == doctest::Approx(75));
  CHECK(k.fontSize == 55);
  CHECK(k.fontColor.r == doctest::Approx(0.94f));
  CHECK(k.dropColor.b == doctest::Approx(0.07f));

  // The canvas becomes one texture drawn over the window rect (the exe's Draw 00415410).
  gfx::RecordingRenderer rr;
  e.wm.draw(rr);
  int blits = 0;
  for (const auto& c : rr.calls())
    if (c.kind == gfx::RecordingRenderer::Call::Kind::DrawTexture && c.texture) {
      ++blits;
      CHECK(c.dst.x == doctest::Approx(10));
      CHECK(c.dst.w == doctest::Approx(262));
      CHECK(c.dst.h == doctest::Approx(150));
      CHECK(c.texture->width() == 262);  // the canvas: canvasW(262) x canvasH(262)
      CHECK(c.texture->height() == 150);
    }
  CHECK(blits == 1);
}
