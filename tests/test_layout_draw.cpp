#include <doctest/doctest.h>

#include <memory>

#include "ScriptHarness.h"
#include "gfx/RecordingRenderer.h"
#include "gfx/TextureCache.h"
#include "script/Formats.h"
#include "script/ScriptHost.h"
#include "ui/DrawContext.h"
#include "ui/Layout.h"
#include "ui/WindowManager.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
using ui::kCenter;
using ui::kMax;
using Kind = gfx::RecordingRenderer::Call::Kind;

namespace {

ui::AxisIn axis(int pos, int size, int d = 800) {
  ui::AxisIn a;
  a.pos = pos;
  a.size = size;
  a.parentDim = d;
  return a;
}

// Creates a window of `type` under `parent` the way GenericCreate does, with a MapStyle.
ui::Window& make(ui::WindowManager& wm, ui::Window& parent, const std::string& type, const ui::MapStyle& style) {
  auto w = wm.createWindow(type);
  ui::Window& ref = parent.adoptFront(std::move(w));
  ref.init(style);
  return ref;
}

ui::MapStyle rectStyle(const char* name, double x, double y, double w, double h, std::vector<double> color) {
  ui::MapStyle s;
  s.set("name", std::string(name)).set("x", x).set("y", y).set("w", w).set("h", h).set("color", std::move(color));
  return s;
}

struct Env : test::ScriptEnv {
  Env() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ui/modal"), gfxFs(CHOCO_GFX_ASSETS, ""), cache(gfxFs, nullptr), host(ctx) {
    wm.setTextures(&cache);
    booted = host.boot();
  }
  assets::AssetFS gfxFs;
  gfx::TextureCache cache;
  script::ScriptHost host;
  bool booted = false;
};

}  // namespace

TEST_CASE("resolveAxis: literal positions, negatives and the align bits") {
  CHECK(ui::resolveAxis(axis(100, 50)).pos == 100);
  CHECK(ui::resolveAxis(axis(100, 50)).size == 50);
  // A negative position measures from the far edge unless align was given.
  CHECK(ui::resolveAxis(axis(-10, 50)).pos == 790);
  ui::AxisIn noWrap = axis(-10, 50);
  noWrap.noWrap = true;
  CHECK(ui::resolveAxis(noWrap).pos == -10);
  // Align centre and right work on literal positions; both leave a zero size alone.
  ui::AxisIn c = axis(100, 50);
  c.alignCenter = true;
  CHECK(ui::resolveAxis(c).pos == 75);
  ui::AxisIn r = axis(100, 50);
  r.alignFar = true;
  CHECK(ui::resolveAxis(r).pos == 50);
  c.size = 0;
  CHECK(ui::resolveAxis(c).pos == 100);
  // Without a parent only the align step runs.
  ui::AxisIn np = axis(-10, 50);
  np.hasParent = false;
  CHECK(ui::resolveAxis(np).pos == -10);
}

TEST_CASE("resolveAxis: kCenter, kMax and fill sizes") {
  CHECK(ui::resolveAxis(axis(int(kCenter), 200)).pos == 300);       // centred in 800
  CHECK(ui::resolveAxis(axis(int(kCenter) + 10, 200)).pos == 310);  // centred, then shifted
  CHECK(ui::resolveAxis(axis(int(kCenter) - 10, 200)).pos == 290);
  CHECK(ui::resolveAxis(axis(int(kMax) - 10, 100)).pos == 790);     // 10 px in from the far edge
  // w = kMax fills from x; kMax - n stops n short.
  auto fill = ui::resolveAxis(axis(20, int(kMax)));
  CHECK(fill.pos == 20);
  CHECK(fill.size == 780);
  CHECK(ui::resolveAxis(axis(20, int(kMax) - 40)).size == 740);
  CHECK(ui::resolveAxis(axis(-20, int(kMax))).size == 20 + 0);  // wraps to 780, then 800 - 780
  // Centre band with a fill size.
  auto cf = ui::resolveAxis(axis(int(kCenter), int(kMax)));
  CHECK(cf.pos == 0);
  CHECK(cf.size == 800);
  auto cn = ui::resolveAxis(axis(int(kCenter) + 30, int(kMax)));
  CHECK(cn.pos == 60);
  CHECK(cn.size == 740);
  // Vertical dimension is just another parent dimension.
  CHECK(ui::resolveAxis(axis(int(kCenter), 281, 600)).pos == 160);
}

TEST_CASE("resolveAxis: negative sizes make the position the far edge") {
  auto a = ui::resolveAxis(axis(300, -100));
  CHECK(a.size == 100);
  CHECK(a.pos == 200);
  // With a negative position the wrap rule applies first (790), then the size is taken off.
  auto b = ui::resolveAxis(axis(-10, -100));
  CHECK(b.size == 100);
  CHECK(b.pos == 690);
  ui::AxisIn nw = axis(-10, -100);
  nw.noWrap = true;
  CHECK(ui::resolveAxis(nw).pos == -110);
  // Very negative sizes only have to be harmless.
  CHECK_NOTHROW(ui::resolveAxis(axis(10, -90000)));
}

TEST_CASE("layout: sizeAndPositionFromStyle on a window under a parent") {
  ui::WindowManager wm;
  ui::Window& root = *wm.screen();
  ui::MapStyle a;
  a.set("x", kCenter).set("y", 10.0).set("w", 200.0).set("h", 100.0);
  CHECK(make(wm, root, "Window", a).rect() == gfx::RectI{300, 10, 200, 100});

  ui::MapStyle b;  // fills the parent
  b.set("x", 0.0).set("y", 0.0).set("w", kMax).set("h", kMax);
  CHECK(make(wm, root, "Window", b).rect() == gfx::RectI{0, 0, 800, 600});

  ui::MapStyle c;  // align centre on a literal position, bottom align on y
  c.set("x", 100.0).set("y", 100.0).set("w", 50.0).set("h", 20.0).set("align", 1.0 + 8.0);
  CHECK(make(wm, root, "Window", c).rect() == gfx::RectI{75, 80, 50, 20});

  ui::MapStyle d;  // with align given, a negative position stays negative
  d.set("x", -10.0).set("y", -10.0).set("w", 5.0).set("h", 5.0).set("align", 0.0);
  CHECK(make(wm, root, "Window", d).rect() == gfx::RectI{-10, -10, 5, 5});
  ui::MapStyle e;  // without align it wraps
  e.set("x", -10.0).set("y", -10.0).set("w", 5.0).set("h", 5.0);
  CHECK(make(wm, root, "Window", e).rect() == gfx::RectI{790, 590, 5, 5});

  // x above the centre band with w == 0 is skipped entirely.
  ui::MapStyle f;
  f.set("x", kCenter).set("y", 7.0).set("w", 0.0).set("h", 4.0);
  CHECK(make(wm, root, "Window", f).rect() == gfx::RectI{0, 0, 0, 0});

  // The test is unsigned in the original, so a negative x with w == 0 also returns before either axis is resolved.
  ui::MapStyle g;
  g.set("x", -10.0).set("y", 7.0).set("w", 0.0).set("h", 4.0).set("align", 2.0);
  CHECK(make(wm, root, "Window", g).rect() == gfx::RectI{0, 0, 0, 0});
  ui::MapStyle g2;  // the same for y
  g2.set("x", 7.0).set("y", -10.0).set("w", 4.0).set("h", 0.0).set("align", 8.0);
  CHECK(make(wm, root, "Window", g2).rect() == gfx::RectI{0, 0, 0, 0});
}

TEST_CASE("layout: a parent of width 0 skips relative values; a second pass changes nothing") {
  ui::WindowManager wm;
  ui::MapStyle p;
  p.set("x", 0.0).set("y", 0.0);  // no size
  ui::Window& parent = make(wm, *wm.screen(), "Window", p);
  CHECK(parent.rect().w == 0);
  ui::MapStyle rel;
  rel.set("x", -5.0).set("y", 3.0).set("w", 10.0).set("h", 10.0);
  CHECK(make(wm, parent, "Window", rel).rect() == gfx::RectI{0, 0, 0, 0});  // x < 0: whole call skipped

  ui::MapStyle q;
  q.set("x", 10.0).set("y", 20.0).set("w", 100.0).set("h", 50.0);
  ui::Window& w = make(wm, *wm.screen(), "Window", q);
  ui::MapStyle again;
  again.set("x", 99.0).set("y", 99.0).set("w", 5.0).set("h", 5.0);
  w.postChildrenInit(again);
  CHECK(w.rect() == gfx::RectI{10, 20, 100, 50});
}

TEST_CASE("fitToChildren only grows, counts hidden children, ignores left/top and moves nothing") {
  ui::WindowManager wm;
  ui::MapStyle ps;
  ps.set("x", 0.0).set("y", 0.0).set("w", 100.0).set("h", 50.0);
  ui::Window& parent = make(wm, *wm.screen(), "Window", ps);
  ui::MapStyle k1;
  k1.set("x", 90.0).set("y", 10.0).set("w", 40.0).set("h", 10.0);  // right edge 130
  ui::Window& c1 = make(wm, parent, "Window", k1);
  ui::MapStyle k2;
  k2.set("x", 5.0).set("y", 60.0).set("w", 10.0).set("h", 30.0);  // bottom edge 90
  ui::Window& c2 = make(wm, parent, "Window", k2);
  c2.setShown(false);
  ui::fitToChildren(parent);
  CHECK(parent.rect() == gfx::RectI{0, 0, 130, 90});
  CHECK(c1.rect().x == 90);
  CHECK(c2.rect().y == 60);
  c1.setSize(1, 1);
  c2.setSize(1, 1);
  ui::fitToChildren(parent);  // never shrinks
  CHECK(parent.rect() == gfx::RectI{0, 0, 130, 90});

  // fit = true in PostChildrenInit, with the style's smaller explicit size not winning.
  ui::MapStyle gs;
  gs.set("x", 0.0).set("y", 0.0).set("w", 10.0).set("h", 10.0);
  ui::Window& grp = make(wm, *wm.screen(), "Window", gs);
  ui::MapStyle gk;
  gk.set("x", 0.0).set("y", 0.0).set("w", 70.0).set("h", 30.0);
  make(wm, grp, "Window", gk);
  ui::MapStyle post = gs;
  post.set("fit", 1.0);
  grp.postChildrenInit(post);
  CHECK(grp.rect() == gfx::RectI{0, 0, 70, 30});
}

TEST_CASE("SetPosition wraps negatives with the current size; SetWindowPos is raw") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('pos'); GenericCreate({ typename = 'Window', name = 'w', x = 1, y = 1, w = 100, h = 50 })") == 0);
  REQUIRE(e.run("SetPosition('w', kCenter, -20)") == 0);
  ui::Window* w = e.wm.findInTopModal("w");
  REQUIRE(w);
  CHECK(w->rect().x == 350);  // 80000 - 80000 + (800/2 - 100/2)
  CHECK(w->rect().y == 580);
  REQUIRE(e.run("SetWindowPos('w', 7.9, -3)") == 0);
  CHECK(w->rect().x == 7);
  CHECK(w->rect().y == -3);
  CHECK(w->rect().w == 100);
  REQUIRE(e.run("SetAlpha('w', 51)") == 0);
  CHECK(w->alpha() == 1.0f);  // a plain Window is not one of the types the fader sets alpha on
  REQUIRE(e.run("GenericCreate({ typename = 'Window', name = 'kid', x = 0, y = 0, w = 300, h = 200 }); PopWindow(); DoFitToChildren()") == 0);
  CHECK(e.wm.creationTop()->rect().w >= 300);
}

TEST_CASE("draw: children go last to first, offsets accumulate, hidden windows are skipped") {
  ui::WindowManager wm;
  ui::Window& root = *wm.screen();
  ui::Window& parent = make(wm, root, "Rectangle", rectStyle("p", 100, 50, 300, 200, {1, 0, 0, 1}));
  parent.postChildrenInit(rectStyle("p", 100, 50, 300, 200, {1, 0, 0, 1}));
  auto addRect = [&](const char* n, double x, double y, std::vector<double> col) -> ui::Window& {
    ui::Window& w = make(wm, parent, "Rectangle", rectStyle(n, x, y, 40, 30, col));
    w.postChildrenInit(rectStyle(n, x, y, 40, 30, col));
    return w;
  };
  addRect("first", 10, 20, {0, 1, 0, 1});   // created first: at the back
  addRect("second", 30, 40, {0, 0, 1, 1});  // created second: in front
  ui::Window& hidden = addRect("third", 50, 60, {1, 1, 0, 1});
  hidden.setShown(false);

  gfx::RecordingRenderer rr;
  wm.draw(rr);
  std::vector<gfx::RecordingRenderer::Call> fills;
  for (const auto& c : rr.calls())
    if (c.kind == Kind::FillRect) fills.push_back(c);
  REQUIRE(fills.size() == 3);
  CHECK(fills[0].dst == gfx::RectF{100, 50, 300, 200});  // the parent first
  CHECK(fills[1].dst == gfx::RectF{110, 70, 40, 30});    // then the back child ("first")
  CHECK(fills[1].color.g == 1);
  CHECK(fills[2].dst == gfx::RectF{130, 90, 40, 30});    // the front child last, so it ends on top
  CHECK(fills[2].color.b == 1);
  // Each window pushes and pops its own clip, nested.
  CHECK(rr.count(Kind::PushClip) == rr.count(Kind::PopClip));
  CHECK(fills[2].clipped);
  CHECK(fills[2].activeClip == gfx::RectI{130, 90, 40, 30});
}

TEST_CASE("draw: a child is clipped to its parent, and a window outside the clip is skipped") {
  ui::WindowManager wm;
  ui::Window& parent = make(wm, *wm.screen(), "Rectangle", rectStyle("p", 0, 0, 100, 100, {1, 1, 1, 1}));
  parent.postChildrenInit(rectStyle("p", 0, 0, 100, 100, {1, 1, 1, 1}));
  ui::Window& far = make(wm, parent, "Rectangle", rectStyle("far", 200, 200, 50, 50, {1, 0, 0, 1}));
  far.postChildrenInit(rectStyle("far", 200, 200, 50, 50, {1, 0, 0, 1}));
  ui::Window& edge = make(wm, parent, "Rectangle", rectStyle("edge", 80, 80, 50, 50, {0, 1, 0, 1}));
  edge.postChildrenInit(rectStyle("edge", 80, 80, 50, 50, {0, 1, 0, 1}));
  gfx::RecordingRenderer rr;
  wm.draw(rr);
  CHECK(rr.count(Kind::FillRect) == 2);  // "far" never draws
  for (const auto& c : rr.calls())
    if (c.kind == Kind::FillRect && c.color.g == 1 && c.color.r == 0) CHECK(c.activeClip == gfx::RectI{80, 80, 20, 20});
}

TEST_CASE("draw: each window uses its own alpha, and a Rectangle's alpha key replaces its colour alpha") {
  ui::WindowManager wm;
  ui::Window& parent = make(wm, *wm.screen(), "Rectangle", rectStyle("p", 0, 0, 100, 100, {1, 1, 1, 1}));
  parent.postChildrenInit(rectStyle("p", 0, 0, 100, 100, {1, 1, 1, 1}));
  ui::MapStyle ks = rectStyle("k", 10, 10, 20, 20, {1, 0, 0, 0.8});
  ui::Window& kid = make(wm, parent, "Rectangle", ks);
  kid.postChildrenInit(ks);  // colour alpha 0.8
  ui::MapStyle k2 = rectStyle("k2", 40, 40, 20, 20, {1, 0, 0, 0.8});
  k2.set("alpha", 0.5);
  ui::Window& kid2 = make(wm, parent, "Rectangle", k2);
  kid2.postChildrenInit(k2);
  parent.setAlpha(0.5f);
  kid.setAlpha(0.5f);
  gfx::RecordingRenderer rr;
  wm.draw(rr);
  std::vector<float> alphas;
  for (const auto& c : rr.calls())
    if (c.kind == Kind::FillRect) alphas.push_back(c.color.a);
  REQUIRE(alphas.size() == 3);
  CHECK(alphas[0] == doctest::Approx(0.5f));  // parent: its own 0.5
  CHECK(alphas[1] == doctest::Approx(0.4f));  // kid (back, drawn first): 0.8 * its own 0.5, not the parent's
  CHECK(alphas[2] == doctest::Approx(0.5f));  // kid2: the alpha key; the parent's 0.5 is not applied
}

TEST_CASE("SetAlpha sets the alpha of Text and Bitmap in the subtree, not Rectangle or plain windows; BitmapTint ignores it") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('al'); GenericCreate({ typename = 'Window', name = 'box', x = 0, y = 0, w = 300, h = 300 });"
                "GenericCreate({ typename = 'Bitmap', name = 'bm', image = 'image/white', x = 0, y = 0 }); PopWindow();"
                "GenericCreate({ typename = 'Text', name = 'tx', label = '#hi', x = 0, y = 0, w = 50, h = 20 }); PopWindow();"
                "GenericCreate({ typename = 'Rectangle', name = 'rc', x = 0, y = 0, w = 20, h = 20, color = Color(255, 0, 0) });"
                "PostCreate({ x = 0, y = 0, w = 20, h = 20, color = Color(255, 0, 0) }, 'Rectangle'); PopWindow();"
                "GenericCreate({ typename = 'BitmapTint', name = 'bt', image = 'image/white', x = 0, y = 0, "
                "tint = Color(255, 255, 255, 255) }); PopWindow(); PopWindow()") == 0);
  REQUIRE(e.run("SetAlpha('box', 51)") == 0);
  auto alphaOf = [&](const char* n) { return e.wm.findInTopModal(n)->alpha(); };
  CHECK(alphaOf("box") == 1.0f);
  CHECK(alphaOf("bm") == doctest::Approx(0.2f));
  CHECK(alphaOf("tx") == doctest::Approx(0.2f));
  CHECK(alphaOf("rc") == 1.0f);
  gfx::RecordingRenderer rr;
  e.wm.draw(rr);
  int tints = 0;
  for (const auto& c : rr.calls())
    if (c.kind == Kind::DrawTexture && c.color.r == 1 && c.color.a != doctest::Approx(0.2f)) ++tints;
  CHECK(tints >= 1);  // the BitmapTint kept its full tint alpha
  // setSubtreeAlpha on the Bitmap directly.
  ui::setSubtreeAlpha(*e.wm.findInTopModal("bm"), 0.75f);
  CHECK(alphaOf("bm") == doctest::Approx(0.75f));
}

TEST_CASE("Bitmap: loads through the texture cache, sizes to the image, scales and flips") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('bm')") == 0);
  REQUIRE(e.run("GenericCreate({ typename = 'Bitmap', name = 'a', image = 'image/checker', x = 5, y = 6 }); PopWindow()") == 0);
  REQUIRE(e.run("GenericCreate({ typename = 'Bitmap', name = 'b', image = 'image/checker', scale = 0.5, hflip = true, "
                "x = kCenter, y = 10, flags = 99 }); PopWindow()") == 0);
  REQUIRE(e.run("GenericCreate({ typename = 'Bitmap', name = 'c', image = 'image/checker', w = 7, h = 9 }); PopWindow()") == 0);
  REQUIRE(e.run("GenericCreate({ typename = 'Bitmap', name = 'missing', image = 'image/nope' }); PopWindow()") == 0);
  auto tex = e.cache.load("image/checker");
  REQUIRE(tex);
  const int tw = tex->width(), th = tex->height();
  auto* a = static_cast<ui::BitmapWindow*>(e.wm.findInTopModal("a"));
  auto* b = static_cast<ui::BitmapWindow*>(e.wm.findInTopModal("b"));
  auto* c = static_cast<ui::BitmapWindow*>(e.wm.findInTopModal("c"));
  REQUIRE(a);
  REQUIRE(b);
  REQUIRE(c);
  CHECK(a->rect() == gfx::RectI{5, 6, tw, th});
  CHECK(b->rect().w == tw / 2);
  CHECK(b->rect().x == 400 - tw / 4);  // kCenter centres the scaled width in the 800-wide modal
  CHECK(c->rect().w == tw);  // style w/h never resize or crop a loaded image
  CHECK(c->rect().h == th);
  e.run("GenericCreate({ typename = 'Bitmap', name = 'notex', image = 'image/nope', w = 7, h = 9 }); PopWindow()");
  CHECK(e.wm.findInTopModal("notex")->rect().w == 7);  // with no texture the style size applies
  CHECK(e.wm.findInTopModal("notex")->rect().h == 9);
  // ImageLoaded: true for a resolved image, false for a missing one or a window with no image.
  REQUIRE(e.run("gA = ImageLoaded('a'); gM = ImageLoaded('missing'); gN = ImageLoaded('nonesuch')") == 0);
  CHECK(e.getString("gA") == "true");
  CHECK(e.getString("gM") == "false");
  CHECK(e.getString("gN") == "false");

  gfx::RecordingRenderer rr;
  e.wm.draw(rr);
  std::vector<gfx::RecordingRenderer::Call> draws;
  for (const auto& call : rr.calls())
    if (call.kind == Kind::DrawTexture) draws.push_back(call);
  REQUIRE(draws.size() == 3);  // a, b, c; the missing image draws nothing
  // Later windows are in front and so draw later: c, b, a is the order of creation reversed in the list, and drawing
  // goes last to first, i.e. a first.
  CHECK(draws[0].dst == gfx::RectF{5, 6, float(tw), float(th)});
  CHECK(draws[1].hflip);
  CHECK(draws[1].dst.w == float(tw / 2));
  // _SetBitmap swaps the texture and rescales.
  REQUIRE(e.run("_SetBitmap('a', 'image/white', 1)") == 0);
  auto white = e.cache.load("image/white");
  REQUIRE(white);
  CHECK(a->rect().w == white->width());
}

TEST_CASE("BitmapTint: tint multiplies, w/h resize, Rectangle and tint use Color tables from the prelude") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('bt')") == 0);
  REQUIRE(e.run("GenericCreate({ typename = 'BitmapTint', name = 't', image = 'image/white', x = 0, y = 0, "
                "tint = Color(255, 128, 0, 51), w = 30, h = 20 }); PopWindow()") == 0);
  REQUIRE(e.run("GenericCreate({ typename = 'Rectangle', name = 'r', x = 100, y = 100, w = 10, h = 10, "
                "color = Color(0, 255, 0) }); PostCreate({ x = 100, y = 100, w = 10, h = 10, color = Color(0, 255, 0) }, 'Rectangle')") == 0);
  auto* t = static_cast<ui::BitmapTintWindow*>(e.wm.findInTopModal("t"));
  REQUIRE(t);
  CHECK(t->rect().w == 30);
  CHECK(t->rect().h == 20);
  gfx::RecordingRenderer rr;
  e.wm.draw(rr);
  bool sawTint = false, sawRect = false;
  for (const auto& c : rr.calls()) {
    if (c.kind == Kind::DrawTexture) {
      sawTint = true;
      CHECK(c.color.r == doctest::Approx(1.0f));
      CHECK(c.color.g == doctest::Approx(128 / 255.0f));
      CHECK(c.color.a == doctest::Approx(1.0f));  // the tint alpha (0.2 here) is ignored, see BitmapTintWindow::draw
    }
    if (c.kind == Kind::FillRect) {
      sawRect = true;
      CHECK(c.color.g == doctest::Approx(1.0f));
      CHECK(c.color.r == doctest::Approx(0.0f));
    }
  }
  CHECK(sawTint);
  CHECK(sawRect);

  // dx/dy are the image centre in window space times scale; absent axes centre on half the window size.
  REQUIRE(e.run("PushModal('bt2'); GenericCreate({ typename = 'BitmapTint', name = 'c', image = 'image/white', x = 200, y = 200, "
                "dx = 50, dy = 40, scale = 2 }); PopWindow();"
                "GenericCreate({ typename = 'BitmapTint', name = 'd', image = 'image/white', x = 300, y = 300, "
                "w = 40, h = 20 }); PopWindow()") == 0);
  auto white = e.cache.load("image/white");
  REQUIRE(white);
  const float iw = float(white->width()), ih = float(white->height());
  gfx::RecordingRenderer rr2;
  e.wm.draw(rr2);
  std::vector<gfx::RectF> dsts;
  for (const auto& c : rr2.calls())
    if (c.kind == Kind::DrawTexture) dsts.push_back(c.dst);
  REQUIRE(dsts.size() == 3);  // t, c, d in creation order reversed: drawn back to front
  // c: scale 2, so the image is 2x and centred at (100, 80) from the window origin (200, 200).
  bool sawC = false, sawD = false;
  for (const auto& r : dsts) {
    if (r.w == iw * 2) {
      sawC = true;
      CHECK(r.x == doctest::Approx(200 + 100 - iw));
      CHECK(r.y == doctest::Approx(200 + 80 - ih));
    } else if (r.x > 250) {
      sawD = true;  // d: window 40x20, so the centre is (20, 10)
      CHECK(r.x == doctest::Approx(300 + 20 - iw / 2));
      CHECK(r.y == doctest::Approx(300 + 10 - ih / 2));
    }
  }
  CHECK(sawC);
  CHECK(sawD);
}

TEST_CASE("Floor, Pow, Mod, Dollars, Date and DayOfYear natives") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("a = Floor(-2.5); b = Pow(2, 10); c = Mod(10, 3); d = Mod(-5, 3); f = Mod(5, 0); g = Mod(1e10, 7); "
                "h = Dollars(1234567); i = Dollars(-1234.6); j = Dollars(0.4); m = Dollars(0/0); k = DayOfYear(100); l = DayOfYear(-4)") == 0);
  CHECK(e.getNumber("a") == -3);
  CHECK(e.getNumber("b") == 1024);
  CHECK(e.getNumber("c") == 1);
  CHECK(e.getNumber("d") == 0);
  CHECK(e.getNumber("f") == 0);
  CHECK(e.getNumber("g") == 3);  // 4294967295 % 7
  CHECK(e.getString("h") == "$1,234,567");
  CHECK(e.getString("i") == "-$1,235");
  CHECK(e.getString("j") == "$0");
  CHECK(e.getString("m") == "$0");
  CHECK(e.getNumber("k") == 700 % 365);
  CHECK(e.getNumber("l") == 0);
  CHECK(e.stubs.count("Floor") == 0);
}

TEST_CASE("formats: Dollars and Mod follow the documented formulas") {
  CHECK(script::formatDollars(999, "$", ",") == "$999");
  CHECK(script::formatDollars(1000, "$", ",") == "$1,000");
  CHECK(script::formatDollars(-1000000, "$", ",") == "-$1,000,000");
  CHECK(script::formatDollars(12345.4, "EUR ", ".") == "EUR 12.345");
  CHECK(script::formatDollars(-0.49, "$", ",") == "$0");
  CHECK(script::formatDollars(std::nan(""), "$", ",") == "$0");
  bool zero = false;
  CHECK(script::modNative(7, 0, &zero) == 0);
  CHECK(zero);
  CHECK(script::modNative(4294967296.0 * 4, 1000) == 295);  // 4294967295 % 1000
}
