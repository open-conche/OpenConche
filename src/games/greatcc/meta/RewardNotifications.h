#pragma once

// The notifications part of VAchievementManager (docs/re/greatcc-boot-and-natives.md 6.2): ShowNewAchievements, ShowNotification,
// HideNotification and NotifyComplete. The windows on screen are known by id and the "hide" switches by the flags they wait on.

#include <memory>
#include <string>
#include <vector>

#include "games/greatcc/core/AchievementManager.h"
#include "gfx/Types.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::games::greatcc {

// Where a new notification goes: its rectangle (in left, top, right, bottom terms of the original's TRect) is pushed down by the
// height of every visible notification it overlaps (the edges count as overlapping), one after the other in list order, and the
// pushes add up. Returns the new top.
int stackedNotificationTop(const gfx::RectI& rect, const std::vector<gfx::RectI>& visible);

class RewardNotifications {
 public:
  RewardNotifications(script::ScriptContext& ctx, AchievementManager& achievements) : ctx_(ctx), achievements_(achievements) {}

  // ShowNotification(list, closeCondition): nothing for an empty list. A window is built from scripts/rewardnotify.lua, given the
  // achievements, placed under the notifications already up, added to the top modal and remembered; for closeCondition 0 a
  // switch is made that HideNotification sets, else the box waits that many milliseconds. True when a window was shown.
  bool show(const std::vector<std::string>& names, unsigned closeCondition);
  // ShowNewAchievements(closeCondition): the achievements acquired and not yet shown, then they are forgotten.
  bool showNew(unsigned closeCondition);
  // HideNotification: every pending switch is set and forgotten.
  void hide();
  // NotifyComplete(window).
  void notifyComplete(ui::WindowId id);
  const std::vector<ui::WindowId>& visible() const { return visible_; }
  std::size_t pendingFlags() const { return flags_.size(); }

 private:
  script::ScriptContext& ctx_;
  AchievementManager& achievements_;
  std::vector<ui::WindowId> visible_;
  std::vector<std::shared_ptr<bool>> flags_;
};

}  // namespace choco::games::greatcc
