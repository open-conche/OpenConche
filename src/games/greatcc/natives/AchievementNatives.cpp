#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/natives/StateNatives.h"

namespace choco::games::greatcc {

namespace {

using script::ScriptContext;

// CheckAllAchievements(category, show, closeCondition): the check; newly acquired ones wait in the queue, and with `show` the
// notification window comes up at once.
void nativeCheckAllAchievements(ScriptContext& ctx, std::string category, bool show, double closeCondition) {
  GreatCCWorld* w = worldOf(ctx);
  if (!w) return;
  w->achievements().checkAll(category);
  if (show && !w->achievements().pending().empty()) w->notices().showNew(static_cast<unsigned>(closeCondition));
}

// ShowNewAchievements(closeCondition): the queue is shown in a notification over the top modal (0 holds it until HideNotification).
void nativeShowNewAchievements(ScriptContext& ctx, double closeCondition) {
  if (GreatCCWorld* w = worldOf(ctx)) w->notices().showNew(static_cast<unsigned>(closeCondition));
}

// HideNotification(): the notifications that wait for it slide away.
void nativeHideNotification(ScriptContext& ctx) {
  if (GreatCCWorld* w = worldOf(ctx)) w->notices().hide();
}

}  // namespace

void bindAchievementNatives(lua_State* L, script::ScriptContext& ctx) {
  detail::rebind<nativeCheckAllAchievements>(L, ctx, "CheckAllAchievements");
  detail::rebind<nativeShowNewAchievements>(L, ctx, "ShowNewAchievements");
  detail::rebind<nativeHideNotification>(L, ctx, "HideNotification");
}

}  // namespace choco::games::greatcc
