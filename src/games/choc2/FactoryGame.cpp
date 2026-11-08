#include "games/choc2/FactoryGame.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

#include "audio/Audio.h"
#include "script/LuaApi.h"
#include "script/ScriptHost.h"
#include "games/choc2/Choc2AudioNatives.h"
#include "ui/DrawContext.h"
#include "ui/WindowManager.h"

namespace choco::game::choc2 {

namespace {

// FACT (docs/re/choc2-factory-binary.md 4): TFactoryGame::Init's miss cells are (missx + (n % 4) * missdx, missy + (n / 4) * missdx)
// with the defaults 631, 136, 39 (the row step is missdx too: missdy is never read), and `image/machine_stack.png` is drawn at
// x = 565 by ui/factory_minigame.lua, so relative to it the columns are 66, 105, 144, 183 and the rows 136 and 175.
constexpr float kStackX = 565;
constexpr float kCellX[4] = {66, 105, 144, 183};
constexpr float kCellY[2] = {136, 175};

// FACT: the gun sheet's nozzle points down at rest (the aim is acos(dir.y), negated for dir.x > 0, so it is pi for the rest
// direction (0, -1); the sprite turns clockwise).
constexpr float kGunOffsetDeg = 180;

// FACT (TickFlying, TEST_RADIUS = 5881): a shot hits a tray whose centre is within sqrt(5881) of its position at that step.
constexpr float kHitRadius = 76.69f;

bool parseNumber(std::string_view s, int& out) {
  if (s.empty() || s.size() > 3) return false;
  out = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    out = out * 10 + (c - '0');
  }
  return true;
}

std::string twoDigits(int n) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%02d", n);
  return buf;
}

}  // namespace

const std::array<gfx::PointF, 8>& factoryMissPoints() {
  static const std::array<gfx::PointF, 8> pts = [] {
    std::array<gfx::PointF, 8> a{};
    for (int r = 0; r < 2; ++r)
      for (int c = 0; c < 4; ++c) a[static_cast<std::size_t>(r * 4 + c)] = {kStackX + kCellX[c], kCellY[r]};
    return a;
  }();
  return pts;
}

ChocolateSpec factorySpec(const ui::Style& style) {
  ChocolateSpec sp;
  FactoryCommon& c = sp.common;
  c.warnOnMiss = style.booleanOr("warn", false);
  c.countdownMs = static_cast<int>(style.numberOr("countdown", 2400));
  c.gameMs = static_cast<int>(style.numberOr("starttime", 60000));
  c.park.assign(factoryMissPoints().begin(), factoryMissPoints().end());
  c.missMarker = "image/missed_ingredient";
  c.scoreChild = "counter";
  c.gunCursor = "image/gun_aimer";     // Chocolatier 2's art tree keeps the cursors under image/ (see FactoryCommon)
  c.plainCursor = "image/cursor";
  c.timeChild = "timer";
  c.countdownChild = "countdown";
  c.clockTexts = true;
  c.pauseAudio = false;  // the script's PauseMusic and ResumeMusic do it
  c.choc2Misses = true;
  c.missScale = 1.0f;

  // The product: its name (the belt picture) and recipe, a map ingredient -> count. pairs() order is undefined, so the
  // names are sorted and each is repeated `count` times.
  std::string productName, typeName;
  if (auto type = style.table("type")) typeName = type->stringOr("name");
  {
    auto product = style.table("product");
    if (product) {
      productName = product->stringOr("name");
      auto recipe = product->table("recipe");
      if (recipe) {
        std::vector<std::pair<std::string, int>> entries;
        recipe->forEachNumber([&](const std::string& name, double count) {
          entries.emplace_back(name, static_cast<int>(std::clamp(std::round(count), 0.0, 16.0)));
        });
        std::sort(entries.begin(), entries.end());
        for (const auto& [name, count] : entries)
          for (int i = 0; i < count; ++i) sp.recipe.push_back(name);
      }
    }
  }
  // FACT: `colorcount` 0 is no colours, anything else is that many colours (default 2); the engine's colours start at 1 and a count
  // of 1 means none, which no Chocolatier 2 product uses (they have 0, 2 or 3).
  sp.colorCount = static_cast<int>(std::clamp(style.numberOr("colorcount", 2), 1.0, 255.0));
  sp.rainbow = false;

  sp.conveyorPath = readStyleCurve(style, "ingredientpath");
  sp.conveyorMs = style.numberOr("conveyortime", 1200);
  const double rate = style.numberOr("ingredientrate", 350);
  sp.spacing = sp.conveyorMs > 0 ? rate / sp.conveyorMs : sp.spacing;  // INFERENCE: one ingredient per `ingredientrate` ms
  sp.productPath = readStyleCurve(style, "productpath");
  sp.productMs = style.numberOr("producttime", 2000);
  sp.trayPath = readStyleCurve(style, "traypath");
  sp.lapMs = style.numberOr("traycycle", 30000);
  sp.trayEnd = style.numberOr("trayEnd", 0);
  sp.trayCount = static_cast<int>(std::clamp(style.numberOr("traycount", 7), 0.0, 64.0));
  sp.speedup = style.numberOr("speedup", 1.1);
  sp.maxSpeed = style.numberOr("maxspeed", 8);
  sp.flightMs = style.numberOr("flytime", 500);  // FACT: ms for a shot to cover the window's width
  sp.ringMs = style.numberOr("bonusflytime", 500);
  // FACT: a colour match always makes recipe-size rings; `bonuscount` (default the recipe size, nil in every product) is what they
  // are worth together, credited once in the tick the first of them arrives.
  if (auto b = style.number("bonuscount")) sp.ringCredit = static_cast<int>(std::clamp(std::round(*b), 0.0, 255.0));

  if (!productName.empty()) sp.look.push_back({"item/" + productName + "_big", {1, 1, 1, 1}});  // FACT: the 64 px picture
  sp.ingredientArt = {"item/", "image/trayring", "", "image/plate", "image/pole"};
  sp.trayPrefix = "image/tray";
  sp.gunSheet = "image/gun_shoot.xml";
  sp.gunBase = "image/gun_base";
  sp.sparkleSheet = "image/powerup.xml";
  sp.gunRotationOffsetDeg = kGunOffsetDeg;
  // No pistons or timer dial. FACT (Draw): the machine `image/machine_<type>` at (690, 299) with the template tray at its centre, two
  // `image/factory_finger` at (612, 223) and (612, 373) resting at 135 and 45 degrees, and the crate at the end of the belt, 23 px up.
  sp.pistons = sp.fingers = sp.timerDial = false;
  sp.machineSprite = !typeName.empty();
  sp.machine = "image/machine_" + typeName;
  sp.machinePos = {690, 299};
  sp.templatePos = gfx::PointF{0, 0};
  sp.fingerImage = "image/factory_finger";
  sp.fingerUpperPos = {612 - 690, 223 - 299};
  sp.fingerLowerPos = {612 - 690, 373 - 299};
  sp.fingerUpperRestDeg = 135;
  sp.fingerLowerRestDeg = 45;
  sp.machineMs = 600;
  sp.ropes = true;
  sp.box = true;
  sp.boxOpen = "image/large_crate_open";
  sp.boxClosed = "image/large_crate";
  sp.boxFront = "image/large_crate_front";
  sp.boxProductOffset = {0, 0};
  sp.boxOffset = {0, -23};
  sp.productScale = 1.0f;
  sp.matchPopup = true;
  sp.hitRadius = kHitRadius;
  sp.flyOutOfWindow = true;
  sp.deliverAtWrap = true;
  sp.independentProducts = true;
  sp.fullBonusStaggerMs = 80;
  sp.ringsAreProducts = true;
  sp.slowAbort = true;
  sp.plainSuccessSounds = true;
  sp.bagRounds = sp.trayCount;
  return sp;
}

std::string factorySound(std::string_view e) {
  static const std::pair<std::string_view, std::string_view> kTable[] = {
      {"clock_tick", "countdown_tick"},
      {"start_bell", "minigame_starting_bell"},
      {"whistle", "warning_bell"},
      {"launch", "minigame_load_launch"},
      {"snatch", "factory_snatch"},
      {"ingredient_complete", "template_complete"},
      {"ingredient_complete_colormatch", "colormatch_complete"},
      {"ingredient_complete_full_bonus", "powerup"},
      {"chocolate_counted", "template_success"},
      {"ingredient_failure_factory shutdown", "factory_shut_down"},
  };
  for (const auto& [from, to] : kTable)
    if (e == from) return std::string(to);
  struct Numbered {
    std::string_view prefix, out;
    bool pad;
  };
  static const Numbered kNumbered[] = {{"ingredient_success_", "ingredient_success_", true},
                                       {"ingredient_color_match_", "color_match_", false},
                                       {"ingredient_failure_", "ingredient_failure_", true}};
  for (const Numbered& n : kNumbered) {
    if (e.size() > n.prefix.size() && e.substr(0, n.prefix.size()) == n.prefix) {
      int v = 0;
      if (!parseNumber(e.substr(n.prefix.size()), v)) return {};
      return std::string(n.out) + (n.pad ? twoDigits(v) : std::to_string(v));
    }
  }
  return {};
}

double factoryPerformance(unsigned production, bool aborted, double startMs, double rateMs, int recipeSize, bool colors) {
  if (aborted) return -1;
  if (rateMs <= 0 || recipeSize <= 0) return 0;
  // TFactoryGame::OnTaskAnimate: unsigned divisions, the whole-game ingredient count added again when colours are on, and the
  // result truncated to an int.
  const double perGame = std::floor(startMs / rateMs);
  const double ideal = std::floor(perGame / recipeSize) + (colors ? perGame : 0);
  if (ideal <= 0) return 0;
  return std::trunc(100.0 * production / ideal);
}

// ------------------------------------------------------------------ FactoryGame

FactoryGame::FactoryGame(ui::WindowManager& wm, script::ScriptContext& ctx) : ChocolateFactory(wm, ctx, {}) {}

FactoryGame* FactoryGame::active() { return dynamic_cast<FactoryGame*>(FactoryWindow::active()); }

void FactoryGame::init(const ui::Style& style) {
  ChocolateFactory::init(style);
  recipeSize_ = static_cast<int>(spec().recipe.size());
  rateMs_ = style.numberOr("ingredientrate", 350);
  colors_ = style.numberOr("colorcount", 2) != 0;
  colorWarn_ = style.booleanOr("colorWarn", false);
}

void FactoryGame::draw(ui::DrawContext& ctx) const {
  ChocolateFactory::draw(ctx);  // the dial, absent here
  if (root_) root_->draw(ctx.renderer, SpriteSpec{ctx.origin, 1, 0, ctx.alpha});
}

void FactoryGame::postDraw(ui::DrawContext&) const {}

double FactoryGame::performance() const {
  return factoryPerformance(score(), aborted(), gameMs_, rateMs_, recipeSize_, colors_);
}

std::string FactoryGame::detail() const {
  int popups = 0;
  for (const ChocolateTray* t : trays()) popups += t->textMs > 0;
  return ChocolateFactory::detail() + " popups=" + std::to_string(popups) + " started=" + (started_ ? "1" : "0") + " performance=" + std::to_string(performance());
}

// ------------------------------------------------------------------ lifecycle

void FactoryGame::resume(lua_State* L) {
  if (!started_) {
    startGame(L);
    started_ = true;  // after: the reset inside startGame must call nothing back
    return;
  }
  if (state_ != State::Over) resumeGame(L);
}

void FactoryGame::pause(lua_State* L) {
  if (started_ && state_ != State::Over) pauseGame(L, "game_paused");
}

void FactoryGame::reset() {
  started_ = false;  // before: the pause inside resetGame must call nothing back
  resetGame();
}

// ------------------------------------------------------------------ the hooks

void FactoryGame::sound(std::string_view name) const {
  const std::string file = factorySound(name);
  if (file.empty() || !ctx_.audio) return;
  ctx_.audio->playFile(script::choc2SoundPath(file));
}

void FactoryGame::callGlobal(lua_State* L, const char* name, const double* arg) const {
  const int top = lua_gettop(L);
  lua_getglobal(L, name);
  if (!lua_isfunction(L, -1)) {
    if (ctx_.warned.insert(std::string("choc2-factory-no-") + name).second)
      ctx_.log.write(std::string("[choc2] FactoryGame: the Lua global ") + name + " is not defined, not called");
    lua_settop(L, top);
    return;
  }
  int nargs = 0;
  if (arg) {
    lua_pushnumber(L, *arg);
    nargs = 1;
  }
  if (lua_pcall(L, nargs, 0, 0) != 0) {
    if (ctx_.host) ctx_.host->logLuaError(L);
    else ctx_.log.write(std::string("[lua-error] ") + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "?"));
  }
  lua_settop(L, top);
}

void FactoryGame::notifyPaused(lua_State* L, const std::string& key) {
  if (!started_ || state_ == State::Over) return;
  const char* callback = key == "game_paused" ? "MinigamePause"
                         : key == "factory_waste" ? "MinigameWarn"
                         : key == "factory_color" ? "MinigameColor"
                                                  : nullptr;
  if (!callback) {
    if (ctx_.warned.insert("choc2-factory-key-" + key).second) ctx_.log.write("[choc2] FactoryGame: no callback for the pause key '" + key + "'");
    return;
  }
  callGlobal(L, callback);
}

void FactoryGame::onSecondMatch() {
  if (!started_ || !colorWarn_ || state_ != State::Playing) return;
  colorWarn_ = false;  // the hint is shown once
  pauseGame(luaForStep(), "factory_color");
}

void FactoryGame::showGameOver() {
  showButtons(false, false);
  const double p = performance();
  callGlobal(luaForStep(), "MinigameEnd", &p);
}

}  // namespace choco::game::choc2
