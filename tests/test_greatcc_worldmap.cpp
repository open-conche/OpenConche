// The Great Chocolate Chase, slice 8 (docs/design/greatcc.md section 6): the world map. Synthetic fixtures only: small PNGs and
// an animation file built here, pin and window tables made from MapStyle, and a world with a few values and level goals.
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
#include "games/greatcc/scene/Effects.h"
#include "games/greatcc/scene/PushPin.h"
#include "games/greatcc/scene/SpriteManager.h"
#include "games/greatcc/scene/VButton.h"
#include "games/greatcc/scene/VObject.h"
#include "games/greatcc/ui/VWorldMap.h"
#include "gfx/RecordingRenderer.h"
#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"
#include "platform/Png.h"
#include "ScriptHarness.h"
#include "ui/Style.h"

using namespace choco;
using namespace choco::games::greatcc;
namespace fs = std::filesystem;

namespace {

using MS = ui::MapStyle;
using Tbl = std::shared_ptr<MS>;
Tbl tbl() { return std::make_shared<MS>(); }
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

// An `.anm` of `frames` 12x12 frames with the given script.
std::string makeAnm(int frames, const std::string& script) {
  Bytes o;
  o.tag("ANIM");
  o.u32(1);
  o.u32(2);
  o.str("C:\\build\\x.xml");
  o.str(".\\*");
  o.tag("IMGN");
  for (std::uint32_t v : {0u, 0u, 0u, 0u}) o.u32(v);
  o.str("sheet.png");
  o.str("");
  o.tag("FRMS");
  o.u32(static_cast<std::uint32_t>(frames));
  for (int i = 0; i < frames; ++i) {
    o.u32(6);
    o.u32(6);
    for (int v : {i * 12, 0, 12, 12}) o.u32(static_cast<std::uint32_t>(v));
    o.f32(1);
    for (int k = 0; k < 6; ++k) o.f32(k == 0 ? kNone : 0.0f);
    o.str("src.png");
    o.u32(0);
  }
  o.tag("SCRP");
  o.str(script);
  return o.b;
}

void writeFile(const fs::path& p, const std::string& bytes) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << bytes;
}

struct MapAssets {
  MapAssets() {
    dir = fs::temp_directory_path() / "choco_greatcc_map";
    fs::remove_all(dir);
    fs::create_directories(dir / "map");
    auto png = [&](const char* name, int w, int h) {
      REQUIRE(platform::writePng((dir / name).string(), w, h, std::vector<unsigned char>(static_cast<std::size_t>(w * h * 4), 255).data()));
    };
    png("map/sheet.png", 48, 12);
    png("map/globe_grey.png", 16, 16);
    png("map/globe_grey_over.png", 18, 18);
    png("map/globe_pink.png", 16, 16);
    png("map/globe_pink_over.png", 18, 18);
    png("map/globe_green.png", 16, 16);
    png("map/globe_gold.png", 16, 16);
    png("map/dot_green.png", 6, 6);
    png("map/dot_blue.png", 6, 6);
    png("map/box.png", 30, 30);
    // DoAnim plays four frames at 60 ms, ReverseAnim the same backwards
    writeFile(dir / "map/anim_pin.anm",
              makeAnm(4, "function DoAnim() delay(60) frames{1,2,3,4} end function ReverseAnim() delay(60) frames{4,3,2,1} end"));
    afs = std::make_unique<assets::AssetFS>(dir.string(), "");
    textures = std::make_unique<gfx::TextureCache>(*afs, nullptr);
    sheets = std::make_unique<gfx::SheetCache>(*afs, *textures);
  }
  fs::path dir;
  std::unique_ptr<assets::AssetFS> afs;
  std::unique_ptr<gfx::TextureCache> textures;
  std::unique_ptr<gfx::SheetCache> sheets;
};

Tbl entry(const std::string& state, const std::string& file, bool anim = false) {
  auto t = tbl();
  t->set("state", state);
  t->set("file", file);
  t->set("anim", anim ? 1.0 : 0.0);
  return t;
}
Tbl asset(std::vector<Tbl> entries) {
  auto t = tbl();
  for (auto& e : entries) t->append(e);
  return t;
}

// The assets table every globe shares (map_globe_style).
Tbl globeAssets() {
  auto a = tbl();
  a->set("none", asset({entry("default", "globe_grey")}));
  a->set("current", asset({entry("default", "globe_pink"), entry("over", "globe_pink_over")}));
  a->set("cleared", asset({entry("default", "globe_green")}));
  a->set("mastered", asset({entry("default", "globe_gold")}));
  a->set("none", asset({entry("default", "globe_grey"), entry("over", "globe_grey_over")}));
  return a;
}

// A pin like map_pins' entries: baseLevel, offsets, ten globes, a score box and two labels.
Tbl pinTable(int baseLevel, double x, double y, double movex, double movey) {
  auto pin = tbl();
  pin->set("baseDir", "map/");
  pin->set("spriteLayer", 1.0);
  pin->set("x", x);
  pin->set("y", y);
  pin->set("movex", movex);
  pin->set("movey", movey);
  pin->set("baseLevel", static_cast<double>(baseLevel));
  auto assets = tbl();
  assets->set("base", asset({entry("disabled", "anim_pin", true), entry("enabled", "anim_pin", true)}));
  pin->set("assets", assets);
  auto globes = tbl();
  const auto shared = globeAssets();
  for (int i = 0; i < 10; ++i) {
    auto g = tbl();
    g->set("id", static_cast<double>(i));
    g->set("x", 72.0 + i * 20);
    g->set("y", 22.0 + i * 20);
    g->set("assets", shared);
    char key[16];
    std::snprintf(key, sizeof key, "globe_%02d", i + 1);
    globes->set(key, g);
  }
  globes->set("font", asset({}));  // a table that is not a globe
  pin->set("globes", globes);
  auto box = tbl();
  box->set("x", 75.0);
  box->set("y", -60.0);
  box->set("w", 130.0);
  box->set("h", 110.0);
  box->set("x1", 5.0);
  box->set("x2", 5.0);
  box->set("y1", 5.0);
  box->set("y2", 5.0);
  box->set("mouse", 0.0);
  auto bassets = tbl();
  bassets->set("background", asset({entry("default", "box")}));
  box->set("assets", bassets);
  auto btext = tbl();
  for (const char* k : {"level", "best", "goal", "mastery"}) {
    auto t = tbl();
    t->set("w", 60.0);
    t->set("h", 20.0);
    btext->set(k, t);
  }
  box->set("text", btext);
  pin->set("box", box);
  auto text = tbl();
  for (const char* k : {"name", "loc"}) {
    auto t = tbl();
    t->set("x", -76.0);
    t->set("y", k[0] == 'n' ? -25.0 : -5.0);
    t->set("w", 200.0);
    t->set("h", 50.0);
    t->set("label", std::string("#") + k);
    text->set(k, t);
  }
  pin->set("text", text);
  return pin;
}

struct Fx : MapAssets, test::ScriptEnv {
  Fx() : MapAssets(), ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/greatcc/state", scratch()) {
    ctx.module = games::findModule("greatcc");
    ctx.module->attach(ctx);
    wm.setSheets(sheets.get());
    wm.setTextures(textures.get());
    wm.pushModal("m");
    world = worldOf(ctx);
    REQUIRE(world);
    world->values().load(
        "<values><value name='current_level' min='0' max='61' saved='true'/><value name='highest_level' min='0' max='60' "
        "saved='true'/><value name='total_worth' value='0' saved='true'/><value name='do_map_transition' value='0' saved='true'/>"
        "<value name='map_dot_interval' value='100'/></values>");
    std::string goals = "<levelgoals>";
    for (int i = 1; i <= 60; ++i)
      goals += "<levelgoal level='" + std::to_string(i) + "' goal='" + std::to_string(i * 100) + "' mastery='" + std::to_string(i * 160) + "'/>";
    world->levels().loadGoals(goals + "</levelgoals>");
    run("gCommandQueue = {}");
  }
  static std::string scratch() {
    const fs::path p = fs::temp_directory_path() / "choco_greatcc_map_user";
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p.string();
  }
  // A pin made, loaded, initialised and added.
  std::shared_ptr<PushPin> pin(int baseLevel, double x = 100, double y = 200, double mx = 40, double my = 0) {
    auto t = pinTable(baseLevel, x, y, mx, my);
    auto p = PushPin::create(ctx);
    p->loadFromTable(StyleChain(*t, nullptr));
    p->init();
    mgr.addObject(p);
    return p;
  }
  void frame(double dt = 16) {
    now += dt;
    mgr.setNow(now);
    mgr.updateObjects();
  }
  // One frame and one drawn frame (the move and alpha effects act while drawing).
  void frames(double ms, double dt = 16) {
    for (double t = 0; t < ms; t += dt) {
      frame(dt);
      mgr.draw(rr, {0, 0});
    }
  }
  gfx::RecordingRenderer rr;
  GreatCCWorld* world = nullptr;
  SpriteManager mgr{ctx};
  double now = 1000;
};

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

int hoverGlows(VSprite& s) {
  int n = 0;
  for (const auto& e : s.effectList())
    if (dynamic_cast<HoverGlow*>(e.get())) ++n;
  return n;
}

}  // namespace

// ---- VButton ----

TEST_CASE("VButton: states follow hover, selection and disabling; the label fills the image and is centred on the origin") {
  Fx e;
  auto b = VButton::create(e.ctx, 0, "");
  CHECK(b->name() == "_0");
  CHECK(b->clickable());
  auto t = tbl();
  t->set("baseDir", "map/");
  t->set("id", 7.0);
  t->set("sound", "audio/sfx/buttonclick.ogg");
  auto a = tbl();
  auto st = asset({entry("default", "globe_grey"), entry("over", "globe_grey_over"), entry("on", "globe_pink"),
                   entry("on_over", "globe_pink_over"), entry("disabled", "globe_gold")});
  a->set("g", st);
  t->set("assets", a);
  b->loadFromTable(StyleChain(*t, nullptr));
  CHECK(b->id() == 7);
  CHECK(b->sound() == "audio/sfx/buttonclick.ogg");
  REQUIRE(b->label());
  CHECK(b->label()->layer() == 2);
  b->setText("7");
  b->init();
  CHECK(b->currentState() == "default");
  // the label is sized to the union rectangle of the image (16x16 around the origin) and the label's own first box (10x10 from
  // the origin), so 18x18, and centred on the button's origin
  CHECK(b->label()->width() == 18);
  CHECK(b->label()->height() == 18);
  CHECK(near(b->label()->position().x, -9));
  CHECK(near(b->label()->position().y, -9));
  b->switchState("over", "over");  // now the label is inside the (larger) image: the size is stable
  CHECK(b->label()->width() == 18);
  b->onMouseEnter();
  CHECK(b->currentState() == "over");
  b->onMouseLeave();
  CHECK(b->currentState() == "default");
  b->setSelected(true);
  CHECK(b->currentState() == "on");
  b->doEvent(SpriteEvent::Enter, {0, 0});
  CHECK(b->currentState() == "on_over");
  b->doEvent(SpriteEvent::Leave, {0, 0});
  CHECK(b->currentState() == "on");
  b->setSelected(false);
  CHECK(b->currentState() == "default");
  b->setDisabled(true);
  CHECK(b->currentState() == "disabled");
  CHECK_FALSE(b->clickable());
  b->doEvent(SpriteEvent::Enter, {0, 0});  // a disabled button ignores the pointer
  CHECK(b->currentState() == "disabled");
  b->setDisabled(false);
  CHECK(b->clickable());
  CHECK(b->currentState() == "over");  // the pointer is on it
  // a state the asset lacks falls back
  b->setDisabled(true);
  b->setDisabled(false);
}

TEST_CASE("VButton: a click posts a button-press message named after the sprite for the window that owns the manager") {
  Fx e;
  auto s = tbl();
  auto win = std::make_unique<VSpriteWindow>(e.wm, e.ctx);
  auto& w = static_cast<VSpriteWindow&>(e.wm.initWindow(std::move(win), *s));
  w.postChildrenInit(*s);
  e.wm.popCreation();
  w.setName("map_window");
  w.sprites().setWindowName("map_window");
  auto b = VButton::create(e.ctx, 3, "btn");
  b->setName("level_btn_03");
  w.sprites().addObject(b);
  std::vector<ui::Message> got;
  e.wm.addMessageListener(w.id(), ui::kButtonPress, [&](const ui::Message& m) { got.push_back(m); });
  b->doEvent(SpriteEvent::Click, {0, 0});
  e.wm.dispatchMessages();
  REQUIRE(got.size() == 1);
  CHECK(got[0].name == "level_btn_03");
  CHECK(got[0].target == w.id());
  b->setDisabled(true);
  b->doEvent(SpriteEvent::Click, {0, 0});
  e.wm.dispatchMessages();
  CHECK(got.size() == 1);  // disabled: nothing
}

// ---- VObject::CopyTo ----

TEST_CASE("VObject::CopyTo shares the assets, switches to the current one and copies children and attributes") {
  Fx e;
  auto t = tbl();
  t->set("baseDir", "map/");
  t->set("upgrade", "kThing");
  auto assets = tbl();
  assets->set("dot", asset({entry("green", "dot_green"), entry("blue", "dot_blue")}));
  t->set("assets", assets);
  auto children = tbl();
  auto kid = tbl();
  kid->set("x", 3.0);
  auto kassets = tbl();
  kassets->set("k", asset({entry("default", "dot_green")}));
  kid->set("assets", kassets);
  children->set("kid", kid);
  t->set("children", children);
  auto src = VObject::create(e.ctx, "", "");
  src->loadFromTable(StyleChain(*t, nullptr));
  src->switchAsset("dot", "blue", "default");
  src->setPosition({4, 5});
  auto dst = VObject::create(e.ctx, "", "");
  src->copyTo(*dst);
  CHECK(dst->hasAsset("dot"));
  CHECK(dst->asset("dot") == src->asset("dot"));  // the same VAsset
  CHECK(dst->currentAsset() == "dot");
  CHECK(dst->currentState() == "blue");
  CHECK(dst->attribute("upgrade") == "kThing");
  REQUIRE(dst->childObject("kid"));
  CHECK(dst->childObject("kid") != src->childObject("kid"));
  CHECK(dst->children().size() == 1);
  CHECK(near(dst->position().x, 4));
  // a template that was never initialised copies to an object that is switched afterwards, as the map does
  auto tmpl = VObject::create(e.ctx, "", "");
  tmpl->loadFromTable(StyleChain(*t, nullptr));
  auto fresh = VObject::create(e.ctx, "", "");
  tmpl->copyTo(*fresh);
  CHECK(fresh->currentAsset().empty());
  fresh->switchAsset("dot", "green", "default");
  CHECK(fresh->currentState() == "green");
}

// ---- VPushPin ----

TEST_CASE("VPushPin: ten globes named level_btn_NN with level = id + baseLevel, moved by (-67,-125), labelled, a box, labels") {
  Fx e;
  auto p = e.pin(11, 213, 500, 40, 0);
  CHECK(p->kind() == std::string("VPushPin"));
  CHECK(p->baseLevel() == 11);
  CHECK(near(p->move().x, 40));
  CHECK(p->clickable());
  REQUIRE(p->globes().size() == 10);
  VButton* g1 = p->globe(11);
  REQUIRE(g1);
  CHECK(g1->name() == "level_btn_11");
  CHECK(g1->label()->text() == "11");
  CHECK(near(g1->position().x, 72 - 67));
  CHECK(near(g1->position().y, 22 - 125));
  VButton* g10 = p->globe(20);
  REQUIRE(g10);
  CHECK(g10->name() == "level_btn_20");
  CHECK(near(g10->position().x, 72 + 180 - 67));
  CHECK(p->globe(10) == nullptr);
  CHECK(p->globe(21) == nullptr);
  REQUIRE(p->box());
  CHECK(p->box()->width() == 130);
  CHECK_FALSE(p->box()->visible());
  CHECK_FALSE(p->box()->mouse());
  // the closed layout: the position of each text sprite after loading
  REQUIRE(p->closedPositions().size() == 2);
  CHECK(near(p->closedPositions().at("name").x, -76));
  CHECK(near(p->closedPositions().at("loc").y, -5));
  // globes are children, hidden until the cascade; a plain one-digit label for level 5 is "%0.1i"
  auto q = e.pin(1);
  CHECK(q->globe(5)->label()->text() == "5");
  CHECK(q->globe(5)->name() == "level_btn_05");
}

TEST_CASE("VPushPin::Init: globe states and listeners from the level manager, glows, and the pin's own visibility") {
  Fx e;
  e.world->levels().setHighestComplete(3, true);
  e.world->levels().levelComplete(2, 2 * 160 + 5);  // mastered
  auto p = e.pin(1);
  e.frame();
  CHECK(p->visible());
  // levels 1-3 complete (2 mastered), 4 is next, the rest are locked
  CHECK(p->globe(1)->currentAsset() == "cleared");
  CHECK(p->globe(2)->currentAsset() == "mastered");
  CHECK(p->globe(3)->currentAsset() == "cleared");
  CHECK(p->globe(4)->currentAsset() == "current");
  for (int l = 5; l <= 10; ++l) {
    CHECK(p->globe(l)->currentAsset() == "none");
    CHECK(p->globe(l)->disabled());
    CHECK_FALSE(p->globe(l)->clickable());
    CHECK(p->globe(l)->listenerCount() == 0);  // no score box for a locked level
    CHECK(hoverGlows(*p->globe(l)) == 0);
  }
  for (int l = 1; l <= 4; ++l) {
    CHECK_FALSE(p->globe(l)->disabled());
    CHECK(p->globe(l)->clickable());
    CHECK(p->globe(l)->listenerCount() == 2);  // the score box listener and the hover glow's
    CHECK(hoverGlows(*p->globe(l)) == 1);
  }
  CHECK(p->selectedGlobe() == p->globe(4));
  // closed, the pin carries its own hover glow (the cue to click it)
  CHECK(hoverGlows(*p) == 1);
  CHECK(p->state() == 0);
  // visibility: not yet reachable, the demo's later locations, and the pin about to be animated in
  auto far = e.pin(11);
  CHECK_FALSE(far->visible());  // next (4) < baseLevel (11)
  e.world->levels().setHighestComplete(10, true);
  far->init();
  CHECK(far->visible());
  e.ctx.config.set("firstpeek", "1");
  auto later = e.pin(21);
  e.world->levels().setHighestComplete(30, true);
  later->init();
  CHECK_FALSE(later->visible());  // the demo shows two locations only
  auto still = e.pin(11);
  CHECK(still->visible());
  e.ctx.config.set("firstpeek", "0");
  e.world->levels().setCurrentLevel(11);
  e.world->values().setValue("do_map_transition", 1, false);
  still->init();
  CHECK_FALSE(still->visible());  // its arrival trail has not finished
  e.world->values().setValue("do_map_transition", 0, false);
  still->init();
  CHECK(still->visible());
}

TEST_CASE("VPushPin::SetOpen: labels move 200 ms (after 450 ms when opening), the glow swaps, the other pins close") {
  Fx e;
  auto a = e.pin(1, 100, 200, 40, 10);
  auto b = e.pin(11, 300, 200, 40, 0);
  e.world->levels().setHighestComplete(10, true);
  a->init();
  b->init();
  e.frame();
  a->setOpen(true);
  CHECK(a->open());
  auto* name = a->textSprite("name");
  REQUIRE(name);
  const gfx::PointF start = name->position();
  e.frames(400);
  CHECK(hoverGlows(*a) == 0);  // opening stopped the pin's glow and it has faded out
  CHECK(near(name->position().x, start.x));  // still waiting out the 450 ms delay
  e.frames(500);
  e.frame();
  CHECK(near(name->position().x, start.x + 40));
  CHECK(near(name->position().y, start.y + 10));
  // a second pin opening closes the first
  b->setOpen(true);
  CHECK(b->open());
  CHECK_FALSE(a->open());
  CHECK(hoverGlows(*a) == 1);  // closing makes a new glow
  e.frames(300);
  e.frame();
  CHECK(near(name->position().x, start.x));  // back at the stored position
  CHECK(near(name->position().y, start.y));
  // setting the same state again changes nothing
  const std::size_t effects = name->effectCount();
  a->setOpen(false);
  CHECK(name->effectCount() == effects);
}

TEST_CASE("VPushPin::Update: closed, opening (pin animation), globe cascade one per 25 ms, open, closing") {
  Fx e;
  e.world->levels().setHighestComplete(2, true);
  auto p = e.pin(1);
  e.frame();
  CHECK(p->state() == 0);
  p->setOpen(true);
  e.frame();
  CHECK(p->state() == 1);  // DoAnim is playing
  CHECK(p->animPlaying());
  for (int l = 1; l <= 10; ++l) CHECK_FALSE(p->globe(l)->visible());
  // the animation (four frames at 60 ms) must finish before the cascade starts
  int guard = 0;
  while (p->state() == 1 && guard++ < 100) e.frame();
  REQUIRE(p->state() == 2);
  CHECK(guard > 8);
  for (int l = 1; l <= 10; ++l) CHECK_FALSE(p->globe(l)->mouse());  // disabled during the cascade
  // each update with the timer above 24 ms shows one globe: 16, 32 -> first, then the carry (32-25)+16 ...
  std::vector<int> shown;
  guard = 0;
  while (p->state() == 2 && guard++ < 200) {
    e.frame();
    int n = 0;
    for (int l = 1; l <= 10; ++l) n += p->globe(l)->visible() ? 1 : 0;
    shown.push_back(n);
  }
  REQUIRE(p->state() == 3);
  CHECK(shown.front() == 0);  // 16 ms is not above 24
  for (std::size_t i = 1; i < shown.size(); ++i) CHECK(shown[i] - shown[i - 1] <= 1);  // never more than one per update
  CHECK(shown.back() == 10);
  for (int l = 1; l <= 10; ++l) {
    CHECK(p->globe(l)->visible());
    CHECK(p->globe(l)->mouse());  // enabled again
  }
  // the cascade: level 1 appears with a 300 ms fade, and the completed levels glow after it (two behind the cursor)
  CHECK(p->selectedGlobe() == p->globe(3));
  bool glow = false;
  for (const auto& ef : p->selectedGlobe()->effectList())
    if (dynamic_cast<GlowEffect*>(ef.get())) glow = true;
  CHECK(glow);  // the current globe pulses for ever
  // closing: globes vanish at once, ReverseAnim plays, then the pin is idle again
  p->setOpen(false);
  e.frame();
  CHECK(p->state() == 4);
  for (int l = 1; l <= 10; ++l) CHECK_FALSE(p->globe(l)->visible());
  CHECK(p->selectedGlobe()->effectCount() == 0);  // CancelEffects took the pulse and the hover glow with it
  guard = 0;
  while (p->state() == 4 && guard++ < 100) e.frame();
  CHECK(p->state() == 0);
  // opening again while it closes: back to DoAnim without going idle
  p->setOpen(true);
  e.frame();
  CHECK(p->state() == 1);
  p->setOpen(false);
  e.frame();
  CHECK(p->state() == 1);  // the animation finishes before the pin reacts
  guard = 0;
  while (p->state() == 1 && guard++ < 100) e.frame();
  CHECK(p->state() == 4);  // then ReverseAnim, without the cascade
  for (int l = 1; l <= 10; ++l) CHECK_FALSE(p->globe(l)->visible());
}

TEST_CASE("VPushPin::OnClick: story mode opens an idle pin and ignores a busy one; endless sends the click to its first globe") {
  Fx e;
  auto s = tbl();
  auto win = std::make_unique<VSpriteWindow>(e.wm, e.ctx);
  auto& w = static_cast<VSpriteWindow&>(e.wm.initWindow(std::move(win), *s));
  w.postChildrenInit(*s);
  e.wm.popCreation();
  w.setName("map_window");
  w.sprites().setWindowName("map_window");
  auto t = pinTable(1, 100, 200, 40, 0);
  auto p = PushPin::create(e.ctx);
  p->loadFromTable(StyleChain(*t, nullptr));
  p->init();
  w.sprites().addObject(p);
  std::vector<ui::Message> got;
  e.wm.addMessageListener(w.id(), ui::kButtonPress, [&](const ui::Message& m) { got.push_back(m); });
  e.world->settings().mode = 0;
  p->doEvent(SpriteEvent::Click, {0, 0});
  CHECK(p->open());
  p->doEvent(SpriteEvent::Click, {0, 0});
  CHECK_FALSE(p->open());  // the state machine has not run yet (state 0): a click toggles
  p->doEvent(SpriteEvent::Click, {0, 0});
  CHECK(p->open());
  double clock = 1000;
  for (int i = 0; i < 8; ++i) {
    e.wm.setNowMs(clock += 16);
    w.onAnimate();
  }
  REQUIRE(p->state() != 0);
  p->doEvent(SpriteEvent::Click, {0, 0});
  CHECK(p->open());  // busy: ignored
  e.world->settings().mode = 1;
  p->doEvent(SpriteEvent::Click, {0, 0});
  e.wm.dispatchMessages();
  REQUIRE(got.size() == 1);
  CHECK(got[0].name == "level_btn_01");
}

TEST_CASE("VPushPin::SetScoreInfo: level, best, goal and mastery texts; 200 ms fade in, 500 ms fade out after 500 ms") {
  Fx e;
  auto p = e.pin(1);
  e.world->levels().setHighestComplete(2, true);
  p->init();
  e.frame();
  TiledObject* box = p->box();
  REQUIRE(box);
  p->setScoreInfo(true, 2);
  CHECK(box->visible());
  CHECK(box->textSprite("goal")->text() == "$200");
  CHECK(box->textSprite("mastery")->text() == "$320");
  CHECK(box->textSprite("best")->text() == "$0");
  CHECK(box->textSprite("level")->text().find("  2") != std::string::npos);
  e.frame();
  e.frame(100);
  gfx::RecordingRenderer rr;
  e.mgr.draw(rr, {0, 0});
  CHECK(box->alpha() > 0.3f);  // part way through the 200 ms fade
  CHECK(box->alpha() < 0.9f);
  e.frame(150);
  e.mgr.draw(rr, {0, 0});
  CHECK(box->alpha() == doctest::Approx(1.0f));
  // leaving: nothing for 500 ms while fully opaque, then 500 ms down
  p->setScoreInfo(false, 2);
  e.frame(400);
  e.mgr.draw(rr, {0, 0});
  CHECK(box->alpha() == doctest::Approx(1.0f));
  e.frame(200);
  e.frame(250);
  e.mgr.draw(rr, {0, 0});
  CHECK(box->alpha() < 1.0f);
  CHECK(box->alpha() > 0.0f);
  // coming back mid-fade restarts from the current alpha with no delay
  const float mid = box->alpha();
  p->setScoreInfo(true, 1);
  e.frame();
  e.frame(20);
  e.mgr.draw(rr, {0, 0});
  CHECK(box->alpha() >= mid);
  // leaving while partly faded in has no delay: it starts falling at once
  p->setScoreInfo(false, 1);
  e.frames(1100);
  CHECK(box->alpha() == doctest::Approx(0.0f));
  p->setScoreInfo(true, 1);
  e.frames(48);
  const float partial = box->alpha();
  CHECK(partial > 0.0f);
  CHECK(partial < 1.0f);
  p->setScoreInfo(false, 1);
  e.frames(64);
  CHECK(box->alpha() < partial);
}

// ---- VWorldMap ----

namespace {
struct MapFx : Fx {
  explicit MapFx(bool withPaths = true) {
    auto s = tbl();
    auto pins = tbl();
    pins->set("iquitos", pinTable(1, 144, 361, -10, 35));
    pins->set("grytviken", pinTable(11, 213, 500, 40, 0));
    pins->set("namche", pinTable(21, 513, 278, 40, 0));
    s->set("pins", pins);
    auto paths = tbl();
    auto trail = tbl();
    for (int i = 0; i < 5; ++i) {
      auto d = tbl();
      d->set("state", i < 3 ? "green" : "blue");
      d->set("x", 100.0 + i * 10);
      d->set("y", 50.0);
      trail->append(d);
    }
    if (withPaths) paths->set("grytviken", trail);
    s->set("paths", paths);
    auto dot = tbl();
    dot->set("baseDir", "map/");
    auto dassets = tbl();
    dassets->set("dot", asset({entry("green", "dot_green"), entry("blue", "dot_blue")}));
    dot->set("assets", dassets);
    s->set("dot", dot);
    auto w = std::make_unique<VWorldMap>(wm, ctx);
    map = &static_cast<VWorldMap&>(wm.initWindow(std::move(w), *s));
    map->setName("world_map_window");
    map->postChildrenInit(*s);
    wm.popCreation();
    style = s;
  }
  void tick(double dt = 16) {
    now += dt;
    wm.setNowMs(now);
    map->onAnimate();
    map->sprites().draw(rr, {0, 0});  // the fades and moves act while drawing
  }
  void ticks(double ms, double dt = 16) {
    for (double t = 0; t < ms; t += dt) tick(dt);
  }
  VWorldMap* map = nullptr;
  Tbl style;
  double now = 1000;
};
}  // namespace

TEST_CASE("VWorldMap: pins in key order, paths, the dot template; natives live with the window and die with it") {
  {
    MapFx e;
    CHECK(e.map->pins().size() == 3);
    CHECK(e.map->pins().count("iquitos") == 1);
    REQUIRE(e.map->paths().count("grytviken") == 1);
    CHECK(e.map->paths().at("grytviken").size() == 5);
    CHECK(e.map->paths().at("grytviken")[3].state == "blue");
    CHECK(e.map->sprites().objects().size() == 3);
    CHECK(e.map->detail().find("pins=3") != std::string::npos);
    for (const char* n : {"ResetMap", "DoCheatMap", "PlayTransition", "ShowCurrentLocation"}) CHECK_FALSE(e.isNil(n));
    CHECK(e.run("PlayTransition('nowhere')") == 0);
    e.wm.destroyAll();
    for (const char* n : {"ResetMap", "DoCheatMap", "PlayTransition", "ShowCurrentLocation"}) CHECK(e.isNil(n));
  }
}

TEST_CASE("VWorldMap: the current location is the pin holding the next level; none after level 60") {
  MapFx e;
  auto cur = [&] {
    PushPin* p = e.map->currentLocationPin();
    return p ? p->baseLevel() : 0;
  };
  CHECK(cur() == 1);  // a fresh game: iquitos
  e.world->levels().setHighestComplete(9, true);
  CHECK(cur() == 1);  // level 10 is still iquitos
  e.world->levels().setHighestComplete(10, true);
  CHECK(cur() == 11);
  e.world->levels().setHighestComplete(25, true);
  CHECK(cur() == 21);
  e.world->levels().setHighestComplete(60, true);
  CHECK(cur() == 0);  // (a pin for 51 is not in this table; and 61 is past every location)
  e.world->levels().setHighestComplete(5, true);
  e.map->showCurrentLocation();
  CHECK(e.map->pins().at("iquitos")->open());
  CHECK_FALSE(e.map->pins().at("grytviken")->open());
  CHECK(e.map->detail().find("open=iquitos") != std::string::npos);
}

TEST_CASE("VWorldMap: a level_btn_NN message selects the level and queues CloseWorldMap(); all button presses are consumed") {
  MapFx e;
  CHECK_FALSE(e.map->onMessage({ui::kCloseWindow, "level_btn_07", 0}));  // another message type: not ours
  CHECK(e.map->onMessage({ui::kButtonPress, "btn_play", 0}));  // consumed, nothing queued
  e.run("n = table.getn(gCommandQueue)");
  CHECK(e.getNumber("n") == 0);
  CHECK(e.map->onMessage({ui::kButtonPress, "level_btn_07", 0}));
  CHECK(e.world->levels().currentLevel() == 7);
  e.run("n = table.getn(gCommandQueue)");
  CHECK(e.getNumber("n") == 1);
  CHECK(e.map->onMessage({ui::kButtonPress, "level_btn_", 0}));  // too short to carry a level (needs more than 10 characters)
  e.run("n = table.getn(gCommandQueue)");
  CHECK(e.getNumber("n") == 1);
  CHECK(e.map->onMessage({ui::kButtonPress, "level_btn_12", 0}));
  CHECK(e.world->levels().currentLevel() == 12);
}

TEST_CASE("VWorldMap::PlayTransition: dots every map_dot_interval, a locked map, then the pin glows and opens") {
  MapFx e;
  e.world->levels().setHighestComplete(10, true);
  e.world->levels().setCurrentLevel(11);
  e.world->values().setValue("do_map_transition", 1, false);
  for (auto& [k, p] : e.map->pins()) p->init();
  PushPin* gryt = e.map->pins().at("grytviken").get();
  CHECK_FALSE(gryt->visible());
  e.map->playTransition("nowhere");  // unknown: nothing starts
  CHECK(e.map->transition().empty());
  CHECK(e.map->sprites().exclusiveCount() == 0);
  e.map->playTransition("grytviken");
  CHECK(e.map->transition() == "grytviken");
  CHECK(e.map->sprites().exclusiveCount() == 1);  // an empty entry: the map ignores the mouse
  CHECK(e.map->sprites().lockDown() == false);
  CHECK_FALSE(e.map->sprites().isValidSprite(gryt));
  e.tick();  // the first frame has no elapsed time: the 1 ms timer is still running
  CHECK(e.map->dotsPlaced() == 0);
  e.tick();  // 16 ms later the first dot appears
  CHECK(e.map->dotsPlaced() == 1);
  auto lastDot = [&]() -> VObject* {
    const auto& all = e.map->sprites().objects();
    for (auto it = all.rbegin(); it != all.rend(); ++it)
      if (!dynamic_cast<PushPin*>(it->get())) return dynamic_cast<VObject*>(it->get());
    return nullptr;
  };
  VObject* dot = lastDot();
  REQUIRE(dot);
  CHECK(near(dot->position().x, 100));
  CHECK(dot->currentState() == "green");
  CHECK(dot->effectCount() == 2);  // the 200 ms fade and the 400 ms glow
  e.ticks(96);  // the interval (100 ms) is not up yet
  CHECK(e.map->dotsPlaced() == 1);
  e.tick();
  CHECK(e.map->dotsPlaced() == 2);
  e.ticks(1000);
  CHECK(e.map->dotsPlaced() == 5);
  CHECK(lastDot()->currentState() == "blue");
  // the trail is done: the pin appears with a glow and a fade, the map unlocks, and the pin opens when the glow ends
  CHECK(e.map->transition().empty());
  CHECK(gryt->visible());
  CHECK(e.map->sprites().exclusiveCount() == 0);
  CHECK_FALSE(gryt->open());
  e.ticks(1000);
  CHECK(gryt->open());
  CHECK(e.map->sprites().objects().size() == 3 + 5);
}

TEST_CASE("DoCheatMap: refused without cheatmode, and the Lua native is the window's doCheat with it") {
  MapFx e;
  e.run("r = DoCheatMap(5, '11')");
  CHECK(e.getString("r") == "Invalid Cheat");
  CHECK(e.world->levels().highestComplete() == 0);
  e.ctx.config.set("cheatmode", "1");
  e.run("r = DoCheatMap(5, '11')");
  CHECK(e.getString("r") == "Set highest level complete to 11");
  CHECK(e.world->levels().highestComplete() == 11);
}

TEST_CASE("VWorldMap::DoCheat: money to the open pin's location, jump to a level, set the highest level") {
  MapFx e;
  CHECK(e.map->doCheat(3, "100") == "No location selected");
  CHECK(e.map->doCheat(3, "0") == "No money added");
  CHECK(e.map->doCheat(3, "-5") == "No money added");
  e.map->pins().at("iquitos")->setOpen(true);
  CHECK(e.map->doCheat(3, "250") == "Added 250 to location 1 and lifetime total");
  CHECK(e.world->levels().locationCash(1) == doctest::Approx(250));
  CHECK(e.world->values().getReal("total_worth", 0) == doctest::Approx(250));
  CHECK(e.map->doCheat(3, "50") == "Added 50 to location 1 and lifetime total");
  CHECK(e.world->levels().locationCash(3) == doctest::Approx(300));  // the same location
  CHECK(e.map->doCheat(0, "") == "");
  CHECK(e.map->doCheat(2, "x") == "");
  CHECK(e.map->doCheat(9, "1") == "Invalid Cheat");
  CHECK(e.map->doCheat(4, "0") == "Invalid Level");
  CHECK(e.map->doCheat(4, "61") == "Invalid Level");
  CHECK(e.map->doCheat(4, "12") == "");
  CHECK(e.world->levels().currentLevel() == 12);
  e.run("n = table.getn(gCommandQueue)");
  CHECK(e.getNumber("n") == 1);  // CloseWorldMap() queued
  CHECK(e.map->doCheat(5, "0") == "Invalid Level");
  CHECK(e.map->doCheat(5, "11") == "Set highest level complete to 11");
  CHECK(e.world->levels().highestComplete() == 11);
  CHECK(e.map->pins().at("grytviken")->open());  // ResetMap showed the new current location
  CHECK_FALSE(e.map->pins().at("iquitos")->open());
  // the Lua native returns the same string
  e.ctx.config.set("cheatmode", "1");
  CHECK(e.run("r = DoCheatMap(9, '1')") == 0);
  CHECK(e.getString("r") == "Invalid Cheat");
}

TEST_CASE("VWorldMap: clicks reach the globes through the sprite window; a locked globe is a hover target but its click does nothing") {
  MapFx e;
  e.world->levels().setHighestComplete(1, true);
  for (auto& [k, p] : e.map->pins()) p->init();
  e.map->showCurrentLocation();
  e.ticks(2500);
  PushPin* iq = e.map->pins().at("iquitos").get();
  REQUIRE(iq->state() == 3);
  VButton* done = iq->globe(1);
  VButton* next = iq->globe(2);
  VButton* locked = iq->globe(5);
  // the globes' absolute centres: the pin position plus the offset, minus the constant
  auto centre = [&](VButton* b) { return gfx::PointI{static_cast<int>(144 + b->position().x), static_cast<int>(361 + b->position().y)}; };
  std::vector<ui::Message> got;
  e.wm.addMessageListener(e.map->id(), ui::kButtonPress, [&](const ui::Message& m) { got.push_back(m); });
  // a locked globe is still a hover target (SetGlobeEnabled turns `mouse` on for every globe), but its click does nothing
  e.map->onMouseMove(centre(locked).x, centre(locked).y);
  e.tick();
  CHECK(e.map->sprites().hover() == locked);
  CHECK(e.map->onMouseDown(centre(locked).x, centre(locked).y));
  CHECK(e.map->onMouseUp(centre(locked).x, centre(locked).y));
  e.wm.dispatchMessages();
  CHECK(got.empty());
  // the next level's globe sends level_btn_02 and the map selects it
  e.map->onMouseMove(centre(next).x, centre(next).y);
  e.tick();
  CHECK(e.map->sprites().hover() == next);
  CHECK(e.map->onMouseDown(centre(next).x, centre(next).y));
  CHECK(e.map->onMouseUp(centre(next).x, centre(next).y));
  e.wm.dispatchMessages();
  REQUIRE(got.size() == 1);
  CHECK(got[0].name == "level_btn_02");
  CHECK(e.world->levels().currentLevel() == 2);
  e.map->onMouseMove(centre(done).x, centre(done).y);
  e.tick();
  CHECK(e.map->sprites().hover() == done);
}
