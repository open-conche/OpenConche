// The Great Chocolate Chase, slice 5 (docs/design/greatcc.md section 6): the sound manager over the voice handles of
// audio::Audio, the squared gain curve, and the sound natives. Synthetic fixtures only: every .ogg path is answered with the
// repo's tiny sine file, and a path containing "missing" with nothing.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "audio/Audio.h"
#include "games/Registry.h"
#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/audio/GreatCCSound.h"
#include "script/AudioBridge.h"
#include "script/ScriptHost.h"
#include "sdk/Users.h"
#include "ScriptHarness.h"

using namespace choco;
using namespace choco::games::greatcc;
namespace fs = std::filesystem;

namespace {

const std::string kState = std::string(CHOCO_TEST_ASSETS) + "/greatcc/state";

std::vector<uint8_t> readAll(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// An Audio service whose loader answers every path (except those naming "missing" or not ending in .ogg) with the 0.5 s stereo
// sine, logging into `lines`. Headless pump mode: update(nowMs) renders into the discard buffer.
struct AudioHolder {
  AudioHolder()
      : audio(
            [](const std::string& path) -> std::optional<audio::SoundCache::Bytes> {
              if (path.find("missing") != std::string::npos) return std::nullopt;
              return readAll(fs::path(CHOCO_TEST_ASSETS) / "audio" / "sine_stereo.ogg");
            },
            audio::EventTable::empty(), [this](const std::string& l) { lines.push_back(l); }) {
    audio.setSquaredGain(true);
  }
  int countLines(const std::string& needle) const {
    int n = 0;
    for (const std::string& l : lines) n += l.find(needle) != std::string::npos;
    return n;
  }
  std::vector<std::string> lines;
  audio::Audio audio;
};

// The sound manager alone, stepped on a simulated clock.
struct SoundEnv : AudioHolder {
  SoundEnv() : sound(&audio) {}
  void step(double ms) {
    now = ms;
    audio.update(ms);
    sound.update(ms);
  }
  // The voice gain of every live voice of the group, in slot order.
  std::vector<float> gains(audio::Group g) const {
    std::vector<float> out;
    for (int i = 0; i < audio::kMaxVoices; ++i) {
      const audio::Voice& v = audio.mixer().voice(i);
      if (v.active && v.group == g) out.push_back(v.gain);
    }
    return out;
  }
  int activeVoices(audio::Group g) const { return static_cast<int>(gains(g).size()); }
  GreatCCSound sound;
  double now = 0;
};

constexpr const char* kTheme = "audio/music/theme.ogg";

}  // namespace

TEST_CASE("greatcc audio: the gain curve is squared, mute sets the master only") {
  AudioHolder h;
  h.audio.setLevels(0.75f, 0.5f, 0.5f, false);
  CHECK(h.audio.mixer().groupGain(audio::Group::Sfx) == doctest::Approx(0.5625));
  CHECK(h.audio.mixer().groupGain(audio::Group::Direct) == doctest::Approx(0.5625));  // group 0 follows the SFX level
  CHECK(h.audio.mixer().groupGain(audio::Group::Music) == doctest::Approx(0.25));
  CHECK(h.audio.mixer().master() == 1.0f);
  h.audio.setLevels(0.75f, 0.5f, 0.5f, true);
  CHECK(h.audio.mixer().master() == 0.0f);
  CHECK(h.audio.mixer().groupGain(audio::Group::Sfx) == doctest::Approx(0.5625));  // the group levels did not move
  h.audio.setSquaredGain(false);  // the other games keep the level itself
  CHECK(h.audio.mixer().groupGain(audio::Group::Sfx) == doctest::Approx(0.75));
}

TEST_CASE("greatcc audio: PlaySFX has no dedupe, and unusable paths are silent") {
  SoundEnv e;
  CHECK(e.sound.playSfx("audio/sfx/a.ogg", false));
  CHECK(e.sound.playSfx("audio/sfx/a.ogg", false));
  CHECK(e.sound.playSfx("audio/sfx/a.ogg", true));
  CHECK(e.sound.activeCount() == 3);
  CHECK(e.activeVoices(audio::Group::Sfx) == 3);
  CHECK(e.countLines("[audio] voice audio/sfx/a.ogg group=sfx loop=0") == 2);
  CHECK(e.countLines("[audio] voice audio/sfx/a.ogg group=sfx loop=1") == 1);
  CHECK_FALSE(e.sound.playSfx("audio/sfx/buttonclick", false));  // no ".ogg": silent, noted once
  CHECK_FALSE(e.sound.playSfx("audio/sfx/buttonclick", false));
  CHECK(e.countLines("no .ogg extension") == 1);
  CHECK_FALSE(e.sound.playSfx("", false));
  CHECK_FALSE(e.sound.playSfx("audio/sfx/missing.ogg", false));
  CHECK(e.sound.activeCount() == 3);
}

TEST_CASE("greatcc audio: finished voices are reaped; the loop keeps its entry") {
  SoundEnv e;
  e.sound.playSfx("audio/sfx/a.ogg", false);  // 0.5 s
  e.sound.playSfx("audio/sfx/loop.ogg", true);
  e.step(0);
  e.step(250);
  CHECK(e.sound.activeCount() == 2);
  e.step(700);  // the one-shot ended before this update: flagged and killed in it
  CHECK(e.sound.activeCount() == 1);
  CHECK(e.activeVoices(audio::Group::Sfx) == 1);
}

TEST_CASE("greatcc audio: a VO that completes frees the slot, the next starts in the same update before the reap") {
  SoundEnv e;
  e.sound.playVO("audio/vo/one.ogg", false, false);
  e.sound.playVO("audio/vo/two.ogg", false, false);
  e.sound.playVO("audio/vo/three.ogg", false, false);
  CHECK(e.sound.voQueueLength() == 3);
  CHECK(e.sound.activeCount() == 0);  // queued, not playing yet
  e.step(0);                          // VO start: one plays, from the VO queue front
  CHECK(e.sound.voQueueLength() == 2);
  CHECK(e.sound.voPlaying());
  CHECK(e.sound.startedLog() == std::vector<std::string>{"audio/vo/one.ogg"});
  e.step(300);  // still playing: nothing else starts
  CHECK(e.sound.startedLog().size() == 1);
  e.step(700);  // one finished: its callback frees CURVO, so two starts in this very update, and one is reaped
  CHECK(e.sound.startedLog() == (std::vector<std::string>{"audio/vo/one.ogg", "audio/vo/two.ogg"}));
  CHECK(e.sound.activeCount() == 1);
  e.step(1400);
  CHECK(e.sound.startedLog().back() == "audio/vo/three.ogg");
  CHECK(e.sound.voQueueLength() == 0);
  e.step(2100);
  CHECK(e.sound.activeCount() == 0);
  CHECK_FALSE(e.sound.voPlaying());
}

TEST_CASE("greatcc audio: PlayVO interrupt, skipIfBusy and PurgeVOQueue") {
  SoundEnv e;
  e.sound.playVO("audio/vo/one.ogg", false, false);
  e.sound.playVO("audio/vo/two.ogg", false, false);
  e.step(0);
  e.sound.playVO("audio/vo/three.ogg", false, true);  // skipIfBusy and the queue is not empty: dropped
  CHECK(e.sound.voQueueLength() == 1);
  e.sound.playVO("audio/vo/four.ogg", true, false);  // interrupt: the playing one is killed, the queue replaced
  CHECK_FALSE(e.sound.voPlaying());
  CHECK(e.sound.voQueueLength() == 1);
  e.step(16);  // four starts; the killed VO is still in ACTIVE until this update's reap, which found it finished
  CHECK(e.sound.startedLog().back() == "audio/vo/four.ogg");
  CHECK(e.sound.activeCount() == 1);
  e.sound.playVO("audio/vo/missing.ogg", false, false);  // does not load: not queued
  CHECK(e.sound.voQueueLength() == 0);
  e.sound.playVO("audio/vo/five.ogg", false, false);
  e.sound.purgeVOQueue();
  CHECK(e.sound.voQueueLength() == 0);
  CHECK_FALSE(e.sound.voPlaying());
  e.step(32);
  CHECK(e.sound.activeCount() == 0);
}

TEST_CASE("greatcc audio: PlayMusic same track is a no-op, a failed load keeps the old track") {
  SoundEnv e;
  CHECK(e.sound.playMusic(kTheme, true, true));
  CHECK(e.sound.currentMusic() == kTheme);
  CHECK(e.sound.startedLog().size() == 1);
  CHECK(e.sound.playMusic(kTheme, true, true));  // same name: nothing restarts, no new voice, no new fade
  CHECK(e.sound.startedLog().size() == 1);
  CHECK(e.sound.fadeCount() == 1);
  CHECK_FALSE(e.sound.playMusic("audio/music/missing.ogg", true, true));
  CHECK_FALSE(e.sound.playMusic("audio/music/nothing", true, true));
  CHECK_FALSE(e.sound.playMusic("", true, true));
  CHECK(e.sound.currentMusic() == kTheme);
  CHECK(e.sound.fadeCount() == 1);  // the old track was not faded out
  e.step(0);
  e.step(2000);
  CHECK(e.activeVoices(audio::Group::Music) == 1);
  CHECK(e.gains(audio::Group::Music)[0] == doctest::Approx(1.0));
  CHECK(e.countLines("[audio] voice audio/music/theme.ogg group=music loop=1") == 1);
}

TEST_CASE("greatcc audio: the crossfade runs over 1000 ms on the squared volume value") {
  SoundEnv e;
  e.sound.playMusic("audio/music/a.ogg", true, true);
  e.step(0);
  CHECK(e.gains(audio::Group::Music) == std::vector<float>{0.0f});  // fades in from silence
  e.step(500);
  CHECK(e.gains(audio::Group::Music)[0] == doctest::Approx(0.25));  // (0.5)^2
  e.step(1000);
  CHECK(e.gains(audio::Group::Music)[0] == doctest::Approx(1.0));
  CHECK(e.sound.fadeCount() == 0);

  e.sound.playMusic("audio/music/b.ogg", true, true);  // a fades out while b fades in
  e.step(1016);  // the fades latch their start at their first update
  CHECK(e.activeVoices(audio::Group::Music) == 2);
  e.step(1516);
  const auto mid = e.gains(audio::Group::Music);
  REQUIRE(mid.size() == 2);
  CHECK(mid[0] == doctest::Approx(0.25));  // a: (1 - 0.5)^2
  CHECK(mid[1] == doctest::Approx(0.25));  // b: 0.5^2
  e.step(1816);
  const auto late = e.gains(audio::Group::Music);
  REQUIRE(late.size() == 2);
  CHECK(late[0] == doctest::Approx(0.04));  // a: (1 - 0.8)^2
  CHECK(late[1] == doctest::Approx(0.64));  // b: 0.8^2
  e.step(2016);  // the fades reach 1: a is killed, b is at full level
  CHECK(e.activeVoices(audio::Group::Music) == 1);
  CHECK(e.gains(audio::Group::Music)[0] == doctest::Approx(1.0));
  CHECK(e.sound.fadeCount() == 0);
  e.step(2032);  // a's entry is reaped
  CHECK(e.sound.activeCount() == 1);
  CHECK(e.sound.currentMusic() == "audio/music/b.ogg");
}

TEST_CASE("greatcc audio: PlayMusic without fade replaces the track at once, muted skips the fades") {
  SoundEnv e;
  e.sound.playMusic("audio/music/a.ogg", true, false);
  e.step(0);
  CHECK(e.gains(audio::Group::Music)[0] == doctest::Approx(1.0));
  e.sound.playMusic("audio/music/b.ogg", true, false);
  CHECK(e.activeVoices(audio::Group::Music) == 2);  // the old one lives until the next update (a tiny overlap)
  e.step(16);
  CHECK(e.activeVoices(audio::Group::Music) == 1);
  CHECK(e.sound.fadeCount() == 0);

  e.audio.setLevels(0.75f, 0.75f, 0.5f, true);
  e.sound.playMusic("audio/music/c.ogg", true, true);  // muted: no fade, the new one starts at its level, the old one ends
  CHECK(e.sound.fadeCount() == 0);
  e.step(32);
  CHECK(e.activeVoices(audio::Group::Music) == 1);
  CHECK(e.gains(audio::Group::Music)[0] == doctest::Approx(1.0));
}

TEST_CASE("greatcc audio: PauseSounds(true, 1000) freezes the SFX voices and leaves the music playing") {
  SoundEnv e;
  e.sound.playSfx("audio/sfx/a.ogg", true);
  e.sound.playMusic(kTheme, true, false);
  e.sound.pauseSounds(true, GreatCCSound::kSfxGroup);
  for (int i = 0; i < audio::kMaxVoices; ++i) {
    const audio::Voice& v = e.audio.mixer().voice(i);
    if (!v.active) continue;
    CHECK(v.paused == (v.group == audio::Group::Sfx));
  }
  // The paused voice keeps its position while the clock runs; the music moves.
  e.step(0);
  std::size_t sfxPos = 0, musicPos = 0;
  for (int i = 0; i < audio::kMaxVoices; ++i) {
    const audio::Voice& v = e.audio.mixer().voice(i);
    if (v.active) (v.group == audio::Group::Sfx ? sfxPos : musicPos) = v.pos;
  }
  e.step(200);
  for (int i = 0; i < audio::kMaxVoices; ++i) {
    const audio::Voice& v = e.audio.mixer().voice(i);
    if (!v.active) continue;
    if (v.group == audio::Group::Sfx) CHECK(v.pos == sfxPos);
    else CHECK(v.pos > musicPos);
  }
  e.sound.pauseSounds(false, GreatCCSound::kSfxGroup);
  for (int i = 0; i < audio::kMaxVoices; ++i) CHECK_FALSE(e.audio.mixer().voice(i).paused);
  // A negative group pauses everything in ACTIVE (the music too).
  e.sound.pauseSounds(true, -1);
  int paused = 0;
  for (int i = 0; i < audio::kMaxVoices; ++i) paused += e.audio.mixer().voice(i).active && e.audio.mixer().voice(i).paused;
  CHECK(paused == 2);
}

TEST_CASE("greatcc audio: focus loss pauses every voice") {
  SoundEnv e;
  e.sound.playMusic(kTheme, true, false);
  e.audio.setForeground(false);
  CHECK(e.audio.mixer().systemPaused());
  e.audio.setForeground(true);
  CHECK_FALSE(e.audio.mixer().systemPaused());
}

TEST_CASE("greatcc audio: PurgeSounds drops the references and kills nothing") {
  SoundEnv e;
  e.sound.playSfx("audio/sfx/a.ogg", true);
  e.sound.playMusic(kTheme, true, false);
  e.step(0);
  e.sound.purgeSounds();
  CHECK(e.sound.activeCount() == 0);
  CHECK(e.sound.currentMusic() == kTheme);
  CHECK(e.audio.voiceCount() == 2);  // both still play
  e.step(16);
  CHECK(e.audio.voiceCount() == 2);
  // The faded-out track of a later change is stopped when its fade ends, although it left ACTIVE.
  e.sound.playMusic("audio/music/b.ogg", true, true);
  e.sound.purgeSounds();
  e.step(100);
  e.step(1200);
  CHECK(e.activeVoices(audio::Group::Music) == 1);
}

TEST_CASE("greatcc audio: a stopped voice's handle reads as done and is never reused") {
  AudioHolder h;
  const auto a = h.audio.playVoice("x.ogg", audio::Group::Sfx, true);
  REQUIRE(a > 0);
  CHECK_FALSE(h.audio.voiceDone(a));
  h.audio.setVoiceGain(a, 0.5f);
  h.audio.stopVoice(a);
  CHECK(h.audio.voiceDone(a));
  const auto b = h.audio.playVoice("x.ogg", audio::Group::Sfx, true);
  CHECK(b != a);
  CHECK(h.audio.voiceDone(a));
  CHECK_FALSE(h.audio.voiceDone(b));
  CHECK(h.audio.playVoice("missing.ogg", audio::Group::Sfx, false) == 0);
  CHECK(h.audio.voiceDone(0));
}

namespace {

// The natives over a booted GREATCC context with a real user directory and an audio service. The audio holder is the first
// base so it outlives the context (the world's destructor stops its voices).
struct NativeEnv : AudioHolder, test::ScriptEnv {
  explicit NativeEnv(const std::string& tag)
      : AudioHolder(), ScriptEnv(kState, scratch(tag)), host(ctx) {
    ctx.audio = &audio;
    ctx.module = games::findModule("greatcc");
    ctx.module->attach(ctx);
    booted = host.boot();
  }
  ~NativeEnv() { wm.destroyAll(); }
  static std::string scratch(const std::string& tag) {
    const fs::path p = fs::temp_directory_path() / ("choco_greatcc_audio_" + tag);
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p.string();
  }
  GreatCCWorld& world() { return *worldOf(ctx); }
  script::ScriptHost host;
  bool booted = false;
};

}  // namespace

TEST_CASE("greatcc audio: the sound natives drive the sound manager") {
  NativeEnv e("natives");
  REQUIRE(e.booted);
  REQUIRE(e.run("PlayMusic('audio/music/theme.ogg', true, true)") == 0);
  CHECK(e.world().sound().currentMusic() == "audio/music/theme.ogg");
  REQUIRE(e.run("PlayMusic('audio/music/theme.ogg', true, true)") == 0);
  CHECK(e.countLines("[audio] voice audio/music/theme.ogg") == 1);
  REQUIRE(e.run("PlaySFX('audio/sfx/a.ogg', false); PlaySFX('audio/sfx/a.ogg')") == 0);  // a missing loop is false
  CHECK(e.world().sound().activeCount() == 3);
  REQUIRE(e.run("PauseSounds(true, 1000)") == 0);
  int pausedSfx = 0, pausedMusic = 0;
  for (int i = 0; i < audio::kMaxVoices; ++i) {
    const audio::Voice& v = e.audio.mixer().voice(i);
    if (v.active && v.paused) (v.group == audio::Group::Sfx ? pausedSfx : pausedMusic)++;
  }
  CHECK(pausedSfx == 2);
  CHECK(pausedMusic == 0);
  REQUIRE(e.run("PurgeSounds(); PurgeVOQueue(); PlayVO('audio/vo/one.ogg', false, false)") == 0);
  CHECK(e.world().sound().activeCount() == 0);
  CHECK(e.world().sound().voQueueLength() == 1);
}

TEST_CASE("greatcc audio: volumes come from the user's prefs; mute touches the master only") {
  NativeEnv e("volumes");
  REQUIRE(e.booted);
  script::applyAudioPrefs(e.ctx);  // what the app does after boot: no user yet, so the profile defaults
  CHECK(e.audio.sfxLevel() == doctest::Approx(0.75));
  CHECK(e.audio.musicLevel() == doctest::Approx(0.75));
  CHECK(e.audio.mixer().groupGain(audio::Group::Music) == doctest::Approx(0.5625));
  REQUIRE(e.run("CreateNewUser('Ada'); CreateNewUser('Bo')") == 0);
  e.ctx.users->setPref("soundlevel", "400");
  e.ctx.users->setPref("musiclevel", "900");
  REQUIRE(e.run("SetCurrentUser(0)") == 0);  // Ada has the defaults
  CHECK(e.audio.sfxLevel() == doctest::Approx(0.75));
  REQUIRE(e.run("SetCurrentUser(1)") == 0);  // Bo
  CHECK(e.audio.sfxLevel() == doctest::Approx(0.4));
  CHECK(e.audio.musicLevel() == doctest::Approx(0.9));
  CHECK(e.audio.mixer().groupGain(audio::Group::Sfx) == doctest::Approx(0.16));
  CHECK(e.audio.mixer().groupGain(audio::Group::Music) == doctest::Approx(0.81));

  // An unsaved slider move, then mute: the master is zeroed and the levels stay; unmute does not re-apply the prefs.
  e.audio.setLevels(0.3f, 0.6f, 0.5f, false);
  REQUIRE(e.run("ToggleSoundMute()") == 0);
  CHECK(e.ctx.users->pref("soundoff") == "1");
  CHECK(e.audio.muted());
  CHECK(e.audio.mixer().master() == 0.0f);
  CHECK(e.audio.sfxLevel() == doctest::Approx(0.3));
  CHECK(e.audio.musicLevel() == doctest::Approx(0.6));
  REQUIRE(e.run("ToggleSoundMute()") == 0);
  CHECK(e.ctx.users->pref("soundoff") == "0");
  CHECK_FALSE(e.audio.muted());
  CHECK(e.audio.mixer().master() == 1.0f);
  CHECK(e.audio.sfxLevel() == doctest::Approx(0.3));  // the stored 400 was not re-applied
  CHECK(e.audio.musicLevel() == doctest::Approx(0.6));
  // Muted users stay muted when selected.
  e.ctx.users->setPref("soundoff", "1");
  REQUIRE(e.run("SetCurrentUser(0); SetCurrentUser(1)") == 0);
  CHECK(e.audio.muted());
}

TEST_CASE("greatcc audio: button sounds go through the world's path rules") {
  NativeEnv e("button");
  REQUIRE(e.booted);
  e.wm.playSound("audio/sfx/buttonclick.ogg", false);
  CHECK(e.countLines("[audio] file audio/sfx/buttonclick.ogg") == 1);
  e.wm.playSound("audio/sfx/buttonclick", false);  // hiscoresubmit.lua's missing extension: silent
  CHECK(e.countLines("[audio] file audio/sfx/buttonclick") == 1);
}

// ---- assets.xml sounds at level load (slice 9) ----

TEST_CASE("preload decodes a usable .ogg into the cache once, and skips an empty path, a missing file and a path without .ogg") {
  SoundEnv e;
  CHECK(e.sound.preload("audio/sfx/a.ogg"));
  CHECK(e.sound.preload("audio/sfx/a.ogg"));  // already cached: still true, nothing new to decode
  CHECK_FALSE(e.sound.preload(""));
  CHECK_FALSE(e.sound.preload("audio/sfx/missing.ogg"));
  CHECK_FALSE(e.sound.preload("audio/sfx/b.wav"));
  CHECK(e.sound.activeCount() == 0);  // a preload plays nothing
  CHECK(e.sound.playSfx("audio/sfx/a.ogg", false));
  CHECK(e.sound.activeCount() == 1);
}
