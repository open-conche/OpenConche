#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "assets/AssetFS.h"
#include "games/Registry.h"
#include "script/Bind.h"
#include "script/ScriptHost.h"
#include "ui/WindowManager.h"

namespace choco::test {

// A LuaState with the context natives need, rooted at a test asset directory.
struct ScriptEnv {
  explicit ScriptEnv(const std::string& assetDir,
                     const std::string& userDir = (std::filesystem::temp_directory_path() / "choco_test_user").string())
      : fs(assetDir, userDir),
        ctx(lua, fs, wm, clock, rng, config, stubs, log) {
    ctx.module = games::findModule("choc3");
    ctx.module->attach(ctx);
    log.setEcho(false);
    log.setKeep(true);
  }
  // Members die in reverse order, so ctx would go before wm; the windows' destructors may read ctx (a MapScreen lowers
  // its world's gate), so they go first.
  ~ScriptEnv() { wm.destroyAll(); }

  // Runs code in the main state; returns the pcall status.
  int run(const std::string& code, const char* chunkName = "=test") {
    lua_State* L = lua.L();
    int top = lua_gettop(L);
    int status = luaL_loadbuffer(L, code.data(), code.size(), chunkName);
    if (status == 0) status = lua_pcall(L, 0, 0, 0);
    if (status != 0) lastError = lua_tostring(L, -1);
    lua_settop(L, top);
    return status;
  }
  std::string getString(const char* global) {
    lua_State* L = lua.L();
    lua_getglobal(L, global);
    std::string s = lua_isnil(L, -1)       ? "<nil>"
                    : lua_isboolean(L, -1) ? (lua_toboolean(L, -1) ? "true" : "false")
                    : lua_isstring(L, -1)  ? lua_tostring(L, -1)
                                           : lua_typename(L, lua_type(L, -1));
    lua_pop(L, 1);
    return s;
  }
  double getNumber(const char* global) {
    lua_State* L = lua.L();
    lua_getglobal(L, global);
    double d = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return d;
  }
  bool isNil(const char* global) {
    lua_State* L = lua.L();
    lua_getglobal(L, global);
    bool nil = lua_isnil(L, -1);
    lua_pop(L, 1);
    return nil;
  }
  script::LuaRef compile(const std::string& code) {
    lua_State* L = lua.L();
    luaL_loadbuffer(L, code.data(), code.size(), "=ref");
    script::LuaRef r(lua, L, -1);
    lua_pop(L, 1);
    return r;
  }

  script::LuaState lua;
  assets::AssetFS fs;
  ui::WindowManager wm;
  script::Clock clock;
  script::Rng rng{1};
  script::Config config;
  script::StubLog stubs;
  script::Log log;
  script::ScriptContext ctx;
  std::string lastError;
};

}  // namespace choco::test
