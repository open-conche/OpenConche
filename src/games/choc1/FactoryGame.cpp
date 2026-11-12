#include "games/choc1/FactoryGame.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "audio/Audio.h"
#include "games/choc1/Choc1AudioNatives.h"
#include "games/choc1/Choc1Music.h"
#include "games/choc1/Choc1World.h"
#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"
#include "ui/DrawContext.h"
#include "ui/ScriptSink.h"
#include "ui/Style.h"
#include "ui/WindowManager.h"

namespace choco::game::choc1 {

namespace {

constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

// A Lua string literal in single quotes.
std::string luaQuote(const std::string& s) {
  std::string out = "'";
  for (const char c : s) {
    if (c == '\'' || c == '\\') out += '\\';
    out += c;
  }
  return out + "'";
}

}  // namespace

FactoryGame::~FactoryGame() {
  // A game closed while paused must not leave the sound paused.
  if (ctx_.audio && sim_ && sim_->state() == FactorySim::State::Paused) ctx_.audio->setGamePaused(false);
}

// ---- Init (0040b080) and PostInit (00410830) ----

void FactoryGame::init(const ui::Style& style) {
  Window::init(style);  // x y w h name
  if (rect().w <= 0 || rect().h <= 0) setSize(800, 600);
  if (name().empty()) setName("factory");
  character_ = style.stringOr("character", character_);
  port_ = style.stringOr("port");
  prodname_ = style.stringOr("prodname", prodname_);

  FactorySim::Config cfg;
  cfg.firstMiss = style.booleanOr("firstMiss", false);
  if (auto type = style.table("type")) {
    cfg.trayCount = static_cast<int>(type->numberOr("trayCount", cfg.trayCount));
    cfg.ingredientRate = static_cast<int>(type->numberOr("ingredientRate", cfg.ingredientRate));
    cfg.speedup = type->numberOr("speedup", cfg.speedup);
    cfg.startspeed = type->numberOr("startspeed", cfg.startspeed);
    cfg.maxspeed = type->numberOr("maxspeed", cfg.maxspeed);
  }
  // The recipe {name = qty}: the ingredient order is the table's own iteration order (the exe's Lua 5.0 order is not known;
  // it only decides which icon sits where on the ring).
  if (auto recipe = style.table("recipe"))
    recipe->forEachNumber([&](const std::string& n, double qty) {
      ingredients_.push_back(n);
      cfg.quantities.push_back(static_cast<int>(qty));
    });
  CrtRand* shared = nullptr;
  if (auto* world = choc1WorldOf(ctx_)) shared = &world->crtRand;
  sim_ = std::make_unique<FactorySim>(std::move(cfg), [this, shared]() { return (shared ? *shared : localRand_).next(); });

  auto* tex = manager().textures();
  if (!tex) return;
  wheel_ = tex->load("image/fac_wheel", "image/fac_wheel_mask");  // the mask keeps its alpha in red (TextureCache); a mismatch is logged there
  flywheel_ = tex->load("image/fac_flywheel");
  gear_ = tex->load("image/fac_gear");
  smallWheel_ = tex->load("image/fac_smallwheel");
  plate_ = tex->load("image/fac_plate");
  machineOver_ = tex->load("image/fac_machineover");
  upperArm_ = tex->load("image/fac_upperarm");
  lowerArm_ = tex->load("image/fac_lowerarm");
  crateFront_ = tex->load("image/fac_cratefront");
  crateLid_ = tex->load("image/fac_cratelid");
  bellows_ = tex->load("image/fac_bellows");
  missedCell_ = tex->load("image/missed_ingredient");
  product_ = tex->load("item/" + prodname_);
  const int total = sim_->total();
  if (total >= 2 && total <= 6) {  // the tray art exists for 2..6 slots (2.2)
    trayEmpty_ = tex->load("image/tray" + std::to_string(total));
    trayFull_ = tex->load("image/traycomplete" + std::to_string(total));
  } else {
    ctx_.log.write("[choc1] FactoryGame: no tray art for " + std::to_string(total) + " ingredients per tray");
  }
  for (const std::string& n : ingredients_) icons_.push_back(tex->load("item/" + n));
  if (manager().sheets()) {
    gun_ = manager().sheets()->get("image/gun_shoot.xml");
    powerup_ = manager().sheets()->get("image/powerup.xml");
  }
}

void FactoryGame::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  // PostInit: the start dialog is up (keeper and gamestart shown, gameend hidden); Reset (2.4) already ran when the sim was
  // made, with its one rand() per queue entry. The pause frame and the countdown start hidden.
  showChild("keeper", true);
  showChild("gamestart", true);
  showChild("gameend", false);
  showChild("paused", false);
  showChild("finished", false);
  showChild("countdown", false);
}

// ---- helpers ----

void FactoryGame::showChild(const char* child, bool shown) {
  if (ui::Window* w = find(child)) w->setShown(shown);
}

void FactoryGame::setLabelOf(const char* child, const std::string& text) {
  if (ui::Window* w = find(child); w && w->hasLabel()) w->setLabel(text);
}

std::string FactoryGame::stringOr(const std::string& key, const std::string& fallback) const {
  if (!ctx_.strings.has(key)) return fallback;
  const std::string t = ctx_.strings.sdkLookup(key);
  return t == "#####" ? fallback : t;
}

// The gun cursor and the arrow, set for real through the app's cursor service (the images `cursor/gun_aimer?simple` at
// StartGame and `cursor/cursor`, hotspots (16,16)/(1,1); docs/re/choc1-factory.md 197-209, 338). Without the service
// (unit tests) or when the platform refuses it (headless dummy video) the request is logged once per name, as before.
void FactoryGame::setCursor(const char* name, bool simpleVariant) {
  if (cursor_ == name) return;
  cursor_ = name;
  const bool gun = std::strcmp(name, "gun_aimer") == 0;
  std::string path = gun ? (simpleVariant ? "cursor/gun_aimer?simple" : "cursor/gun_aimer") : "cursor/cursor";
  const int hot = gun ? 16 : 1;
  if (ctx_.setCursor && ctx_.setCursor(path, hot, hot)) return;
  if (ctx_.stubs.hit(std::string("cursor.") + name))
    ctx_.log.write(std::string("[unimplemented] cursor ") + name + " (the cursor is not changed)");
}

void FactoryGame::playSound(const std::string& name) { script::choc1PlaySound(ctx_, name); }

// StartMusic's work for the factory's own tune (FUN_00409ef0: the same music manager); "" stops it.
void FactoryGame::playMusic(const std::string& file) {
  auto* world = choc1WorldOf(ctx_);
  if (!world || !ctx_.audio) return;
  AudioMusicSink sink(*ctx_.audio);
  world->music.start(sink, file, world->nowMs());
}

// ---- natives (2.3) ----

void FactoryGame::beginRun() {
  setCursor("gun_aimer", true);  // StartGame's ?simple variant
  showChild("keeper", false);
  timerText_ = "1:00";
  counterText_ = "0";
  countdownText_ = "3";
  setLabelOf("timer", timerText_);
  setLabelOf("counter", counterText_);
  setLabelOf("countdown", countdownText_);
  showChild("countdown", true);
  countdownVisible_ = true;
  manager().startWindowAnimation(id(), FactorySim::kStepMs, true);  // INFERENCE: a late 10 ms task catches up
  playMusic("minigame.ogg");
}

void FactoryGame::startGame() {
  if (!sim_) return;
  sim_->start();
  beginRun();
  processEvents();
}

void FactoryGame::restartGame() {
  if (!sim_) return;
  sim_->restart();
  showChild("paused", false);  // Reset step 5
  showChild("finished", false);
  beginRun();
  processEvents();
}

void FactoryGame::togglePause() {
  if (!sim_) return;
  sim_->togglePause();
  processEvents();
}

void FactoryGame::onForeground(bool fg) {
  // 0040afc0 (from the close and deactivate messages): a game that is not in its start dialog and not already paused is
  // paused. Regaining the foreground resumes nothing. INFERENCE: the exe's two messages are taken as "focus lost".
  if (fg || !sim_) return;
  if (sim_->state() != FactorySim::State::Idle && sim_->state() != FactorySim::State::Paused) togglePause();
}

bool FactoryGame::escapePause() {
  if (!sim_) return false;
  const auto s = sim_->state();
  if (s == FactorySim::State::Idle || s == FactorySim::State::Paused) return false;
  togglePause();
  return true;
}

// ---- events ----

void FactoryGame::showEnd() {
  // 2.9 step 3: chartext gets <port>_<verdict>, else the plain verdict key.
  const std::string verdict = sim_->verdict();
  std::string text = stringOr(port_ + "_" + verdict, "");
  if (text.empty()) text = stringOr(verdict, verdict);
  if (ui::Window* keeper = find("keeper"))
    if (ui::Window* chartext = keeper->find("chartext"); chartext && chartext->hasLabel()) chartext->setLabel(text);
  showChild("gamestart", false);
  showChild("gameend", true);
  showChild("keeper", true);
  setCursor("cursor");
}

void FactoryGame::processEvents() {
  for (const FactorySim::Event& e : sim_->takeEvents()) {
    using K = FactorySim::Event::Kind;
    switch (e.kind) {
      case K::Paused:
        if (ctx_.audio) ctx_.audio->setGamePaused(e.arg != 0);  // FUN_00443ea0(soundMgr, pause, -1)
        showChild("paused", e.arg != 0);
        setLabelOf("pause_label", stringOr(e.arg ? "resume" : "pause", e.arg ? "resume" : "pause"));
        setCursor(e.arg ? "cursor" : "gun_aimer");
        break;
      case K::FirstMiss:
        // 2.7: the character's dialog, queued for the next command drain.
        if (ui::ScriptSink* sink = manager().scriptSink())
          sink->runString("table.insert(gCommandQueue, function() local c = LCharacter:ByName(" + luaQuote(character_) +
                          ") c:UIModalSpecial(GetString('wasted_once')) end)");
        break;
      case K::Ended:
        if (e.arg) playMusic("");  // an aborted run stops the music
        playSound(FactorySim::soundName(e));
        showEnd();
        break;
      default: {
        const std::string s = FactorySim::soundName(e);
        if (!s.empty()) playSound(s);
        break;
      }
    }
  }
}

void FactoryGame::syncTexts() {
  const std::string count = std::to_string(sim_->count());
  if (count != counterText_) {
    counterText_ = count;
    setLabelOf("counter", counterText_);
  }
  if (sim_->state() == FactorySim::State::Countdown) {
    const std::string d = std::to_string(sim_->countdownDigit());
    if (d != countdownText_) {
      countdownText_ = d;
      setLabelOf("countdown", d);
    }
  }
  if (countdownVisible_ && !sim_->countdownShown()) {
    countdownVisible_ = false;
    showChild("countdown", false);
  }
  if (sim_->state() == FactorySim::State::Running) {  // Draw: the timer text, state 2 only
    const std::string t = sim_->timerText();
    if (t != timerText_) {
      timerText_ = t;
      setLabelOf("timer", t);
    }
  }
}

bool FactoryGame::onAnimate() {
  if (!sim_) return false;
  sim_->tick();
  processEvents();
  syncTexts();
  return true;
}

// ---- input ----

bool FactoryGame::onMouseMove(int x, int y) {
  if (sim_) sim_->aim(x, y);
  return false;
}

bool FactoryGame::onMouseDown(int x, int y) {
  if (!sim_ || !sim_->click(x, y)) return false;
  processEvents();
  return true;
}

std::optional<gfx::PointF> FactoryGame::autoAim() const {
  if (!sim_ || !shown()) return std::nullopt;
  if (const auto p = sim_->bestAim()) return gfx::PointF{static_cast<float>(p->x), static_cast<float>(p->y)};
  return std::nullopt;
}

// ---- drawing (3) ----

void FactoryGame::drawCentered(ui::DrawContext& ctx, const gfx::Texture& t, double cx, double cy, float rotationDeg) const {
  const float w = static_cast<float>(t.width()), h = static_cast<float>(t.height());
  const gfx::RectF dst{ctx.origin.x + static_cast<float>(cx) - w / 2, ctx.origin.y + static_cast<float>(cy) - h / 2, w, h};
  ctx.renderer.drawTexture(t, {}, dst, {1, 1, 1, ctx.alpha}, gfx::Blend::Normal, rotationDeg, {w / 2, h / 2}, false);
}

// A sheet frame with its registration point on (x, y), rotated about that point.
void FactoryGame::drawFrame(ui::DrawContext& ctx, const gfx::Texture& image, const gfx::SheetFrame& f, double x, double y,
                            float rotationDeg) const {
  const gfx::RectF src{static_cast<float>(f.src.x), static_cast<float>(f.src.y), static_cast<float>(f.src.w), static_cast<float>(f.src.h)};
  const gfx::RectF dst{ctx.origin.x + static_cast<float>(x) - static_cast<float>(f.reg.x),
                       ctx.origin.y + static_cast<float>(y) - static_cast<float>(f.reg.y), src.w, src.h};
  ctx.renderer.drawTexture(image, src, dst, {1, 1, 1, ctx.alpha}, gfx::Blend::Normal, rotationDeg,
                           {static_cast<float>(f.reg.x), static_cast<float>(f.reg.y)}, false);
}

void FactoryGame::drawItem(ui::DrawContext& ctx, int ingredient, double cx, double cy) const {
  if (ingredient >= 0 && ingredient < static_cast<int>(icons_.size()) && icons_[static_cast<std::size_t>(ingredient)])
    drawCentered(ctx, *icons_[static_cast<std::size_t>(ingredient)], cx, cy);
}

void FactoryGame::draw(ui::DrawContext& ctx) const {
  if (!sim_) return;
  using S = FactorySim;
  const FactorySim& sim = *sim_;
  const float angleDeg = static_cast<float>(sim.angle() * kRadToDeg);
  const int total = sim.total();

  // 1. the four conveyor wheels, turned by the spawn clock (0.5 px/ms on a radius of about 24).
  if (smallWheel_)
    for (int i = 0; i < 4; ++i) drawCentered(ctx, *smallWheel_, 16 + 100 * i, 608, static_cast<float>(sim.spawnClock() * 0.5 / 24.0 * kRadToDeg));
  // 2. the belt: the plate under every node that has turned the corner, then the ingredient.
  for (const FactorySim::BeltNode& n : sim.belt()) {
    double x = n.s, y = S::kBeltY;
    if (n.s > 300) x = 300, y = S::kBeltY - (n.s - 300);
    if (y < S::kBeltY && plate_) drawCentered(ctx, *plate_, x, y);
    drawItem(ctx, n.ingredient, x, y);
  }
  // 3. the flywheel, the big wheel with its trays (upright: they counter-rotate), the gun.
  if (flywheel_) drawCentered(ctx, *flywheel_, 123, 15, angleDeg);
  if (wheel_) drawCentered(ctx, *wheel_, S::kCentreX, S::kCentreY, angleDeg);
  for (int i = 0; i < static_cast<int>(sim.trays().size()); ++i) {
    const FactorySim::Tray& t = sim.trays()[static_cast<std::size_t>(i)];
    const FactorySim::Point p = sim.trayScreen(i, sim.angle());
    const auto& image = t.filled == total && total > 0 ? trayFull_ : trayEmpty_;
    if (image) drawCentered(ctx, *image, p.x, p.y);
    for (int k = 0; k < total; ++k) {
      if (t.slots[static_cast<std::size_t>(k)] == S::kEmpty) continue;
      const double a = 2 * 3.14159265358979323846 * k / total;
      drawItem(ctx, t.slots[static_cast<std::size_t>(k)], p.x + 50 * std::sin(a), p.y - 50 * std::cos(a));
    }
    // The power-up sheet plays once over a collected tray (11 frames; the period is an inference).
    if (t.powerupMs >= 0 && powerup_ && powerup_->image && !powerup_->frames.empty()) {
      const std::size_t f = static_cast<std::size_t>(t.powerupMs / kPowerupFrameMs);
      if (f < powerup_->frames.size() - 1) drawFrame(ctx, *powerup_->image, powerup_->frames[f], p.x, p.y);  // the last frame is blank
    }
  }
  if (gun_ && gun_->image && !gun_->frames.empty()) {
    const std::size_t last = gun_->frames.size() - 1;
    const double ms = sim.gunAnimMs();
    const std::size_t f = ms >= 0 && ms < kGunFrameMs * static_cast<double>(last + 1) ? std::min(last, static_cast<std::size_t>(ms / kGunFrameMs)) : 0;
    drawFrame(ctx, *gun_->image, gun_->frames[f], S::kCentreX, S::kCentreY, static_cast<float>(sim.gunAngle() * kRadToDeg));
  }
  // 4. the loaded ingredient at the pivot (states 2, 3 and 5).
  if (sim.loaded() >= 0 && (sim.state() == S::State::Running || sim.state() == S::State::Paused || sim.state() == S::State::Over))
    drawItem(ctx, sim.loaded(), S::kCentreX, S::kCentreY);
  // 5. the shots in flight.
  for (const FactorySim::Shot& s : sim.shots()) drawItem(ctx, s.ingredient, s.x, s.y);
  // 6. the crate and the output conveyor: the bellows, the products dropping in, the crate (front, lid), the product label.
  // INFERENCE: which of the three crate pictures goes where follows their sizes and the backdrop.
  if (bellows_) drawCentered(ctx, *bellows_, 700, 469);
  if (product_)
    for (const double v : sim.products())
      if (v >= 477) drawCentered(ctx, *product_, 700, v);
  if (crateFront_) drawCentered(ctx, *crateFront_, 695, 537);
  // The lid is drawn only with nothing in transit (docs/re/choc-factory-game.md, crate step), so it is open for a falling product.
  if (crateLid_ && sim.products().empty()) drawCentered(ctx, *crateLid_, 693.5, 504);
  if (product_) drawCentered(ctx, *product_, 699, 548);
  // 7. the missed panel: a cell and the wasted ingredient on it, four to a row.
  for (int n = 0; n < static_cast<int>(sim.missedIngredients().size()) && n < 8; ++n) {
    const double cx = 631 + 46 * (n & 3), cy = 144 + 46 * (n >> 2);
    if (missedCell_) drawCentered(ctx, *missedCell_, cx, cy);
    drawItem(ctx, sim.missedIngredients()[static_cast<std::size_t>(n)], cx, cy);
  }
  // 8. the arms and their gears, the machine overlay, the recipe hub.
  const double arm = sim.armPosition();
  const float gearDeg = arm < 0 ? 0.0f : static_cast<float>(arm * 360.0 / 7.0);  // cosmetic
  if (upperArm_) {
    const double dx = S::armUpperOffset(arm);
    ctx.renderer.drawTexture(*upperArm_, {}, {ctx.origin.x + static_cast<float>(723 + dx - 154), ctx.origin.y + 273 - 44,
                                              static_cast<float>(upperArm_->width()), static_cast<float>(upperArm_->height())},
                             {1, 1, 1, ctx.alpha}, gfx::Blend::Normal);
  }
  if (lowerArm_) {
    const double dx = S::armLowerOffset(arm);
    ctx.renderer.drawTexture(*lowerArm_, {}, {ctx.origin.x + static_cast<float>(723 + dx - 154), ctx.origin.y + 323 - 18,
                                               static_cast<float>(lowerArm_->width()), static_cast<float>(lowerArm_->height())},
                             {1, 1, 1, ctx.alpha}, gfx::Blend::Normal);
  }
  if (gear_) {
    drawCentered(ctx, *gear_, 646, 329, gearDeg);
    drawCentered(ctx, *gear_, 639, 272, -gearDeg);
  }
  if (machineOver_) drawCentered(ctx, *machineOver_, 700, 300);  // INFERENCE: centred on the hub
  if (trayFull_) drawCentered(ctx, *trayFull_, 700, 300);
  for (int k = 0; k < total; ++k) {
    const double a = 2 * 3.14159265358979323846 * k / total;
    drawItem(ctx, sim.slotTemplate()[static_cast<std::size_t>(k)], 700 + 50 * std::sin(a), 300 - 50 * std::cos(a));
  }
}

// ---- dump ----

std::string FactoryGame::detail() const {
  if (!sim_) return " (no sim)";
  const FactorySim& s = *sim_;
  auto name = [&](int i) { return i >= 0 && i < static_cast<int>(ingredients_.size()) ? ingredients_[static_cast<std::size_t>(i)] : std::string("?"); };
  std::string out;
  char buf[200];
  std::snprintf(buf, sizeof buf, " state=%d elapsed=%d speed=%.3f angle=%.4f count=%d misses=%d shots=%zu belt=%zu queue=%zu products=%zu",
                static_cast<int>(s.state()), s.elapsedMs(), s.speed(), s.angle(), s.count(), s.misses(), s.shots().size(), s.belt().size(),
                s.queue().size(), s.products().size());
  out += buf;
  out += s.loaded() >= 0 ? " gun=loaded:" + name(s.loaded()) : " gun=empty";
  out += " verdict=" + (s.verdict().empty() ? std::string("-") : s.verdict());
  out += " cursor=" + cursor_;
  out += " template=";
  for (int k = 0; k < s.total(); ++k) out += (k ? "," : "") + name(s.slotTemplate()[static_cast<std::size_t>(k)]);
  out += " trays=";
  for (std::size_t i = 0; i < s.trays().size(); ++i) {
    const FactorySim::Tray& t = s.trays()[i];
    std::snprintf(buf, sizeof buf, "%s%zu@%d,%d:%d/%d", i ? " " : "", i, t.cx(), t.cy(), t.filled, s.total());
    out += buf;
  }
  return out;
}

// ---- the window in the chain ----

namespace {
FactoryGame* findIn(ui::Window& w) {
  if (auto* g = dynamic_cast<FactoryGame*>(&w)) return g;
  for (const auto& c : w.children())
    if (auto* g = findIn(*c)) return g;
  return nullptr;
}
}  // namespace

FactoryGame* FactoryGame::inChain(ui::WindowManager& wm) {
  const auto& modals = wm.modals();
  for (auto it = modals.rbegin(); it != modals.rend(); ++it)
    if (auto* g = findIn(**it)) return g;
  return nullptr;
}

}  // namespace choco::game::choc1
