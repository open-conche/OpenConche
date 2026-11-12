#pragma once

// RTransitionManager's Lua native (docs/re/greatcc-ui-library.md 3.1): Lua_DoTransition(type). The Lua text DoTransition that
// wraps it is defined by registerGreatccGlobals.

#include "script/Bind.h"

namespace choco::games::greatcc {

// Binds Lua_DoTransition over its stub. It makes the transition for a number 1001..1011, suspends the calling coroutine
// until the transition's overlay finishes, and returns true then; any other argument returns false at once.
void bindTransitionNatives(lua_State* L, script::ScriptContext& ctx);

}  // namespace choco::games::greatcc
