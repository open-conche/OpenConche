#include <doctest/doctest.h>

#include <cmath>

#include "ScriptHarness.h"
#include "assets/AssetFS.h"
#include "gfx/TextureCache.h"
#include "ui/widgets/Widgets.h"
#include "gfx/Curve.h"
#include "script/ScriptHost.h"
#include "ui/DrawContext.h"
#include "ui/Transition.h"
#include "ui/WindowManager.h"

using namespace choco;

namespace {

struct Env : test::ScriptEnv {
  Env() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ui/modal"), host(ctx) {
    booted = host.boot();
    wm.setLogger([this](const std::string& line) { log.write(line); });
    // A modal "tr" holding a container "x" (0,5 100x50) with a Text child, and a bare "y" with no fader-capable content.
    REQUIRE(run("PushModal('tr'); gEnd = 0; gOrder = ''; "
                "GenericCreate({ typename = 'Window', name = 'x', x = 0, y = 5, w = 100, h = 50 }); "
                "GenericCreate({ typename = 'Text', name = 'tx', label = '#hi', x = 0, y = 0, w = 50, h = 20 }); "
                "PopWindow(); PopWindow(); "
                "GenericCreate({ typename = 'Window', name = 'y', x = 0, y = 0, w = 10, h = 10 }); PopWindow()") == 0);
  }
  ui::Window& x() { return *wm.findInTopModal("x"); }
  float textAlpha() { return wm.findInTopModal("tx")->alpha(); }
  // Runs one transition call at script time `now` and steps the transitions at `at` ms.
  void tickAt(double at) { clock.nowMs = at; wm.tickTransitions(at); }
  script::ScriptHost host;
  bool booted = false;
};

}  // namespace

TEST_CASE("Transition fadein: alpha 0 at once, ramps to alpha, onend once at the end") {
  Env e;
  REQUIRE(e.booted);
  e.clock.nowMs = 1000;
  REQUIRE(e.run("Transition{ 'fadein', window = 'x', time = 200, alpha = 0.5, onend = function() gEnd = gEnd + 1 end }") == 0);
  CHECK(e.textAlpha() == 0.0f);  // t = 0, before any tick
  CHECK(e.wm.transitionCount() == 1);
  e.tickAt(1100);
  CHECK(e.textAlpha() == doctest::Approx(0.25f));
  CHECK(e.getNumber("gEnd") == 0);
  e.tickAt(1200);
  CHECK(e.textAlpha() == doctest::Approx(0.5f));
  CHECK(e.getNumber("gEnd") == 1);
  e.tickAt(1300);
  CHECK(e.getNumber("gEnd") == 1);  // exactly once
  CHECK(e.wm.transitionCount() == 0);
}

TEST_CASE("Transition fadeout: starts at alpha, ramps down to 0") {
  Env e;
  REQUIRE(e.booted);
  e.clock.nowMs = 0;
  REQUIRE(e.run("Transition{ 'fadeout', window = 'x', time = 400 }") == 0);
  CHECK(e.textAlpha() == 1.0f);  // nothing applied until the first step
  e.tickAt(100);
  CHECK(e.textAlpha() == doctest::Approx(0.75f));
  e.tickAt(400);
  CHECK(e.textAlpha() == 0.0f);
}

TEST_CASE("Transition swipe: moves start to end with truncation; defaults to the window position") {
  Env e;
  REQUIRE(e.booted);
  e.clock.nowMs = 0;
  REQUIRE(e.run("Transition{ 'swipe', window = 'x', time = 100, startx = 1000, endx = 0, starty = 5, endy = 55 }") == 0);
  e.tickAt(0);  // t = 0
  CHECK(e.x().rect().x == 1000);
  CHECK(e.x().rect().y == 5);
  e.tickAt(50);
  CHECK(e.x().rect().x == 500);
  CHECK(e.x().rect().y == 30);
  e.tickAt(100);
  CHECK(e.x().rect().x == 0);
  CHECK(e.x().rect().y == 55);
  // Only endx given: y keeps the window's own value on both ends.
  REQUIRE(e.run("Transition{ 'swipe', window = 'x', time = 10, endx = 300 }") == 0);
  e.tickAt(100);
  CHECK(e.x().rect().x == 0);  // start = the current x
  e.tickAt(110);
  CHECK(e.x().rect().x == 300);
  CHECK(e.x().rect().y == 55);
}

TEST_CASE("Transition path: follows the Bezier chain by arc length") {
  Env e;
  REQUIRE(e.booted);
  e.clock.nowMs = 0;
  // One straight segment from (0,0) to (300,0): the point at half the time is the middle.
  REQUIRE(e.run("Transition{ 'path', window = 'x', time = 100, path = { {0,0}, {100,0}, {200,0}, {300,0} } }") == 0);
  e.tickAt(50);
  CHECK(e.x().rect().x == doctest::Approx(150).epsilon(0.01));
  e.tickAt(100);
  CHECK(e.x().rect().x == 300);
  CHECK(e.x().rect().y == 0);
  gfx::Curve c;
  c.addControlPoint(0, 0);
  c.addControlPoint(100, 0);
  c.addControlPoint(200, 0);
  c.addControlPoint(300, 0);
  c.addControlPoint(300, 100);
  c.addControlPoint(300, 200);
  c.addControlPoint(300, 300);
  CHECK(c.length() == doctest::Approx(600).epsilon(0.01));
  CHECK(c.at(0.5f).x == doctest::Approx(300).epsilon(0.01));
  CHECK(c.at(0.5f).y == doctest::Approx(0).epsilon(0.05));
  CHECK(c.at(1).y == 300);
  // No path table: nothing starts and onend is not called.
  REQUIRE(e.run("Transition{ 'path', window = 'x', onend = function() gEnd = gEnd + 1 end }") == 0);
  CHECK(e.wm.transitionCount() == 0);
}

TEST_CASE("Transition center_fadein: hides, centres, fades; center only moves and drops onend") {
  Env e;
  REQUIRE(e.booted);
  e.clock.nowMs = 0;
  REQUIRE(e.run("Transition{ 'center_fadein', window = 'x', time = 100, onend = function() gEnd = gEnd + 1 end }") == 0);
  CHECK_FALSE(e.x().shown());
  CHECK(e.textAlpha() == 0.0f);
  CHECK(e.x().rect().x == 350);  // (800 - 100) / 2
  CHECK(e.x().rect().y == 5);
  e.tickAt(50);
  CHECK(e.x().shown());  // every step shows it
  CHECK(e.textAlpha() == doctest::Approx(0.5f));
  e.tickAt(100);
  CHECK(e.textAlpha() == 1.0f);
  CHECK(e.getNumber("gEnd") == 1);

  REQUIRE(e.run("Transition{ 'center', window = 'y', onend = function() gEnd = gEnd + 10 end }") == 0);
  CHECK(e.wm.findInTopModal("y")->rect().x == 395);
  CHECK(e.wm.transitionCount() == 0);
  e.tickAt(500);
  CHECK(e.getNumber("gEnd") == 1);  // center never calls onend
}

TEST_CASE("Transition zoomin: a Bitmap is texture x t at each tick, full size on the last frame, onend once") {
  Env e;
  REQUIRE(e.booted);
  assets::AssetFS gfxFs(CHOCO_GFX_ASSETS, "");
  gfx::TextureCache cache(gfxFs, nullptr);
  e.wm.setTextures(&cache);
  REQUIRE(e.run("GenericCreate({ typename = 'Bitmap', name = 'bm', image = 'image/checker', x = 30, y = 40 }); PopWindow()") == 0);
  auto* bm = static_cast<ui::BitmapWindow*>(e.wm.findInTopModal("bm"));
  REQUIRE(bm);
  const int tw = bm->rect().w, th = bm->rect().h;
  REQUIRE(tw > 4);
  e.clock.nowMs = 1000;
  REQUIRE(e.run("Transition{ 'zoomin', window = 'bm', time = 100, onend = function() gEnd = gEnd + 1 end }") == 0);
  CHECK(e.log.count("[unimplemented]") == 0);
  for (int ms = 25; ms < 100; ms += 25) {
    e.tickAt(1000 + ms);
    const double t = ms / 100.0;
    CHECK(bm->rect().w == static_cast<int>(std::lround(tw * t)));
    CHECK(bm->rect().h == static_cast<int>(std::lround(th * t)));
    CHECK(bm->scale() == doctest::Approx(t));
    CHECK(bm->rect().x == 30);  // grows from the top-left corner
    CHECK(bm->rect().y == 40);
    CHECK(e.getNumber("gEnd") == 0);
  }
  e.tickAt(1100);
  CHECK(bm->rect().w == tw);
  CHECK(bm->rect().h == th);
  CHECK(bm->scale() == 1.0);
  CHECK(e.getNumber("gEnd") == 1);
  e.tickAt(1200);
  CHECK(e.getNumber("gEnd") == 1);
}

TEST_CASE("Transition zoomin on a window that is not a Bitmap only times and calls onend") {
  Env e;
  REQUIRE(e.booted);
  e.clock.nowMs = 0;
  REQUIRE(e.run("Transition{ 'zoomin', window = 'x', time = 50, onend = function() gEnd = gEnd + 1 end }") == 0);
  e.tickAt(25);
  CHECK(e.x().rect().w == 100);
  e.tickAt(50);
  CHECK(e.getNumber("gEnd") == 1);
  CHECK(e.x().rect().w == 100);
}

TEST_CASE("Transition: missing window and unknown style never call onend; time defaults to 100 and 0 ends on the next tick") {
  Env e;
  REQUIRE(e.booted);
  e.clock.nowMs = 0;
  REQUIRE(e.run("local f = function() gEnd = gEnd + 1 end; "
                "Transition{ 'fadein', window = 'nope', onend = f }; "
                "Transition{ 'bogus', window = 'x', onend = f }; "
                "Transition{ 'fadeout', window = 'x', onend = f }") == 0);  // default time
  CHECK(e.wm.transitionCount() == 1);
  e.tickAt(99);
  CHECK(e.getNumber("gEnd") == 0);
  e.tickAt(100);
  CHECK(e.getNumber("gEnd") == 1);
  CHECK(e.log.count("[transition] unknown style") == 1);

  e.clock.nowMs = 200;
  ui::setSubtreeAlpha(e.x(), 1.0f);
  REQUIRE(e.run("Transition{ 'fadeout', window = 'x', time = 0, onend = function() gEnd = gEnd + 1 end }") == 0);
  CHECK(e.getNumber("gEnd") == 1);  // not synchronous
  CHECK(e.textAlpha() == 1.0f);
  e.tickAt(200);
  CHECK(e.getNumber("gEnd") == 2);
  CHECK(e.textAlpha() == 0.0f);
  // A negative time clamps to 0.
  REQUIRE(e.run("Transition{ 'fadeout', window = 'x', time = -5, onend = function() gEnd = gEnd + 1 end }") == 0);
  e.tickAt(200);
  CHECK(e.getNumber("gEnd") == 3);
}

TEST_CASE("Transition: destroying the target ends it and runs onend once, with a log") {
  Env e;
  REQUIRE(e.booted);
  e.clock.nowMs = 0;
  REQUIRE(e.run("Transition{ 'fadeout', window = 'x', time = 1000, onend = function() gEnd = gEnd + 1 end }; "
                "DestroyWindow('x')") == 0);
  e.tickAt(10);
  CHECK(e.getNumber("gEnd") == 1);
  CHECK(e.wm.transitionCount() == 0);
  CHECK(e.log.count("[transition] target window") == 1);
  e.wm.clearDestroyed();
  e.tickAt(2000);
  CHECK(e.getNumber("gEnd") == 1);
}

TEST_CASE("Transition: ClearModalChildren (SwapToModal) ends it, and an onend may start a new transition") {
  Env e;
  REQUIRE(e.booted);
  e.clock.nowMs = 0;
  REQUIRE(e.run("Transition{ 'fadeout', window = 'x', time = 1000, onend = function() gEnd = gEnd + 1; "
                "GenericCreate({ typename = 'Window', name = 'z', x = 0, y = 0, w = 10, h = 10 }); PopWindow(); "
                "Transition{ 'swipe', window = 'z', time = 100, endx = 50 } end }; "
                "ClearModalChildren()") == 0);
  e.tickAt(10);
  CHECK(e.getNumber("gEnd") == 1);
  CHECK(e.wm.transitionCount() == 1);  // the one the onend started
  e.tickAt(110);
  CHECK(e.wm.transitionCount() == 0);
  CHECK(e.wm.findInTopModal("z")->rect().x == 50);
}

TEST_CASE("Transition: popping the modal ends it and runs onend once") {
  Env e;
  REQUIRE(e.booted);
  e.clock.nowMs = 0;
  REQUIRE(e.run("Transition{ 'fadeout', window = 'x', time = 1000, onend = function() gEnd = gEnd + 1 end }; "
                "PopModal('tr')") == 0);
  e.tickAt(10);
  CHECK(e.getNumber("gEnd") == 1);
  e.wm.clearDestroyed();
  e.tickAt(20);
  CHECK(e.getNumber("gEnd") == 1);
}
