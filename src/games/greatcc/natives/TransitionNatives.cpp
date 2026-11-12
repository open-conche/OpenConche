#include "games/greatcc/natives/TransitionNatives.h"

#include <exception>

#include "games/greatcc/GreatCCWorld.h"
#include "script/ScriptHost.h"

namespace choco::games::greatcc {

namespace {

int pushBool(lua_State* L, bool v) {
  lua_pushboolean(L, v ? 1 : 0);
  return 1;
}

int luaDoTransition(lua_State* L) {
  script::ScriptContext& ctx = script::contextOf(L);
  try {
    GreatCCWorld* world = worldOf(ctx);
    if (!world || !lua_isnumber(L, 1)) return pushBool(L, false);
    TransitionManager& tm = world->transitions();
    std::unique_ptr<Overlay> overlay = tm.make(static_cast<int>(lua_tonumber(L, 1)), "");
    if (!overlay) return pushBool(L, false);
    const script::ScriptHost::Suspension s = ctx.host ? ctx.host->suspend(L) : script::ScriptHost::Suspension{};
    if (!s.valid()) {
      // Not Main's coroutine, or a C call in between: the transition still plays, but nothing can wait for it.
      ctx.log.write("[greatcc] DoTransition: cannot suspend here, not waiting for the transition");
      tm.submit(std::move(overlay));
      return pushBool(L, true);
    }
    overlay->waiter = s;
    tm.submit(std::move(overlay));
    return lua_yield(L, 0);  // the monitor resumes the coroutine with true
  } catch (const std::exception& e) {
    ctx.log.write(std::string("[native-error] Lua_DoTransition: ") + e.what());
  }
  return pushBool(L, false);
}

}  // namespace

void bindTransitionNatives(lua_State* L, script::ScriptContext& ctx) { script::rebindRaw(L, ctx, "Lua_DoTransition", luaDoTransition); }

}  // namespace choco::games::greatcc
