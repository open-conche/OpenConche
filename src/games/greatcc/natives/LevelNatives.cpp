#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/natives/StateNatives.h"

namespace choco::games::greatcc {

namespace {

using script::ScriptContext;

double nativeGetGoalScore(ScriptContext& ctx, int level) {
  GreatCCWorld* w = worldOf(ctx);
  return w ? w->levels().goal(level) : 0;
}
double nativeGetMasteryScore(ScriptContext& ctx, int level) {
  GreatCCWorld* w = worldOf(ctx);
  return w ? w->levels().mastery(level) : 0;
}
double nativeGetCurrentResult(ScriptContext& ctx) {
  GreatCCWorld* w = worldOf(ctx);
  return w ? w->levels().currentResult() : 0;
}
double nativeGetHighestComplete(ScriptContext& ctx) {
  GreatCCWorld* w = worldOf(ctx);
  return w ? w->levels().highestComplete() : 0;
}
double nativeGetCurrentLevel(ScriptContext& ctx) {
  GreatCCWorld* w = worldOf(ctx);
  return w ? w->levels().currentLevel() : 0;
}
void nativeSetCurrentLevel(ScriptContext& ctx, int level) {
  if (GreatCCWorld* w = worldOf(ctx)) w->levels().setCurrentLevel(level);
}

}  // namespace

void bindLevelNatives(lua_State* L, script::ScriptContext& ctx) {
  using detail::rebind;
  rebind<nativeGetMasteryScore>(L, ctx, "GetMasteryScore");
  rebind<nativeGetGoalScore>(L, ctx, "GetGoalScore");
  rebind<nativeGetCurrentResult>(L, ctx, "GetCurrentResult");
  rebind<nativeGetHighestComplete>(L, ctx, "GetHighestComplete");
  rebind<nativeGetCurrentLevel>(L, ctx, "GetCurrentLevel");
  rebind<nativeSetCurrentLevel>(L, ctx, "SetCurrentLevel");
}

}  // namespace choco::games::greatcc
