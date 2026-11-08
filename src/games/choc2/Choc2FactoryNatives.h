#pragma once

#include "script/Bind.h"

namespace choco::script {

// Chocolatier 2's factory mini-game (docs/design/choc2-minigames.md 2): the window types FactoryGame and FactoryStatus and the
// natives ResumeMinigame, PauseMinigame, ResetMinigame and GetProduction. The natives act on the newest live FactoryGame and
// do nothing without one (GetProduction gives 0). Called from registerChoc2Natives after the stub rows, so the real
// natives replace the stubs of the same names.
void registerChoc2FactoryNatives(lua_State* L, ScriptContext& ctx);

}  // namespace choco::script
