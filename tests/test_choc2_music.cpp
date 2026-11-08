// Chocolatier 2's music state machine (docs/re/choc2-audio.md). sine_stereo.ogg is half a second long, so every "file"
// below is a 0.5 s clip and a fake clock steps over it.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

#include "ScriptHarness.h"
#include "audio/Audio.h"
#include "games/choc2/Choc2Music.h"
#include "games/Registry.h"
#include "games/choc2/Session.h"
#include "games/choc3/AudioEvents.h"

using namespace choco::audio;
namespace fs = std::filesystem;

namespace {

const fs::path kFixtures = fs::path(CHOCO_TEST_ASSETS) / "audio";

std::vector<uint8_t> readAll(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

bool hasLine(const std::string& dump, const std::string& line) { return dump.find(line + "\n") != std::string::npos; }
bool hasPrefix(const std::string& dump, const std::string& prefix) { return dump.find(prefix) != std::string::npos; }

struct Harness {
  std::vector<std::string> lines;
  Audio audio;
  choco::game::choc2::Choc2Music music;
  explicit Harness(uint32_t seed = 1)
      : audio(
            [](const std::string&) -> std::optional<SoundCache::Bytes> { return readAll(kFixtures / "sine_stereo.ogg"); },
            choco::games::choc3::audioEvents(), [this](const std::string& l) { lines.push_back(l); }, seed) {}
  void set(const std::string& state, const std::string& file = {}) { music.setState(audio, state, file); }
  void step(double t) {
    audio.update(t);
    music.tick(audio);
  }
  int count(const std::string& needle) const {
    return static_cast<int>(std::count_if(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(needle) != std::string::npos; }));
  }
};

}  // namespace

TEST_CASE("Choc2Music: SetMusicState plays the menu track, loops the mini-game tracks and fades out on silent") {
  Harness h;
  h.step(0);
  h.set("normal", "music/gameplay4.ogg");
  CHECK(hasPrefix(h.audio.dump(), "audio music music/gameplay4.ogg"));
  h.step(3000);  // looped well past its 0.5 s
  CHECK(hasPrefix(h.audio.dump(), "audio music music/gameplay4.ogg"));
  h.set("factory_wait");
  CHECK(hasPrefix(h.audio.dump(), "audio music music/minigame_wait.ogg"));
  h.set("");
  CHECK(hasPrefix(h.audio.dump(), "audio music music/minigame.ogg"));
  h.step(9000);
  CHECK(hasPrefix(h.audio.dump(), "audio music music/minigame.ogg"));
  h.set("silent");
  CHECK(hasLine(h.audio.dump(), "audio music none"));
  h.step(11000);
  CHECK(h.audio.mixer().fading() == nullptr);
  h.set("bogus");
  h.set("bogus");
  CHECK(h.count("unknown music state 'bogus'") == 1);
}

TEST_CASE("Choc2Music: a repeated identical state is a no-op and does not rebuild the track") {
  Harness h;
  h.step(0);
  h.set("");
  const MusicTrack* first = h.audio.mixer().current();
  h.step(200);
  h.set("");
  CHECK(h.audio.mixer().current() == first);
  CHECK(h.count("music file music/minigame.ogg") == 1);
  h.set("silent");
  h.set("silent");
  CHECK(h.count("state 'silent'  ignored") == 1);
  h.set("normal", "music/gameplay4.ogg");
  h.set("normal", "music/gameplay4.ogg");
  CHECK(h.count("music file music/gameplay4.ogg") == 1);
  // The rotation keeps what plays: two plain "normal" calls start one gameplay track.
  h.set("normal");
  h.set("normal");
  h.step(300);  // the rotation starts on the next update
  h.set("normal");
  CHECK(h.count("music file music/gameplay") == 2);  // gameplay4 once, one rotation track once
}

TEST_CASE("Choc2Music: a sting plays once, a plain normal during it does not cut it, then the rotation returns") {
  Harness h;
  h.step(0);
  h.set("normal", "music/paris_sting.ogg");
  CHECK(hasPrefix(h.audio.dump(), "audio music music/paris_sting.ogg"));
  h.step(100);
  h.set("normal");  // the port view's call right after the arrival sting started
  CHECK(hasPrefix(h.audio.dump(), "audio music music/paris_sting.ogg"));
  h.step(400);
  CHECK(hasPrefix(h.audio.dump(), "audio music music/paris_sting.ogg"));
  h.step(700);  // ended: the update that reaps it starts the rotation
  const std::string d = h.audio.dump();
  CHECK((hasPrefix(d, "audio music music/gameplay1.ogg") || hasPrefix(d, "audio music music/gameplay2.ogg") ||
         hasPrefix(d, "audio music music/gameplay3.ogg")));
  CHECK(h.count("music file music/paris_sting.ogg once") == 1);
}

TEST_CASE("Choc2Music: the rotation goes on by itself, never repeats a track back to back and never plays gameplay4") {
  Harness h(7);
  h.step(0);
  h.set("normal");
  for (int t = 100; t <= 20000; t += 100) h.step(t);
  std::vector<std::string> played;
  for (const std::string& l : h.lines) {
    const auto at = l.find("music file music/gameplay");
    if (at != std::string::npos) played.push_back(l.substr(at + 25, 1));  // the digit of gameplay<n>
  }
  CHECK(played.size() >= 20);  // a 0.5 s track each
  for (std::size_t i = 0; i < played.size(); ++i) {
    CHECK(played[i] != "4");
    if (i > 0) CHECK(played[i] != played[i - 1]);
  }
}

namespace {

// The digits of the gameplay<n> tracks the rotation started, in order, over 8 s of 0.5 s tracks.
std::string rotationDigits(uint32_t seed, int* draws = nullptr) {
  Harness h(seed);
  h.step(0);
  h.set("normal");
  for (int t = 100; t <= 8000; t += 100) h.step(t);
  std::string digits;
  for (const std::string& l : h.lines) {
    const auto at = l.find("music file music/gameplay");
    if (at != std::string::npos) digits += l[at + 25];
  }
  if (draws) *draws = static_cast<int>(digits.size());
  return digits;
}

}  // namespace

TEST_CASE("Choc2Music: the rotation order is fixed for a seed, differs between seeds, and draws once per track") {
  int draws = 0;
  const std::string a = rotationDigits(3, &draws);
  CHECK(a == "32132323121312");
  CHECK(draws >= 14);
  CHECK(rotationDigits(3) == a);
  CHECK(rotationDigits(4) != a);
}

TEST_CASE("Choc2Music: normal then silent in one frame starts nothing and draws nothing; a sting is left to end first") {
  Harness h(5);
  h.step(0);
  h.set("normal");
  h.set("silent");  // a hidden port: the view's normal, then LPort:SetAmbient's silent
  h.step(100);
  CHECK(h.count("music file") == 0);
  CHECK(hasLine(h.audio.dump(), "audio music none"));
  // The next normal still gives the same first track as a run that never saw the cancelled pair (no RNG draw was burnt).
  h.set("normal");
  h.step(200);
  CHECK(h.count("music file music/gameplay") == 1);
  Harness ref(5);
  ref.step(0);
  ref.set("normal");
  ref.step(200);
  CHECK(h.lines.back() == ref.lines.back());

  // Arriving at a hidden port: the sting plays out, then silence (no rotation).
  Harness g(5);
  g.step(0);
  g.set("normal", "music/amazon_sting.ogg");
  g.set("normal");
  g.set("silent");
  g.step(300);
  CHECK(hasPrefix(g.audio.dump(), "audio music music/amazon_sting.ogg"));
  g.step(900);
  CHECK(hasLine(g.audio.dump(), "audio music none"));
  g.step(3000);
  CHECK(g.count("music file music/gameplay") == 0);
  CHECK(hasLine(g.audio.dump(), "audio music none"));
  g.set("normal");  // a later visit to an ordinary port
  g.step(3100);
  CHECK(g.count("music file music/gameplay") == 1);
}

TEST_CASE("Choc2Music: a state request resumes paused music and leaves the ambience") {
  Harness h;
  h.step(0);
  h.set("");
  h.audio.setMusicPaused(true);
  h.set("");  // RestartGame: no ResumeMusic, only the state
  CHECK_FALSE(h.audio.musicPaused());
  h.audio.setAmbientFile("sfx/port_loop.ogg");
  h.set("silent");
  CHECK(hasLine(h.audio.dump(), "audio env sfx/port_loop.ogg"));
}

TEST_CASE("Choc2Music: SetMusicState from Lua reaches the world's state machine, ticked through afterAudio") {
  choco::test::ScriptEnv e(std::string(CHOCO_TEST_ASSETS) + "/choc2_ledger");
  e.ctx.module = choco::games::findModule("choc2");
  e.ctx.module->attach(e.ctx);
  e.ctx.module->registerNatives(e.lua.L(), e.ctx);
  std::vector<std::string> lines;
  Audio audio(
      [](const std::string&) -> std::optional<SoundCache::Bytes> { return readAll(kFixtures / "sine_stereo.ogg"); },
      choco::games::choc3::audioEvents(), [&](const std::string& l) { lines.push_back(l); }, 1);
  e.ctx.audio = &audio;
  CHECK(e.run("SetMusicState('normal')") == 0);
  audio.update(0);
  e.ctx.gameState->afterAudio(audio);
  const auto n = std::count_if(lines.begin(), lines.end(), [](const std::string& l) { return l.find("music file music/gameplay") != std::string::npos; });
  CHECK(n == 1);
}
