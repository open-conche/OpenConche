// LoadIngredients, LoadProducts, LoadPortFileList and LoadQuestFileList: Chocolatier 3's content loaders, bound through
// GameModule::registerContentExtras.
#include <string>

#include "games/choc3/Choc3Natives.h"
#include "games/choc3/ContentLoaders.h"
#include "script/Bind.h"
#include "script/ScriptHost.h"

namespace choco::script {

namespace {

// Raw natives: they call back into Lua with the native's own state.

void logWarnings(ScriptContext& ctx, const assets::Warnings& w) {
  for (const auto& line : w) ctx.log.write("[content] " + line);
}

// Calls the Lua global `fn` with one table of string values, in file order (SimpleXmlToLua: every value is a string).
void callWithTable(lua_State* L, ScriptContext& ctx, const char* fn, const assets::KeyValues& kv) {
  const int top = lua_gettop(L);
  lua_getglobal(L, fn);
  if (!lua_isfunction(L, -1)) {
    if (ctx.warned.insert(std::string("content#") + fn).second)
      ctx.log.write(std::string("[content] ") + fn + " is not defined; its data is skipped");
    lua_settop(L, top);
    return;
  }
  lua_newtable(L);
  for (const auto& [k, v] : kv) {
    lua_pushlstring(L, k.data(), k.size());
    lua_pushlstring(L, v.data(), v.size());
    lua_rawset(L, -3);
  }
  if (lua_pcall(L, 1, 0, 0) != 0) {
    if (ctx.host) ctx.host->logLuaError(L);
    else ctx.log.write(std::string("[lua-error] ") + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "?"));
  }
  lua_settop(L, top);
}

int nativeLoadIngredients(lua_State* L) {
  ScriptContext& ctx = contextOf(L);
  assets::Warnings w;
  auto items = assets::loadIngredients(ctx.fs, &w);
  logWarnings(ctx, w);
  for (const auto& kv : items) callWithTable(L, ctx, "CreateIngredient", kv);
  ctx.log.write("[content] LoadIngredients: " + std::to_string(items.size()) + " ingredients");
  return 0;
}

int nativeLoadProducts(lua_State* L) {
  ScriptContext& ctx = contextOf(L);
  assets::Warnings w;
  auto items = assets::loadProducts(ctx.fs, &w);
  logWarnings(ctx, w);
  for (const auto& kv : items) callWithTable(L, ctx, "CreateProductFromXML", kv);
  ctx.log.write("[content] LoadProducts: " + std::to_string(items.size()) + " products");
  return 0;
}

// Fills the table argument with 1-based paths.
int fillFileList(lua_State* L, std::vector<std::string> (*load)(const assets::AssetFS&, assets::Warnings*)) {
  ScriptContext& ctx = contextOf(L);
  assets::Warnings w;
  auto files = load(ctx.fs, &w);
  logWarnings(ctx, w);
  if (!lua_istable(L, 1)) {
    if (ctx.warned.insert(std::string(nativeNameOf(L)) + "#arg").second)
      ctx.log.write(std::string("[lua-arg] ") + nativeNameOf(L) + " arg 1: expected table, got " +
                    lua_typename(L, lua_type(L, 1)));
    return 0;
  }
  int n = 0;
  for (const auto& f : files) {
    lua_pushnumber(L, ++n);
    lua_pushlstring(L, f.data(), f.size());
    lua_rawset(L, 1);
  }
  luaL_setn(L, 1, n);
  ctx.log.write(std::string("[content] ") + nativeNameOf(L) + ": " + std::to_string(n) + " files");
  return 0;
}

int nativeLoadPortFileList(lua_State* L) { return fillFileList(L, assets::loadPortFileList); }
int nativeLoadQuestFileList(lua_State* L) { return fillFileList(L, assets::loadQuestFileList); }

template <lua_CFunction Fn>
int guarded(lua_State* L) {
  try {
    return Fn(L);
  } catch (const std::exception& e) {
    detail::logNativeError(L, contextOf(L), nativeNameOf(L), e.what());
  } catch (...) {
    detail::logNativeError(L, contextOf(L), nativeNameOf(L), "unknown exception");
  }
  return 0;
}

}  // namespace

void registerContentLoaderNatives(lua_State* L, ScriptContext& ctx) {
  bindRaw(L, ctx, "LoadIngredients", &guarded<nativeLoadIngredients>);
  bindRaw(L, ctx, "LoadProducts", &guarded<nativeLoadProducts>);
  bindRaw(L, ctx, "LoadPortFileList", &guarded<nativeLoadPortFileList>);
  bindRaw(L, ctx, "LoadQuestFileList", &guarded<nativeLoadQuestFileList>);
}

}  // namespace choco::script
