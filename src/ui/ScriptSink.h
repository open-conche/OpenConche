#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "script/LuaState.h"
#include "ui/Message.h"
#include "ui/Style.h"

namespace choco::ui {

// What the window side calls on the script host (script-host design section 2). ui keeps Lua values only as LuaRefs.
class ScriptSink {
 public:
  virtual ~ScriptSink() = default;
  // The OnMessage branches. `message` is null for a bare button command (the script then sees kCommandOnly).
  virtual void deliver(const Message* message, const script::LuaRef* command) = 0;
  // Runs `fn` inside the script: injected when Main is waiting, else pcall on the main state.
  virtual void runInScript(const script::LuaRef& fn) = 0;
  // pcall on the main state (transition onend, rollover contents).
  virtual bool callProtected(const script::LuaRef& fn) = 0;
  // DoLuaString: compiles and runs source text the same way (a Rollover's `contents`). Hosts without a script ignore it.
  virtual void runString(std::string_view code) { (void)code; }
  // The current user's preference `key` (soundlevel and so on); `def` when the host has no user store.
  virtual std::string userPref(const std::string& key, const std::string& def) {
    (void)key;
    return def;
  }
  // Calls `fn` with a read-only view of the global table `name` (Player, _AllPorts), or null if it is not a table. The
  // view is valid only inside the callback. Hosts without a script call it with null.
  virtual void withGlobal(const char* name, const std::function<void(const Style*)>& fn) {
    (void)name;
    fn(nullptr);
  }
  virtual void setGlobalString(const char* name, std::string_view value) = 0;
};

}  // namespace choco::ui
