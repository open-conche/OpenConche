#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "script/Bind.h"
#include "ui/WindowManager.h"

namespace choco::script {

// Owns the Main coroutine and everything that registers into the Lua state (script-host design sections 2 to 4).
class ScriptHost : public ui::ScriptSink {
 public:
  enum class CoState { NotStarted, Running, WaitingMessage, Yielded, Dead };

  explicit ScriptHost(ScriptContext& ctx);  // sets ctx.host and installs itself as the window manager's sink
  ~ScriptHost() override;
  ScriptHost(const ScriptHost&) = delete;
  ScriptHost& operator=(const ScriptHost&) = delete;

  // Boot sequence steps 2 to 7 (step 1 is LuaState). False only if the prelude or mainloop.lua cannot be loaded.
  // Lua errors from running mainloop.lua are logged and boot still succeeds.
  bool boot();

  // Once per frame with the script clock in ms: advances the clock and resumes Main on timeout, poll or plain yield.
  void tick(double nowMs);

  // ui::ScriptSink
  void deliver(const ui::Message* message, const LuaRef* command) override;
  void runInScript(const LuaRef& fn) override;
  bool callProtected(const LuaRef& fn) override;
  void setGlobalString(const char* name, std::string_view value) override;
  void withGlobal(const char* name, const std::function<void(const ui::Style*)>& fn) override;
  std::string userPref(const std::string& key, const std::string& def) override;

  // DoLuaString: compiles and runs the code inside Main (injected) or with pcall when Main is not waiting.
  void runString(std::string_view code) override;

  // Logs "[lua-error] <message on top of L's stack>" and counts it. For natives that pcall into Lua.
  void logLuaError(lua_State* L);

  // True if the global holds a Lua truth value (used for gQuitActive).
  bool globalIsTrue(const char* name);
  bool globalIsFunction(const char* name);

  // Diagnostics (F10 / `diag`): Main's state, what it waits for, its Lua queues and a traceback of the coroutine.
  std::string diagnose();

  CoState state() const { return state_; }
  bool mainAlive() const { return state_ != CoState::Dead && state_ != CoState::NotStarted; }
  bool gameStateTouched() const { return ctx_.gameStateTouched; }  // see ScriptContext
  bool mainReturned() const { return returned_; }  // Main ended without an error
  int luaErrorCount() const { return errors_; }
  // Frames in which Main was resumed from tick: used by tests.
  std::uint64_t resumeCount() const { return resumes_; }

 private:
  friend int hostHandleGetMessage(lua_State*);
  friend int hostYield(lua_State*);
  void resume(int nargs);
  static void resumeErrorHook(lua_State* L);  // lua_resume's error hook: takes Main's traceback before the frames are discarded
  void queueMessage(const ui::Message* message, const LuaRef* command);
  void startMain(lua_State* L);

  ScriptContext& ctx_;
  std::optional<LuaRef> thread_;  // lua_newthread, anchored in the registry
  lua_State* co_ = nullptr;       // valid while thread_ is set
  CoState state_ = CoState::NotStarted;
  CoState pendingYield_ = CoState::Running;  // what the last yield native asked for; Running = none
  double wakeAtMs_ = -1;
  std::uint64_t waitFrame_ = 0;
  int errors_ = 0;
  bool returned_ = false;
  bool deadLogged_ = false;
  std::string deathTraceback_;  // Main's traceback at the error that killed it (empty while alive)
  std::string deathMessage_;
  std::uint64_t resumes_ = 0;
};

// Registers GetTimer, CurrentTime and RandRange over the stubs.
void registerCoreNatives(lua_State* L, ScriptContext& ctx);

// The window natives and the content, config and string natives.
void registerWindowNatives(lua_State* L, ScriptContext& ctx);
void registerContentNatives(lua_State* L, ScriptContext& ctx);
void registerTextNatives(lua_State* L, ScriptContext& ctx);
// SoundEvent (script/AudioBridge.h).
void registerAudioNatives(lua_State* L, ScriptContext& ctx);
// Users, preferences, save strings, user files and scores.
void registerUserNatives(lua_State* L, ScriptContext& ctx);
// The harness's expect-lua: `return (<expr>)` on the main state with pcall. True when the result is truthy; an error is
// false and sets `error`. Never yields and is never injected.
bool evalLuaCondition(ScriptContext& ctx, const std::string& expr, std::string& error);

// Runs the AddWindowType template for `name` and records it in the window manager. False for an invalid name.
bool addWindowType(lua_State* L, ScriptContext& ctx, const std::string& name);

}  // namespace choco::script
