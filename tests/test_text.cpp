#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include "assets/AssetFS.h"
#include "gfx/RecordingRenderer.h"
#include "gfx/text/TextLayout.h"

using namespace choco::gfx;
using namespace choco::gfx::text;
namespace fs = std::filesystem;

namespace {

struct FakeUploader : TextureUploader {
  int uploads = 0, releases = 0;
  std::set<void*> live;
  void* upload(const std::uint8_t*, int, int) override {
    ++uploads;
    void* h = new int(uploads);
    live.insert(h);
    return h;
  }
  void release(void* h) override {
    ++releases;
    live.erase(h);
    delete static_cast<int*>(h);
  }
};

// Accepts in-place updates and records the rectangles.
struct PatchUploader : FakeUploader {
  struct Patch {
    int x, y, w, h, stride;
    const std::uint8_t* rgba;
  };
  std::vector<Patch> patches;
  const std::uint8_t* pageBase = nullptr;  // the CPU page the last whole upload was given
  void* upload(const std::uint8_t* rgba, int w, int h) override {
    pageBase = rgba;
    return FakeUploader::upload(rgba, w, h);
  }
  bool update(void*, int x, int y, int w, int h, const std::uint8_t* rgba, int stride) override {
    patches.push_back({x, y, w, h, stride, rgba});
    return true;
  }
};

std::vector<std::byte> readAll(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::vector<char> c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  std::vector<std::byte> b(c.size());
  for (std::size_t i = 0; i < c.size(); ++i) b[i] = static_cast<std::byte>(c[i]);
  return b;
}

Contour rect(float x0, float y0, float x1, float y1) {
  Contour c;
  c.start = {x0, y0};
  c.segs = {{false, {}, {x1, y0}}, {false, {}, {x1, y1}}, {false, {}, {x0, y1}}};
  return c;
}

// Monospace test font: advance 60, ascent 75, descent 25, so the em box is 100 units and a size-20 line has
// 0.16 px per unit, 9.6 px per character and a baseline 15 px below the top.
std::shared_ptr<const Font> mono(bool boxes = false) { return makeMonospaceFont("test", 60, boxes); }

TextStyle style(float size = 20) {
  TextStyle s;
  s.font = mono();
  s.size = size;
  return s;
}

std::vector<std::string> lineTexts(const TextLayout& l) {
  std::vector<std::string> out(static_cast<std::size_t>(l.lineCount()));
  for (auto& g : l.glyphPositions()) out[static_cast<std::size_t>(g.line)] += static_cast<char>(g.cp);
  return out;
}

}  // namespace

TEST_CASE("every shipped font parses") {
  const fs::path dir = fs::path(CHOCO_REFORGED_ASSETS) / "fonts";
  if (!fs::exists(dir)) {
    MESSAGE("Reforged fonts not present, skipped");
    return;
  }
  const std::map<std::string, std::size_t> expected = {
      {"arial", 1419},        {"arima", 1120},        {"choco3", 489},           {"fertigo-italic", 368},
      {"fertigo-script", 369}, {"fertigo", 565},      {"gabriela", 566},         {"nanummyeongjo", 11882},
      {"notoserif-sc", 30928}, {"notoserif-tc", 20748}, {"shipporimincho", 15363}, {"srisakdi", 727},
      {"trirong", 724},       {"yatraone", 439}};
  std::size_t seen = 0;
  for (auto& [name, count] : expected) {
    CAPTURE(name);
    auto bytes = readAll(dir / (name + ".mvec"));
    REQUIRE_FALSE(bytes.empty());
    std::string err;
    auto f = Font::fromBytes(std::move(bytes), name, &err);
    REQUIRE_MESSAGE(f, err);
    CHECK(f->glyphCount() == count);
    const int w = f->glyphIndex('W');
    REQUIRE(w >= 0);
    CHECK(f->ascent() == doctest::Approx(-f->metrics(w).minY));
    CHECK(f->ascent() > 0);
    CHECK(f->descent() >= 0);
    CHECK(f->has(' '));
    // Program extents never overlap each other or the table.
    std::vector<std::pair<std::uint32_t, std::size_t>> ext;
    for (int i = 0; i < static_cast<int>(f->glyphCount()); ++i) ext.push_back({f->programOffset(i), f->programLength(i)});
    std::sort(ext.begin(), ext.end());
    bool overlap = false;
    for (std::size_t i = 1; i < ext.size(); ++i) overlap |= ext[i - 1].first + ext[i - 1].second > ext[i].first;
    CHECK_FALSE(overlap);
    CHECK(ext.front().first >= 12 + 18 * f->glyphCount());
    ++seen;
  }
  CHECK(seen == 14);
}

TEST_CASE("shipped glyph outlines decode") {
  const fs::path p = fs::path(CHOCO_REFORGED_ASSETS) / "fonts/fertigo.mvec";
  if (!fs::exists(p)) return;
  auto f = Font::fromBytes(readAll(p), "fertigo");
  REQUIRE(f);
  const int o = f->glyphIndex('O');
  REQUIRE(o >= 0);
  auto oc = f->outline(o);
  CHECK(oc.size() >= 2);  // outer and inner contour
  bool anyQuad = false;
  for (auto& c : oc)
    for (auto& s : c.segs) anyQuad |= s.quad;
  CHECK(anyQuad);
  const GlyphMetrics m = f->metrics(o);
  for (auto& c : oc) {
    CHECK(c.start.x >= m.minX - 0.01f);
    CHECK(c.start.y >= m.minY - 0.01f);
    CHECK(c.start.y <= m.maxY + 0.01f);
  }
  CHECK(f->outline(f->glyphIndex(' ')).empty());
  CHECK(f->glyphIndex(0x10ffff) == -1);
}

TEST_CASE("outline round trip through buildMvec and unknown opcodes") {
  GlyphSpec a;
  a.cp = 'A';
  a.advance = 50;
  Contour c;
  c.start = {1, -10};
  c.segs = {{false, {}, {20, -10}}, {true, {30, -10}, {30, 0}}, {false, {}, {1, 0}}};
  a.contours = {c};
  GlyphSpec w;
  w.cp = 'W';
  w.advance = 70;
  w.contours = {rect(0, -40, 60, 0)};
  auto bytes = buildMvec({w, a});  // unsorted on purpose
  auto f = Font::fromBytes(bytes, "rt");
  REQUIRE(f);
  CHECK(f->glyphCount() == 2);
  CHECK(f->glyphIndex('A') == 0);
  CHECK(f->glyphIndex('W') == 1);
  CHECK(f->ascent() == doctest::Approx(40));
  auto o = f->outline(0);
  REQUIRE(o.size() == 1);
  CHECK(o[0].start == PointF{1, -10});
  REQUIRE(o[0].segs.size() == 3);
  CHECK(o[0].segs[1].quad);
  CHECK(o[0].segs[1].c == PointF{30, -10});
  CHECK(o[0].segs[1].p == PointF{30, 0});
  CHECK(f->metrics(0).advance == doctest::Approx(50));
  CHECK(f->metrics(0).maxX == doctest::Approx(30));

  // 03 nn, 00, 01, 07 and 0xff are skipped; the glyph still decodes.
  std::vector<std::byte> b = bytes;
  const auto off = f->programOffset(1);  // W
  std::vector<std::byte> prog;
  for (int v : {0x02, 0x01, 0x03, 0x05, 0x00, 0x01, 0x07, 0xff}) prog.push_back(static_cast<std::byte>(v));
  b.insert(b.begin() + off + 2, prog.begin() + 2, prog.end());
  // Offsets after W (none: it is the last program) stay valid.
  auto g = Font::fromBytes(b, "skip");
  REQUIRE(g);
  CHECK(g->outline(1).size() == 1);
  CHECK(g->programLength(1) == f->programLength(1) + 6);
}

TEST_CASE("corrupt fonts are rejected") {
  auto good = buildMvec({[] {
    GlyphSpec g;
    g.cp = 'W';
    g.advance = 10;
    g.contours = {rect(0, -5, 5, 0)};
    return g;
  }()});
  std::string err;
  REQUIRE(Font::fromBytes(good, "g", &err));
  auto expectBad = [&](std::vector<std::byte> b, const char* what) {
    std::string e;
    CHECK_MESSAGE(!Font::fromBytes(std::move(b), "bad", &e), what);
    CHECK_FALSE(e.empty());
  };
  expectBad({}, "empty");
  auto b = good;
  b[0] = std::byte{'X'};
  expectBad(b, "magic");
  b = good;
  b[4] = std::byte{200};  // count past the file
  expectBad(b, "count");
  b = good;
  b.resize(good.size() - 1);  // loses the closing 08
  expectBad(b, "unterminated");
  b = good;
  b[12 + 14] = std::byte{4};  // offset points into the table
  expectBad(b, "offset in table");
  b[12 + 14] = std::byte{255};
  b[12 + 15] = std::byte{255};
  expectBad(b, "offset past end");
  b = good;
  b[4] = std::byte{0};
  expectBad(b, "zero glyphs");
  // two glyphs, descending code points
  GlyphSpec x, y;
  x.cp = 'a';
  y.cp = 'b';
  auto two = buildMvec({x, y});
  REQUIRE(Font::fromBytes(two, "two"));
  std::swap(two[12], two[12 + 18]);
  expectBad(two, "unsorted");
}

TEST_CASE("metrics: ascent, descent, scale and baseline") {
  auto f = mono();
  CHECK(f->ascent() == doctest::Approx(75));
  CHECK(f->descent() == doctest::Approx(25));  // from 'y'
  CHECK(f->pixelScale(20) == doctest::Approx(0.16f));
  CHECK(f->baseline(20) == doctest::Approx(15.0f));  // 0.15*20 + 0.8*20*0.75
  CHECK(f->glyphScaleY(f->glyphIndex('W')) == doctest::Approx(1.0f));
  // lithos faces are 0.7 of the size
  auto l = Font::fromBytes(buildMvec({[] {
                             GlyphSpec g;
                             g.cp = 'W';
                             g.contours = {rect(0, -75, 10, 25)};
                             return g;
                           }()}),
                           "fonts/Lithos-Regular.mvec");
  REQUIRE(l);
  CHECK(l->pixelScale(100) == doctest::Approx(0.8f * 100 / 100 * 0.7f));
  // underscore is excluded from the descent
  GlyphSpec w, u;
  w.cp = 'W';
  w.contours = {rect(0, -50, 10, 5)};
  u.cp = '_';
  u.contours = {rect(0, 0, 10, 40)};
  auto d = Font::fromBytes(buildMvec({w, u}), "d");
  REQUIRE(d);
  CHECK(d->descent() == doctest::Approx(5));
  // a tall glyph is squashed to the line box
  GlyphSpec tall;
  tall.cp = 0x4e2d;
  tall.contours = {rect(0, -150, 10, 0)};
  auto t = Font::fromBytes(buildMvec({w, tall}), "t");
  REQUIRE(t);
  CHECK(t->glyphScaleY(t->glyphIndex(0x4e2d)) < 1.0f);
  CHECK(t->glyphScaleY(t->glyphIndex('W')) == doctest::Approx(1.0f));
}

TEST_CASE("FontCache falls back to default.mvec, then to the built-in font") {
  auto dir = fs::temp_directory_path() / "choco_text_fonts";
  fs::remove_all(dir);
  fs::create_directories(dir / "fonts");
  GlyphSpec w;
  w.cp = 'W';
  w.advance = 33;
  w.contours = {rect(0, -80, 10, 20)};
  auto bytes = buildMvec({w});
  auto write = [&](const char* name, const std::vector<std::byte>& b) {
    std::ofstream(dir / name, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
  };
  write("fonts/real.mvec", bytes);
  write("fonts/broken.mvec", std::vector<std::byte>(40, std::byte{1}));
  choco::assets::AssetFS afs(dir.string(), "");
  FontCache cache(&afs);

  auto real = cache.get("fonts/real.mvec");
  CHECK(real->name() == "fonts/real.mvec");
  CHECK(cache.get("Fonts/REAL.mvec") == real);
  // no default.mvec yet: missing and broken names both end at the built-in
  auto missing = cache.get("fonts/nope.mvec");
  CHECK(missing == cache.builtin());
  CHECK(cache.get("fonts/broken.mvec") == cache.builtin());
  CHECK(cache.get("__DEFAULT") == cache.builtin());
  CHECK(cache.builtin()->ascent() > 0);
  CHECK(cache.builtin()->descent() > 0);

  // a game's embedded font takes the built-in's place at the end of the chain (and is "__DEFAULT"); cached names keep theirs
  FontCache withEmbedded(&afs);
  withEmbedded.setEmbedded(real);
  CHECK(withEmbedded.get("nope.mvec") == real);
  CHECK(withEmbedded.get("__DEFAULT") == real);
  CHECK(withEmbedded.get("fonts/real.mvec")->name() == "fonts/real.mvec");

  write("fonts/default.mvec", bytes);
  choco::assets::AssetFS afs2(dir.string(), "");
  FontCache cache2(&afs2);
  auto viaDefault = cache2.get("fonts/nope.mvec");
  CHECK(viaDefault->name() == "fonts/default.mvec");
  CHECK(cache2.get("fonts/nope.mvec") == viaDefault);  // cached under the requested name
  CHECK(cache2.get("__DEFAULT") == cache2.builtin());
  FontCache none;  // no asset files at all
  CHECK(none.get("anything") == none.builtin());
}

TEST_CASE("rasteriser: coverage of simple shapes") {
  auto square = [](float x0, float y0, float x1, float y1) { return Polygon{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}; };
  auto bm = rasterize({square(2, 3, 12, 13)});
  CHECK(bm.x == 2);
  CHECK(bm.y == 3);
  CHECK(bm.w == 10);
  CHECK(bm.h == 10);
  int full = 0;
  for (auto a : bm.alpha) full += a == 255;
  CHECK(full == 100);

  // half-covered column and row
  auto half = rasterize({square(2.5f, 3.0f, 4.0f, 5.5f)});
  REQUIRE(half.w == 2);
  CHECK(half.at(0, 0) == 128);   // x coverage 0.5
  CHECK(half.at(1, 0) == 255);
  CHECK(half.at(1, 2) == 128);   // y coverage 0.5 (two of four sub-scanlines)
  CHECK(half.at(0, 2) == 64);

  // same-direction overlap does not exceed full coverage; a reversed inner square is a hole
  auto both = rasterize({square(0, 0, 10, 10), square(2, 2, 8, 8)});
  CHECK(both.at(5, 5) == 255);
  Polygon inner = square(3, 3, 7, 7);
  std::reverse(inner.begin(), inner.end());
  auto ring = rasterize({square(0, 0, 10, 10), inner});
  CHECK(ring.at(5, 5) == 0);
  CHECK(ring.at(1, 5) == 255);
  CHECK(ring.at(8, 8) == 255);

  CHECK(rasterize({}).empty());
  CHECK(rasterize({square(0, 0, 5000, 5)}).empty());  // too large
}

TEST_CASE("rasteriser: flattening, shear and strokes") {
  Contour c;
  c.start = {0, 0};
  c.segs = {{true, {10, 0}, {10, 10}}, {false, {}, {0, 10}}};
  auto polys = flatten({c}, 1, 1, false, 0);
  REQUIRE(polys.size() == 1);
  CHECK(polys[0].size() == 1 + 3 + 1);  // a quadratic is three line segments
  CHECK(polys[0][3].x == doctest::Approx(10));
  CHECK(polys[0][1].x == doctest::Approx(2 * 10.0f / 3 - 10.0f / 9));  // t = 1/3: 2(1-t)t*10 + t^2*10

  // italic: x += y / -3, so a point 30 above the baseline moves 10 right
  Contour tri;
  tri.start = {0, -30};
  tri.segs = {{false, {}, {5, 0}}, {false, {}, {0, 0}}};
  auto it = flatten({tri}, 2, 2, true, 1.5f);
  CHECK(it[0][0].x == doctest::Approx((0 + 10) * 2 + 1.5f));
  CHECK(it[0][0].y == doctest::Approx(-60));
  CHECK(it[0][2].x == doctest::Approx(1.5f));

  Polygon sq{{4, 4}, {8, 4}, {8, 8}, {4, 8}};
  auto plain = rasterize({sq});
  auto wide = unite(plain, rasterize(strokePolygons({sq}, 1.0f)));
  CHECK(wide.x == plain.x - 1);
  CHECK(wide.w == plain.w + 2);
  CHECK(wide.at(0, 0) > 0);  // the stroke's corner is filled
  CHECK(wide.at(1, 1) == 255);
}

TEST_CASE("rasteriser: hand-made glyphs through the cache") {
  GlyphSpec w, box, ring;
  w.cp = 'W';
  w.advance = 80;
  w.contours = {rect(0, -100, 80, 0)};
  box.cp = 'B';
  box.advance = 80;
  box.contours = {rect(10, -50, 60, 0)};  // 50 x 50 units
  ring.cp = 'O';
  ring.advance = 80;
  Contour inner;  // the hole winds the other way
  inner.start = {30, -30};
  inner.segs = {{false, {}, {30, -10}}, {false, {}, {50, -10}}, {false, {}, {50, -30}}};
  ring.contours = {rect(10, -50, 70, 0), inner};
  auto f = Font::fromBytes(buildMvec({w, box, ring}), "h");
  REQUIRE(f);

  RasterParams p;
  p.scale = 0.2f;  // 50 units -> 10 px
  auto bm = rasterizeGlyph(*f, f->glyphIndex('B'), p);
  CHECK(bm.x == 2);
  CHECK(bm.y == -10);
  CHECK(bm.w == 10);
  CHECK(bm.h == 10);
  for (auto a : bm.alpha) CHECK(a == 255);

  p.subX = 2;  // half a pixel right
  auto shifted = rasterizeGlyph(*f, f->glyphIndex('B'), p);
  CHECK(shifted.w == 11);
  CHECK(shifted.at(0, 0) == 128);
  CHECK(shifted.at(10, 0) == 128);

  p.subX = 0;
  auto o = rasterizeGlyph(*f, f->glyphIndex('O'), p);
  REQUIRE(o.w == 12);
  CHECK(o.at(5, 5) == 0);    // inside the hole: x = 2 + 5.5 -> units 37, y = -10 + 5.5
  CHECK(o.at(0, 0) == 255);  // ink
  CHECK(o.at(o.w - 1, o.h - 1) == 255);

  // bold / outline stroke widens by `stroke` pixels each side
  p.stroke = 1.0f;
  auto bold = rasterizeGlyph(*f, f->glyphIndex('B'), p);
  CHECK(bold.w == 12);
  CHECK(bold.h == 12);

  // atlas: one rasterisation per (glyph, sub-pixel, stroke, scale)
  FakeUploader up;
  {
    GlyphAtlas atlas(&up);
    RasterParams q;
    q.scale = 0.2f;
    auto e1 = atlas.get(*f, f->glyphIndex('B'), q);
    auto e2 = atlas.get(*f, f->glyphIndex('B'), q);
    CHECK(atlas.rasterisations() == 1);
    CHECK(e1.texture == e2.texture);
    CHECK(e1.u0 == e2.u0);
    q.subX = 1;
    atlas.get(*f, f->glyphIndex('B'), q);
    q.italic = true;
    atlas.get(*f, f->glyphIndex('B'), q);
    q.scale = 0.4f;
    auto big = atlas.get(*f, f->glyphIndex('B'), q);
    CHECK(atlas.rasterisations() == 4);
    CHECK(big.w > e1.w);
    CHECK(atlas.pageCount() == 1);
    CHECK(up.uploads == 0);
    atlas.flush();
    CHECK(up.uploads == 1);
    atlas.flush();
    CHECK(up.uploads == 1);  // nothing changed
    CHECK(e1.v0 >= 0);
    CHECK(e1.u1 <= 1);
    atlas.reuploadAll();
    CHECK(up.uploads == 2);
    CHECK(up.live.size() == 1);
  }
  CHECK(up.live.empty());

  // a page is uploaded whole once, then patched: the texture lives on and only the new glyph's rectangle is sent
  PatchUploader pu;
  {
    GlyphAtlas atlas(&pu);
    RasterParams q;
    q.scale = 0.2f;
    auto first = atlas.get(*f, f->glyphIndex('B'), q);
    atlas.flush();
    CHECK(pu.uploads == 1);
    CHECK(pu.patches.empty());
    q.subX = 2;
    auto second = atlas.get(*f, f->glyphIndex('B'), q);
    atlas.flush();
    CHECK(pu.uploads == 1);  // no new texture
    CHECK(pu.releases == 0);
    REQUIRE(pu.patches.size() == 1);
    CHECK(pu.patches[0].stride == GlyphAtlas::kPageSize * 4);
    CHECK(pu.patches[0].w == second.w);
    CHECK(pu.patches[0].h == second.h);
    CHECK(pu.patches[0].x == static_cast<int>(std::lround(second.u0 * GlyphAtlas::kPageSize)));
    CHECK(pu.patches[0].y == static_cast<int>(std::lround(second.v0 * GlyphAtlas::kPageSize)));
    // the pointer is the first texel of that rectangle inside the page the whole upload was given
    REQUIRE(pu.pageBase != nullptr);
    CHECK(pu.patches[0].rgba == pu.pageBase + (static_cast<std::size_t>(pu.patches[0].y) * GlyphAtlas::kPageSize +
                                               static_cast<std::size_t>(pu.patches[0].x)) * 4);
    CHECK(first.texture == second.texture);
    CHECK(atlas.partialUploads() == 1);
    atlas.flush();
    CHECK(pu.patches.size() == 1);  // clean page: nothing sent
    atlas.reuploadAll();  // a device reset needs a fresh texture, not a patch
    CHECK(pu.uploads == 2);
    CHECK(pu.patches.size() == 1);
  }
}

TEST_CASE("atlas packs without overlap and starts new pages") {
  GlyphSpec w;
  w.cp = 'W';
  w.advance = 10;
  w.contours = {rect(0, -100, 100, 0)};
  auto f = Font::fromBytes(buildMvec({w}), "pk");
  REQUIRE(f);
  FakeUploader up;
  GlyphAtlas atlas(&up);
  std::vector<GlyphAtlas::Entry> es;
  for (int i = 0; i < 80; ++i) {
    RasterParams p;
    p.scale = 1.0f + static_cast<float>(i) * 0.01f;  // ~100 px glyphs, distinct keys
    es.push_back(atlas.get(*f, 0, p));
  }
  CHECK(atlas.pageCount() >= 2);
  for (std::size_t i = 0; i < es.size(); ++i) {
    for (std::size_t j = i + 1; j < es.size(); ++j) {
      if (es[i].texture != es[j].texture) continue;
      const bool apart = es[i].u1 <= es[j].u0 || es[j].u1 <= es[i].u0 || es[i].v1 <= es[j].v0 || es[j].v1 <= es[i].v0;
      CHECK(apart);
    }
  }
  // overflowing kMaxPages empties the atlas at the next draw, never during one
  for (int i = 0; i < 400; ++i) {
    RasterParams p;
    p.scale = 1.0f + static_cast<float>(i + 100) * 0.003f;
    atlas.get(*f, 0, p);
  }
  CHECK(atlas.pageCount() > static_cast<std::size_t>(GlyphAtlas::kMaxPages));
  atlas.beginDraw();
  CHECK(atlas.glyphCount() == 0);
  CHECK(atlas.pageCount() <= static_cast<std::size_t>(GlyphAtlas::kMaxPages));
}

TEST_CASE("atlas: an output scale change empties it at once, the same scale does nothing") {
  GlyphSpec w;
  w.cp = 'W';
  w.advance = 10;
  w.contours = {rect(0, -100, 100, 0)};
  auto f = Font::fromBytes(buildMvec({w}), "os");
  REQUIRE(f);
  FakeUploader up;
  GlyphAtlas atlas(&up);
  atlas.setOutputScale(1.0f);  // empty atlas: only remembered
  RasterParams p;
  p.scale = 0.2f;
  atlas.get(*f, 0, p);
  atlas.flush();
  CHECK(atlas.glyphCount() == 1);
  CHECK(atlas.rasterisations() == 1);
  const std::size_t pages = atlas.pageCount();
  const std::size_t uploads = up.uploads;
  atlas.setOutputScale(1.0f);  // same value: no-op
  CHECK(atlas.glyphCount() == 1);
  CHECK(atlas.rasterisations() == 1);
  CHECK(up.uploads == uploads);
  atlas.setOutputScale(1.5f);
  CHECK(atlas.glyphCount() == 0);
  CHECK(atlas.pageCount() == pages);
  p.scale = 0.3f;
  atlas.get(*f, 0, p);
  CHECK(atlas.rasterisations() == 2);
  CHECK(atlas.glyphCount() == 1);
  atlas.setOutputScale(1.5f);
  CHECK(atlas.glyphCount() == 1);
  CHECK(atlas.rasterisations() == 2);

  // many alternating scales: the page count stays bounded and only the current scale's entries remain
  for (int i = 0; i < 50; ++i) {
    const float s = (i % 2) ? 1.0f : 2.0f;
    atlas.setOutputScale(s);
    CHECK(atlas.glyphCount() == 0);
    p.scale = 0.2f * s;
    for (int k = 0; k < 20; ++k) {
      p.subX = k % 4;
      p.stroke = static_cast<float>(k / 4);
      atlas.get(*f, 0, p);
    }
    atlas.beginDraw();
    CHECK(atlas.pageCount() <= static_cast<std::size_t>(GlyphAtlas::kMaxPages));
    CHECK(atlas.glyphCount() == 20);
  }
}

TEST_CASE("layout: wrapping at spaces, newlines and overlong words") {
  // size 20: 9.6 px per character; a 100 px box holds 10 characters (first glyph pad and the 1 px margin included)
  auto lay = [](std::string_view t, int w = 100) { return TextLayout::build(t, style(), w, 200); };
  CHECK(lineTexts(lay("hello")) == std::vector<std::string>{"hello"});
  CHECK(lineTexts(lay("aaaaaaaaaa")) == std::vector<std::string>{"aaaaaaaaaa"});
  CHECK(lineTexts(lay("hello world foo")) == std::vector<std::string>{"hello", "world foo"});
  CHECK(lineTexts(lay("aaaaaaaaaaaaaaaaaaaaaaaaa")) == std::vector<std::string>{"aaaaaaaaaa", "aaaaaaaaaa", "aaaaa"});
  CHECK(lineTexts(lay("ab cd\nef")) == std::vector<std::string>{"ab cd", "ef"});
  CHECK(lineTexts(lay("a\n\nb")).size() == 3);  // a blank line between
  CHECK(lineTexts(lay("a\n")).size() == 1);       // a trailing newline adds nothing
  CHECK(lineTexts(lay("a<br>b</p>c")) == std::vector<std::string>{"a", "b", "c"});
  CHECK(lineTexts(lay("a<br><br>b")).size() == 3);
  // a long word after a short one: the short one stays, the word breaks per character on its own line
  auto l = lineTexts(lay("hi aaaaaaaaaaaaaaaaaaaaa"));
  REQUIRE(l.size() == 4);  // hi / 10 / 10 / 1
  CHECK(l[0] == "hi");
  CHECK(l[1] == "aaaaaaaaaa");
  // one character wider than the box still takes the line
  CHECK(lineTexts(lay("abc", 5)) == std::vector<std::string>{"a", "b", "c"});
  CHECK(TextLayout::build("", style(), 100, 100).lineCount() == 0);
  CHECK(TextLayout::build("x", style(), 0, 100).lineCount() == 0);
  // line height is size; total height is the sum
  CHECK(lay("a<br>b<br>c").bounds().h == 60);
}

TEST_CASE("layout: keepLastOverflowChar (Chocolatier 1) keeps a final overflowing glyph on its line") {
  // size 20, 100 px box: 10 characters fit; an 11-character word overflows on its last letter only.
  TextStyle on = style();
  on.keepLastOverflowChar = true;
  CHECK(lineTexts(TextLayout::build("aaaaaaaaaaa", style(), 100, 200)) == std::vector<std::string>{"aaaaaaaaaa", "a"});
  const auto kept = TextLayout::build("aaaaaaaaaaa", on, 100, 200);
  CHECK(lineTexts(kept) == std::vector<std::string>{"aaaaaaaaaaa"});
  CHECK(kept.bounds().h == 20);
  // not the last glyph, or a space candidate: the existing rules
  CHECK(lineTexts(TextLayout::build("aaaaaaaaaaaa", on, 100, 200)) == std::vector<std::string>{"aaaaaaaaaa", "aa"});
  CHECK(lineTexts(TextLayout::build("hello world", on, 100, 200)) == std::vector<std::string>{"hello", "world"});
  // text that fits is untouched
  CHECK(lineTexts(TextLayout::build("hello", on, 100, 200)) == std::vector<std::string>{"hello"});
}

TEST_CASE("layout: alignment") {
  auto at = [](int flags, int w = 200, int h = 100) {
    TextStyle s = style();
    s.flags = flags;
    auto l = TextLayout::build("a", s, w, h);
    auto g = l.glyphPositions();
    REQUIRE(g.size() == 1);
    return std::make_pair(g[0], l.bounds());
  };
  // pen 42.4 qpx (38.4 + the 4 qpx first-glyph pad) -> lineW 10 px; the offset adds pad * 0.25 + 0.5 = 1.5 px
  auto [gl, bl] = at(kHAlignLeft);
  CHECK(gl.x == doctest::Approx(1.5f));
  CHECK(gl.y == doctest::Approx(15.0f));
  CHECK(bl.x == 1);
  CHECK(bl.w == 10);  // (int)11.5 - (int)1.5: bounds truncate like the original
  CHECK(bl.y == 0);
  CHECK(bl.h == 20);
  auto [gc, bc] = at(kHAlignCenter);
  CHECK(gc.x == doctest::Approx(100 - 5.0f + 1.5f));
  auto [gr, br] = at(kHAlignRight);
  CHECK(gr.x == doctest::Approx(199 - 10 + 1.5f));
  auto [gv, bv] = at(kHAlignLeft + kVAlignCenter);
  CHECK(gv.y == doctest::Approx(40 + 15.0f));
  CHECK(bv.y == 40);
  auto [gb, bb] = at(kVAlignBottom);
  CHECK(gb.y == doctest::Approx(80 + 15.0f));
  CHECK(bb.y == 80);

  // a <center> block applies to its own lines only
  auto l = TextLayout::build("a<center>b</center>c", style(), 200, 100);
  auto g = l.glyphPositions();
  REQUIRE(g.size() == 3);
  CHECK(g[0].x == doctest::Approx(1.5f));
  CHECK(g[1].x == doctest::Approx(100 - 5.0f + 1.5f));
  CHECK(g[2].x == doctest::Approx(1.5f));
  CHECK(l.lineCount() == 3);
  CHECK(g[2].y == doctest::Approx(g[0].y + 40));

  // TTightText: width plus pad, height as measured
  RectI m = TextLayout::measure("a", style(), 200, 100);
  CHECK(m.w == 10 + 3);
  CHECK(m.h == 20);
  CHECK(TextLayout::measure("a", style(), 200, 100, 0).w == 10);
}

TEST_CASE("layout: markup") {
  auto glyphs = [](std::string_view t, int w = 400) { return TextLayout::build(t, style(), w, 200).glyphPositions(); };
  CHECK(glyphs("a<x>b").size() == 2);              // unknown tags are swallowed
  CHECK(glyphs("a<b>b</b><i>c</i><u>d</u>").size() == 4);
  CHECK(glyphs("a < b").size() == 5);               // no closing '>': an ordinary character
  auto esc = glyphs("\\<b\\>\\\\");
  REQUIRE(esc.size() == 4);
  CHECK(esc[0].cp == '<');
  CHECK(esc[3].cp == '\\');
  CHECK(glyphs("<A HREF=\"x\">link</a>").size() == 4);  // <a ...> is colour only
  // <tab pos=N> sets the pen to N px
  auto tab = glyphs("a<tab pos='50'>b");
  REQUIRE(tab.size() == 2);
  CHECK(tab[1].x - tab[0].x == doctest::Approx(50 - 0.0f).epsilon(0.02));
  // <font size> changes the line height and the advance
  auto big = TextLayout::build("a<font size='40'>b</font>c", style(), 400, 200);
  CHECK(big.bounds().h == 40);
  auto bg = big.glyphPositions();
  REQUIRE(bg.size() == 3);
  CHECK(bg[2].x - bg[1].x == doctest::Approx(19.2f).epsilon(0.02));  // 'b' is 40 px: 60 units * 0.32
  // italic shifts the pen by (scale/3)*descent when switched on
  auto ital = glyphs("a<i>b</i>");
  CHECK(ital[1].x - ital[0].x == doctest::Approx(9.6f + 0.16f / 3 * 25).epsilon(0.02));
  // ... and by (scale/3)*ascent when switched off before the next glyph
  auto off = glyphs("<i>a</i>b");
  CHECK(off[1].x - off[0].x == doctest::Approx(9.6f + 0.16f / 3 * 75).epsilon(0.02));
  // UTF-8: two- and three-byte sequences are one glyph each
  GlyphSpec w, e, z;
  w.cp = 'W';
  w.advance = 60;
  w.explicitBounds = true;
  w.minY = -75;
  e.cp = 0xe9;
  e.advance = 60;
  z.cp = 0x4e2d;
  z.advance = 100;
  auto uf = Font::fromBytes(buildMvec({w, e, z}), "u");
  REQUIRE(uf);
  TextStyle us = style();
  us.font = uf;
  auto ug = TextLayout::build("\xc3\xa9\xe4\xb8\xad\xc3\xa9", us, 400, 100).glyphPositions();
  REQUIRE(ug.size() == 3);
  CHECK(ug[0].cp == 0xe9);
  CHECK(ug[1].cp == 0x4e2d);
  CHECK(ug[2].x - ug[1].x == doctest::Approx(100 * 0.8f * 20 / 75).epsilon(0.02));  // no descent in this font: em box is 75 units
  // characters in no font take no space
  CHECK(TextLayout::build("a\xf0\x9f\x98\x80""b", style(), 400, 100).glyphPositions().size() == 2);
  // hex colours
  CHECK(parseHexColor("#ff8000").color.g == doctest::Approx(128 / 255.0f));
  CHECK(parseHexColor("00ff0080").color.a == doctest::Approx(128 / 255.0f));
  CHECK(parseHexColor("00ff00").color.a == 1.0f);
  CHECK_FALSE(parseHexColor("xyz").ok);
}

TEST_CASE("layout: drawing") {
  FakeUploader up;
  GlyphAtlas atlas(&up);
  RecordingRenderer rr;
  TextStyle s = style(20);
  s.font = mono(true);
  s.color = {1, 0.5f, 0.25f, 1};

  rr.setOutputScale(2);
  auto l = TextLayout::build("ab<font color='00ff00'>c</font>", s, 200, 100);
  l.draw(rr, atlas, {10, 20}, 0.5f);
  REQUIRE(rr.count(RecordingRenderer::Call::Kind::DrawGeometry) == 1);
  const auto& call = rr.calls().back();
  REQUIRE(call.vertices.size() == 12);
  CHECK(call.texture != nullptr);
  CHECK(call.vertices[0].color.r == doctest::Approx(1.0f));
  CHECK(call.vertices[0].color.a == doctest::Approx(0.5f));
  CHECK(call.vertices[8].color.g == doctest::Approx(1.0f));
  CHECK(call.vertices[8].color.r == doctest::Approx(0.0f));
  for (auto& v : call.vertices) {
    CHECK(v.pos.x * 2 == doctest::Approx(std::round(v.pos.x * 2)));  // quads sit on device pixels
    CHECK(v.pos.y * 2 == doctest::Approx(std::round(v.pos.y * 2)));
    CHECK(v.uv.x >= 0);
    CHECK(v.uv.x <= 1);
  }
  // box: 50..60 units wide at 0.16 px -> glyph bitmap about 4.8 x 12 px inside the line
  float minY = 1e9f, maxY = -1e9f;
  for (auto& v : call.vertices) minY = std::min(minY, v.pos.y), maxY = std::max(maxY, v.pos.y);
  CHECK(minY >= 20 - 0.01f);
  CHECK(maxY <= 20 + 20 + 0.01f);
  CHECK(atlas.rasterisations() >= 2);  // a, b share a bitmap only when their sub-pixel phases agree

  // alpha 0 draws nothing; a second draw reuses the cached glyphs
  rr.clear();
  const auto before = atlas.rasterisations();
  l.draw(rr, atlas, {10, 20}, 0);
  CHECK(rr.calls().empty());
  l.draw(rr, atlas, {10, 20}, 1);
  CHECK(atlas.rasterisations() == before);

  // outline: a stroke layer first, then the fill; underline: a one pixel rule
  rr.clear();
  auto o = TextLayout::build("<outline color='0000ff' size=2><u>a</u></outline>", s, 200, 100);
  o.draw(rr, atlas, {0, 0});
  const auto geo = rr.count(RecordingRenderer::Call::Kind::DrawGeometry);
  CHECK(geo >= 1);
  CHECK(rr.count(RecordingRenderer::Call::Kind::FillRect) == 1);
  const auto& first = rr.calls().front();
  CHECK(first.kind == RecordingRenderer::Call::Kind::DrawGeometry);
  CHECK(first.vertices[0].color.b == doctest::Approx(1.0f));
  CHECK(rr.calls()[1].vertices[0].color.b == doctest::Approx(0.25f));
  const auto& fill = rr.calls()[1];
  CHECK((first.vertices[1].pos.x - first.vertices[0].pos.x) > (fill.vertices[1].pos.x - fill.vertices[0].pos.x) + 3.9f);

  // without an installed TextSystem the default draw does nothing
  rr.clear();
  l.draw(rr, {0, 0});
  CHECK(rr.calls().empty());
}

TEST_CASE("layout: box overflow stops drawing") {
  FakeUploader up;
  GlyphAtlas atlas(&up);
  RecordingRenderer rr;
  TextStyle s = style(20);
  s.font = mono(true);
  auto quads = [&](std::string_view text, int h, int flags = 0) {
    rr.clear();
    s.flags = flags;
    TextLayout::build(text, s, 200, h).draw(rr, atlas, {0, 0});
    std::size_t n = 0;
    for (auto& c : rr.calls())
      if (c.kind == RecordingRenderer::Call::Kind::DrawGeometry) n += c.vertices.size() / 4;
    return n;
  };
  CHECK(quads("a", 10) == 0);                       // the box is shorter than the font: nothing shows
  CHECK(quads("a", 20) == 1);                       // bottom == height still fits
  CHECK(quads("a<br>b<br>c", 40) == 2);             // a 2-line box with 3 lines draws 2
  CHECK(quads("a<br>b<br>c", 100) == 3);
  CHECK(quads("a<br>b<br>c<br>d", 40, kVAlignBottom) == 3);  // the first line ends above the box (bottom -20)
  // the layout itself keeps every line
  CHECK(TextLayout::build("a<br>b<br>c", s, 200, 40).lineCount() == 3);
}

TEST_CASE("layout: glyphs missing from the run font come from the default font") {
  GlyphSpec w, a;
  w.cp = 'W';
  w.advance = 60;
  w.contours = {rect(0, -75, 40, 0)};
  a.cp = 'a';
  a.advance = 60;
  a.contours = {rect(0, -50, 40, 0)};
  auto limited = Font::fromBytes(buildMvec({w, a}), "limited");
  REQUIRE(limited);
  FakeUploader up;
  GlyphAtlas atlas(&up);
  RecordingRenderer rr;
  FontCache fonts;  // no files: default.mvec is missing, so the built-in font is the default
  TextStyle s = style();
  s.font = limited;
  s.fonts = &fonts;
  auto countQuads = [&](const TextStyle& st, std::string_view text) {
    rr.clear();
    TextLayout::build(text, st, 200, 100).draw(rr, atlas, {0, 0});
    std::size_t n = 0;
    for (auto& c : rr.calls())
      if (c.kind == RecordingRenderer::Call::Kind::DrawGeometry) n += c.vertices.size() / 4;
    return n;
  };
  CHECK(TextLayout::build("aq", s, 200, 100).glyphPositions().size() == 2);
  CHECK(countQuads(s, "aq") == 2);  // 'q' is drawn with a visible built-in glyph
  CHECK(countQuads(s, "a\xc3\xa9") == 1);  // not in the built-in font either: nothing, and no width
  TextStyle noCache = s;
  noCache.fonts = nullptr;
  CHECK(countQuads(noCache, "aq") == 2);
  CHECK(builtinFont()->has('q'));
  CHECK(builtinFont()->has('7'));
  CHECK(builtinFont()->ascent() == doctest::Approx(84));
  CHECK(builtinFont()->descent() == doctest::Approx(12));
  // the fallback glyph is sized by its own font's metrics for the same line box
  auto g = TextLayout::build("aq", s, 200, 100).glyphPositions();
  CHECK(g[1].x - g[0].x == doctest::Approx(60 * limited->pixelScale(20)).epsilon(0.03));
}

TEST_CASE("layout: italic offset restarts on every wrapped line") {
  auto firstX = [](std::string_view text) {
    auto l = TextLayout::build(text, style(), 100, 200);
    std::vector<float> xs(static_cast<std::size_t>(l.lineCount()), -1);
    for (auto& g : l.glyphPositions())
      if (xs[static_cast<std::size_t>(g.line)] < 0) xs[static_cast<std::size_t>(g.line)] = g.x;
    return xs;
  };
  auto plain = firstX("aaaaaaaaaaaaaaaaaaaaaaaaa");
  auto ital = firstX("<i>aaaaaaaaaaaaaaaaaaaaaaaaa</i>");
  REQUIRE(plain.size() >= 3);
  REQUIRE(ital.size() >= 3);
  const float shift = 0.16f / 3 * 25;  // (scale / 3) * descent, in pixels
  for (std::size_t i = 0; i < ital.size(); ++i) {
    CHECK(ital[i] - plain[i] == doctest::Approx(shift).epsilon(0.05));  // every line, not only the first
  }
}

TEST_CASE("layout: links, bold outlines, stray bytes and device snapping") {
  FakeUploader up;
  GlyphAtlas atlas(&up);
  RecordingRenderer rr;
  TextStyle s = style(20);
  s.font = mono(true);
  s.color = {1, 0.5f, 0.25f, 1};
  auto l = TextLayout::build("<a id='x' color='00ff00'>a</a>", s, 200, 100);
  l.draw(rr, atlas, {0, 0});
  REQUIRE(rr.calls().size() == 1);
  CHECK(rr.calls()[0].vertices[0].color.g == doctest::Approx(0.5f));  // not hovered: plain colour
  l.setHoveredLink("x");
  rr.clear();
  l.draw(rr, atlas, {0, 0});
  CHECK(rr.calls()[0].vertices[0].color.g == doctest::Approx(1.0f));
  l.setHoveredLink("y");
  rr.clear();
  l.draw(rr, atlas, {0, 0});
  CHECK(rr.calls()[0].vertices[0].color.g == doctest::Approx(0.5f));

  // bold widens the outline's stroke as well
  auto quadW = [&](std::string_view text, float os) {
    rr.clear();
    rr.setOutputScale(os);
    TextStyle big = s;
    big.size = 80;
    TextLayout::build(text, big, 400, 200).draw(rr, atlas, {0, 0});
    return rr.calls()[0].vertices[1].pos.x - rr.calls()[0].vertices[0].pos.x;  // first call is the outline layer
  };
  CHECK(quadW("<b><outline size=2>a</outline></b>", 2) > quadW("<outline size=2>a</outline>", 2) + 0.25f);

  // a stray continuation byte is its own code point
  GlyphSpec w, hi;
  w.cp = 'W';
  w.advance = 60;
  w.explicitBounds = true;
  w.minY = -75;
  w.maxY = 25;
  hi.cp = 0x80;
  hi.advance = 60;
  auto f80 = Font::fromBytes(buildMvec({w, hi}), "f80");
  TextStyle s80 = style();
  s80.font = f80;
  auto g = TextLayout::build("\x80", s80, 100, 100).glyphPositions();
  REQUIRE(g.size() == 1);
  CHECK(g[0].cp == 0x80);

  // snapping uses absolute device coordinates: with the letterbox origin at 213.5 the quads' device edges are whole
  rr.setOutputScale(1.92f);
  rr.setOutputOrigin({213.5f, 0});
  rr.clear();
  TextLayout::build("abc", s, 200, 100).draw(rr, atlas, {10.3f, 5.1f});
  for (auto& v : rr.calls()[0].vertices) {
    const float dx = v.pos.x * 1.92f + 213.5f, dy = v.pos.y * 1.92f;
    CHECK(dx == doctest::Approx(std::round(dx)).epsilon(1e-4));
    CHECK(dy == doctest::Approx(std::round(dy)).epsilon(1e-4));
  }
  // non-square output pixels rasterise each axis at its own scale
  rr.setOutputScale(2);
  rr.setOutputOrigin({0, 0});
  rr.setYRatio(1.5f);
  rr.clear();
  TextLayout::build("a", s, 200, 100).draw(rr, atlas, {0, 0});
  const auto& v = rr.calls()[0].vertices;
  CHECK(v[2].pos.y - v[0].pos.y == doctest::Approx(12.0f).epsilon(0.1));  // same logical size, more rows of pixels
  CHECK(v[1].pos.x - v[0].pos.x == doctest::Approx(5.76f).epsilon(0.15));
  rr.setYRatio(1.0f);
}

TEST_CASE("glyph bitmaps are rasterised at output resolution") {
  GlyphSpec w;
  w.cp = 'W';
  w.advance = 80;
  w.contours = {rect(0, -100, 50, 0)};
  auto f = Font::fromBytes(buildMvec({w}), "dev");
  REQUIRE(f);
  FakeUploader up;
  GlyphAtlas atlas(&up);
  RasterParams p;
  p.scale = 0.1f;
  auto e1 = atlas.get(*f, 0, p);
  p.scale = 0.2f;  // the same text at output scale 2
  auto e2 = atlas.get(*f, 0, p);
  CHECK(e1.w == 5);
  CHECK(e2.w == 10);
  CHECK(e2.h == 20);
}

TEST_CASE("layout: a leading newline is an empty counted line of normal height") {
  // Chocolatier 2's messages text starts with a '#' that the label strip turns into a leading newline (docs/re/choc2-ledger-binary.md 12.1).
  auto l = TextLayout::build("\nfirst\nsecond", style(), 200, 200);
  CHECK(l.lineCount() == 3);
  auto g = l.glyphPositions();
  REQUIRE_FALSE(g.empty());
  CHECK(g.front().line == 1);                                        // line 0 has no glyphs
  CHECK(g.front().y == doctest::Approx(20 + 15.0f));                 // 'first' sits one line height (20) down
  CHECK(g.back().y == doctest::Approx(g.front().y + 20));
}

TEST_CASE("layout: a clip height taller than the window keeps the last line of centred overflowing text") {
  TextStyle s = style();
  s.flags = 5;  // vertically centred
  const std::string text = "a b c d e f";
  // narrow window and short height: many lines, centred, so the text runs past the window bottom
  const auto dflt = TextLayout::build(text, s, 30, 60);
  const int n = dflt.lineCount();
  REQUIRE(n == 6);
  CHECK(dflt.drawnLineCount() < n);  // default: the window height is the clip
  const auto same = TextLayout::build(text, s, 30, 60, 60);
  CHECK(same.drawnLineCount() == dflt.drawnLineCount());
  const auto tall = TextLayout::build(text, s, 30, 60, 200);
  CHECK(tall.lineCount() == n);
  CHECK(tall.drawnLineCount() > dflt.drawnLineCount());
  CHECK(tall.drawnLineCount() >= n - 2);  // only lines that end above the top are skipped
}
