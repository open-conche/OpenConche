#pragma once

// Chocolatier 1's natives (docs/re/choc1-fit.md): the rows of docs/re/api-choc1.tsv through generated/Stubs_choc1.inc, plus the
// few hand-written ones. Bound after Chocolatier 3's, so a Chocolatier 1 name overrides a Chocolatier 3 one.

#include <cstddef>

#include <string>

#include "assets/Content.h"
#include "script/Bind.h"
#include "script/Stubs.h"

namespace choco::script {

// UtilDollars (docs/re/choc1-fit.md section 2): `amount` truncated toward zero to 64 bits, its low 32 bits taken as unsigned (a
// negative amount wraps to about 4.29e9), split into billions, millions, thousands and ones, and formatted with the first
// non-zero of the strings formatBillions / formatMillions / formatThousands, else formatOnes. Those hold printf slots written
// with doubled percents (`$%%d,%%03d`); the string table's lookup turns `%%` into `%` (inference: that is the formatter's
// meaning), so 1234 gives `$1,234`. A missing key falls back to the same English formats.
std::string choc1Dollars(const assets::StringTable& strings, double amount);

// Every function name the table binds (the generated API_NAME rows): after boot each must be a Lua function, as a stub, an
// alias, an SDK native or a hand-written native.
const char* const* choc1ApiNames(std::size_t& count);

// The stub rows, the hand-written natives and the aliases. Must run after every Chocolatier 3 native is registered.
void registerChoc1Natives(lua_State* L, ScriptContext& ctx);
// The WINDOW_TYPE rows as placeholder types, only for names no earlier registration has (Chocolatier 3's HiscoreWindow,
// CreditsWindow, XSellWindow ... stay as they are).
void registerChoc1WindowTypes(lua_State* L, ScriptContext& ctx);

}  // namespace choco::script
