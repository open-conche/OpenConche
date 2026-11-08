#pragma once

#include "script/Bind.h"

namespace choco::script {

// Chocolatier 2's BonusGame window type (docs/design/choc2-minigames.md 2.6): a CratesGame built from CratesSpec::bonus()
// (25 crates, large_crate art). PrepareGame and SelectPrize are the shared Chocolatier 3 natives and act on it as on a
// CratesGame. Called from registerChoc2Natives after the stub rows, so this replaces the placeholder type.
void registerChoc2BonusNatives(lua_State* L, ScriptContext& ctx);

}  // namespace choco::script
