#include "script/ScriptHost.h"
#include "sdk/Users.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "assets/Content.h"
#include "script/Embedded.h"
#include "script/GameModule.h"
#include "script/Loaders.h"
#include "script/LuaStyle.h"
#include "script/Messages.h"

namespace choco::script {

namespace {
// A yield only works when no C function (pcall, dofile, table.sort...) is
// between the coroutine body and the native; otherwise Lua 5.0 raises "attempt
// to yield across metamethod/C-call boundary".
bool canYield(lua_State *L) {
  lua_Debug ar;
  for (int level = 1; lua_getstack(L, level, &ar); ++level) {
    if (lua_getinfo(L, "S", &ar) && ar.what && ar.what[0] == 'C')
      return false;
  }
  return true;
}
} // namespace

// HandleGetMessage([timeout]): records how long to wait and yields
// gYieldResult. A nil timeout is tonumber(nil) = 0, a poll on every tick.
// Called by the prelude's WaitForMessage, which always passes its first
// argument.
int hostHandleGetMessage(lua_State *L) {
  ScriptContext &ctx = contextOf(L);
  ScriptHost *h = ctx.host;
  if (L == h->co_ && canYield(L)) {
    double wake = -1;
    if (lua_gettop(L) == 1) {
      double t = lua_tonumber(L, 1);
      if (std::isnan(t) || t < 0)
        t = 0;
      wake = ctx.clock.nowMs + t;
    }
    h->wakeAtMs_ = wake;
    h->waitFrame_ = ctx.clock.frame;
    h->pendingYield_ = ScriptHost::CoState::WaitingMessage;
  }
  lua_getglobal(L, "gYieldResult");
  return lua_yield(L, 1);
}

// coroutine.yield: as stock, but Main's plain yields are recognised so the host
// knows why it stopped.
int hostYield(lua_State *L) {
  ScriptHost *h = contextOf(L).host;
  if (L == h->co_ && canYield(L))
    h->pendingYield_ = ScriptHost::CoState::Yielded;
  return lua_yield(L, lua_gettop(L));
}

namespace {
std::vector<ScriptHost *> &liveHosts() {
  static std::vector<ScriptHost *> hosts;
  return hosts;
}
} // namespace

void ScriptHost::resumeErrorHook(lua_State *L) {
  for (ScriptHost *h : liveHosts()) {
    if (h->co_ != L)
      continue; // only Main's own coroutine; sprite-script states are left
                // alone
    std::string tb;
    char buf[320];
    lua_Debug ar;
    // Runs from luaD_throw (see cmake/PatchLua.cmake), so the frames are still
    // live and function names are available.
    for (int level = 0; level < 40 && lua_getstack(L, level, &ar); ++level) {
      lua_getinfo(L, "Sln", &ar);
      std::snprintf(buf, sizeof buf, "  #%d %s:%d in %s '%s' (%s)\n", level,
                    ar.short_src, ar.currentline,
                    ar.namewhat ? ar.namewhat : "", ar.name ? ar.name : "?",
                    ar.what ? ar.what : "?");
      tb += buf;
    }
    h->deathTraceback_ = std::move(tb);
    return;
  }
}

ScriptHost::ScriptHost(ScriptContext &ctx) : ctx_(ctx) {
  liveHosts().push_back(this);
  lua_resumeerrorhook = &ScriptHost::resumeErrorHook;
  ctx_.host = this;
  ctx_.wm.setScriptSink(this);
}

ScriptHost::~ScriptHost() {
  auto &hosts = liveHosts();
  hosts.erase(std::remove(hosts.begin(), hosts.end(), this), hosts.end());
  if (hosts.empty())
    lua_resumeerrorhook = nullptr;
  ctx_.wm.setScriptSink(nullptr);
  thread_.reset();
  co_ = nullptr;
  ctx_.host = nullptr;
}

void ScriptHost::logLuaError(lua_State *L) {
  const char *msg = lua_tostring(L, -1);
  ctx_.log.write(std::string("[lua-error] ") +
                 (msg ? msg : "(error object is not a string)"));
  ++errors_;
  ctx_.wm.noteScriptError();
}

bool ScriptHost::boot() {
  lua_State *L = ctx_.lua.L();
  if (!ctx_.module) {
    ctx_.log.write("[engine-error] no game module");
    return false;
  }
  const GameModule &module = *ctx_.module;

  // Stubs go in first so that every real native below overwrites its stub
  // (design section 6).
  module.registerStubs(L, ctx_);

  // Step 2: host loaders and coroutine.yield.
  registerLoaders(L, ctx_);
  lua_getglobal(L, "coroutine");
  lua_pushstring(L, "yield");
  lua_pushlightuserdata(L, &ctx_);
  lua_pushstring(L, "coroutine.yield");
  lua_pushcclosure(L, hostYield, 2);
  lua_rawset(L, -3);
  lua_pop(L, 1);

  // Step 3: _ALERT, DebugOut (toparams is a generated stub).
  registerLogging(L, ctx_);
  registerMessageType(L);

  // Step 4: the SDK natives (over the stubs).
  registerCoreNatives(L, ctx_);
  module.registerCoreExtras(L, ctx_);
  registerWindowNatives(L, ctx_);
  registerContentNatives(L, ctx_);
  module.registerContentExtras(L, ctx_);
  registerTextNatives(L, ctx_);
  registerUserNatives(L, ctx_);
  registerAudioNatives(L, ctx_);
  // The game's natives go over the SDK ones, then HandleGetMessage, then the
  // game's window types.
  module.registerNatives(L, ctx_);
  bindRaw(L, ctx_, "HandleGetMessage", hostHandleGetMessage);
  module.registerWindowTypes(L, ctx_);

  // Step 5: the prelude, unchanged, then host.lua.
  struct Chunk {
    std::string_view code;
    const char *name;
  } chunks[] = {{embeddedPrelude(), "prelude.lua"},
                {embeddedHost(), "host.lua"}};
  for (const auto &c : chunks) {
    if (loadBuffer(L, c.code, c.name) != 0 || lua_pcall(L, 0, 0, 0) != 0) {
      ctx_.log.write(std::string("[engine-error] ") + c.name + ": " +
                     lua_tostring(L, -1));
      lua_settop(L, 0);
      return false;
    }
  }

  // Step 6: constants and settings.xml.
  for (const auto &kv : assets::loadSettings(ctx_.fs))
    ctx_.config.set(kv.first, kv.second);
  // Online play does not exist here, so the high-score screen is always the
  // local-only one (a --config value still wins).
  ctx_.config.set("hiscorelocal", "1");
  {
    assets::Warnings warnings;
    ctx_.strings.loadFile(ctx_.fs, module.profile().stringsPath,
                          assets::StringTable::Layer::Sdk, &warnings);
    for (const auto &w : warnings)
      ctx_.log.write("[content] " + w);
  }
  struct Num {
    const char *name;
    double v;
  } nums[] = {{"kScreenWidth", 800},
              {"kScreenHeight", 600},
              {"kGameWidth", 800},
              {"kGameHeight", 600},
              {"kGameScale", 1}};
  for (const auto &n : nums) {
    lua_pushnumber(L, n.v);
    lua_setglobal(L, n.name);
  }
  module.registerGlobals(L, ctx_);
  std::string version = "ver " + ctx_.config.get("version");
  // Chocolatier 2's settings.xml has no version key, which left "ver " on the
  // credits dialog's footer: its title instead.
  if (module.profile().versionFromTitle && ctx_.config.get("version").empty())
    version = ctx_.strings.sdkLookup("gametitle");
  lua_pushlstring(L, version.data(), version.size());
  lua_setglobal(L, "gVersionString");

  // Step 7: run the game's entry script; a function result becomes Main.
  const char *mainloop = module.profile().mainloop;
  if (loadChunk(L, ctx_, mainloop) != 0) {
    ctx_.log.write(std::string("[engine-error] ") + mainloop + ": " +
                   lua_tostring(L, -1));
    lua_settop(L, 0);
    return false;
  }
  if (lua_pcall(L, 0, 1, 0) != 0) {
    logLuaError(L);
    lua_settop(L, 0);
    state_ = CoState::Dead;
    return true;
  }
  startMain(L);
  return true;
}

void ScriptHost::startMain(lua_State *L) {
  if (!lua_isfunction(L, -1)) {
    ctx_.log.write(std::string("[script] ") + ctx_.module->profile().mainloop +
                   " did not return a function; no Main to run");
    lua_settop(L, 0);
    state_ = CoState::Dead;
    return;
  }
  lua_State *co = lua_newthread(L); // stack: Main, thread
  thread_.emplace(ctx_.lua, L, -1);
  co_ = co;
  lua_pop(L, 1);
  lua_xmove(L, co, 1);
  resume(0);
}

void ScriptHost::resume(int nargs) {
  ++resumes_;
  state_ = CoState::Running;
  pendingYield_ = CoState::Running; // "no yield recorded"; the yielding natives
                                    // set it just before lua_yield
  int status = lua_resume(co_, nargs);
  if (status != 0) {
    const char *m = lua_tostring(co_, -1);
    deathMessage_ = m ? m : "(error object is not a string)";
    ++errors_;
    // Kept from before: a script that fails while a window script loads inside
    // Main is attributed to that modal (test_ui 'a script error is attributed
    // to the modal').
    ctx_.wm.noteScriptError();
    ctx_.log.write(std::string("[lua-error] ") + deathMessage_);
    ctx_.log.write("[script] Main died: " + deathMessage_ +
                   " \u2014 the game will no longer respond");
    if (!deathTraceback_.empty())
      ctx_.log.write(
          "[script] Main traceback at the error (level 0 = innermost):\n" +
          deathTraceback_.substr(0, deathTraceback_.size() - 1));
    lua_settop(co_, 0);
    thread_.reset();
    co_ = nullptr;
    state_ = CoState::Dead;
  } else if (pendingYield_ == CoState::Running) {
    // Neither HandleGetMessage nor yield marked a stop: Main returned.
    ctx_.log.write("[script] Main returned");
    returned_ = true;
    thread_.reset();
    co_ = nullptr;
    state_ = CoState::Dead;
  } else {
    state_ = pendingYield_;
    lua_settop(co_, 0); // the yielded values
    if (state_ != CoState::Suspended)
      suspendToken_ = 0;
  }
}

ScriptHost::Suspension ScriptHost::suspend(lua_State *L) {
  if (L != co_ || !canYield(L))
    return {};
  pendingYield_ = CoState::Suspended;
  suspendToken_ = ++nextToken_;
  return {suspendToken_};
}

bool ScriptHost::resume(const Suspension &s, bool value) {
  if (!s.valid() || s.token != suspendToken_ || state_ != CoState::Suspended ||
      !co_)
    return false;
  suspendToken_ = 0;
  lua_pushboolean(co_, value ? 1 : 0);
  resume(1);
  return true;
}

void ScriptHost::tick(double nowMs) {
  ctx_.clock.nowMs = nowMs;
  ++ctx_.clock.frame;
  if (ctx_.gameState)
    ctx_.gameState->tick(nowMs);
  if (state_ == CoState::Yielded) {
    resume(0);
  } else if (state_ == CoState::WaitingMessage && wakeAtMs_ >= 0 &&
             nowMs >= wakeAtMs_ && ctx_.clock.frame > waitFrame_) {
    resume(0);
  }
}

std::string ScriptHost::diagnose() {
  static const char *const kNames[] = {
      "not started", "running (inside a resume)",
      "suspended in WaitForMessage (HandleGetMessage)",
      "suspended in a plain yield (Pause/Yield)", "dead",
      "suspended by a native (waiting for a resume)"};
  char buf[256];
  std::string out =
      std::string("Main coroutine: ") + kNames[static_cast<int>(state_)];
  if (state_ == CoState::WaitingMessage) {
    if (wakeAtMs_ >= 0)
      std::snprintf(buf, sizeof buf, ", wakes at %g ms (script clock now %g)",
                    wakeAtMs_, ctx_.clock.nowMs);
    else
      std::snprintf(buf, sizeof buf, ", no timeout (wakes only on a message)");
    out += buf;
  }
  std::snprintf(buf, sizeof buf, "; resumes=%llu lua-errors=%d returned=%d\n",
                static_cast<unsigned long long>(resumes_), errors_,
                returned_ ? 1 : 0);
  out += buf;
  lua_State *L = ctx_.lua.L();
  for (const char *q : {"gMessageQueue", "gCommandQueue"}) {
    lua_getglobal(L, q);
    out += std::string("Lua ") + q + ": " +
           (lua_istable(L, -1) ? std::to_string(luaL_getn(L, -1)) + " entries"
                               : "(not a table)") +
           "\n";
    lua_pop(L, 1);
  }
  if (state_ == CoState::Dead && !deathMessage_.empty()) {
    out += "MAIN IS DEAD: " + deathMessage_ +
           "\nthe game will no longer respond to input. Traceback at the "
           "error:\n" +
           deathTraceback_;
  }
  if (co_) {
    out += "Main traceback (level 0 = innermost):\n";
    lua_Debug ar;
    for (int level = 0; level < 40 && lua_getstack(co_, level, &ar); ++level) {
      lua_getinfo(co_, "Snl", &ar);
      std::snprintf(buf, sizeof buf, "  #%d %s:%d in %s '%s' (%s)\n", level,
                    ar.short_src, ar.currentline,
                    ar.namewhat ? ar.namewhat : "", ar.name ? ar.name : "?",
                    ar.what ? ar.what : "?");
      out += buf;
    }
  }
  return out;
}

void ScriptHost::queueMessage(const ui::Message *message,
                              const LuaRef *command) {
  lua_State *L = ctx_.lua.L();
  auto append = [&](const char *tableName, auto &&pushValue) {
    lua_getglobal(L, tableName);
    if (lua_istable(L, -1)) {
      int n = luaL_getn(L, -1);
      pushValue();
      lua_rawseti(L, -2, n + 1);
      luaL_setn(L, -1,
                n + 1); // keep the cached size in step, as table.insert does
    }
    lua_pop(L, 1);
  };
  if (message)
    append("gMessageQueue", [&] { pushMessage(L, *message); });
  if (command)
    append("gCommandQueue", [&] { command->push(L); });
}

void ScriptHost::deliver(const ui::Message *message, const LuaRef *command) {
  switch (state_) {
  case CoState::WaitingMessage: {
    lua_State *co = co_;
    pushMessage(co, message ? *message : ui::Message{ui::kCommandOnly, "", 0});
    int n = 1;
    if (command) {
      command->push(co);
      n = 2;
    }
    resume(n);
    break;
  }
  case CoState::Yielded:
  case CoState::Suspended: // suspended in a native is a plain yield to the message pump
    if (command)
      callProtected(*command); // a plain Yield: the message is dropped
    break;
  case CoState::Dead:
    if (!deadLogged_) {
      deadLogged_ = true;
      ctx_.log.write("[script] Main is not running; messages are only queued");
    }
    queueMessage(message, command);
    break;
  case CoState::NotStarted:
  case CoState::Running:
    queueMessage(message, command);
    break;
  }
}

void ScriptHost::runInScript(const LuaRef &fn) {
  if (state_ == CoState::WaitingMessage) {
    lua_pushnil(co_);
    fn.push(co_);
    resume(2);
  } else { // a plain Yield would discard the resume arguments, so call fn
           // directly, as deliver() does
    callProtected(fn);
  }
}

bool ScriptHost::callProtected(const LuaRef &fn) {
  lua_State *L = ctx_.lua.L();
  int top = lua_gettop(L);
  fn.push(L);
  bool ok = lua_pcall(L, 0, 0, 0) == 0;
  if (!ok)
    logLuaError(L);
  lua_settop(L, top);
  return ok;
}

bool ScriptHost::globalIsFunction(const char *name) {
  lua_State *L = ctx_.lua.L();
  lua_getglobal(L, name);
  const bool v = lua_isfunction(L, -1) != 0;
  lua_pop(L, 1);
  return v;
}

bool ScriptHost::globalIsTrue(const char *name) {
  if (!name || !*name)
    return false; // a profile with no such global
  lua_State *L = ctx_.lua.L();
  lua_getglobal(L, name);
  bool v = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return v;
}

void ScriptHost::setGlobalString(const char *name, std::string_view value) {
  lua_State *L = ctx_.lua.L();
  lua_pushlstring(L, value.data(), value.size());
  lua_setglobal(L, name);
}

void ScriptHost::withGlobal(const char *name,
                            const std::function<void(const ui::Style *)> &fn) {
  lua_State *L = ctx_.lua.L();
  const int top = lua_gettop(L);
  lua_getglobal(L, name);
  if (lua_istable(L, -1)) {
    const LuaStyle view(L, lua_gettop(L), &ctx_.lua);
    fn(&view);
  } else {
    fn(nullptr);
  }
  lua_settop(L, top);
}

std::string ScriptHost::userPref(const std::string &key,
                                 const std::string &def) {
  return ctx_.users ? ctx_.users->pref(key, def) : def;
}

void ScriptHost::runString(std::string_view code) {
  lua_State *L = ctx_.lua.L();
  int top = lua_gettop(L);
  std::optional<LuaRef> fn;
  if (loadBuffer(L, code, "string") != 0) {
    logLuaError(L);
  } else {
    fn.emplace(ctx_.lua, L, -1);
  }
  lua_settop(L, top);
  if (fn)
    runInScript(*fn);
}

} // namespace choco::script
