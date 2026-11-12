#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <string>

#include "script/LuaApi.h"

namespace choco::gfx {
class SheetCache;
class SpriteSheet;
}
namespace choco::script {
class Rng;
class Log;
}  // namespace choco::script

namespace choco::sdk {

class SpriteScript;

// What an animation script drives: the sprite (or scene node) that owns the script. sdk::Sprite is one implementation; a
// game's own scene node can be another. The script never touches anything else of its owner.
class ScriptTarget {
 public:
  virtual ~ScriptTarget() = default;
  // The sheet being animated (frame count, script text, path for logs); null when there is none.
  virtual const gfx::SpriteSheet* scriptSheet() const = 0;
  virtual void scriptSetSheet(std::shared_ptr<const gfx::SpriteSheet> sheet) = 0;
  // `i` is already range-checked against the sheet's frame count (-1 allowed).
  virtual void scriptSetFrame(int i) = 0;
  virtual void scriptSetAlpha(float a) = 0;
  virtual void scriptSetLayer(int layer) = 0;
  virtual void scriptRemove() = 0;
  // Pause(pause, depth) and IsPlaying(depth): this target's script and those of its children down to `depth` (-1: all).
  virtual void scriptPause(bool pause, int depth, double now) = 0;
  virtual bool scriptIsPlaying(int depth) const = 0;
};

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

  SpriteScript(ScriptTarget& owner, SpriteContext ctx);
  ~SpriteScript();
  SpriteScript(const SpriteScript&) = delete;
  SpriteScript& operator=(const SpriteScript&) = delete;

  // Adds a global function to the script's Lua state, after the built-in natives and before the sheet's own text runs, so it
  // must be called before the first play(). The function runs on the script's state or its coroutine: it must not throw.
  // Upvalues are not available to it; capture what it needs in the std::function. A later registration of the same name wins.
  using Native = std::function<int(lua_State*)>;
  void addNative(const std::string& name, Native fn);

  // Starts `fn` on a new coroutine and runs it to its first yield. False when the function is missing or the script
  // could not be set up (logged once).
  bool play(const char* fn, double now);
  // Resumes the coroutine if it is due.
  void tick(double now);
  // Ends the running function (the sprite's Stop): nothing is playing afterwards. The state and its globals stay. Called from a
  // native of the function itself (an unlock() callback that changes the sprite) it is deferred: the function is halted at its
  // next native call and the coroutine is released when the resume returns.
  void stop();
  // Runs a piece of Lua text in the script's state (a game's SetDelay runs `delay(ms);`). False when the state does not exist
  // yet (nothing has been played) or the text failed (each failure is logged with the sheet's name).
  bool runChunk(const std::string& text);

  // True while a coroutine is alive: yielded (possibly paused) and not finished, dead or failed.
  bool playing() const { return thread_ != nullptr && !dead_; }
  bool paused() const { return playing() && wakeAt_ < 0; }
  double wakeAt() const { return wakeAt_; }
  // Pause/resume this script alone: asleep for ever, or due 1 ms from `now`. No effect when nothing is playing.
  void setPaused(bool pause, double now) {
    if (playing()) wakeAt_ = pause ? -1 : now + 1;
  }
  // The state, for tests.
  lua_State* state() const { return L_; }

 private:
  friend struct SpriteNatives;
  bool setup();
  bool startFunction(const char* fn);
  void resume();
  void fail(const std::string& what);
  void releaseThread();

  ScriptTarget& owner_;
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
  bool running_ = false;        // the coroutine is executing (a native of it is calling back)
  bool stopRequested_ = false;  // stop() or play() arrived while running_: applied when the resume returns
  bool hasPending_ = false;
  std::string pendingFn_;       // the function a play() that arrived while running_ starts afterwards
  bool failedSetup_ = false;
  bool warnedRandom_ = false;
  struct Extra {
    std::string name;
    Native fn;
  };
  std::deque<Extra> extras_;  // stable addresses: each closure points at its entry
};

}  // namespace choco::sdk
