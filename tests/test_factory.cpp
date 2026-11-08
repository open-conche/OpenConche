#include <doctest/doctest.h>

#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ScriptHarness.h"
#include "audio/Audio.h"
#include "games/common/Appearance.h"
#include "sdk/Ticker.h"
#include "games/common/factory/Chocolate.h"
#include "games/choc3/factory/Coffee.h"
#include "script/ScriptHost.h"
#include "games/choc3/Choc3Natives.h"
#include "ui/Style.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
using game::ChocolateFactory;
using game::ChocolateTray;
using game::CoffeeFactory;
using game::CoffeeGrid;
using game::Hopper;
using game::FactoryWindow;
using game::Ingredient;
using game::IngredientQueue;
using game::ScalingTask;
using State = game::FactoryWindow::State;

// ---------------------------------------------------------------- foundations

TEST_CASE("FixedStep: frames at k*1000/60 give exactly 100 steps by 1000 ms") {
  sdk::FixedStep s;
  s.start(0, 10);
  int n = 0;
  for (int k = 1; k <= 60; ++k) {
    const double now = k * 1000.0 / 60.0;
    int perFrame = 0;
    while (s.due(now)) ++n, ++perFrame;
    CHECK(perFrame >= 1);
    CHECK(perFrame <= 2);
  }
  CHECK(n == 100);
}

TEST_CASE("FixedStep: a gap over a second resyncs to one step, and stop stops") {
  sdk::FixedStep s;
  s.start(0, 10);
  int n = 0;
  while (s.due(1500)) ++n;
  CHECK(n == 1);
  CHECK_FALSE(s.due(1505));
  CHECK(s.due(1510));
  s.stop();
  CHECK_FALSE(s.due(5000));
  sdk::FixedStep t;
  t.start(0, 10);
  n = 0;
  while (t.due(500)) ++n;  // late but within a second: all the steps are made up
  CHECK(n == 50);
}

namespace {
struct CountWindow : ui::Window {
  using Window::Window;
  int calls = 0, stopAt = -1;
  bool onAnimate() override {
    ++calls;
    if (calls == stopAt) manager().stopWindowAnimation(id());
    return true;
  }
};
}  // namespace

TEST_CASE("window animation with catch-up runs once per due step and ends when the callback stops it") {
  test::ScriptEnv env(std::string(CHOCO_TEST_ASSETS) + "/ledger");
  ui::ModalWindow& m = env.wm.pushModal("m");
  auto w = std::make_unique<CountWindow>(env.wm);
  auto* cw = static_cast<CountWindow*>(&m.adoptFront(std::move(w)));
  env.wm.setNowMs(0);
  env.wm.startWindowAnimation(cw->id(), 10, true);
  env.wm.tickWindowAnimations(100);
  CHECK(cw->calls == 10);
  cw->stopAt = 13;
  env.wm.tickWindowAnimations(200);  // would be 10 more steps; the callback stops itself at its 13th call
  CHECK(cw->calls == 13);
  env.wm.tickWindowAnimations(300);
  CHECK(cw->calls == 13);
  env.wm.startWindowAnimation(cw->id(), 10, true);
  env.wm.tickWindowAnimations(5000);  // more than a second late: one step
  CHECK(cw->calls == 14);
}

TEST_CASE("Sprite::release keeps the pointer valid and removeChild destroys the subtree") {
  sdk::Sprite root;
  auto& a = root.addChild(std::make_unique<sdk::Sprite>(1, "a"));
  auto& b = root.addChild(std::make_unique<sdk::Sprite>(2, "b"));
  b.addChild(std::make_unique<sdk::Sprite>(0, "c"));
  sdk::Sprite* aPtr = &a;
  auto owned = root.release(a);
  REQUIRE(owned);
  CHECK(owned.get() == aPtr);
  CHECK(owned->parent() == nullptr);
  CHECK(root.children().size() == 1);
  b.addChild(std::move(owned));
  CHECK(aPtr->parent() == &b);
  root.removeChild(b);
  CHECK(root.children().empty());
  sdk::Sprite stray;
  CHECK(root.release(stray) == nullptr);
}

TEST_CASE("ScalingTask: a paused tick does nothing, scale changes apply from now, reset and restart leave no jump") {
  ScalingTask t;
  CHECK_FALSE(t.tick(10));  // starts paused
  t.restart();
  CHECK(t.tick(10));
  t.animate();
  CHECK(t.time() == doctest::Approx(10));
  t.scale = 3;
  CHECK(t.tick(10));
  t.animate();
  CHECK(t.time() == doctest::Approx(40));
  t.pause();
  CHECK_FALSE(t.tick(10));
  t.restart();  // the pause gap is not counted
  CHECK(t.tick(10));
  t.animate();
  CHECK(t.time() == doctest::Approx(70));
  t.softReset(1);  // trays: the position carries on
  CHECK(t.time() == doctest::Approx(70));
  t.reset(1);
  CHECK(t.time() == 0);
  CHECK(t.paused);
}

TEST_CASE("ScalingTask::animateDelta is the phase change, across a wrap too") {
  ScalingTask t;
  t.reset(1);
  t.restart();
  double sum = 0;
  for (int i = 0; i < 125; ++i) {  // 1250 ms of a 1000 ms period
    t.tick(10);
    sum += t.animateDelta(1000);
  }
  CHECK(sum == doctest::Approx(1.25));
  CHECK(t.phase(1000) == doctest::Approx(0.25));
}

TEST_CASE("gunRotationDeg points up at 0 and turns clockwise") {
  CHECK(game::gunRotationDeg({1, 0}, {0, 0}) == doctest::Approx(90));
  CHECK(game::gunRotationDeg({0, 1}, {0, 0}) == doctest::Approx(180));
  CHECK(std::cos(game::gunRotationDeg({0, -1}, {0, 0}) * 3.14159265f / 180.0f) == doctest::Approx(1));  // 360 is up too
  CHECK(game::gunRotationDeg({-1, 0}, {0, 0}) == doctest::Approx(270));
}

// ---------------------------------------------------------------- queue, shots, hit test

TEST_CASE("the ingredient bag holds five copies of each entry, with colours in range") {
  script::Rng rng(5);
  const std::vector<std::string> recipe{"a", "b", "c"};
  auto bag = IngredientQueue::makeBag(recipe, 3, false, rng);
  REQUIRE(bag.size() == 15);
  std::map<std::string, int> count;
  for (const auto& e : bag) {
    ++count[e.name];
    CHECK(e.color >= 1);
    CHECK(e.color <= 3);
  }
  CHECK(count["a"] == 5);
  CHECK(count["b"] == 5);
  CHECK(count["c"] == 5);
  bool sawWild = false;
  for (int i = 0; i < 20; ++i)
    for (const auto& e : IngredientQueue::makeBag(recipe, 3, true, rng)) sawWild |= e.color == 0;
  CHECK(sawWild);  // rainbow allows 0
  for (const auto& e : IngredientQueue::makeBag(recipe, 1, false, rng)) CHECK(e.color == 0);
  CHECK(IngredientQueue::makeBag({}, 1, false, rng).empty());
}

TEST_CASE("the conveyor is ready after 120 steps, drops the head to 0.8 and is ready again 24 steps later") {
  test::ScriptEnv env(std::string(CHOCO_TEST_ASSETS) + "/ledger");
  game::Textures tex(env.wm, nullptr);
  sdk::Sprite base;
  IngredientQueue q({"a", "b"}, 1, false, gfx::Curve::fromPoints({{0, 0}, {100, 0}, {200, 0}, {300, 0}}), 1.0 / 5, 1200, base);
  q.clock.restart();
  auto step = [&] {
    q.clock.tick(10);
    q.update(q.clock.animateDelta(q.tripMs), env.rng, tex);
  };
  for (int i = 0; i < 119; ++i) step();
  CHECK_FALSE(q.ready());
  step();
  CHECK(q.ready());
  CHECK(q.onBelt() == 5);
  Ingredient* front = q.pop();
  REQUIRE(front);
  CHECK(front->pos.x == doctest::Approx(300));
  CHECK(q.head() == doctest::Approx(0.8));
  CHECK(q.pop() == nullptr);
  for (int i = 0; i < 23; ++i) step();
  CHECK_FALSE(q.ready());
  step();
  CHECK(q.ready());
  q.reset();
  CHECK(q.onBelt() == 0);
  CHECK(q.head() == 0);
}

TEST_CASE("a shot flies 40 px per step and ends at step 25; the coffee form ends at distance / 4 ms") {
  sdk::Sprite base;
  auto& ing = static_cast<Ingredient&>(base.addChild(std::make_unique<Ingredient>("a")));
  game::Flight f;
  f.ing = &ing;
  f.start = {0, 0};
  f.dir = {1000, 0};
  CHECK(f.tick(0.04) < 1);
  CHECK(ing.pos.x == doctest::Approx(40));
  double u = 0;
  int steps = 1;
  while (u < 1 && steps < 100) u = f.tick(0.04), ++steps;
  CHECK(steps == 25);
  CHECK(ing.pos.x == doctest::Approx(1000));
  game::Flight c;
  c.ing = &ing;
  c.start = {0, 0};
  c.dir = {400, 0};
  c.distance = 400;  // 100 ms
  int n = 0;
  while (c.tick(0.04) < 1) ++n;
  CHECK(n == 9);  // the 10th step (100 ms) arrives
}

TEST_CASE("segmentHitsCircle: tangent, pass-through and near miss at 91") {
  using game::segmentHitsCircle;
  CHECK(segmentHitsCircle({0, 91}, {100, 91}, {50, 0}, 91));      // tangent
  CHECK(segmentHitsCircle({0, 0}, {100, 0}, {50, 10}, 91));       // through
  CHECK_FALSE(segmentHitsCircle({0, 92}, {100, 92}, {50, 0}, 91));  // near miss
  CHECK(segmentHitsCircle({0, 0}, {10, 0}, {-50, 0}, 91));        // the end point counts
  CHECK_FALSE(segmentHitsCircle({0, 0}, {10, 0}, {-100, 0}, 91));  // past the start: the segment, not the line
  CHECK(segmentHitsCircle({5, 5}, {5, 5}, {5, 50}, 91));          // a zero-length segment
}

// ---------------------------------------------------------------- trays and the pure rules

TEST_CASE("tray slot offsets, colour rules, speed and tray choice") {
  const auto s0 = ChocolateTray::slotOffset(0, 2);
  const auto s1 = ChocolateTray::slotOffset(1, 2);
  CHECK(s0.x == doctest::Approx(0));
  CHECK(s0.y == doctest::Approx(-50));
  CHECK(s1.x == doctest::Approx(0).epsilon(1e-4));
  CHECK(s1.y == doctest::Approx(50));
  const auto t1 = ChocolateTray::slotOffset(1, 3);
  CHECK(t1.x == doctest::Approx(std::sin(2 * 3.14159265 / 3) * 50));
  CHECK(t1.y == doctest::Approx(25).epsilon(1e-3));

  int re = 0;
  CHECK(game::trayColorAfter(0, 2, &re) == 2);
  CHECK(re == -1);
  CHECK(game::trayColorAfter(2, 0, &re) == 2);  // a wild ingredient takes the tray's colour
  CHECK(re == 2);
  CHECK(game::trayColorAfter(1, 2, &re) == 4);
  CHECK(game::trayColorAfter(4, 3, &re) == 4);
  CHECK(game::trayColorAfter(4, 0, &re) == 4);
  CHECK(re == 4);
  CHECK(game::trayColorAfter(3, 3, &re) == 3);
  CHECK(game::trayColorAfter(0, 0, &re) == 0);

  double s = 1;
  for (int i = 0; i < 3; ++i) s = game::speedAfterFull(s, 1.1, 8);
  CHECK(s == doctest::Approx(1.331));
  for (int i = 0; i < 40; ++i) s = game::speedAfterFull(s, 1.1, 8);
  CHECK(s == 8);
  CHECK(game::speedAfterMiss(8) == 4);
  CHECK(game::speedAfterMiss(1.3) == 1);
  CHECK(game::speedAfterMiss(1) == 1);

  std::vector<game::TrayChoice> c{{false, 2, 0, {}}, {true, 0, 0, {}}, {true, 1, 2, {}}, {true, 1, 0, {}}};
  CHECK(game::chooseTray(c, 0) == 3);  // the fullest compatible; tray 2 is another colour
  CHECK(game::chooseTray(c, 2) == 2);  // fullest of the compatible ones: colour 2 or 0
  c[1].accepts = c[2].accepts = c[3].accepts = false;
  CHECK(game::chooseTray(c, 0) == -1);
}

TEST_CASE("tray crossing is wrap-aware and canAccept ignores colour") {
  ChocolateTray t(0, {"a", "b"}, 0, 0.5);
  t.last = 0.4;
  CHECK(t.crossed(0.6));
  t.last = 0.6;
  CHECK_FALSE(t.crossed(0.7));
  t.last = 0.9;
  CHECK_FALSE(t.crossed(0.3));  // wraps, but 0.5 is not in [0.9,1) or [0,0.3]
  CHECK(t.crossed(0.55));       // wraps (0.55 < 0.9) and 0.5 <= 0.55
  ChocolateTray z(0, {"a", "b"}, 0, 0);
  z.last = 0.99;
  CHECK(z.crossed(0.01));  // the first point of the path is crossed on every lap
  Ingredient a("a"), b("b"), c("c");
  CHECK(z.canAccept(a));
  CHECK_FALSE(z.canAccept(c));
  z.slots[0] = &a;
  CHECK_FALSE(z.canAccept(a));
  CHECK(z.canAccept(b));
  z.slots[0] = nullptr;
}

// ---------------------------------------------------------------- the factory model

namespace {

struct FillCall {
  std::string file;
  std::string text;
  bool hasText;
};

std::shared_ptr<ui::MapStyle> pts(std::initializer_list<std::pair<double, double>> p) {
  auto t = std::make_shared<ui::MapStyle>();
  for (auto [x, y] : p) {
    auto e = std::make_shared<ui::MapStyle>();
    e->append(x).append(y);
    t->append(e);
  }
  return t;
}

struct FactoryFixture : test::ScriptEnv {
  explicit FactoryFixture(int colorCount = 1, std::vector<std::string> recipe = {"a", "b"}, bool warn = false, int trayCount = 5,
                          bool withRecycler = false,
                          choco::audio::Audio* audioService = nullptr)  // captured by the factory at construction
      : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ledger") {
    ctx.audio = audioService;
    modal = &wm.pushModal("m");
    modal->setRect({0, 0, 800, 600});
    auto parentOwned = std::make_unique<ui::PlaceholderWindow>(wm, "Bitmap");
    parent = &modal->adoptFront(std::move(parentOwned));
    parent->setRect({0, 0, 800, 600});
    for (const char* n : {"go", "pause"}) {
      auto b = std::make_unique<ui::PlaceholderWindow>(wm, "Window");
      b->setName(n);
      parent->adoptFront(std::move(b));
    }
    ui::MapStyle st;
    auto rec = std::make_shared<ui::MapStyle>();
    for (const auto& r : recipe) rec->append(r);
    st.set("recipe", rec);
    st.set("colorcount", static_cast<double>(colorCount));
    st.set("warnOnMiss", warn ? 1.0 : 0.0);
    st.set("conveyorpath", pts({{0, 500}, {100, 500}, {200, 500}, {300, 500}}));
    st.set("traypath", pts({{700, 10}, {700, 10}, {700, 10}, {700, 10}}));
    st.set("productpath", pts({{696, 300}, {696, 320}, {696, 500}, {696, 544}}));
    st.set("traycount", static_cast<double>(trayCount));
    if (withRecycler) {
      st.set("recycler", 1.0);
      st.set("recyclerpath", pts({{400, 300}, {400, 300}, {400, 300}, {400, 300}}));
    }
    auto missedPts = pts({{10, 100}, {20, 100}, {30, 100}, {40, 100}, {50, 100}, {60, 100}, {70, 100}, {80, 100}});
    st.set("missed", missedPts);
    auto w = std::make_unique<ChocolateFactory>(wm, ctx, [this](lua_State*, ui::Window&, const std::string& file, const std::string* text) {
      fills.push_back({file, text ? *text : "", text != nullptr});
    });
    f = static_cast<ChocolateFactory*>(&parent->adoptFront(std::move(w)));
    f->init(st);
    f->setRect({0, 0, 800, 600});
    f->postChildrenInit(st);
    // As in the game, `information` is created after the factory, so the first PauseGame has nothing to fill.
    auto infoOwned = std::make_unique<ui::PlaceholderWindow>(wm, "Bitmap");
    info = &parent->adoptFront(std::move(infoOwned));
    info->setName("information");
  }
  void step(int n = 1) {
    for (int i = 0; i < n; ++i) f->onAnimate();
  }
  void start() {
    f->startGame(lua.L());
    step(299);
    REQUIRE(f->state() == State::Countdown);
    step(1);
    REQUIRE(f->state() == State::Playing);
  }
  // Steps until the conveyor can fire (it is exactly 1.0 once ready).
  void untilReady() {
    for (int i = 0; i < 400 && !f->conveyor()->ready(); ++i) step();
    REQUIRE(f->conveyor()->ready());
  }
  Ingredient& newIngredient(const std::string& name, int color = 0) {
    auto ing = std::make_unique<Ingredient>(name);
    if (color) ing->setColor(color, tex);
    return static_cast<Ingredient&>(f->spriteTree().addChild(std::move(ing)));
  }
  game::Textures tex{wm, nullptr};
  ui::Window* modal = nullptr;
  ui::Window* parent = nullptr;
  ui::Window* info = nullptr;
  ChocolateFactory* f = nullptr;
  std::vector<FillCall> fills;
};

}  // namespace

TEST_CASE("the countdown lasts 299 steps, then play lasts 6000 steps and ends in ENDING") {
  FactoryFixture fx;
  CHECK(fx.f->state() == State::Countdown);
  CHECK(fx.f->paused());  // the intro overlay keeps its own Lua content: no fill happened
  CHECK(fx.fills.empty());
  fx.f->startGame(fx.lua.L());
  CHECK_FALSE(fx.f->paused());
  fx.step(298);
  CHECK(fx.f->state() == State::Countdown);
  fx.step(1);
  CHECK(fx.f->state() == State::Countdown);
  fx.step(1);
  CHECK(fx.f->state() == State::Playing);
  CHECK(fx.f->clockMs() == 60000);
  fx.step(5999);
  CHECK(fx.f->state() == State::Playing);
  CHECK(fx.f->clockMs() == 10);
  fx.step(1);  // the 6000th playing step
  CHECK(fx.f->state() == State::Ending);
}

TEST_CASE("PauseGame fills the sibling information window with the pause script and ResumeGame hides it") {
  FactoryFixture fx;
  fx.start();
  fx.f->pauseGame(fx.lua.L(), "choco_first_missed");
  REQUIRE(fx.fills.size() == 1);  // StartGame's ResetGame found the game already paused
  CHECK(fx.fills[0].file == "ui/game_pause.lua");
  CHECK(fx.fills[0].text == "choco_first_missed");
  CHECK(fx.f->paused());
  CHECK(fx.info->shown());
  CHECK(fx.ctx.wm.windowAnimationCount() == 0);
  const int clock = fx.f->clockMs();
  fx.f->pauseGame(fx.lua.L(), "again");  // already paused: nothing more
  CHECK(fx.fills.size() == 1);
  fx.f->resumeGame(fx.lua.L());
  CHECK_FALSE(fx.f->paused());
  CHECK_FALSE(fx.info->shown());
  CHECK(fx.ctx.wm.windowAnimationCount() == 1);
  fx.step();
  CHECK(fx.f->clockMs() == clock - 10);
}

TEST_CASE("PauseGame pauses CADI audio once the game started, ResumeGame lifts it, and a factory destroyed while paused unpauses") {
  choco::audio::Audio audio([](const std::string&) { return std::optional<choco::audio::SoundCache::Bytes>(); }, choco::audio::EventTable::empty(), [](const std::string&) {});
  FactoryFixture fx(1, {"a", "b"}, false, 5, false, &audio);
  fx.start();
  CHECK_FALSE(audio.gamePaused());
  fx.f->pauseGame(fx.lua.L(), "game_paused");
  CHECK(audio.gamePaused());
  CHECK(audio.mixer().cadiPaused());
  CHECK_FALSE(audio.focusPaused());  // the game pause leaves the TSound class (button clicks) alone
  CHECK_FALSE(audio.mixer().systemPaused());
  fx.f->resumeGame(fx.lua.L());
  CHECK_FALSE(audio.gamePaused());
  fx.f->pauseGame(fx.lua.L(), "game_paused");
  REQUIRE(audio.gamePaused());
  ui::Window* w = fx.f;
  fx.wm.destroyWindow(*w);
  fx.wm.clearDestroyed();
  CHECK_FALSE(audio.gamePaused());  // deviation from the original, docs/design/audio.md decision 12
  fx.ctx.audio = nullptr;
}

TEST_CASE("a click while paused fires nothing (a deliberate fix of the original)") {
  FactoryFixture fx;
  fx.start();
  fx.untilReady();
  fx.f->pauseGame(fx.lua.L(), "");
  fx.f->onMouseDown(10, 10);
  CHECK(fx.f->shots() == 0);
  fx.f->resumeGame(fx.lua.L());
  fx.f->onMouseDown(10, 10);
  CHECK(fx.f->shots() == 1);
}

TEST_CASE("a shot that misses is parked, halves nothing at speed 1 and the 240 ms cooldown applies") {
  FactoryFixture fx;
  fx.start();
  fx.untilReady();
  fx.f->onMouseDown(0, 0);
  CHECK(fx.f->shots() == 1);
  fx.f->onMouseDown(0, 0);  // not ready: the click is lost
  CHECK(fx.f->shots() == 1);
  fx.step(24);
  CHECK(fx.f->conveyor()->ready());
  fx.step(1);
  CHECK(fx.f->shots() == 0);  // 25 steps after the shot
  CHECK(fx.f->misses() == 1);
  REQUIRE(fx.f->parked().size() == 1);
  CHECK(fx.f->parked()[0]->pos.x == doctest::Approx(10));
  CHECK(fx.f->parked()[0]->scale == doctest::Approx(0.6f));
  CHECK(fx.f->traySpeed() == 1);
}

TEST_CASE("a miss halves a fast tray speed, never below 1") {
  FactoryFixture fx(1, {"a", "b"}, false, 1);
  fx.start();
  auto& tray = *fx.f->trays()[0];
  for (int i = 0; i < 2; ++i) {  // one tray, two names: fill it twice to speed up twice
    fx.f->forceHit(tray, fx.newIngredient("a"));
    fx.f->forceHit(tray, fx.newIngredient("b"));
    tray.clear();
  }
  CHECK(fx.f->traySpeed() == doctest::Approx(1.21));
  fx.untilReady();
  fx.f->onMouseDown(0, 0);
  fx.step(25);
  CHECK(fx.f->misses() == 1);
  CHECK(fx.f->traySpeed() == 1);
}

TEST_CASE("the first miss pauses with choco_first_missed when warnOnMiss is on") {
  FactoryFixture fx(1, {"a", "b"}, true);
  fx.start();
  fx.untilReady();
  fx.f->onMouseDown(0, 0);
  fx.step(25);
  CHECK(fx.f->misses() == 1);
  CHECK(fx.f->paused());
  REQUIRE_FALSE(fx.fills.empty());
  CHECK(fx.fills.back().file == "ui/game_pause.lua");
  CHECK(fx.fills.back().text == "choco_first_missed");
}

TEST_CASE("the 8th miss aborts, the tray scale decays by 0.95 per step and game over fills information") {
  FactoryFixture fx;
  fx.start();
  for (int i = 0; i < 8; ++i) {
    CHECK(fx.f->state() == State::Playing);
    fx.untilReady();
    fx.f->onMouseDown(0, 0);
    fx.step(25);
  }
  CHECK(fx.f->misses() == 8);
  CHECK(fx.f->state() == State::Aborted);
  CHECK(fx.f->parked().size() == 8);
  int steps = 0;
  while (fx.f->state() == State::Aborted && steps < 500) fx.step(), ++steps;
  // From scale 1: 0.95^n < 0.05 after 59 steps.
  CHECK(steps == 59);
  CHECK(fx.f->state() == State::Over);
  REQUIRE_FALSE(fx.fills.empty());
  CHECK(fx.fills.back().file == "ui/chocolate_gameover.lua");
  CHECK_FALSE(fx.fills.back().hasText);
  CHECK(fx.ctx.wm.windowAnimationCount() == 0);
  CHECK(fx.f->score() == 0);
}

TEST_CASE("a hit places the ingredient on its slot, the tray fills, and a plain full tray scores a product on crossing") {
  FactoryFixture fx(1, {"a", "b"}, false, 1);
  fx.start();
  ChocolateTray& tray = *fx.f->trays()[0];
  Ingredient& a = fx.newIngredient("a");
  fx.f->forceHit(tray, a);
  CHECK(tray.fill == 1);
  CHECK(tray.slots[0] == &a);
  CHECK(a.parent() == &tray);
  CHECK(a.pos.y == doctest::Approx(-50));
  CHECK_FALSE(tray.full());
  fx.f->forceHit(tray, fx.newIngredient("b"));
  CHECK(tray.full());
  CHECK(fx.f->traySpeed() == doctest::Approx(1.1));
  CHECK(fx.f->productsOnBelt() == 0);
  // The path is one point, so the phase crosses the endpoint 0 once per lap: 30000 / 1.1 ms, plus the 3000 ms belt.
  int steps = 0;
  while (fx.f->score() == 0 && steps < 5000) fx.step(), ++steps;
  CHECK(fx.f->score() == 1);
  CHECK(fx.f->bonus() == 0);
  CHECK(tray.fill == 0);
}

TEST_CASE("a colour match scores a product and one ring per slot at once and clears the tray") {
  FactoryFixture fx(2, {"a", "b", "c"}, false, 1);
  fx.start();
  ChocolateTray& tray = *fx.f->trays()[0];
  for (const char* n : {"a", "b"}) fx.f->forceHit(tray, fx.newIngredient(n, 1));
  CHECK(tray.color == 1);
  CHECK(tray.ring->visible);
  CHECK(fx.f->ringsFlying() == 0);
  fx.f->forceHit(tray, fx.newIngredient("c", 1));
  CHECK(tray.fill == 0);  // cleared at once
  CHECK(tray.color == 0);
  CHECK(fx.f->ringsFlying() == 3);
  CHECK(fx.f->productsOnBelt() == 1);
  fx.step(60);  // rings: 500 ms
  CHECK(fx.f->ringsFlying() == 0);
  CHECK(fx.f->score() == 3);
  CHECK(fx.f->bonus() == 3);
  fx.step(260);  // the product: 3000 ms in all
  CHECK(fx.f->score() == 4);
  CHECK(fx.f->bonus() == 3);
}

TEST_CASE("a wild ingredient takes the tray colour and two real colours make the tray white") {
  FactoryFixture fx(3, {"a", "b", "c"}, false, 2);
  fx.start();
  ChocolateTray& t0 = *fx.f->trays()[0];
  fx.f->forceHit(t0, fx.newIngredient("a", 2));
  Ingredient& wild = fx.newIngredient("b", 0);
  fx.f->forceHit(t0, wild);
  CHECK(t0.color == 2);
  CHECK(wild.color() == 2);
  ChocolateTray& t1 = *fx.f->trays()[1];
  fx.f->forceHit(t1, fx.newIngredient("a", 1));
  fx.f->forceHit(t1, fx.newIngredient("b", 3));
  CHECK(t1.color == 4);
  fx.f->forceHit(t1, fx.newIngredient("c", 1));
  CHECK(t1.full());
  CHECK(t1.color == 4);
  CHECK(fx.f->ringsFlying() == 0);  // white is a mismatch: no rings, no instant product
}

TEST_CASE("all trays full at once: each tray is cleared and makes one product, no rings") {
  FactoryFixture fx(1, {"a"}, false, 5);
  fx.start();
  // Fill four trays (one slot each): they are full and waiting. The fifth completes the board.
  for (int i = 0; i < 5; ++i) fx.f->forceHit(*fx.f->trays()[static_cast<std::size_t>(i)], fx.newIngredient("a"));
  CHECK(fx.f->productsOnBelt() == 0);  // the board test runs on the tray task's next tick
  fx.step();
  CHECK(fx.f->productsOnBelt() == 5);
  CHECK(fx.f->ringsFlying() == 0);
  // The belt queues: each product waits until the one ahead is 0.1 along (300 ms), so they arrive 30 steps apart.
  std::vector<int> arrivals;
  for (int i = 1; i <= 800 && arrivals.size() < 5; ++i) {
    const std::size_t before = fx.f->productsOnBelt();
    fx.step();
    if (fx.f->productsOnBelt() < before) arrivals.push_back(i);
  }
  REQUIRE(arrivals.size() == 5);
  for (std::size_t i = 1; i < 5; ++i) CHECK(arrivals[i] - arrivals[i - 1] == 30);
  for (auto* t : fx.f->trays()) CHECK(t->fill == 0);
}

TEST_CASE("a model run: two plain trays and one matched tray of three score 2 + 4 once the belts drain") {
  FactoryFixture fx(2, {"a", "b", "c"}, false, 3);
  fx.start();
  // Plain trays need colour 0 for every ingredient: a wild ingredient on an uncoloured tray changes nothing.
  for (int t = 0; t < 2; ++t)
    for (const char* n : {"a", "b", "c"}) fx.f->forceHit(*fx.f->trays()[static_cast<std::size_t>(t)], fx.newIngredient(n, 0));
  for (const char* n : {"a", "b", "c"}) fx.f->forceHit(*fx.f->trays()[2], fx.newIngredient(n, 2));
  // The third tray cleared at once (3 rings + 1 product). The first two wait for their crossing: three trays are full
  // only momentarily, so no board bonus: the matched tray was already cleared.
  int steps = 0;
  while (steps < 9000 && (fx.f->score() < 6)) fx.step(), ++steps;
  CHECK(fx.f->score() == 6);
  CHECK(fx.f->bonus() == 3);
}

TEST_CASE("ENDING waits for flights, rings, products and full trays, then decays to OVER") {
  FactoryFixture fx(1, {"a", "b"}, false, 1);
  fx.start();
  ChocolateTray& tray = *fx.f->trays()[0];
  fx.f->forceHit(tray, fx.newIngredient("a"));
  fx.f->forceHit(tray, fx.newIngredient("b"));  // full: speed 1.1, waiting to cross
  fx.step(6000 - 0);                            // run the clock out (the tray may deliver meanwhile)
  while (fx.f->state() == State::Playing) fx.step();
  CHECK(fx.f->state() == State::Ending);
  int guard = 0;
  while (fx.f->state() == State::Ending && guard++ < 20000) fx.step();
  CHECK(fx.f->state() == State::Over);
  CHECK(fx.f->score() >= 1);  // the full tray was delivered and counted before the decay began
  CHECK(fx.f->productsOnBelt() == 0);
  CHECK(fx.fills.back().file == "ui/chocolate_gameover.lua");
}

TEST_CASE("the recycler eats a shot without a hit or a miss") {
  FactoryFixture fx(1, {"a", "b"}, false, 5, true);
  fx.start();
  REQUIRE(fx.f->hasRecycler());
  fx.untilReady();
  const std::size_t sprites = fx.f->spriteTree().children().size();
  const gfx::PointF target = fx.f->recyclerPos();
  CHECK(fx.f->onMouseDown(static_cast<int>(target.x), static_cast<int>(target.y)) == false);
  CHECK(fx.f->shots() == 1);
  for (int i = 0; i < 60 && fx.f->shots() > 0; ++i) fx.step();
  CHECK(fx.f->shots() == 0);
  CHECK(fx.f->recycled() == 1);
  CHECK(fx.f->misses() == 0);
  CHECK(fx.f->parked().empty());
  CHECK(fx.f->spriteTree().children().size() == sprites);  // the shot's ingredient sprite is gone
}

TEST_CASE("destroying the factory mid-run leaves nothing dangling") {
  FactoryFixture fx(2, {"a", "b"}, false, 2);
  fx.start();
  fx.f->forceHit(*fx.f->trays()[0], fx.newIngredient("a", 1));
  fx.untilReady();
  fx.f->onMouseDown(0, 0);
  fx.step(10);
  fx.untilReady();
  fx.f->onMouseDown(0, 0);
  fx.step(26);  // one shot parked
  CHECK(fx.f->misses() >= 1);
  fx.f->pauseGame(fx.lua.L(), "");
  ui::Window* w = fx.f;
  fx.wm.destroyWindow(*w);
  fx.wm.clearDestroyed();
  CHECK(FactoryWindow::active() == nullptr);
  fx.wm.tickWindowAnimations(1000);  // the animation entry died with the window
  CHECK(fx.wm.windowAnimationCount() == 0);
}

TEST_CASE("the autoshoot policy answers only when a shot can be placed") {
  FactoryFixture fx(1, {"a", "b"}, false, 2);
  CHECK_FALSE(fx.f->autoAim().has_value());  // not playing
  fx.start();
  CHECK_FALSE(fx.f->autoAim().has_value());  // the conveyor is not ready
  fx.untilReady();
  auto p = fx.f->autoAim();
  REQUIRE(p.has_value());
  CHECK(p->x == doctest::Approx(700));
  fx.f->onMouseDown(static_cast<int>(p->x), static_cast<int>(p->y));
  CHECK_FALSE(fx.f->autoAim().has_value());  // a shot is in flight
}

TEST_CASE("the natives do nothing without a factory and GetScore gives 0") {
  test::ScriptEnv env(std::string(CHOCO_TEST_ASSETS) + "/ledger");
  script::registerFactoryNatives(env.lua.L(), env.ctx);
  CHECK(env.run("StartGame(); PauseGame('x'); ResumeGame(); gS = GetScore()") == 0);
  CHECK(env.getNumber("gS") == 0);
}

TEST_CASE("loadAppearance: a code gives the _big item, lists give tinted custom layers") {
  ui::MapStyle code;
  code.set("code", std::string("b01"));
  auto a = game::loadAppearance(code);
  REQUIRE(a.size() == 1);
  CHECK(a[0].image == "items/b01_big");
  CHECK(a[0].tint == gfx::ColorF{1, 1, 1, 1});

  ui::MapStyle list;
  auto tinted = std::make_shared<ui::MapStyle>();
  tinted->append(std::string("bar/layer1_01")).append(0.5).append(0.2).append(0.0);
  list.append(tinted);
  list.append(std::string("bar/layer2_03"));
  auto b = game::loadAppearance(list);
  REQUIRE(b.size() == 2);
  CHECK(b[0].image == "custom/bar/layer1_01");
  CHECK(b[0].tint == gfx::ColorF{0.5f, 0.2f, 0.0f, 1});
  CHECK(b[1].image == "custom/bar/layer2_03");
  CHECK(b[1].tint == gfx::ColorF{1, 1, 1, 1});

  ui::MapStyle product;  // a product table with an `appearance` list
  auto lst = std::make_shared<ui::MapStyle>();
  lst->append(std::string("bar/layer1_02"));
  product.set("appearance", lst);
  product.set("code", std::string("ignored"));
  auto c = game::loadAppearance(product);
  REQUIRE(c.size() == 1);
  CHECK(c[0].image == "custom/bar/layer1_02");
}

TEST_CASE("Try Again from the game-over button: the button's window dies while its command runs") {
  FactoryFixture fx;
  fx.start();
  for (int i = 0; i < 8; ++i) {
    fx.untilReady();
    fx.f->onMouseDown(0, 0);
    fx.step(25);
  }
  int guard = 0;
  while (fx.f->state() != State::Over && guard++ < 500) fx.step();
  REQUIRE(fx.f->state() == State::Over);
  // The game-over dialog's `done`/`again` button lives in `information`; FillWindow destroys it (deferred).
  auto btnOwned = std::make_unique<ui::PlaceholderWindow>(fx.wm, "Window");
  btnOwned->setName("again");
  ui::Window& btn = fx.info->adoptFront(std::move(btnOwned));
  // The command: StartGame, then (inside) FillWindow destroys the button, then more calls on the factory.
  fx.f->startGame(fx.lua.L());
  fx.wm.destroyWindow(btn);
  fx.f->pauseGame(fx.lua.L(), "");
  fx.f->resumeGame(fx.lua.L());
  fx.wm.clearDestroyed();
  fx.step(300);
  CHECK(fx.f->state() == State::Playing);
  CHECK(fx.f->misses() == 0);
  CHECK(FactoryWindow::active() == fx.f);
}

TEST_CASE("closing the dialog during the countdown leaves nothing dangling") {
  FactoryFixture fx(2, {"a", "b"}, false, 2);
  fx.f->startGame(fx.lua.L());
  fx.step(100);
  CHECK(fx.f->state() == State::Countdown);
  ui::Window* w = fx.f;
  fx.wm.destroyWindow(*w);
  fx.wm.clearDestroyed();
  CHECK(FactoryWindow::active() == nullptr);
  fx.wm.tickWindowAnimations(1000);
  CHECK(fx.wm.windowAnimationCount() == 0);
}

TEST_CASE("Escape pauses while playing but not on the game-over screen") {
  FactoryFixture fx;
  fx.start();
  ui::KeyEvent ev;
  ev.name = "Escape";
  fx.f->onKeyDown(ev);
  CHECK(fx.f->paused());
  fx.f->resumeGame(fx.lua.L());
  for (int i = 0; i < 8; ++i) {
    fx.untilReady();
    fx.f->onMouseDown(0, 0);
    fx.step(25);
  }
  int guard = 0;
  while (fx.f->state() != State::Over && guard++ < 500) fx.step();
  REQUIRE(fx.f->state() == State::Over);
  fx.f->resumeGame(fx.lua.L());  // no-op safety
  const bool wasPaused = fx.f->paused();
  fx.f->onKeyDown(ev);
  CHECK(fx.f->paused() == wasPaused);
  CHECK(fx.fills.back().file == "ui/chocolate_gameover.lua");
}

// ---------------------------------------------------------------- the coffee factory

namespace {

CoffeeGrid namedGrid(int cols, int rows, const std::vector<std::string>& rowsText) {
  CoffeeGrid g;
  g.configure(cols, rows, 45, 45, {146, 15});
  for (int r = 0; r < rows && r < static_cast<int>(rowsText.size()); ++r)
    for (int c = 0; c < cols; ++c) {
      const char ch = rowsText[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
      if (ch != '.') g.slots[static_cast<std::size_t>(r * cols + c)].name = std::string(1, ch);
    }
  return g;
}

struct CoffeeFixture : test::ScriptEnv {
  explicit CoffeeFixture(std::vector<std::string> recipe = {"a", "b"}, bool warn = false)
      : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ledger") {
    modal = &wm.pushModal("m");
    modal->setRect({0, 0, 800, 600});
    auto parentOwned = std::make_unique<ui::PlaceholderWindow>(wm, "Bitmap");
    parent = &modal->adoptFront(std::move(parentOwned));
    parent->setRect({0, 0, 800, 600});
    for (const char* n : {"go", "pause"}) {
      auto b = std::make_unique<ui::PlaceholderWindow>(wm, "Window");
      b->setName(n);
      parent->adoptFront(std::move(b));
    }
    ui::MapStyle st;
    auto rec = std::make_shared<ui::MapStyle>();
    for (const auto& r : recipe) rec->append(r);
    st.set("recipe", rec);
    st.set("warnOnMiss", warn ? 1.0 : 0.0);
    st.set("conveyorpath", pts({{0, 500}, {100, 500}, {200, 500}, {300, 500}}));
    st.set("productpath", pts({{696, 300}, {696, 320}, {696, 500}, {696, 544}}));
    st.set("columns", 6.0);
    st.set("rows", 9.0);
    st.set("gridx", 146.0);
    st.set("gridy", 15.0);
    st.set("griddx", 45.0);
    st.set("griddy", 45.0);
    st.set("gridspeed", 1e9);  // the scroll all but stands still, so a test controls the grid
    st.set("productspacing", 1000.0);
    auto mb = std::make_shared<ui::MapStyle>();
    for (double v : {0, 0, 0, 1, 2, 2, 3, 3, 4, 5}) mb->append(v);
    st.set("matchBonus", mb);
    st.set("missed", pts({{10, 100}, {20, 100}, {30, 100}, {40, 100}, {50, 100}, {60, 100}, {70, 100}, {80, 100}}));
    auto w = std::make_unique<CoffeeFactory>(wm, ctx, [this](lua_State*, ui::Window&, const std::string& file, const std::string* text) {
      fills.push_back({file, text ? *text : "", text != nullptr});
    });
    f = static_cast<CoffeeFactory*>(&parent->adoptFront(std::move(w)));
    f->init(st);
    f->setRect({0, 0, 800, 600});
    f->postChildrenInit(st);
    auto infoOwned = std::make_unique<ui::PlaceholderWindow>(wm, "Bitmap");
    info = &parent->adoptFront(std::move(infoOwned));
    info->setName("information");
  }
  void step(int n = 1) {
    for (int i = 0; i < n; ++i) f->onAnimate();
  }
  void start() {
    f->startGame(lua.L());
    step(300);
    REQUIRE(f->state() == State::Playing);
  }
  void untilReady() {
    for (int i = 0; i < 400 && !f->conveyor()->ready(); ++i) step();
    REQUIRE(f->conveyor()->ready());
  }
  void fire(gfx::PointF p) { f->onMouseDown(static_cast<int>(p.x), static_cast<int>(p.y)); }
  ui::Window* modal = nullptr;
  ui::Window* parent = nullptr;
  ui::Window* info = nullptr;
  CoffeeFactory* f = nullptr;
  std::vector<FillCall> fills;
};

constexpr int idxAt(int col, int row) { return row * 6 + col; }

}  // namespace

TEST_CASE("coffee grid: slotIndex edges, right and bottom exclusive, and it follows the scrolled position") {
  CoffeeGrid g;
  g.configure(6, 9, 45, 45, {146, 15});
  CHECK(g.slotIndex({146, 15}) == 0);
  CHECK(g.slotIndex({145.9f, 15}) == -1);
  CHECK(g.slotIndex({146, 14.9f}) == -1);
  CHECK(g.slotIndex({191, 15}) == 1);  // the shared edge belongs to the right cell
  CHECK(g.slotIndex({146 + 270 - 0.1f, 15 + 405 - 0.1f}) == 53);
  CHECK(g.slotIndex({146 + 270, 20}) == -1);
  CHECK(g.slotIndex({150, 15 + 405}) == -1);
  g.pos.y = 40;  // scrolled down
  CHECK(g.slotIndex({150, 20}) == -1);
  CHECK(g.slotIndex({150, 40}) == 0);
  CHECK(g.slotIndex({150, 40 + 405 - 1}) == 48);
  const gfx::PointF c = g.slotCentre(7);  // col 1, row 1
  CHECK(c.x == doctest::Approx(67.5f));
  CHECK(c.y == doctest::Approx(67.5f));
}

TEST_CASE("coffee grid: runs, and floodClear takes the whole connected group in the original's order") {
  CoffeeGrid g = namedGrid(5, 4, {"aab..", ".abb.", ".a.b.", "....."});
  CHECK(g.countHorizontal(1) == 2);
  CHECK(g.countVertical(6) == 3);  // (1,1): a above and below
  CHECK(g.countHorizontal(7) == 2);
  CHECK(g.countHorizontal(3) == 0);  // a free cell
  auto cleared = g.floodClear(1);
  std::vector<int> got;
  for (const auto& c : cleared) got.push_back(c.index);
  CHECK(got == std::vector<int>{0, 1, 6, 11});  // all four a's, not only a line of three
  CHECK(g.slots[2].name == "b");                 // the b group is untouched
  CHECK(g.filled() == 4);
  CHECK(g.floodClear(1).empty());

  CoffeeGrid plus = namedGrid(3, 3, {".x.", "xxx", ".x."});
  got.clear();
  for (const auto& c : plus.floodClear(4)) got.push_back(c.index);
  CHECK(got == std::vector<int>{3, 1, 4, 5, 7});  // left, up, self, right, down
  CHECK(plus.filled() == 0);
}

TEST_CASE("coffee grid: shiftRows moves rows down, drops the bottom row and refills from RandRange(0, 2N)") {
  CoffeeGrid g = namedGrid(3, 3, {"x.y", "pqr", "s.t"});
  Ingredient s("s"), t("t"), keep("p");
  g.slots[6].ing = &s;
  g.slots[8].ing = &t;
  g.slots[3].ing = &keep;
  script::Rng rng(7), mirror(7);
  const std::vector<std::string> recipe{"a", "b"};  // N = 2: a cell fills when the roll is 0 or 1 of 0..4
  auto fallen = g.shiftRows(rng, recipe, nullptr);
  REQUIRE(fallen.size() == 2);
  CHECK(fallen[0] == &s);
  CHECK(fallen[1] == &t);
  for (int c = 0; c < 3; ++c) {
    const int roll = mirror.range(0, 4);
    CHECK(g.slots[static_cast<std::size_t>(c)].name == (roll < 2 ? recipe[static_cast<std::size_t>(roll)] : std::string()));
  }
  CHECK(g.slots[3].name == "x");  // the old row 0 is row 1 now
  CHECK(g.slots[5].name == "y");
  CHECK(g.slots[6].name == "p");
  CHECK(g.slots[4].name.empty());
  CHECK(g.slots[6].ing == &keep);
  CHECK(keep.pos.x == doctest::Approx(22.5f));  // snapped to its new cell
  CHECK(keep.pos.y == doctest::Approx(112.5f));
}

TEST_CASE("coffee: the match bonus lookup has 0 in front, clamps past the end and is 0 when the table is absent") {
  const std::vector<int> table{0, 0, 0, 0, 1, 2, 2, 3, 3, 4, 5};
  CHECK(game::matchBonusFor(table, 1) == 0);
  CHECK(game::matchBonusFor(table, 3) == 0);
  CHECK(game::matchBonusFor(table, 4) == 1);
  CHECK(game::matchBonusFor(table, 5) == 2);
  CHECK(game::matchBonusFor(table, 9) == 4);
  CHECK(game::matchBonusFor(table, 10) == 5);
  CHECK(game::matchBonusFor(table, 40) == 5);
  CHECK(game::matchBonusFor({}, 6) == 0);
}

TEST_CASE("coffee hopper: ties go to the later entry, release is all or nothing, the needle is 24 - 4.8 * min(c, 10)") {
  Hopper h({"coffee", "coffee", "cream"});
  CHECK(h.add("coffee"));
  CHECK(h.counts() == std::vector<int>{0, 1, 0});  // both at 0: the later one
  CHECK(h.add("coffee"));
  CHECK(h.counts() == std::vector<int>{1, 1, 0});  // now the smaller count
  CHECK_FALSE(h.add("tea"));
  CHECK_FALSE(h.release());
  CHECK(h.counts() == std::vector<int>{1, 1, 0});  // nothing changed
  CHECK(h.add("cream"));
  CHECK(h.release());
  CHECK(h.counts() == std::vector<int>{0, 0, 0});
  h.add("cream");
  h.reset();
  CHECK(h.counts() == std::vector<int>{0, 0, 0});
  CHECK(Hopper::needleY(0) == doctest::Approx(24));
  CHECK(Hopper::needleY(5) == doctest::Approx(0));
  CHECK(Hopper::needleY(10) == doctest::Approx(-24));
  CHECK(Hopper::needleY(25) == doctest::Approx(-24));
}

TEST_CASE("coffee autoshoot policy: the free slot in rows 1 to rows-2 with the longest run, ties to the lowest index") {
  const int cols = 4, rows = 4;
  std::vector<std::string> names(16);
  names[4] = "a";  // row 1: a . a .
  names[6] = "a";
  CHECK(game::chooseSlot(names, cols, rows, "a") == 5);  // a run of 3 through (1,1)
  CHECK(game::chooseSlot(names, cols, rows, "b") == 5);  // nothing runs: the lowest free index in the band
  std::vector<std::string> full(16, "z");
  CHECK(game::chooseSlot(full, cols, rows, "a") == -1);  // the band is full
  std::vector<std::string> edge(16);
  edge[0] = "a", edge[2] = "a";  // row 0 is outside the band
  CHECK(game::chooseSlot(edge, cols, rows, "a") == 4);
}

TEST_CASE("coffee: the case belt starts a case once the last one is past productspacing / producttime") {
  CHECK_FALSE(game::caseSpawnReady(0.333, 1000, 3000));
  CHECK(game::caseSpawnReady(0.34, 1000, 3000));
  CHECK_FALSE(game::caseSpawnReady(1.0 / 3.0, 1000, 3000));
  CoffeeFixture fx;
  fx.start();
  for (int i = 0; i < 4; ++i) fx.f->hopper().add("a"), fx.f->hopper().add("b");
  std::vector<int> spawns;
  auto total = [&] {
    int n = 0;
    for (int c : fx.f->hopper().counts()) n += c;
    return n;
  };
  int stock = total();
  for (int i = 1; i <= 500; ++i) {
    fx.step();
    if (total() < stock) spawns.push_back(i);  // release() took a set
    stock = total();
  }
  REQUIRE(spawns.size() == 4);
  CHECK(spawns[0] == 1);  // an empty belt starts a case at once
  for (std::size_t i = 1; i < spawns.size(); ++i) {
    const int gap = spawns[i] - spawns[i - 1];
    CHECK(gap >= 100);
    CHECK(gap <= 102);  // 1000 ms of a 3000 ms trip, a step is 1/300
  }
  CHECK(fx.f->hopper().counts() == std::vector<int>{0, 0});
}

TEST_CASE("coffee: a shot into an empty slot lands there, and one into a full slot is a miss that parks") {
  CoffeeFixture fx;
  fx.start();
  fx.untilReady();
  const std::string name = fx.f->conveyor()->peek()->name();
  const int slot = idxAt(2, 5);
  fx.fire(fx.f->grid().slotCentreWindow(slot));
  CHECK(fx.f->shots() == 1);
  fx.step(30);
  CHECK(fx.f->shots() == 0);
  CHECK(fx.f->grid().slots[static_cast<std::size_t>(slot)].name == name);
  CHECK(fx.f->misses() == 0);
  fx.untilReady();
  fx.fire(fx.f->grid().slotCentreWindow(slot));  // now occupied: a 1000 px flight, 250 ms
  fx.step(24);
  CHECK(fx.f->shots() == 1);
  fx.step(1);
  CHECK(fx.f->misses() == 1);
  REQUIRE(fx.f->parked().size() == 1);
  CHECK(fx.f->parked()[0]->pos.x == doctest::Approx(10));
}

TEST_CASE("coffee: a slot filled while the shot is in the air refuses it on arrival") {
  CoffeeFixture fx;
  fx.start();
  fx.untilReady();
  const int slot = idxAt(1, 4);
  fx.fire(fx.f->grid().slotCentreWindow(slot));
  REQUIRE(fx.f->shots() == 1);
  CHECK(fx.f->dropInto(slot, "a"));  // another ingredient got there first
  fx.step(40);
  CHECK(fx.f->misses() == 1);
  CHECK(fx.f->grid().slots[static_cast<std::size_t>(slot)].name == "a");
}

TEST_CASE("coffee: a row of three clears, speeds the grid by 1.1 and pays no bonus; five pay 2 and raise a popup") {
  CoffeeFixture fx;
  fx.start();
  CHECK(fx.f->dropInto(idxAt(0, 6), "a"));
  CHECK(fx.f->dropInto(idxAt(1, 6), "a"));
  CHECK(fx.f->grid().filled() == 2);
  CHECK(fx.f->dropInto(idxAt(2, 6), "a"));
  CHECK(fx.f->grid().filled() == 0);
  CHECK(fx.f->collectionFlights() == 3);
  CHECK(fx.f->gridSpeed() == doctest::Approx(1.1));
  CHECK(fx.f->score() == 0);
  CHECK(fx.f->popups() == 0);
  fx.step(24);
  CHECK(fx.f->collectionFlights() == 3);
  fx.step(1);  // 250 ms later they reach the hopper
  CHECK(fx.f->collectionFlights() == 0);
  CHECK(fx.f->hopper().counts() == std::vector<int>{3, 0});

  for (int c : {0, 1, 3, 4}) CHECK(fx.f->dropInto(idxAt(c, 7), "b"));
  CHECK(fx.f->score() == 0);
  CHECK(fx.f->dropInto(idxAt(2, 7), "b"));
  CHECK(fx.f->score() == 2);
  CHECK(fx.f->bonus() == 2);
  CHECK(fx.f->popups() == 1);
  CHECK(fx.f->gridSpeed() == doctest::Approx(1.21));
  fx.step(199);
  CHECK(fx.f->popups() == 1);
  fx.step(1);  // 2000 ms in total
  CHECK(fx.f->popups() == 0);
}

TEST_CASE("coffee: the flood clear takes a bent group of five, not just its line of three") {
  CoffeeFixture fx;
  fx.start();
  CHECK(fx.f->dropInto(idxAt(2, 6), "a"));
  CHECK(fx.f->dropInto(idxAt(3, 6), "a"));
  CHECK(fx.f->dropInto(idxAt(4, 5), "a"));
  CHECK(fx.f->dropInto(idxAt(4, 4), "a"));
  CHECK(fx.f->grid().filled() == 4);
  CHECK(fx.f->dropInto(idxAt(4, 6), "a"));  // the line (2,3,4) of row 6, and the two above col 4
  CHECK(fx.f->grid().filled() == 0);
  CHECK(fx.f->collectionFlights() == 5);
  CHECK(fx.f->bonus() == 2);
}

TEST_CASE("coffee: a miss halves the grid speed, never below 1, and a shifted-off ingredient is not a miss") {
  CoffeeFixture fx;
  fx.start();
  for (int round = 0; round < 2; ++round) {
    CHECK(fx.f->dropInto(idxAt(0, 6), "a"));
    CHECK(fx.f->dropInto(idxAt(1, 6), "a"));
    CHECK(fx.f->dropInto(idxAt(2, 6), "a"));
  }
  CHECK(fx.f->gridSpeed() == doctest::Approx(1.21));
  fx.untilReady();
  fx.fire({0, 0});
  fx.step(25);
  CHECK(fx.f->misses() == 1);
  CHECK(fx.f->gridSpeed() == doctest::Approx(1.0));
  CHECK(fx.f->dropInto(idxAt(1, 8), "a"));  // the bottom row falls off at the next shift
  fx.f->forceShift();
  CHECK(fx.f->grid().slots[static_cast<std::size_t>(idxAt(1, 8))].name != "a");
  CHECK(fx.f->misses() == 1);
}

TEST_CASE("coffee: the target marker follows the cursor over the grid while playing only") {
  CoffeeFixture fx;
  REQUIRE(fx.f->marker() != nullptr);
  CHECK_FALSE(fx.f->marker()->visible);
  fx.f->onMouseMove(170, 40);  // not playing
  CHECK_FALSE(fx.f->marker()->visible);
  fx.start();
  fx.f->onMouseMove(170, 40);
  CHECK(fx.f->marker()->visible);
  CHECK(fx.f->marker()->pos.x == doctest::Approx(22.5f));
  CHECK(fx.f->marker()->pos.y == doctest::Approx(22.5f));
  fx.f->onMouseMove(600, 300);  // outside the grid
  CHECK_FALSE(fx.f->marker()->visible);
}

TEST_CASE("coffee: the first miss pauses when warnOnMiss is on, the 8th miss aborts and the next step is game over") {
  CoffeeFixture fx({"a", "b"}, true);
  fx.start();
  fx.untilReady();
  fx.fire({0, 0});
  fx.step(25);
  CHECK(fx.f->misses() == 1);
  CHECK(fx.f->paused());
  REQUIRE_FALSE(fx.fills.empty());
  CHECK(fx.fills.back().text == "coffee_first_missed");
  fx.f->resumeGame(fx.lua.L());
  for (int i = 1; i < 8; ++i) {
    CHECK(fx.f->state() == State::Playing);
    fx.untilReady();
    fx.fire({0, 0});
    fx.step(25);
  }
  CHECK(fx.f->misses() == 8);
  CHECK(fx.f->state() == State::Aborted);
  fx.step();
  CHECK(fx.f->state() == State::Over);
  CHECK(fx.fills.back().file == "ui/chocolate_gameover.lua");
  CHECK(fx.ctx.wm.windowAnimationCount() == 0);
  CHECK(fx.f->score() == 0);
}

TEST_CASE("coffee: focus change on the game-over screen still pauses, and OK re-enters Over with the fill back") {
  CoffeeFixture fx({"a", "b"}, false);
  fx.start();
  int guard = 0;
  while (fx.f->state() != State::Over && guard++ < 500) {
    if (fx.f->state() == State::Playing) {
      fx.untilReady();
      fx.fire({0, 0});
      fx.step(25);
    } else {
      fx.step();
    }
  }
  REQUIRE(fx.f->state() == State::Over);
  REQUIRE_FALSE(fx.f->paused());
  fx.f->focusChanged();
  CHECK(fx.f->paused());
  CHECK(fx.fills.back().text == "game_paused");
  fx.f->resumeGame(fx.lua.L());
  CHECK_FALSE(fx.f->paused());
  CHECK(fx.f->state() == State::Over);
  CHECK(fx.fills.back().file == "ui/chocolate_gameover.lua");
  CHECK(fx.ctx.wm.windowAnimationCount() == 0);
}

TEST_CASE("coffee: the grid is paused through the countdown (SetGameState COUNTDOWN pauses all five tasks)") {
  CoffeeFixture fx;
  fx.f->startGame(fx.lua.L());
  const float y0 = fx.f->grid().pos.y;
  fx.step(299);
  CHECK(fx.f->state() == State::Countdown);
  CHECK(fx.f->grid().previousT == 0);
  CHECK(fx.f->grid().pos.y == doctest::Approx(y0));
  CHECK(fx.f->grid().pos.y == doctest::Approx(15.0));
  CHECK(fx.f->grid().filled() == 0);
}

TEST_CASE("coffee: resume in PLAYING resets the grid speed and shifts a row (kept quirk); Try Again leaves no cases (the fix)") {
  CoffeeFixture fx;
  fx.start();
  for (int c : {0, 1, 2}) CHECK(fx.f->dropInto(idxAt(c, 6), "a"));
  CHECK(fx.f->gridSpeed() == doctest::Approx(1.1));
  fx.step(5);
  const double previous = fx.f->grid().previousT;
  CHECK(previous > 0);
  fx.f->pauseGame(fx.lua.L(), "");
  fx.f->resumeGame(fx.lua.L());
  CHECK(fx.f->gridSpeed() == doctest::Approx(1.0));
  const auto slotsBefore = [&] {
    std::vector<bool> v;
    for (const auto& s : fx.f->grid().slots) v.push_back(s.ing != nullptr);
    return v;
  };
  const auto filledBefore = slotsBefore();
  fx.step();
  CHECK(fx.f->grid().previousT < previous);  // the phase restarted below the stale one, which shifts a row
  CHECK(slotsBefore() != filledBefore);      // shiftRows ran: a new row 0 arrived

  for (int i = 0; i < 3; ++i) fx.f->hopper().add("a"), fx.f->hopper().add("b");
  fx.step(150);
  REQUIRE(fx.f->casesOnBelt() >= 1);
  fx.f->startGame(fx.lua.L());  // Try Again
  CHECK(fx.f->casesOnBelt() == 0);
  CHECK(fx.f->delivered() == 0);
  CHECK(fx.f->score() == 0);
  CHECK(fx.f->hopper().counts() == std::vector<int>{0, 0});
  fx.step(800);
  CHECK(fx.f->score() == 0);  // nothing from the aborted run scores in the new one
}

TEST_CASE("coffee: ENDING waits for the cases on the belt, then it is game over with their score") {
  CoffeeFixture fx;
  fx.start();
  fx.step(5800);  // 2 s of play left
  CHECK(fx.f->state() == State::Playing);
  fx.f->hopper().add("a");
  fx.f->hopper().add("b");
  fx.step(2);  // the case starts
  REQUIRE(fx.f->casesOnBelt() == 1);
  int steps = 0;
  while (fx.f->state() == State::Playing && steps < 400) fx.step(), ++steps;
  CHECK(fx.f->state() == State::Ending);
  CHECK(fx.f->casesOnBelt() == 1);
  fx.step(100);
  CHECK(fx.f->state() == State::Ending);
  steps = 0;
  while (fx.f->state() != State::Over && steps < 600) fx.step(), ++steps;
  CHECK(fx.f->state() == State::Over);
  CHECK(fx.f->score() == 1);
  CHECK(fx.f->delivered() == 1);
  CHECK(fx.fills.back().file == "ui/chocolate_gameover.lua");
}

TEST_CASE("coffee: autoAim answers only when a shot can be placed, inside the free band of the grid") {
  CoffeeFixture fx;
  CHECK_FALSE(fx.f->autoAim().has_value());
  fx.start();
  CHECK_FALSE(fx.f->autoAim().has_value());  // the conveyor is not ready
  fx.untilReady();
  auto p = fx.f->autoAim();
  REQUIRE(p.has_value());
  const int idx = fx.f->grid().slotIndex(*p);
  REQUIRE(idx >= 0);
  CHECK(idx / 6 >= 1);
  CHECK(idx / 6 <= 7);
  fx.fire(*p);
  CHECK_FALSE(fx.f->autoAim().has_value());  // a shot is in flight
}

TEST_CASE("coffee: destroying the factory mid-run leaves nothing dangling") {
  CoffeeFixture fx;
  fx.start();
  for (int c : {0, 1, 2, 3, 4}) fx.f->dropInto(idxAt(c, 6), "a");  // flights to the hopper and a popup
  fx.f->dropInto(idxAt(1, 2), "b");
  fx.f->hopper().add("a");
  fx.f->hopper().add("b");
  fx.step(10);
  fx.untilReady();
  fx.fire({0, 0});
  fx.step(26);  // one shot parked
  fx.untilReady();
  fx.fire(fx.f->grid().slotCentreWindow(idxAt(3, 3)));  // one in the air
  fx.f->pauseGame(fx.lua.L(), "");
  ui::Window* w = fx.f;
  fx.wm.destroyWindow(*w);
  fx.wm.clearDestroyed();
  CHECK(FactoryWindow::active() == nullptr);
  fx.wm.tickWindowAnimations(1000);
  CHECK(fx.wm.windowAnimationCount() == 0);
}

// ---- pistons, arms and fingers (docs/re/factories-questions.md 8) ----

TEST_CASE("the machine fingers swing about their anchors") {
  using P = ChocolateFactory::Pivot;
  const P pv{{100, 200}, {10, -20}};
  CHECK(ChocolateFactory::pivotedPos(pv, 0).x == doctest::Approx(110));
  CHECK(ChocolateFactory::pivotedPos(pv, 0).y == doctest::Approx(180));
  CHECK(ChocolateFactory::pivotedPos(pv, 180).x == doctest::Approx(90));
  CHECK(ChocolateFactory::pivotedPos(pv, 180).y == doctest::Approx(220));
  // A quarter turn clockwise on screen (y down) takes "right" to "down" and "up" to "right".
  CHECK(ChocolateFactory::pivotedPos(pv, 90).x == doctest::Approx(120));
  CHECK(ChocolateFactory::pivotedPos(pv, 90).y == doctest::Approx(210));
}

TEST_CASE("the machine turns the template, upper and lower fingers one revolution in 40 steps, opposite for the upper") {
  FactoryFixture fx(1, {"a", "b"}, false, 2);
  fx.start();
  const float y0 = fx.f->upperFinger()->pos.y;
  CHECK(fx.f->upperFinger()->layer() == -1);
  CHECK(fx.f->trayTemplate()->layer() == 0);
  ChocolateTray& tray = *fx.f->trays()[0];
  fx.f->forceHit(tray, fx.newIngredient("a"));
  fx.f->forceHit(tray, fx.newIngredient("b"));
  int steps = 0;
  while (tray.fill > 0 && steps < 5000) fx.step(), ++steps;
  INFO("steps " << steps << " fill " << tray.fill << " score " << fx.f->score());
  REQUIRE(fx.f->trayTemplate()->rotationDeg > 0);
  CHECK(fx.f->trayTemplate()->rotationDeg == doctest::Approx(9.9));  // machineTime 1 + 10 = 11 of 400 on the first step
  CHECK(fx.f->upperFinger()->rotationDeg == doctest::Approx(-9.9));
  CHECK(fx.f->lowerFinger()->rotationDeg == doctest::Approx(9.9));
  fx.step(39);
  CHECK(fx.f->trayTemplate()->rotationDeg == doctest::Approx(0));  // wrapped
  CHECK(fx.f->upperFinger()->pos.y == doctest::Approx(y0));
}

TEST_CASE("tray pistons telescope from the top to 50 px above the tray") {
  FactoryFixture fx(1, {"a", "b"}, false, 1);
  fx.start();
  const ChocolateTray& tray = *fx.f->trays()[0];  // the test path is one point (700, 10)
  REQUIRE(tray.pistons.size() == 4);
  for (std::size_t i = 0; i < 4; ++i) {
    CHECK(tray.pistons[i]->pos.x == doctest::Approx(700));
    CHECK(tray.pistons[i]->pos.y == doctest::Approx(static_cast<float>(4 - i) * (10 - 50) / 4 + 2));  // test images are 0 x 0: anchor y is -2
    CHECK(tray.pistons[i]->layer() == -100);
  }
}

TEST_CASE("conveyor pistons are made per belt item, follow it, and stay put once the belt is parked") {
  FactoryFixture fx(1, {"a", "b"}, false, 1);
  fx.start();
  fx.step(10);
  const IngredientQueue& q = *fx.f->conveyor();
  REQUIRE(q.pistonCount() >= 1);
  CHECK(q.pistonCount() == q.onBelt());
  const gfx::PointF p = q.peek()->pos;
  CHECK(q.pistonRoot(0).pos.x == doctest::Approx(p.x));
  CHECK(q.pistonRoot(0).pos.y == doctest::Approx(p.y + 78));  // anchor (7.5, -16) on a 15 x 124 image
  CHECK(q.pistonArm(0).pos.y == doctest::Approx((600 - p.y) / 2));
  fx.untilReady();
  const gfx::PointF parked = q.pistonRoot(0).pos;
  const std::size_t n = q.pistonCount();
  fx.step(5);
  CHECK(q.pistonRoot(0).pos.x == doctest::Approx(parked.x));
  CHECK(q.pistonCount() == n);
}

// ---------------------------------------------------------------- the spec (docs/design/choc2-minigames.md 2.1)

TEST_CASE("readSpec reads Chocolatier 3's keys into the spec, with the old defaults for the rest") {
  FactoryFixture fx(3, {"a", "b", "c"}, true, 4);
  ui::MapStyle st;
  auto rec = std::make_shared<ui::MapStyle>();
  rec->append("x").append("y");
  st.set("recipe", rec);
  st.set("conveyorpath", pts({{0, 500}, {100, 500}, {200, 500}, {300, 500}}));
  st.set("conveyorcount", 4.0);
  st.set("conveyortime", 900.0);
  st.set("gunspeed", 300.0);
  st.set("trayend", 0.9);
  st.set("traycount", 7.0);
  st.set("rainbow", 1.0);
  st.set("machine", std::string("image/m"));
  const game::ChocolateSpec sp = fx.f->readSpec(st);
  CHECK(sp.recipe == std::vector<std::string>{"x", "y"});
  CHECK(sp.colorCount == 1);
  CHECK(sp.rainbow);
  CHECK(sp.spacing == 1.0 / 4);
  CHECK(sp.conveyorMs == 900);
  CHECK(sp.flightMs == 300);
  CHECK(sp.ringMs == 500);
  CHECK(sp.productMs == 3000);
  CHECK(sp.lapMs == 30000);
  CHECK(sp.trayEnd == 0.9);
  CHECK(sp.trayCount == 7);
  CHECK(sp.speedup == 1.1);
  CHECK(sp.maxSpeed == 8);
  CHECK(sp.machine == "image/m");
  CHECK_FALSE(sp.recycler);
  CHECK(sp.common.countdownMs == 3000);
  CHECK(sp.common.gameMs == 60000);
  CHECK(sp.common.warnMs == 8000);
  CHECK(sp.common.park.empty());
  CHECK(sp.common.scoreChild == "score");
  CHECK(sp.trayPrefix == "image/tray");
  CHECK(sp.gunSheet == "image/gun.xml");
  CHECK(sp.ingredientArt.itemPrefix == "items/");
  CHECK(sp.pistons);
  CHECK(sp.fingers);
  CHECK(sp.machineSprite);
  CHECK(sp.box);
  CHECK(sp.timerDial);
  CHECK_FALSE(sp.templatePos.has_value());
  CHECK(sp.gunRotationOffsetDeg == 0);
  CHECK(sp.ringsPerMatch == 0);
  // The built factory kept the fixture's own spec.
  CHECK(fx.f->spec().trayCount == 4);
  CHECK(fx.f->spec().common.park.size() == 8);
  CHECK(fx.f->spec().common.warnOnMiss);
}
