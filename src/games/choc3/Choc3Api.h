#pragma once

#include <cstddef>
#include <string_view>

#include "script/Stubs.h"

namespace choco::script {

// Chocolatier 3's API tables (generated/Stubs_choc3.inc): every function and window type the game's Lua names.
const StubRow* stubRows(std::size_t& count);
const char* const* stubWindowTypes(std::size_t& count);
// True for any function name in the API tables or defined by the prelude/host (used to validate real native names).
bool isApiName(std::string_view name);

// Registers every stub as a global. Each logs "[unimplemented] Name" once and counts its calls in ctx.stubs.
void registerStubs(lua_State* L, ScriptContext& ctx);

}  // namespace choco::script
