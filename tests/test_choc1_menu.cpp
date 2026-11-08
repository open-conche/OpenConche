// Chocolatier 1 main-menu natives (docs/re/choc1-fit.md): the money counter, the window-tree helpers over the modal
// chain, UtilPulseImage and the SimpleAnimation frame rule.
#include <doctest/doctest.h>

#include <string>

#include "games/Registry.h"
#include "games/choc1/Choc1Money.h"
#include "games/choc1/Choc1World.h"
#include "games/choc1/SimpleAnimation.h"
#include "games/common/MoneyCounter.h"
#include "ScriptHarness.h"
#include "ui/widgets/TextWidgets.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
using game::choc1::Choc1Money;
using game::choc1::Choc1World;

TEST_CASE("MoneyCounter::setTargetSteps: max(1, gap / steps) whole dollars, no overshoot") {
  game::MoneyCounter c;
  c.snap(100);
  CHECK(c.setTargetSteps(100, 72) == false);  // no change
  CHECK(c.setTargetSteps(7300, 72));          // gap 7200 / 72 = 100
  CHECK(c.delta == 100);
  int ticks = 0;
  while (c.step()) ++ticks;
  CHECK(ticks == 72);
  CHECK(c.display == 7300);
  c.snap(0);
  c.setTargetSteps(10, 72);  // a small gap still moves one dollar a tick
  CHECK(c.delta == 1);
  ticks = 0;
  while (c.step()) ++ticks;
  CHECK(ticks == 10);
  c.snap(1000);
  c.setTargetSteps(400, 72);  // counting down: the step is negative, and the target is hit exactly
  CHECK(c.delta == -8);       // 600 / 72 = 8.33 truncates to 8
  while (c.step()) {}
  CHECK(c.display == 400);
  c.snap(0);
  c.setTargetSteps(12.9, 72);  // whole dollars
  CHECK(c.target == 12);
}

TEST_CASE("Choc1Money: period 1000/hz (integer), about 3 s, money_earned only when rising and running") {
  Choc1Money m;
  m.snap(500);
  CHECK_FALSE(m.enabled());
  CHECK_FALSE(m.setTarget(900));  // no task: it snaps and plays nothing
  CHECK(m.shown() == 900);
  m.setRate(24, 0);
  CHECK(m.periodMs() == 41);  // 1000 / 24
  m.setRate(0, 0);
  CHECK_FALSE(m.enabled());
  m.setRate(24, 0);
  CHECK(m.setTarget(900 + 7200));  // rising: the sound plays; gap 7200 / 72 = 100 a tick
  CHECK_FALSE(m.setTarget(100));   // falling: no sound
  m.snap(0);
  m.setTarget(7200);
  double t = 0;
  int steps = 0;
  while (m.shown() != 7200 && t < 10000) {
    t += 41;
    if (m.tick(t)) ++steps;
  }
  CHECK(steps == 72);  // 72 ticks of 41 ms: 2.95 s
  CHECK(t == doctest::Approx(2952));
  CHECK_FALSE(m.tick(t + 41));  // at the target nothing changes
}

TEST_CASE("SimpleAnimation: the frame index is floor(elapsed * fps / 1000) modulo the frame count") {
  using game::choc1::SimpleAnimation;
  CHECK(SimpleAnimation::frameAt(0, 12, 9) == 0);
  CHECK(SimpleAnimation::frameAt(83, 12, 9) == 1);  // the period is 1000/12 = 83 whole ms
  CHECK(SimpleAnimation::frameAt(82, 12, 9) == 0);
  CHECK(SimpleAnimation::frameAt(1000, 24, 100) == 24);  // 41 ms: 1000/41
  CHECK(SimpleAnimation::frameAt(1000, 12, 9) == 3);  // 12 frames = one pass and three more
  CHECK(SimpleAnimation::frameAt(200, 5, 9) == 1);
  CHECK(SimpleAnimation::frameAt(5000, 10, 0) == 0);
  CHECK(SimpleAnimation::frameAt(-5, 10, 4) == 0);
}

namespace {

struct MenuEnv : test::ScriptEnv {
  MenuEnv() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ui/modal"), host(ctx) {
    ctx.module = games::findModule("choc1");
    ctx.module->attach(ctx);
    booted = host.boot();
  }
  Choc1World& world() { return *game::choc1::choc1WorldOf(ctx); }
  template <class W>
  W& add(ui::Window& parent, const char* name, int x = 0, int y = 0) {
    auto w = std::make_unique<W>(wm);
    W& ref = *w;
    ref.setName(name);
    ref.setPos(x, y);
    parent.adoptFront(std::move(w));
    return ref;
  }
  script::ScriptHost host;
  bool booted = false;
};

}  // namespace

TEST_CASE("Choc1 money natives write every Text named money in the whole modal chain") {
  MenuEnv e;
  REQUIRE(e.booted);
  ui::ModalWindow& low = e.wm.pushModal("view");
  auto& lowMoney = e.add<ui::TextWindow>(low, "money");
  ui::ModalWindow& top = e.wm.pushModal("dialog");
  auto& topMoney = e.add<ui::TextWindow>(top, "money");
  REQUIRE(e.run("SetMoneyDisplay(1234)") == 0);
  CHECK(lowMoney.label() == "$1,234");  // the view under the dialog is written too
  CHECK(topMoney.label() == "$1,234");
  REQUIRE(e.run("SetUpdateRate(24); SetMoneyDisplayTarget(1234 + 7200)") == 0);
  CHECK(topMoney.label() == "$1,234");  // counting starts at the next tick
  double t = 0;
  for (int i = 0; i < 80; ++i) e.world().tick(t += 41);
  CHECK(topMoney.label() == "$8,434");
  CHECK(lowMoney.label() == "$8,434");
}

TEST_CASE("Choc1 window natives: enable/disable keep a button's state, set-on, text colour, global string, position") {
  MenuEnv e;
  REQUIRE(e.booted);
  ui::ModalWindow& low = e.wm.pushModal("view");
  auto& tab = e.add<ui::ButtonWindow>(low, "tab");
  auto& lowText = e.add<ui::TextWindow>(low, "caption");
  auto& panel = e.add<ui::BitmapWindow>(low, "panel", 5, 6);
  ui::ModalWindow& top = e.wm.pushModal("dialog");
  auto& topText = e.add<ui::TextWindow>(top, "caption");
  auto& topPanel = e.add<ui::BitmapWindow>(top, "panel", 1, 2);
  (void)panel;

  REQUIRE(e.run("UtilSetButtonOn('tab')") == 0);  // the button is in the modal under the top one
  CHECK(tab.on());
  REQUIRE(e.run("UtilDisableWindow('tab')") == 0);
  CHECK_FALSE(tab.shown());
  CHECK(tab.on());  // no button reset, unlike EnableWindow
  REQUIRE(e.run("UtilEnableWindow('tab')") == 0);
  CHECK(tab.shown());

  REQUIRE(e.run("UtilSetGlobalString('caption', 'Hello')") == 0);
  CHECK(lowText.label() == "Hello");
  CHECK(topText.label() == "Hello");
  REQUIRE(e.run("UtilSetTextColor('caption', {0.5, 0.25})") == 0);  // the blue defaults to 1.0, the alpha is 1.0
  CHECK(topText.textStyle().color.r == doctest::Approx(0.5));
  CHECK(topText.textStyle().color.g == doctest::Approx(0.25));
  CHECK(topText.textStyle().color.b == doctest::Approx(1.0));
  CHECK(lowText.textStyle().color.a == doctest::Approx(1.0));

  REQUIRE(e.run("UtilPositionWindow('panel', 40, 50)") == 0);  // below the top modal only
  CHECK(topPanel.rect().x == 40);
  CHECK(topPanel.rect().y == 50);
}

TEST_CASE("Choc1 UtilPulseImage: a 100 ms task cycles the grey of the BitmapTint, then resets it") {
  MenuEnv e;
  REQUIRE(e.booted);
  ui::ModalWindow& m = e.wm.pushModal("view");
  auto& tint = e.add<ui::BitmapTintWindow>(m, "pic");
  REQUIRE(e.run("UtilPulseImage('pic', 3)") == 0);
  CHECK(tint.tint().r == doctest::Approx(1.0));
  e.world().tick(100);
  CHECK(tint.tint().r == doctest::Approx(1.0));  // cycle entry 0
  e.world().tick(200);
  CHECK(tint.tint().g == doctest::Approx(0.9));
  e.world().tick(300);
  CHECK(tint.tint().b == doctest::Approx(0.8));
  CHECK(tint.tint().a == doctest::Approx(1.0));
  e.world().tick(400);  // count is used up: back to 1.0 and the task ends
  CHECK(tint.tint().r == doctest::Approx(1.0));
  CHECK(e.world().pulses.empty());
}

TEST_CASE("PolylinePath: constant speed along the polyline, loop wraps, no loop clamps") {
  using game::choc1::PolylinePath;
  const PolylinePath line(210000, true, {{-400, 120}, {400, 120}});
  CHECK(line.at(0).x == doctest::Approx(-400));
  CHECK(line.at(105000).x == doctest::Approx(0));
  CHECK(line.at(105000).y == doctest::Approx(120));
  CHECK(line.at(210000).x == doctest::Approx(-400));  // looped: back to the first point
  CHECK(line.at(210000 + 52500).x == doctest::Approx(-200));
  const PolylinePath once(1000, false, {{0, 0}, {100, 0}});
  CHECK(once.at(500).x == doctest::Approx(50));
  CHECK(once.at(1000).x == doctest::Approx(100));
  CHECK(once.at(99999).x == doctest::Approx(100));  // clamped at the last point
  // The airship: (200,75) -> (-100,75) -> (1200,75) -> (200,75), 300 + 1300 + 1000 = 2600 long over 90000 ms.
  const PolylinePath ship(90000, true, {{200, 75}, {-100, 75}, {1200, 75}, {200, 75}});
  CHECK(ship.at(0).x == doctest::Approx(200));
  CHECK(ship.at(90000 * 300 / 2600).x == doctest::Approx(-100).epsilon(0.001));  // end of the first leg
  CHECK(ship.at(45000).x == doctest::Approx(900));  // half way = 1300 along: 300 on the first leg, 1000 into the second
  CHECK(ship.at(45000).y == doctest::Approx(75));
  CHECK(ship.at(89999).x == doctest::Approx(200).epsilon(0.01));
  // The steamship: no loop needed to see the direction (right to left).
  const PolylinePath steam(25000, true, {{1200, 323}, {-400, 323}});
  CHECK(steam.at(12500).x == doctest::Approx(400));
  CHECK(PolylinePath().at(5).x == 0);  // no points
}

TEST_CASE("SimpleAnimation: random backdates by rand() % T (path time, else 20000); tiles repeat while below the limit") {
  using game::choc1::SimpleAnimation;
  for (unsigned r : {0u, 1u, 209999u, 210000u, 4000000000u}) {
    CHECK(SimpleAnimation::randomBackdate(210000, true, r) < 210000);
    CHECK(SimpleAnimation::randomBackdate(0, false, r) < 20000);
  }
  CHECK(SimpleAnimation::randomBackdate(1000, true, 2500) == 500);
  // The defaults give one copy; xtile 800 on an 800 px frame (limit 400 + 800) gives two (x and x + 800).
  CHECK(SimpleAnimation::tileOffsets(100, 8000, 400 + 800).size() == 1);
  CHECK(SimpleAnimation::tileOffsets(-400, 800, 400 + 800) == std::vector<float>{-400, 400});
  CHECK(SimpleAnimation::tileOffsets(131, 800, 400 + 800) == std::vector<float>{131, 931});
  CHECK(SimpleAnimation::tileOffsets(500, 800, 1200).size() == 1);  // 500 + 800 is past the limit
}

TEST_CASE("SimpleAnimation: a still is centred on its point; the first tile starts at the centre") {
  using game::choc1::SimpleAnimation;
  const gfx::RectF r = SimpleAnimation::stillRect(0, 64, 800, 128, 1.0f);  // sf_clouds at animy 64, offset (-400,0) -> x -400
  CHECK(r.y == 0);
  CHECK(r.h == 128);
  CHECK(SimpleAnimation::stillRect(-400, 64, 800, 128, 1.0f).x == -800);
  CHECK(SimpleAnimation::stillRect(100, 100, 40, 20, 0.5f).x == 90);  // scale about the centre
  CHECK(SimpleAnimation::stillRect(100, 100, 40, 20, 0.5f).w == 20);
  CHECK(SimpleAnimation::tileOffsets(-400, 800, 400 + 800) == std::vector<float>{-400, 400});
}

TEST_CASE("SimpleAnimation: heading and the mirror rule") {
  using game::choc1::PolylinePath;
  using game::choc1::SimpleAnimation;
  const PolylinePath p(1000, false, {{0, 0}, {100, 0}, {0, 0}});  // right for half the time, then left
  const double right = p.heading(100), left = p.heading(700);
  CHECK(right == doctest::Approx(0.0));  // acos(1) = 0, negated: -0
  CHECK(SimpleAnimation::mirrored(true, true, right));  // exactly horizontal, rightward: still mirrored
  CHECK(left == doctest::Approx(3.14159265));
  const PolylinePath diag(1000, false, {{0, 0}, {10, 10}});
  CHECK(diag.heading(10) < 0);
  CHECK(PolylinePath(1000, false, {{0, 0}, {0, 50}}).heading(10) == doctest::Approx(1.5707963));
  CHECK(p.heading(5000) == doctest::Approx(3.14159265));  // stopped: the last segment
  CHECK(PolylinePath().heading(10) == 0);
  CHECK(SimpleAnimation::mirrored(true, true, diag.heading(10)));  // moving right: mirrored
  CHECK_FALSE(SimpleAnimation::mirrored(true, true, left));
  CHECK_FALSE(SimpleAnimation::mirrored(true, false, 0));
  CHECK_FALSE(SimpleAnimation::mirrored(false, true, diag.heading(10)));
  CHECK_FALSE(SimpleAnimation::mirrored(true, true, PolylinePath(1000, false, {{0, 0}, {0, 50}}).heading(10)));
}
