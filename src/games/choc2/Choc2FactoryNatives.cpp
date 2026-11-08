#include "games/choc2/Choc2FactoryNatives.h"

#include <exception>
#include <memory>
#include <string>

#include "games/choc2/FactoryGame.h"
#include "games/choc2/FactoryStatus.h"
#include "script/LuaApi.h"
#include "script/ScriptHost.h"
#include "ui/WindowManager.h"

namespace choco::script {

namespace {

using game::choc2::FactoryGame;

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

// The FactoryGame the natives act on. With none open the call is counted as an unimplemented use (once logged), never silent.
FactoryGame* gameOrLog(lua_State* L) {
  ScriptContext& ctx = contextOf(L);
  FactoryGame* g = FactoryGame::active();
  if (!g && ctx.stubs.hit(nativeNameOf(L))) ctx.log.write(std::string("[unimplemented] ") + nativeNameOf(L) + " (no FactoryGame is open)");
  return g;
}

int nativeResumeMinigame(lua_State* L) {
  if (FactoryGame* g = gameOrLog(L)) g->resume(L);
  return 0;
}

int nativePauseMinigame(lua_State* L) {
  if (FactoryGame* g = gameOrLog(L)) g->pause(L);
  return 0;
}

int nativeResetMinigame(lua_State* L) {
  if (FactoryGame* g = gameOrLog(L)) g->reset();
  return 0;
}

int nativeGetProduction(lua_State* L) {
  FactoryGame* g = gameOrLog(L);
  lua_pushnumber(L, g ? static_cast<lua_Number>(g->production()) : 0);
  return 1;
}

}  // namespace

void registerChoc2FactoryNatives(lua_State* L, ScriptContext& ctx) {
  ScriptContext* c = &ctx;
  ctx.wm.addWindowType("FactoryGame", [c](ui::WindowManager& wm) { return std::make_unique<FactoryGame>(wm, *c); });
  ctx.wm.addWindowType("FactoryStatus", [c](ui::WindowManager& wm) { return std::make_unique<game::choc2::FactoryStatus>(wm, *c); });
  bindRaw(L, ctx, "ResumeMinigame", &guarded<nativeResumeMinigame>);
  bindRaw(L, ctx, "PauseMinigame", &guarded<nativePauseMinigame>);
  bindRaw(L, ctx, "ResetMinigame", &guarded<nativeResetMinigame>);
  bindRaw(L, ctx, "GetProduction", &guarded<nativeGetProduction>);
}

}  // namespace choco::script
