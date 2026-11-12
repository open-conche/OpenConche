#include "games/greatcc/meta/RewardNotifications.h"

#include <algorithm>

#include "games/greatcc/meta/VRewardNotify.h"
#include "games/greatcc/ui/RWindow.h"
#include "script/RunFile.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

int stackedNotificationTop(const gfx::RectI& rect, const std::vector<gfx::RectI>& visible) {
  int left = rect.x, top = rect.y, right = rect.x + rect.w, bottom = rect.y + rect.h;
  for (const gfx::RectI& o : visible) {
    const int ol = o.x, ot = o.y, orr = o.x + o.w, ob = o.y + o.h;
    if (ol <= right && ot <= bottom && left <= orr && top <= ob) {
      top += ob - ot;
      bottom += ob - ot;
    }
  }
  return top;
}

bool RewardNotifications::show(const std::vector<std::string>& names, unsigned closeCondition) {
  if (names.empty()) return false;
  ui::ModalWindow* top = ctx_.wm.topModal();
  if (!top) return false;
  // scripts/rewardnotify.lua is run into a window of its own; its `reward_notify` is taken out of it
  auto holder = std::make_unique<RWindow>(ctx_.wm);
  script::runFileInto(ctx_, ctx_.lua.L(), *holder, "scripts/rewardnotify.lua");
  auto* notify = dynamic_cast<VRewardNotify*>(holder->find("reward_notify"));
  if (!notify) {
    ctx_.log.write("[greatcc] scripts/rewardnotify.lua made no reward_notify");
    return false;
  }
  std::unique_ptr<ui::Window> owned = notify->parent()->orphan(*notify);
  notify->addAchievements(names);
  // the notifications still up that it would overlap push it down (a window that went with its modal is forgotten)
  std::erase_if(visible_, [&](ui::WindowId id) { return ctx_.wm.window(id) == nullptr; });
  std::vector<gfx::RectI> others;
  for (ui::WindowId id : visible_)
    if (ui::Window* w = ctx_.wm.window(id)) others.push_back(w->rect());
  const gfx::RectI r = notify->rect();
  notify->setPos(r.x, stackedNotificationTop(r, others));
  top->adoptFront(std::move(owned));
  visible_.push_back(notify->id());
  std::shared_ptr<bool> flag;
  if (closeCondition == 0) {
    flag = std::make_shared<bool>(false);
    flags_.push_back(flag);
  }
  notify->setCloseCondition(closeCondition, flag);
  notify->start();
  return true;
}

bool RewardNotifications::showNew(unsigned closeCondition) {
  const std::vector<std::string> names = achievements_.takePending();
  return show(names, closeCondition);
}

void RewardNotifications::hide() {
  for (auto& f : flags_)
    if (f) *f = true;
  flags_.clear();
}

void RewardNotifications::notifyComplete(ui::WindowId id) { std::erase(visible_, id); }

}  // namespace choco::games::greatcc
