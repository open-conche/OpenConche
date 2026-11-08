#pragma once

// Chocolatier 1's HiscoreWindow (THiscore, ctor 00414b30, the update at 00416150-area reads in
// docs/re/choc1-logo-and-hiscore.md) on top of the shared offline screen: the score column reads the string-table
// format `formatScore` ("%1%/week" — the game's score is the weekly production rate) with the number in the money
// format and capped at 999,999,999, and the gamemode label shows `empire_mode` / `free_mode` ("Story Mode" /
// "Free Play Mode"). The row name, the "%d." rank and the p1 icon behaviour are the shared window's.

#include "games/choc3/HiscoreWindow.h"
#include "script/ScriptContext.h"

namespace choco::game::choc1 {

class HiscoreWindow : public game::HiscoreWindow {
 public:
  HiscoreWindow(ui::WindowManager& wm, script::ScriptContext& ctx) : game::HiscoreWindow(wm, ctx) {}

 protected:
  std::string scoreTextOf(const sdk::HiscoreRow& row) const override;
  void updateText() override;
};

}  // namespace choco::game::choc1
