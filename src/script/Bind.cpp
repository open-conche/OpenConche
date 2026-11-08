#include "script/Bind.h"

#include <string>

namespace choco::script {

ScriptContext& contextOf(lua_State* L) {
  return *static_cast<ScriptContext*>(lua_touserdata(L, lua_upvalueindex(1)));
}

const char* nativeNameOf(lua_State* L) { return lua_tostring(L, lua_upvalueindex(2)); }

void bindRaw(lua_State* L, ScriptContext& ctx, const char* name, lua_CFunction fn) {
  lua_pushlightuserdata(L, &ctx);
  lua_pushstring(L, name);
  lua_pushcclosure(L, fn, 2);
  lua_setglobal(L, name);
  ctx.nativeNames.emplace_back(name);
}

// A later binding of a name another module already bound (Chocolatier 2's StartFireworks over Chocolatier 3's): the
// global is replaced, the native-name list keeps its single entry so the boot trace stays stable.
void rebindRaw(lua_State* L, ScriptContext& ctx, const char* name, lua_CFunction fn) {
  lua_pushlightuserdata(L, &ctx);
  lua_pushstring(L, name);
  lua_pushcclosure(L, fn, 2);
  lua_setglobal(L, name);
  if (std::find(ctx.nativeNames.begin(), ctx.nativeNames.end(), name) == ctx.nativeNames.end())
    ctx.nativeNames.emplace_back(name);
}

namespace {
// "file:line" of the Lua function that called the native, or "?".
std::string callerWhere(lua_State* L) {
  lua_Debug ar;
  if (lua_getstack(L, 1, &ar) && lua_getinfo(L, "Sl", &ar) && ar.currentline > 0) {
    return std::string(ar.short_src) + ":" + std::to_string(ar.currentline);
  }
  return "?";
}
}  // namespace

namespace detail {

void warnArg(CallCtx& c, int idx, const char* expected) {
  const char* got = lua_typename(c.L, lua_type(c.L, idx));
  std::string key = std::string(c.name) + "#" + std::to_string(idx) + "#" + got;
  if (!c.ctx->warned.insert(key).second) return;
  c.ctx->log.write(std::string("[lua-arg] ") + c.name + " arg " + std::to_string(idx) + ": expected " + expected +
                   ", got " + got + " (" + callerWhere(c.L) + ")");
}

void logNativeError(lua_State* L, ScriptContext& ctx, const char* name, const char* what) {
  (void)L;
  ctx.log.write(std::string("[native-error] ") + name + ": " + what);
}

}  // namespace detail

}  // namespace choco::script
