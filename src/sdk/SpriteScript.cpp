#include "sdk/SpriteScript.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "sdk/Sprite.h"
#include "gfx/SheetCache.h"
#include "script/Embedded.h"
#include "script/LuaCompat.h"
#include "script/ScriptContext.h"

namespace choco::sdk {

// The natives, as plain C functions: Lua is built as C, so nothing here may throw or call lua_error.
struct SpriteNatives {
  static SpriteScript* self(lua_State* L) { return static_cast<SpriteScript*>(lua_touserdata(L, lua_upvalueindex(1))); }

  // WaitLuaNextFrame(ms?): sleep for ms from now, or for ever without an argument. Yields when run on the coroutine.
  static int waitLuaNextFrame(lua_State* L) {
    SpriteScript* s = self(L);
    s->wakeAt_ = (lua_gettop(L) >= 1 && lua_isnumber(L, 1)) ? s->now_ + lua_tonumber(L, 1) : -1;
    if (L == s->thread_) {
      s->yielded_ = true;
      return lua_yield(L, 0);
    }
    return 0;
  }

  static int setCurrentFrame(lua_State* L) {
    SpriteScript* s = self(L);
    const double i = lua_tonumber(L, 1);
    const int n = s->owner_.sheet ? static_cast<int>(s->owner_.sheet->frames.size()) : 0;
    if (i > -2 && i < n) s->owner_.frame = static_cast<int>(i);
    return 0;
  }

  static int setAlpha(lua_State* L) {
    self(L)->owner_.alpha = static_cast<float>(lua_tonumber(L, 1));
    return 0;
  }

  // Load(xml, image, mask): the sprite switches to that sheet. False when it cannot be loaded.
  static int load(lua_State* L) {
    SpriteScript* s = self(L);
    const char* xml = lua_tostring(L, 1);
    const char* image = lua_tostring(L, 2);
    const char* mask = lua_tostring(L, 3);
    auto sheet = xml ? s->ctx_.sheets.get(xml, image ? image : "", mask ? mask : "") : nullptr;
    const bool ok = sheet != nullptr;
    if (ok) s->owner_.sheet = std::move(sheet);
    lua_pushboolean(L, ok);
    return 1;
  }

  static int getScript(lua_State* L) {
    const SpriteScript* s = self(L);
    lua_pushstring(L, s->owner_.sheet ? s->owner_.sheet->script.c_str() : "");
    return 1;
  }

  static void pauseTree(Sprite& sp, bool pause, int depth, double now) {
    if (SpriteScript* sc = sp.scriptOf(); sc && sc->playing()) sc->wakeAt_ = pause ? -1 : now + 1;
    if (depth == 0) return;
    for (const auto& c : sp.children()) pauseTree(*c, pause, depth - 1, now);
  }

  // Pause(pause, depth): pausing sleeps for ever, resuming wakes in 1 ms; children down to `depth` follow (-1: all).
  static int pause(lua_State* L) {
    SpriteScript* s = self(L);
    const bool p = lua_toboolean(L, 1) != 0;
    const int depth = lua_isnumber(L, 2) ? static_cast<int>(lua_tonumber(L, 2)) : -1;
    pauseTree(s->owner_, p, depth, s->now_);
    return 0;
  }

  static bool playingTree(const Sprite& sp, int depth) {
    if (const SpriteScript* sc = sp.scriptOf(); sc && sc->playing()) return true;
    if (depth == 0) return false;
    for (const auto& c : sp.children())
      if (playingTree(*c, depth - 1)) return true;
    return false;
  }

  static int isPlaying(lua_State* L) {
    const int depth = lua_isnumber(L, 1) ? static_cast<int>(lua_tonumber(L, 1)) : -1;
    lua_pushboolean(L, playingTree(self(L)->owner_, depth));
    return 1;
  }

  static int die(lua_State* L) {
    self(L)->dead_ = true;
    return 0;
  }

  static int removeSprite(lua_State* L) {
    self(L)->owner_.removed = true;
    return 0;
  }

  static int setLayer(lua_State* L) {
    self(L)->owner_.setLayer(static_cast<int>(lua_tonumber(L, 1)));
    return 0;
  }

  // Random(a, b): a + rand % (b - a), so b is never returned; a when the range is empty (the original divides by zero).
  static int random(lua_State* L) {
    SpriteScript* s = self(L);
    const int a = static_cast<int>(lua_tonumber(L, 1)), b = static_cast<int>(lua_tonumber(L, 2));
    if (b < a && !s->warnedRandom_) {
      s->warnedRandom_ = true;
      s->ctx_.log.write("[sprite-lua] Random(" + std::to_string(a) + ", " + std::to_string(b) +
                        "): the original wraps an unsigned range here; returning the low bound");
    }
    lua_pushnumber(L, b <= a ? a : s->ctx_.rng.sideRange(a, b - 1));
    return 1;
  }

  static int alertOrDebug(lua_State* L) {
    SpriteScript* s = self(L);
    std::string line = "[sprite-lua]";
    for (int i = 1; i <= lua_gettop(L); ++i)
      if (const char* t = lua_tostring(L, i)) line += std::string(" ") + t;
    s->ctx_.log.write(line);
    return 0;
  }

  static void countHook(lua_State* L, lua_Debug*) {
    lua_pushstring(L, "script ran too long without yielding");
    lua_error(L);
  }
};

SpriteScript::SpriteScript(Sprite& owner, SpriteContext ctx) : owner_(owner), ctx_(ctx) {}

SpriteScript::~SpriteScript() {
  if (L_) lua_close(L_);  // closes the coroutine with it
}

void SpriteScript::fail(const std::string& what) {
  const std::string sheet = owner_.sheet ? owner_.sheet->path : std::string("?");
  ctx_.log.write("[sprite-lua] " + sheet + ": " + what);
  dead_ = true;
  failed_ = true;
}

bool SpriteScript::setup() {
  if (ready_) return true;
  if (failedSetup_) return false;
  L_ = lua_open();
  luaopen_base(L_);
  luaopen_string(L_);
  choco::script::installGameStringMeta(L_);
  luaopen_table(L_);
  lua_pushnil(L_);  // coroutines are the host's business: a script that yields itself would look dead
  lua_setglobal(L_, "coroutine");
  lua_settop(L_, 0);
  struct Reg {
    const char* name;
    lua_CFunction fn;
  } natives[] = {{"WaitLuaNextFrame", SpriteNatives::waitLuaNextFrame}, {"SetCurrentFrame", SpriteNatives::setCurrentFrame},
                 {"SetAlpha", SpriteNatives::setAlpha}, {"Load", SpriteNatives::load},
                 {"GetScript", SpriteNatives::getScript}, {"Pause", SpriteNatives::pause},
                 {"IsPlaying", SpriteNatives::isPlaying}, {"Die", SpriteNatives::die},
                 {"RemoveSprite", SpriteNatives::removeSprite}, {"SetLayer", SpriteNatives::setLayer},
                 {"Random", SpriteNatives::random}, {"_ALERT", SpriteNatives::alertOrDebug},
                 {"DebugOut", SpriteNatives::alertOrDebug}};
  // The embedded chunk first (it only defines functions), then the natives, then the sheet's own text.
  const std::string_view code = script::embeddedAnimationScript();
  if (luaL_loadbuffer(L_, code.data(), code.size(), "=animationscript") != 0 || lua_pcall(L_, 0, 0, 0) != 0) {
    const char* why = lua_tostring(L_, -1);
    ctx_.log.write(std::string("[sprite-lua] animationscript: ") + (why ? why : "error"));
    failedSetup_ = true;
    return false;
  }
  for (const Reg& r : natives) {
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(L_, r.fn, 1);
    lua_setglobal(L_, r.name);
  }
  const std::string& text = owner_.sheet->script;
  // The top level of the sheet's text runs under the same instruction limit as the coroutine.
  lua_sethook(L_, SpriteNatives::countHook, LUA_MASKCOUNT, kInstructionLimit);
  const bool loadFailed = luaL_loadbuffer(L_, text.data(), text.size(), "=sheet") != 0;
  const bool failed = loadFailed || lua_pcall(L_, 0, 0, 0) != 0;
  lua_sethook(L_, nullptr, 0, 0);
  if (failed) {
    const char* why = lua_tostring(L_, -1);
    fail(why ? why : "error");
    failedSetup_ = true;
    lua_settop(L_, 0);
    return false;
  }
  ready_ = true;
  return true;
}

void SpriteScript::releaseThread() {
  if (threadRef_ != LUA_NOREF) lua_unref(L_, threadRef_);
  threadRef_ = LUA_NOREF;
  thread_ = nullptr;
}

bool SpriteScript::play(const char* fn, double now) {
  now_ = now;
  if (!owner_.sheet || !setup()) return false;
  releaseThread();
  dead_ = false;
  failed_ = false;
  wakeAt_ = -1;
  thread_ = lua_newthread(L_);
  threadRef_ = lua_ref(L_, 1);  // pops the thread and anchors it
  lua_getglobal(thread_, fn);
  if (!lua_isfunction(thread_, -1)) {
    fail(std::string("no function '") + fn + "' in the sheet's script");
    lua_settop(thread_, 0);
    return false;
  }
  resume();
  return !failed_;
}

void SpriteScript::resume() {
  yielded_ = false;
  lua_sethook(thread_, SpriteNatives::countHook, LUA_MASKCOUNT, kInstructionLimit);
  const int status = lua_resume(thread_, 0);
  if (status != 0) {
    fail(lua_tostring(thread_, -1) ? lua_tostring(thread_, -1) : "error");
    lua_settop(thread_, 0);
  } else if (!yielded_) {
    dead_ = true;  // the function returned
  }
  lua_sethook(thread_, nullptr, 0, 0);
}

void SpriteScript::tick(double now) {
  now_ = now;
  if (!thread_ || dead_ || wakeAt_ < 0 || now + 1e-6 < wakeAt_) return;
  resume();
}

}  // namespace choco::sdk
