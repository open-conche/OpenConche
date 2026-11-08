#include "games/common/factory/FactoryWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "audio/Audio.h"
#include "games/common/World.h"
#include "script/AudioBridge.h"
#include "ui/DrawContext.h"
#include "ui/WindowManager.h"
#include "ui/widgets/Widgets.h"

namespace choco::game {

namespace {

std::vector<FactoryWindow*>& liveFactories() {
  static std::vector<FactoryWindow*> v;
  return v;
}

// Sets callerState_ for the length of a native's run, so the nested pause fill uses the caller's Lua state.
struct CallerScope {
  CallerScope(lua_State*& slot, lua_State* L) : slot_(slot), old_(slot) { slot_ = L; }
  ~CallerScope() { slot_ = old_; }
  lua_State*& slot_;
  lua_State* old_;
};

}  // namespace

FactoryWindow::FactoryWindow(ui::WindowManager& wm, script::ScriptContext& ctx, FillFn fill)
    : BSGWindow(wm), ctx_(ctx), tex_(wm, &ctx.log), fillFn_(std::move(fill)), audio_(ctx.audio) {
  fill_ = false;  // no flat fill: the window draws its Text children and its sprites only
  liveFactories().push_back(this);
}

FactoryWindow::~FactoryWindow() {
  // Deviation (docs/re/audio.md s.10.7): the original leaves CADI paused if a factory dies while paused; we unpause, so
  // leaving through a path other than the pause dialog's OK can't freeze the music.
  if (paused_ && audio_ && common_.pauseAudio) audio_->setGamePaused(false);
  std::erase(liveFactories(), this);
}

FactoryWindow* FactoryWindow::active() { return liveFactories().empty() ? nullptr : liveFactories().back(); }

// ------------------------------------------------------------------ building

void FactoryWindow::init(const ui::Style& style) {
  BSGWindow::init(style);
  root_ = std::make_unique<Sprite>(0, "factory");
  root_->topLeft = true;
  back_ = std::make_unique<Sprite>(0, "back");
  back_->topLeft = true;
  // The window is its modal's default focus, so a message nobody else handles reaches it (CF 3.2 step 1).
  for (ui::Window* p = parent(); p; p = p->parent()) {
    if (p->isModal()) {
      static_cast<ui::ModalWindow*>(p)->defaultFocus = id();
      break;
    }
  }
}

FactoryCommon FactoryWindow::parseCommon(const ui::Style& style) {
  FactoryCommon c;
  c.warnOnMiss = style.booleanOr("warnOnMiss", false);
  c.countdownMs = static_cast<int>(style.numberOr("countdowntime", 3000));
  c.gameMs = static_cast<int>(style.numberOr("gametime", 60000));
  c.warnMs = static_cast<int>(style.numberOr("warntime", 8000));
  if (auto pts = style.table("missed")) {
    for (int i = 1; i <= pts->size(); ++i) {
      if (auto p = pts->element(i)) c.park.push_back({static_cast<float>(p->numberAt(1).value_or(0)), static_cast<float>(p->numberAt(2).value_or(0))});
    }
  }
  return c;
}

void FactoryWindow::applyCommon(const FactoryCommon& c) {
  common_ = c;
  warnOnMiss_ = c.warnOnMiss;
  countdownMs_ = c.countdownMs;
  gameMs_ = c.gameMs;
  warnMs_ = c.warnMs;
  park_ = c.park;
}

void FactoryWindow::buildTimer() {
  auto face = std::make_unique<Sprite>(0, "timer");
  face->texture = tex_.texture("image/timer_face");
  face->pos = {720, 80};
  face_ = &back_->addChild(std::move(face));
  auto hand = std::make_unique<Sprite>(1, "timer_hand");
  hand->texture = tex_.texture("image/timer_hand");
  hand_ = &face_->addChild(std::move(hand));
}

void FactoryWindow::postChildrenInit(const ui::Style& style) {
  BSGWindow::postChildrenInit(style);
  setSubtreeAlpha(*this, 1.0f);  // SetAlpha(1.0, true): the BSGWindow default hides the window and its children
  if (face_) {
    if (const ui::Window* score = find(common_.scoreChild)) face_->pos = {static_cast<float>(score->rect().x + score->rect().w / 2), static_cast<float>(score->rect().y + score->rect().h / 2)};
  }
  resetGame();
  if (World* w = worldOf(ctx_)) w->setFactory(id());
  manager().setFocus(id());  // keys reach onKeyDown (Escape pauses)
}

// ------------------------------------------------------------------ small helpers

ui::Window* FactoryWindow::sibling(const char* name) const {
  ui::Window* p = parent();
  return p ? p->find(name) : nullptr;
}

void FactoryWindow::setText(const std::string& child, const std::string& text) {
  if (ui::Window* w = find(child); w && w->hasLabel()) w->setLabel(text);
}

void FactoryWindow::showButtons(bool go, bool pause) {
  auto set = [](ui::Window* w, bool shown) {
    if (!w) return;
    w->setShown(shown);
    if (auto* b = dynamic_cast<ui::ButtonWindow*>(w)) b->resetState();
  };
  set(sibling("go"), go);
  set(sibling("pause"), pause);
}

void FactoryWindow::sound(std::string_view name) const {
  script::soundEvent(ctx_, std::string(name));
}

void FactoryWindow::setCursor(const char* name) {
  if (cursor_ == name) return;
  cursor_ = name;
  const bool gun = std::strcmp(name, "gun_aimer") == 0;
  const std::string path = gun ? common_.gunCursor : common_.plainCursor;
  const int hot = gun ? 16 : 1;
  if (ctx_.setCursor && ctx_.setCursor(path, hot, hot)) return;
  if (ctx_.stubs.hit(std::string("cursor.") + name)) ctx_.log.write(std::string("[unimplemented] cursor ") + name + " (the cursor is not changed)");
}

void FactoryWindow::fillSibling(lua_State* L, ui::Window& target, const char* file, const std::string* pauseText) {
  if (fillFn_) fillFn_(L, target, file, pauseText);
}

void FactoryWindow::addScore(unsigned n, bool bonus) {
  score_ += n;
  if (bonus) bonus_ += n;
  setText(common_.scoreChild, std::to_string(score_));
}

// ------------------------------------------------------------------ lifecycle (CF 4.1)

void FactoryWindow::resetGame() {
  clock_ = gameMs_ + countdownMs_;
  score_ = bonus_ = misses_ = 0;
  state_ = State::Countdown;
  resetParts();
  pauseGame(luaForStep(), "game_paused");
  for (Ingredient* i : missed_) root_->removeChild(*i);
  missed_.clear();
  setText(common_.scoreChild, "0");
  if (common_.clockTexts) {
    setText(common_.timeChild, clockText(gameMs_));
    setText(common_.countdownChild, "");
  } else {
    setText(common_.timeChild, "");
  }
  sound(introSound());
  showButtons(false, false);
}

void FactoryWindow::startGame(lua_State* L) {
  CallerScope scope(callerState_, L);
  resetGame();
  manager().startWindowAnimation(id(), kStepMs, true);
  setState(State::Countdown);
  resumeGame(L);
}

void FactoryWindow::pauseGame(lua_State* L, const std::string& key) {
  setCursor("cursor");
  if (paused_) return;
  paused_ = true;
  if (state_ != State::Init && audio_ && common_.pauseAudio) audio_->setGamePaused(true);  // BSGSoundPause, only once the game started
  showButtons(true, false);
  pauseParts();
  manager().stopWindowAnimation(id());
  if (ui::Window* info = sibling("information")) {
    fillSibling(L, *info, "ui/game_pause.lua", &key);
    info->setShown(true);
  }
  notifyPaused(L, key);
}

void FactoryWindow::focusChanged() {
  pauseGame(luaForStep(), "game_paused");
}

void FactoryWindow::resumeGame(lua_State* L) {
  CallerScope scope(callerState_, L);
  setCursor("gun_aimer");
  if (!paused_) return;
  showButtons(false, true);
  if (ui::Window* info = sibling("information")) info->setShown(false);
  restartParts();
  manager().startWindowAnimation(id(), kStepMs, true);
  setState(state_);  // re-applies the state's entry actions, as the original does
  paused_ = false;
  if (audio_ && common_.pauseAudio) audio_->setGamePaused(false);  // BSGSoundUnpause
}

void FactoryWindow::setState(State s) {
  enterState(s);
  state_ = s;
}

void FactoryWindow::showGameOver() {
  if (ui::Window* info = sibling("information")) {
    fillSibling(luaForStep(), *info, gameOverFile(), nullptr);
    info->setShown(true);
  }
  showButtons(false, false);
}

// ------------------------------------------------------------------ the clock rows (CF 4.2)

std::string FactoryWindow::clockText(int ms) {
  const int secs = (ms + 999) / 1000;
  char buf[24];
  std::snprintf(buf, sizeof buf, "%d:%02d", secs / 60, secs % 60);
  return buf;
}

// Chocolatier 2: the whole seconds left, rounded down (`%lu:%02lu` of t / 1000), so 59990 reads 0:59.
std::string FactoryWindow::clockTextFloor(int ms) {
  const int secs = std::max(ms, 0) / 1000;
  char buf[24];
  std::snprintf(buf, sizeof buf, "%d:%02d", secs / 60, secs % 60);
  return buf;
}

void FactoryWindow::stepClock() {
  if (state_ == State::Countdown) {
    const int old = clock_;
    const bool counting = gameMs_ + kStepInt < old;
    clock_ -= kStepInt;
    if (counting) {
      // Chocolatier 2 compares the ceilings of the remaining seconds (TFactoryGame::OnTaskAnimate), Chocolatier 3 the floors.
      const bool digit = common_.clockTexts ? (old - gameMs_ + 999) / 1000 != (clock_ - gameMs_ + 999) / 1000 : old / 1000 != clock_ / 1000;
      if (digit) {
        sound("clock_tick");
        setText(common_.countdownChild, std::to_string((clock_ - gameMs_ + 999) / 1000));
      }
    } else {
      sound("start_bell");
      setState(State::Playing);
      setText(common_.countdownChild, "");
    }
  } else if (state_ == State::Playing) {
    if (clock_ <= kStepInt) {
      if (hand_) hand_->rotationDeg = 0;
      setText(common_.timeChild, "0:00");
      setState(State::Ending);
    } else {
      const int old = clock_;
      clock_ -= kStepInt;
      if (warnMs_ < old && clock_ <= warnMs_) sound("whistle");
      setText(common_.timeChild, common_.clockTexts ? clockTextFloor(clock_) : clockText(clock_));
      if (hand_ && gameMs_ > 0) hand_->rotationDeg = static_cast<float>(-(2.0 * clock_ / gameMs_) * 180.0);
    }
  }
}

// ------------------------------------------------------------------ misses (CF 5.4)

void FactoryWindow::missed(Ingredient& ing) {
  missed_.push_back(&ing);
  if (misses_ < park_.size()) {
    ing.scale = common_.missScale;
    ing.pos = park_[misses_];
    if (!common_.missMarker.empty()) {
      auto mark = std::make_unique<Sprite>(-1, "miss_marker");  // under the ingredient, and removed with it
      mark->texture = tex_.texture(common_.missMarker);
      ing.addChild(std::move(mark));
    }
  }
  ++misses_;
  if (common_.choc2Misses) {
    // TFactoryGame::TickFlying: the speed, then the sound, then the first-miss pause or the abort.
    ScalingTask& speed = speedTask();
    speed.scale = (speed.scale - 1.0) * 0.5 + 1.0;
    const bool last = misses_ == park_.size();
    sound(last ? "snatch" : "ingredient_failure_" + std::to_string(misses_ + 1));
    if (warnOnMiss_ && misses_ == 1) {
      pauseGame(luaForStep(), firstMissKey());
    } else if (last) {
      setState(State::Aborted);
      sound("ingredient_failure_factory shutdown");
    }
    return;
  }
  if (misses_ == park_.size()) {
    setState(State::Aborted);
    sound("ingredient_failure_factory shutdown");
    return;
  }
  ScalingTask& speed = speedTask();
  speed.scale = std::max(1.0, speed.scale * 0.5);
  sound("ingredient_failure_" + std::to_string(misses_));
  if (warnOnMiss_ && misses_ == 1) pauseGame(luaForStep(), firstMissKey());
}

// ------------------------------------------------------------------ input

bool FactoryWindow::onMouseMove(int x, int y) {
  aim({static_cast<float>(x), static_cast<float>(y)});
  return false;
}

bool FactoryWindow::onMouseDown(int x, int y) {
  if (state_ == State::Playing && !paused_) fire({static_cast<float>(x), static_cast<float>(y)});
  // The original returns the window's swallow flag, which is off: the press goes on to the Pause button behind the
  // window (which would otherwise never be reachable).
  return swallow_;
}

bool FactoryWindow::onKeyDown(const ui::KeyEvent& ev) {
  if (ev.name == "Escape" && !paused_ && state_ != State::Over) pauseGame(luaForStep(), "game_paused");
  return false;  // the modal's onChar then presses `cancel`
}

// ------------------------------------------------------------------ frame, draw, dump

void FactoryWindow::frame(double nowMs) {
  if (!root_) return;
  root_->tickScripts(nowMs);
  root_->resortDirty();
}

void FactoryWindow::draw(ui::DrawContext& ctx) const {
  if (back_) back_->draw(ctx.renderer, SpriteSpec{ctx.origin, 1, 0, ctx.alpha});
}

void FactoryWindow::postDraw(ui::DrawContext& ctx) const {
  if (root_) root_->draw(ctx.renderer, SpriteSpec{ctx.origin, 1, 0, ctx.alpha});
  // The big countdown digit sits where the gun is: drawn again above the sprites so it can be read (a deviation: the
  // original draws it under them).
  if (const ui::Window* c = find(common_.countdownChild); c && !c->label().empty()) ui::drawWindow(*c, ctx);
}

std::string FactoryWindow::detail() const {
  char buf[200];
  std::snprintf(buf, sizeof buf, " state=%d paused=%d clock=%d score=%u bonus=%u misses=%u cursor=%s", static_cast<int>(state_),
                paused_ ? 1 : 0, clock_, score_, bonus_, misses_, cursor_.c_str());
  return buf;
}

}  // namespace choco::game
