#pragma once

// The Great Chocolate Chase's per-context state (docs/design/greatcc.md 3.2). It owns what the original keeps as singletons:
// the settings, the player's progress and the managers built on it, and the transition manager. It dies before the window
// tree, so nothing in a window or sprite stores a pointer into it: windows fetch worldOf(ctx) per call.

#include <map>

#include "games/greatcc/audio/GreatCCSound.h"
#include "games/greatcc/core/AchievementManager.h"
#include "games/greatcc/core/GameStateStore.h"
#include "games/greatcc/core/LevelManager.h"
#include "games/greatcc/core/Progress.h"
#include "games/greatcc/core/QuestManager.h"
#include "games/greatcc/core/Settings.h"
#include "games/greatcc/core/UpgradeManager.h"
#include "games/greatcc/core/ValueManager.h"
#include "games/greatcc/meta/PlotManager.h"
#include "games/greatcc/meta/RewardNotifications.h"
#include "games/greatcc/tutorial/TutorialManager.h"
#include "gfx/Types.h"
#include "games/greatcc/ui/Transitions.h"
#include "script/GameModule.h"
#include "script/ScriptContext.h"

namespace choco::games::greatcc {

class GreatCCWorld : public script::GameState {
 public:
  explicit GreatCCWorld(script::ScriptContext& ctx);
  ~GreatCCWorld() override;
  void tick(double nowMs) override;
  // The audio focus pause (everything, music included, as TSoundManager::AppActivate).
  void setForeground(bool foreground, double nowMs) override;
  void afterAudio(audio::Audio&) override { sound_.update(lastNowMs_); }

  const Settings& settings() const { return settings_; }
  Settings& settings() { return settings_; }
  TransitionManager& transitions() { return transitions_; }
  GreatCCSound& sound() { return sound_; }
  const Progress& progress() const { return progress_; }
  Progress& progress() { return progress_; }
  ValueManager& values() { return values_; }
  LevelManager& levels() { return levels_; }
  AchievementManager& achievements() { return achievements_; }
  // TGameState: the events the stations raise and the tutorials mark (the Lua `SetState` writes it too).
  GameStateStore& gameStates() { return gameStates_; }
  // VTutorialManager: the tutorials of the shop screen and the Tutorials option.
  TutorialManager& tutorials() { return tutorials_; }
  const TutorialManager& tutorials() const { return tutorials_; }

  // mChocTint: the console's `choctint` colour per layer type (the original's global). A layer added to a chocolate afterwards takes
  // the colour of its type.
  void setChocolateTint(int layerType, const gfx::ColorF& c) { chocTint_[layerType] = c; }
  const gfx::ColorF* chocolateTint(int layerType) const {
    const auto it = chocTint_.find(layerType);
    return it == chocTint_.end() ? nullptr : &it->second;
  }

  // Whether the application has the foreground (as far as setForeground told).
  bool foreground() const { return foreground_; }
  // The main loop's pause rule (docs/re/greatcc-boot-and-natives.md 1.2): inside a game, InsideGame(false) and the pause dialog is
  // queued on Main's coroutine. Escape and losing the foreground do this; so does InsideGame(true) while not in the foreground.
  void pauseInsideGame();
  // VUpgradeManager: the upgrades of the location being played.
  UpgradeManager& upgrades() { return upgrades_; }
  const UpgradeManager& upgrades() const { return upgrades_; }
  // VUpgradeManager::IsActive(name, category).
  bool upgradeActive(const std::string& name, const std::string& category) const { return upgrades_.isActive(name, category); }
  // VQuestManager: the quests of the story levels; VPlotManager: the diary pages of the location being played.
  QuestManager& quests() { return quests_; }
  PlotManager& plots() { return plots_; }
  // The achievement notification windows (VAchievementManager::ShowNotification and its flags).
  RewardNotifications& notices() { return notices_; }
  // The window of the shop screen while one exists (0 otherwise): an id, never a pointer (design 3.2).
  ui::WindowId shopWindowId() const { return shopWindow_; }
  void setShopWindowId(ui::WindowId id) { shopWindow_ = id; }

  // Boot step 4 (the user store exists by then): attaches the store, reads the data files (values.xml, levels/goals.xml,
  // achievements.xml), the global `gamemode` pref, and loads the current user's state.
  void init(script::ScriptContext& ctx);
  // LoadStateForCurrentUser: every manager is rebuilt from the current user's progress file (defaults with no user).
  void reloadState(script::ScriptContext& ctx);

  // The user natives. Each writes the outgoing user's pending changes first and reloads afterwards.
  void createUser(script::ScriptContext& ctx, const std::string& name);
  void setCurrentUser(script::ScriptContext& ctx, int index);
  void deleteUser(script::ScriptContext& ctx, int index);
  // ChangeCurrentUserName (TSettings): the user's best row of each of the four tables is logged again under the new name (the old
  // rows stay), then the name changes. Nothing happens when the store refuses the name.
  void renameCurrentUser(script::ScriptContext& ctx, const std::string& name);
  // TPfHiscores::LogScore for the current user in the current game mode's table (`a` is the game data string, `b` the medals).
  void logScore(script::ScriptContext& ctx, long long score, const std::string& a, const std::string& b);
  // SetCurrentGameMode: also the global `gamemode` pref.
  void setGameMode(script::ScriptContext& ctx, int mode);
  // Writes the current mode to the global pref (after SetCurrentDifficulty moved it).
  void persistGameMode(script::ScriptContext& ctx);

 private:
  script::ScriptContext& ctx_;
  double lastNowMs_ = 0;
  bool foreground_ = true;
  std::map<int, gfx::ColorF> chocTint_;
  ui::WindowId shopWindow_ = 0;
  Settings settings_;
  // Progress first: the managers keep a reference to it (and to the value manager).
  Progress progress_;
  ValueManager values_;
  UpgradeManager upgrades_;
  QuestManager quests_;
  PlotManager plots_;
  LevelManager levels_;
  AchievementManager achievements_;
  RewardNotifications notices_;
  GameStateStore gameStates_;
  TutorialManager tutorials_;
  TransitionManager transitions_;
  GreatCCSound sound_;
};

// The world of `ctx`, or null when the context has none (or another game's).
// IsEnabled(kCheatMode): the `cheatmode` setting is non-zero (--cheatmode or --config cheatmode=1). The game's Cheats buttons are
// enabled by it; the cheat natives refuse without it.
bool cheatModeOn(script::ScriptContext& ctx);

GreatCCWorld* worldOf(script::ScriptContext& ctx);

}  // namespace choco::games::greatcc
