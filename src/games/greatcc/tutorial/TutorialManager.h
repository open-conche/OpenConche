#pragma once

// VTutorialManager (docs/re/greatcc-meta-flow.md 1.2): the tutorials of locations/tutorials.lua by name, the one that is running, the
// Tutorials option, and the entry points the level calls (a customer arrives, a customer orders, the endless run starts). It is a
// member of the world: the sprites reach it through worldOf(ctx), and it reaches the level's sprite window through the id the shop
// gave it. The world is made before the window tree and dies after it.

#include <map>
#include <memory>
#include <string>

#include "games/greatcc/core/GameStateStore.h"
#include "games/greatcc/core/Progress.h"
#include "games/greatcc/core/Settings.h"
#include "games/greatcc/tutorial/Tutorial.h"
#include "ui/Message.h"
#include "ui/Style.h"
#include "ui/Window.h"

namespace choco::games::greatcc {

class SpriteManager;

class TutorialManager {
 public:
  TutorialManager(script::ScriptContext& ctx, Progress& progress, const Settings& settings, GameStateStore& states);
  ~TutorialManager();
  TutorialManager(const TutorialManager&) = delete;
  TutorialManager& operator=(const TutorialManager&) = delete;

  // LoadFromTable: every table-valued key of `tutorials` makes a tutorial named by the lower-cased key (the other keys, `modes`, stay
  // visible to them as defaults); then each reads its state.
  void loadFromTable(const ui::Style& tutorials);
  void clearTutorials();
  // LoadCurrentUser: every tutorial reads its state from the player's progress.
  void loadCurrentUser();
  Tutorial* tutorial(const std::string& name) const;
  std::size_t tutorialCount() const { return tutorials_.size(); }

  // SetSpriteManager: the window whose sprite manager the dim, the lockdown, the exclusive set and the soft pause act on. An id, so a
  // closed window leaves nothing dangling.
  void setLevelWindow(ui::WindowId id) { levelWindow_ = id; }
  ui::WindowId levelWindow() const { return levelWindow_; }
  // The sprite manager of that window (null when it is gone).
  SpriteManager* levelSprites() const;
  // SetCurrentLevel: the number of the level that is being played (0 for none).
  void setCurrentLevel(int number) { level_ = number; }
  int currentLevel() const { return level_; }

  // The Options "Tutorials" checkbox: kept in the player's progress. The skip button turns it off.
  bool shouldShowTutorials() const { return progress_.showTutorials(); }
  void showTutorials(bool show) { progress_.setShowTutorials(show); }

  // An active tutorial that is not finished.
  bool tutorialInEffect() const { return active_ && !active_->isComplete(); }
  const std::shared_ptr<Tutorial>& active() const { return active_; }

  // CheckTutorial(name): nothing while a tutorial runs or when the name is unknown; the tutorial must be for the game mode, the level and
  // its own state. With Tutorials off it is marked started and `TutorialEnd` is 1; else it is activated and pushed.
  void checkTutorial(const std::string& name);
  void checkTutorial(const std::shared_ptr<Tutorial>& t);
  // CheckTutorialsByTrigger(type, object): in name order, until one is pushed: a tutorial whose trigger_type is `type` and whose
  // trigger_object is empty or `object` is checked.
  void checkTutorialsByTrigger(const std::string& type, const std::string& object);
  // PushTutorial: a TutorialWindow over the top modal, locking the level; with no modal, popped at once.
  void pushTutorial(const std::shared_ptr<Tutorial>& t);
  // PopTutorial: only for the active one: the lock ends and the states are written.
  void popTutorial(const std::shared_ptr<Tutorial>& t);
  // ResetTutorials: every tutorial back to its start (the windows of finished ones close), no active one.
  void resetTutorials();
  // HandleMessage and HandleMouse go to the active tutorial (false with none).
  bool handleMessage(const ui::Message& m);
  bool handleMouse(ui::Window* container, gfx::PointI screenPt);

  // The persistence of a tutorial's state (fix F4): each its own key, or with the fix off the one shared by all.
  int loadState(const std::string& tutorialName) const;
  void saveState(const std::string& tutorialName, int state);
  static std::string stateKey(const std::string& tutorialName, bool fixBugs) {
    return fixBugs ? "tutorial." + tutorialName : "tutorial.shared";
  }

  script::ScriptContext& context() const { return ctx_; }
  Progress& progress() { return progress_; }
  GameStateStore& gameStates() { return states_; }
  const Settings& settings() const { return settings_; }

 private:
  script::ScriptContext& ctx_;
  Progress& progress_;
  const Settings& settings_;
  GameStateStore& states_;
  ui::WindowId levelWindow_ = 0;
  int level_ = 0;
  std::map<std::string, std::shared_ptr<Tutorial>> tutorials_;
  std::shared_ptr<Tutorial> active_;
};

}  // namespace choco::games::greatcc
