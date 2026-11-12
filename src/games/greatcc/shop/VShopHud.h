#pragma once

// VShopHud (docs/re/greatcc-shop-simulation.md 8): the shop's HUD window of scripts/hud.lua. It finds its gauges by name and
// follows the level: the clock (RTimer) and its sweep, the goal and mastery markers with the needle, and in an endless mode the
// anger count. The shop window drives it: levelInit at each level load, updateHud on every tick.

#include "games/greatcc/core/LevelData.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::games::greatcc {

class BLayeredWindow;
class KDial;
class KSweepDial;
class RTimer;

class VShopHud : public ui::Window {
 public:
  VShopHud(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), ctx_(ctx) {}
  std::string_view typeName() const override { return "VShopHud"; }
  // Finds progress_gauge, dialPower, game_timer, timer_sweep, goal_score, mastery_score, anger_score and level_name; in an
  // endless mode the gauge shows layer 2, level_name is hidden and the timer moves up 10 px.
  void postChildrenInit(const ui::Style& style) override;
  std::string detail() const override;

  // LevelInit: the timer is reset (counting up when `endless`, else limited to the level's length), the sweep is full and the
  // needle at 0; story: the goal and mastery texts and the extension window hidden (it holds the Upgrade button); endless: the
  // anger text; the level name text.
  void levelInit(const LevelData* level, bool endless);
  // UpdateHud: the timer runs unless the sprites are soft-paused or a tutorial is up; the sweep follows the remaining time;
  // the gauge layer and needle follow the score (story) or the anger (endless).
  void updateHud(bool softPaused, bool tutorialInEffect, bool endless);

  RTimer* timer() const { return timer_; }
  KDial* needle() const { return needle_; }
  KSweepDial* sweep() const { return sweep_; }
  BLayeredWindow* gauge() const { return gauge_; }

 private:
  script::ScriptContext& ctx_;
  BLayeredWindow* gauge_ = nullptr;
  KDial* needle_ = nullptr;
  RTimer* timer_ = nullptr;
  KSweepDial* sweep_ = nullptr;
  ui::Window* goalText_ = nullptr;
  ui::Window* masteryText_ = nullptr;
  ui::Window* angerText_ = nullptr;
  ui::Window* nameText_ = nullptr;
  int levelMs_ = 0;  // the limit cached at +0x50
};

}  // namespace choco::games::greatcc
