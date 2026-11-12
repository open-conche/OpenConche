// The Great Chocolate Chase, slice 20 (docs/design/greatcc.md section 6): the developer console and its ten commands, the Debug button's
// zone reload, the achievement cheat window and MainDebug, and the Ctrl+T / Ctrl+S overlays' state and text.
#include <doctest/doctest.h>

#include <climits>
#include <filesystem>
#include <memory>
#include <string>

#include "games/Registry.h"
#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/debug/DebugCommand.h"
#include "games/greatcc/scene/SpriteManager.h"
#include "games/greatcc/scene/VObject.h"
#include "games/greatcc/shop/ShopWindow.h"
#include "games/greatcc/ui/TMainMenu.h"
#include "games/greatcc/ui/VChooseAchievements.h"
#include "script/RunFile.h"
#include "sdk/Users.h"
#include "ui/widgets/InputWidgets.h"
#include "ScriptHarness.h"

using namespace choco;
using namespace choco::games::greatcc;

namespace {

namespace fs = std::filesystem;
const std::string kDebug = std::string(CHOCO_TEST_ASSETS) + "/greatcc/debug";

std::string scratch(const std::string& tag) {
  const fs::path p = fs::temp_directory_path() / ("choco_greatcc_debug_" + tag);
  std::error_code ec;
  fs::remove_all(p, ec);
  fs::create_directories(p, ec);
  return p.string();
}

struct Env : test::ScriptEnv {
  explicit Env(const std::string& tag) : ScriptEnv(kDebug, scratch(tag)), host(ctx) {
    ctx.module = games::findModule("greatcc");
    ctx.module->attach(ctx);
    booted = host.boot();
    ctx.users->create("Zed");  // a current player: the saved values and the achievements persist
  }
  ~Env() { wm.destroyAll(); }
  GreatCCWorld& world() { return *worldOf(ctx); }
  script::ScriptHost host;
  bool booted = false;
};

// A shop window in a modal of its own (no level loaded), with sprites by name in its manager.
struct ShopEnv : Env {
  explicit ShopEnv(const std::string& tag) : Env(tag) {
    wm.pushModal("m");
    ui::MapStyle style;
    style.set("x", 0.0).set("y", 0.0).set("w", 800.0).set("h", 600.0);
    shop = static_cast<ShopWindow*>(&wm.initWindow(std::make_unique<ShopWindow>(wm, ctx), style));
    wm.popCreation();
    wm.topModal()->defaultFocus = shop->id();
  }
  std::shared_ptr<VObject> sprite(const std::string& name) {
    auto o = VObject::create(ctx, "", "");
    o->setName(name);
    shop->sprites().addObject(o);
    return o;
  }
  ShopWindow* shop = nullptr;
};

ui::KeyEvent key(const std::string& name, bool ctrl) {
  ui::KeyEvent k;
  k.name = name;
  k.ctrl = ctrl;
  return k;
}

}  // namespace

TEST_CASE("console line: the command word is lower-cased, the arguments keep their case, empty pieces go, only spaces separate") {
  DebugCommand c = parseDebugCommand("SetPos  Station01 10  20 ");
  CHECK(c.word == "setpos");
  REQUIRE(c.args.size() == 3);
  CHECK(c.args[0] == "Station01");
  CHECK(c.args[1] == "10");
  CHECK(c.args[2] == "20");
  CHECK(parseDebugCommand("").word.empty());
  CHECK(parseDebugCommand("closeconsole").args.empty());
  CHECK(parseDebugCommand("choctint,bucket,1,0,0").word == "choctint,bucket,1,0,0");  // commas are not separators
  CHECK(parseDebugCommand("setpos\ta 1 2").word == "setpos\ta");                      // nor are tabs
  CHECK(parseDebugCommand("   ").word.empty());
  CHECK(parseDebugCommand(" x").word.empty());  // the word is what is before the first space
}

TEST_CASE("console numbers: atoi and atof reads, out of range clamps, nothing non-finite") {
  CHECK(debugToInt("12") == 12);
  CHECK(debugToInt("-3") == -3);
  CHECK(debugToInt("12x") == 12);
  CHECK(debugToInt("abc") == 0);
  CHECK(debugToInt("99999999999999999999") == INT_MAX);
  CHECK(debugToInt("-99999999999999999999") == INT_MIN);
  CHECK(debugToFloat("1.5") == doctest::Approx(1.5f));
  CHECK(debugToFloat("-.25") == doctest::Approx(-0.25f));
  CHECK(debugToFloat("zzz") == 0.0f);
  CHECK(debugToFloat("nan") == 0.0f);
  CHECK(debugToFloat("inf") == 0.0f);
  CHECK(debugToFloat("1e99") < 3.1e38f);
  CHECK(debugAddMinutes(1000, 2) == 121000);
  CHECK(debugAddMinutes(1000, -5) == 0);
  CHECK(debugAddMinutes(INT_MAX - 5, 1000000) == INT_MAX);
  CHECK(debugAddMinutes(0, INT_MIN) == 0);
  CHECK(chocolateLayerTypeFromString("Bucket") == 0);
  CHECK(chocolateLayerTypeFromString("kIngredientLayer") == 1);
  CHECK(chocolateLayerTypeFromString("chocolate") == 2);
  CHECK(chocolateLayerTypeFromString("INFUSION") == 3);
  CHECK(chocolateLayerTypeFromString("toppinglayer") == 4);
  CHECK(chocolateLayerTypeFromString("nothing") == -1);
  CHECK(chocolateLayerTypeFromString("") == -1);
}

TEST_CASE("console: Ctrl+C opens console.lua inside the modal, the field has the focus, closeconsole removes it and Ctrl+C works again") {
  ShopEnv e("console");
  REQUIRE(e.booted);
  ui::ModalWindow* modal = e.wm.topModal();
  CHECK_FALSE(e.shop->onKeyDown(key("C", false)));  // no ctrl: not the console
  CHECK(modal->find("console_window") == nullptr);
  CHECK(e.shop->onKeyDown(key("C", true)));
  ui::Window* console = modal->find("console_window");
  REQUIRE(console);
  CHECK(console->parent() == modal);  // a plain child of the modal, the temporary window is gone
  CHECK(modal->find("command_textedit") != nullptr);
  CHECK(e.wm.focus() == modal->find("command_textedit")->id());
  CHECK(e.wm.topModal() == modal);  // not a modal
  e.wm.clearDestroyed();
  // the buttons' commands reach DoCommand
  e.run("DoCommand('closeconsole')");
  CHECK(modal->find("console_window") == nullptr);
  CHECK(e.wm.focus() == e.shop->id());  // the key focus is back on the shop
  CHECK(e.shop->onKeyDown(key("c", true)));
  CHECK(modal->find("console_window") != nullptr);
  // a closeconsole with no console does nothing
  e.run("DoCommand('closeconsole')");
  e.run("DoCommand('closeconsole')");
  CHECK(modal->find("console_window") == nullptr);
}

TEST_CASE("console: the commands set values, positions, layers, visibility, scale, delay and upgrades; bad input does nothing") {
  ShopEnv e("commands");
  REQUIRE(e.booted);
  GreatCCWorld& w = e.world();
  auto top = e.sprite("station01");
  auto child = VObject::create(e.ctx, "", "");
  child->setName("child");
  top->addChild(child);
  e.shop->doCommand("setvalue plain 42");
  CHECK(w.values().getReal("plain", 0) == doctest::Approx(42));
  e.shop->doCommand("SETVALUE brandnew 7.5 extra ignored");
  CHECK(w.values().getReal("brandnew", 0) == doctest::Approx(7.5));
  e.shop->doCommand("setvalue plain");  // too few arguments
  e.shop->doCommand("setvalue plain abc");  // not a number reads 0
  CHECK(w.values().getReal("plain", 99) == doctest::Approx(0));
  top->attachToAnchor("hand");
  e.shop->doCommand("setpos station01 30 -40");
  CHECK(top->position().x == 30);
  CHECK(top->position().y == -40);
  CHECK(top->anchorName().empty());  // detached from its anchor
  e.shop->doCommand("setpos station01 5");
  CHECK(top->position().x == 30);
  e.shop->doCommand("setpos Station01 1 1");  // names are case sensitive
  CHECK(top->position().x == 30);
  e.shop->doCommand("setpos nobody 1 1");
  e.shop->doCommand("setpos station01 nan inf");
  CHECK(top->position().x == 0);
  // a top-level sprite sorts by its depth, a child by its layer
  e.shop->doCommand("setlayer station01 6");
  CHECK(top->depth() == 6);
  e.shop->doCommand("setlayer child 3");
  CHECK(child->layer() == 3);
  CHECK(child->depth() != 3);
  e.shop->doCommand("setvisible station01 0");
  CHECK_FALSE(top->visible());
  e.shop->doCommand("setvisible station01 2");
  CHECK(top->visible());
  e.shop->doCommand("setscale child 2 0.5");
  CHECK(child->scale().x == doctest::Approx(2.0f));
  CHECK(child->scale().y == doctest::Approx(0.5f));
  e.shop->doCommand("setdelay station01 250");
  CHECK(top->delay() == doctest::Approx(250));
  // choctint: missing components are 1; the tint is for the layers added afterwards
  e.shop->doCommand("choctint Topping 0.5");
  REQUIRE(w.chocolateTint(4));
  CHECK(w.chocolateTint(4)->r == doctest::Approx(0.5f));
  CHECK(w.chocolateTint(4)->g == doctest::Approx(1.0f));
  CHECK(w.chocolateTint(4)->b == doctest::Approx(1.0f));
  CHECK(w.chocolateTint(4)->a == doctest::Approx(1.0f));
  e.shop->doCommand("choctint kBucketLayer 0 0.25 0.75 9");
  REQUIRE(w.chocolateTint(0));
  CHECK(w.chocolateTint(0)->g == doctest::Approx(0.25f));
  CHECK(w.chocolateTint(0)->b == doctest::Approx(0.75f));
  e.shop->doCommand("choctint");
  e.shop->doCommand("unknowncommand 1 2 3");
  e.shop->doCommand("");
  e.shop->doCommand("addminutes 1");  // no level clock here
  e.shop->doCommand("addminutes abc");
  e.shop->doCommand("getupgrade nosuch nothing");  // an upgrade that does not exist
}

TEST_CASE("console: Ctrl+T and Ctrl+S toggle the debug flags of the shop's sprite manager and never consume the key") {
  ShopEnv e("overlay_keys");
  SpriteManager& m = e.shop->sprites();
  CHECK(m.debugFlags() == 0);
  CHECK(m.debugText().empty());
  CHECK_FALSE(e.shop->onKeyDown(key("T", true)));
  CHECK(m.debugFlags() == SpriteManager::kDebugAssets);
  CHECK_FALSE(e.shop->onKeyDown(key("S", true)));
  CHECK(m.debugFlags() == (SpriteManager::kDebugAssets | SpriteManager::kDebugMouse));
  CHECK(e.shop->detail().find(" debug=3") != std::string::npos);
  CHECK_FALSE(e.shop->onKeyDown(key("t", false)));  // no ctrl: nothing
  CHECK(m.debugFlags() == 3);
  CHECK_FALSE(e.shop->onKeyDown(key("T", true)));
  CHECK(m.debugFlags() == SpriteManager::kDebugMouse);
  CHECK_FALSE(e.shop->onKeyDown(key("S", true)));
  CHECK(m.debugFlags() == 0);
}

TEST_CASE("overlay text: Ctrl+S shows the mouse and the trace sprite and replaces Ctrl+T's asset text") {
  ShopEnv e("overlay_text");
  SpriteManager& m = e.shop->sprites();
  m.setDebugFlags(SpriteManager::kDebugMouse);
  CHECK(m.debugText() == "x: -10000\ny: -10000\n");
  m.onMouseMove({12, 34});
  CHECK(m.debugText() == "x: 12\ny: 34\n");
  m.setTraceSprite("station03");
  CHECK(m.debugText() == "x: 12\ny: 34\n\nTraceSprite: station03");
  m.setDebugFlags(SpriteManager::kDebugAssets);
  const std::string assets = m.debugText();
  CHECK(assets.rfind("SystemRam: 0 bytes\nCurrent VRam: 0 bytes\nMax VRam: 0 bytes\nPeak VRam: 0 bytes\n", 0) == 0);
  m.setDebugFlags(SpriteManager::kDebugAssets | SpriteManager::kDebugMouse);
  CHECK(m.debugText().rfind("x: 12", 0) == 0);  // both on: only the mouse text
}

TEST_CASE("overlay text: the asset text counts the textures the sprites drew this frame, w * h * 4 bytes each") {
  ShopEnv e("overlay_assets");
  SpriteManager& m = e.shop->sprites();
  m.setDebugFlags(SpriteManager::kDebugAssets);
  CHECK(m.debugText().find("Current VRam: 0 bytes") != std::string::npos);
}

TEST_CASE("zone reload: zones and points are reused by name, connections rebuilt, the path dropped, disabled kept, removed ones stay") {
  ZoneGraph g;
  const std::string xml1 =
      "<zonecontroller><zone name='z'><point name='a' x='0' y='0'/><point name='b' x='30' y='0'/><point name='c' x='30' y='40'/>"
      "<connection first='a' second='b'/><connection first='b' second='c'/></zone></zonecontroller>";
  REQUIRE(g.load(xml1));
  ZonePoint* a = g.findPoint("z", "a");
  ZonePoint* b = g.findPoint("z", "b");
  ZonePoint* c = g.findPoint("z", "c");
  REQUIRE((a && b && c));
  g.disablePoint("z", "c", true);
  g.addPath(a, b);
  REQUIRE_FALSE(g.pathEmpty());
  CHECK(b->connections.size() == 2);
  // the file changed: a moved, c's connection dropped, a new point d joined to a
  const std::string xml2 =
      "<zonecontroller><zone name='z'><point name='a' x='5' y='6'/><point name='b' x='30' y='0'/><point name='d' x='0' y='50'/>"
      "<connection first='a' second='b'/><connection first='a' second='d'/></zone></zonecontroller>";
  REQUIRE(g.load(xml2));
  CHECK(g.pathEmpty());
  CHECK(g.findPoint("z", "a") == a);  // the same objects
  CHECK(g.findPoint("z", "b") == b);
  CHECK(a->x == 5);
  CHECK(a->y == 6);
  CHECK(a->connections.size() == 2);
  CHECK(b->connections.size() == 1);
  CHECK(g.findPoint("z", "c") == c);  // removed from the file, still there
  CHECK(c->connections.empty());
  CHECK(c->disabled);  // the flag is not reset
  CHECK(g.findPoint("z", "d") != nullptr);
  CHECK(g.zones().size() == 1);
  // text that is not XML changes nothing
  CHECK_FALSE(g.load("<zonecontroller"));
  CHECK(a->x == 5);
}

TEST_CASE("DoDebug: the shop reads zones/zones.xml again; before a graph exists it does nothing") {
  ShopEnv e("dodebug");
  REQUIRE(e.booted);
  e.run("DoDebug()");  // no graph yet
  CHECK(e.shop->zones() == nullptr);
  auto g = std::make_shared<ZoneGraph>();
  e.shop->setZones(g);
  ZonePoint& stale = g->addPoint(g->addZone("avatar"), 1, 1, "a", false, false);
  stale.disabled = true;
  ZonePoint* extra = &g->addPoint(*g->findZone("avatar"), 9, 9, "gone", false, false);
  g->addPath(&stale, &stale);
  REQUIRE_FALSE(g->pathEmpty());
  e.run("DoDebug()");
  CHECK(g->pathEmpty());
  CHECK(g->findPoint("avatar", "a") == &stale);
  CHECK(stale.x == 10);  // the file's coordinates
  CHECK(stale.y == 10);
  CHECK(stale.disabled);
  CHECK(g->findPoint("avatar", "gone") == extra);
  CHECK(g->findPoint("avatar", "b") != nullptr);
  CHECK(stale.connections.size() == 1);
  CHECK(!e.log.contains("[native-error]"));
}

TEST_CASE("AcquireAll and release: every achievement is acquired and saved with nothing queued; release zeroes the statistic") {
  Env e("acquire");
  REQUIRE(e.booted);
  AchievementManager& a = e.world().achievements();
  e.world().values().setValue("num_bars", 9, true);
  CHECK(a.acquiredNames().empty());
  a.acquireAll();
  CHECK(a.acquiredNames().size() == a.size());
  CHECK(a.pending().empty());  // no end-level announcement
  CHECK(e.world().values().getReal("num_bars", 0) == doctest::Approx(9));  // the statistic is untouched
  CHECK(a.release("alpha", "B"));
  CHECK_FALSE(a.isAcquired("b"));
  CHECK(e.world().values().getReal("num_bars", 1) == doctest::Approx(0));
  a.releaseAll();
  CHECK(a.acquiredNames().empty());
  // MainDebug is what the main menu's native does
  ui::MapStyle style;
  style.set("x", 0.0).set("y", 0.0).set("w", 800.0).set("h", 600.0);
  e.wm.pushModal("menu");
  e.wm.initWindow(std::make_unique<TMainMenu>(e.wm, e.ctx), style);
  e.wm.popCreation();
  REQUIRE_FALSE(e.isNil("MainDebug"));
  e.run("MainDebug()");
  CHECK(a.acquiredNames().size() == a.size());
  CHECK(a.pending().empty());
  CHECK(e.world().progress().getInt("achievement.zed", 0) == 1);
}

TEST_CASE("VChooseAchievements: a toggle per achievement in order, the buttons and the toggles acquire and release") {
  Env e("choose");
  REQUIRE(e.booted);
  AchievementManager& a = e.world().achievements();
  a.acquire("alpha", "a");
  e.wm.pushModal("choose_dialog");
  script::runFileInto(e.ctx, e.lua.L(), *e.wm.topModal(), "scripts/choose.lua");
  VChooseAchievements* win = nullptr;
  for (const auto& c : e.wm.topModal()->children())
    if (auto* w = dynamic_cast<VChooseAchievements*>(c.get())) win = w;
  REQUIRE(win);
  CHECK(win->toggles().size() == a.size());
  VButton* t = win->toggle("btn_achieve_a");
  REQUIRE(t);
  CHECK(t->selected());  // acquired at the start
  REQUIRE(win->toggle("btn_achieve_zed"));
  CHECK_FALSE(win->toggle("btn_achieve_zed")->selected());
  CHECK(win->toggle("btn_achieve_zed")->label()->text() == "zed");  // the raw achievement id is the label
  // two columns, 25 px rows, in category order ("" first: loose; then alpha: a, b, med; then zeta: zed)
  auto pos = [&](const char* n) { return win->toggle(std::string("btn_achieve_") + n)->position(); };
  CHECK(pos("loose").x == 20);
  CHECK(pos("loose").y == 20);
  CHECK(pos("a").x == doctest::Approx((415 - 40) / 2 + 20));
  CHECK(pos("a").y == 20);
  CHECK(pos("b").x == 20);
  CHECK(pos("b").y == 45);
  CHECK(pos("med").x == doctest::Approx((415 - 40) / 2 + 20));
  CHECK(pos("med").y == 45);
  // Acquire All, then Release All
  e.wm.sendMessage({ui::kButtonPress, "btn_acquire", win->id()});
  CHECK(a.acquiredNames().size() == a.size());
  for (const auto& [name, tg] : win->toggles()) CHECK(tg->selected());
  e.world().values().setValue("num_bars", 5, true);
  e.wm.sendMessage({ui::kButtonPress, "btn_release", win->id()});
  CHECK(a.acquiredNames().empty());
  CHECK(e.world().values().getReal("num_bars", 1) == doctest::Approx(0));  // the backing statistic goes with it
  for (const auto& [name, tg] : win->toggles()) CHECK_FALSE(tg->selected());
  // one toggle: it flips itself, then the message follows
  t->setSelected(true);
  e.wm.sendMessage({ui::kButtonPress, "btn_achieve_a", win->id()});
  CHECK(a.isAcquired("a"));
  t->setSelected(false);
  e.wm.sendMessage({ui::kButtonPress, "btn_achieve_a", win->id()});
  CHECK_FALSE(a.isAcquired("a"));
  // Test Notify shows the acquired ones
  a.acquire("alpha", "a");
  e.wm.sendMessage({ui::kButtonPress, "btn_notify", win->id()});
  CHECK(e.world().notices().visible().size() <= 1);
  // an unrelated button press is not handled
  CHECK_FALSE(win->onMessage({ui::kButtonPress, "something_else", win->id()}));
  CHECK_FALSE(win->onMessage({ui::kDefaultAction, "btn_acquire", win->id()}));
}

TEST_CASE("Ctrl+D on the main menu runs MainDebug only with cheatmode and only while the menu is the top modal") {
  Env e("ctrl_d");
  REQUIRE(e.booted);
  AchievementManager& a = e.world().achievements();
  ui::MapStyle style;
  style.set("x", 0.0).set("y", 0.0).set("w", 800.0).set("h", 600.0);
  e.wm.pushModal("menu");
  auto* menu = static_cast<TMainMenu*>(&e.wm.initWindow(std::make_unique<TMainMenu>(e.wm, e.ctx), style));
  e.wm.popCreation();
  CHECK_FALSE(menu->onKeyDown(key("D", true)));
  CHECK(a.acquiredNames().empty());  // no cheatmode: nothing
  e.config.set("cheatmode", "1");
  CHECK_FALSE(menu->onKeyDown(key("D", false)));  // no ctrl
  CHECK(a.acquiredNames().empty());
  e.wm.pushModal("above");  // another modal on top: the key is not the menu's
  menu->onKeyDown(key("D", true));
  CHECK(a.acquiredNames().empty());
  e.wm.popModal("above");
  e.wm.clearDestroyed();
  menu->onKeyDown(key("d", true));
  CHECK(a.acquiredNames().size() == a.size());
  CHECK(a.pending().empty());
  CHECK(e.log.contains("[greatcc] MainDebug: all achievements acquired"));
}
