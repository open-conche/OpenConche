#pragma once

#include <string>

#include "script/LuaApi.h"

namespace choco::gfx {
class SheetCache;
}
namespace choco::script {
class Rng;
class Log;
}  // namespace choco::script

namespace choco::sdk {

class Sprite;

// What a sprite script needs from the world. All three outlive every sprite (the App owns them before the windows).
struct SpriteContext {
  gfx::SheetCache& sheets;
  script::Rng& rng;
  script::Log& log;
};

// The per-sprite animation script (docs/re/port-window.md section 5, docs/design/game-windows.md 3.4). Each scripted
// sprite owns a Lua state with only the base, string and table libraries. It runs the embedded animationscript.lua,
// then the sheet's script text, and plays one function of it (normally DoAnim) on a coroutine. The script yields with
// WaitLuaNextFrame(ms) and is resumed on the first tick at or after its wake time, on the script clock.
class SpriteScript {
 public:
  // The count hook aborts a script that runs this many instructions without yielding.
  static constexpr int kInstructionLimit = 1000000;

  SpriteScript(Sprite& owner, SpriteContext ctx);
  ~SpriteScript();
  SpriteScript(const SpriteScript&) = delete;
  SpriteScript& operator=(const SpriteScript&) = delete;

  // Starts `fn` on a new coroutine and runs it to its first yield. False when the function is missing or the script
  // could not be set up (logged once).
  bool play(const char* fn, double now);
  // Resumes the coroutine if it is due.
  void tick(double now);

  // True while a coroutine is alive: yielded (possibly paused) and not finished, dead or failed.
  bool playing() const { return thread_ != nullptr && !dead_; }
  bool paused() const { return playing() && wakeAt_ < 0; }
  double wakeAt() const { return wakeAt_; }
  // The state, for tests.
  lua_State* state() const { return L_; }

 private:
  friend struct SpriteNatives;
  bool setup();
  void resume();
  void fail(const std::string& what);
  void releaseThread();

  Sprite& owner_;
  SpriteContext ctx_;
  lua_State* L_ = nullptr;
  lua_State* thread_ = nullptr;
  int threadRef_ = LUA_NOREF;
  double now_ = 0;
  double wakeAt_ = -1;  // < 0: asleep until Pause(false)
  bool dead_ = false;
  bool failed_ = false;  // the last play() or resume ended in an error
  bool yielded_ = false;  // set by the yielding native just before it yields
  bool ready_ = false;
  bool failedSetup_ = false;
  bool warnedRandom_ = false;
};

}  // namespace choco::sdk
