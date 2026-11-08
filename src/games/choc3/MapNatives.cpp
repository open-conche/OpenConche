#include "games/common/MapWindow.h"
#include "games/common/World.h"
#include "script/LuaStyle.h"
#include "script/ScriptHost.h"
#include "games/choc3/Choc3Natives.h"

namespace choco::script {

namespace {

// InitiateTravel{ path, type, events }: copies the table into a travel on the map and runs no Lua. Without a map it logs
// and does nothing (the original dereferenced a null pointer).
void nativeInitiateTravel(ScriptContext& ctx, TableArg t) {
  game::MapWindow* map = (game::worldOf(ctx) ? game::worldOf(ctx)->map() : nullptr);
  if (!map) {
    ctx.log.write("[map] InitiateTravel: there is no map window");
    return;
  }
  if (!t.valid()) return;
  const LuaStyle style(t.L, t.index, &ctx.lua);
  const gfx::PointF from = map->currentPort() ? map->currentPort()->pos : gfx::PointF{};
  if (!map->initiateTravel(game::parseTravelRequest(style, from), ctx.wm.nowMs()))
    ctx.log.write("[map] InitiateTravel ignored: a travel already exists on this map");
}

void nativePauseTravel(ScriptContext& ctx) {
  if (game::MapWindow* map = (game::worldOf(ctx) ? game::worldOf(ctx)->map() : nullptr)) map->pauseTravel();
}

void nativeResumeTravel(ScriptContext& ctx) {
  if (game::MapWindow* map = (game::worldOf(ctx) ? game::worldOf(ctx)->map() : nullptr)) map->resumeTravel(ctx.wm.nowMs());
}

}  // namespace

void registerMapNatives(lua_State* L, ScriptContext& ctx) {
  bind<nativeInitiateTravel>(L, ctx, "InitiateTravel");
  bind<nativePauseTravel>(L, ctx, "PauseTravel");
  bind<nativeResumeTravel>(L, ctx, "ResumeTravel");
}

bool evalLuaCondition(ScriptContext& ctx, const std::string& expr, std::string& error) {
  lua_State* L = ctx.lua.L();
  const int top = lua_gettop(L);
  const std::string code = "return (" + expr + ")";
  bool ok = false;
  if (luaL_loadbuffer(L, code.data(), code.size(), "=expect-lua") != 0 || lua_pcall(L, 0, 1, 0) != 0) {
    const char* m = lua_tostring(L, -1);
    error = m ? m : "(error)";
  } else {
    ok = lua_toboolean(L, -1) != 0;
  }
  lua_settop(L, top);
  return ok;
}

}  // namespace choco::script
