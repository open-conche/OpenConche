#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "assets/AssetFS.h"
#include "assets/Content.h"
#include "script/LuaState.h"
#include "ui/WindowManager.h"

namespace choco::script {

class ScriptHost;
class GameModule;
class GameState;
}  // namespace choco::script
namespace choco::audio {
class Audio;
}
namespace choco::sdk {
class UserStore;
}
namespace choco::script {

// Line log: stdout (flushed per line, so interleaving with crashes is readable) and, optionally, kept for tests.
class Log {
 public:
  void write(const std::string& line) {
    if (echo_) {
      std::fputs(line.c_str(), stdout);
      std::fputc('\n', stdout);
      std::fflush(stdout);
    }
    if (keep_) lines_.push_back(line);
    if (tee_) tee_(line);
  }
  // Every written line also goes here (the --log file). Called whether or not echo is on.
  void setTee(std::function<void(const std::string&)> f) { tee_ = std::move(f); }
  void setEcho(bool v) { echo_ = v; }
  void setKeep(bool v) { keep_ = v; }
  const std::vector<std::string>& lines() const { return lines_; }
  int count(std::string_view prefix) const {
    int n = 0;
    for (const auto& l : lines_) n += l.compare(0, prefix.size(), prefix) == 0;
    return n;
  }
  bool contains(std::string_view part) const {
    for (const auto& l : lines_)
      if (l.find(part) != std::string::npos) return true;
    return false;
  }

 private:
  bool echo_ = true, keep_ = false;
  std::function<void(const std::string&)> tee_;
  std::vector<std::string> lines_;
};

// The script clock in ms. Headless: frame * 1000 / 60. Set by ScriptHost::tick.
struct Clock {
  double nowMs = 0;
  std::uint64_t frame = 0;
};

// The one deterministic generator (RandRange and later the crates game).
class Rng {
 public:
  explicit Rng(std::uint32_t seed = 1) : gen_(seed), side_(seed + 1), fx_(seed + 2) {}
  void seed(std::uint32_t s) {
    gen_.seed(s);
    side_.seed(s + 1);
    fx_.seed(s + 2);
  }
  // The particle effects' own stream (the original's TRandom inside the particle system and libc rand() in the fireworks),
  // seeded from seed + 2, so effects cannot shift the game's rolls. Same inclusive range rule as range().
  int fxRange(int lo, int hi);
  double fxUniform() { return unit(fx_); }
  // The sprite scripts' own stream (the original's platform Rand, not the global TRandom): seeded from seed + 1, so
  // sheet scripts cannot shift the game's rolls. Same inclusive range rule as range().
  int sideRange(int lo, int hi);
  // Inclusive, lo + (int)((hi-lo+1) * u); hi < lo gives lo.
  int range(int lo, int hi);
  // [0, 1) from two raw mt19937 words. The engine's output is specified bit-exactly by the standard, but
  // std::uniform_real_distribution is not, so this keeps seeded runs identical across libc++, libstdc++ and MSVC.
  double uniform() { return unit(gen_); }
  // One raw 32-bit word of the game stream (the original's `Rand32`).
  std::uint32_t next32() { return gen_(); }

 private:
  static double unit(std::mt19937& g) {
    const std::uint64_t hi = g(), lo = g();
    return static_cast<double>(((hi << 32) | lo) >> 11) * (1.0 / 9007199254740992.0);  // 53 bits / 2^53
  }
  std::mt19937 gen_, side_, fx_;
};

// settings.xml keys.
class Config {
 public:
  // settings.xml values; a key given on the command line (--config) keeps its override.
  void set(const std::string& k, const std::string& v) {
    if (!locked_.count(k)) map_[k] = v;
  }
  void override(const std::string& k, const std::string& v) {
    map_[k] = v;
    locked_.insert(k);
  }
  const std::string* find(const std::string& k) const {
    auto it = map_.find(k);
    return it == map_.end() ? nullptr : &it->second;
  }
  std::string get(const std::string& k, const std::string& def = {}) const {
    auto* v = find(k);
    return v ? *v : def;
  }

 private:
  std::map<std::string, std::string> map_;
  std::set<std::string> locked_;
};

// Counts calls to stubbed natives; written once at exit.
class StubLog {
 public:
  // Returns true the first time `name` is hit.
  bool hit(const std::string& name) { return ++counts_[name] == 1; }
  int count(const std::string& name) const {
    auto it = counts_.find(name);
    return it == counts_.end() ? 0 : it->second;
  }
  std::size_t distinct() const { return counts_.size(); }
  // "name<TAB>count" lines, most frequent first (ties by name).
  std::string render() const;
  bool writeFile(const std::string& path) const;

 private:
  std::map<std::string, int> counts_;
};

// Handed to every native as a light-userdata upvalue. Outlives the Lua state's use because its members do.
struct ScriptContext {
  ScriptContext(LuaState& l, assets::AssetFS& f, ui::WindowManager& w, Clock& c, Rng& r, Config& cfg, StubLog& s, Log& lg);
  ~ScriptContext();
  LuaState& lua;
  assets::AssetFS& fs;
  ui::WindowManager& wm;
  Clock& clock;
  Rng& rng;
  Config& config;
  StubLog& stubs;
  Log& log;
  // The game's per-context state (a game::World, or a choc2::Choc2World), made by module->attach(ctx); dies before the
  // window manager and Lua. Reach it through game::worldOf / game::choc2::sessionOf.
  std::unique_ptr<GameState> gameState;
  ScriptHost* host = nullptr;
  // Which game's module runs on this context (script/GameModule.h). Set by the app (and by ScriptEnv in tests, which
  // defaults to Chocolatier 3) before the ScriptHost is built and read-only after; ScriptHost::boot fails without one.
  const GameModule* module = nullptr;
  audio::Audio* audio = nullptr;  // set by the app; null in unit tests and without a script host (script/AudioBridge.h)
  // Set by the app: switches the display mode (and stores the "fullscreen" pref); false if the platform refused.
  // Unset in tests, where ToggleFullScreen only flips the stored pref.
  std::function<bool()> toggleFullscreen;
  // Set by the app: whether the display is fullscreen now (the Options check box reads it). Unset in tests: the stored
  // "fullscreen" pref stands in.
  std::function<bool()> isFullscreen;
  // Set by the app: shows a custom image cursor ("cursor/<name>[?variant]", the hotspot in image pixels - the factory
  // mini-games' aimer and arrow). Returns false when the image or the platform refused it (headless dummy video).
  // Unset in tests: the factory window logs `cursor.<name>` once, as before.
  std::function<bool(const std::string& path, int hotspotX, int hotspotY)> setCursor;
  assets::StringTable strings;  // bsg* / _GetString (strings.xml loaded at boot)
  // True once the Lua side has loaded a non-empty save or stored one through LoadGameString/SaveGameString. The exit save
  // in App.cpp needs it: a Main that died before Player:LoadGame/Reset ran must not save the bare Player literal.
  bool gameStateTouched = false;
  std::shared_ptr<sdk::UserStore> users;  // created by registerUserNatives from the fs user root
  // Stores the "fullscreen" global pref after a toggle (no-op before the user store exists).
  void saveFullscreenPref(bool fullscreen);
  std::set<std::string> warned;  // warn-once keys
  std::vector<std::string> nativeNames;  // every real native registered through bind/bindRaw
};

}  // namespace choco::script
