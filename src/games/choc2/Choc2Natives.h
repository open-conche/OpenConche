#pragma once

// Chocolatier 2's natives: the rows of docs/re/api-choc2.tsv through
// generated/Stubs_choc2.inc, plus the few hand-written ones. Only a ScriptContext whose game is Choc2 gets them.

#include <cstddef>

#include <string>

#include "assets/Content.h"
#include "script/Bind.h"
#include "script/Stubs.h"  // AliasRow, registerAliasRows

namespace choco::script {

// bsgDate / bsgShortDate (_free/b.c 0x2091c, docs/re/choc2-ledger-binary.md 6): the game date of week `weeks` plus `days`
// extra days. The 52 weeks of a year are cut into 12 months; the day is (weeks into the month) * 7 + days, 0 becoming 1 and
// anything past 28 clamped to 28; the year starts at 1925. Long: the string table's formatDate with the month name
// ("January 1, 1925"); short: "D Mon YYYY" with the month cut to three characters ("1 Jan 1925"). Negative or
// fractional arguments truncate to unsigned like the original.
std::string choc2Date(const assets::StringTable& strings, double weeks, double days, bool shortForm);

// Every function name in docs/re/api-choc2.tsv (the generated API_NAME rows): after boot each must be a Lua function, as a
// stub, an alias or a hand-written native.
const char* const* choc2ApiNames(std::size_t& count);

// Everything for the Choc2 game, in the order the design gives (section 2.2): the stub rows, the hand-written natives, the
// aliases and the window classes. Must run after every Chocolatier 3 native is registered.
void registerChoc2Natives(lua_State* L, ScriptContext& ctx);
// The Choc2-only engine globals (the six screen globals of the TSV's `global` rows). Boot step 6.
void registerChoc2Globals(lua_State* L);
// The game's WINDOW_TYPE rows (placeholders for the classes that do not exist yet; real factories registered earlier win).
void registerChoc2WindowTypes(lua_State* L, ScriptContext& ctx);

}  // namespace choco::script
