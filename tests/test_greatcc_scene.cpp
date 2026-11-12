// The Great Chocolate Chase, slice 7 (docs/design/greatcc.md section 6): the scene core. Synthetic fixtures only: a sheet
// image and `.anm` files built here, drawn through the recording renderer.
#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "assets/AssetFS.h"
#include "games/Registry.h"
#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/scene/Digit.h"
#include "games/greatcc/scene/Effects.h"
#include "games/greatcc/scene/Mat3.h"
#include "games/greatcc/scene/SpriteManager.h"
#include "games/greatcc/scene/TextSprite.h"
#include "games/greatcc/scene/TiledObject.h"
#include "games/greatcc/scene/VObject.h"
#include "games/greatcc/scene/VSprite.h"
#include "games/greatcc/scene/VSpriteWindow.h"
#include "gfx/RecordingRenderer.h"
#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"
#include "platform/Png.h"
#include "script/LuaStyle.h"
#include "GreatccAnm.h"
#include "ScriptHarness.h"
#include "ui/Style.h"

using namespace choco;
using namespace choco::games::greatcc;
namespace fs = std::filesystem;

namespace {

using MS = ui::MapStyle;
using Tbl = std::shared_ptr<MS>;
Tbl tbl() { return std::make_shared<MS>(); }

using greatcc_test::FrameSpec;
using greatcc_test::kNone;
using greatcc_test::makeAnm;

void writeFile(const fs::path& p, const std::string& bytes) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << bytes;
}

// The asset tree: a 40x10 sheet image and the `.anm` files, with the cache stack over it.
struct SceneAssets {
  SceneAssets() {
    dir = fs::temp_directory_path() / "choco_greatcc_scene";
    fs::remove_all(dir);
    fs::create_directories(dir / "anim");
    std::vector<unsigned char> px(40 * 10 * 4, 255);
    REQUIRE(platform::writePng((dir / "anim/sheet.png").string(), 40, 10, px.data()));
    REQUIRE(platform::writePng((dir / "anim/plain.png").string(), 30, 20, std::vector<unsigned char>(30 * 20 * 4, 255).data()));
    FrameSpec a;  // frame 0: reg (2,3); anchor "hand"
    a.regX = 2;
    a.regY = 3;
    a.x = 0;
    a.anchors = {{5, -6, "hand"}};
    FrameSpec b;  // frame 1: half alpha
    b.x = 10;
    b.scale = 0.5f;
    FrameSpec c;  // frame 2: a pure translation xform
    c.x = 20;
    c.hasXform = true;
    c.xform[4] = -10;
    c.xform[5] = -17;
    FrameSpec d;  // frame 3
    d.x = 30;
    const char* three = "function DoAnim() delay(60) frames{1,2,3} end function Other() frames{4} end function Pair() frames{1,2,3} end function Slow() WaitLuaNextFrame(100) SetCurrentFrame(1) end";
    writeFile(dir / "anim/hero.anm", makeAnm({a, b, c, d}, three));
    writeFile(dir / "anim/swap.anm",
              makeAnm({a, b}, "function DoAnim() Load('anim/blink', '', '') WaitLuaNextFrame(10) SetCurrentFrame(1) end"));
    writeFile(dir / "anim/reent.anm", makeAnm({a, b, c, d}, "function DoAnim() boom() frames{2} end function Other() frames{4} end"));
    writeFile(dir / "anim/still.anm", makeAnm({a}, "function DoAnim() frames{1} end"));
    writeFile(dir / "anim/blink.anm", makeAnm({a, b}, "function DoAnim() frames{1} end function DoFlame() frames{2} end"));
    afs = std::make_unique<assets::AssetFS>(dir.string(), "");
    textures = std::make_unique<gfx::TextureCache>(*afs, nullptr);
    sheets = std::make_unique<gfx::SheetCache>(*afs, *textures);
  }
  std::shared_ptr<const gfx::SpriteSheet> sheet(const std::string& n) { return sheets->get("anim/" + n); }
  fs::path dir;
  std::unique_ptr<assets::AssetFS> afs;
  std::unique_ptr<gfx::TextureCache> textures;
  std::unique_ptr<gfx::SheetCache> sheets;
};

// A GREATCC context (the world attached, fixbugs as asked) with the sheet stack installed on the window manager.
struct Scene : SceneAssets, test::ScriptEnv {
  explicit Scene(bool fixBugs = true) : SceneAssets(), ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/greatcc/state", scratch()) {
    config.set("fixbugs", fixBugs ? "1" : "0");
    ctx.module = games::findModule("greatcc");
    ctx.module->attach(ctx);
    wm.setSheets(sheets.get());
    wm.setTextures(textures.get());
    wm.pushModal("m");
  }
  static std::string scratch() {
    const fs::path p = fs::temp_directory_path() / "choco_greatcc_scene_user";
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p.string();
  }
  std::shared_ptr<VSprite> sprite(const std::string& sheetName, std::string name = {}) {
    auto s = std::make_shared<VSprite>(ctx, std::move(name));
    s->setSheet(sheet(sheetName));
    return s;
  }
  SpriteManager mgr{ctx};
  gfx::RecordingRenderer rr;
  // Draws the manager at the origin and returns the geometry calls of that pass.
  std::vector<gfx::RecordingRenderer::Call> drawn(gfx::PointF origin = {}) {
    rr.clear();
    mgr.draw(rr, origin);
    std::vector<gfx::RecordingRenderer::Call> out;
    for (const auto& c : rr.calls())
      if (c.kind == gfx::RecordingRenderer::Call::Kind::DrawGeometry) out.push_back(c);
    return out;
  }
  void at(double now) {
    mgr.setNow(now);
    mgr.updateObjects();
  }
};

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
bool nearPt(gfx::PointF p, float x, float y) { return near(p.x, x) && near(p.y, y); }

}  // namespace

// ---- matrix and drawing ----

TEST_CASE("scene matrix: rotation about a point, composition order, row-vector translation") {
  const Mat3 r = Mat3::rotationAbout(10, 0, static_cast<float>(M_PI / 2));
  CHECK(nearPt(r.apply({10, 0}), 10, 0));  // the centre stays
  CHECK(nearPt(r.apply({20, 0}), 10, 10));  // a quarter turn clockwise on a y-down screen
  const Mat3 t = Mat3::translation(5, 7);
  const Mat3 s = Mat3::scaling(2, 3);
  CHECK(nearPt((t * s).apply({1, 1}), 12, 24));  // translate first, then scale
  CHECK(nearPt((s * t).apply({1, 1}), 7, 10));
}

TEST_CASE("scene draw: registration, flips and the sprite's matrix place the frame quad") {
  Scene e;
  auto s = e.sprite("hero");
  s->setPosition({100, 50});
  e.mgr.addObject(s);
  auto calls = e.drawn();
  REQUIRE(calls.size() == 1);
  const auto& v = calls[0].vertices;
  REQUIRE(v.size() == 4);
  // frame 0: 10x10, registration (2,3): the quad's corner is at -reg from the position
  CHECK(nearPt(v[0].pos, 98, 47));
  CHECK(nearPt(v[2].pos, 108, 57));
  // a horizontal flip mirrors the image inside the box and moves the origin to W - regX
  s->setFlip(true, false);
  calls = e.drawn();
  CHECK(nearPt(calls[0].vertices[0].pos, 92, 47));
  CHECK(calls[0].vertices[0].uv.x > calls[0].vertices[1].uv.x);
  s->setFlip(false, true);
  calls = e.drawn();
  CHECK(nearPt(calls[0].vertices[0].pos, 98, 43));  // origin y = H - regY = 7
  CHECK(calls[0].vertices[0].uv.y > calls[0].vertices[3].uv.y);
  // scale and rotation act about the registration point (the origin of the quad)
  s->setFlip(false, false);
  s->setScale(2, 2);
  calls = e.drawn();
  CHECK(nearPt(calls[0].vertices[0].pos, 96, 44));
  CHECK(nearPt(calls[0].vertices[2].pos, 116, 64));
  s->setScale(1, 1);
  s->setRotation(0, 0, static_cast<float>(M_PI / 2));
  calls = e.drawn();
  CHECK(nearPt(calls[0].vertices[0].pos, 103, 48));  // (-2,-3) turned a quarter: (3,-2), then the position
}

TEST_CASE("scene draw: a frame's scale word is its alpha, its xform shifts the quad before the sprite's transform") {
  Scene e;
  auto s = e.sprite("hero");
  s->setPosition({100, 50});
  e.mgr.addObject(s);
  s->setCurrentFrame(1);
  auto calls = e.drawn();
  REQUIRE(calls.size() == 1);
  CHECK(near(calls[0].vertices[0].color.a, 0.5f));
  CHECK(near(calls[0].vertices[0].pos.x, 100));  // frame 1 has no registration point and no xform
  s->setAlpha(0.5f);
  calls = e.drawn();
  CHECK(near(calls[0].vertices[0].color.a, 0.25f));  // the sprite's alpha and the frame's multiply
  s->setAlpha(1);
  s->setCurrentFrame(2);
  calls = e.drawn();
  CHECK(nearPt(calls[0].vertices[0].pos, 90, 33));  // the (-10,-17) translation
  // the xform comes first: scaling the sprite scales the shift too
  s->setScale(2, 2);
  calls = e.drawn();
  CHECK(nearPt(calls[0].vertices[0].pos, 80, 16));
  // bounds and hit areas ignore both
  CHECK(nearPt({s->getRect(DrawSpec{}, 0).x, s->getRect(DrawSpec{}, 0).y}, 100, 50));
  CHECK(near(s->getRect(DrawSpec{}, 0).w, 20));
}

TEST_CASE("scene draw: a plain texture is centred unless the state overrides the registration point") {
  Scene e;
  auto s = std::make_shared<VSprite>(e.ctx, "plain");
  s->setPlainTexture(e.textures->load("anim/plain.png"));
  REQUIRE(s->plainTexture());
  s->setPosition({100, 100});
  e.mgr.addObject(s);
  auto calls = e.drawn();
  REQUIRE(calls.size() == 1);
  CHECK(nearPt(calls[0].vertices[0].pos, 85, 90));
  s->setRegOverride({0, 0});
  calls = e.drawn();
  CHECK(nearPt(calls[0].vertices[0].pos, 100, 100));
}

// ---- order ----

TEST_CASE("scene order: top level by depth then y; children below layer 0 before the image; layers stable") {
  Scene e;
  auto a = e.sprite("still", "a");
  auto b = e.sprite("still", "b");
  auto c = e.sprite("still", "c");
  auto d = e.sprite("still", "d");
  a->setDepth(1);
  b->setDepth(0);
  b->setPosition({0, 300});
  c->setDepth(0);
  c->setPosition({0, 100});
  d->setDepth(-5);
  for (const auto& s : {a, b, c, d}) e.mgr.addObject(s);
  e.drawn();
  std::string order;
  for (VSprite* s : e.mgr.lastDrawOrder()) order += s->name();
  CHECK(order == "dcba");  // depth -5, then depth 0 by y (100, 300), then depth 1

  // children: layer < 0 draws first (behind the parent's own image), equal layers keep insertion order
  Scene f;
  auto p = f.sprite("still", "p");
  auto k1 = f.sprite("blink", "k1");
  auto k2 = f.sprite("blink", "k2");
  auto k3 = f.sprite("blink", "k3");
  k1->setLayer(2);
  k2->setLayer(-1);
  k3->setLayer(2);
  p->addChild(k1);
  p->addChild(k2);
  p->addChild(k3);
  REQUIRE(p->children().size() == 3);
  CHECK(p->children()[0]->name() == "k2");
  CHECK(p->children()[1]->name() == "k1");
  CHECK(p->children()[2]->name() == "k3");
  k3->setLayer(0);  // re-sorts the parent at once
  CHECK(p->children()[1]->name() == "k3");
  f.mgr.addObject(p);
  const auto calls = f.drawn();
  REQUIRE(calls.size() == 4);  // k2, p, k3, k1
  CHECK(nearPt(calls[0].vertices[0].pos, -2, -3));
}

TEST_CASE("scene order: an invisible sprite hides its subtree; selfVisible hides only the image") {
  Scene e;
  auto p = e.sprite("still", "p");
  auto k = e.sprite("still", "k");
  p->addChild(k);
  e.mgr.addObject(p);
  CHECK(e.drawn().size() == 2);
  p->setSelfVisible(false);
  CHECK(e.drawn().size() == 1);
  p->setSelfVisible(true);
  p->setVisible(false);
  CHECK(e.drawn().empty());
  p->setVisible(true);
  k->setVisible(false);
  CHECK(e.drawn().size() == 1);
}

// ---- lifecycle ----

namespace {
struct Watched : VSprite {
  using VSprite::VSprite;
  std::vector<std::string> names;
  void onAnimComplete(const std::string& a) override { names.push_back(a); }
};
}  // namespace

TEST_CASE("scene play: frames follow the script, OnAnimComplete fires once and clears the name") {
  Scene e;
  auto s = std::make_shared<Watched>(e.ctx, "hero");
  s->setSheet(e.sheet("hero"));
  e.mgr.addObject(s);
  e.at(1000);
  s->play("", false, false, 0);
  CHECK(s->animName() == "DoAnim");
  CHECK(s->currentFrame() == 0);
  e.at(1060);
  CHECK(s->currentFrame() == 1);
  e.at(1120);
  CHECK(s->currentFrame() == 2);
  CHECK(s->names.empty());
  e.at(1180);  // the function returns on this resume
  e.at(1190);
  REQUIRE(s->names.size() == 1);
  CHECK(s->names[0] == "DoAnim");
  CHECK(s->animName().empty());
  e.at(1300);
  CHECK(s->names.size() == 1);
}

TEST_CASE("scene play: a loop restarts from the script's beginning, Stop ends it, same name keeps playing") {
  Scene e;
  auto s = std::make_shared<Watched>(e.ctx, "hero");
  s->setSheet(e.sheet("hero"));
  e.mgr.addObject(s);
  e.at(1000);
  s->play("", true, false, 0);
  e.at(1060);
  e.at(1120);
  CHECK(s->currentFrame() == 2);
  e.at(1180);
  e.at(1190);  // finished: stopped and played again
  CHECK(s->currentFrame() == 0);
  CHECK(s->animName() == "DoAnim");
  CHECK(s->animPlaying());
  CHECK(s->names.empty());  // a loop never completes
  // playing the same name while it plays does not restart it
  e.at(1250);
  CHECK(s->currentFrame() == 1);
  s->play("", true, false, 0);
  CHECK(s->currentFrame() == 1);
  // another function replaces it
  s->play("Other", false, false, 0);
  CHECK(s->currentFrame() == 3);
  s->stop(false);
  CHECK(s->animName().empty());
  CHECK_FALSE(s->animPlaying());
  e.at(1400);
  e.at(1500);
  CHECK(s->names.empty());
}

TEST_CASE("scene play: a delayed play starts after the delay, counted in elapsed time") {
  Scene e;
  auto s = e.sprite("hero", "hero");
  e.mgr.addObject(s);
  e.at(1000);
  s->play("", false, false, 200);
  CHECK(s->animName() == "DoAnim");
  CHECK_FALSE(s->animPlaying());
  e.at(1100);
  CHECK_FALSE(s->animPlaying());
  e.at(1210);
  CHECK(s->animPlaying());
}

TEST_CASE("scene play: playChildren plays every child's default function; Stop(recursive) stops direct children only") {
  Scene e;
  auto p = e.sprite("hero", "p");
  auto k = e.sprite("hero", "k");
  auto g = e.sprite("hero", "g");
  p->addChild(k);
  k->addChild(g);
  e.mgr.addObject(p);
  e.at(1000);
  p->play("", true, true, 0);
  CHECK(k->animPlaying());
  CHECK(g->animPlaying());
  p->stop(true);
  CHECK_FALSE(k->animPlaying());
  CHECK(g->animPlaying());  // grandchildren are not stopped
}

TEST_CASE("scene play: SetDelay stores the delay and runs delay(ms) in the script state when there is one") {
  Scene e;
  auto s = e.sprite("hero", "hero");
  e.mgr.addObject(s);
  s->setDelay(30, false);  // no state yet
  CHECK(s->delay() == 30);
  e.at(1000);
  s->play("Pair", false, false, 0);
  REQUIRE(s->script());
  e.at(1020);
  CHECK(s->currentFrame() == 1);  // the default frame delay is short
  s->setDelay(125, false);
  e.at(1040);
  CHECK(s->currentFrame() == 2);  // the pending wake was already due
  s->stop(false);
  s->play("Pair", false, false, 0);  // frame 0 now, then 125 ms per frame
  CHECK(s->currentFrame() == 0);
  e.at(1100);
  CHECK(s->currentFrame() == 0);
  e.at(1170);
  CHECK(s->currentFrame() == 1);
  auto p = e.sprite("hero", "p");
  auto k = e.sprite("hero", "k");
  p->addChild(k);
  e.mgr.addObject(p);
  p->play("Pair", false, true, 0);
  p->setDelay(40, true);
  CHECK(k->delay() == 40);
  k->play("Pair", false, false, 0);
  e.at(1190);
  CHECK(k->currentFrame() == 0);
  e.at(1215);
  CHECK(k->currentFrame() == 1);
}

TEST_CASE("scene time: GetElapsed is 0 without a clock and for a step of 3000 ms or more") {
  Scene e;
  auto lone = e.sprite("still", "lone");
  CHECK(lone->getElapsed() == 0);
  auto s = e.sprite("still", "s");
  e.mgr.addObject(s);
  e.at(100);
  e.mgr.setNow(3099);
  CHECK(near(static_cast<float>(s->getElapsed()), 2999));
  e.mgr.setNow(3100);
  CHECK(s->getElapsed() == 0);
  e.mgr.setNow(5000);
  CHECK(s->getElapsed() == 0);
}

// ---- anchors ----

TEST_CASE("scene anchors: re-applied every update from the parent's current frame, negated by the parent's flips") {
  Scene e;
  auto parent = e.sprite("hero", "parent");
  auto child = e.sprite("still", "child");
  child->attachToAnchor("hand");
  parent->addChild(child);
  e.mgr.addObject(parent);
  e.at(1000);
  CHECK(nearPt(child->position(), 5, -6));
  parent->setFlip(true, false);
  e.at(1016);
  CHECK(nearPt(child->position(), -5, -6));
  parent->setFlip(true, true);
  e.at(1032);
  CHECK(nearPt(child->position(), -5, 6));
  parent->setFlip(false, false);
  parent->setCurrentFrame(1);  // frame 1 has no such anchor: the position stays
  child->setPosition({1, 2});
  e.at(1048);
  CHECK(nearPt(child->position(), 1, 2));
  // a flipped grandparent counts (IsFlipped goes up the chain)
  Scene f;
  auto top = f.sprite("still", "top");
  auto mid = f.sprite("hero", "mid");
  auto leaf = f.sprite("still", "leaf");
  leaf->attachToAnchor("hand");
  mid->addChild(leaf);
  top->addChild(mid);
  top->setFlip(true, false);
  f.mgr.addObject(top);
  f.at(1000);
  CHECK(nearPt(leaf->position(), -5, -6));
}

// ---- removal ----

TEST_CASE("scene manager: queued removals leave at the next update; global effects reach every object") {
  Scene e;
  auto a = e.sprite("still", "a");
  auto b = e.sprite("still", "b");
  auto k = e.sprite("still", "k");
  a->addChild(k);
  e.mgr.addObject(a);
  e.mgr.addObject(b);
  auto fade = std::make_shared<AlphaEffect>(true, 1.0f, 1000);
  e.mgr.addGlobalEffect(fade);
  CHECK(a->hasEffect(fade->id()));
  CHECK(b->hasEffect(fade->id()));
  auto late = e.sprite("still", "late");
  e.mgr.addObject(late);
  CHECK(late->hasEffect(fade->id()));
  e.mgr.queueRemoval(*b);
  e.mgr.queueRemoval(*k);
  CHECK(e.mgr.objects().size() == 3);
  e.at(1000);
  CHECK(e.mgr.objects().size() == 2);
  CHECK(a->children().empty());
  CHECK(e.mgr.findSpriteByName("late") == late.get());
  CHECK(e.mgr.findSpriteByName("b") == nullptr);
  std::vector<std::string> seen;
  e.mgr.forEachSprite([&](VSprite& s) { return seen.push_back(s.name()), true; }, true);
  CHECK(seen.size() == 2);
}

// ---- effects ----

namespace {
struct Spy : SpriteEffect {
  Spy(std::string tag, std::vector<std::string>& log) : tag(std::move(tag)), log(log) {}
  void onPreDraw(VSprite&, DrawEnv&) override { log.push_back("preDraw " + tag); }
  void onPreRender(VSprite&, DrawEnv&) override { log.push_back("preRender " + tag); }
  void onPostRender(VSprite&, DrawEnv&) override { log.push_back("postRender " + tag); }
  void onPostDraw(VSprite&, DrawEnv&) override { log.push_back("postDraw " + tag); }
  std::string tag;
  std::vector<std::string>& log;
};
}  // namespace

TEST_CASE("scene effects: pre hooks in ascending id order, post hooks descending; same effect twice is counted") {
  Scene e;
  std::vector<std::string> log;
  auto s = e.sprite("still", "s");
  e.mgr.addObject(s);
  auto one = std::make_shared<Spy>("1", log);
  auto two = std::make_shared<Spy>("2", log);
  s->addEffect(two);  // added first, but `one` has the lower id
  s->addEffect(one);
  s->addEffect(one);
  CHECK(s->effectCount() == 2);
  e.drawn();
  const std::vector<std::string> want = {"preDraw 1", "preDraw 2", "preRender 1", "preRender 2",
                                         "postRender 2", "postRender 1", "postDraw 2", "postDraw 1"};
  CHECK(log == want);
  s->removeEffect(one);
  CHECK(s->hasEffect(one->id()));  // one of two holds is left
  s->removeEffect(one);
  CHECK_FALSE(s->hasEffect(one->id()));
  CHECK(one->refs() == 0);
}

TEST_CASE("scene effects: alpha fades follow dt*max/duration, honour the delay and end destroyed") {
  Scene e;
  auto s = e.sprite("still", "s");
  e.mgr.addObject(s);
  auto in = std::make_shared<AlphaEffect>(true, 1.0f, 1000, -1, 200);
  s->addEffect(in);
  CHECK(s->alpha() == 0);  // start alpha 0 for a fade in
  e.at(1000);              // the first update has no elapsed time
  e.at(1100);
  CHECK(near(in->alpha(), 0));  // still in the delay
  e.at(1200);
  e.at(1700);  // the delay ran out during the 1200 step; 500 ms of fade
  CHECK(near(in->alpha(), 0.5f));
  e.drawn();
  CHECK(near(s->alpha(), 0.5f));
  e.at(2200);
  CHECK(near(in->alpha(), 1));
  CHECK_FALSE(in->inProgress());
  e.drawn();  // PostRender marks it destroyed
  CHECK(in->destroyed());
  e.at(2216);
  CHECK(s->effectCount() == 0);
  CHECK(e.mgr.effectCount() == 0);

  auto out = std::make_shared<AlphaEffect>(false, 1.0f, 400);
  s->addEffect(out);
  CHECK(s->alpha() == 1);
  e.at(3000);
  e.at(3100);
  CHECK(near(out->alpha(), 0.75f));
  e.at(3500);
  CHECK(near(out->alpha(), 0));
  CHECK_FALSE(out->inProgress());
}

TEST_CASE("scene effects: tint blends toward its colour while drawing and restores the sprite's tint; stop ramps it away") {
  Scene e;
  auto s = e.sprite("still", "s");
  e.mgr.addObject(s);
  auto tint = std::make_shared<TintEffect>(0.5f, 300, gfx::ColorF{1, 0, 0, 1}, 0.0f);
  s->addEffect(tint);
  e.at(1000);
  e.at(1150);
  CHECK(near(tint->intensity(), 0.25f));
  auto calls = e.drawn();
  REQUIRE(calls.size() == 1);
  CHECK(near(calls[0].vertices[0].color.r, 1));
  CHECK(near(calls[0].vertices[0].color.g, 0.75f));
  CHECK(near(s->tint().g, 1));  // restored after the draw
  e.at(1300);
  CHECK(near(tint->intensity(), 0.5f));
  e.at(1400);
  CHECK(near(tint->intensity(), 0.5f));  // holds at the target
  tint->stop();
  e.at(1550);
  CHECK(near(tint->intensity(), 0.25f));
  e.at(1700);
  CHECK(tint->destroyed());
}

TEST_CASE("scene effects: glow is a triangle wave drawn again, additive, and propagates to children") {
  Scene e;
  auto p = e.sprite("still", "p");
  auto k = e.sprite("still", "k");
  p->addChild(k);
  e.mgr.addObject(p);
  auto glow = std::make_shared<GlowEffect>(0.5f, 1000, -1, 0);
  p->addEffect(glow);
  CHECK(k->hasEffect(glow->id()));
  e.at(1000);
  e.at(1500);  // half a period in: halfway up the first rise (each period is one half wave)
  CHECK(near(glow->intensity(), 0.25f));
  auto calls = e.drawn();
  REQUIRE(calls.size() == 4);  // parent, parent glow, child, child glow
  CHECK(calls[0].blend == gfx::Blend::Normal);
  CHECK(calls[1].blend == gfx::Blend::Additive);
  CHECK(near(calls[1].vertices[0].color.a, 0.25f));
  // the glow's alpha is the intensity times the sprite's own and inherited alpha
  p->setAlpha(0.5f);
  calls = e.drawn();
  CHECK(near(calls[1].vertices[0].color.a, 0.125f));
  CHECK(near(calls[3].vertices[0].color.a, 0.25f * 1 * 0.5f));  // the child inherits the parent's alpha
  p->setAlpha(1);
  e.at(2000);
  CHECK(near(glow->intensity(), 0.5f));  // the peak after one period
  e.at(2500);
  CHECK(near(glow->intensity(), 0.25f));  // the second period falls
  e.at(3000);
  CHECK(near(glow->intensity(), 0.0f));
  glow->stop();
  e.at(3100);
  CHECK(glow->destroyed());
}

TEST_CASE("scene effects: hover glow rises and falls at dt*max/100 and draws its halo at the four offsets") {
  Scene e;
  auto s = e.sprite("still", "s");
  e.mgr.addObject(s);
  auto hover = std::make_shared<HoverGlow>(0.2f, true, true, true);
  s->addEffect(hover);
  CHECK(s->listenerCount() == 0);  // the listener joins at the next update
  e.at(1000);
  CHECK(s->listenerCount() == 1);
  e.at(1100);
  CHECK(near(hover->intensity(), 0));
  hover->setHovered(true);
  e.at(1200);
  CHECK(near(hover->intensity(), 0.2f));  // 100 ms * 0.2 / 100, capped at max
  auto calls = e.drawn();
  CHECK(calls.size() == 5);  // the sprite and four halo passes
  CHECK(near(calls[1].vertices[0].pos.x - calls[4].vertices[0].pos.x, -1));  // shifted by -0.5 and +0.5 on x
  CHECK(near(calls[1].vertices[0].pos.y - calls[2].vertices[0].pos.y, -1));
  hover->setHovered(false);
  e.at(1300);
  CHECK(near(hover->intensity(), 0));
  CHECK(e.drawn().size() == 1);
  // the one-axis variant still draws four passes; the flags only decide the half-pixel shift
  auto one = std::make_shared<HoverGlow>(0.2f, false, true, false);
  auto t = e.sprite("still", "t");
  e.mgr.addObject(t);
  t->addEffect(one);
  one->setHovered(true);
  e.at(1400);
  e.at(1500);
  CHECK(e.drawn().size() == 1 + 1 + 4);
}

TEST_CASE("scene effects: wipe clips the sprite's rectangle by progress, a wipe away hides it at the end") {
  Scene e;
  auto s = e.sprite("hero", "s");
  s->setCurrentFrame(3);  // 10x10, no registration point
  s->setPosition({100, 50});
  e.mgr.addObject(s);
  auto wipe = std::make_shared<WipeEffect>(0, false, 1000);
  s->addEffect(wipe);
  e.at(1000);
  e.at(1500);
  CHECK(near(wipe->progress(), 0.5f));
  e.rr.clear();
  e.mgr.draw(e.rr, {});
  const gfx::RectI* clip = nullptr;
  int pops = 0;
  for (const auto& c : e.rr.calls()) {
    if (c.kind == gfx::RecordingRenderer::Call::Kind::PushClip) clip = &c.clip;
    pops += c.kind == gfx::RecordingRenderer::Call::Kind::PopClip;
  }
  REQUIRE(clip);
  CHECK(*clip == gfx::RectI{100, 55, 10, 5});  // the top half is wiped away (direction 0)
  CHECK(pops == 1);
  const gfx::RectF r{0, 0, 10, 10};
  CHECK(WipeEffect(1, false, 1).clipOf(r) == gfx::RectF{0, 0, 10, 10});
  e.at(2000);
  CHECK(s->visible());
  e.drawn();
  CHECK_FALSE(s->visible());  // the wipe away ended on that draw
  CHECK(wipe->destroyed());

  auto reveal = std::make_shared<WipeEffect>(2, true, 1000);
  CHECK(reveal->clipOf(r) == gfx::RectF{0, 0, 0, 10});
}

TEST_CASE("scene effects: move eases from where the sprite was, linear at accel 0, delayed, ending on the target") {
  Scene e;
  auto s = e.sprite("still", "s");
  s->setPosition({10, 20});
  e.mgr.addObject(s);
  auto move = std::make_shared<SpriteMove>(gfx::PointF{110, 20}, 1000, 0.0f, 200);
  s->addEffect(move);
  e.at(1000);
  e.at(1200);  // the delay is over, no movement yet
  e.drawn();
  CHECK(nearPt(s->position(), 10, 20));
  e.at(1700);
  e.drawn();
  CHECK(nearPt(s->position(), 60, 20));
  e.at(2400);
  e.drawn();
  CHECK(nearPt(s->position(), 110, 20));
  CHECK(move->destroyed());

  auto t = e.sprite("still", "t");
  e.mgr.addObject(t);
  auto eased = std::make_shared<SpriteMove>(gfx::PointF{100, 0}, 1000, 1.0f, 0);
  t->addEffect(eased);
  e.at(3000);
  e.at(3500);
  CHECK(near(eased->eased(), 0.375f));  // u*(1 - a/2) + a*u^2/2 at u = 0.5
}

// ---- VObject ----

namespace {
Tbl entry(const std::string& state, const std::string& file, int base = 0) {
  auto t = tbl();
  t->set("state", state);
  t->set("file", file);
  t->set("base", static_cast<double>(base));
  return t;
}
Tbl asset(const std::string& baseDir, std::vector<Tbl> entries) {
  auto t = tbl();
  if (!baseDir.empty()) t->set("baseDir", baseDir);
  for (auto& e : entries) t->append(e);
  return t;
}
}  // namespace

TEST_CASE("VObject loader: scale reads element 1 for both axes with the fixes off, element 2 as y with them on (F3)") {
  for (bool fix : {false, true}) {
    Scene e(fix);
    auto o = tbl();
    o->set("scale", std::vector<double>{0.96, 1.0});
    auto assets = tbl();
    assets->set("anim", asset("anim/", {entry("default", "hero")}));
    o->set("assets", assets);
    auto obj = VObject::create(e.ctx, "tube", "");
    obj->loadFromTable(StyleChain(*o, nullptr));
    CHECK(near(obj->scale().x, 0.96f));
    CHECK(near(obj->scale().y, fix ? 1.0f : 0.96f));
    CHECK(e.log.contains("[fix] F3") == fix);
    // the same pair repeated is no change, and a single value is both axes either way
    auto u = tbl();
    u->set("scale", std::vector<double>{0.5});
    auto o2 = VObject::create(e.ctx, "u", "");
    o2->loadFromTable(StyleChain(*u, nullptr));
    CHECK(near(o2->scale().x, 0.5f));
    CHECK(near(o2->scale().y, 0.5f));
  }
}

TEST_CASE("VObject loader: keys, defaults, the first asset by name, states with base frame, layer and play") {
  Scene e;
  auto o = tbl();
  o->set("name", "Mixer");
  o->set("x", 10.0);
  o->set("y", 20.0);
  o->set("layer", 3.0);
  o->set("spriteLayer", -2.0);
  o->set("alpha", 0.5);
  o->set("rotation", std::vector<double>{0, 0, 0});
  o->set("tint", std::vector<double>{1, 0.5, 0.25, 1});
  o->set("Upgrade", "kPress1");
  o->set("clickable", 1.0);
  o->set("mouse", 0.0);
  o->set("hflip", 1.0);
  auto assets = tbl();
  auto zeta = entry("default", "hero", 2);
  zeta->set("play", "Other");
  assets->set("zeta", asset("anim/", {zeta}));
  auto alpha = entry("default", "blink", 1);
  auto alt = entry("alt", "still", 0);
  alt->set("layer", 9.0);
  alt->set("regX", 1.0);
  alt->set("regY", 4.0);
  assets->set("Alpha", asset("anim/", {alpha, alt}));
  o->set("assets", assets);
  auto obj = VObject::create(e.ctx, "", "thing");
  CHECK(obj->name().find("object_thing") == 0);
  obj->loadFromTable(StyleChain(*o, nullptr));
  CHECK(obj->name() == "Mixer");
  CHECK(nearPt(obj->position(), 10, 20));
  CHECK(obj->layer() == 3);
  CHECK(obj->depth() == -2);
  CHECK(near(obj->alpha(), 0.5f));
  CHECK(near(obj->tint().b, 0.25f));
  CHECK(obj->clickable());
  CHECK_FALSE(obj->mouse());
  CHECK(obj->hflip());
  CHECK(obj->attribute("upgrade") == "kPress1");  // the key is lower-cased
  CHECK(obj->attribute("missing", "d") == "d");
  CHECK(obj->hasAsset("ALPHA"));
  obj->init();
  CHECK(obj->currentAsset() == "alpha");  // alphabetical, not table order
  CHECK(obj->currentState() == "default");
  CHECK(obj->currentFrame() == 1);  // the state's base
  obj->switchAsset("alpha", "alt", "default");
  CHECK(obj->currentState() == "alt");
  CHECK(obj->layer() == 9);
  obj->switchAsset("alpha", "nope", "default");  // falls back
  CHECK(obj->currentState() == "default");
  obj->switchAsset("nothing", "default", "default");  // unknown asset: the image and the current asset are cleared
  CHECK(obj->currentAsset().empty());
  CHECK(obj->sheet() == nullptr);
  obj->switchAsset("Alpha", "default", "default");  // the key is not lower-cased
  CHECK(obj->currentAsset().empty());
  obj->switchAsset("alpha", "default", "default");
  CHECK(obj->currentAsset() == "alpha");
  obj->switchAsset("zeta", "default", "default");
  CHECK(obj->currentFrame() == 2);
  // Play with an empty name uses the state's `play` function; Stop returns to the base frame
  obj->setPosition({0, 0});
  obj->play("", false, false, 0);
  CHECK(obj->animName() == "Other");
  obj->stop(false);
  CHECK(obj->currentFrame() == 2);
}

TEST_CASE("VObject loader: lookups climb the parents by depth, children and text are built, `style` is a base table") {
  Scene e;
  auto root = tbl();
  root->set("x", 10.0);
  root->set("visible", 0.0);
  root->set("spriteLayer", 9.0);
  root->set("baseDir", "anim/");
  auto kids = tbl();
  kids->set("spriteLayer", 7.0);  // a plain value of the group table reaches its entries
  kids->set("leaf", 1.0);
  auto kid = tbl();  // inherits visible (unlimited depth) but not x or spriteLayer(depth 1 reaches the parent: yes)
  auto kidAssets = tbl();
  kidAssets->set("a", asset("", {entry("default", "still")}));  // baseDir from the object above
  auto plainAsset = asset("", {entry("default", "plain")});
  plainAsset->set("anim", 0.0);  // `anim = false` at the asset level reaches its states (depth 2)
  kidAssets->set("c", plainAsset);
  kid->set("assets", kidAssets);
  kids->set("one", kid);
  auto based = tbl();  // gets its assets from a `style` table
  auto styleBase = tbl();
  auto baseAssets = tbl();
  baseAssets->set("b", asset("anim/", {entry("default", "blink", 1)}));
  styleBase->set("assets", baseAssets);
  based->set("style", styleBase);
  kids->set("two", based);
  root->set("children", kids);
  auto texts = tbl();
  auto label = tbl();
  label->set("label", "#Hello");
  label->set("w", 100.0);
  label->set("h", 20.0);
  label->set("bold", 1.0);
  label->set("x", 3.0);
  texts->set("diary", label);
  root->set("text", texts);
  auto obj = VObject::create(e.ctx, "root", "");
  obj->loadFromTable(StyleChain(*root, nullptr));
  obj->init();
  VObject* one = obj->childObject("ONE");
  REQUIRE(one);
  CHECK_FALSE(one->visible());     // visible=false of the parent applies (unlimited depth)
  CHECK(one->position().x == 0);   // x reads this table only
  CHECK(one->depth() == 7);        // spriteLayer reaches one level up (the group), not the object above it
  CHECK(one->leaf());
  CHECK(one->currentAsset() == "a");
  CHECK(one->currentFrame() == 0);
  CHECK(one->sheet());              // baseDir came from the root
  one->switchAsset("c", "default", "default");
  CHECK(one->plainTexture());
  CHECK(one->sheet() == nullptr);
  one->switchAsset("a", "default", "default");
  VObject* two = obj->childObject("two");
  REQUIRE(two);
  CHECK(two->currentAsset() == "b");  // from the `style` base
  CHECK(two->currentFrame() == 1);
  TextSprite* t = obj->textSprite("diary");
  REQUIRE(t);
  CHECK(t->text() == "Hello");
  CHECK(t->markup() == "<b>Hello</b>");
  CHECK(t->width() == 100);
  CHECK(t->position().x == 3);
  CHECK(obj->children().size() == 3);
  // loading again detaches what the first load made
  obj->loadFromTable(StyleChain(*root, nullptr));
  CHECK(obj->children().size() == 3);
  auto empty = tbl();
  obj->loadFromTable(StyleChain(*empty, nullptr));
  CHECK(obj->children().empty());
  CHECK_FALSE(obj->hasAsset("a"));
}

TEST_CASE("VObject: continuous objects loop their play function; a missing file is logged once and shows nothing") {
  Scene e;
  auto o = tbl();
  o->set("continuous", 1.0);
  auto assets = tbl();
  auto flame = entry("default", "blink");
  flame->set("play", "DoFlame");
  assets->set("flame", asset("anim/", {flame}));
  assets->set("ghost", asset("anim/", {entry("default", "missing")}));
  o->set("assets", assets);
  auto obj = VObject::create(e.ctx, "f", "");
  obj->loadFromTable(StyleChain(*o, nullptr));
  e.mgr.addObject(obj);
  e.at(1000);
  obj->init();
  CHECK(obj->currentAsset() == "flame");
  CHECK(obj->animName() == "DoFlame");
  CHECK(obj->animPlaying());
  CHECK(obj->currentFrame() == 1);
  // after the function ends a loop starts it again
  e.at(1100);
  e.at(1200);
  CHECK(obj->animName() == "DoFlame");
  CHECK(obj->animPlaying());
  obj->switchAsset("ghost", "default", "default");
  obj->switchAsset("ghost", "default", "default");
  CHECK(obj->sheet() == nullptr);
  int n = 0;
  for (const auto& l : e.log.lines()) n += l.find("'anim/missing' did not load") != std::string::npos;
  CHECK(n == 1);
}

// ---- nine-slice ----

TEST_CASE("VTiledObject: nine pieces by the insets, corners native, edges and centre stretched; draws centred pieces") {
  const auto p = tiledPieces(100, 80, 300, 200, 20, 30, 10, 40);
  // top-left corner: (0,0,x1,y1) centred at (x1/2, y1/2)
  CHECK(p[0].src == gfx::RectF{0, 0, 20, 10});
  CHECK(nearPt(p[0].center, 10, 5));
  // bottom-right: (Rs,Bs,TW,TH) with Rs = 100-30, Bs = 80-40, centred at (w - x2/2, h - y2/2)
  CHECK(p[3].src == gfx::RectF{70, 40, 30, 40});
  CHECK(nearPt(p[3].center, 285, 180));
  // centre: scale (w-x1-x2)/(Rs-L) by (h-y1-y2)/(Bs-T)
  CHECK(p[8].src == gfx::RectF{20, 10, 50, 30});
  CHECK(near(p[8].scale.x, 250.0f / 50));
  CHECK(near(p[8].scale.y, 150.0f / 30));
  CHECK(nearPt(p[8].center, 20 + 125, 10 + 75));
  // top edge stretches along x only
  CHECK(near(p[4].scale.x, 5));
  CHECK(near(p[4].scale.y, 1));
  // the insets clamp to the texture for the source rectangles but not for the corner centres
  const auto q = tiledPieces(10, 10, 100, 100, 30, 0, 5, 0);
  CHECK(q[0].src.w == 10);
  CHECK(q[0].drawn);
  CHECK(!q[2].drawn);  // empty source rectangles are skipped

  Scene e;
  auto o = tbl();
  o->set("x1", 4.0);
  o->set("x2", 4.0);
  o->set("y1", 3.0);
  o->set("y2", 3.0);
  o->set("w", 50.0);
  o->set("h", 40.0);
  auto assets = tbl();
  auto st = entry("default", "plain", 0);
  st->set("anim", 0.0);
  assets->set("background", asset("anim/", {st}));
  o->set("assets", assets);
  auto tiled = TiledObject::create(e.ctx, "box", "");
  tiled->loadFromTable(StyleChain(*o, nullptr));
  tiled->init();
  tiled->setPosition({100, 100});
  e.mgr.addObject(tiled);
  const auto calls = e.drawn();
  CHECK(calls.size() == 9);
  CHECK(nearPt({tiled->getRect(DrawSpec{}, 0).x, tiled->getRect(DrawSpec{}, 0).y}, 100, 100));
  CHECK(near(tiled->getRect(DrawSpec{}, 0).w, 50));
  tiled->setSize(60, 40);  // the size can change after load
  CHECK(near(tiled->getRect(DrawSpec{}, 0).w, 60));
}

// ---- digits and the register ----

TEST_CASE("VDigit: rolls one step per 31 ms from -1 toward its target and clamps to 0..9") {
  Scene e;
  auto o = tbl();
  o->set("w", 15.0);
  o->set("h", 25.0);
  auto d = Digit::create(e.ctx, "digit_1");
  d->loadFromTable(StyleChain(*o, nullptr));
  e.mgr.addObject(d);
  REQUIRE(d->text());
  CHECK(d->text()->text() == "0");
  CHECK(d->shown() == -1);
  d->setDigit(3);
  e.at(1000);
  CHECK(d->shown() == 0);
  e.at(1010);  // under 31 ms since the last tick
  CHECK(d->shown() == 0);
  e.at(1031);
  CHECK(d->shown() == 1);
  e.at(1062);
  e.at(1093);
  CHECK(d->shown() == 3);
  CHECK(d->text()->text() == "3");
  e.at(1124);
  CHECK(d->shown() == 3);
  d->setDigit(1);  // counts down too
  e.at(1155);
  CHECK(d->shown() == 2);
  d->setDigit(40);
  CHECK(d->target() == 9);
  d->setDigit(-4);
  CHECK(d->target() == 0);
}

namespace {
struct RegisterFx : Scene {
  explicit RegisterFx(const std::string& context) : Scene() {
    wm.setScriptSink(nullptr);
    auto s = tbl();
    s->set("w", 125.0);
    s->set("h", 25.0);
    s->set("numDigits", 7.0);
    s->set("context", context);
    auto digit = tbl();
    digit->set("w", 15.0);
    digit->set("h", 25.0);
    s->set("digit", digit);
    auto w = std::make_unique<VRegister>(wm, ctx);
    reg = &static_cast<VRegister&>(wm.initWindow(std::move(w), *s));
    reg->postChildrenInit(*s);
    wm.popCreation();
  }
  VRegister* reg = nullptr;
};
}  // namespace

TEST_CASE("VRegister: digits laid out right to left, visible by place, one digit per decimal place") {
  RegisterFx e("");
  REQUIRE(e.reg);
  REQUIRE(e.reg->digits().size() == 7);
  CHECK(e.reg->digits()[0]->name() == "digit_1");
  CHECK(e.reg->digits()[1]->name() == "digit_10");
  CHECK(e.reg->digits()[6]->name() == "digit_1000000");
  CHECK(nearPt(e.reg->digits()[0]->position(), 110, 0));  // width 125 - digit width 15
  CHECK(nearPt(e.reg->digits()[6]->position(), 20, 0));
  e.reg->setValue(4305);
  CHECK(e.reg->digits()[0]->target() == 5);
  CHECK(e.reg->digits()[1]->target() == 0);
  CHECK(e.reg->digits()[2]->target() == 3);
  CHECK(e.reg->digits()[3]->target() == 4);
  CHECK(e.reg->digits()[3]->visible());
  CHECK_FALSE(e.reg->digits()[4]->visible());
  e.reg->setValue(0);
  CHECK(e.reg->digits()[0]->visible());  // the ones place always shows
  CHECK_FALSE(e.reg->digits()[1]->visible());
}

TEST_CASE("VRegister: the value per context and mode (current_level_total, current_worth) refreshes on the animation tick") {
  struct Case {
    const char* context;
    int mode;
    int expect;
  };
  for (const Case& c : {Case{"", 0, 70}, Case{"endlevel", 0, 1000}, Case{"prelevel", 0, 1070}, Case{"other", 0, 70},
                        Case{"endlevel", 2, 70}, Case{"prelevel", 1, 70}}) {
    RegisterFx e(c.context);
    GreatCCWorld* w = worldOf(e.ctx);
    REQUIRE(w);
    w->settings().mode = c.mode;
    w->values().setValue("current_worth", 1000, false);
    w->values().setValue("current_level_total", 70, false);
    e.wm.setNowMs(1000);
    CHECK(e.reg->shownValue() == -1);  // the start value forces a first draw
    CHECK(e.reg->valueNow() == c.expect);
    e.reg->onAnimate();
    CHECK(e.reg->shownValue() == c.expect);
  }
}

// ---- windows ----

TEST_CASE("VObjectWindow: builds its `objects`, adds a move effect with the original defaults, ignores other types") {
  Scene e;
  auto s = tbl();
  s->set("w", 800.0);
  s->set("h", 600.0);
  auto objects = tbl();
  auto steam = tbl();
  steam->set("name", "steam_anim");
  steam->set("x", 5.0);
  steam->set("continuous", 1.0);
  auto assets = tbl();
  assets->set("anim", asset("anim/", {entry("default", "blink")}));
  steam->set("assets", assets);
  auto effects = tbl();
  auto mv = tbl();
  mv->set("type", "Move");
  mv->set("x", 105.0);
  auto other = tbl();
  other->set("type", "fade");
  effects->append(mv);
  effects->append(other);
  steam->set("effects", effects);
  objects->set("steam", steam);
  s->set("objects", objects);
  auto w = std::make_unique<VObjectWindow>(e.wm, e.ctx);
  auto& win = static_cast<VObjectWindow&>(e.wm.initWindow(std::move(w), *s));
  win.postChildrenInit(*s);
  e.wm.popCreation();
  REQUIRE(win.sprites().objects().size() == 1);
  VSprite* obj = win.sprites().findSpriteByName("steam_anim");
  REQUIRE(obj);
  CHECK(obj->effectCount() == 1);  // the move; `fade` is not recognised
  CHECK(win.name() == "sprite_window");
  CHECK(win.detail().find("sprites=1") != std::string::npos);
  // the window updates its manager on the modal's clock
  e.wm.setNowMs(1000);
  win.onAnimate();
  e.wm.setNowMs(1300);
  win.onAnimate();
  gfx::RecordingRenderer rr;
  ui::DrawContext dc{rr, {0, 0}, 1, {0, 0, 800, 600}};
  win.draw(dc);
  CHECK_FALSE(rr.calls().empty());
  CHECK(nearPt(obj->position(), 105, 0));  // the move ran to its target during that draw
}

TEST_CASE("Style::forEachEntry visits string keys with their tables or text, for a map and for Lua tables") {
  auto t = tbl();
  t->set("a", "text");
  t->set("n", 2.5);
  t->set("sub", tbl());
  std::vector<std::string> seen;
  t->forEachEntry([&](const std::string& k, const ui::Style* sub, const std::optional<std::string>& text) {
    seen.push_back(k + (sub ? "=table" : "=" + text.value_or("?")));
  });
  CHECK(seen == std::vector<std::string>{"a=text", "n=2.5", "sub=table"});

  test::ScriptEnv env(std::string(CHOCO_TEST_ASSETS) + "/greatcc/state");
  REQUIRE(env.run("t = { x = 4, name = 'n', child = { y = 1 }, 'array', flag = true }") == 0);
  lua_State* L = env.lua.L();
  lua_getglobal(L, "t");
  {
    script::LuaStyle style = script::LuaStyle::plainView(L, lua_gettop(L), &env.lua);
    std::map<std::string, std::string> got;
    style.forEachEntry([&](const std::string& k, const ui::Style* sub, const std::optional<std::string>& text) {
      got[k] = sub ? std::string("table:") + std::to_string(sub->numberOr("y", -1)) : text.value_or("?");
    });
    CHECK(got.size() == 4);  // the array element has no string key
    CHECK(got["x"] == "4");
    CHECK(got["name"] == "n");
    CHECK(got["child"] == "table:1.000000");
    CHECK(got["flag"] == "?");
  }
  lua_pop(L, 1);
}

// ---- the shipped tables (only with the install present) ----

namespace {
struct ShippedScene : test::ScriptEnv {
  explicit ShippedScene(const std::string& assets)
      : ScriptEnv(assets, (fs::temp_directory_path() / "choco_greatcc_scene_shipped").string()),
        textures(fs, nullptr),
        sheets(fs, textures) {
    ctx.module = games::findModule("greatcc");
    ctx.module->attach(ctx);
    wm.setSheets(&sheets);
    wm.setTextures(&textures);
    wm.pushModal("m");
  }
  gfx::TextureCache textures;
  gfx::SheetCache sheets;
};
}  // namespace

namespace {
void checkShippedTables(const std::string& assets) {
  ShippedScene e(assets);
  script::ScriptHost host(e.ctx);
  REQUIRE(host.boot());
  REQUIRE(e.run("require('locations/iquitos/backgrounds.lua')") == 0);
  lua_State* L = e.lua.L();

  // the background: `baseDir = "assets/anim/iquitos/"`, three states of one asset, `mouse = false`, depth -2
  lua_getglobal(L, "iquitosBackground");
  REQUIRE(lua_istable(L, -1));
  {
    script::LuaStyle style = script::LuaStyle::plainView(L, lua_gettop(L), &e.lua);
    auto bg = VObject::create(e.ctx, "", "background");
    bg->loadFromTable(StyleChain(style, nullptr));
    bg->init();
    CHECK(bg->name() == "background_object");
    CHECK(bg->depth() == -2);
    CHECK_FALSE(bg->mouse());
    CHECK(bg->currentAsset() == "background");
    CHECK(bg->sheet() != nullptr);
    CHECK(bg->currentFrame() == 0);
    e.log.setKeep(true);
    CHECK_FALSE(e.log.contains("did not load"));
  }
  lua_pop(L, 1);

  // a style base with a child: flameObject's `flame` is continuous and plays DoFlame from base frame 4
  lua_getglobal(L, "flameObject");
  REQUIRE(lua_istable(L, -1));
  {
    script::LuaStyle style = script::LuaStyle::plainView(L, lua_gettop(L), &e.lua);
    auto flame = VObject::create(e.ctx, "f", "");
    flame->loadFromTable(StyleChain(style, nullptr));
    SpriteManager mgr(e.ctx);
    mgr.setNow(1000);
    mgr.addObject(flame);
    flame->init();
    VObject* child = flame->childObject("flame");
    REQUIRE(child);
    CHECK(child->continuous());
    CHECK(child->animName() == "DoFlame");
    CHECK(child->animPlaying());
  }
  lua_pop(L, 1);

  // the mixer: group-level `leaf = true`, the non-uniform scale {0.96, 1.0} of `anim_tube` (F3 is on in this context)
  lua_getglobal(L, "machine_mixer_style");
  REQUIRE(lua_istable(L, -1));
  {
    script::LuaStyle style = script::LuaStyle::plainView(L, lua_gettop(L), &e.lua);
    auto mixer = VObject::create(e.ctx, "m", "");
    mixer->loadFromTable(StyleChain(style, nullptr));
    CHECK(mixer->attribute("baseDir") == "anim/machines/mixer/");
    VObject* tube = mixer->childObject("anim_tube");
    REQUIRE(tube);
    CHECK(tube->name() == "machine_tube");
    CHECK(near(tube->scale().x, 0.96f));
    CHECK(near(tube->scale().y, 1.0f));
    CHECK(tube->leaf());  // from `children = { leaf = true, ... }`
    CHECK(tube->hasAsset("anim"));
    VObject* panel = mixer->childObject("panel");
    REQUIRE(panel);
    CHECK(panel->layer() == 5);
  }
  lua_pop(L, 1);
}
}  // namespace

TEST_CASE("VObject loader on the shipped Iquitos and machine tables") {
  // The Windows install (assets.pfp) and the Mac bundle's loose files.
  for (const std::string& assets : {std::string(CHOCO_GREATCC_ASSETS), std::string(CHOCO_GREATCC_MAC_ASSETS)}) {
    if (assets.empty() || !(fs::exists(fs::path(assets) / "assets.pfp") || fs::exists(fs::path(assets) / "locations/machines.lua")))
      continue;
    CAPTURE(assets);
    checkShippedTables(assets);
  }
}



// ---- review fixes ----

TEST_CASE("scene script: a native of the running function may swap the sheet, stop and play without touching freed state") {
  Scene e;
  auto s = e.sprite("swap", "s");
  e.mgr.addObject(s);
  e.at(1000);
  s->play("", false, false, 0);  // Load() runs inside the coroutine and replaces the sprite's script host
  REQUIRE(s->sheet());
  CHECK(s->sheet()->path.find("blink") != std::string::npos);
  CHECK(s->currentFrame() == 0);
  e.at(1100);
  e.at(1200);
  CHECK(s->currentFrame() == 0);  // the old function was halted: its SetCurrentFrame(1) never ran
  CHECK_FALSE(e.log.contains("[sprite-lua]"));

  auto r = e.sprite("reent", "r");
  e.mgr.addObject(r);
  r->script()->addNative("boom", [&](lua_State*) {
    r->stop(false);
    r->play("Other", false, false, 0);
    return 0;
  });
  r->play("", false, false, 0);
  CHECK(r->currentFrame() == 3);  // Other ran after the old function was released; DoAnim's frames{2} did not
  CHECK(r->animName() == "Other");
  r->stop(false);
  CHECK_FALSE(r->animPlaying());
  CHECK_FALSE(e.log.contains("[sprite-lua]"));
}

TEST_CASE("scene draw: alpha multiplies down the tree, flips XOR, blend is inherited when the child's is -1") {
  Scene e;
  auto p = e.sprite("hero", "p");
  auto k = e.sprite("hero", "k");
  p->addChild(k);
  e.mgr.addObject(p);
  p->setAlpha(0.5f);
  k->setAlpha(0.5f);
  auto calls = e.drawn();
  REQUIRE(calls.size() == 2);
  CHECK(near(calls[0].vertices[0].color.a, 0.5f));
  CHECK(near(calls[1].vertices[0].color.a, 0.25f));
  p->setAlpha(1);
  k->setAlpha(1);
  // flips: the parent's hflip reaches the child; both set cancel out
  p->setFlip(true, false);
  calls = e.drawn();
  CHECK(calls[1].vertices[0].uv.x > calls[1].vertices[1].uv.x);
  k->setFlip(true, false);
  calls = e.drawn();
  CHECK(calls[1].vertices[0].uv.x < calls[1].vertices[1].uv.x);
  p->setFlip(false, false);
  k->setFlip(false, false);
  // blend: a sprite's mode is for its own image; its child is handed -1 and draws normally unless it has its own
  CHECK(p->blend() == -1);
  p->setBlend(2);
  calls = e.drawn();
  CHECK(calls[0].blend == gfx::Blend::Additive);
  CHECK(calls[1].blend == gfx::Blend::Normal);
  k->setBlend(2);
  calls = e.drawn();
  CHECK(calls[1].blend == gfx::Blend::Additive);
}

TEST_CASE("scene effects: tint propagates to the children; cancelling a propagating effect takes it from the children too") {
  Scene e;
  auto p = e.sprite("still", "p");
  auto k = e.sprite("still", "k");
  auto g = e.sprite("still", "g");
  p->addChild(k);
  k->addChild(g);
  e.mgr.addObject(p);
  auto tint = std::make_shared<TintEffect>(0.5f, 300, gfx::ColorF{1, 0, 0, 1}, 0.0f);
  CHECK(tint->propagate());
  p->addEffect(tint);
  CHECK(k->hasEffect(tint->id()));
  CHECK(g->hasEffect(tint->id()));
  p->cancelEffects();
  CHECK_FALSE(p->hasEffect(tint->id()));
  CHECK_FALSE(k->hasEffect(tint->id()));
  CHECK_FALSE(g->hasEffect(tint->id()));
  CHECK(tint->refs() == 0);
}

TEST_CASE("scene time: the first GetElapsed is measured from the clock, so a sprite added late waits its full delay") {
  Scene e;
  auto s = e.sprite("hero", "s");
  e.mgr.setNow(1500);
  e.mgr.addObject(s);
  s->play("", false, false, 200);
  e.at(1500);  // the sprite's first update
  CHECK_FALSE(s->animPlaying());
  e.at(1600);
  CHECK_FALSE(s->animPlaying());
  e.at(1710);
  CHECK(s->animPlaying());
  // a clock at or behind the last update gives no elapsed time
  e.mgr.setNow(1700);
  CHECK(s->getElapsed() == 0);
  e.mgr.setNow(1710);
  CHECK(s->getElapsed() == 0);
}

TEST_CASE("VObject: asset entries read state/file/mask from their own table, regX/regY one level up; names and attributes") {
  Scene e;
  auto o = tbl();
  o->set("file", "hero");  // the object's own `file` is not an entry's
  o->set("x", 10.0);
  o->set("category", "Aesthetic");
  auto assets = tbl();
  auto only = tbl();
  auto bare = tbl();
  bare->set("state", "default");  // no file of its own: ignored
  only->append(bare);
  auto regd = asset("anim/", {entry("default", "hero")});
  regd->set("regX", 5.0);
  regd->set("regY", 6.0);
  assets->set("bare", only);
  assets->set("regd", regd);
  o->set("assets", assets);
  auto kids = tbl();
  kids->set("FlameA", tbl());
  o->set("children", kids);
  auto obj = VObject::create(e.ctx, "o", "");
  obj->loadFromTable(StyleChain(*o, nullptr));
  CHECK(obj->asset("bare")->states().empty());
  const AssetState* st = obj->asset("regd")->state("default");
  REQUIRE(st);
  CHECK(st->hasReg);
  CHECK(near(st->reg.x, 5));
  CHECK(obj->attribute("x") == "10");  // attributes keep every plain entry
  CHECK(obj->attribute("category") == "Aesthetic");
  CHECK(obj->attribute("file") == "hero");
  REQUIRE(obj->children().size() == 1);
  CHECK(obj->children()[0]->name() == "flamea");  // an auto-child takes the lower-cased key
}

TEST_CASE("VObject: a looping function restarts through the base Stop and Play, not the object's override") {
  Scene e;
  auto o = tbl();
  o->set("continuous", 1.0);
  auto assets = tbl();
  auto st = entry("default", "hero", 3);
  st->set("play", "Slow");
  assets->set("a", asset("anim/", {st}));
  o->set("assets", assets);
  auto obj = VObject::create(e.ctx, "o", "");
  obj->loadFromTable(StyleChain(*o, nullptr));
  e.mgr.addObject(obj);
  e.at(1000);
  obj->init();
  CHECK(obj->currentFrame() == 3);  // the state's base frame while the function waits
  e.at(1100);                       // SetCurrentFrame(1) runs, the function ends and the loop starts it again
  CHECK(obj->currentFrame() == 1);  // the object's Stop override would have put the base frame back
  CHECK(obj->animPlaying());
}

TEST_CASE("scene matrix: the scale multiplies the 2x2 part only, the rotation's translation is not scaled (Multiply2x2)") {
  Scene e;
  auto s = e.sprite("still", "s");
  s->setRotation(10, 0, static_cast<float>(M_PI / 2));
  s->setScale(2, 2);
  s->setPosition({100, 0});
  CHECK(nearPt(s->localMatrix().apply({0, 0}), 110, -10));  // translation (10,-10) plus the position
  CHECK(nearPt(s->localMatrix().apply({1, 0}), 110, -8));   // the linear part turned and doubled
}

TEST_CASE("scene: SpriteManager::draw with a scale shrinks the whole scene about the origin it is given (the upgrade shop's picture of the shop)") {
  Scene e;
  auto s = e.sprite("still", "s");
  s->setPosition({100, 60});
  e.mgr.addObject(s);
  const auto full = e.drawn({0, 0});
  REQUIRE(full.size() == 1);
  e.rr.clear();
  e.mgr.draw(e.rr, {342, 93}, 0.5f);
  std::vector<gfx::RecordingRenderer::Call> half;
  for (const auto& c : e.rr.calls())
    if (c.kind == gfx::RecordingRenderer::Call::Kind::DrawGeometry) half.push_back(c);
  REQUIRE(half.size() == 1);
  REQUIRE(full[0].vertices.size() == half[0].vertices.size());
  REQUIRE_FALSE(full[0].vertices.empty());
  for (std::size_t i = 0; i < full[0].vertices.size(); ++i) {
    // scaled first (about the scene's own origin), then moved to (342, 93)
    CHECK(half[0].vertices[i].pos.x == doctest::Approx(342 + full[0].vertices[i].pos.x * 0.5f));
    CHECK(half[0].vertices[i].pos.y == doctest::Approx(93 + full[0].vertices[i].pos.y * 0.5f));
  }
}
