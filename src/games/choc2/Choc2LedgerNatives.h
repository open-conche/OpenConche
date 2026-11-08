#pragma once

#include "script/Bind.h"

namespace choco::script {

// Chocolatier 2's DynamicWindow and Ledger classes and the natives around them (docs/design/choc2-port-and-ledger.md 2.3):
// SetLedgerContents, UpdateLedgerContents, SetLedgerMode, ResetLedger, UpdateBadge, SetQuestHint, UpdateDynamicContent,
// bsgGetMaxScroll and bsgSetStartLine. Also makes the Session. Called from registerChoc2Natives after the stub rows, so the
// real natives replace the stubs of the same names.
void registerChoc2LedgerNatives(lua_State* L, ScriptContext& ctx);

}  // namespace choco::script
