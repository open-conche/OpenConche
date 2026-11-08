#include "script/RunFile.h"

#include "script/ScriptHost.h"

namespace choco::script {

void runFileInto(ScriptContext& ctx, lua_State* L, ui::Window& target, const std::string& path) {
  ui::WindowManager& wm = ctx.wm;
  const std::size_t depth = wm.creationDepth();
  wm.pushCreation(target);
  const int top = lua_gettop(L);
  lua_getglobal(L, "dofile");
  lua_pushlstring(L, path.data(), path.size());
  if (lua_pcall(L, 1, 0, 0) != 0) {
    if (ctx.host) ctx.host->logLuaError(L);
    else ctx.log.write(std::string("[lua-error] ") + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "?"));
  }
  lua_settop(L, top);
  while (wm.creationDepth() > depth) wm.popCreation();
}

}  // namespace choco::script
