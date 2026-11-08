#pragma once

#include "script/LuaApi.h"

namespace choco::script {

// Call right after luaopen_string. The game's luaopen_string ends with lua_setdefaultmetatable(L, LUA_TSTRING) on the
// `string` library table itself (docs/re/embedded-lua.md section 7): strings index into the library, so `s:len()` and
// `("x").len` work and `("x").foo` is nil. Leaves the stack as it found it.
inline void installGameStringMeta(lua_State* L) {
  lua_getglobal(L, "string");
  lua_setdefaultmetatable(L, LUA_TSTRING);
}

}  // namespace choco::script
