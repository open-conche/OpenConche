#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/natives/StateNatives.h"

namespace choco::games::greatcc {

namespace {

// _SetState(key, value): TGameState::SetState. The Lua `SetState("MainMenuScreenTime", 1)` and the like start and stop analytics timers
// in the original; the store only keeps the strings (docs/re/greatcc-boot-and-natives.md 1.6).
void nativeSetState(script::ScriptContext& ctx, std::string key, std::string value) {
  if (GreatCCWorld* w = worldOf(ctx)) w->gameStates().set(key, value);
}

}  // namespace

void bindGameStateNatives(lua_State* L, script::ScriptContext& ctx) { detail::rebind<nativeSetState>(L, ctx, "_SetState"); }

}  // namespace choco::games::greatcc
