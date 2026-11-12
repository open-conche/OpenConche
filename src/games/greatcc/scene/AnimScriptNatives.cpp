#include "games/greatcc/scene/AnimScriptNatives.h"

#include "games/greatcc/GreatCCWorld.h"

namespace choco::games::greatcc {

void addSpriteScriptNatives(sdk::SpriteScript& script, script::ScriptContext& ctx, std::function<unsigned()> delayMs) {
  script.addNative("getdelay", [delayMs = std::move(delayMs)](lua_State* L) {
    lua_pushnumber(L, delayMs ? static_cast<double>(delayMs()) : 60.0);
    return 1;
  });
  script.addNative("playsound", [&ctx](lua_State* L) {
    // The loop flag is lua_toboolean style: absent is false. A non-string file name plays nothing.
    const char* file = lua_isstring(L, 1) ? lua_tostring(L, 1) : nullptr;
    if (!file) {
      ctx.log.write("[native-error] playsound: the first argument is not a string");
      return 0;
    }
    if (GreatCCWorld* w = worldOf(ctx)) w->sound().playSfx(file, lua_toboolean(L, 2) != 0);
    return 0;
  });
}

void addMachineCallback(sdk::SpriteScript& script, const std::string& name, std::function<void()> fn) {
  script.addNative(name, [fn = std::move(fn)](lua_State*) {
    if (fn) fn();
    return 0;
  });
}

}  // namespace choco::games::greatcc
