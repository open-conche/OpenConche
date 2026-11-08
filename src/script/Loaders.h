#pragma once

#include <string_view>

#include "script/Bind.h"

namespace choco::script {

// Reads `name` through AssetFS and loads it as a chunk named "@name" (text or Lua 5.0 bytecode). Returns 0 and pushes
// the function, or a Lua error status and pushes the message ("cannot open <name>" when the file is missing, status
// LUA_ERRFILE).
int loadChunk(lua_State* L, ScriptContext& ctx, std::string_view name);

// Loads an in-memory chunk with the given chunk name (without the '@').
int loadBuffer(lua_State* L, std::string_view code, const char* chunkName);

// Replaces dofile, loadfile and require with versions that read through AssetFS (script-host design section 5).
// require is the 5.0.2 algorithm: _LOADED caching, LUA_PATH templates split on ';' with '?' substituted, _REQUIREDNAME,
// and no environment variable fallback.
void registerLoaders(lua_State* L, ScriptContext& ctx);

// Natives that log: _ALERT ("[lua-error] ...") and DebugOut ("[debug] ..." with every argument stringified).
void registerLogging(lua_State* L, ScriptContext& ctx);

}  // namespace choco::script
