#include "script/Stubs.h"

#include <algorithm>
#include <string>
#include <vector>

namespace choco::script {

namespace {

int stubFunction(lua_State* L) {
  ScriptContext& ctx = contextOf(L);
  const char* name = nativeNameOf(L);
  const StubRow& row = *static_cast<const StubRow*>(lua_touserdata(L, lua_upvalueindex(3)));
  if (ctx.stubs.hit(name)) ctx.log.write(std::string("[unimplemented] ") + name);
  switch (row.ret) {
    case Ret::None: return 0;
    case Ret::Nil: lua_pushnil(L); return 1;
    case Ret::String: lua_pushstring(L, ""); return 1;
    case Ret::Number: lua_pushnumber(L, 0); return 1;
    case Ret::Bool: lua_pushboolean(L, 0); return 1;
    case Ret::Table: lua_newtable(L); return 1;
  }
  return 0;
}

}  // namespace

void registerStubRows(lua_State* L, ScriptContext& ctx, const StubRow* rows, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    lua_pushlightuserdata(L, &ctx);
    lua_pushstring(L, rows[i].name);
    lua_pushlightuserdata(L, const_cast<StubRow*>(&rows[i]));
    lua_pushcclosure(L, stubFunction, 3);
    lua_setglobal(L, rows[i].name);
  }
}

std::size_t registerAliasRows(lua_State* L, ScriptContext& ctx, const AliasRow* rows, std::size_t count) {
  // The fallback stubs (the same shape as a generated STUB row) live as long as the process: a StubRow's address is a
  // closure upvalue.
  static std::vector<std::vector<StubRow>> keep;
  std::size_t bound = 0;
  std::vector<StubRow> fallback;
  for (std::size_t i = 0; i < count; ++i) {
    const bool real = std::find(ctx.nativeNames.begin(), ctx.nativeNames.end(), rows[i].target) != ctx.nativeNames.end();
    lua_CFunction fn = nullptr;
    if (real) {
      lua_getglobal(L, rows[i].target);
      fn = lua_tocfunction(L, -1);
      lua_pop(L, 1);
    }
    if (fn) {
      bindRaw(L, ctx, rows[i].name, fn);
      ++bound;
    } else {
      fallback.push_back({rows[i].name, Ret::Nil, ""});
    }
  }
  if (!fallback.empty()) {
    keep.push_back(std::move(fallback));
    registerStubRows(L, ctx, keep.back().data(), keep.back().size());
  }
  return bound;
}

}  // namespace choco::script
