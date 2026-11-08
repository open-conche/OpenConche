// The Macintosh cadilite.spec parser and compiler (docs/design/mac-audio.md s.1, s.2) and the engine kinds it needs.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "audio/Audio.h"
#include "audio/CadiLite.h"

using namespace choco::audio;
namespace fs = std::filesystem;

namespace {

const fs::path kAssets = fs::path(CHOCO_TEST_ASSETS) / "audio";

std::vector<uint8_t> readAll(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::string readText(const fs::path& p) {
  const auto b = readAll(p);
  return std::string(b.begin(), b.end());
}

const EventDef* find(const CadiLiteCompiled& c, const std::string& name) {
  for (const EventDef& d : c.events)
    if (d.name == name) return &d;
  return nullptr;
}

bool anyMessage(const CadiLiteSpec& s, const std::string& needle) {
  return std::any_of(s.messages.begin(), s.messages.end(), [&](const std::string& m) { return m.find(needle) != std::string::npos; });
}

const CadiLiteCompiled& fixture() {
  static const CadiLiteCompiled c = compileCadiLite(readText(kAssets / "cadilite" / "cadilite.spec"));
  return c;
}

}  // namespace

TEST_CASE("CadiLite grammar: blank lines separate events, trailing space is trimmed, loop is any case, commas separate") {
  const auto s = parseCadiLite(":\"a\"  \nplay Sfx a.ogg \t \n   \n:\"b\"\nplay Music b.ogg LoOp\n\n:\"c\"\nplay,Environments,c.ogg,loop");
  REQUIRE(s.blocks.size() == 3);
  CHECK(s.errors == 0);
  CHECK(s.blocks[0].entries.size() == 1);
  CHECK(s.blocks[0].entries[0].file == "a.ogg");
  CHECK(s.blocks[1].entries[0].loop);
  CHECK(s.blocks[2].entries[0].group == "Environments");  // the last event ends at end of file
  CHECK(s.blocks[2].entries[0].loop);
}

TEST_CASE("CadiLite grammar: the first duplicate wins") {
  const auto s = parseCadiLite(":\"a\"\nplay Sfx first.ogg\n\n:\"a\"\nplay Sfx second.ogg\n");
  REQUIRE(s.blocks.size() == 1);
  CHECK(s.blocks[0].entries[0].file == "first.ogg");
  CHECK(s.duplicates == 1);
  CHECK(s.errors == 0);
}

TEST_CASE("CadiLite grammar: a call must name an event defined earlier") {
  const auto s = parseCadiLite(":\"a\"\n\"b\"\nplay Sfx a.ogg\n\n:\"b\"\nplay Sfx b.ogg\n\n:\"c\"\n\"a\"\n\"c\"\n");
  CHECK(s.errors == 2);  // "b" is defined later; "c" calls itself
  CHECK(anyMessage(s, "cadilite.spec:2:"));
  REQUIRE(s.blocks.size() == 3);
  CHECK(s.blocks[0].entries.size() == 1);  // the bad call is dropped, the play stays
  CHECK(s.blocks[2].entries.size() == 1);  // only the call to "a" stays
}

TEST_CASE("CadiLite grammar: a bad directive is skipped with its line number and loading goes on") {
  const auto s = parseCadiLite("junk\n:\"a\"\nplay Sfx\nplay Sfx a.ogg once\nplay Sfx a.ogg loop extra\nstop\nfly away\nplay Sfx ok.ogg\n");
  CHECK(s.errors == 6);
  CHECK(anyMessage(s, "[audio] cadilite.spec:1:"));  // outside an event
  CHECK(anyMessage(s, "cadilite.spec:3:"));
  CHECK(anyMessage(s, "cadilite.spec:7:"));
  REQUIRE(s.blocks.size() == 1);
  REQUIRE(s.blocks[0].entries.size() == 1);
  CHECK(s.blocks[0].entries[0].file == "ok.ogg");
}

TEST_CASE("CadiLite grammar: an event header without a blank line before it is an error; the colon counts in column 0 only") {
  const auto s = parseCadiLite(":\"a\"\nplay Sfx a.ogg\n:\"b\"\nplay Sfx b.ogg\n  :\"c\"\n\n  :\"d\"\n");
  CHECK(s.errors == 3);
  REQUIRE(s.blocks.size() == 1);
  CHECK(s.blocks[0].name == "a");
  CHECK(s.blocks[0].entries.size() == 2);  // line 4 still belongs to a
}

TEST_CASE("CadiLite compile: the fixture rows") {
  const auto& c = fixture();
  CHECK(c.errors == 0);
  CHECK(c.duplicates == 1);
  CHECK(c.unknown == 3);  // stop_menu, stop_win, mainmenu

  const EventDef* click = find(c, "click");
  REQUIRE(click);
  CHECK(click->kind == Kind::Oneshot);
  CHECK(!click->restart);
  CHECK(click->group == Group::Sfx);
  CHECK(click->channelVolume == 1.0f);
  CHECK(click->files == std::vector<std::string>{"click.ogg"});
  REQUIRE(click->fileRes.size() == 1);
  CHECK(click->fileRes[0].primary == "cadi/click.ogg");
  CHECK(click->fileRes[0].fallback.empty());
}

TEST_CASE("CadiLite compile: Sfx loop, restart one-shot, music, ambience, stops, silent and unknown") {
  const auto& c = fixture();
  CHECK(find(c, "hum")->kind == Kind::Loop);

  const EventDef* win = find(c, "win");
  REQUIRE(win);
  CHECK(win->kind == Kind::Oneshot);
  CHECK(win->restart);

  const EventDef* menu = find(c, "menu");
  REQUIRE(menu);
  CHECK(menu->kind == Kind::MusicFile);
  CHECK(menu->loop);
  CHECK(menu->group == Group::Music);
  CHECK(menu->fileRes[0].primary == "cadi/menu.ogg");
  CHECK(find(c, "sting")->kind == Kind::MusicFile);  // it also stops a file that only the Sfx group plays
  CHECK(find(c, "stop_music")->stopGroup == StopGroup::Music);  // extra stops of other groups' files do not matter
  CHECK(!find(c, "sting")->loop);

  const EventDef* bed = find(c, "bed_a");
  REQUIRE(bed);
  CHECK(bed->kind == Kind::AmbienceLoop);
  CHECK(bed->group == Group::Env);
  CHECK(bed->files == std::vector<std::string>{"bed_a.ogg"});
  CHECK(bed->sweeteners.empty());
  CHECK(bed->attackMs == -1);
  CHECK(bed->releaseMs == -1);
  CHECK(find(c, "bed_b")->kind == Kind::AmbienceLoop);

  const EventDef* stopMusic = find(c, "stop_music");
  REQUIRE(stopMusic);
  CHECK(stopMusic->kind == Kind::Stop);
  CHECK(stopMusic->stopGroup == StopGroup::Music);
  // stop_env names both beds, which are every Environments file
  CHECK(find(c, "stop_env")->kind == Kind::Stop);
  CHECK(find(c, "stop_env")->stopGroup == StopGroup::Environments);

  CHECK(find(c, "quiet")->kind == Kind::Silent);  // the group "sfx" is not Sfx

  // A stop that does not cover its group, a lone stop of an Sfx file, and several plays are Unknown.
  CHECK(find(c, "stop_menu")->kind == Kind::Unknown);
  CHECK(find(c, "stop_win")->kind == Kind::Unknown);
  CHECK(find(c, "mainmenu")->kind == Kind::Unknown);
}

TEST_CASE("CadiLite compile: the first duplicate of the fixture is the one kept") {
  const EventDef* click = find(fixture(), "click");
  REQUIRE(click);
  CHECK(click->files[0] == "click.ogg");
  CHECK(std::count_if(fixture().events.begin(), fixture().events.end(), [](const EventDef& d) { return d.name == "click"; }) == 1);
}

TEST_CASE("CadiLite compile: an unknown event warns once at load") {
  const auto c = compileCadiLite(":\"a\"\nplay Sfx a.ogg\nplay Sfx b.ogg\n");
  REQUIRE(c.events.size() == 1);
  CHECK(c.events[0].kind == Kind::Unknown);
  CHECK(c.unknown == 1);
  CHECK(c.messages.size() == 1);
  CHECK(c.messages[0].find("cadilite.spec:1:") != std::string::npos);
}

TEST_CASE("CadiLite compile: a play whose stop names another file is Unknown") {
  const auto c = compileCadiLite(":\"s\"\nstop x.ogg\n\n:\"a\"\n\"s\"\nplay Sfx a.ogg\n\n:\"x\"\nplay Sfx x.ogg\n");
  CHECK(find(c, "a")->kind == Kind::Unknown);
}

// ---- With Audio ----

namespace {

struct Mac {
  std::vector<std::string> lines;
  CadiLiteCompiled compiled = compileCadiLite(readText(kAssets / "cadilite" / "cadilite.spec"));
  EventTable table{compiled.events};
  Audio audio{[](const std::string&) -> std::optional<SoundCache::Bytes> { return readAll(kAssets / "sine_stereo.ogg"); }, table,
              [this](const std::string& l) { lines.push_back(l); }, 1};
  int count(const std::string& needle) const {
    return static_cast<int>(std::count_if(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(needle) != std::string::npos; }));
  }
  bool dumpHas(const std::string& s) const { return audio.dump().find(s) != std::string::npos; }
};

}  // namespace

TEST_CASE("EventTable(vector): the first of two same-named definitions is found") {
  EventDef a, b;
  a.name = b.name = "x";
  a.kind = Kind::Oneshot;
  b.kind = Kind::Loop;
  const EventTable t({a, b});
  REQUIRE(t.lookup("x"));
  CHECK(t.lookup("x")->kind == Kind::Oneshot);
  CHECK(t.aliases().empty());
}

TEST_CASE("Audio: a MusicFile fired twice restarts with a cut each time") {
  Mac m;
  m.audio.event("menu");
  m.audio.event("menu");
  CHECK(m.count("music file cadi/menu.ogg loop fade 0/0") == 2);
  CHECK(m.dumpHas("audio music cadi/menu.ogg"));
  m.audio.event("sting");
  CHECK(m.count("music file cadi/sting.ogg once fade 0/0") == 1);
  CHECK(m.dumpHas("audio music cadi/sting.ogg"));
}

TEST_CASE("Audio: stop_music empties the music; ambience A then B leaves one environment") {
  Mac m;
  m.audio.event("menu");
  m.audio.event("stop_music");
  m.audio.update(10);
  CHECK(m.dumpHas("audio music none"));

  m.audio.event("bed_a");
  CHECK(m.dumpHas("audio env bed_a"));
  m.audio.event("bed_a");
  CHECK(m.count("environment ignored") == 1);
  m.audio.event("bed_b");
  CHECK(m.dumpHas("audio env bed_b"));
  m.audio.event("stop_env");
  CHECK(m.dumpHas("audio env none"));
}

TEST_CASE("Audio: a restart one-shot leaves one voice; a plain one-shot layers") {
  Mac m;
  m.audio.event("win");
  m.audio.event("win");
  m.audio.update(1);
  CHECK(m.audio.mixer().activeVoices() == 1);
  m.audio.event("click");
  m.audio.event("click");
  m.audio.update(2);
  CHECK(m.audio.mixer().activeVoices() == 3);
}

TEST_CASE("Audio: Unknown and Silent spec events are quiet, an undefined name logs once") {
  Mac m;
  m.audio.event("mainmenu");
  m.audio.event("quiet");
  CHECK(m.audio.mixer().activeVoices() == 0);
  m.audio.event("nope");
  m.audio.event("nope");
  CHECK(m.count("unknown event nope") == 1);
  CHECK(m.count("effect unknown") == 1);
}

TEST_CASE("CadiLite: the real Macintosh spec compiles with the music, ambience and restart rows") {
  const fs::path spec = fs::path(CHOCO_MAC_ASSETS) / "cadi" / "cadilite.spec";
  if (!fs::exists(spec)) return;  // the Mac assets are not in this checkout
  const auto c = compileCadiLite(readText(spec));
  CHECK(c.errors == 0);
  CHECK(c.duplicates == 3);
  auto kind = [&](const char* n) {
    const EventDef* d = find(c, n);
    REQUIRE_MESSAGE(d, n);
    return d->kind;
  };
  CHECK(kind("main_menu") == Kind::MusicFile);
  CHECK(find(c, "main_menu")->loop);
  for (const char* n : {"lima_sting", "port_music2", "map_screen", "chocolate_making_game_intro", "chocolate_making_game", "chocolate_making_game_outro"})
    CHECK_MESSAGE(kind(n) == Kind::MusicFile, n);
  CHECK(kind("win_money") == Kind::Oneshot);
  CHECK(find(c, "win_money")->restart);
  CHECK(kind("Stop_Environments") == Kind::Stop);
  CHECK(find(c, "Stop_Environments")->stopGroup == StopGroup::Environments);
  CHECK(kind("stop music") == Kind::Stop);
  CHECK(find(c, "stop music")->stopGroup == StopGroup::Music);
  CHECK(kind("zurich") == Kind::AmbienceLoop);
  std::vector<std::string> unknown;
  for (const EventDef& d : c.events)
    if (d.kind == Kind::Unknown) unknown.push_back(d.name);
  MESSAGE("unknown: " << [&] { std::string s; for (auto& n : unknown) s += n + ";"; return s; }());
  CHECK(unknown == std::vector<std::string>{"stop Music", "stop Sfx", "mainmenu"});
}

// ---- The choc3 native-row merge ----

#include "games/choc3/AudioEvents.h"

TEST_CASE("choc3 Macintosh table: the spec rows plus the native Direct, Volume, Builtin and Silent rows and Stop_Music") {
  std::vector<std::string> messages;
  const EventTable t = choco::games::choc3::macAudioEvents(readText(kAssets / "cadilite" / "cadilite.spec"), &messages);
  CHECK(!messages.empty());
  CHECK(messages.back().find("3 without an engine equivalent") != std::string::npos);
  auto kind = [&](const char* n) {
    const EventDef* d = t.lookup(n);
    REQUIRE_MESSAGE(d, n);
    return d->kind;
  };
  CHECK(kind("menu") == Kind::MusicFile);   // a spec row
  CHECK(kind("ui_click") == Kind::Direct);  // native rows
  CHECK(kind("firework3") == Kind::Direct);
  CHECK(kind("Music Volume") == Kind::Volume);
  CHECK(kind("Pause") == Kind::Builtin);
  CHECK(kind("Unpause") == Kind::Builtin);
  CHECK(kind("cadi/ui_click.ogg") == Kind::Silent);
  CHECK(kind("Stop_Music") == Kind::Stop);
  CHECK(t.lookup("Stop_Music")->stopGroup == StopGroup::Music);
  // Windows-only rows are not carried over.
  CHECK(!t.lookup("launch"));
  CHECK(!t.lookup("machine_spin"));
  CHECK(!t.lookup("Stop_Environments"));  // not in the fixture spec
  // Every name appears once.
  std::set<std::string> names;
  for (const EventDef& d : t.events()) CHECK(names.insert(d.name).second);
}

TEST_CASE("choc3 Macintosh table: a spec event named like a native row wins") {
  const EventTable t = choco::games::choc3::macAudioEvents(":\"ui_click\"\nplay Sfx mine.ogg\n\n:\"Stop_Music\"\nplay Sfx x.ogg\n");
  CHECK(t.lookup("ui_click")->kind == Kind::Oneshot);
  CHECK(t.lookup("Stop_Music")->kind == Kind::Oneshot);
}
