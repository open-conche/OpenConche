#include "games/greatcc/tutorial/Tutorial.h"

#include <algorithm>
#include <functional>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/scene/SpriteManager.h"
#include "games/greatcc/tutorial/TutorialManager.h"
#include "games/greatcc/tutorial/TutorialSpider.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {

constexpr double kFadeMs = 500;  // the alpha fade of a step and of the tutorial

std::shared_ptr<AlphaEffect> fade(bool in) { return std::make_shared<AlphaEffect>(in, 1.0f, kFadeMs, -1.0f, 0); }

SpriteManager* levelSprites(script::ScriptContext& ctx) {
  GreatCCWorld* w = worldOf(ctx);
  return w ? w->tutorials().levelSprites() : nullptr;
}

// The strings of an array table: an empty one is skipped.
void forEachName(const StyleChain& style, const char* key, const std::function<void(const std::string&)>& fn) {
  const auto t = style.table(key, 2);
  if (!t) return;
  for (int i = 1; i <= t->size(); ++i) {
    const std::string s = t->stringAt(i).value_or("");
    if (!s.empty()) fn(s);
  }
}

}  // namespace

// ================================================================ the step

TutorialStep::TutorialStep(script::ScriptContext& ctx, int number) : TiledObject(ctx, "", "tutorialstep"), number_(number) {}

double TutorialStep::nowMs() const {
  if (manager()) return clockNow();
  ui::ModalWindow* top = context().wm.topModal();
  return top ? context().wm.modalNow(*top) : 0;
}

void TutorialStep::loadFromTable(const StyleChain& style) {
  lookup_.clear();
  action_.clear();
  exempt_.clear();
  triggers_.clear();
  duration_ = style.intOr("duration", duration_, 2);
  delay_ = style.intOr("delay", delay_, 2);
  pause_ = style.boolOr("pause", true, 2);
  dim_ = style.boolOr("dim", true, 2);
  skip_ = style.boolOr("skip", true, 2);
  // highlight_objects: only exempt from the dim
  forEachName(style, "highlight_objects", [&](const std::string& n) { exempt_.insert(n); });
  // action_objects: exempt, looked up, clickable
  forEachName(style, "action_objects", [&](const std::string& n) {
    lookup_.insert(n);
    exempt_.insert(n);
    action_.insert(n);
  });
  // trigger_objects: { name, kind, action = bool }: exempt and looked up, clickable unless action = false
  if (const auto t = style.table("trigger_objects", 2)) {
    for (int i = 1; i <= t->size(); ++i) {
      const auto e = t->element(i);
      if (!e) continue;
      const std::string name = e->stringAt(1).value_or("");
      if (name.empty()) continue;
      triggers_[name] = static_cast<int>(e->numberAt(2).value_or(0));
      lookup_.insert(name);
      exempt_.insert(name);
      if (e->booleanOr("action", true)) action_.insert(name);
    }
  }
  // button: the Continue button, a child of the step (this table and one parent)
  if (const auto b = style.table("button", 1)) {
    const StyleChain bc(*b, nullptr);
    auto button = VButton::create(context(), 0, "");
    button->loadFromTable(bc);
    button->init();
    addChild(button);
  }
  const bool any = style.boolOr("clickAny", clickAny_);
  clickAny_ = !(!any && (duration_ >= 0 || !lookup_.empty()));
  TiledObject::loadFromTable(style);
  noPanel_ = width() < 1 || height() < 1;
  if (noPanel_) return;
  if (!text_) {
    text_ = TextSprite::create(context());
    addChild(text_);
  }
  text_->loadFromTable(style);
  text_->setPosition({0, 0});
  // The tables name the arrows' places on the screen: they move to places relative to the panel.
  for (const auto& [key, child] : childObjects()) {
    child->setMouse(false);
    child->setPosition({child->position().x - position().x, child->position().y - position().y});
  }
}

void TutorialStep::init() {
  begun_ = 0;
  if (fadeIn_) {
    fadeIn_->destroy();
    fadeIn_.reset();
  }
  if (fadeOut_) {
    fadeOut_->destroy();
    fadeOut_.reset();
  }
  setVisible(false);
  TiledObject::init();
}

void TutorialStep::begin(const std::shared_ptr<SpriteEffect>& fadeInEffect, bool tutorialPause) {
  if (begun_ != 0) return;
  begun_ = nowMs();
  if (!noPanel_) {
    fadeIn_ = fadeInEffect;
    addEffect(fadeIn_);
    setVisible(true);
  } else if (fadeInEffect) {
    fadeInEffect->stop();  // nothing shows, so there is nothing to wait for
  }
  if (SpriteManager* level = levelSprites(context())) level->softPause(pause_ && tutorialPause);
}

void TutorialStep::end(const std::shared_ptr<SpriteEffect>& fadeOutEffect) {
  if (!noPanel_) {
    removeEffect(fadeIn_);
    fadeIn_.reset();
    fadeOut_ = fadeOutEffect;
    addEffect(fadeOut_);
  } else if (fadeOutEffect) {
    fadeOutEffect->stop();
  }
  if (SpriteManager* level = levelSprites(context())) level->softPause(false);
}

void TutorialStep::advance() {
  if (advanced_ == 0) advanced_ = nowMs();
}

bool TutorialStep::durationComplete() const { return begun_ == 0 || duration_ < 0 || duration_ <= nowMs() - begun_; }

bool TutorialStep::delayComplete() const { return advanced_ == 0 || delay_ < 0 || delay_ <= nowMs() - advanced_; }

void TutorialStep::update(bool softPaused) {
  const std::shared_ptr<VSprite> keep = shared_from_this();
  TiledObject::update(softPaused);
  if (fadeOut_ && !fadeOut_->inProgress()) {
    begun_ = 0;
    setVisible(false);
    fadeOut_.reset();
  }
}

int TutorialStep::trigger(const std::string& name) const {
  const auto it = triggers_.find(name);
  return it == triggers_.end() ? -1 : it->second;
}

// ================================================================ the tutorial

Tutorial::Tutorial(script::ScriptContext& ctx, std::string name, int slot)
    : VObject(ctx, "", ""), tutorialName_(std::move(name)), slot_(slot) {}

TutorialManager* Tutorial::tutorials() const {
  GreatCCWorld* w = worldOf(context());
  return w ? &w->tutorials() : nullptr;
}

Tutorial::~Tutorial() = default;

void Tutorial::loadFromTable(const StyleChain& style) {
  steps_.clear();
  modes_.clear();
  VObject::loadFromTable(style);
  minLevel_ = style.intOr("minlevel", 0);
  maxLevel_ = style.intOr("maxlevel", INT_MAX, 0);
  window_ = style.stringOr("window", "", 0);
  pause_ = style.boolOr("pause", pause_);
  repeatable_ = style.boolOr("repeatable", repeatable_, 0);
  tryonce_ = style.boolOr("tryonce", tryonce_, 0);
  if (const auto modes = style.table("modes"))
    for (int i = 1; i <= modes->size(); ++i) modes_.insert(static_cast<int>(modes->numberAt(i).value_or(0)));
  if (const auto steps = style.table("steps", 0)) {
    const StyleChain group(*steps, &style);
    for (int i = 1; i <= steps->size(); ++i) {
      const auto entry = steps->element(i);
      if (!entry) continue;
      const StyleChain sc(*entry, &group);
      auto s = TutorialStep::create(context(), i);
      s->loadFromTable(sc);
      s->init();
      addChild(s);
      steps_[i] = std::move(s);
    }
  }
  if (const auto skip = style.table("skip", 1)) {
    const StyleChain sc(*skip, nullptr);
    skip_ = VButton::create(context(), 0, "");
    skip_->loadFromTable(sc);
    skip_->init();
    addChild(skip_);
  }
}

void Tutorial::init() {
  step_ = 0;
  if (fade_) fade_->destroy();
  fade_.reset();
  deferred_ = false;
  complete_ = true;
  finished_ = true;
  for (const auto& [n, s] : steps_)
    if (s) s->init();
  if (!spider_) spider_ = std::make_shared<TutorialSpider>(*this);
  else spider_->reset(true);
}

// ---- state ----

void Tutorial::setState(int s, bool save) {
  if (state_ >= s) return;
  state_ = s;
  if (save) updateCurrentUser();
}

void Tutorial::loadCurrentUser() {
  if (TutorialManager* m = tutorials()) state_ = static_cast<signed char>(m->loadState(tutorialName_));
}

void Tutorial::updateCurrentUser() {
  if (TutorialManager* m = tutorials()) m->saveState(tutorialName_, state_);
}

// ---- running ----

std::shared_ptr<TutorialStep> Tutorial::currentStep() const { return step(step_); }

std::shared_ptr<TutorialStep> Tutorial::step(int number) const {
  const auto it = steps_.find(number);
  return it == steps_.end() ? nullptr : it->second;
}

bool Tutorial::activate(bool fresh) {
  const std::shared_ptr<VSprite> keep = shared_from_this();
  if (fresh) {
    init();
    complete_ = false;
    finished_ = false;
    spider_->activate();
    advanceStep();
    setState(1, true);
  } else {
    step_ = numSteps();
    advanceStep();
  }
  return true;
}

void Tutorial::advanceStep() {
  // The call may come from a click, an arrival, a resume or a message, and the step it moves to may take the tutorial out of the
  // game: it finishes on a live object.
  const std::shared_ptr<VSprite> keep = shared_from_this();
  if (complete_) return;
  if (fade_ && fade_->inProgress()) {
    deferred_ = true;  // the step is still fading in: remembered, retried by Update
    return;
  }
  if (const auto current = currentStep()) current->advance();
  const auto current = currentStep();
  if (current && !(current->durationComplete() && current->delayComplete())) {
    deferred_ = true;
    return;
  }
  TutorialManager* manager = tutorials();
  if (manager && !manager->shouldShowTutorials()) {
    // Tutorials were turned off meanwhile (the skip button): past the last step at once, the dim and the soft pause ended
    step_ = numSteps() + 1;
    setState(1, true);
    if (spider_) spider_->reset(false);
    complete_ = true;
    return;
  }
  ++step_;
  const auto next = currentStep();
  if (!next) {
    // the last step is done: it ends with no fade of its own, the tutorial's alpha takes it out
    if (const auto previous = step(step_ - 1)) previous->end(nullptr);
    if (spider_) spider_->reset(false);
    complete_ = true;
    setState(3, true);
  } else {
    if (step_ > 1)
      if (const auto previous = step(step_ - 1)) previous->end(fade(false));
    fade_ = fade(true);
    next->begin(fade_, pause_);
    if (spider_) spider_->setStep(step_);
  }
  deferred_ = false;
}

int Tutorial::trigger(const std::string& name) const {
  const auto current = currentStep();
  return current ? current->trigger(name) : 0;
}

bool Tutorial::handleMessage(const ui::Message& m) {
  if (m.type != ui::kButtonPress) return false;
  if (skip_ && m.name == skip_->name()) {
    if (TutorialManager* m = tutorials()) m->showTutorials(false);
    advanceStep();
    return true;
  }
  if (trigger(m.name) != kClick) return false;
  advanceStep();
  return true;
}

bool Tutorial::handleMouse(ui::Window* container, gfx::PointI screenPt) {
  if (!container || window_.empty()) return true;
  ui::Window* base = container->find(window_);
  if (!base) base = container;
  const gfx::PointF origin = ui::WindowManager::screenOrigin(*base);
  int px = screenPt.x - static_cast<int>(origin.x), py = screenPt.y - static_cast<int>(origin.y);
  // TWindow::ChildWindowFromPoint(base, point, -1): the deepest shown window under the point (the base itself when none is)
  auto deepest = [&]() -> ui::Window* {
    ui::Window* w = base;
    int x = px, y = py;
    for (;;) {
      ui::Window* hit = nullptr;
      for (const auto& c : w->children()) {
        const gfx::RectI& r = c->rect();
        if (c->shown() && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
          hit = c.get();
          break;
        }
      }
      if (!hit) return w;
      x -= hit->rect().x;
      y -= hit->rect().y;
      w = hit;
    }
  };
  // A window with the kIgnore trigger is collapsed to no width while the point is probed, so that what is under it shows.
  struct Collapsed {
    ui::Window* w;
    int width;
  };
  std::vector<Collapsed> collapsed;
  ui::Window* found = nullptr;
  while ((found = deepest()) != nullptr) {
    if (trigger(found->name()) != kIgnore) break;
    collapsed.push_back({found, found->rect().w});
    found->setSize(0, found->rect().h);
  }
  for (const Collapsed& c : collapsed) c.w->setSize(c.width, c.w->rect().h);
  if (!found || found == base) return false;
  for (ui::Window* w = found; w && w != base; w = w->parent())
    if (trigger(w->name()) != -1) return true;
  return false;
}

void Tutorial::update(bool softPaused) {
  const std::shared_ptr<VSprite> keep = shared_from_this();
  VObject::update(softPaused);
  const auto current = currentStep();
  if (!current) return;
  if (fade_ && fade_->inProgress()) return;
  if (deferred_ && current->durationComplete() && current->delayComplete()) {
    deferred_ = false;
    advanceStep();
  }
}

}  // namespace choco::games::greatcc
