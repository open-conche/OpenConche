#include <doctest/doctest.h>
#include "platform/Png.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

#include "ScriptHarness.h"
#include "games/choc3/CharWindow.h"
#include "games/common/PortDef.h"
#include "games/common/PortWindow.h"
#include "games/common/sprite/PathSprite.h"
#include "sdk/SpriteScript.h"
#include "gfx/RecordingRenderer.h"
#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"
#include "ui/DrawContext.h"
#include "games/choc3/Choc3Natives.h"

using namespace choco;
namespace fs = std::filesystem;
using game::PathSprite;
using game::PortWindow;
using sdk::Sprite;

namespace {

void writePng(const fs::path& p, int w, int h) {
  fs::create_directories(p.parent_path());
  std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 4, 255);
  REQUIRE(choco::platform::writePng(p.string(), w, h, px.data()));
}

void writeText(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << text;
}

std::string sheetXml(const std::string& image, int frames, const std::string& script) {
  std::string x = "<?xml version=\"1.0\"?><sequence name=\"t\" source=\"t\"><img regx=\"0\" regy=\"0\" name=\"" + image +
                  "\" w=\"0\" h=\"0\"/><framelist>";
  for (int i = 0; i < frames; ++i)
    x += "<frame regx=\"10\" regy=\"10\" name=\"f\" w=\"20\" h=\"20\" x=\"" + std::to_string(i * 20) + "\" y=\"0\"/>";
  x += "</framelist>";
  if (!script.empty()) x += "<script luascript=\"" + script + "\"/>";
  return x + "</sequence>";
}

// A small asset tree: building pictures, a 3-frame sheet per script under test.
struct Assets {
  Assets() {
    dir = fs::temp_directory_path() / "choco_port_assets";
    fs::remove_all(dir);
    writePng(dir / "ports/t/b1.png", 40, 40);
    writePng(dir / "ports/t/b2.png", 40, 40);
    writePng(dir / "ports/t/b3.png", 40, 40);
    writePng(dir / "ports/temp_building.png", 40, 40);
    writePng(dir / "ports/t/car.png", 20, 20);
    writePng(dir / "sheets/anim.png", 60, 20);
    struct S {
      const char* name;
      const char* script;
      int frames;
    };
    // The asset index is built when the AssetFS is made, so every sheet is written first.
    const S table[] = {
      {"d16", "function DoAnim() frames{1,2} end", 3},
      {"fade", "function DoAnim() delay(50) fadeout(4) frames{1,1,1,1} setAlpha(0.5) end", 3},
      {"other", "function DoAnim() SetCurrentFrame(2) end", 3},
      {"main", "function DoAnim() run{'sheets/other.xml'} end", 3},
      {"bad", "function DoAnim() error('boom') end", 3},
      {"loopy", "function DoAnim() while true do end end", 3},
      {"good", "function DoAnim() frames{1,2,3} end", 3},
      {"strmeta", "function DoAnim() SetCurrentFrame(('abc'):len() - 1) end", 3},
      {"lib", "function DoAnim() if math == nil and random(1, 1) == 1 then SetCurrentFrame(2) end end", 3},
      {"par", "function DoAnim() frames{1,2,3} end", 3},
      {"kid", "function DoAnim() while true do frames{1,2} end end", 3},
      {"ctl", "function DoAnim() halt() end", 3},
      {"k2", "function DoAnim() while true do frames{1,2} end end", 3},
      {"ip", "function DoAnim() if IsPlaying() then SetCurrentFrame(1) end Die() frames{2} end", 3},
      {"rnd", "function DoAnim() r1 = Random(0, 1000) r2 = Random(2, 1) SetCurrentFrame(r2) end", 3},
      {"rm", "function DoAnim() SetLayer(7) RemoveSprite() die() frames{3} end", 3},
      {"wave", "function DoAnim() delay(100) while true do frames{1,2,3} end end", 3},
      {"wave", "function DoAnim() delay(100) while true do frames{1,2,3} end end", 3}};
    for (const S& e : table) writeText(dir / "sheets" / (std::string(e.name) + ".xml"), sheetXml("anim.png", e.frames, e.script));
    afs = std::make_unique<assets::AssetFS>(dir.string(), "");
    textures = std::make_unique<gfx::TextureCache>(*afs, nullptr);
    sheets = std::make_unique<gfx::SheetCache>(*afs, *textures);
    log.setEcho(false);
    log.setKeep(true);
  }
  std::shared_ptr<const gfx::SpriteSheet> sheet(const std::string& name) { return sheets->get("sheets/" + name + ".xml"); }
  fs::path dir;
  std::unique_ptr<assets::AssetFS> afs;
  std::unique_ptr<gfx::TextureCache> textures;
  std::unique_ptr<gfx::SheetCache> sheets;
  script::Rng rng{1};
  script::Log log;
  sdk::SpriteContext ctx() { return {*sheets, rng, log}; }
};

std::shared_ptr<ui::MapStyle> tbl(std::initializer_list<std::pair<const char*, ui::MapStyle::Value>> kv) {
  auto t = std::make_shared<ui::MapStyle>();
  for (auto& [k, v] : kv) t->set(k, v);
  return t;
}

std::shared_ptr<ui::MapStyle> point(double x, double y) {
  auto t = std::make_shared<ui::MapStyle>();
  t->append(x).append(y);
  return t;
}

}  // namespace

// ---------------------------------------------------------------- SpriteScript

TEST_CASE("SpriteScript: the default delay is 16 and frames{} sets 0-based frames one delay apart") {
  Assets a;
  Sprite s;
  s.sheet = a.sheet("d16");
  REQUIRE(s.sheet);
  auto c = a.ctx();
  REQUIRE(s.play("DoAnim", c, 100));
  CHECK(s.frame == 0);
  CHECK(s.scriptOf()->wakeAt() == doctest::Approx(116));
  s.tickScripts(115);
  CHECK(s.frame == 0);
  s.tickScripts(116);
  CHECK(s.frame == 1);
  CHECK(s.scriptOf()->wakeAt() == doctest::Approx(132));
  s.tickScripts(132);  // the function returns
  CHECK_FALSE(s.scriptOf()->playing());
  CHECK(s.frame == 1);
}

TEST_CASE("SpriteScript: delay(n), fadeout(4) and setAlpha") {
  Assets a;
  Sprite s;
  s.sheet = a.sheet("fade");
  auto c = a.ctx();
  REQUIRE(s.play("DoAnim", c, 0));
  CHECK(s.alpha == doctest::Approx(0.75));
  CHECK(s.scriptOf()->wakeAt() == doctest::Approx(50));
  for (double t = 50; t <= 150; t += 50) s.tickScripts(t);
  CHECK(s.alpha == doctest::Approx(0.0).epsilon(1e-6));
  s.tickScripts(200);
  CHECK(s.alpha == doctest::Approx(0.5));
}

TEST_CASE("SpriteScript: run{} switches the sheet and runs its function inline") {
  Assets a;
  a.sheet("other");
  Sprite s;
  s.sheet = a.sheet("main");
  auto c = a.ctx();
  REQUIRE(s.play("DoAnim", c, 0));
  CHECK(s.sheet->path == "sheets/other.xml");
  CHECK(s.frame == 2);
}

TEST_CASE("SpriteScript: an error ends only that sprite, and a runaway loop is stopped by the instruction hook") {
  Assets a;
  Sprite bad, loopy, good;
  bad.sheet = a.sheet("bad");
  loopy.sheet = a.sheet("loopy");
  good.sheet = a.sheet("good");
  auto c = a.ctx();
  CHECK_FALSE(bad.play("DoAnim", c, 0));
  CHECK_FALSE(bad.scriptOf()->playing());
  CHECK_FALSE(loopy.play("DoAnim", c, 0));
  CHECK(a.log.contains("ran too long"));
  CHECK(a.log.count("[sprite-lua]") == 2);
  CHECK(good.play("DoAnim", c, 0));
  CHECK(good.scriptOf()->playing());
}

TEST_CASE("SpriteScript: the sprite state has no math, Random(a,a) is a, and the main state has no frames") {
  Assets a;
  Sprite s;
  s.sheet = a.sheet("lib");
  auto c = a.ctx();
  REQUIRE(s.play("DoAnim", c, 0));
  CHECK(s.frame == 2);
  script::LuaState main;
  lua_getglobal(main.L(), "frames");
  CHECK(lua_isnil(main.L(), -1));
  lua_pop(main.L(), 1);
}

TEST_CASE("SpriteScript: the sprite state has the game's string default metatable") {
  Assets a;
  Sprite s;
  s.sheet = a.sheet("strmeta");
  auto c = a.ctx();
  REQUIRE(s.play("DoAnim", c, 0));
  CHECK(s.frame == 2);  // ("abc"):len() - 1
}

TEST_CASE("SpriteScript: Pause, IsPlaying, Die, RemoveSprite and a deferred SetLayer") {
  Assets a;
  Sprite parent;
  parent.sheet = a.sheet("par");
  auto child = std::make_unique<Sprite>(5, "child");
  child->sheet = a.sheet("kid");
  Sprite& kid = parent.addChild(std::move(child));
  auto c = a.ctx();
  REQUIRE(parent.play("DoAnim", c, 0));
  REQUIRE(kid.play("DoAnim", c, 0));
  CHECK(kid.scriptOf()->playing());
  parent.tickScripts(16);
  parent.tickScripts(32);
  parent.tickScripts(48);  // the parent's function ends; the child loops on
  CHECK_FALSE(parent.scriptOf()->playing());
  CHECK(kid.scriptOf()->playing());

  Sprite p2;
  p2.sheet = a.sheet("ctl");
  auto k2 = std::make_unique<Sprite>(0, "k2");
  k2->sheet = a.sheet("k2");
  Sprite& kid2 = p2.addChild(std::move(k2));
  REQUIRE(kid2.play("DoAnim", c, 0));
  REQUIRE(p2.play("DoAnim", c, 0));  // halt() pauses itself and its children
  CHECK(kid2.scriptOf()->paused());
  kid2.tickScripts(1000);
  CHECK(kid2.frame == 0);  // asleep

  Sprite q;
  q.sheet = a.sheet("rm");
  auto holder = std::make_unique<Sprite>(0, "holder");
  Sprite& qq = holder->addChild(std::make_unique<Sprite>(1, "q"));
  Sprite& sib = holder->addChild(std::make_unique<Sprite>(3, "sib"));
  qq.sheet = q.sheet;
  REQUIRE(qq.play("DoAnim", c, 0));
  CHECK(qq.removed);
  CHECK(qq.layer() == 7);
  CHECK_FALSE(qq.scriptOf()->playing());  // die() stopped it: frames{3} after it never ran
  // (the script runs on to its next yield after die(), as the original does; it just never resumes)
  CHECK(holder->children()[0].get() == &qq);  // the re-sort is deferred, not done inside the script
  holder->resortDirty();
  CHECK(holder->children()[0].get() == &sib);
  CHECK(holder->children()[1].get() == &qq);

  // IsPlaying() from inside the running script sees itself.
  Sprite ip;
  ip.sheet = a.sheet("ip");
  REQUIRE(ip.play("DoAnim", c, 0));
  CHECK(ip.frame == 1);
  CHECK_FALSE(ip.scriptOf()->playing());
}

TEST_CASE("SpriteScript: Random draws from its own stream, so sheet scripts do not shift the game's rolls") {
  Assets a;
  script::Rng reference{1};
  std::vector<int> expected;
  for (int i = 0; i < 5; ++i) expected.push_back(reference.range(0, 1000000));
  Sprite s;
  s.sheet = a.sheet("rnd");
  auto c = a.ctx();
  REQUIRE(s.play("DoAnim", c, 0));
  CHECK(s.frame == 2);  // Random(2, 1) keeps the low bound
  CHECK(a.log.count("[sprite-lua] Random(2, 1)") == 1);
  s.play("DoAnim", c, 0);
  CHECK(a.log.count("[sprite-lua] Random(2, 1)") == 1);  // logged once
  for (int i = 0; i < 5; ++i) CHECK(a.rng.range(0, 1000000) == expected[static_cast<std::size_t>(i)]);
  // The side stream is seeded from seed + 1 and is repeatable.
  script::Rng x{7}, y{7};
  for (int i = 0; i < 10; ++i) CHECK(x.sideRange(0, 99) == y.sideRange(0, 99));
}

// ---------------------------------------------------------------- PathSprite

namespace {
gfx::Curve line(float x0, float y0, float x1, float y1) { return gfx::Curve::fromPoints({{x0, y0}, {x0, y0}, {x1, y1}, {x1, y1}}); }
}  // namespace

TEST_CASE("PathSprite: loop wraps, bounce folds at 1, oneway ends at the end and is removed") {
  PathSprite loop;
  loop.setCurve(line(0, 0, 100, 0), 1000, PathSprite::Mode::Loop);
  loop.update(1500);
  CHECK(loop.pos.x == doctest::Approx(50).epsilon(0.01));

  PathSprite bounce;
  bounce.setCurve(line(0, 0, 100, 0), 1000, PathSprite::Mode::Bounce);
  bounce.update(1250);
  CHECK(bounce.pos.x == doctest::Approx(75).epsilon(0.01));
  bounce.update(250);  // m = 1.5 -> 0.5
  CHECK(bounce.pos.x == doctest::Approx(50).epsilon(0.01));

  PathSprite once;
  once.setCurve(line(0, 0, 100, 0), 1000, PathSprite::Mode::OneWay);
  once.update(600);
  CHECK_FALSE(once.removed);
  once.update(600);
  CHECK(once.removed);
  CHECK(once.pos.x == doctest::Approx(100));
}

TEST_CASE("PathSprite: speed uses the previous y, scale uses the new y and extrapolates") {
  PathSprite p;
  p.setCurve(line(0, 0, 0, 200), 1000, PathSprite::Mode::Loop);
  p.setScaling(0, 100, 1, 3);
  p.setSpeed(1, 3);
  p.update(100);  // y was 0: speed 1
  CHECK(p.time() == doctest::Approx(100));
  CHECK(p.pos.y == doctest::Approx(20).epsilon(0.01));
  CHECK(p.scale == doctest::Approx(1.4).epsilon(0.01));
  p.update(100);  // y was 20: speed 1 + 0.2 * 2 = 1.4
  CHECK(p.time() == doctest::Approx(240));
  p.update(1200);  // below yNear: unclamped
  CHECK(p.pos.y > 100);
  CHECK(p.scale > 3);
}

TEST_CASE("PathSprite: the frame for a heading, with 16 frames") {
  CHECK(PathSprite::frameForHeading({0, -1}, 16) == 0);
  CHECK(PathSprite::frameForHeading({1, 0}, 16) == 4);
  CHECK(PathSprite::frameForHeading({0, 1}, 16) == 8);
  CHECK(PathSprite::frameForHeading({-1, 0}, 16) == 12);
  CHECK(PathSprite::frameForHeading({0, 0}, 16) == 4);  // no tangent at the very ends: 90 degrees
}

// ---------------------------------------------------------------- PortDef

TEST_CASE("PortDef: defaults, motion strings, behind and the path-sprite scale") {
  auto port = std::make_shared<ui::MapStyle>();
  port->set("name", std::string("t"));
  auto sprites = std::make_shared<ui::MapStyle>();
  auto path = std::make_shared<ui::MapStyle>();
  path->append(point(0, 0)).append(point(10, 0));
  sprites->append(tbl({{"image", std::string("ports/t/a")}}));
  sprites->append(tbl({{"image", std::string("ports/t/b")}, {"path", path}, {"motion", std::string("bounce")}, {"time", 900.0}}));
  sprites->append(tbl({{"image", std::string("ports/t/c")}, {"motion", std::string("sideways")}, {"behind", tbl({{"layer", 800.0}})}}));
  port->set("sprites", sprites);
  auto blds = std::make_shared<ui::MapStyle>();
  blds->append(tbl({{"name", std::string("b1")}, {"x", 5.0}, {"labelx", 7.0}}));
  port->set("buildings", blds);

  const game::PortDef d = game::parsePort(*port);
  CHECK(d.name == "t");
  CHECK(d.yFar == 0);
  CHECK(d.yNear == 800);
  CHECK(d.scaleFar == 1);
  REQUIRE(d.buildings.size() == 1);
  CHECK(d.buildings[0].x == 5);
  CHECK(d.buildings[0].layer == 0);
  CHECK(d.buildings[0].labelX.value() == 7);
  CHECK_FALSE(d.buildings[0].labelY.has_value());
  REQUIRE(d.sprites.size() == 3);
  CHECK(d.sprites[0].frequency == 100);
  CHECK(d.sprites[0].scale == 1);
  CHECK(d.sprites[0].random);
  CHECK(d.sprites[0].speedFar == 1);
  CHECK_FALSE(d.sprites[0].hasPath);
  CHECK(d.sprites[1].hasPath);
  CHECK(d.sprites[1].path.size() == 2);
  CHECK(d.sprites[1].scale == -1);
  CHECK(d.sprites[1].mode == PathSprite::Mode::Bounce);
  CHECK(d.sprites[2].mode == PathSprite::Mode::Loop);  // unknown strings loop
  CHECK(d.sprites[2].layer == 799);
}

// ---------------------------------------------------------------- PortWindow

namespace {

struct FakeSink : ui::ScriptSink {
  std::vector<std::string> strings;
  void deliver(const ui::Message*, const script::LuaRef*) override {}
  void runInScript(const script::LuaRef&) override {}
  bool callProtected(const script::LuaRef&) override { return true; }
  void runString(std::string_view code) override { strings.emplace_back(code); }
  void setGlobalString(const char*, std::string_view) override {}
};

struct PortFixture {
  PortFixture(std::initializer_list<std::tuple<const char*, double, double, double>> buildings, int labelWidth = 0) {
    wm.setTextures(a.textures.get());
    wm.setSheets(a.sheets.get());
    wm.setScriptSink(&sink);
    auto port = std::make_shared<ui::MapStyle>();
    port->set("name", std::string("t"));
    auto bl = std::make_shared<ui::MapStyle>();
    for (auto& [n, x, y, layer] : buildings)
      bl->append(tbl({{"name", std::string(n)}, {"x", x}, {"y", y}, {"layer", layer}}));
    port->set("buildings", bl);
    style.set("port", port);
    auto w = std::make_unique<PortWindow>(wm, a.rng, a.log);
    win = w.get();
    wm.initWindow(std::move(w), style);
    win->setRect({0, 0, 800, 600});
    if (labelWidth > 0)
      for (auto& [n, x, y, layer] : buildings) {
        auto l = std::make_unique<ui::BSGWindow>(wm);
        l->setName(n);
        l->setAlpha(1);
        l->setRect({0, 0, labelWidth, 18});
        win->adoptFront(std::move(l));
      }
    win->postChildrenInit(style);
  }
  Assets a;
  ui::WindowManager wm;
  FakeSink sink;
  ui::MapStyle style;
  PortWindow* win = nullptr;
};

}  // namespace

TEST_CASE("PortWindow: the highest layer wins a hit, a tie keeps the first, and right and bottom edges are exclusive") {
  PortFixture f({{"b1", 100, 100, 5}, {"b2", 110, 100, 5}, {"b3", 130, 100, 9}});
  REQUIRE(f.win->buildings().size() == 3);
  CHECK(f.win->findBuilding({105, 100}) == 0);   // b1 and b2: a tie keeps the first
  CHECK(f.win->findBuilding({119, 100}) == 2);   // b3 is higher
  CHECK(f.win->findBuilding({125, 100}) == 2);
  CHECK(f.win->findBuilding({79, 100}) == -1);
  CHECK(f.win->findBuilding({80, 100}) == 0);    // left edge inclusive
  CHECK(f.win->findBuilding({90, 120}) == -1);   // the bottom edge (120) is exclusive
  CHECK(f.win->findBuilding({90, 119}) == 0);
  CHECK(f.win->findBuilding({120, 100}) == 2);   // b1's right edge is exclusive; b2 and b3 are there, b3 higher
  CHECK(f.win->findBuilding({149, 100}) == 2);
  CHECK(f.win->findBuilding({150, 100}) == -1);
  CHECK(f.win->findBuilding({82, 120}) == -1);
  CHECK(f.win->findBuilding({82, 119}) == 0);
}

TEST_CASE("PortWindow: hover shows the highlight; clicking runs the exact string; a release elsewhere does nothing") {
  PortFixture f({{"b1", 100, 100, 5}, {"b3", 300, 100, 9}});
  CHECK_FALSE(f.win->onMouseMove(100, 100));  // never consumed
  CHECK(f.win->target() == 0);
  CHECK(f.win->buildings()[0].highlight->visible);
  CHECK(f.win->spriteRoot()->countVisible("highlight:b1") == 1);
  CHECK(f.win->onMouseDown(100, 100));
  CHECK_FALSE(f.win->onMouseUp(300, 100));  // released over another building
  CHECK(f.sink.strings.empty());
  CHECK(f.win->onMouseDown(100, 100));
  CHECK(f.win->onMouseUp(101, 101));
  REQUIRE(f.sink.strings.size() == 1);
  CHECK(f.sink.strings[0] == "gCurrentBuilding=nil;b1:OnClick()");
  CHECK(f.win->spriteRoot()->countVisible("highlight:b1") == 0);
  CHECK_FALSE(f.win->onMouseDown(500, 500));
  f.win->onMouseMove(300, 100);
  CHECK(f.win->target() == 1);
  CHECK(f.win->spriteRoot()->countVisible("highlight:b1") == 0);
}

TEST_CASE("PortWindow: the pulse, the intro fade, rolloverScale and label placement") {
  CHECK(PortWindow::pulse(0).alpha == doctest::Approx(0.8));
  CHECK(PortWindow::pulse(750).alpha == doctest::Approx(0.75));
  CHECK(PortWindow::pulse(750).f == doctest::Approx(0.5));
  CHECK(PortWindow::pulse(1500).alpha == doctest::Approx(0.5));
  CHECK(PortWindow::pulse(2250).alpha == doctest::Approx(0.75));
  CHECK(PortWindow::introBrightness(0) == doctest::Approx(0.25));
  CHECK(PortWindow::introBrightness(500) == doctest::Approx(0.4375));
  CHECK(PortWindow::introBrightness(1500) == doctest::Approx(0.8125));
  CHECK(PortWindow::introBrightness(2050) == doctest::Approx(1));
  CHECK(PortWindow::introLabelAlpha(1500) == doctest::Approx(0.5));
  CHECK(PortWindow::introLabelAlpha(2050) == doctest::Approx(0));
  CHECK(PortWindow::rolloverScaleFor(100, 100) == doctest::Approx(0.2));   // capped at +20%
  CHECK(PortWindow::rolloverScaleFor(1000, 1000) == doctest::Approx(0.05));
  CHECK(PortWindow::rolloverScaleFor(0, 10) == 0);

  PortFixture f({{"b1", 790, 100, 5}, {"b2", 10, 100, 6}}, 60);
  const auto& b = f.win->buildings();
  REQUIRE(b.size() == 2);
  ui::Window* l1 = f.wm.window(b[0].label);
  ui::Window* l2 = f.wm.window(b[1].label);
  REQUIRE(l1);
  REQUIRE(l2);
  CHECK(l1->rect().x == 740);  // clamped to the right edge
  CHECK(l2->rect().x == 0);    // clamped to the left
  CHECK(l1->rect().y == 110);  // y + height / 4

  // The task ticks by the clock: the intro runs at 50 ms steps and labels fade out between 1 and 2 s.
  f.win->frame(50);
  CHECK(f.win->spriteRoot()->tint.r == doctest::Approx(0.25 + 0.75 * 0.025).epsilon(0.001));
  f.win->frame(1500);
  CHECK(l1->alpha() == doctest::Approx(0.5));  // halfway through the fade-out
  f.win->frame(2050);
  CHECK(f.win->spriteRoot()->tint.r == doctest::Approx(1));
  CHECK(l1->alpha() == doctest::Approx(0));
}

TEST_CASE("PortWindow: PausePortAnimations stops the task only") {
  PortFixture f({{"b1", 100, 100, 5}});
  f.win->pauseAnimations(true);
  f.win->frame(500);
  f.win->frame(2000);
  CHECK(f.win->spriteRoot()->tint.r == doctest::Approx(0.25));
  f.wm.setNowMs(2000);
  f.win->pauseAnimations(false);
  f.win->frame(2050);
  CHECK(f.win->spriteRoot()->tint.r == doctest::Approx(1));  // a long pause ends the intro at once
}

TEST_CASE("PortWindow: a building with no picture uses temp_building on a debug grid") {
  PortFixture f({{"missing1", 5, 5, 1}, {"missing2", 5, 5, 2}});
  const auto& b = f.win->buildings();
  REQUIRE(b.size() == 2);
  CHECK(b[0].sprite->pos.x == 50);
  CHECK(b[0].sprite->pos.y == 100);
  CHECK(b[1].sprite->pos.x == 150);
  CHECK(b[1].sprite->pos.y == 120);
  CHECK(b[0].sprite->texture);
}

TEST_CASE("PortWindow: a scenery sheet with several frames plays its script; path sprites move 50 ms a tick") {
  Assets a;
  a.sheet("wave");
  writePng(a.dir / "ports/t/car.png", 20, 20);
  ui::WindowManager wm;
  wm.setTextures(a.textures.get());
  wm.setSheets(a.sheets.get());
  auto port = std::make_shared<ui::MapStyle>();
  port->set("name", std::string("t"));
  auto path = std::make_shared<ui::MapStyle>();
  path->append(point(0, 300)).append(point(0, 300)).append(point(1000, 300)).append(point(1000, 300));
  auto sprites = std::make_shared<ui::MapStyle>();
  sprites->append(tbl({{"image", std::string("sheets/wave.xml")}, {"x", 50.0}, {"y", 60.0}}));
  sprites->append(tbl({{"image", std::string("ports/t/car")}, {"path", path}, {"time", 10000.0}, {"random", 0.0}}));
  port->set("sprites", sprites);
  ui::MapStyle style;
  style.set("port", port);
  auto w = std::make_unique<PortWindow>(wm, a.rng, a.log);
  PortWindow* win = w.get();
  wm.initWindow(std::move(w), style);
  win->postChildrenInit(style);
  REQUIRE(win->pathSprites().size() == 1);
  const Sprite* wave = nullptr;
  for (const auto& c : win->spriteRoot()->children())
    if (c->debugName == "scenery:wave") wave = c.get();
  REQUIRE(wave);
  CHECK(wave->frame == 0);
  win->frame(50);
  win->frame(100);  // the script's first wake is at 100
  CHECK(wave->frame == 1);
  win->frame(200);
  CHECK(wave->frame == 2);
  CHECK(win->pathSprites()[0]->time() == doctest::Approx(150));  // three ticks of 50
  CHECK(win->pathSprites()[0]->pos.x == doctest::Approx(15).epsilon(0.05));

  // PausePortAnimations stops the 50 ms task (the path sprite) but the sprite scripts keep running.
  win->pauseAnimations(true);
  const float pathTime = win->pathSprites()[0]->time();
  const int before = wave->frame;
  win->frame(300);
  win->frame(400);
  CHECK(win->pathSprites()[0]->time() == pathTime);
  CHECK(wave->frame != before);
}

// ---------------------------------------------------------------- CharWindow and SetMood

namespace {
bool haveReforged() { return fs::is_directory(CHOCO_REFORGED_ASSETS); }
}  // namespace

TEST_CASE("CharWindow: the mood frame table for 4 and 3 frames") {
  using game::CharWindow;
  CHECK(CharWindow::moodFrame(4, 0) == 1);
  CHECK(CharWindow::moodFrame(4, 33) == 1);
  CHECK(CharWindow::moodFrame(4, 34) == 2);
  CHECK(CharWindow::moodFrame(4, 66) == 2);
  CHECK(CharWindow::moodFrame(4, 67) == 3);
  CHECK(CharWindow::moodFrame(4, 100) == 3);
  CHECK(CharWindow::moodFrame(3, 0) == 1);
  CHECK(CharWindow::moodFrame(3, 33) == 1);
  CHECK(CharWindow::moodFrame(3, 34) == 0);
  CHECK(CharWindow::moodFrame(3, 66) == 0);
  CHECK(CharWindow::moodFrame(3, 67) == 2);
  CHECK(CharWindow::moodFrame(3, 100) == 2);
  CHECK(CharWindow::moodFrame(1, 50) == -1);
}

TEST_CASE("CharWindow: size, the 200 ms cross-fade, a mid-fade retarget, and SetMood by name") {
  if (!haveReforged()) {
    MESSAGE("SKIPPED: " CHOCO_REFORGED_ASSETS " not present");
    return;
  }
  test::ScriptEnv env(CHOCO_REFORGED_ASSETS);
  gfx::TextureCache textures(env.fs, nullptr);
  gfx::SheetCache sheets(env.fs, textures);
  env.wm.setTextures(&textures);
  env.wm.setSheets(&sheets);
  script::registerPortNatives(env.lua.L(), env.ctx);
  auto make = [&](const char* name, double mood, double scale) {
    ui::MapStyle st;
    st.set("name", std::string(name)).set("happiness", mood).set("scale", scale);
    auto w = std::make_unique<game::CharWindow>(env.wm);
    game::CharWindow* p = w.get();
    env.wm.initWindow(std::move(w), st);
    p->postChildrenInit(st);
    return p;
  };
  auto* a1 = make("main_alex", 50, 1);
  auto* a2 = make("main_alex", 50, 0.5);
  auto* other = make("main_chas", 50, 1);
  CHECK(a1->rect().w == 160);
  CHECK(a1->rect().h == 240);
  CHECK(a2->rect().w == 80);
  CHECK(a1->shownFrame() == 2);  // neutral, at once
  CHECK(a1->targetFrame() == 2);

  env.wm.setNowMs(0);
  REQUIRE(env.run("SetMood('main_alex', 90)") == 0);
  CHECK(a1->targetFrame() == 3);
  CHECK(a2->targetFrame() == 3);
  CHECK(other->targetFrame() == 2);
  CHECK(a1->shownFrame() == 2);
  env.wm.tickWindowAnimations(50);
  CHECK(a1->fadeTimer() == 50);
  env.wm.tickWindowAnimations(100);
  CHECK(a1->fadeTimer() == 100);
  // Retargeting mid-fade restarts the fade from the frame still shown.
  a1->setMood(0);
  CHECK(a1->targetFrame() == 1);
  CHECK(a1->fadeTimer() == 0);
  CHECK(a1->shownFrame() == 2);
  for (double t = 150; t <= 300; t += 50) env.wm.tickWindowAnimations(t);
  CHECK(a2->shownFrame() == 3);  // a2 took 4 ticks: 50, 100, 150, 200
  CHECK(a2->fadeTimer() == 200);
  CHECK(a1->shownFrame() == 1);
  CHECK(env.wm.windowAnimationCount() == 0);
  env.run("SetMood('nobody', 10)");  // no window: nothing happens
}

TEST_CASE("BSGWindow frame: the controls/rollover border is drawn 4 px outside the rectangle, tiled and clipped") {
  if (!haveReforged()) {
    MESSAGE("SKIPPED: " CHOCO_REFORGED_ASSETS " not present");
    return;
  }
  assets::AssetFS afs(CHOCO_REFORGED_ASSETS, "");
  gfx::TextureCache textures(afs, nullptr);
  gfx::SheetCache sheets(afs, textures);
  ui::WindowManager wm;
  wm.setTextures(&textures);
  wm.setSheets(&sheets);
  ui::MapStyle st;
  st.set("frame", std::string("controls/rollover")).set("color", std::vector<double>{1, 0.84, 0.67, 1});
  auto w = std::make_unique<ui::BSGWindow>(wm);
  ui::BSGWindow* win = w.get();
  wm.initWindow(std::move(w), st);
  win->postChildrenInit(st);
  win->setRect({100, 100, 150, 20});
  REQUIRE(win->drawsOutside());
  gfx::RecordingRenderer r;
  ui::drawWindow(*win, ui::DrawContext{r, {0, 0}, 1});
  int corners = 0, tiles = 0;
  bool ul = false;
  for (const auto& c : r.calls()) {
    if (c.kind != gfx::RecordingRenderer::Call::Kind::DrawTexture) continue;
    if (c.dst.w == 4 && c.dst.h == 4) {
      ++corners;
      if (c.dst.x == 96 && c.dst.y == 96) ul = true;
    } else {
      ++tiles;
    }
  }
  CHECK(corners == 4);
  CHECK(ul);
  CHECK(tiles == 3 + 3 + 1 + 1);  // 150 px wide: three 64 px tiles top and bottom; 20 px high: one each side
  // The strips are clipped to the window's span so that the last tile is cut off, not squeezed: the top and bottom runs
  // to x 100..250 (the third tile spans 228..292), the left and right strips to y 100..120.
  std::vector<gfx::RectI> clips;
  for (const auto& c : r.calls())
    if (c.kind == gfx::RecordingRenderer::Call::Kind::PushClip) clips.push_back(c.clip);
  REQUIRE(clips.size() == 4);  // the last is the window clip again, from postDraw
  CHECK(clips[0] == gfx::RectI{100, 100, 150, 20});  // the window itself
  CHECK(clips[1].x == 100);
  CHECK(clips[1].w == 150);
  CHECK(clips[2].y == 100);
  CHECK(clips[2].h == 20);
  bool cutOff = false;
  for (const auto& c : r.calls()) {
    if (c.kind != gfx::RecordingRenderer::Call::Kind::DrawTexture || !(c.dst.w == 64 && c.dst.h == 4)) continue;
    CHECK(c.clipped);
    CHECK(c.activeClip.x == 100);
    CHECK(c.activeClip.w == 150);
    if (c.dst.x + c.dst.w > 250) cutOff = true;  // drawn whole, cut by the clip
  }
  CHECK(cutOff);
  CHECK(r.count(gfx::RecordingRenderer::Call::Kind::PushClip) == r.count(gfx::RecordingRenderer::Call::Kind::PopClip));
}
