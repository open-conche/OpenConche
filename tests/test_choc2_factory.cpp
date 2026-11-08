// Chocolatier 2's FactoryGame and FactoryStatus (docs/design/choc2-minigames.md 2.1 and 6): the key adapter, the sound map, the
// performance formula, the lifecycle and the callbacks on a real window, and the status bars.
#include <doctest/doctest.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "games/Registry.h"
#include "ScriptHarness.h"
#include "games/choc2/FactoryGame.h"
#include "games/choc2/FactoryStatus.h"
#include "games/choc2/Choc2FactoryNatives.h"
#include "ui/Style.h"
#include "ui/widgets/TextWidgets.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
using game::choc2::FactoryGame;
using game::choc2::FactoryStatus;
using State = game::FactoryWindow::State;

namespace {

std::shared_ptr<ui::MapStyle> points(std::initializer_list<std::pair<double, double>> p) {
  auto t = std::make_shared<ui::MapStyle>();
  for (auto [x, y] : p) {
    auto e = std::make_shared<ui::MapStyle>();
    e->append(x).append(y);
    t->append(e);
  }
  return t;
}

std::shared_ptr<ui::MapStyle> recipeMap(std::initializer_list<std::pair<const char*, double>> r) {
  auto t = std::make_shared<ui::MapStyle>();
  for (auto [name, n] : r) t->set(name, n);
  return t;
}

// The table ui/factory_minigame.lua builds for the starter product b_01 (item/itemdefs.lua), with the hard-coded values.
ui::MapStyle b01Style(bool warn = false, bool colorWarn = false) {
  ui::MapStyle st;
  st.set("warn", warn ? 1.0 : 0.0);
  st.set("colorWarn", colorWarn ? 1.0 : 0.0);
  auto product = std::make_shared<ui::MapStyle>();
  product->set("name", std::string("b_01"));
  product->set("recipe", recipeMap({{"sugar", 1}, {"cacao", 1}}));
  st.set("product", product);
  auto type = std::make_shared<ui::MapStyle>();
  type->set("name", std::string("square"));
  st.set("type", type);
  st.set("traypath", points({{-75, 300}, {166, 8}, {490, 361}, {658, 300}}));
  st.set("trayEnd", 0.95);
  st.set("ingredientpath", points({{0, 570}, {100, 570}, {200, 570}, {300, 570}}));
  st.set("productpath", points({{689, 300}, {689, 400}, {689, 500}, {689, 555}}));
  st.set("traycount", 5.0);
  st.set("colorcount", 0.0);
  st.set("traycycle", 30000.0);
  st.set("conveyortime", 1200.0);
  st.set("flytime", 500.0);
  st.set("producttime", 2000.0);
  st.set("speedup", 1.1);
  st.set("starttime", 60000.0);
  st.set("countdown", 2400.0);
  st.set("bonusflytime", 500.0);
  st.set("maxspeed", 8.0);
  st.set("ingredientrate", 250.0);
  return st;
}

// What MinigameEnd's cut-offs do with a performance (ui/factory_minigame.lua, MinigameEnd): handicap first.
const char* endKey(double performance, double handicap) {
  const double p = performance * handicap;
  if (p < 0) return "stopped";
  if (p < 11) return "low";
  if (p < 19) return "med";
  if (p < 55) return "high";
  return "awesome";
}

struct GameFixture : test::ScriptEnv {
  explicit GameFixture(bool warn = false, bool colorWarn = false, int colorCount = 0, bool threeIngredients = false)
      : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ledger") {
    script::registerChoc2FactoryNatives(lua.L(), ctx);
    REQUIRE(run(R"(
      gPause, gWarn, gColor, gEnd = 0, 0, 0, 0
      function MinigamePause() gPause = gPause + 1 end
      function MinigameWarn() gWarn = gWarn + 1 end
      function MinigameColor() gColor = gColor + 1 end
      function MinigameEnd(performance, fp) gEnd = gEnd + 1; gPerf = performance; gSecond = fp end
    )") == 0);
    modal = &wm.pushModal("m");
    modal->setRect({0, 0, 800, 600});
    parent = &modal->adoptFront(std::make_unique<ui::PlaceholderWindow>(wm, "Bitmap"));
    parent->setRect({0, 0, 800, 600});
    auto pauseButton = std::make_unique<ui::PlaceholderWindow>(wm, "Window");
    pauseButton->setName("pause");
    parent->adoptFront(std::move(pauseButton));
    ui::MapStyle st = b01Style(warn, colorWarn);
    st.set("colorcount", static_cast<double>(colorCount));
    if (threeIngredients) {
      auto product = std::make_shared<ui::MapStyle>();
      product->set("name", std::string("b_01"));
      product->set("recipe", recipeMap({{"sugar", 1}, {"cacao", 1}, {"milk", 1}}));
      st.set("product", product);
    }
    auto w = std::make_unique<FactoryGame>(wm, ctx);
    g = static_cast<FactoryGame*>(&parent->adoptFront(std::move(w)));
    g->init(st);
    g->setRect({0, 0, 800, 600});
    for (const char* n : {"counter", "timer", "countdown"}) {
      auto t = std::make_unique<ui::TextWindow>(wm);
      t->setName(n);
      g->adoptFront(std::move(t));
    }
    g->postChildrenInit(st);
  }
  std::string text(const char* name) { return g->find(name)->label(); }
  void step(int n = 1) {
    for (int i = 0; i < n; ++i) g->onAnimate();
  }
  void untilReady() {
    for (int i = 0; i < 400 && !g->conveyor()->ready(); ++i) step();
    REQUIRE(g->conveyor()->ready());
  }
  // One shot along the bottom edge of the screen: it passes under every tray and is parked as a miss.
  void miss() {
    untilReady();
    g->onMouseDown(799, 599);
    step(60);
  }
  game::Ingredient& ingredient(const std::string& name, int color) {
    auto ing = std::make_unique<game::Ingredient>(name, std::make_shared<const game::IngredientArt>(g->spec().ingredientArt));
    if (color) ing->setColor(color, tex);
    return static_cast<game::Ingredient&>(g->spriteTree().addChild(std::move(ing)));
  }
  void startPlaying() {
    REQUIRE(run("ResumeMinigame()") == 0);
    step(239);
    REQUIRE(g->state() == State::Countdown);
    step(1);
    REQUIRE(g->state() == State::Playing);
  }
  // A colour match on tray 0: the two ingredients of b_01's recipe in colour 1.
  void colorMatch(std::size_t tray = 0) {
    g->forceHit(*g->trays()[tray], ingredient("sugar", 1));
    g->forceHit(*g->trays()[tray], ingredient("cacao", 1));
  }
  game::Textures tex{wm, nullptr};
  ui::Window* modal = nullptr;
  ui::Window* parent = nullptr;
  FactoryGame* g = nullptr;
};

}  // namespace

// ---------------------------------------------------------------- the key adapter

TEST_CASE("factorySpec: b_01's table gives the Chocolatier 2 spec") {
  const ui::MapStyle st = b01Style(true, true);
  const game::ChocolateSpec sp = game::choc2::factorySpec(st);
  CHECK(sp.recipe == std::vector<std::string>{"cacao", "sugar"});  // sorted names
  CHECK(sp.trayCount == 5);
  CHECK(sp.trayEnd == 0.95);
  CHECK(sp.spacing == doctest::Approx(250.0 / 1200.0));
  CHECK(sp.conveyorMs == 1200);
  CHECK(sp.flightMs == 500);
  CHECK(sp.ringMs == 500);
  CHECK(sp.lapMs == 30000);
  CHECK(sp.productMs == 2000);
  CHECK(sp.common.countdownMs == 2400);
  CHECK(sp.common.gameMs == 60000);
  CHECK(sp.common.warnMs == 8000);  // Chocolatier 3's, the bell at 8 s
  CHECK(sp.common.warnOnMiss);
  CHECK(sp.colorCount == 1);  // colorcount 0 is no colours
  CHECK_FALSE(sp.rainbow);
  CHECK(sp.speedup == 1.1);
  CHECK(sp.maxSpeed == 8);
  CHECK(sp.ringsPerMatch == 0);  // a colour match always makes recipe-size rings
  CHECK(sp.ringCredit == 0);     // bonuscount is nil everywhere
  REQUIRE(sp.common.park.size() == 8);
  CHECK(sp.common.park[0].x == 631);  // 565 + 66
  CHECK(sp.common.park[0].y == 136);  // missy; the row step is missdx (39), the binary never reads missdy
  CHECK(sp.common.park[3].x == 748);
  CHECK(sp.common.park[4].y == 175);
  CHECK(sp.common.park[7].x == 748);
  CHECK(sp.common.missMarker == "image/missed_ingredient");
  CHECK(sp.common.scoreChild == "counter");
  CHECK(sp.common.timeChild == "timer");
  CHECK(sp.common.countdownChild == "countdown");
  CHECK(sp.common.clockTexts);
  CHECK_FALSE(sp.common.pauseAudio);
  CHECK(sp.ingredientArt.itemPrefix == "item/");
  CHECK(sp.ingredientArt.ring == "image/trayring");
  CHECK(sp.gunSheet == "image/gun_shoot.xml");
  CHECK(sp.gunRotationOffsetDeg == 180);
  CHECK_FALSE(sp.pistons);
  CHECK(sp.machineSprite);
  CHECK(sp.machine == "image/machine_square");
  CHECK(sp.machinePos.x == 690);
  CHECK(sp.machinePos.y == 299);
  CHECK(sp.fingerImage == "image/factory_finger");
  CHECK(sp.box);
  CHECK(sp.boxOffset.y == -23);
  CHECK_FALSE(sp.timerDial);
  CHECK(sp.hitRadius == doctest::Approx(76.69).epsilon(0.001));  // sqrt(TEST_RADIUS = 5881)
  CHECK(sp.common.choc2Misses);
  CHECK(sp.common.missScale == 1.0f);
  CHECK(sp.bagRounds == 5);
  REQUIRE(sp.templatePos.has_value());
  CHECK(sp.templatePos->x == 0);  // the machine's centre
  CHECK(sp.templatePos->y == 0);
  REQUIRE(sp.look.size() == 1);
  CHECK(sp.look[0].image == "item/b_01_big");
}

TEST_CASE("factorySpec: a coloured type, bonuscount, and the Chocolatier 3 defaults for missing keys") {
  ui::MapStyle st = b01Style();
  st.set("traycount", 7.0);
  st.set("colorcount", 2.0);
  st.set("bonuscount", 3.0);
  const game::ChocolateSpec sq = game::choc2::factorySpec(st);
  CHECK(sq.trayCount == 7);
  CHECK(sq.colorCount == 2);
  CHECK(sq.ringsPerMatch == 0);
  CHECK(sq.ringCredit == 3);
  const game::ChocolateSpec none = game::choc2::factorySpec(ui::MapStyle{});
  CHECK(none.trayCount == 7);  // the binary's defaults: 7 trays, 2 colours, countdown 2400, ingredientrate 350
  CHECK(none.trayEnd == 0);
  CHECK(none.lapMs == 30000);
  CHECK(none.conveyorMs == 1200);
  CHECK(none.common.countdownMs == 2400);
  CHECK(none.colorCount == 2);
  CHECK(none.recipe.empty());
  CHECK_FALSE(none.machineSprite);
  CHECK(none.look.empty());
}

TEST_CASE("factorySpec: a recipe's counts repeat each name, names sorted") {
  ui::MapStyle st = b01Style();
  auto product = std::make_shared<ui::MapStyle>();
  product->set("name", std::string("x"));
  product->set("recipe", recipeMap({{"sugar", 2}, {"almond", 1}, {"cacao", 1}}));
  st.set("product", product);
  CHECK(game::choc2::factorySpec(st).recipe == std::vector<std::string>{"almond", "cacao", "sugar", "sugar"});
}

// ---------------------------------------------------------------- the sound map

TEST_CASE("factorySound maps Chocolatier 3's events to Chocolatier 2's files") {
  using game::choc2::factorySound;
  CHECK(factorySound("clock_tick") == "countdown_tick");
  CHECK(factorySound("start_bell") == "minigame_starting_bell");
  CHECK(factorySound("whistle") == "warning_bell");
  CHECK(factorySound("launch") == "minigame_load_launch");
  CHECK(factorySound("snatch") == "factory_snatch");
  CHECK(factorySound("ingredient_complete") == "template_complete");
  CHECK(factorySound("ingredient_complete_colormatch") == "colormatch_complete");
  CHECK(factorySound("ingredient_complete_full_bonus") == "powerup");
  CHECK(factorySound("chocolate_counted") == "template_success");
  CHECK(factorySound("ingredient_failure_factory shutdown") == "factory_shut_down");
  CHECK(factorySound("ingredient_success_1") == "ingredient_success_01");
  CHECK(factorySound("ingredient_success_5") == "ingredient_success_05");
  CHECK(factorySound("ingredient_color_match_1") == "color_match_1");
  CHECK(factorySound("ingredient_color_match_6") == "color_match_6");
  CHECK(factorySound("ingredient_failure_1") == "ingredient_failure_01");
  CHECK(factorySound("ingredient_failure_7") == "ingredient_failure_07");
  CHECK(factorySound("ingredient_failure_8") == "ingredient_failure_08");
  for (const char* silent : {"chocolate_making_game", "chocolate_making_game_intro", "chocolate_making_game_outro", "game_over",
                             "recycler", "ingredient_success_x", "ingredient_success_", "nonsense", ""})
    CHECK(factorySound(silent).empty());
}

// ---------------------------------------------------------------- performance

TEST_CASE("factoryPerformance: trunc(100 * production / ideal), ideal = floor(floor(start / rate) / size) (+ floor(start / rate) with colours), -1 after the abort") {
  using game::choc2::factoryPerformance;
  CHECK(factoryPerformance(13, false, 60000, 250, 2, false) == 10);  // 10.83 truncated
  CHECK(factoryPerformance(120, false, 60000, 250, 2, false) == 100);
  CHECK(factoryPerformance(13, true, 60000, 250, 2, false) == -1);
  CHECK(factoryPerformance(0, true, 60000, 250, 2, true) == -1);
  CHECK(factoryPerformance(12, false, 60000, 250, 3, false) == 15);  // ideal 80
  CHECK(factoryPerformance(12, false, 60000, 250, 3, true) == 3);    // ideal 80 + 240 = 320: 3.75 truncated
  CHECK(factoryPerformance(5, false, 100, 250, 2, true) == 0);       // ideal 0
  CHECK(factoryPerformance(5, false, 60000, 0, 2, false) == 0);
  CHECK(factoryPerformance(5, false, 60000, 250, 0, false) == 0);
}

TEST_CASE("the cut-offs with b_01's handicap 0.9 (the performance is an int): up to 15 cases read low, 16 to 26 med, 27 high") {
  using game::choc2::factoryPerformance;
  auto key = [](unsigned cases) { return endKey(factoryPerformance(cases, false, 60000, 250, 2, false), 0.9); };
  CHECK(std::string(key(0)) == "low");
  CHECK(std::string(key(13)) == "low");
  CHECK(std::string(key(15)) == "low");  // 12 * 0.9 = 10.8
  CHECK(std::string(key(16)) == "med");  // 13 * 0.9 = 11.7
  CHECK(std::string(key(26)) == "med");  // 21 * 0.9 = 18.9
  CHECK(std::string(key(27)) == "high");  // 22 * 0.9 = 19.8
  CHECK(std::string(key(74)) == "high");  // 61 * 0.9 = 54.9
  CHECK(std::string(key(75)) == "awesome");  // 62 * 0.9 = 55.8
  CHECK(std::string(endKey(-1, 0.9)) == "stopped");
  CHECK(std::string(endKey(10, 1.0)) == "low");
  CHECK(std::string(endKey(11, 1.0)) == "med");
  CHECK(std::string(endKey(19, 1.0)) == "high");
  CHECK(std::string(endKey(55, 1.0)) == "awesome");
}

// ---------------------------------------------------------------- the window

TEST_CASE("a FactoryGame is built idle, with Chocolatier 2's child texts") {
  GameFixture fx;
  CHECK(fx.g->state() == State::Countdown);
  CHECK(fx.g->paused());
  CHECK_FALSE(fx.g->started());
  CHECK(fx.text("counter") == "0");
  CHECK(fx.text("timer") == "1:00");
  CHECK(fx.text("countdown") == "");
  CHECK(fx.g->trays().size() == 5);
  CHECK(fx.g->hasRecycler() == false);
  CHECK(fx.g->trayTemplate() != nullptr);
  CHECK(fx.g->upperFinger() != nullptr);  // image/factory_finger, twice
  CHECK(fx.g->gunPos().x == 300);
  CHECK(fx.g->gunPos().y == 570);
  CHECK(fx.g->gunSprite()->rotationDeg == 180);  // the nozzle points up at rest
  CHECK(fx.getNumber("gPause") == 0);            // the construction's own pause called nothing back
  CHECK(fx.g->detail().find("started=0") != std::string::npos);
  CHECK(std::string(fx.g->typeName()) == "FactoryGame");
}

TEST_CASE("ResumeMinigame from idle shows no digit, then 2 and 1 (240 steps) and starts play; Pause and Resume carry on") {
  GameFixture fx;
  REQUIRE(fx.run("ResumeMinigame()") == 0);
  CHECK(fx.g->started());
  CHECK_FALSE(fx.g->paused());
  CHECK(fx.text("countdown") == "");  // 2400 ms: nothing for the first 400, then 2, then 1
  CHECK(fx.getNumber("gPause") == 0);  // the start's reset called nothing
  fx.step(39);
  CHECK(fx.text("countdown") == "");
  fx.step(1);
  CHECK(fx.text("countdown") == "2");
  fx.step(199);
  CHECK(fx.g->state() == State::Countdown);
  CHECK(fx.text("countdown") == "1");
  fx.step(1);
  CHECK(fx.g->state() == State::Playing);
  CHECK(fx.text("countdown") == "");
  fx.step(150);
  CHECK(fx.text("timer") == "0:58");  // 58 500 ms: m:ss of the whole seconds
  const int clock = fx.g->clockMs();
  REQUIRE(fx.run("PauseMinigame()") == 0);
  CHECK(fx.getNumber("gPause") == 1);
  CHECK(fx.g->paused());
  CHECK(fx.ctx.wm.windowAnimationCount() == 0);
  REQUIRE(fx.run("PauseMinigame()") == 0);  // already paused: no second call
  CHECK(fx.getNumber("gPause") == 1);
  REQUIRE(fx.run("ResumeMinigame()") == 0);
  CHECK_FALSE(fx.g->paused());
  fx.step(3);
  CHECK(fx.g->clockMs() == clock - 30);
  CHECK(fx.getNumber("gPause") == 1);
}

TEST_CASE("PauseMinigame while idle or over calls nothing; Escape pauses a started game") {
  GameFixture fx;
  REQUIRE(fx.run("PauseMinigame()") == 0);
  CHECK(fx.getNumber("gPause") == 0);
  fx.startPlaying();
  ui::KeyEvent esc;
  esc.name = "Escape";
  fx.g->onKeyDown(esc);
  CHECK(fx.getNumber("gPause") == 1);
  CHECK(fx.g->paused());
  REQUIRE(fx.run("ResumeMinigame()") == 0);
  fx.step(6000);
  fx.step(80);  // the end decays to OVER
  REQUIRE(fx.g->state() == State::Over);
  CHECK(fx.getNumber("gEnd") == 1);
  REQUIRE(fx.run("PauseMinigame()") == 0);
  CHECK(fx.getNumber("gPause") == 1);
  REQUIRE(fx.run("ResumeMinigame()") == 0);  // over: nothing
  CHECK(fx.g->state() == State::Over);
}

TEST_CASE("the first miss calls MinigameWarn once when warn is set, and not otherwise") {
  GameFixture warned(true);
  warned.startPlaying();
  warned.miss();
  CHECK(warned.g->misses() == 1);
  CHECK(warned.getNumber("gWarn") == 1);
  CHECK(warned.g->paused());
  CHECK(warned.getNumber("gPause") == 0);  // the warning is not MinigamePause
  REQUIRE(warned.run("ResumeMinigame()") == 0);
  warned.miss();
  CHECK(warned.g->misses() == 2);
  CHECK(warned.getNumber("gWarn") == 1);

  GameFixture plain(false);
  plain.startPlaying();
  plain.miss();
  CHECK(plain.g->misses() == 1);
  CHECK(plain.getNumber("gWarn") == 0);
  CHECK_FALSE(plain.g->paused());
}

TEST_CASE("the colour hint: MinigameColor once, when a tray holds two matching ingredients and is not full; production counts rings and products") {
  GameFixture fx(false, true, 2, true);
  fx.startPlaying();
  fx.g->forceHit(*fx.g->trays()[0], fx.ingredient("sugar", 1));
  CHECK(fx.getNumber("gColor") == 0);
  fx.g->forceHit(*fx.g->trays()[0], fx.ingredient("cacao", 1));
  CHECK(fx.getNumber("gColor") == 1);
  CHECK(fx.g->paused());
  REQUIRE(fx.run("ResumeMinigame()") == 0);
  fx.g->forceHit(*fx.g->trays()[0], fx.ingredient("milk", 1));  // the colour match: three rings and a product
  CHECK(fx.getNumber("gColor") == 1);
  CHECK_FALSE(fx.g->paused());
  CHECK(fx.g->ringsFlying() == 3);
  CHECK(fx.g->productsOnBelt() == 1);
  fx.g->forceHit(*fx.g->trays()[1], fx.ingredient("sugar", 2));
  fx.g->forceHit(*fx.g->trays()[1], fx.ingredient("cacao", 2));  // a second pair: the hint was shown
  CHECK(fx.getNumber("gColor") == 1);
  CHECK_FALSE(fx.g->paused());
  fx.step(300);  // rings (500 ms) and the product (2000 ms) arrive
  REQUIRE(fx.run("gProduction = GetProduction()") == 0);
  CHECK(fx.getNumber("gProduction") == 4);  // 3 rings + 1 product
  CHECK(fx.g->production() == 4);
}

TEST_CASE("a two-ingredient colour match completes the tray and gives no hint") {
  GameFixture fx(false, true, 2);
  fx.startPlaying();
  fx.colorMatch(0);
  CHECK(fx.getNumber("gColor") == 0);
  CHECK(fx.g->ringsFlying() == 2);
  CHECK(fx.g->productsOnBelt() == 1);
}

TEST_CASE("a colour match sets the tray's Nx popup timer to 1500 ms in its colour; it expires, and Reset clears it") {
  GameFixture fx(false, false, 2);
  fx.startPlaying();
  fx.colorMatch(0);
  const auto& tray = *fx.g->trays()[0];
  CHECK(tray.color == 0);  // cleared, the colour was captured first
  CHECK(tray.textColor == 1);
  CHECK(tray.textMs == doctest::Approx(1500));
  CHECK(fx.g->trays()[1]->textMs == 0);
  REQUIRE(tray.popup);
  CHECK(tray.popup->visible);
  fx.step(100);  // 500 ms left: still opaque; the fade is over the last 500 ms
  CHECK(tray.popup->alpha == doctest::Approx(1.0f));
  fx.step(25);
  CHECK(tray.popup->alpha == doctest::Approx(0.5f));
  fx.step(24);
  CHECK(tray.textMs == doctest::Approx(10));
  fx.step(1);
  CHECK(tray.textMs == 0);
  CHECK_FALSE(tray.popup->visible);

  fx.colorMatch(0);
  CHECK(tray.textMs == doctest::Approx(1500));
  REQUIRE(fx.run("PauseMinigame()") == 0);
  CHECK(fx.g->paused());  // the stepping stops with the window animation, so the timer (a step count) freezes
  CHECK(tray.textMs == doctest::Approx(1500));
  REQUIRE(fx.run("ResetMinigame()") == 0);
  CHECK(tray.textMs == 0);
  CHECK_FALSE(tray.popup->visible);
}

TEST_CASE("without colorWarn a colour match calls nothing") {
  GameFixture fx(false, false, 2);
  fx.startPlaying();
  fx.colorMatch(0);
  CHECK(fx.getNumber("gColor") == 0);
  CHECK_FALSE(fx.g->paused());
}

TEST_CASE("a normal end calls MinigameEnd(100 * P / ideal, nil) with P == GetProduction()") {
  GameFixture fx(false, false, 2);
  fx.startPlaying();
  fx.colorMatch(0);
  fx.step(5990);
  fx.step(100);
  REQUIRE(fx.g->state() == State::Over);
  CHECK(fx.getNumber("gEnd") == 1);
  // The binary truncates to an int, and with colours on the ideal is 120 + 240: 100 * 3 / 360 is 0.
  CHECK(fx.getNumber("gPerf") == 0);
  CHECK(fx.getString("gSecond") == "<nil>");
  REQUIRE(fx.run("gProduction = GetProduction()") == 0);
  CHECK(fx.getNumber("gProduction") == 3);
  CHECK(fx.g->performance() == 0);
}

TEST_CASE("eight misses abort the game and call MinigameEnd(-1, nil), keeping the count") {
  GameFixture fx(false, false, 2);
  fx.startPlaying();
  fx.colorMatch(0);
  fx.step(300);
  CHECK(fx.g->production() == 3);
  for (int i = 0; i < 8; ++i) fx.miss();
  CHECK(fx.g->misses() == 8);
  // With no shot or product out the abort ends at once (TFactoryGame::OnTaskAnimate).
  CHECK(fx.g->state() == State::Over);
  fx.step(200);
  REQUIRE(fx.g->state() == State::Over);
  CHECK(fx.getNumber("gEnd") == 1);
  CHECK(fx.getNumber("gPerf") == -1);
  CHECK(fx.getString("gSecond") == "<nil>");
  CHECK(fx.g->production() == 3);  // kept on an abort
}

TEST_CASE("ResetMinigame calls nothing, zeroes the production and the next Resume starts a countdown") {
  GameFixture fx(false, false, 2);
  fx.startPlaying();
  fx.colorMatch(0);
  fx.step(300);
  REQUIRE(fx.g->production() == 3);
  fx.step(5800);
  REQUIRE(fx.g->state() == State::Over);
  const double ends = fx.getNumber("gEnd"), pauses = fx.getNumber("gPause");
  REQUIRE(fx.run("ResetMinigame()") == 0);
  CHECK(fx.getNumber("gEnd") == ends);
  CHECK(fx.getNumber("gPause") == pauses);
  CHECK_FALSE(fx.g->started());
  CHECK(fx.g->paused());
  CHECK(fx.g->production() == 0);
  CHECK(fx.text("counter") == "0");
  CHECK(fx.text("timer") == "1:00");
  REQUIRE(fx.run("ResumeMinigame()") == 0);
  CHECK(fx.g->started());
  CHECK(fx.text("countdown") == "");  // never "3": the digit is blank until the ceiling of the remaining seconds drops
  fx.step(100);
  CHECK(fx.text("countdown") == "2");  // this fixture's countdown is 240 ms; the real 2400 ms one is blank for its first 400
  fx.step(100);
  CHECK(fx.text("countdown") == "1");
  fx.step(240);
  CHECK(fx.g->state() == State::Playing);
  CHECK(fx.getNumber("gPause") == pauses);
}

TEST_CASE("the natives without a FactoryGame do nothing and GetProduction gives 0") {
  test::ScriptEnv env(std::string(CHOCO_TEST_ASSETS) + "/ledger");
  script::registerChoc2FactoryNatives(env.lua.L(), env.ctx);
  CHECK(env.run("ResumeMinigame(); PauseMinigame(); ResetMinigame(); gP = GetProduction()") == 0);
  CHECK(env.getNumber("gP") == 0);
  CHECK(env.log.contains("[unimplemented] ResumeMinigame"));
}

TEST_CASE("a missing callback is logged once, not fatal") {
  GameFixture fx;
  REQUIRE(fx.run("MinigamePause = nil") == 0);
  fx.startPlaying();
  REQUIRE(fx.run("PauseMinigame()") == 0);
  CHECK(fx.g->paused());
  CHECK(fx.log.contains("the Lua global MinigamePause is not defined"));
}

TEST_CASE("a callback that errors is logged as [lua-error] and the game goes on") {
  GameFixture fx;
  REQUIRE(fx.run("function MinigamePause() error('boom') end") == 0);
  fx.startPlaying();
  REQUIRE(fx.run("PauseMinigame()") == 0);
  CHECK(fx.g->paused());
  CHECK(fx.log.contains("[lua-error]"));
}

TEST_CASE("a Chocolatier 3 factory window never calls the Chocolatier 2 callbacks") {
  // The hooks default to nothing: notifyPaused is only overridden by FactoryGame.
  GameFixture fx;
  fx.startPlaying();
  CHECK(fx.getNumber("gPause") + fx.getNumber("gWarn") + fx.getNumber("gColor") + fx.getNumber("gEnd") == 0);
}

// ---------------------------------------------------------------- FactoryStatus

namespace {

std::shared_ptr<ui::MapStyle> tuple(std::initializer_list<double> nums) {
  auto e = std::make_shared<ui::MapStyle>();
  for (double n : nums) e->append(n);
  return e;
}

}  // namespace

TEST_CASE("FactoryStatus.band: loads, full, medium and trouble") {
  CHECK(FactoryStatus::band(3) == 0);
  CHECK(FactoryStatus::band(1) == 0);
  CHECK(FactoryStatus::band(0.9999) == 1);
  CHECK(FactoryStatus::band(0.76) == 1);
  CHECK(FactoryStatus::band(0.75) == 2);
  CHECK(FactoryStatus::band(0.51) == 2);
  CHECK(FactoryStatus::band(0.5) == 3);  // the binary's tests are strict: exactly 0.5 is trouble
  CHECK(FactoryStatus::band(0.3) == 3);
  CHECK(FactoryStatus::band(0) == 3);
  CHECK(FactoryStatus::periodMs(0) == 0);
  CHECK(FactoryStatus::periodMs(20) == 1000);
  CHECK(FactoryStatus::periodMs(21) == 667);
  CHECK(FactoryStatus::periodMs(41) == 500);
  CHECK(FactoryStatus::periodMs(61) == 400);
}

TEST_CASE("FactoryStatus.fillHeight clamps the fill to the bar") {
  const gfx::RectF r{0, 0, 10, 40};
  CHECK(FactoryStatus::fillHeight(r, 0.5) == 20);
  CHECK(FactoryStatus::fillHeight(r, 2) == 40);
  CHECK(FactoryStatus::fillHeight(r, -1) == 0);
}

TEST_CASE("FactoryStatus parses five-number bars and six-value products and the four colours") {
  test::ScriptEnv env(std::string(CHOCO_TEST_ASSETS) + "/ledger");
  ui::MapStyle st;
  auto bars = std::make_shared<ui::MapStyle>();
  bars->append(tuple({4, 70, 79, 125, 0.3}));
  bars->append(tuple({87, 70, 162, 125, 1.7}));
  st.set("bars", bars);
  auto products = std::make_shared<ui::MapStyle>();
  auto p = std::make_shared<ui::MapStyle>();
  p->append(41.5).append(120.0).append(std::string("b_01")).append(24.0).append(std::string("bar")).append(2.0);
  products->append(p);
  st.set("products", products);
  st.set("colorTrouble", std::vector<double>{1, 0, 0, 1});
  FactoryStatus w(env.wm, env.ctx);
  w.init(st);
  REQUIRE(w.bars().size() == 2);
  CHECK(w.bars()[0].rect.x == 4);
  CHECK(w.bars()[0].rect.y == 70);
  CHECK(w.bars()[0].rect.w == 75);   // right - left
  CHECK(w.bars()[0].rect.h == 55);   // bottom - top
  CHECK(w.bars()[0].fill == doctest::Approx(0.3));
  CHECK(w.bars()[1].fill == doctest::Approx(1.7));
  REQUIRE(w.products().size() == 1);
  CHECK(w.products()[0].at.x == 41.5f);
  CHECK(w.products()[0].at.y == 120);
  CHECK(w.products()[0].name == "b_01");
  CHECK(w.products()[0].rate == 24);
  CHECK(w.products()[0].type == "bar");
  CHECK(w.products()[0].recipeSize == 2);
  CHECK(w.color(3).r == 1);
  CHECK(w.color(3).g == 0);
  CHECK(std::string(w.typeName()) == "FactoryStatus");
  CHECK(w.detail().find("bars=2 products=1") != std::string::npos);
}

TEST_CASE("StartProductionAnim and StopProductionAnim set the FactoryStatus flag; the panel clock ticks 50 ms regardless") {
  test::ScriptEnv env(std::string(CHOCO_TEST_ASSETS) + "/ledger");
  script::ScriptHost host(env.ctx);
  env.ctx.module = games::findModule("choc2");
  env.ctx.module->attach(env.ctx);
  REQUIRE(host.boot());
  FactoryStatus::setAnimating(false);
  REQUIRE(env.run("StartProductionAnim()") == 0);
  CHECK(FactoryStatus::animating());
  FactoryStatus w(env.wm, env.ctx);
  CHECK(w.elapsedMs() == 0);
  w.onAnimate();
  w.onAnimate();
  CHECK(w.elapsedMs() == 100);
  REQUIRE(env.run("StopProductionAnim()") == 0);
  CHECK_FALSE(FactoryStatus::animating());
  CHECK_FALSE(env.log.contains("behaviour unknown"));
}
