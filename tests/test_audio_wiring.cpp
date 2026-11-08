// Audio: the prefs mapping, the script bridge, the device-less pump and the mixer lock.
#include <algorithm>
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>

#include "audio/Audio.h"
#include "games/choc3/AudioEvents.h"
#include "audio/Prefs.h"
#include "sdk/Users.h"
#include "script/AudioBridge.h"
#include "script/ScriptHost.h"

using namespace choco;
namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> readAll(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// Natives over a throwaway user directory, with an Audio service (log lines kept) answering every file with the sine fixture.
struct WiredEnv {
  explicit WiredEnv(const char* tag, unsigned seed = 1)
      : userDir(fs::temp_directory_path() / (std::string("choco_audiowire_") + tag)),
        fsys(CHOCO_TEST_ASSETS, (fs::remove_all(userDir), fs::create_directories(userDir), userDir.string())),
        ctx(lua, fsys, wm, clock, rng, config, stubs, log),
        audio(
            [](const std::string&) -> std::optional<audio::SoundCache::Bytes> {
              return readAll(fs::path(CHOCO_TEST_ASSETS) / "audio" / "sine_stereo.ogg");
            },
            games::choc3::audioEvents(), [this](const std::string& l) { lines.push_back(l); }, seed) {
    log.setEcho(false);
    log.setKeep(true);
    script::registerUserNatives(lua.L(), ctx);
    script::registerAudioNatives(lua.L(), ctx);
    ctx.audio = &audio;
  }
  ~WiredEnv() { fs::remove_all(userDir); }
  int run(const std::string& code) {
    lua_State* L = lua.L();
    int status = luaL_loadbuffer(L, code.data(), code.size(), "=t");
    if (status == 0) status = lua_pcall(L, 0, 0, 0);
    if (status != 0) err = lua_tostring(L, -1);
    lua_settop(L, 0);
    return status;
  }
  bool dumpHas(const std::string& s) const { return audio.dump().find(s) != std::string::npos; }
  fs::path userDir;
  script::LuaState lua;
  assets::AssetFS fsys;
  ui::WindowManager wm;
  script::Clock clock;
  script::Rng rng{1};
  script::Config config;
  script::StubLog stubs;
  script::Log log;
  script::ScriptContext ctx;
  std::vector<std::string> lines;
  audio::Audio audio;
  std::string err;
};

struct CountingLock : audio::MixerLock {
  void lock() override {
    ++locks;
    held = true;
  }
  void unlock() override {
    ++unlocks;
    held = false;
  }
  int locks = 0, unlocks = 0;
  bool held = false;
};

}  // namespace

TEST_CASE("prefs: per mille to level, with defaults for empty and garbage text, clamped") {
  using audio::levelFromPerMille;
  CHECK(levelFromPerMille("650", 750) == doctest::Approx(0.65f));
  CHECK(levelFromPerMille("0", 750) == 0.0f);
  CHECK(levelFromPerMille("1000", 750) == 1.0f);
  CHECK(levelFromPerMille("", 750) == doctest::Approx(0.75f));
  CHECK(levelFromPerMille("loud", 500) == doctest::Approx(0.5f));
  CHECK(levelFromPerMille("nan", 500) == doctest::Approx(0.5f));
  CHECK(levelFromPerMille("inf", 500) == doctest::Approx(0.5f));
  CHECK(levelFromPerMille("-inf", 500) == doctest::Approx(0.5f));
  CHECK(levelFromPerMille("-20", 500) == 0.0f);
  CHECK(levelFromPerMille("2500", 500) == 1.0f);
}

TEST_CASE("prefs: every per mille value survives level and back, as SaveVolumes stores it") {
  for (int pm = 0; pm <= 1000; ++pm) {
    const float level = audio::levelFromPerMille(std::to_string(pm), 0);
    CHECK(audio::perMilleFromLevel(level) == pm);
  }
}

TEST_CASE("prefs: levelsFromPrefs uses 750/650/500 and unmuted when nothing is stored") {
  const auto l = audio::levelsFromPrefs([](const char*) { return std::string(); });
  CHECK(l.sfx == doctest::Approx(0.75f));
  CHECK(l.music == doctest::Approx(0.65f));
  CHECK(l.env == doctest::Approx(0.5f));
  CHECK_FALSE(l.muted);
  const auto m = audio::levelsFromPrefs([](const char* k) { return std::string(k) == "soundoff" ? "1" : k == std::string("musiclevel") ? "100" : ""; });
  CHECK(m.muted);
  CHECK(m.music == doctest::Approx(0.1f));
  CHECK(m.sfx == doctest::Approx(0.75f));
}

TEST_CASE("SoundEvent: the native reaches Audio, ui_click is direct, and an unknown name is silent and never errors") {
  WiredEnv e("native");
  REQUIRE(e.run("SoundEvent('ui_click'); SoundEvent('no_such_event'); SoundEvent('cadi/ui_click.ogg'); SoundEvent('start_bell')") == 0);
  CHECK(e.dumpHas("cadi/ui_click.ogg=1"));
  CHECK(e.dumpHas("start_bell=1"));
  CHECK(e.stubs.count("SoundEvent") == 0);
  CHECK(e.log.count("[unimplemented]") == 0);
}

TEST_CASE("soundEvent without an audio service keeps the logged stub, once per name") {
  WiredEnv e("stub");
  e.ctx.audio = nullptr;
  script::soundEvent(e.ctx, "start_bell");
  script::soundEvent(e.ctx, "start_bell");
  CHECK(e.stubs.count("SoundEvent.start_bell") == 2);
  CHECK(e.log.count("[unimplemented] SoundEvent start_bell") == 1);
}

TEST_CASE("prefs reach the mixer: selecting a user, ToggleSoundMute and the defaults") {
  WiredEnv e("prefs");
  script::applyAudioPrefs(e.ctx);  // no user yet: the defaults
  CHECK(e.dumpHas("audio levels sfx=0.750 music=0.650 env=0.500 mute=0"));
  REQUIRE(e.run("CreateNewUser('Ann'); CreateNewUser('Bob')") == 0);
  e.ctx.users->setPref("soundlevel", "300");  // Bob
  e.ctx.users->setPref("ambientlevel", "1000");
  REQUIRE(e.run("SetCurrentUser(0)") == 0);  // Ann kept the defaults she was created with
  CHECK(e.dumpHas("audio levels sfx=0.750 music=0.650 env=0.500 mute=0"));
  REQUIRE(e.run("SetCurrentUser(1)") == 0);
  CHECK(e.dumpHas("audio levels sfx=0.300 music=0.650 env=1.000 mute=0"));
  REQUIRE(e.run("ToggleSoundMute()") == 0);
  CHECK(e.ctx.users->pref("soundoff") == "1");
  CHECK(e.dumpHas("mute=1"));
  CHECK(e.audio.mixer().master() == 0.0f);
  REQUIRE(e.run("SetCurrentUser(0)") == 0);  // mute is per user
  CHECK(e.dumpHas("mute=0"));
  REQUIRE(e.run("SetCurrentUser(1); ToggleSoundMute()") == 0);
  CHECK(e.dumpHas("mute=0"));
  CHECK(e.audio.mixer().master() == 1.0f);
}

TEST_CASE("ToggleSoundMute: muting keeps an unsaved slider level, unmuting re-applies the stored prefs") {
  WiredEnv e("mute_unsaved");
  REQUIRE(e.run("CreateNewUser('Ann')") == 0);
  e.audio.setLevels(0.25f, 0.65f, 0.5f, false);  // a slider moved on the open Options screen, not yet stored
  REQUIRE(e.run("ToggleSoundMute()") == 0);
  CHECK(e.dumpHas("audio levels sfx=0.250 music=0.650 env=0.500 mute=1"));
  REQUIRE(e.run("ToggleSoundMute()") == 0);
  CHECK(e.dumpHas("audio levels sfx=0.750 music=0.650 env=0.500 mute=0"));  // the stored soundlevel
}

TEST_CASE("pump mode: update renders the simulated clock into the discard buffer, and a one-shot ends on time") {
  WiredEnv e("pump");
  e.audio.update(0);
  e.audio.event("ui_click");
  REQUIRE(e.audio.mixer().activeVoices() == 1);
  e.audio.update(10);
  CHECK(e.audio.mixer().framesRendered() == 441);
  CHECK(e.audio.mixer().activeVoices() == 1);
  e.audio.update(60000);  // far past the end of the fixture
  CHECK(e.audio.mixer().framesRendered() == 60000u * 441u / 10u);
  CHECK(e.audio.mixer().activeVoices() == 0);
  e.audio.setPump(false);  // a device drives the mixer now
  e.audio.update(70000);
  CHECK(e.audio.mixer().framesRendered() == 60000u * 441u / 10u);
}

TEST_CASE("Audio: the same seed gives the same [audio] log across two runs, through random draws") {
  auto run = [](unsigned seed) {
    WiredEnv e("det", seed);
    e.audio.update(0);
    for (const char* n : {"launch", "launch", "launch", "start_bell", "buy"}) e.audio.event(n);
    e.audio.event("baghdad");  // an ambience with sweeteners: each one is a draw from the audio RNG
    e.audio.update(60000);
    return e.lines;
  };
  const auto a = run(1);
  auto has = [&](const char* s) {
    return std::any_of(a.begin(), a.end(), [&](const std::string& l) { return l.find(s) != std::string::npos; });
  };
  REQUIRE(has("-> random "));     // the layered pick drew
  REQUIRE(has("-> sweetener "));  // and so did the ambience
  CHECK(a == run(1));
  CHECK(a != run(2));  // the log really depends on the stream, so the equality above is not trivial
}

TEST_CASE("Mixer: its lock is held for each main-thread edit, re-entrantly, and never left held") {
  CountingLock lock;
  WiredEnv e("lock");
  e.audio.mixer().setLock(&lock);
  e.audio.event("ui_click");
  CHECK(lock.locks > 0);
  CHECK(lock.locks == lock.unlocks);
  CHECK_FALSE(lock.held);
  const int before = lock.locks;
  e.audio.event("Pause");  // Audio -> setGamePaused -> Mixer::setCadiPaused: one lock
  CHECK(lock.locks > before);
  CHECK(lock.locks == lock.unlocks);
  e.audio.setForeground(false);
  e.audio.setLevels(0.2f, 0.2f, 0.2f, true);
  CHECK(lock.locks == lock.unlocks);
  CHECK_FALSE(lock.held);
  e.audio.mixer().setLock(nullptr);
}

TEST_CASE("focus and game pause: the truth table, and regaining focus keeps a game pause") {
  WiredEnv e("pause");
  CHECK(e.dumpHas("pause=none"));
  e.audio.setGamePaused(true);
  CHECK(e.dumpHas("pause=game"));
  e.audio.setForeground(false);
  CHECK(e.dumpHas("pause=focus+game"));
  e.audio.setForeground(true);
  CHECK(e.dumpHas("pause=game"));
  e.audio.setGamePaused(false);
  e.audio.setForeground(false);
  CHECK(e.dumpHas("pause=focus"));
  e.audio.setForeground(true);
  CHECK(e.dumpHas("pause=none"));
}
