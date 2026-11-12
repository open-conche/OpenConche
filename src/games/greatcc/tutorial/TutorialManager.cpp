#include "games/greatcc/tutorial/TutorialManager.h"

#include <string>

#include "games/greatcc/scene/SpriteManager.h"
#include "games/greatcc/scene/StyleChain.h"
#include "games/greatcc/scene/VSpriteWindow.h"
#include "games/greatcc/tutorial/TutorialWindow.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

TutorialManager::TutorialManager(script::ScriptContext& ctx, Progress& progress, const Settings& settings, GameStateStore& states)
    : ctx_(ctx), progress_(progress), settings_(settings), states_(states) {}

TutorialManager::~TutorialManager() = default;

SpriteManager* TutorialManager::levelSprites() const {
  if (levelWindow_ == 0) return nullptr;
  auto* w = dynamic_cast<VSpriteWindow*>(ctx_.wm.window(levelWindow_));
  return w && !w->isDestroyed() ? &w->sprites() : nullptr;
}

// ---- loading ----

void TutorialManager::clearTutorials() {
  tutorials_.clear();
  active_.reset();
  level_ = 0;
  levelWindow_ = 0;
}

void TutorialManager::loadFromTable(const ui::Style& tutorials) {
  tutorials_.clear();
  const StyleChain root(tutorials, nullptr);
  tutorials.forEachEntry([&](const std::string& key, const ui::Style* table, const std::optional<std::string>&) {
    if (!table) return;  // `modes` and the like: defaults the tutorials read through their parent
    const StyleChain chain(*table, &root);
    const std::string name = VObject::lower(key);
    auto t = Tutorial::create(ctx_, name, 0);  // every tutorial is given the persistence slot 0
    t->loadFromTable(chain);
    t->init();
    tutorials_[name] = std::move(t);
  });
  loadCurrentUser();
}

void TutorialManager::loadCurrentUser() {
  for (auto& [name, t] : tutorials_)
    if (t) t->loadCurrentUser();
}

Tutorial* TutorialManager::tutorial(const std::string& name) const {
  const auto it = tutorials_.find(name);
  return it == tutorials_.end() ? nullptr : it->second.get();
}

int TutorialManager::loadState(const std::string& tutorialName) const {
  return progress_.getInt(stateKey(tutorialName, settings_.fixBugs), 0);
}

void TutorialManager::saveState(const std::string& tutorialName, int state) {
  if (settings_.fixBugs && ctx_.warned.insert("greatcc-F4").second)
    ctx_.log.write("[fix] F4 tutorials: each tutorial keeps its own state (the original gives every tutorial the one slot 0)");
  progress_.setInt(stateKey(tutorialName, settings_.fixBugs), state);
}

// ---- checking ----

void TutorialManager::checkTutorial(const std::string& name) {
  const auto it = tutorials_.find(name);
  if (it != tutorials_.end()) checkTutorial(it->second);
}

void TutorialManager::checkTutorial(const std::shared_ptr<Tutorial>& t) {
  if (active_ || !t) return;
  if (!t->inMode(settings_.mode) || !t->inRange(level_) || !t->shouldShow()) return;
  if (!shouldShowTutorials()) {
    // Tutorials are off: the tutorial counts as started and nothing shows
    t->setState(1, true);
    states_.set("TutorialEnd", "1");
    return;
  }
  if (t->activate(true)) pushTutorial(t);
}

void TutorialManager::checkTutorialsByTrigger(const std::string& type, const std::string& object) {
  // The tutorials are looked at in name order until one is pushed (the map is copied: a check may reload the table).
  const auto all = tutorials_;
  for (const auto& [name, t] : all) {
    if (active_) break;
    if (!t) continue;
    const std::string triggerType = t->attribute("trigger_type");
    const std::string triggerObject = t->attribute("trigger_object");
    if (triggerType != type) continue;
    if (!triggerObject.empty() && triggerObject != object) continue;
    checkTutorial(t);
  }
}

// ---- push and pop ----

void TutorialManager::pushTutorial(const std::shared_ptr<Tutorial>& t) {
  if (!t) return;
  active_ = t;
  ui::ModalWindow* top = ctx_.wm.topModal();
  if (!top) {
    popTutorial(t);
    return;
  }
  auto window = std::make_unique<TutorialWindow>(ctx_.wm, ctx_);
  auto& placed = static_cast<TutorialWindow&>(top->adoptFront(std::move(window)));
  placed.postChildrenInit(ui::MapStyle());
  placed.setTutorial(t);
  if (SpriteManager* level = levelSprites()) level->setLockDown(true);
  states_.set("TutorialTime_" + std::to_string(states_.playCounter()), "1");
}

void TutorialManager::popTutorial(const std::shared_ptr<Tutorial>& t) {
  if (active_ != t) return;
  active_.reset();
  if (SpriteManager* level = levelSprites()) level->setLockDown(false);
  states_.set("TutorialTime_" + std::to_string(states_.playCounter()), "0");
  states_.set("TutorialEnd", shouldShowTutorials() ? "0" : "1");
}

void TutorialManager::resetTutorials() {
  const auto all = tutorials_;
  for (const auto& [name, t] : all)
    if (t) t->init();
  active_.reset();
}

bool TutorialManager::handleMessage(const ui::Message& m) { return active_ && active_->handleMessage(m); }

bool TutorialManager::handleMouse(ui::Window* container, gfx::PointI screenPt) {
  return active_ && active_->handleMouse(container, screenPt);
}

}  // namespace choco::games::greatcc
