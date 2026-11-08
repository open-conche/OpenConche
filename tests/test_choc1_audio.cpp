// Chocolatier 1's music queue (docs/re/choc1-natives.md section 6) on a fake clock, and its sound names.
#include <doctest/doctest.h>

#include <set>
#include <string>
#include <vector>

#include "games/choc1/Choc1AudioNatives.h"
#include "games/choc1/Choc1Music.h"

using choco::game::choc1::Choc1Music;
using choco::game::choc1::MusicSink;
using State = Choc1Music::State;

namespace {

// Every track lasts `length` ms of the fake clock. fadeOutAndStop makes playing() false at once, as Audio does.
struct FakeSink : MusicSink {
  double now = 0;
  double length = 10000;
  std::vector<std::string> events;
  std::set<std::string> missing;
  bool isPlaying = false;
  double endsAt = 0;

  bool play(const std::string& path) override {
    if (missing.count(path)) {
      events.push_back("missing " + path);
      return false;
    }
    events.push_back("play " + path + " @" + std::to_string(static_cast<int>(now)));
    isPlaying = true;
    endsAt = now + length;
    return true;
  }
  void fadeOutAndStop(int ms) override {
    events.push_back("fade " + std::to_string(ms) + " @" + std::to_string(static_cast<int>(now)));
    isPlaying = false;
  }
  bool playing() const override { return isPlaying && now < endsAt; }
};

struct Env {
  FakeSink sink;
  Choc1Music music;
  void start(const std::string& f) { music.start(sink, f, sink.now); }
  void queue(const std::string& f, double s) { music.queue(sink, f, s, sink.now); }
  void advanceTo(double t) {
    for (double at = sink.now; at < t;) {
      at = std::min(at + 50, t);
      sink.now = at;
      music.tick(sink, sink.now);
    }
  }
};

}  // namespace

TEST_CASE("Choc1Music: StartMusic plays audio/music/<file> once, the same file again is a no-op") {
  Env e;
  e.start("menu.ogg");
  REQUIRE(e.sink.events.size() == 1);
  CHECK(e.sink.events[0] == "play audio/music/menu.ogg @0");
  CHECK(e.music.state() == State::Playing);
  e.advanceTo(3000);
  e.start("menu.ogg");
  CHECK(e.sink.events.size() == 1);
}

TEST_CASE("Choc1Music: the menu's Start plus two Queue(2 s) loops with two seconds of silence") {
  Env e;
  e.start("menu.ogg");
  e.queue("menu.ogg", 2);
  e.queue("menu.ogg", 2);
  CHECK(e.music.queued() == 2);
  e.advanceTo(10000);  // the first track ends at 10 s: the queue waits 2 s
  CHECK(e.music.state() == State::Waiting);
  e.advanceTo(11950);
  CHECK(e.sink.events.size() == 1);
  e.advanceTo(12000);
  REQUIRE(e.sink.events.size() == 2);
  CHECK(e.sink.events[1] == "play audio/music/menu.ogg @12000");
  e.advanceTo(22000);  // ends at 22 s, the second entry waits until 24 s
  e.advanceTo(24000);
  CHECK(e.sink.events.size() == 3);
  e.advanceTo(40000);  // the queue is empty now: idle
  CHECK(e.music.state() == State::Idle);
  CHECK(e.sink.events.size() == 3);
}

TEST_CASE("Choc1Music: QueueMusic with nothing playing starts at once and ignores the delay") {
  Env e;
  e.queue("gameplay1.ogg", 8);
  REQUIRE(e.sink.events.size() == 1);
  CHECK(e.sink.events[0] == "play audio/music/gameplay1.ogg @0");
}

TEST_CASE("Choc1Music: a different StartMusic fades the playing track over 1000 ms, then starts (no overlap)") {
  Env e;
  e.start("menu.ogg");
  e.advanceTo(2000);
  e.start("gameplay1.ogg");
  REQUIRE(e.sink.events.size() == 2);
  CHECK(e.sink.events[1] == "fade 1000 @2000");
  CHECK(e.music.state() == State::Fading);
  e.advanceTo(2950);
  CHECK(e.sink.events.size() == 2);
  e.advanceTo(3000);
  REQUIRE(e.sink.events.size() == 3);
  CHECK(e.sink.events[2] == "play audio/music/gameplay1.ogg @3000");
  CHECK(e.music.playingFile() == "gameplay1.ogg");
}

TEST_CASE("Choc1Music: StartMusic clears the queue; an empty name fades out and stops everything") {
  Env e;
  e.start("menu.ogg");
  e.queue("a.ogg", 1);
  e.queue("b.ogg", 1);
  e.start("storyloop.ogg");  // the queued entries are dropped
  CHECK(e.music.queued() == 1);  // only the new track, waiting for the fade
  e.advanceTo(1000);
  CHECK(e.music.playingFile() == "storyloop.ogg");
  e.queue("c.ogg", 0);
  e.start("");
  CHECK(e.music.state() == State::Fading);
  CHECK(e.music.queued() == 0);
  e.advanceTo(5000);
  CHECK(e.music.state() == State::Idle);
  CHECK(e.sink.events.back().rfind("fade 1000", 0) == 0);
}

TEST_CASE("Choc1Music: StartMusic during a queue wait starts at once; a missing file is skipped") {
  Env e;
  e.start("menu.ogg");
  e.queue("menu.ogg", 2);
  e.advanceTo(10100);
  CHECK(e.music.state() == State::Waiting);
  e.start("gameplay2.ogg");
  CHECK(e.music.state() == State::Playing);
  CHECK(e.sink.events.back() == "play audio/music/gameplay2.ogg @10100");

  Env m;
  m.sink.missing.insert("audio/music/gone.ogg");
  m.start("menu.ogg");
  m.queue("gone.ogg", 0);
  m.queue("menu.ogg", 0);
  m.advanceTo(10000);
  CHECK(m.sink.events.size() == 3);  // menu, missing gone, menu again
  CHECK(m.music.state() == State::Playing);
}

TEST_CASE("Choc1 sound names: the nine aliases, else audio/sfx/<name>.ogg") {
  using choco::script::choc1SoundPath;
  CHECK(choc1SoundPath("open") == "audio/sfx/window_open.ogg");
  CHECK(choc1SoundPath("close") == "audio/sfx/window_close.ogg");
  CHECK(choc1SoundPath("charopen") == "audio/sfx/character_dlog_open.ogg");
  CHECK(choc1SoundPath("charclose") == "audio/sfx/character_dlog_close.ogg");
  CHECK(choc1SoundPath("boat") == "audio/sfx/ship.ogg");
  CHECK(choc1SoundPath("train") == "audio/sfx/train.ogg");
  CHECK(choc1SoundPath("airship") == "audio/sfx/airship.ogg");
  CHECK(choc1SoundPath("fanfare") == "audio/sfx/medal_fanfare.ogg");
  CHECK(choc1SoundPath("mcfanfare") == "audio/sfx/win_game_fanfare.ogg");
  CHECK(choc1SoundPath("buy") == "audio/sfx/buy.ogg");
  CHECK(choc1SoundPath("").empty());
  CHECK(choco::script::choc1Prefetch().size() == 12);
}
