#pragma once

#include "script/Bind.h"

namespace choco::script {

// Chocolatier 2's DiceGame window type and its natives (docs/design/choc2-minigames.md 2.5): RollFirst, RollSecond, StopOne,
// PlayerTotal and CharacterTotal, which act on the newest live DiceGame and do nothing (0 for the totals) without one.
// Called from registerChoc2Natives after the stub rows, so these replace the stubs of the same names.
void registerChoc2DiceNatives(lua_State* L, ScriptContext& ctx);

}  // namespace choco::script
