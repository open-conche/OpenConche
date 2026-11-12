#include "games/greatcc/natives/ScopedNatives.h"

#include <map>
#include <utility>

#include "script/LuaState.h"
#include "script/ScriptContext.h"

namespace choco::games::greatcc {

namespace {
// Which owner bound each scoped name last, per Lua state.
std::map<std::pair<lua_State*, std::string>, const ScopedNatives*>& owners() {
  static std::map<std::pair<lua_State*, std::string>, const ScopedNatives*> m;
  return m;
}
}  // namespace

ScopedNatives::ScopedNatives(script::ScriptContext& ctx) : L_(ctx.lua.L()), ctx_(ctx) {}

void ScopedNatives::bindRaw(const char* name, lua_CFunction fn) {
  script::rebindRaw(L_, ctx_, name, fn);
  names_.emplace_back(name);
  owners()[{L_, name}] = this;
}

void ScopedNatives::bindOwned(const char* name, lua_CFunction fn, ui::WindowId owner) {
  bindRaw(name, fn);  // the name bookkeeping, then the same global with the owner upvalue added
  lua_pushlightuserdata(L_, &ctx_);
  lua_pushstring(L_, name);
  lua_pushnumber(L_, static_cast<lua_Number>(owner));
  lua_pushcclosure(L_, fn, 3);
  lua_setglobal(L_, name);
}

ui::WindowId ScopedNatives::ownerOf(lua_State* L) {
  return lua_isnumber(L, lua_upvalueindex(3)) ? static_cast<ui::WindowId>(lua_tonumber(L, lua_upvalueindex(3))) : 0;
}

ScopedNatives::~ScopedNatives() {
  for (const std::string& n : names_) {
    const auto it = owners().find({L_, n});
    if (it == owners().end() || it->second != this) continue;  // a newer owner holds the name
    owners().erase(it);
    lua_pushnil(L_);
    lua_setglobal(L_, n.c_str());
  }
}

}  // namespace choco::games::greatcc
