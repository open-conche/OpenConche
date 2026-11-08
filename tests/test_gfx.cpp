#include <doctest/doctest.h>
#include "platform/Png.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include "assets/AssetFS.h"
#include "gfx/ClipStack.h"
#include "gfx/HitMask.h"
#include "gfx/Quad.h"
#include "gfx/RecordingRenderer.h"
#include "gfx/TextureCache.h"
#include "platform/Platform.h"
#include "platform/SdlRenderer.h"
#include "platform/Window.h"

using namespace choco::gfx;
namespace fs = std::filesystem;

namespace {

fs::path makeTemp(const char* name) {
  fs::path p = fs::temp_directory_path() / (std::string("choco_gfx_") + name);
  fs::remove_all(p);
  fs::create_directories(p);
  return p;
}

void writePng(const fs::path& p, int w, int h, unsigned char r, unsigned char g, unsigned char b) {
  fs::create_directories(p.parent_path());
  std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 4);
  for (std::size_t i = 0; i < px.size(); i += 4) {
    px[i] = r;
    px[i + 1] = g;
    px[i + 2] = b;
    px[i + 3] = 255;
  }
  REQUIRE(choco::platform::writePng(p.string(), w, h, px.data()));
}

void writeJpg(const fs::path& p, int w, int h) {
  fs::create_directories(p.parent_path());
  // A committed solid-grey fixture at the exact size: nothing in-tree writes JPEG.
  const fs::path fixture = fs::path(CHOCO_TEST_ASSETS) / "gfx" / "image" /
                           ("grey_" + std::to_string(w) + "x" + std::to_string(h) + ".jpg");
  REQUIRE(fs::is_regular_file(fixture));
  REQUIRE(fs::copy_file(fixture, p));
}

struct FakeUploader : TextureUploader {
  int uploads = 0, releases = 0;
  int lastW = 0, lastH = 0;
  std::set<void*> live;
  void* upload(const std::uint8_t*, int w, int h) override {
    ++uploads;
    lastW = w;
    lastH = h;
    void* handle = new int(uploads);
    live.insert(handle);
    return handle;
  }
  void release(void* handle) override {
    ++releases;
    live.erase(handle);
    delete static_cast<int*>(handle);
  }
};

}  // namespace

TEST_CASE("TextureCache probes .png before .jpg and honours explicit extensions") {
  auto dir = makeTemp("probe");
  writeJpg(dir / "image/both.jpg", 8, 8);
  writePng(dir / "image/both.png", 4, 4, 255, 0, 0);
  writeJpg(dir / "image/only.jpg", 6, 6);
  choco::assets::AssetFS afs(dir.string(), "");
  FakeUploader up;
  TextureCache cache(afs, &up);

  auto both = cache.load("image/both");
  REQUIRE(both);
  CHECK(both->path() == "image/both.png");
  CHECK(both->width() == 4);
  auto jpg = cache.load("image/both.jpg");
  REQUIRE(jpg);
  CHECK(jpg->width() == 8);
  auto only = cache.load("IMAGE/Only");
  REQUIRE(only);
  CHECK(only->path() == "image/only.jpg");
  CHECK_FALSE(cache.load("image/missing"));

  // JPEG 2000 resolves but cannot be decoded: reported as a failed load.
  std::ofstream(dir / "image/j.jp2", std::ios::binary) << "not really";
  choco::assets::AssetFS afs2(dir.string(), "");
  TextureCache cache2(afs2, &up);
  CHECK_FALSE(cache2.load("image/j"));
}

TEST_CASE("TextureCache splits ?options and caches by resolved path") {
  auto dir = makeTemp("opts");
  writePng(dir / "image/a.png", 4, 4, 0, 255, 0);
  choco::assets::AssetFS afs(dir.string(), "");
  FakeUploader up;
  TextureCache cache(afs, &up);

  auto r = cache.resolve("image/a?mask=1&x");
  CHECK(r.path == "image/a.png");
  CHECK(r.options == "mask=1&x");

  auto t1 = cache.load("image/a?opt=1");
  REQUIRE(t1);
  CHECK(t1->options() == "opt=1");
  CHECK(t1->path() == "image/a.png");
  auto t2 = cache.load("Image/A.PNG");
  CHECK(t2 == t1);
  CHECK(up.uploads == 1);
  CHECK(cache.size() == 1);
}

TEST_CASE("TextureCache trim drops only unused entries and releases them") {
  auto dir = makeTemp("trim");
  writePng(dir / "a.png", 2, 2, 1, 2, 3);
  writePng(dir / "b.png", 2, 2, 1, 2, 3);
  choco::assets::AssetFS afs(dir.string(), "");
  FakeUploader up;
  TextureCache cache(afs, &up);
  auto keep = cache.load("a");
  cache.load("b");
  CHECK(cache.size() == 2);
  cache.trim();
  CHECK(cache.size() == 1);
  CHECK(up.releases == 1);
  CHECK(cache.load("a") == keep);
  keep.reset();
  cache.trim();
  CHECK(cache.size() == 0);
  CHECK(up.live.empty());
}

TEST_CASE("TextureCache reuploadAll keeps shared_ptrs valid and swaps the handle") {
  auto dir = makeTemp("reup");
  writePng(dir / "a.png", 3, 5, 9, 9, 9);
  choco::assets::AssetFS afs(dir.string(), "");
  FakeUploader up;
  TextureCache cache(afs, &up);
  auto t = cache.load("a");
  REQUIRE(t);
  void* before = t->handle();
  cache.reuploadAll();
  CHECK(up.uploads == 2);
  CHECK(t->handle() != nullptr);
  CHECK(up.live.size() == 1);
  CHECK(up.live.count(t->handle()) == 1);
  CHECK(t->width() == 3);
  CHECK(cache.load("a") == t);
  CHECK(t->handle() != before);
}

TEST_CASE("TextureCache loadMask shares the resolution rules") {
  auto dir = makeTemp("maskload");
  writePng(dir / "image/m.png", 4, 2, 200, 0, 0);
  choco::assets::AssetFS afs(dir.string(), "");
  TextureCache cache(afs, nullptr);
  auto m = cache.loadMask("image/m?x=1");
  REQUIRE(m);
  CHECK(m->width() == 4);
  CHECK(m->hit(3, 1));
  CHECK_FALSE(cache.loadMask("image/nothing"));
}

TEST_CASE("HitMask thresholds") {
  // 4x1: black, (19,19,19), (20,0,0), (0,0,20)
  HitMask m(4, 1, {0, 0, 0, 19, 19, 19, 20, 0, 0, 0, 0, 20});
  CHECK_FALSE(m.hit(0, 0));
  CHECK_FALSE(m.hit(1, 0));
  CHECK(m.hit(2, 0));
  CHECK(m.hit(3, 0));
  CHECK_FALSE(m.hit(4, 0));
  CHECK_FALSE(m.hit(-1, 0));
  CHECK_FALSE(m.hit(0, 1));
}

TEST_CASE("HitMask hitScaled maps proportionally") {
  // 2x2 mask, only the bottom-right pixel set.
  HitMask m(2, 2, {0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255});
  CHECK_FALSE(m.hitScaled(10, 10, 100, 100));
  CHECK_FALSE(m.hitScaled(49.9f, 99, 100, 100));
  CHECK(m.hitScaled(50, 50, 100, 100));
  CHECK(m.hitScaled(99.9f, 99.9f, 100, 100));
  CHECK_FALSE(m.hitScaled(100, 50, 100, 100));  // outside the rect
  CHECK_FALSE(m.hitScaled(-1, 50, 100, 100));
  CHECK(m.hitScaled(1, 1, 2, 2));               // rect the same size as the mask
  CHECK_FALSE(m.hitScaled(0, 1, 2, 2));
  CHECK_FALSE(m.hitScaled(5, 5, 0, 0));
  CHECK_FALSE(HitMask().hitScaled(1, 1, 2, 2));
}

TEST_CASE("ClipStack intersects and restores") {
  ClipStack s;
  CHECK_FALSE(s.active());
  CHECK(s.push({0, 0, 100, 100}) == RectI{0, 0, 100, 100});
  CHECK(s.push({50, 60, 100, 100}) == RectI{50, 60, 50, 40});
  CHECK(s.push({0, 0, 60, 70}) == RectI{50, 60, 10, 10});
  CHECK_FALSE(s.blocksAll());
  s.pop();
  CHECK(s.current() == RectI{50, 60, 50, 40});
  CHECK(s.push({200, 200, 5, 5}).empty());
  CHECK(s.blocksAll());
  s.pop();
  s.pop();
  CHECK(s.current() == RectI{0, 0, 100, 100});
  s.pop();
  CHECK_FALSE(s.active());
  s.pop();  // unbalanced pop is harmless
  CHECK(s.depth() == 0);
  // touching rectangles do not overlap
  CHECK(RectI::intersect({0, 0, 10, 10}, {10, 0, 10, 10}).empty());
}

TEST_CASE("RecordingRenderer records calls with their arguments") {
  auto dir = makeTemp("rec");
  writePng(dir / "t.png", 4, 4, 1, 1, 1);
  choco::assets::AssetFS afs(dir.string(), "");
  FakeUploader up;
  TextureCache cache(afs, &up);
  auto tex = cache.load("t");

  RecordingRenderer r;
  r.setOutputScale(2);
  CHECK(r.outputScale() == 2);
  r.pushClip({0, 0, 100, 100});
  r.pushClip({50, 50, 100, 100});
  r.fillRect({1, 2, 3, 4}, {1, 0, 0, 0.5f}, Blend::Additive);
  r.drawTexture(*tex, {0, 0, 4, 4}, {10, 20, 8, 8}, {0.5f, 0.5f, 0.5f, 1}, Blend::Subtract, 45, {4, 4}, true, false);
  const Vertex v[3] = {{{0, 0}, {}, {0, 0}}, {{1, 0}, {}, {1, 0}}, {{0, 1}, {}, {0, 1}}};
  const int idx[3] = {0, 1, 2};
  r.drawGeometry(tex.get(), v, idx, Blend::Multiply);
  r.popClip();
  r.fillRect({0, 0, 1, 1}, {}, Blend::Normal);
  CHECK(r.createTarget(8, 8) == nullptr);
  r.setTarget(nullptr);

  const auto& c = r.calls();
  REQUIRE(c.size() == 9);
  using K = RecordingRenderer::Call::Kind;
  CHECK(c[0].kind == K::PushClip);
  CHECK(c[1].effectiveClip == RectI{50, 50, 50, 50});
  CHECK(c[2].kind == K::FillRect);
  CHECK(c[2].dst == RectF{1, 2, 3, 4});
  CHECK(c[2].color.a == 0.5f);
  CHECK(c[2].blend == Blend::Additive);
  CHECK(c[2].clipped);
  CHECK(c[2].activeClip == RectI{50, 50, 50, 50});
  CHECK(c[3].kind == K::DrawTexture);
  CHECK(c[3].texture == tex.get());
  CHECK(c[3].dst == RectF{10, 20, 8, 8});
  CHECK(c[3].rotation == 45);
  CHECK(c[3].origin == PointF{4, 4});
  CHECK(c[3].hflip);
  CHECK_FALSE(c[3].vflip);
  CHECK(c[3].blend == Blend::Subtract);
  CHECK(c[4].kind == K::DrawGeometry);
  CHECK(c[4].vertices.size() == 3);
  CHECK(c[4].indices == std::vector<int>{0, 1, 2});
  CHECK(c[5].kind == K::PopClip);
  CHECK(c[6].activeClip == RectI{0, 0, 100, 100});
  CHECK(c[8].kind == K::SetTarget);
  CHECK(r.count(K::FillRect) == 2);
  r.clear();
  CHECK(r.calls().empty());
}

TEST_CASE("buildTexturedQuad: whole texture, no inset") {
  auto q = buildTexturedQuad({}, {10, 20, 100, 50}, 64, 32, {1, 1, 1, 1}, 0, {}, false, false);
  CHECK(q[0].pos == PointF{10, 20});
  CHECK(q[1].pos == PointF{110, 20});
  CHECK(q[2].pos == PointF{110, 70});
  CHECK(q[3].pos == PointF{10, 70});
  CHECK(q[0].uv == PointF{0, 0});
  CHECK(q[2].uv == PointF{1, 1});
  // an explicit full-size src is also the whole texture
  auto f = buildTexturedQuad({0, 0, 64, 32}, {0, 0, 64, 32}, 64, 32, {}, 0, {}, false, false);
  CHECK(f[0].uv == PointF{0, 0});
  CHECK(f[2].uv == PointF{1, 1});
}

TEST_CASE("buildTexturedQuad: sub-rectangle is inset by half a texel") {
  auto q = buildTexturedQuad({16, 8, 32, 16}, {0, 0, 32, 16}, 64, 32, {}, 0, {}, false, false);
  CHECK(q[0].uv.x == doctest::Approx(16.5f / 64));
  CHECK(q[0].uv.y == doctest::Approx(8.5f / 32));
  CHECK(q[2].uv.x == doctest::Approx(47.5f / 64));
  CHECK(q[2].uv.y == doctest::Approx(23.5f / 32));
  // one-texel sides are left alone
  auto one = buildTexturedQuad({4, 4, 1, 1}, {0, 0, 8, 8}, 64, 32, {}, 0, {}, false, false);
  CHECK(one[0].uv.x == doctest::Approx(4.0f / 64));
  CHECK(one[2].uv.x == doctest::Approx(5.0f / 64));
}

TEST_CASE("buildTexturedQuad: flips swap the inset UVs") {
  auto q = buildTexturedQuad({16, 8, 32, 16}, {0, 0, 32, 16}, 64, 32, {}, 0, {}, true, true);
  CHECK(q[0].uv.x == doctest::Approx(47.5f / 64));
  CHECK(q[1].uv.x == doctest::Approx(16.5f / 64));
  CHECK(q[0].uv.y == doctest::Approx(23.5f / 32));
  CHECK(q[3].uv.y == doctest::Approx(8.5f / 32));
  CHECK(q[0].pos == PointF{0, 0});  // positions are not mirrored, only the image
}

TEST_CASE("buildTexturedQuad: a src past the texture edge is clipped, never wrapped") {
  // 100x100 dst for a 50x50 src, of which only the top 30 rows exist in a 64x30 texture.
  auto q = buildTexturedQuad({0, 0, 50, 50}, {10, 20, 100, 100}, 64, 30, {}, 0, {}, false, false);
  CHECK(q[2].pos.x == doctest::Approx(110));
  CHECK(q[2].pos.y == doctest::Approx(20 + 60));
  CHECK(q[2].uv.y <= 1.0f);
  CHECK(q[0].uv.y >= 0.0f);
  // vflip: the cut edge is at the top of dst
  auto f = buildTexturedQuad({0, 0, 50, 50}, {10, 20, 100, 100}, 64, 30, {}, 0, {}, false, true);
  CHECK(f[0].pos.y == doctest::Approx(20 + 40));
  CHECK(f[2].pos.y == doctest::Approx(120));
  // a negative src origin clips on the left/top
  auto n = buildTexturedQuad({-10, -10, 50, 50}, {0, 0, 50, 50}, 64, 64, {}, 0, {}, false, false);
  CHECK(n[0].pos == PointF{10, 10});
  CHECK(n[0].uv.x >= 0.0f);
  // entirely outside: nothing to draw
  auto o = buildTexturedQuad({0, 40, 10, 10}, {0, 0, 10, 10}, 64, 30, {}, 0, {}, false, false);
  CHECK(o[2].color.a == 0.0f);
}

TEST_CASE("buildTexturedQuad: rotation is clockwise about dst.xy + origin") {
  // 90 degrees about the centre of a 20x10 rect at (0,0).
  auto q = buildTexturedQuad({}, {0, 0, 20, 10}, 8, 8, {}, 90, {10, 5}, false, false);
  CHECK(q[0].pos.x == doctest::Approx(15));
  CHECK(q[0].pos.y == doctest::Approx(-5));
  CHECK(q[1].pos.x == doctest::Approx(15));
  CHECK(q[1].pos.y == doctest::Approx(15));
  CHECK(q[2].pos.x == doctest::Approx(5));
  CHECK(q[2].pos.y == doctest::Approx(15));
  // pivot at the top-left corner: the top-right corner swings down
  auto p = buildTexturedQuad({}, {100, 100, 20, 10}, 8, 8, {}, 90, {0, 0}, false, false);
  CHECK(p[0].pos.x == doctest::Approx(100));
  CHECK(p[0].pos.y == doctest::Approx(100));
  CHECK(p[1].pos.x == doctest::Approx(100));
  CHECK(p[1].pos.y == doctest::Approx(120));
}

// Axis-aligned 1:1 textured quads must keep drawing after the window is resized and the letterbox re-applied (the
// headless emulation of a fullscreen toggle), also when a clip rectangle cuts the scaled sprite. SDL's software renderer
// used to wreck RLE-encoded static textures on a clipped scaled blit (see SdlRenderer's constructor).
TEST_CASE("SdlRenderer batching keeps draw order across fills and clip changes") {
  auto dir = makeTemp("batch_order");
  for (auto [name, rgb] : {std::pair{"red", 0}, std::pair{"blue", 2}}) {
    std::vector<unsigned char> px(8 * 8 * 4, 0);
    for (int i = 0; i < 64; ++i) {
      px[static_cast<std::size_t>(i) * 4 + static_cast<std::size_t>(rgb)] = 255;
      px[static_cast<std::size_t>(i) * 4 + 3] = 255;
    }
    REQUIRE(choco::platform::writePng((dir / (std::string(name) + ".png")).string(), 8, 8, px.data()));
  }
  choco::platform::Platform platform(true);
  REQUIRE(platform.ok());
  choco::platform::WindowConfig cfg;
  cfg.headless = true;
  choco::platform::Window window(platform, cfg);
  REQUIRE(window.ok());
  choco::platform::SdlRenderer renderer(window);
  choco::assets::AssetFS afs(dir.string(), "");
  TextureCache cache(afs, &renderer);
  auto red = cache.load("red");
  auto blue = cache.load("blue");
  REQUIRE(red);
  REQUIRE(blue);
  Renderer& r = renderer;
  const ColorF white{1, 1, 1, 1};
  auto pixel = [&](const choco::platform::Image& img, int x, int y) {
    const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(x)) * 4;
    return std::array<int, 3>{img.rgba[i], img.rgba[i + 1], img.rgba[i + 2]};
  };
  const std::array<int, 3> kRed{255, 0, 0}, kBlue{0, 0, 255}, kGreen{0, 255, 0};

  window.beginFrame();
  window.clear({0, 0, 0, 255});
  // 1. red quad, a green fill over it, red again over part of the fill: each later draw must win where they overlap
  r.drawTexture(*red, {}, {100, 100, 40, 40}, white, Blend::Normal);
  r.fillRect({110, 100, 40, 40}, {0, 1, 0, 1}, Blend::Opaque);
  r.drawTexture(*red, {}, {120, 100, 40, 40}, white, Blend::Normal);
  // 2. same texture either side of a clip change: the first quad is unclipped, the second is cut at x = 130
  r.drawTexture(*blue, {}, {100, 200, 20, 40}, white, Blend::Normal);
  r.pushClip({130, 0, 100, 600});
  r.drawTexture(*blue, {}, {120, 200, 40, 40}, white, Blend::Normal);
  r.popClip();
  // 3. same texture, then a different blend mode over part of it: half-grey tint, opaque, overwrites the overlap
  r.drawTexture(*red, {}, {100, 300, 40, 40}, white, Blend::Normal);
  r.drawTexture(*red, {}, {120, 300, 40, 40}, {0.5f, 0.5f, 0.5f, 1}, Blend::Opaque);
  window.endFrame();
  choco::platform::Image img;
  REQUIRE(window.readLogicalPixels(img));
  CHECK(pixel(img, 105, 120) == kRed);    // only the first red quad
  CHECK(pixel(img, 115, 120) == kGreen);  // the fill over the first quad
  CHECK(pixel(img, 125, 120) == kRed);    // the second red quad over the fill
  CHECK(pixel(img, 155, 120) == kRed);    // only the second red quad
  CHECK(pixel(img, 105, 220) == kBlue);   // the unclipped quad
  CHECK(pixel(img, 125, 220) == std::array<int, 3>{0, 0, 0});  // left of the clip: the second quad is cut
  CHECK(pixel(img, 145, 220) == kBlue);   // right of the clip: drawn
  CHECK(pixel(img, 105, 320) == kRed);
  const auto grey = pixel(img, 135, 320);  // the tinted quad replaced the red
  CHECK(std::abs(grey[0] - 128) <= 2);
  CHECK(grey[1] == 0);
  CHECK(grey[2] == 0);
}

TEST_CASE("SdlRenderer keeps drawing unrotated 1:1 quads after a resize, clipped or not") {
  auto dir = makeTemp("resize_quad");
  {  // a red disc whose edge fades out and whose corners are clear, like the slider knob: soft alpha is what makes SDL RLE-encode the surface
    std::vector<unsigned char> px(32 * 32 * 4);
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 32; ++x) {
        const int dx = x - 16, dy = y - 16, d2 = dx * dx + dy * dy;
        unsigned char* p = &px[(static_cast<std::size_t>(y) * 32 + x) * 4];
        p[0] = 255, p[1] = p[2] = 0;
        p[3] = d2 < 100 ? 255 : (d2 < 256 ? static_cast<unsigned char>(255 - (d2 - 100) * 255 / 156) : 0);
      }
    REQUIRE(choco::platform::writePng((dir / "red.png").string(), 32, 32, px.data()));
  }
  choco::platform::Platform platform(true);
  REQUIRE(platform.ok());
  choco::platform::WindowConfig cfg;
  cfg.headless = true;
  choco::platform::Window window(platform, cfg);
  REQUIRE(window.ok());
  choco::platform::SdlRenderer renderer(window);
  choco::assets::AssetFS afs(dir.string(), "");
  TextureCache cache(afs, &renderer);
  auto tex = cache.load("red");
  REQUIRE(tex);
  Renderer& r = renderer;

  // Logical pixels of the 32x32 sprite at (300,300) that are red; the clip, when on, starts one pixel into the sprite.
  auto litPixels = [&](bool clip, float tiltDeg) {
    window.beginFrame();
    window.clear({0, 0, 0, 255});
    if (clip) r.pushClip({283, 301, 270, 33});
    r.drawTexture(*tex, {}, {300, 300, 32, 32}, {1, 1, 1, 1}, Blend::Normal, tiltDeg, {16, 16});
    if (clip) r.popClip();
    window.endFrame();
    choco::platform::Image img;
    REQUIRE(window.readLogicalPixels(img));
    int n = 0;
    for (int y = 296; y < 336; ++y)
      for (int x = 296; x < 336; ++x) n += img.rgba[(static_cast<std::size_t>(y) * img.width + x) * 4] > 200;
    return n;
  };
  const int full = litPixels(false, 0), cut = litPixels(true, 0);
  CHECK(full > 250);
  CHECK(cut > 250);
  REQUIRE(window.toggleFullscreen());  // 800x600 -> 2560x1440: scale 2.4, the sprite is no longer 1:1 on screen
  for (int i = 0; i < 3; ++i) {
    CHECK(litPixels(true, 0) > 250);  // first: the clipped scaled blit that used to poison the texture
    CHECK(litPixels(false, 0) > 250);
    CHECK(litPixels(false, 0) > 250);
  }
  REQUIRE(window.toggleFullscreen());
  CHECK(litPixels(false, 0) == full);
  CHECK(litPixels(true, 0) == cut);
}
