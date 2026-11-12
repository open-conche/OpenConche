// The Great Chocolate Chase, slice 6 (docs/design/greatcc.md section 6): `.anm` version 2 in the engine's reader and the
// animation-script natives (getdelay, playsound, unlock, drop). Synthetic fixtures are built here; the shipped files are only
// read by the last test, and only when the assets are present.
#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "assets/AssetFS.h"
#include "audio/Audio.h"
#include "games/Registry.h"
#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/scene/AnimScriptNatives.h"
#include "gfx/AnmSheet.h"
#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"
#include "platform/Png.h"
#include "script/ScriptHost.h"
#include "sdk/Sprite.h"
#include "sdk/SpriteScript.h"
#include "ScriptHarness.h"

using namespace choco;
using namespace choco::games::greatcc;
namespace fs = std::filesystem;

namespace {

constexpr float kNone = -10000000.0f;

struct Bytes {
  std::string b;
  void tag(const char* t) { b.append(t, 4); }
  void u32(std::uint32_t v) { b.append(reinterpret_cast<const char*>(&v), 4); }
  void f32(float f) {
    std::uint32_t v;
    std::memcpy(&v, &f, 4);
    u32(v);
  }
  void str(const std::string& s) {
    u32(static_cast<std::uint32_t>(s.size()));
    b += s;
  }
};

struct FrameSpec {
  int regX = 0, regY = 0, x = 0, y = 0, w = 10, h = 10;
  float scale = 1;
  bool hasXform = false;
  float xform[6] = {1, 0, 0, 1, 0, 0};
  std::vector<std::tuple<int, int, std::string>> anchors;
};

std::string makeAnm(int format, const std::vector<FrameSpec>& frames, const std::string& script, const std::string& mask = "") {
  Bytes o;
  o.tag("ANIM");
  o.u32(1);
  o.u32(format);
  o.str("C:\\build\\x.xml");
  o.str(".\\*");
  o.tag("IMGN");
  if (format == 2) {
    for (std::uint32_t v : {273u, 266u, 0u, 0u}) o.u32(v);
  } else {
    o.u32(7);
    o.u32(9);
    o.u32(0);
    o.u32(0);
    for (int i = 0; i < 4; ++i) o.f32(kNone);
  }
  o.str("sheet.png");
  o.str(mask);
  o.tag("FRMS");
  o.u32(static_cast<std::uint32_t>(frames.size()));
  for (const FrameSpec& f : frames) {
    o.u32(static_cast<std::uint32_t>(f.regX));
    o.u32(static_cast<std::uint32_t>(f.regY));
    for (int v : {f.x, f.y, f.w, f.h}) o.u32(static_cast<std::uint32_t>(v));
    if (format == 2) o.f32(f.scale);
    for (int k = 0; k < 6; ++k) o.f32(f.hasXform ? f.xform[k] : (k == 0 ? kNone : 0.0f));
    o.str("src.png");
    o.u32(static_cast<std::uint32_t>(f.anchors.size()));
    for (const auto& [ax, ay, an] : f.anchors) {
      o.u32(static_cast<std::uint32_t>(ax));
      o.u32(static_cast<std::uint32_t>(ay));
      o.str(an);
    }
  }
  if (!script.empty()) {
    o.tag("SCRP");
    o.str(script);
  }
  return o.b;
}

}  // namespace

TEST_CASE("anm v2: frames of 13 words, scale as alpha, xform, anchors, mask and script") {
  FrameSpec a;
  a.regX = -3;
  a.regY = 4;
  a.x = 10;
  a.y = 20;
  a.w = 30;
  a.h = 40;
  a.anchors = {{5, static_cast<int>(-6), "righthand"}, {1, 2, "lefthand"}};
  FrameSpec b;
  b.scale = 0.5f;
  b.hasXform = true;
  b.xform[4] = -10;
  b.xform[5] = -17;
  FrameSpec c;  // plain
  const std::string bytes = makeAnm(2, {a, b, c}, "function DoAnim() frames{1} end", "sheet.mask.png");
  gfx::SheetData d;
  std::string err;
  REQUIRE(gfx::AnmSheet::parse(bytes, d, err));
  CHECK(err.empty());
  CHECK(d.image == "sheet.png");
  CHECK(d.mask == "sheet.mask.png");
  CHECK(d.reg == gfx::PointI{0, 0});  // the four IMGN words are not a registration point in v2
  CHECK(d.script == "function DoAnim() frames{1} end");
  REQUIRE(d.frames.size() == 3);
  CHECK(d.frames[0].reg == gfx::PointI{-3, 4});
  CHECK(d.frames[0].src == gfx::RectI{10, 20, 30, 40});
  REQUIRE(d.frames[0].anchors.size() == 2);
  CHECK(d.frames[0].anchors[0].name == "righthand");
  CHECK(d.frames[0].anchors[0].pos == gfx::PointI{5, -6});
  CHECK(d.frames[0].alpha == 1.0f);
  CHECK_FALSE(d.frames[0].hasXform);
  CHECK(d.frames[1].alpha == 0.5f);
  CHECK(d.frames[1].hasXform);
  CHECK(d.frames[1].xform[0] == 1.0f);
  CHECK(d.frames[1].xform[4] == -10.0f);
  CHECK(d.frames[1].xform[5] == -17.0f);
  CHECK_FALSE(d.frames[2].hasXform);
  CHECK(d.warnings.empty());
}

TEST_CASE("anm v2: no script gets the default one; truncation and bad headers are errors") {
  FrameSpec f;
  gfx::SheetData d;
  std::string err;
  REQUIRE(gfx::AnmSheet::parse(makeAnm(2, {f, f}, ""), d, err));
  CHECK(d.script.find("DoAnim") != std::string::npos);
  const std::string whole = makeAnm(2, {f}, "x=1");
  for (std::size_t cut : {std::size_t(3), std::size_t(20), whole.size() - 12}) {
    gfx::SheetData e;
    std::string why;
    CHECK_FALSE(gfx::AnmSheet::parse(std::string_view(whole).substr(0, cut), e, why));
    CHECK_FALSE(why.empty());
  }
  std::string bad = whole;
  bad[4] = 7;  // the first header u32 must be 1
  CHECK_FALSE(gfx::AnmSheet::parse(bad, d, err));
  CHECK_FALSE(gfx::AnmSheet::parse(makeAnm(2, {}, ""), d, err));  // no frames
}

TEST_CASE("anm v1 still parses as before: sheet registration, 12-word frames, warnings for real xforms") {
  FrameSpec a;
  a.regX = 5;
  a.regY = 6;
  a.anchors = {{1, 2, "chair1"}};
  FrameSpec b;
  b.hasXform = true;
  gfx::SheetData d;
  std::string err;
  REQUIRE(gfx::AnmSheet::parse(makeAnm(1, {a, b}, "x=1"), d, err));
  CHECK(d.reg == gfx::PointI{7, 9});
  REQUIRE(d.frames.size() == 2);
  CHECK(d.frames[0].reg == gfx::PointI{5, 6});
  CHECK(d.frames[0].anchors[0].name == "chair1");
  CHECK(d.frames[0].alpha == 1.0f);
  CHECK_FALSE(d.frames[1].hasXform);  // v1 never applies an xform
  CHECK_FALSE(d.warnings.empty());
}

namespace {

void writeFile(const fs::path& p, const std::string& bytes) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << bytes;
}

// Sheets of synthetic v2 files plus the cache stack to load them through.
struct AnmAssets {
  AnmAssets() {
    dir = fs::temp_directory_path() / "choco_greatcc_anm";
    fs::remove_all(dir);
    fs::create_directories(dir / "anim");
    std::vector<unsigned char> px(40 * 10 * 4, 255);
    REQUIRE(platform::writePng((dir / "anim/sheet.png").string(), 40, 10, px.data()));
    FrameSpec f;
    const struct {
      const char* name;
      const char* script;
    } table[] = {
        {"delay", "function DoAnim() d = getdelay() SetCurrentFrame(0) end"},
        {"sound", "function DoAnim() playsound('audio/sfx/macaw.ogg', false) playsound('audio/sfx/loop.ogg', true) end"},
        {"machine", "function DoAnim() frames{1} unlock() frames{2} drop() end"},
        {"badsound", "function DoAnim() playsound() playsound(nil, true) end"},
        {"nomachine", "function DoAnim() unlock() end"},
        {"toplevel", "n = getdelay() function DoAnim() end"},
        {"plain", "function DoAnim() frames{1,2} end"},
    };
    for (const auto& t : table) writeFile(dir / "anim" / (std::string(t.name) + ".anm"), makeAnm(2, {f, f, f}, t.script));
    afs = std::make_unique<assets::AssetFS>(dir.string(), "");
    textures = std::make_unique<gfx::TextureCache>(*afs, nullptr);
    sheets = std::make_unique<gfx::SheetCache>(*afs, *textures);
    log.setEcho(false);
    log.setKeep(true);
  }
  std::shared_ptr<const gfx::SpriteSheet> sheet(const std::string& n) { return sheets->get("anim/" + n); }
  sdk::SpriteContext ctx() { return {*sheets, rng, log}; }
  fs::path dir;
  std::unique_ptr<assets::AssetFS> afs;
  std::unique_ptr<gfx::TextureCache> textures;
  std::unique_ptr<gfx::SheetCache> sheets;
  script::Rng rng{1};
  script::Log log;
};

// A ScriptTarget that is not an sdk::Sprite, to show the interface is enough.
struct FakeTarget : sdk::ScriptTarget {
  std::shared_ptr<const gfx::SpriteSheet> sheet;
  int frame = 0, layer = 0;
  float alpha = 1;
  bool removed = false;
  const gfx::SpriteSheet* scriptSheet() const override { return sheet.get(); }
  void scriptSetSheet(std::shared_ptr<const gfx::SpriteSheet> s) override { sheet = std::move(s); }
  void scriptSetFrame(int i) override { frame = i; }
  void scriptSetAlpha(float a) override { alpha = a; }
  void scriptSetLayer(int l) override { layer = l; }
  void scriptRemove() override { removed = true; }
  void scriptPause(bool, int, double) override {}
  bool scriptIsPlaying(int) const override { return false; }
};

std::string kState() { return std::string(CHOCO_TEST_ASSETS) + "/greatcc/state"; }

std::vector<std::uint8_t> readAll(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

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

// A booted GREATCC context with an audio service (the audio holder is the first base so it outlives the context).
struct WorldEnv : AudioHolder, test::ScriptEnv {
  WorldEnv() : AudioHolder(), ScriptEnv(kState(), scratch()), host(ctx) {
    ctx.audio = &audio;
    ctx.module = games::findModule("greatcc");
    ctx.module->attach(ctx);
    booted = host.boot();
  }
  ~WorldEnv() { wm.destroyAll(); }
  static std::string scratch() {
    const fs::path p = fs::temp_directory_path() / "choco_greatcc_anm_user";
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p.string();
  }
  script::ScriptHost host;
  bool booted = false;
};

}  // namespace

TEST_CASE("greatcc anim natives: getdelay answers the sprite's delay, at the top level and in functions") {
  AnmAssets a;
  WorldEnv e;
  REQUIRE(e.booted);
  unsigned delay = 60;
  FakeTarget t;
  t.sheet = a.sheet("toplevel");
  REQUIRE(t.sheet);
  CHECK(t.sheet->frames[0].alpha == 1.0f);
  sdk::SpriteScript top(t, a.ctx());
  addSpriteScriptNatives(top, e.ctx, [&] { return delay; });
  REQUIRE(top.play("DoAnim", 0));
  lua_getglobal(top.state(), "n");
  CHECK(lua_tonumber(top.state(), -1) == 60);  // the chunk's top level ran with getdelay bound
  lua_pop(top.state(), 1);

  FakeTarget t2;
  t2.sheet = a.sheet("delay");
  sdk::SpriteScript s(t2, a.ctx());
  addSpriteScriptNatives(s, e.ctx, [&] { return delay; });
  delay = 125;
  REQUIRE(s.play("DoAnim", 0));
  lua_getglobal(s.state(), "d");
  CHECK(lua_tonumber(s.state(), -1) == 125);
  lua_pop(s.state(), 1);
  CHECK(a.log.lines().empty());
}

TEST_CASE("greatcc anim natives: playsound reaches the sound manager (group 1000, no dedupe, loop flag)") {
  AnmAssets a;
  WorldEnv e;
  REQUIRE(e.booted);
  FakeTarget t;
  t.sheet = a.sheet("sound");
  REQUIRE(t.sheet);
  sdk::SpriteScript s(t, a.ctx());
  addSpriteScriptNatives(s, e.ctx, [] { return 60u; });
  REQUIRE(s.play("DoAnim", 0));
  CHECK(e.countLines("[audio] voice audio/sfx/macaw.ogg group=sfx loop=0") == 1);
  CHECK(e.countLines("[audio] voice audio/sfx/loop.ogg group=sfx loop=1") == 1);
  CHECK(worldOf(e.ctx)->sound().activeCount() == 2);
}

TEST_CASE("greatcc anim natives: playsound with a bad argument is logged and plays nothing") {
  AnmAssets a;
  WorldEnv e;
  REQUIRE(e.booted);
  FakeTarget t;
  t.sheet = a.sheet("badsound");
  sdk::SpriteScript s(t, a.ctx());
  addSpriteScriptNatives(s, e.ctx, [] { return 60u; });
  REQUIRE(s.play("DoAnim", 0));
  CHECK(worldOf(e.ctx)->sound().activeCount() == 0);
  int n = 0;
  for (const std::string& l : e.log.lines()) n += l.find("[native-error] playsound") != std::string::npos;
  CHECK(n == 2);
}

TEST_CASE("greatcc anim natives: unlock and drop run the machine's callbacks only where they were added") {
  AnmAssets a;
  WorldEnv e;
  REQUIRE(e.booted);
  std::vector<std::string> calls;
  FakeTarget t;
  t.sheet = a.sheet("machine");
  sdk::SpriteScript s(t, a.ctx());
  addMachineCallback(s, "unlock", [&] { calls.push_back("unlock"); });
  addMachineCallback(s, "drop", [&] { calls.push_back("drop"); });
  REQUIRE(s.play("DoAnim", 0));
  CHECK(calls.empty());  // the first frames{1} sleeps
  s.tick(10000);
  s.tick(20000);
  CHECK(calls == std::vector<std::string>{"unlock", "drop"});

  // A sheet whose sprite never got `unlock` fails like the original's missing global: that script ends, logged.
  FakeTarget t2;
  t2.sheet = a.sheet("nomachine");
  sdk::SpriteScript s2(t2, a.ctx());
  CHECK_FALSE(s2.play("DoAnim", 0));
  CHECK_FALSE(a.log.lines().empty());
}

TEST_CASE("greatcc anim natives: sdk::Sprite drives its script through the interface, Pause and IsPlaying reach children") {
  AnmAssets a;
  sdk::Sprite parent;
  sdk::Sprite& kid = parent.addChild(std::make_unique<sdk::Sprite>());
  sdk::SpriteContext sc = a.ctx();
  kid.sheet = a.sheet("plain");
  CHECK_FALSE(parent.scriptIsPlaying(-1));
  REQUIRE(kid.play("DoAnim", sc, 0));
  CHECK(parent.scriptIsPlaying(-1));
  CHECK_FALSE(parent.scriptIsPlaying(0));  // depth 0 is the sprite itself
  parent.scriptPause(true, -1, 0);
  CHECK(kid.scriptOf()->paused());
  parent.scriptPause(false, -1, 100);
  CHECK_FALSE(kid.scriptOf()->paused());
  parent.tickScripts(10000);
  CHECK(kid.frame == 1);
}

namespace {

// Every .anm of one asset set parses, its script loads, and DoAnim (when there is one) runs to its first yield with the four
// natives bound.
void checkAllAnm(const std::string& root, bool pfp) {
  if (root.empty() || !fs::is_directory(root) || (pfp && !fs::exists(fs::path(root) / "assets.pfp"))) {
    MESSAGE("SKIPPED: " << root << " not present");
    return;
  }
  assets::AssetFS afs(root, "");
  REQUIRE(afs.ok());
  const std::vector<std::string> files = afs.listBySuffix(".anm");
  CHECK(files.size() == 250);
  std::string firstError;
  std::vector<std::string> missingImage;
  int v2 = 0, scripted = 0, alphaFrames = 0, xformFrames = 0;
  for (const std::string& f : files) {
    auto bytes = afs.readFile(f);
    gfx::SheetData d;
    std::string err;
    if (!bytes || !gfx::AnmSheet::parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), d, err)) {
      if (firstError.empty()) firstError = f + ": " + err;
      continue;
    }
    ++v2;
    for (const gfx::SheetFrame& fr : d.frames) {
      alphaFrames += fr.alpha != 1.0f;
      xformFrames += fr.hasXform;
    }
    {
      // One shipped sheet names an image that is not there (the .jpg exists instead) and no Lua file uses it.
      const std::string dir = f.substr(0, f.find_last_of('/') + 1);
      if (!afs.exists(dir + d.image)) {
        missingImage.push_back(f);
        continue;
      }
    }
    if (d.script.find("function DoAnim(") == std::string::npos) continue;
    ++scripted;
    script::Rng rng{1};
    script::Log log;
    log.setEcho(false);
    log.setKeep(true);
    assets::AssetFS dummy(root, "");
    gfx::TextureCache textures(dummy, nullptr);
    gfx::SheetCache sheets(dummy, textures);
    FakeTarget t;
    t.sheet = sheets.get(f);
    if (!t.sheet) {
      if (firstError.empty()) firstError = f + ": SheetCache::get failed";
      continue;
    }
    sdk::SpriteScript s(t, {sheets, rng, log});
    s.addNative("getdelay", [](lua_State* L) { lua_pushnumber(L, 60); return 1; });
    s.addNative("playsound", [](lua_State*) { return 0; });
    s.addNative("unlock", [](lua_State*) { return 0; });
    s.addNative("drop", [](lua_State*) { return 0; });
    if (!s.play("DoAnim", 0) && firstError.empty()) firstError = f + ": DoAnim failed to start";
  }
  CHECK(firstError.empty());
  if (!firstError.empty()) MESSAGE(firstError);
  CHECK(missingImage == std::vector<std::string>{"anim/customers/posse/yachtsman/idle_angry.anm"});
  CHECK(v2 == 250);
  CHECK(scripted > 90);
  CHECK(alphaFrames == 4);   // sauce_base frame 27 (0.5) and ui/upsell/anim_puff frames 3-5 (0.75, 0.5, 0.25)
  CHECK(xformFrames == 10);  // sauce_base 27-30, ingred_base 3-5 and anim_puff 3-5
}

}  // namespace

TEST_CASE("Real data: every shipped GREATCC .anm parses and its DoAnim starts (Windows install)") {
  checkAllAnm(CHOCO_GREATCC_ASSETS, true);
}

TEST_CASE("Real data: every shipped GREATCC .anm parses and its DoAnim starts (Mac bundle)") {
  checkAllAnm(CHOCO_GREATCC_MAC_ASSETS, false);
}
