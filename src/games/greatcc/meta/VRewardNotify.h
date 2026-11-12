#pragma once

// VRewardNotify (docs/re/greatcc-meta-flow.md 6): the "New Achievements" box at the top right of the screen. Built by
// scripts/rewardnotify.lua, it gets one item (an icon, the title and the description) per achievement just earned, up to six, stacked
// under each other on a nine-slice background that slides in from the right edge, waits and slides out again. How long it waits is
// decided by the close condition it is given: 0 waits until HideNotification, anything else is a time in milliseconds.

#include <memory>
#include <string>
#include <vector>

#include "games/greatcc/scene/TiledObject.h"
#include "games/greatcc/scene/VSpriteWindow.h"

namespace choco::games::greatcc {

class VRewardNotify : public VSpriteWindow {
 public:
  VRewardNotify(ui::WindowManager& wm, script::ScriptContext& ctx) : VSpriteWindow(wm, ctx) {}
  std::string_view typeName() const override { return "VRewardNotify"; }
  // The keys `transition` (300 ms), `duration` (5000 ms) and `spacing` (0); the `background` table makes the box (initialised and
  // added) and the `item` table the prototype every item is copied from.
  void postChildrenInit(const ui::Style& style) override;
  // AddAchievements: an item per name (the ones the table knows), at most six: a copy of the prototype put under the one before
  // (`spacing` apart), its title and description set, sized to its text (at least 55 high), its icon switched to the achievement and
  // centred on it; then the box and the window take the height of them all.
  void addAchievements(const std::vector<std::string>& names);
  // SetCloseCondition(n, flag): the hold time becomes `n` ms; `flag` (null for a timed one) is the switch HideNotification sets.
  void setCloseCondition(unsigned n, std::shared_ptr<bool> flag);
  // The window is in its modal: the phases are timed from now.
  void start();
  // OnTaskAnimate: slide in (phase 0), hold (1), slide out (2), then the window closes. The slides move the box by
  // (time since the phase began) x width / transition each frame, so they speed up as they go.
  bool onAnimate() override;
  std::string detail() const override;

  // Close: the achievement manager forgets the window and the window is destroyed.
  void close();
  int phase() const { return phase_; }
  int itemCount() const { return items_; }
  const std::shared_ptr<TiledObject>& background() const { return background_; }
  unsigned holdMs() const { return duration_; }

 private:
  std::shared_ptr<TiledObject> background_, item_;
  std::shared_ptr<bool> closeFlag_;
  float transition_ = 300, spacing_ = 0;
  unsigned duration_ = 5000;
  int phase_ = 0, items_ = 0;
  double phaseStart_ = 0;
  bool started_ = false, closed_ = false;
};

}  // namespace choco::games::greatcc
