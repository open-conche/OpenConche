#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

#include "ScriptHarness.h"
#include "games/common/Ledger.h"
#include "sdk/Users.h"
#include "games/choc3/HiscoreWindow.h"
#include "games/common/World.h"
#include "gfx/RecordingRenderer.h"
#include "games/common/Calendar.h"
#include "script/Formats.h"
#include "script/ScriptHost.h"
#include "ui/Layout.h"
#include "ui/widgets/TextWidgets.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
using game::DookieDropper;
using game::LedgerWindow;
using test::ScriptEnv;

namespace {

// A ledger over a fake tree: the children the game's ledger.lua makes, with no art. The Lua side is a plain Player table.
struct LedgerFixture : ScriptEnv {
  LedgerFixture() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ledger") {
    ctx.strings.loadRows({{"rank_1", "Apprentice"}, {"rank_2", "Confectioner"}, {"score_format", "%1%/week"},
                          {"date_format", "%1% %2%, %3%"}, {"month_1", "Jan"}, {"month_2", "Feb"}, {"month_3", "Mar"},
                          {"month_4", "Apr"}, {"month_5", "May"}, {"month_6", "Jun"}, {"month_7", "Jul"},
                          {"month_8", "Aug"}, {"month_9", "Sep"}, {"month_10", "Oct"}, {"month_11", "Nov"},
                          {"month_12", "Dec"}});
    modal = &wm.pushModal("m");
    modal->setRect({0, 0, 800, 600});
    auto owned = std::make_unique<LedgerWindow>(wm, ctx);
    ledger = static_cast<LedgerWindow*>(&modal->adoptFront(std::move(owned)));
    ledger->setRect({0, 0, 800, 600});
    for (const char* n : {"money", "day", "rank", "score", "questText"}) add<ui::TextWindow>(n);
    add<ui::BitmapWindow>("ledger_questgoals");
    for (const char* n : {"zur_factory", "cap_factory"}) {
      auto& d = add<DookieDropper>(n, ctx);
      d.setSize(79, 132);
    }
    add<ui::PlaceholderWindow>("map_port", "Button");
    add<ui::PlaceholderWindow>("port_map", "Button");
    REQUIRE(run("Player = { money = 0, time = 10, rank = 1, factories = {}, supply = {} }") == 0);
    ledger->prepareInstance();
    // Start every test from a blank panel, as ledger.lua builds it.
    for (const char* n : {"money", "day", "rank", "score", "questText"}) child(n)->setLabel("");
    child("score")->setShown(true);
  }
  template <class W, class... A>
  W& add(const char* name, A&&... a) {
    auto w = std::make_unique<W>(wm, std::forward<A>(a)...);
    W* raw = w.get();
    ledger->adoptFront(std::move(w));
    raw->setName(name);
    return *raw;
  }
  ui::Window* child(const char* name) { return ledger->find(name); }
  std::string label(const char* name) { return child(name)->label(); }
  void setMoney(double v) {
    REQUIRE(run("Player.money = " + std::to_string(v)) == 0);
  }
  void ticks(int n) {
    for (int i = 0; i < n; ++i) ledger->animate();
  }
  ui::Window* modal = nullptr;
  LedgerWindow* ledger = nullptr;
};

}  // namespace

TEST_CASE("ledger: the money counter reaches its target in 20 steps, linearly, without overshooting") {
  LedgerFixture f;
  f.setMoney(1000);
  f.ledger->update("money", "");
  CHECK(f.ledger->targetMoney() == 1000);
  CHECK(f.ledger->deltaMoney() == 50);
  CHECK(f.ledger->displayMoney() == 0);
  for (int i = 1; i <= 20; ++i) {
    f.ledger->animate();
    CHECK(f.ledger->displayMoney() == doctest::Approx(50.0 * i));
    if (i == 10) CHECK(f.label("money") == "$500");
  }
  CHECK(f.ledger->displayMoney() == 1000);
  CHECK(f.label("money") == "$1,000");
  f.ledger->animate();  // nothing more to do
  CHECK(f.ledger->displayMoney() == 1000);
}

TEST_CASE("ledger: a retarget mid-count recomputes the step from the displayed value") {
  LedgerFixture f;
  f.setMoney(1000);
  f.ledger->update("money", "");
  f.ticks(10);
  REQUIRE(f.ledger->displayMoney() == 500);
  f.setMoney(100);
  f.ledger->update("money", "");
  CHECK(f.ledger->deltaMoney() == doctest::Approx(-20));  // (100 - 500) / 20
  f.ticks(20);
  CHECK(f.ledger->displayMoney() == 100);
  f.ticks(3);
  CHECK(f.ledger->displayMoney() == 100);  // it clamps and never overshoots
  // The same money again changes nothing.
  f.ledger->update("money", "");
  CHECK(f.ledger->targetMoney() == 100);
}

TEST_CASE("ledger: only newplayer redraws the money text at the current display value") {
  LedgerFixture f;
  f.setMoney(1000);
  f.ledger->update("money", "");
  CHECK(f.label("money") == "");  // the text is untouched until the first step (the fixture's tree has no label)
  f.ledger->update("newplayer", "");
  CHECK(f.label("money") == "$0");  // display is still 0: the counter does not jump
  CHECK(f.ledger->displayMoney() == 0);
}

TEST_CASE("ledger: large amounts shrink the money font") {
  LedgerFixture f;
  f.setMoney(123456789);
  f.ledger->prepareInstance();
  f.ledger->update("newplayer", "");
  CHECK(f.label("money") == "<font size='25'>$123,456,792</font>");  // through float: 123456789 -> 123456792
  f.setMoney(2e9);
  f.ledger->prepareInstance();
  f.ledger->update("newplayer", "");
  CHECK(f.label("money") == "<font size='22'>$2,000,000,000</font>");
  f.setMoney(2e11);
  f.ledger->prepareInstance();
  f.ledger->update("newplayer", "");
  CHECK(f.label("money").find("size='20'") != std::string::npos);
}

TEST_CASE("ledger: the quest indicator flashes 250 ms phases for 1500 ms and then stays complete") {
  LedgerFixture f;
  CHECK(f.ledger->flashQuest());  // the constructor starts with flashQuest true
  f.ledger->setQuestIndicator("off");
  CHECK_FALSE(f.ledger->flashQuest());
  CHECK(f.ledger->questImage() == LedgerWindow::kQuestIncomplete);
  f.ledger->setQuestIndicator("flash");
  CHECK(f.ledger->flashQuest());
  CHECK(f.ledger->flashTimer() == 1500);
  CHECK(f.ledger->questImage() == LedgerWindow::kQuestComplete);
  CHECK(f.stubs.count("SoundEvent.quest_requirements_met") == 1);
  f.ledger->setQuestIndicator("flash");  // a repeat neither restarts the timer nor makes a sound
  CHECK(f.ledger->flashTimer() == 1500);
  CHECK(f.stubs.count("SoundEvent.quest_requirements_met") == 1);

  std::vector<bool> complete;
  for (int i = 1; i <= 30; ++i) {
    f.ledger->animate();
    CHECK(f.ledger->flashTimer() == static_cast<unsigned>(1500 - 50 * i));
    complete.push_back(f.ledger->questImage() == LedgerWindow::kQuestComplete);
  }
  // Timer 1450..1250 incomplete, 1200..1000 complete, and so on in blocks of five steps; the last block is complete.
  for (int i = 0; i < 30; ++i) {
    const int timer = 1500 - 50 * (i + 1);
    CHECK(complete[i] == (timer % 500 < 250));
  }
  CHECK_FALSE(complete[0]);
  CHECK(complete[5]);
  CHECK_FALSE(complete[10]);
  CHECK(complete[29]);
  f.ticks(10);  // after the flash it sticks on complete
  CHECK(f.ledger->flashTimer() == 0);
  CHECK(f.ledger->questImage() == LedgerWindow::kQuestComplete);

  f.ledger->setQuestIndicator("on");
  CHECK_FALSE(f.ledger->flashQuest());
  f.ledger->setQuestIndicator("flash");  // a new transition into flash makes the sound again
  CHECK(f.stubs.count("SoundEvent.quest_requirements_met") == 2);
  f.ledger->setQuestIndicator("anything else");
  CHECK(f.ledger->questImage() == LedgerWindow::kQuestIncomplete);
  CHECK_FALSE(f.ledger->flashQuest());
}

TEST_CASE("ledger: RealUpdateLedger modes have exactly their documented effects") {
  LedgerFixture f;
  REQUIRE(f.run("Player.money = 2000; Player.time = 10; Player.rank = 2") == 0);
  const std::string date = [] {
    const game::GameDate d = game::dateFromWeeks(10);
    static const char* m[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return std::string(m[d.month]) + " " + std::to_string(d.day) + ", " + std::to_string(d.year);
  }();

  SUBCASE("time touches the day text only") {
    f.ledger->update("time", "");
    CHECK(f.label("day") == date);
    CHECK(f.label("rank") == "");
    CHECK(f.label("score") == "$200/week");  // time also refreshes the score line
    CHECK(f.ledger->targetMoney() == 0);
  }
  SUBCASE("money retargets the counter and writes the score line") {
    f.ledger->update("money", "");
    CHECK(f.ledger->targetMoney() == 2000);
    CHECK(f.label("score") == "$200/week");
    CHECK(f.label("day") == "");
  }
  SUBCASE("rank writes the rank name and shows the score at ranks above 1") {
    f.ledger->update("rank", "");
    CHECK(f.label("rank") == "Confectioner");
    CHECK(f.child("score")->shown());
    f.run("Player.rank = 1");
    f.ledger->update("rank", "");
    CHECK(f.label("rank") == "Apprentice");
    CHECK_FALSE(f.child("score")->shown());
  }
  SUBCASE("quest writes the data verbatim, and nothing else") {
    f.ledger->update("quest", "<font size='11'>Do the thing");
    CHECK(f.label("questText") == "<font size='11'>Do the thing");
    CHECK(f.label("day") == "");
    CHECK(f.label("rank") == "");
  }
  SUBCASE("all does everything except the quest line and the views") {
    f.ledger->update("all", "ignored");
    CHECK(f.label("day") == date);
    CHECK(f.label("rank") == "Confectioner");
    CHECK(f.label("score") == "$200/week");
    CHECK(f.label("questText") == "");
    CHECK(f.child("map_port")->shown());
    CHECK(f.child("port_map")->shown());
  }
  SUBCASE("newplayer is all plus the quest line") {
    f.ledger->update("newplayer", "Quest!");
    CHECK(f.label("questText") == "Quest!");
    CHECK(f.label("day") == date);
  }
  SUBCASE("portview and mapview swap the map and port buttons") {
    f.ledger->update("portview", "");
    CHECK_FALSE(f.child("map_port")->shown());
    CHECK(f.child("port_map")->shown());
    f.ledger->update("mapview", "");
    CHECK(f.child("map_port")->shown());
    CHECK_FALSE(f.child("port_map")->shown());
    CHECK(f.label("day") == "");  // the views touch nothing else
  }
  SUBCASE("an unknown mode, and a mode in the wrong case, change nothing") {
    f.ledger->update("Money", "");
    f.ledger->update("bogus", "x");
    CHECK(f.ledger->targetMoney() == 0);
    CHECK(f.label("day") == "");
    CHECK(f.label("questText") == "");
    CHECK(f.label("score") == "");
  }
  SUBCASE("the score line is money over weeks, floored, and zero at time zero") {
    f.run("Player.money = 1000; Player.time = 3");
    f.ledger->update("money", "");
    CHECK(f.label("score") == "$333/week");
    f.run("Player.time = 0");
    f.ledger->update("time", "");
    CHECK(f.label("score") == "$0/week");
  }
}

TEST_CASE("ledger: the factory mode shows the droppers that have a factory and hides the others") {
  LedgerFixture f;
  REQUIRE(f.run("Player.factories = { zur_factory = { current = '', production = 7, stall = false } }") == 0);
  f.ledger->update("factory", "");
  CHECK(f.child("zur_factory")->shown());
  CHECK_FALSE(f.child("cap_factory")->shown());
  auto* d = dynamic_cast<DookieDropper*>(f.child("zur_factory"));
  REQUIRE(d);
  CHECK(d->production() == 7);
  CHECK(d->stalled() == false);
  CHECK(f.label("day") == "");  // only the droppers
  f.run("Player.factories.cap_factory = { production = 0, stall = true }");
  f.ledger->update("factory", "");
  CHECK(f.child("cap_factory")->shown());
}

TEST_CASE("DookieDropper: bar colours switch at 0.75 and 0.5 of the weeks a column holds") {
  const gfx::ColorF green{50 / 255.0f, 205 / 255.0f, 30 / 255.0f, 1};
  const gfx::ColorF yellow{1, 252 / 255.0f, 15 / 255.0f, 1};
  const gfx::ColorF red{1, 14 / 255.0f, 14 / 255.0f, 1};
  CHECK(DookieDropper::barColour(2) == green);
  CHECK(DookieDropper::barColour(0.75) == green);
  CHECK(DookieDropper::barColour(0.7499) == yellow);
  CHECK(DookieDropper::barColour(0.5) == yellow);
  CHECK(DookieDropper::barColour(0.4999) == red);
  CHECK(DookieDropper::barColour(0) == red);
  CHECK(DookieDropper::barHeightFor(0.5, 42) == doctest::Approx(21));
  CHECK(DookieDropper::barHeightFor(3, 42) == 42);  // capped at barHeight
  CHECK(DookieDropper::barHeightFor(-1, 42) == 0);
}

TEST_CASE("DookieDropper: weeks per column by rank") {
  const double expect[] = {4, 6, 8, 8, 8, 10, 10};
  for (int r = 0; r <= 6; ++r) CHECK(DookieDropper::weeksPerColumn(r) == expect[r]);
  CHECK(DookieDropper::weeksPerColumn(9) == 10);
}

TEST_CASE("DookieDropper: bars are centred columns of a sixth of the width, growing up from the baseline") {
  // Width 79, three ingredients, rank 2 (8 weeks): columns 13.17 wide, the first centred at 26.33.
  const auto bars = DookieDropper::layoutBars({8, 4, 1}, 2, 79, 58, 42);
  REQUIRE(bars.size() == 3);
  CHECK(bars[0].colour == DookieDropper::barColour(1));
  CHECK(bars[1].colour == DookieDropper::barColour(0.5));
  CHECK(bars[2].colour == DookieDropper::barColour(0.125));
  CHECK(bars[0].height == 42);
  CHECK(bars[1].height == doctest::Approx(21));
  CHECK(bars[0].centreX == doctest::Approx(26.3333f));
  CHECK(bars[1].centreX == doctest::Approx(39.5f));
  CHECK(bars[0].rect.x == 22);   // 26.33 - 6.58 + 2.5
  CHECK(bars[0].rect.w == 9);    // 13.17 - 4
  CHECK(bars[0].rect.y == 13);   // 58 - 42 - 3
  CHECK(bars[0].rect.h == 42);
  CHECK(bars[0].rect.y + bars[0].rect.h == 55);  // every bar stands on 55
  CHECK(bars[1].rect.y + bars[1].rect.h == 55);
  CHECK(bars[2].rect.y == 49);  // a fractional height (5.25) truncates: 58 - 5.25 - 3 = 49.75
}

TEST_CASE("DookieDropper: the animation clock, the timer hand and the product pulse") {
  LedgerFixture f;
  REQUIRE(f.run("Player.factories = { zur_factory = { current = '', production = 5, stall = false } }") == 0);
  f.ledger->update("factory", "");
  auto* d = dynamic_cast<DookieDropper*>(f.child("zur_factory"));
  REQUIRE(d);
  CHECK(d->clockMs() == 2000);  // the constructor leaves the clock finished
  f.ledger->animateProduction();
  CHECK(d->clockMs() == 0);
  f.ticks(1);
  CHECK(d->clockMs() == 50);
  CHECK(d->handAngle() == doctest::Approx(2 * 3.14159265 * 50 / 2000));
  CHECK(d->productScale() == doctest::Approx((50 % 2000 + 1) * 0.25 / 2000));
  f.ticks(19);
  CHECK(d->clockMs() == 1000);
  CHECK(d->handAngle() == doctest::Approx(3.14159265));
  f.ticks(40);
  CHECK(d->clockMs() == 2000);
  CHECK(d->productScale() == doctest::Approx(0.25));  // at rest at the size it was built with
  f.ticks(5);
  CHECK(d->clockMs() == 2000);

  // A stalled factory does not animate its sprite.
  REQUIRE(f.run("Player.factories.zur_factory.stall = true") == 0);
  f.ledger->update("factory", "");
  f.ledger->animateProduction();
  f.ticks(2);
  CHECK(d->handAngle() == doctest::Approx(2 * 3.14159265));  // unchanged from the last full turn
}

TEST_CASE("DookieDropper: it reads the product, the ingredients and the supply, and draws coloured bars") {
  LedgerFixture f;
  REQUIRE(f.run("_AllProducts = { fudge = { code = 'fudge', counts = { sugar = 2 } } };"
                "Player.rank = 2;"
                "Player.supply = { sugar = 4.5 };"
                "Player.factories = { zur_factory = { current = 'fudge', production = 3, stall = false } }") == 0);
  f.ledger->update("factory", "");
  auto* d = dynamic_cast<DookieDropper*>(f.child("zur_factory"));
  REQUIRE(d);
  CHECK(d->product() == "fudge");
  REQUIRE(d->ingredients().size() == 1);
  CHECK(d->ingredients()[0] == "sugar");
  CHECK(d->playerRank() == 2);

  gfx::RecordingRenderer rr;
  ui::DrawContext ctx{rr, {0, 0}, 1, {0, 0, 800, 600}};
  d->setPos(100, 100);
  ui::drawWindow(*d, ctx);
  // Supply 4.5 weeks over 8 per column: ratio 0.5625, the middle (yellow) band, 23.6 px tall (23 whole pixels).
  const auto& calls = rr.calls();
  const gfx::ColorF yellow = DookieDropper::barColour(0.5625);
  int found = 0;
  for (const auto& c : calls)
    if (c.kind == gfx::RecordingRenderer::Call::Kind::FillRect && c.color == yellow) {
      ++found;
      CHECK(c.dst.h == 23);
      CHECK(c.dst.y == 100 + 31);
    }
  CHECK(found == 1);

  // More supply turns it green.
  REQUIRE(f.run("Player.supply.sugar = 7") == 0);
  rr.clear();
  ui::drawWindow(*d, ctx);
  CHECK(rr.count(gfx::RecordingRenderer::Call::Kind::FillRect) == 1);
  for (const auto& c : rr.calls())
    if (c.kind == gfx::RecordingRenderer::Call::Kind::FillRect) CHECK(c.color == DookieDropper::barColour(1));
}

// ---- the host: grab, release, lifetime ----

namespace {

struct HostFixture : ScriptEnv {
  HostFixture() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ledger"), host(ctx) {
    REQUIRE(host.boot());
    REQUIRE(run("Player = { money = 500, time = 0, rank = 1, factories = {}, supply = {} }") == 0);
    wm.pushModal("screen");
    wm.topModal()->setRect({0, 0, 800, 600});
  }
  ~HostFixture() { wm.clearDestroyed(); }
  game::LedgerHost& ledger() { return game::worldOf(ctx)->ledger; }
  script::ScriptHost host;
};

}  // namespace

TEST_CASE("ledger host: the natives exist before any ledger and do nothing") {
  HostFixture f;
  REQUIRE(game::worldOf(f.ctx));
  CHECK(f.ledger().get() == nullptr);
  CHECK(f.run("RealUpdateLedger('all', ''); SetLedgerQuestIndicator('on'); AnimateProduction(); ReleaseLedger()") == 0);
  CHECK(f.log.count("[lua-error]") == 0);
  CHECK(f.ledger().get() == nullptr);
}

TEST_CASE("ledger host: grab builds the ledger from ledger.lua into the top modal, release detaches, grab re-attaches") {
  HostFixture f;
  REQUIRE(f.run("GrabLedger()") == 0);
  game::LedgerWindow* l = f.ledger().get();
  REQUIRE(l);
  CHECK(f.log.count("[lua-error]") == 0);
  CHECK(l->name() == "ledger");
  CHECK(l->parent() == f.wm.topModal());
  CHECK(f.wm.topModal()->children().front().get() == l);
  CHECK(l->dropperCount() == 2);
  CHECK(l->find("money") != nullptr);
  const ui::WindowId id = l->id();

  // Release keeps it (and its id) away from the tree.
  REQUIRE(f.run("ReleaseLedger()") == 0);
  CHECK(f.ledger().detached());
  CHECK(f.ledger().get() == l);
  CHECK(l->parent() == nullptr);
  CHECK(f.wm.findInChain("ledger") == nullptr);
  CHECK(f.run("ReleaseLedger()") == 0);  // a second release is harmless

  // A different screen takes it back: same window, same id, in front of the new modal's children.
  f.wm.pushModal("other");
  f.wm.topModal()->setRect({0, 0, 800, 600});
  REQUIRE(f.run("GrabLedger()") == 0);
  CHECK(f.ledger().get() == l);
  CHECK(l->id() == id);
  CHECK(l->parent() == f.wm.topModal());
  CHECK_FALSE(f.ledger().detached());

  // Grabbing again from another modal moves it from where it is.
  f.wm.pushModal("third");
  f.wm.topModal()->setRect({0, 0, 800, 600});
  REQUIRE(f.run("GrabLedger()") == 0);
  CHECK(f.ledger().get() == l);
  CHECK(l->parent() == f.wm.topModal());
}

TEST_CASE("ledger host: grab, release, grab keeps the money counter and the quest flash state") {
  HostFixture f;
  REQUIRE(f.run("GrabLedger()") == 0);
  game::LedgerWindow* l = f.ledger().get();
  REQUIRE(l);
  REQUIRE(f.run("Player.money = 1000; RealUpdateLedger('money', ''); SetLedgerQuestIndicator('off'); SetLedgerQuestIndicator('flash')") == 0);
  for (int i = 0; i < 7; ++i) l->animate();
  const double display = l->displayMoney(), target = l->targetMoney(), delta = l->deltaMoney();
  const unsigned timer = l->flashTimer();
  REQUIRE(display == doctest::Approx(675));  // from the $500 it started at, 25 a step
  REQUIRE(target == 1000);
  REQUIRE(timer == 1500 - 7 * 50);
  const std::string text = l->find("money")->label();

  REQUIRE(f.run("ReleaseLedger()") == 0);
  f.wm.pushModal("other");
  f.wm.topModal()->setRect({0, 0, 800, 600});
  REQUIRE(f.run("GrabLedger()") == 0);
  REQUIRE(f.ledger().get() == l);  // the same window, not rebuilt
  CHECK(l->displayMoney() == display);
  CHECK(l->targetMoney() == target);
  CHECK(l->deltaMoney() == delta);
  CHECK(l->flashQuest());
  CHECK(l->flashTimer() == timer);
  CHECK(l->find("money")->label() == text);
  l->animate();  // and it carries on from there
  CHECK(l->displayMoney() == doctest::Approx(display + delta));
  CHECK(l->flashTimer() == timer - 50);
}

TEST_CASE("ledger host: a ledger lost with its modal is rebuilt by the next grab") {
  HostFixture f;
  f.wm.pushModal("screen2");
  f.wm.topModal()->setRect({0, 0, 800, 600});
  REQUIRE(f.run("GrabLedger()") == 0);
  const ui::WindowId first = f.ledger().get()->id();
  f.wm.popModal("screen2");
  CHECK(f.ledger().get() == nullptr);  // gone as soon as the modal is, before it is deleted
  f.wm.clearDestroyed();
  CHECK(f.ledger().get() == nullptr);
  CHECK(f.run("RealUpdateLedger('all', '')") == 0);  // the natives go back to doing nothing

  f.wm.pushModal("screen3");
  f.wm.topModal()->setRect({0, 0, 800, 600});
  REQUIRE(f.run("GrabLedger()") == 0);
  REQUIRE(f.ledger().get());
  CHECK(f.ledger().get()->id() != first);
  CHECK(f.ledger().get()->find("money") != nullptr);
}

TEST_CASE("ledger host: the 50 ms task runs on the script clock, one step per call") {
  HostFixture f;
  f.ctx.clock.nowMs = 0;
  REQUIRE(f.run("GrabLedger()") == 0);
  auto* l = f.ledger().get();
  REQUIRE(l);
  REQUIRE(f.run("Player.money = 1000; RealUpdateLedger('money', '')") == 0);
  const double start = l->displayMoney();
  for (int frame = 1; frame <= 60; ++frame) {  // one second at 60 Hz
    const double now = frame * 1000.0 / 60.0;
    f.ctx.clock.nowMs = now;
    f.ledger().tick(now);
    if (frame == 2) CHECK(l->displayMoney() == start);       // 33 ms: no step yet
    if (frame == 3) CHECK(l->displayMoney() > start);        // 50 ms: the first step
  }
  CHECK(l->displayMoney() == doctest::Approx(1000));
  CHECK(l->displayMoney() <= 1000);
}

// ---- the small natives ----

TEST_CASE("natives: movies are skipped") {
  HostFixture f;
  REQUIRE(f.run("a = StartMovie('splash/intro_movie.swf', true, false); b = IsMoviePlaying(); StopMovie(); c = StartMovie('')") == 0);
  CHECK(f.getString("a") == "false");
  CHECK(f.getString("b") == "false");
  CHECK(f.getString("c") == "false");
  CHECK(f.stubs.count("StartMovie") == 2);
  CHECK(f.log.count("[unimplemented] StartMovie") == 1);
}

TEST_CASE("natives: SubmitToServer logs once and runs SwitchModes(false)") {
  HostFixture f;
  REQUIRE(f.run("gSwitch = {}; function SwitchModes(v) table.insert(gSwitch, tostring(v)) end;"
                "SubmitToServer('Alice', '', '', false, false); SubmitToServer('Alice', 'a', 'p', true, true)") == 0);
  f.host.tick(1);  // the injected calls may be waiting for Main
  REQUIRE(f.run("n = table.getn(gSwitch); first = gSwitch[1]; second = gSwitch[2]") == 0);
  CHECK(f.getNumber("n") == 2);
  CHECK(f.getString("first") == "false");
  CHECK(f.getString("second") == "false");
  CHECK(f.log.count("[unimplemented] SubmitToServer") == 1);
}

TEST_CASE("natives: GetLuaServerSubmitSetupVars returns valid Lua with the exact offline text") {
  HostFixture f;
  REQUIRE(f.run("a = GetLuaServerSubmitSetupVars(false); b = GetLuaServerSubmitSetupVars(true)") == 0);
  CHECK(f.getString("a") == "gNameEdit=''; gAccountEdit=''; gPassEdit=''; gRemember=false; gMedalsMode=false;gEligibleScore=0");
  CHECK(f.getString("b") == "gNameEdit=''; gAccountEdit=''; gPassEdit=''; gRemember=false; gMedalsMode=true;");
  // Both run as Lua and set what the dialog reads.
  REQUIRE(f.run("loadstring(a)(); m1 = gMedalsMode; e1 = gEligibleScore; loadstring(b)(); m2 = gMedalsMode") == 0);
  CHECK(f.getString("m1") == "false");
  CHECK(f.getNumber("e1") == 0);
  CHECK(f.getString("m2") == "true");
}

TEST_CASE("natives: the remembered name in the setup vars is escaped into valid Lua") {
  HostFixture f;
  f.ctx.users = std::make_shared<sdk::UserStore>("");
  f.ctx.users->create("Pat");
  REQUIRE(f.ctx.users->setPref("_anonusername", "O'Brien\\n"));  // O'Brien, a backslash, n
  REQUIRE(f.run("a = GetLuaServerSubmitSetupVars(true); loadstring(a)(); name = gNameEdit") == 0);
  CHECK(f.getString("a").find("gNameEdit='O\\'Brien\\\\n';") == 0);
  CHECK(f.getString("name") == "O'Brien\\n");
}

TEST_CASE("natives: control characters in the remembered name are escaped with three digits") {
  HostFixture f;
  f.ctx.users = std::make_shared<sdk::UserStore>("");
  f.ctx.users->create("Pat");
  REQUIRE(f.ctx.users->setPref("_anonusername", std::string("a\x01" "2b\x7f" "9")));
  REQUIRE(f.run("a = GetLuaServerSubmitSetupVars(true); loadstring(a)(); name = gNameEdit; n = string.len(name)") == 0);
  CHECK(f.getString("a").find("gNameEdit='a\\0012b\\1279';") == 0);
  CHECK(f.getNumber("n") == 6);  // a, \1, 2, b, \127, 9: the digits after an escape stay separate characters
}

TEST_CASE("natives: SubmitWindow has its Lua constructor global") {
  HostFixture f;
  REQUIRE(f.run("t = type(SubmitWindow)") == 0);
  CHECK(f.getString("t") == "function");
  REQUIRE(f.run("w = SubmitWindow{ name = 'x' }; ty = type(w)") == 0);
  CHECK(f.getString("ty") == "function");
}

TEST_CASE("natives: SubmitWindow and the game window types are real types") {
  HostFixture f;
  auto sw = f.wm.createWindow("SubmitWindow");
  CHECK(sw->typeName() == "SubmitWindow");
  CHECK(f.wm.createWindow("DookieDropper")->typeName() == "DookieDropper");
  CHECK(f.wm.createWindow("LedgerWindow")->typeName() == "LedgerWindow");
}

TEST_CASE("calendar: weeks to a date and a day of the year follow the documented formulas") {
  CHECK(game::dayOfYear(53) == (53 * 7) % 365);
  CHECK(game::dayOfYear(0.9) == 6);  // weeks are not truncated before the *7: 6.3 -> 6
  CHECK(game::dayOfYear(-4) == 0);
  // weeks 100: days = 871, offset 2 (not leap), day 141 of the year -> 22 May 1948.
  auto d = game::dateFromWeeks(100);
  CHECK(d.year == 1948);
  CHECK(d.month == 5);
  CHECK(d.day == 22);
  // The first week: days = 171, offset 0 (counted as a leap year), so Feb has 29 days: 20 June 1946.
  auto d0 = game::dateFromWeeks(0);
  CHECK(d0.year == 1946);
  CHECK(d0.month == 6);
  CHECK(d0.day == 20);
  // Fractional weeks: 0.5 * 7 + 171 = 174.5 -> 174, i.e. 23 June; truncating the weeks first would give 20 June.
  CHECK(game::dateFromWeeks(0.5).day == 23);
  // Gregorian on the offset: 1946+100 has offset 100 (not leap), offset 4 is leap (day 4*365+60 is 29 Feb 1950).
  auto leap = game::dateFromWeeks((4 * 365 + 59 - 171) / 7.0);
  CHECK(leap.year == 1950);
  CHECK(leap.month == 2);
  CHECK(leap.day == 29);
  // Offset 100 is divisible by 4 and by 100, so not leap; offset 400 is.
  auto o100 = game::dateFromWeeks((100 * 365 + 59 - 171) / 7.0);
  CHECK(o100.month == 3);
  CHECK(o100.day == 1);
  auto o400 = game::dateFromWeeks((400 * 365 + 59 - 171) / 7.0);
  CHECK(o400.month == 2);
  CHECK(o400.day == 29);
}

// The Chocolatier 3 side reads _MONEY_SIGN / _NUMERIC_SEPARATOR in several places (Lua Dollars, the ledger, the
// hiscore text; the crates text is covered by the crates scenario). All of them must agree for every global setting.
TEST_CASE("money format: Dollars, the ledger text and the hiscore text agree for every sign and separator setting") {
  struct Setting {
    const char* lua;
    const char* sign;
    const char* sep;
  };
  const Setting settings[] = {{"_MONEY_SIGN = 'EUR '; _NUMERIC_SEPARATOR = '.'", "EUR ", "."},
                              {"_MONEY_SIGN = 7; _NUMERIC_SEPARATOR = 8", "$", ","},
                              {"_MONEY_SIGN = nil; _NUMERIC_SEPARATOR = nil", "$", ","}};
  for (const Setting& st : settings) {
    LedgerFixture f;
    script::registerCoreNatives(f.lua.L(), f.ctx);
    CAPTURE(st.lua);
    REQUIRE(f.run(st.lua) == 0);
    for (double v : {0.4, 1234.5, -1e6}) {
      CAPTURE(v);
      const std::string want = script::formatDollars(v, st.sign, st.sep);
      // Lua Dollars
      REQUIRE(f.run("r = Dollars(" + std::to_string(v) + ")") == 0);
      CHECK(f.getString("r") == want);
      // The ledger's money label once the counter has arrived
      f.setMoney(v);
      f.ledger->update("money", "");
      f.ticks(25);
      f.ledger->update("newplayer", "");
      std::string label = f.label("money");
      if (label.rfind("<font", 0) == 0) label = label.substr(label.find('>') + 1, label.rfind("</font>") - label.find('>') - 1);
      CHECK(label == want);
      // The hiscore score text ("%1%/week")
      CHECK(game::HiscoreWindow::scoreText(f.ctx, static_cast<long long>(v)) ==
            script::formatDollars(static_cast<double>(static_cast<long long>(v)), st.sign, st.sep) + "/week");
    }
  }
}
