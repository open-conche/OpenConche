// The Great Chocolate Chase, slice 14 (docs/design/greatcc.md section 6): the tutorials. Synthetic fixtures only: a level window with a
// few named sprites, tutorials built from MapStyle (the shape of locations/tutorials.lua), a world with a current player.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "assets/AssetFS.h"
#include "games/Registry.h"
#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/natives/StateNatives.h"
#include "games/greatcc/scene/VSpriteWindow.h"
#include "games/greatcc/tutorial/Tutorial.h"
#include "games/greatcc/tutorial/TutorialManager.h"
#include "games/greatcc/tutorial/TutorialSpider.h"
#include "games/greatcc/tutorial/TutorialWindow.h"
#include "gfx/TextureCache.h"
#include "gfx/text/Font.h"
#include "gfx/text/TextLayout.h"
#include "platform/Png.h"
#include "ScriptHarness.h"
#include "sdk/Users.h"
#include "ui/Style.h"

using namespace choco;
using namespace choco::games::greatcc;
namespace fs = std::filesystem;

namespace {

using MS = ui::MapStyle;
using Tbl = std::shared_ptr<MS>;
Tbl tbl() { return std::make_shared<MS>(); }
Tbl list(std::vector<double> v) {
  auto t = tbl();
  for (double d : v) t->append(d);
  return t;
}
Tbl names(std::vector<std::string> v) {
  auto t = tbl();
  for (auto& s : v) t->append(s);
  return t;
}
// { name, kind, action = bool }
Tbl trig(const std::string& name, int kind, int action = -1) {
  auto t = tbl();
  t->append(name).append(static_cast<double>(kind));
  if (action >= 0) t->set("action", static_cast<double>(action));
  return t;
}
Tbl triggers(std::vector<Tbl> v) {
  auto t = tbl();
  for (auto& e : v) t->append(e);
  return t;
}

// The art every tutorial shares: a panel nine-slice texture and a button picture.
Tbl artAssets() {
  auto state = tbl();
  state->set("state", "default").set("file", "tut/back").set("anim", 0.0);
  auto group = tbl();
  group->append(state);
  auto assets = tbl();
  assets->set("background", group);
  return assets;
}
Tbl skipButton() {
  auto state = tbl();
  state->set("state", "default").set("file", "tut/btn").set("anim", 0.0);
  auto group = tbl();
  group->append(state);
  auto assets = tbl();
  assets->set("default", group);
  auto b = tbl();
  b->set("name", "tutorial_skip").set("x", 20.0).set("y", 575.0).set("clickable", 1.0);
  b->set("assets", assets);
  return b;
}
Tbl textStyle() {
  auto t = tbl();
  t->set("x1", 8.0).set("y1", 8.0).set("x2", 8.0).set("y2", 8.0);
  t->set("margin", std::vector<double>{4, 4, 4, 4});
  return t;
}
// A step with a panel at (x, y).
Tbl step(const std::string& label, double x = 100, double y = 20) {
  auto s = tbl();
  s->set("x", x).set("y", y).set("w", 120.0).set("h", 80.0);
  s->set("style", textStyle());
  s->set("label", label);
  return s;
}
Tbl stepsOf(std::vector<Tbl> v) {
  auto t = tbl();
  for (auto& s : v) t->append(s);
  return t;
}
Tbl tutorialOf(std::vector<Tbl> steps, const std::string& type = "", const std::string& object = "") {
  auto t = tbl();
  t->set("minlevel", 1.0).set("maxlevel", 3.0).set("assets", artAssets()).set("skip", skipButton());
  t->set("steps", stepsOf(std::move(steps)));
  if (!type.empty()) t->set("trigger_type", type);
  if (!object.empty()) t->set("trigger_object", object);
  return t;
}
Tbl rootOf(std::vector<std::pair<std::string, Tbl>> tutorials) {
  auto r = tbl();
  r->set("modes", list({0}));
  for (auto& [name, t] : tutorials) r->set(name, t);
  return r;
}

struct TutAssets {
  TutAssets() {
    dir = fs::temp_directory_path() / "choco_greatcc_tutorial";
    fs::remove_all(dir);
    fs::create_directories(dir / "tut");
    REQUIRE(platform::writePng((dir / "tut/back.png").string(), 40, 40, std::vector<unsigned char>(40 * 40 * 4, 255).data()));
    REQUIRE(platform::writePng((dir / "tut/btn.png").string(), 30, 12, std::vector<unsigned char>(30 * 12 * 4, 255).data()));
  }
  fs::path dir;
};

struct Fx : TutAssets, test::ScriptEnv {
  Fx() : TutAssets(), ScriptEnv(dir.string(), scratch()), textures(fs, nullptr) {
    ctx.module = games::findModule("greatcc");
    ctx.module->attach(ctx);
    wm.setTextures(&textures);
    wm.setNowMs(now);
    wm.pushModal("game");
    world = worldOf(ctx);
    REQUIRE(world);
    ctx.users = std::make_shared<sdk::UserStore>(scratch() + "_store");
    world->progress().attach(ctx.users);
    ctx.users->create("Zed");
    ctx.strings.loadRows({{"t1", "First"}, {"t2", "Second"}, {"t3", "Third"}, {"t4", "Fourth"}});
    MS style;
    style.set("x", 0.0).set("y", 0.0).set("w", 800.0).set("h", 600.0);
    level = static_cast<VSpriteWindow*>(&wm.initWindow(std::make_unique<VSpriteWindow>(wm, ctx), style));
    level->setName("level");
    level->postChildrenInit(style);
    wm.popCreation();
    world->tutorials().setLevelWindow(level->id());
    world->tutorials().setCurrentLevel(1);
  }
  ~Fx() { wm.destroyAll(); }
  static std::string scratch() {
    const fs::path p = fs::temp_directory_path() / "choco_greatcc_tutorial_user";
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p.string();
  }

  std::shared_ptr<VObject> sprite(const std::string& name) {
    auto s = VObject::create(ctx, name, "");
    level->sprites().addObject(s);
    sprites[name] = s;
    return s;
  }
  void sprites_(std::initializer_list<const char*> ns) {
    for (const char* n : ns) sprite(n);
  }
  void load(const Tbl& root) { world->tutorials().loadFromTable(*root); }
  TutorialManager& tm() { return world->tutorials(); }
  void run_ms(double ms) {
    for (double t = 0; t < ms; t += 16) wm.tickWindowAnimations(now += 16);
  }
  TutorialWindow* window() { return dynamic_cast<TutorialWindow*>(wm.topModal()->find("tutorial_window")); }
  // The step the tutorial is on (0 when none is running).
  int stepNo() { return tm().active() ? tm().active()->stepNumber() : 0; }
  // A step's request takes effect once the fade-in of the step before it is over.
  void settle() { run_ms(600); }
  void click(const std::string& name) { sprites.at(name)->doEvent(SpriteEvent::Click, {}); }
  void arrive(const std::string& name) { tellTutorialsOfArrival(*sprites.at(name)); }
  void resume(const std::string& name, bool still = false) { tellTutorialsOfResume(still, *sprites.at(name)); }

  gfx::TextureCache textures;
  GreatCCWorld* world = nullptr;
  VSpriteWindow* level = nullptr;
  std::map<std::string, std::shared_ptr<VObject>> sprites;
  double now = 1000;
};

}  // namespace

// ================================================================ the rules

TEST_CASE("Tutorial::shouldShow: the truth table of state, tryonce and repeatable") {
  Fx f;
  struct Row {
    int state;
    bool tryonce, repeatable, show;
  };
  const Row rows[] = {
      {0, false, false, true}, {0, true, false, true},  {1, false, false, true},  {1, true, false, false},
      {3, false, false, false}, {3, true, false, false}, {3, false, true, true},  {1, true, true, true},
      {2, false, false, false}, {0, true, true, true},
  };
  for (const Row& r : rows) {
    auto t = tutorialOf({step("t1")});
    t->set("tryonce", r.tryonce ? 1.0 : 0.0).set("repeatable", r.repeatable ? 1.0 : 0.0);
    f.load(rootOf({{"a", t}}));
    Tutorial* tut = f.tm().tutorial("a");
    REQUIRE(tut);
    tut->setState(r.state, false);
    CHECK_MESSAGE(tut->shouldShow() == r.show, "state " << r.state << " tryonce " << r.tryonce << " repeatable " << r.repeatable);
  }
}

TEST_CASE("Tutorial::setState only goes up, and saves only when asked") {
  Fx f;
  f.load(rootOf({{"a", tutorialOf({step("t1")})}}));
  Tutorial* t = f.tm().tutorial("a");
  t->setState(1, false);
  CHECK(t->state() == 1);
  CHECK(f.world->progress().get("tutorial.a") == "");  // not saved
  t->setState(3, true);
  CHECK(f.world->progress().get("tutorial.a") == "3");
  t->setState(1, true);  // lower: ignored
  CHECK(t->state() == 3);
  CHECK(f.world->progress().get("tutorial.a") == "3");
  t->setState(3, true);
  CHECK(t->state() == 3);
}

TEST_CASE("Tutorial: minlevel and window and pause read what the table says; the modes come from the table around it") {
  Fx f;
  auto own = tutorialOf({step("t1")});
  own->set("modes", list({1, 2, 3}));
  own->set("window", "dlg");
  own->set("pause", 0.0);
  f.load(rootOf({{"Plain", tutorialOf({step("t1")})}, {"own", own}}));
  Tutorial* plain = f.tm().tutorial("plain");  // the key is lower-cased
  Tutorial* o = f.tm().tutorial("own");
  REQUIRE(plain);
  REQUIRE(o);
  CHECK(plain->inMode(0));
  CHECK_FALSE(plain->inMode(1));  // `modes = {0}` is the root's
  CHECK_FALSE(o->inMode(0));
  CHECK(o->inMode(2));
  CHECK(plain->inRange(1));
  CHECK(plain->inRange(3));
  CHECK_FALSE(plain->inRange(0));
  CHECK_FALSE(plain->inRange(4));
  CHECK(o->windowName() == "dlg");
  CHECK(plain->windowName().empty());
  CHECK(plain->pause());
  CHECK_FALSE(o->pause());
  // `modes = {0}` is a table too, so the original makes a tutorial of it as well: it has no steps and no trigger
  CHECK(f.tm().tutorialCount() == 3);
  REQUIRE(f.tm().tutorial("modes"));
  CHECK(f.tm().tutorial("modes")->numSteps() == 0);
}

TEST_CASE("TutorialStep: clickAny is false only with a duration or something named; otherwise any click advances") {
  Fx f;
  auto plainStep = step("t1");  // nothing named, no duration: any click
  auto withDuration = step("t2");
  withDuration->set("duration", 1000.0);
  auto withTrigger = step("t3");
  withTrigger->set("trigger_objects", triggers({trig("a", kClick)}));
  auto withAction = step("t4");
  withAction->set("action_objects", names({"a"}));
  auto highlightOnly = step("t1");
  highlightOnly->set("highlight_objects", names({"a"}));
  auto forced = step("t2");
  forced->set("trigger_objects", triggers({trig("a", kClick)}));
  forced->set("clickAny", 1.0);
  auto zero = step("t3");
  zero->set("duration", 0.0);
  f.load(rootOf({{"a", tutorialOf({plainStep, withDuration, withTrigger, withAction, highlightOnly, forced, zero})}}));
  Tutorial* t = f.tm().tutorial("a");
  REQUIRE(t->numSteps() == 7);
  CHECK(t->step(1)->clickAny());
  CHECK_FALSE(t->step(2)->clickAny());
  CHECK_FALSE(t->step(3)->clickAny());
  CHECK_FALSE(t->step(4)->clickAny());
  CHECK(t->step(5)->clickAny());  // a highlight names nothing that can be clicked
  CHECK(t->step(6)->clickAny());
  CHECK_FALSE(t->step(7)->clickAny());  // duration 0 is not negative
}

TEST_CASE("TutorialStep: the lists, the exclusive set rules and the panel") {
  Fx f;
  auto s = step("t1", 150, 40);
  s->set("highlight_objects", names({"avatar"}));
  s->set("action_objects", names({"cust"}));
  s->set("trigger_objects", triggers({trig("st", kArrival), trig("quiet", kResume, 0), trig("list", kNoTrigger)}));
  auto arrow = tbl();
  arrow->set("x", 160.0).set("y", 90.0);
  auto kids = tbl();
  kids->set("arrow", arrow);
  s->set("children", kids);
  s->set("duration", 5.0).set("delay", 6.0).set("pause", 0.0).set("dim", 0.0).set("skip", 0.0);
  f.load(rootOf({{"a", tutorialOf({s})}}));
  const auto st = f.tm().tutorial("a")->step(1);
  REQUIRE(st);
  CHECK(st->lookupNames() == std::set<std::string>{"cust", "st", "quiet", "list"});
  CHECK(st->exemptNames() == std::set<std::string>{"avatar", "cust", "st", "quiet", "list"});
  CHECK(st->actionNames() == std::set<std::string>{"cust", "st", "list"});  // action = false is left out
  CHECK(st->trigger("st") == kArrival);
  CHECK(st->trigger("quiet") == kResume);
  CHECK(st->trigger("list") == kNoTrigger);
  CHECK(st->trigger("cust") == -1);  // named but without a trigger
  CHECK(st->trigger("nobody") == -1);
  CHECK(st->duration() == 5);
  CHECK(st->delay() == 6);
  CHECK_FALSE(st->pause());
  CHECK_FALSE(st->dim());
  CHECK_FALSE(st->skip());
  CHECK(st->hasPanel());
  CHECK(st->position().x == 150.0f);
  // the arrow's place was a place on the screen: it is now relative to the panel
  const VObject* a = st->childObject("arrow");
  REQUIRE(a);
  CHECK(a->position().x == 10.0f);
  CHECK(a->position().y == 50.0f);
  CHECK_FALSE(a->mouse());
  REQUIRE(st->text());
  CHECK(st->text()->text() == "t1");  // (no string table here: the label itself)
  CHECK(st->text()->position().x == 0.0f);
  CHECK_FALSE(st->visible());  // a step is hidden until it begins
}

TEST_CASE("TutorialStep: a step with no panel size has no panel and no text") {
  Fx f;
  auto bare = tbl();
  bare->set("trigger_objects", triggers({trig("c1", kResume, 0)}));
  f.load(rootOf({{"a", tutorialOf({bare})}}));
  const auto st = f.tm().tutorial("a")->step(1);
  REQUIRE(st);
  CHECK_FALSE(st->hasPanel());
  CHECK(st->text() == nullptr);
}

// ================================================================ checking and running

TEST_CASE("TutorialManager::checkTutorial: the mode, the level and the state decide; a running tutorial blocks the rest") {
  Fx f;
  f.sprites_({"a1", "b1"});
  auto onlyLevel2 = tutorialOf({step("t1")});
  onlyLevel2->set("minlevel", 2.0).set("maxlevel", 2.0);
  auto endless = tutorialOf({step("t1")});
  endless->set("modes", list({1}));
  f.load(rootOf({{"a", tutorialOf({step("t1")})}, {"b", tutorialOf({step("t2")})}, {"lvl2", onlyLevel2}, {"endl", endless}}));
  f.tm().checkTutorial("lvl2");  // level 1
  CHECK_FALSE(f.tm().active());
  f.tm().checkTutorial("endl");  // story mode
  CHECK_FALSE(f.tm().active());
  f.tm().checkTutorial("nobody");  // unknown
  CHECK_FALSE(f.tm().active());
  f.tm().checkTutorial("a");
  REQUIRE(f.tm().active());
  CHECK(f.tm().active()->tutorialName() == "a");
  CHECK(f.tm().tutorialInEffect());
  CHECK(f.tm().active()->state() == 1);
  CHECK(f.tm().active()->stepNumber() == 1);
  f.tm().checkTutorial("b");  // blocked while a is running
  CHECK(f.tm().active()->tutorialName() == "a");
  CHECK(f.tm().tutorial("b")->state() == 0);
  CHECK(f.level->sprites().lockDown());  // the level is locked
  CHECK(f.window());
  CHECK(f.world->gameStates().get("TutorialTime_0") == "1");
}

TEST_CASE("TutorialManager::checkTutorialsByTrigger: type and object, in name order, an empty object matches anything") {
  Fx f;
  f.load(rootOf({{"zz", tutorialOf({step("t1")}, "customer_arrival", "")},
                 {"a2", tutorialOf({step("t2")}, "customer_arrival", "bob")},
                 {"a1", tutorialOf({step("t3")}, "customer_order", "bob")}}));
  f.tm().checkTutorialsByTrigger("customer_arrival", "alice");  // a2 wants bob; zz takes anyone
  REQUIRE(f.tm().active());
  CHECK(f.tm().active()->tutorialName() == "zz");
  f.tm().resetTutorials();
  CHECK_FALSE(f.tm().active());
  f.tm().checkTutorialsByTrigger("customer_arrival", "bob");  // a2 comes before zz
  CHECK(f.tm().active()->tutorialName() == "a2");
  f.tm().resetTutorials();
  f.tm().checkTutorialsByTrigger("customer_order", "alice");
  CHECK_FALSE(f.tm().active());
  f.tm().checkTutorialsByTrigger("customer_order", "bob");
  CHECK(f.tm().active()->tutorialName() == "a1");
}

TEST_CASE("TutorialManager::checkTutorial: with Tutorials off the tutorial counts as started and nothing shows") {
  Fx f;
  f.load(rootOf({{"a", tutorialOf({step("t1")})}}));
  f.tm().showTutorials(false);
  CHECK_FALSE(f.tm().shouldShowTutorials());
  CHECK_FALSE(f.world->progress().showTutorials());
  f.tm().checkTutorial("a");
  CHECK_FALSE(f.tm().active());
  CHECK_FALSE(f.window());
  CHECK(f.tm().tutorial("a")->state() == 1);
  CHECK(f.world->gameStates().get("TutorialEnd") == "1");
  CHECK(f.world->progress().get("tutorial.a") == "1");
}

TEST_CASE("Tutorial: the steps advance by their triggers, and each step builds what the spider says") {
  Fx f;
  f.sprites_({"avatar", "cust", "st", "quiet", "other", "btn"});
  auto s1 = step("t1");
  s1->set("highlight_objects", names({"avatar"}));
  s1->set("action_objects", names({"cust"}));
  s1->set("trigger_objects", triggers({trig("st", kArrival)}));
  auto s2 = step("t2");
  s2->set("trigger_objects", triggers({trig("quiet", kResume, 0)}));
  auto s3 = step("t3");
  s3->set("trigger_objects", triggers({trig("btn", kClick), trig("other", kNoTrigger)}));
  auto s4 = step("t4");
  s4->set("trigger_objects", triggers({trig("st", kArrival)}));
  f.load(rootOf({{"a", tutorialOf({s1, s2, s3, s4})}}));
  f.tm().checkTutorial("a");
  REQUIRE(f.tm().active());
  Tutorial* t = f.tm().active().get();
  auto& level = f.level->sprites();
  // step 1: the customer and the station can be clicked, nothing else; the dim is up
  CHECK(f.stepNo() == 1);
  CHECK(level.exclusiveCount() == 2);
  CHECK(level.isValidSprite(f.sprites["cust"].get()));
  CHECK(level.isValidSprite(f.sprites["st"].get()));
  CHECK_FALSE(level.isValidSprite(f.sprites["avatar"].get()));  // highlighted, not clickable
  CHECK_FALSE(level.isValidSprite(f.sprites["other"].get()));
  CHECK(level.softPaused());
  REQUIRE(t->spider()->dimEffect());
  CHECK(f.sprites["other"]->hasEffect(t->spider()->dimEffect()->id()));
  CHECK_FALSE(f.sprites["avatar"]->hasEffect(t->spider()->dimEffect()->id()));
  CHECK_FALSE(f.sprites["st"]->hasEffect(t->spider()->dimEffect()->id()));
  // a click, a resume, an arrival of the wrong kind or on the wrong sprite change nothing
  f.settle();
  f.click("st");
  f.resume("st");
  f.arrive("cust");
  f.arrive("other");
  CHECK(f.stepNo() == 1);
  // the arrival at the station advances
  f.arrive("st");
  CHECK(f.stepNo() == 2);
  CHECK(level.exclusiveCount() == 0);  // the quiet one is not clickable: the level stays locked out
  CHECK(level.lockDown());
  f.settle();
  f.resume("quiet", true);  // still waiting: not the end of the wait
  CHECK(f.stepNo() == 2);
  f.resume("quiet");
  CHECK(f.stepNo() == 3);
  CHECK(level.exclusiveCount() == 2);
  CHECK(level.isValidSprite(f.sprites["btn"].get()));
  CHECK(level.isValidSprite(f.sprites["other"].get()));  // clickable, though it never advances
  f.settle();
  f.click("other");  // kNoTrigger: no listener
  CHECK(f.stepNo() == 3);
  CHECK(f.sprites["other"]->listenerCount() == 0);
  CHECK(f.sprites["btn"]->listenerCount() == 1);
  f.click("btn");
  CHECK(f.stepNo() == 4);
  // the sprite of step 3 let go of the listener at the next update
  f.settle();
  CHECK(f.sprites["btn"]->listenerCount() == 0);
  f.arrive("st");  // the last step: the tutorial is over
  CHECK(t->isComplete());
  CHECK(t->state() == 3);
  CHECK_FALSE(f.tm().tutorialInEffect());
  CHECK(level.exclusiveCount() == 0);
  CHECK_FALSE(level.softPaused());
  CHECK(f.world->progress().get("tutorial.a") == "3");
  // the window fades the tutorial out, pops it (the lock ends) and closes
  f.run_ms(100);
  CHECK_FALSE(f.tm().active());
  CHECK_FALSE(level.lockDown());
  CHECK(f.world->gameStates().get("TutorialTime_0") == "0");
  CHECK(f.world->gameStates().get("TutorialEnd") == "0");
  f.run_ms(700);
  f.wm.clearDestroyed();
  CHECK(f.window() == nullptr);
}

TEST_CASE("Tutorial: a step that is still fading in remembers the request and takes it when the fade is over") {
  Fx f;
  f.sprites_({"st"});
  auto s1 = step("t1");
  s1->set("trigger_objects", triggers({trig("st", kArrival)}));
  auto s2 = step("t2");
  s2->set("trigger_objects", triggers({trig("st", kArrival)}));
  f.load(rootOf({{"a", tutorialOf({s1, s2})}}));
  f.tm().checkTutorial("a");
  f.run_ms(48);    // the sprites take the listener at their next update
  f.arrive("st");  // 50 ms into the step's 500 ms fade-in
  CHECK(f.stepNo() == 1);
  f.run_ms(200);
  CHECK(f.stepNo() == 1);
  f.run_ms(400);
  CHECK(f.stepNo() == 2);  // taken once the fade was over
}

TEST_CASE("Tutorial: duration and delay gate the advance, and Update retries it") {
  Fx f;
  f.sprites_({"st"});
  auto s1 = step("t1");
  s1->set("trigger_objects", triggers({trig("st", kArrival)}));
  s1->set("duration", 2000.0);
  auto s2 = step("t2");
  s2->set("trigger_objects", triggers({trig("st", kArrival)}));
  s2->set("delay", 1500.0);
  auto s3 = step("t3");
  s3->set("trigger_objects", triggers({trig("st", kArrival)}));
  f.load(rootOf({{"a", tutorialOf({s1, s2, s3})}}));
  f.tm().checkTutorial("a");
  f.run_ms(800);
  f.arrive("st");  // 800 ms into a 2000 ms step
  CHECK(f.stepNo() == 1);
  f.run_ms(800);
  CHECK(f.stepNo() == 1);
  f.run_ms(600);  // 2200 ms in: the request that was remembered goes through
  CHECK(f.stepNo() == 2);
  f.settle();
  f.arrive("st");  // the delay is counted from the request
  CHECK(f.stepNo() == 2);
  f.run_ms(1000);
  CHECK(f.stepNo() == 2);
  f.run_ms(700);
  CHECK(f.stepNo() == 3);
  // never begun or requested: always complete
  const auto fresh = f.tm().tutorial("a")->step(1);
  f.tm().resetTutorials();
  CHECK(fresh->durationComplete());
  CHECK(fresh->delayComplete());
}

TEST_CASE("Tutorial: soft pause follows the step and the tutorial") {
  Fx f;
  f.sprites_({"st"});
  auto paused = step("t1");
  paused->set("trigger_objects", triggers({trig("st", kArrival)}));
  auto free = step("t2");
  free->set("pause", 0.0);
  free->set("trigger_objects", triggers({trig("st", kArrival)}));
  auto never = tutorialOf({step("t3")});
  never->set("pause", 0.0);
  never->set("steps", stepsOf({[&] {
                       auto s = step("t3");
                       s->set("trigger_objects", triggers({trig("st", kArrival)}));
                       return s;
                     }()}));
  f.load(rootOf({{"a", tutorialOf({paused, free})}, {"b", never}}));
  f.tm().checkTutorial("a");
  CHECK(f.level->sprites().softPaused());
  f.settle();
  f.arrive("st");
  CHECK_FALSE(f.level->sprites().softPaused());  // the second step does not pause
  f.settle();
  f.arrive("st");
  CHECK_FALSE(f.level->sprites().softPaused());
  f.run_ms(800);
  f.tm().resetTutorials();
  f.tm().checkTutorial("b");  // the tutorial says pause = false: its steps do not pause
  REQUIRE(f.tm().active());
  CHECK_FALSE(f.level->sprites().softPaused());
}

TEST_CASE("Tutorial: the dim fades an exempt sprite in and out as the steps go on") {
  Fx f;
  f.sprites_({"x", "y", "z", "st"});
  auto s1 = step("t1");
  s1->set("highlight_objects", names({"x"}));
  s1->set("trigger_objects", triggers({trig("st", kArrival)}));
  auto s2 = step("t2");
  s2->set("highlight_objects", names({"y"}));
  s2->set("trigger_objects", triggers({trig("st", kArrival)}));
  auto s3 = step("t3");
  s3->set("highlight_objects", names({"y", "z"}));
  s3->set("trigger_objects", triggers({trig("st", kArrival)}));
  f.load(rootOf({{"a", tutorialOf({s1, s2, s3})}}));
  f.tm().checkTutorial("a");
  Tutorial* t = f.tm().active().get();
  const int dim = t->spider()->dimEffect()->id();
  CHECK_FALSE(f.sprites["x"]->hasEffect(dim));
  CHECK(f.sprites["y"]->hasEffect(dim));
  CHECK(f.sprites["z"]->hasEffect(dim));
  f.settle();
  f.arrive("st");  // step 2: y loses the dim (it had it), x is exempt no longer and gets its own tint instead of the shared one
  CHECK(f.stepNo() == 2);
  CHECK_FALSE(f.sprites["y"]->hasEffect(dim));
  CHECK(f.sprites["y"]->effectCount() == 1);  // the copy that fades out
  CHECK_FALSE(f.sprites["x"]->hasEffect(dim));
  CHECK(f.sprites["x"]->effectCount() == 1);  // its own tint that fades in
  CHECK(f.sprites["z"]->hasEffect(dim));
  f.settle();
  f.arrive("st");  // step 3: z loses the dim, y stays exempt, x keeps its tint
  CHECK(f.stepNo() == 3);
  CHECK_FALSE(f.sprites["z"]->hasEffect(dim));
  CHECK(f.sprites["x"]->effectCount() == 1);
  f.settle();
  // the end: the dim and the tints stop and fade out by themselves
  f.arrive("st");
  CHECK(t->isComplete());
  CHECK(t->spider()->dimEffect() == nullptr);
  f.run_ms(600);
  CHECK(f.sprites["x"]->effectCount() == 0);
}

TEST_CASE("Tutorial: Skip turns the option off, collapses the tutorial and ends the lock") {
  Fx f;
  f.sprites_({"st"});
  auto s1 = step("t1");
  s1->set("trigger_objects", triggers({trig("st", kArrival)}));
  auto s2 = step("t2");
  s2->set("trigger_objects", triggers({trig("st", kArrival)}));
  f.load(rootOf({{"a", tutorialOf({s1, s2})}}));
  f.tm().checkTutorial("a");
  Tutorial* t = f.tm().active().get();
  REQUIRE(t->skipButton());
  CHECK(t->skipButton()->visible());
  CHECK(t->skipButton()->name() == "tutorial_skip");
  f.settle();
  CHECK_FALSE(f.tm().handleMessage({ui::kButtonPress, "other_button", 0}));
  CHECK(f.tm().handleMessage({ui::kButtonPress, "tutorial_skip", 0}));
  CHECK_FALSE(f.tm().shouldShowTutorials());
  CHECK_FALSE(f.world->progress().showTutorials());
  CHECK(t->isComplete());
  CHECK_FALSE(f.tm().tutorialInEffect());
  CHECK(t->state() == 1);  // skipped, not finished
  CHECK_FALSE(f.level->sprites().softPaused());
  CHECK(f.level->sprites().exclusiveCount() == 0);
  f.run_ms(100);
  CHECK_FALSE(f.level->sprites().lockDown());
  CHECK(f.world->gameStates().get("TutorialEnd") == "1");  // the option is off
  f.run_ms(700);
  f.wm.clearDestroyed();
  CHECK(f.window() == nullptr);
  // a later trigger does not show it again
  f.tm().checkTutorial("a");
  CHECK_FALSE(f.tm().active());
}

TEST_CASE("Tutorial: Skip on the button of the window is the same, through a real click on the button sprite") {
  Fx f;
  f.sprites_({"st"});
  auto s1 = step("t1");
  s1->set("trigger_objects", triggers({trig("st", kArrival)}));
  f.load(rootOf({{"a", tutorialOf({s1})}}));
  f.tm().checkTutorial("a");
  f.settle();
  f.window()->tutorial()->skipButton()->doEvent(SpriteEvent::Click, {});  // posts the message at the window
  f.wm.dispatchMessages();
  CHECK_FALSE(f.tm().tutorialInEffect());
  CHECK_FALSE(f.world->progress().showTutorials());
}

TEST_CASE("Tutorial: the Continue button's message advances a step that names it") {
  Fx f;
  auto s1 = step("t1");
  s1->set("trigger_objects", triggers({trig("button_advance", kClick)}));
  auto b = skipButton();
  b->set("name", "button_advance").set("x", 5.0).set("y", 6.0);
  s1->set("button", b);
  s1->set("clickAny", 0.0);
  f.load(rootOf({{"a", tutorialOf({s1, step("t2")})}}));
  f.tm().checkTutorial("a");
  f.settle();
  const auto st = f.tm().active()->step(1);
  REQUIRE(st);
  CHECK(st->clickAny() == false);
  VObject* button = st->childObject("button_advance");
  CHECK(button == nullptr);  // a button is a child, not a table child
  bool found = false;
  for (const auto& c : st->children())
    if (c->name() == "button_advance") {
      found = true;
      CHECK(c->position().x == 5.0f);
    }
  CHECK(found);
  CHECK(f.tm().handleMessage({ui::kButtonPress, "button_advance", 0}));
  CHECK(f.stepNo() == 2);
}

TEST_CASE("Tutorial window: a click on a step with clickAny advances it and is taken; others fall through") {
  Fx f;
  auto any = step("t1");  // nothing named: any click advances
  auto second = step("t2");
  second->set("trigger_objects", triggers({trig("zz", kClick)}));
  f.load(rootOf({{"a", tutorialOf({any, second})}}));
  f.tm().checkTutorial("a");
  f.settle();
  TutorialWindow* w = f.window();
  REQUIRE(w);
  CHECK(w->onMouseDown(700, 500));  // taken: the step advanced
  CHECK(f.stepNo() == 2);
  f.settle();
  CHECK_FALSE(w->onMouseDown(700, 500));  // a step that waits for something: the click goes on to the game
  CHECK(f.stepNo() == 2);
  CHECK_FALSE(w->onMouseUp(700, 500));
  CHECK_FALSE(w->onMouseMove(700, 500));
}

// ================================================================ the window filter

namespace {

struct Pane : ui::Window {
  Pane(ui::WindowManager& wm, const std::string& name, int x, int y, int w, int h) : ui::Window(wm) {
    setName(name);
    setRect({x, y, w, h});
  }
};

}  // namespace

TEST_CASE("Tutorial::handleMouse: no window named allows everything; a named one allows what has a trigger, seen through kIgnore") {
  Fx f;
  auto s = step("t1");
  s->set("trigger_objects", triggers({trig("buy", kClick), trig("list", kNoTrigger), trig("backdrop", kIgnore)}));
  auto plain = tutorialOf({step("t1")});
  auto named = tutorialOf({s});
  named->set("window", "container");
  f.load(rootOf({{"plain", plain}, {"named", named}}));
  ui::Window& container = f.wm.topModal()->adoptFront(std::make_unique<Pane>(f.wm, "container", 50, 40, 300, 200));
  // front to back: backdrop (everything), buy, list, then a plain panel
  container.adoptFront(std::make_unique<Pane>(f.wm, "panel", 0, 0, 300, 200));
  container.adoptFront(std::make_unique<Pane>(f.wm, "list", 100, 20, 100, 100));
  container.adoptFront(std::make_unique<Pane>(f.wm, "buy", 10, 150, 80, 30));
  ui::Window& bd = container.adoptFront(std::make_unique<Pane>(f.wm, "backdrop", 0, 0, 300, 200));
  ui::Window* modal = f.wm.topModal();
  // no window named by the tutorial: allowed anywhere
  f.tm().checkTutorial("plain");
  REQUIRE(f.tm().active());
  CHECK(f.tm().handleMouse(modal, {60, 50}));
  CHECK(f.tm().handleMouse(nullptr, {60, 50}));
  f.tm().resetTutorials();
  f.tm().checkTutorial("named");
  REQUIRE(f.tm().active());
  Tutorial* t = f.tm().active().get();
  // the backdrop is transparent to the probe: what is under it decides
  CHECK(t->handleMouse(modal, {50 + 10 + 5, 40 + 150 + 5}));   // buy: a click trigger
  CHECK(t->handleMouse(modal, {50 + 100 + 5, 40 + 20 + 5}));   // list: kNoTrigger still counts as named
  CHECK_FALSE(t->handleMouse(modal, {50 + 250, 40 + 190}));    // the plain panel under the backdrop: nothing named
  CHECK(bd.rect().w == 300);                                   // the backdrop's width came back
  CHECK_FALSE(t->handleMouse(modal, {20, 20}));                // outside the container: the container itself
  CHECK(bd.rect().w == 300);
}

// ================================================================ persistence (fix F4)

namespace {

// Two tutorials: a repeatable one that runs, and a tryonce one that must still show afterwards.
Tbl twoTutorials() {
  auto a = tutorialOf({step("t1")});
  a->set("repeatable", 1.0);
  auto b = tutorialOf({step("t2")});
  b->set("tryonce", 1.0);
  return rootOf({{"a", a}, {"b", b}});
}

}  // namespace

TEST_CASE("Tutorials keep their own state across a save and a reload (F4 on)") {
  Fx f;
  f.world->settings().fixBugs = true;
  f.load(twoTutorials());
  f.tm().checkTutorial("a");  // one step that any click ends
  f.settle();
  REQUIRE(f.window());
  CHECK(f.window()->onMouseDown(700, 500));
  CHECK(f.tm().tutorial("a")->state() == 3);
  f.run_ms(800);
  f.wm.clearDestroyed();
  CHECK(f.world->progress().get("tutorial.a") == "3");
  CHECK_FALSE(f.world->progress().has("tutorial.shared"));
  f.world->progress().flush();
  f.world->reloadState(f.ctx);  // what a new session or another player does: every tutorial reads its state again
  CHECK(f.tm().tutorial("a")->state() == 3);
  CHECK(f.tm().tutorial("a")->shouldShow());  // repeatable
  CHECK(f.tm().tutorial("b")->state() == 0);
  CHECK(f.tm().tutorial("b")->shouldShow());  // the tryonce one has not run: it shows
}

TEST_CASE("With the fixes off every tutorial shares one slot: one finished tutorial hides the tryonce one after a reload") {
  Fx f;
  f.world->settings().fixBugs = false;
  f.load(twoTutorials());
  f.tm().checkTutorial("a");
  f.settle();
  REQUIRE(f.window());
  f.window()->onMouseDown(700, 500);
  f.run_ms(800);
  f.wm.clearDestroyed();
  CHECK(f.world->progress().get("tutorial.shared") == "3");
  CHECK_FALSE(f.world->progress().has("tutorial.a"));
  CHECK(f.tm().tutorial("b")->state() == 0);  // in this session `b` has not heard of it
  f.world->progress().flush();
  f.world->reloadState(f.ctx);
  CHECK(f.tm().tutorial("a")->state() == 3);
  CHECK(f.tm().tutorial("b")->state() == 3);
  CHECK_FALSE(f.tm().tutorial("b")->shouldShow());  // the original's bug: `b` never shows
  // and the byte goes down again when another tutorial starts: the slot holds the last state written by anyone
  f.tm().resetTutorials();
  f.tm().tutorial("a")->setState(3, false);
  f.tm().tutorial("b")->setState(3, false);
  f.world->progress().setInt("tutorial.shared", 0);
  f.world->progress().flush();
  f.world->reloadState(f.ctx);
  f.tm().checkTutorial("b");
  REQUIRE(f.tm().active());
  CHECK(f.world->progress().get("tutorial.shared") == "1");
  f.world->progress().flush();
  f.world->reloadState(f.ctx);
  CHECK(f.tm().tutorial("a")->state() == 1);
}

TEST_CASE("The tutorial states are the player's own: another player starts from nothing") {
  Fx f;
  f.world->settings().fixBugs = true;
  f.load(twoTutorials());
  f.tm().tutorial("b")->setState(3, true);
  f.world->progress().flush();
  int zed = 0;
  for (int i = 0; i < f.ctx.users->count(); ++i)
    if (f.ctx.users->name(i) == "Zed") zed = i;
  f.world->createUser(f.ctx, "Ann");
  CHECK(f.tm().tutorial("b")->state() == 0);
  f.world->setCurrentUser(f.ctx, zed);
  CHECK(f.tm().tutorial("b")->state() == 3);
}

// ================================================================ safety

TEST_CASE("A level reload while a tutorial is up: it closes at once and nothing refers to the old step") {
  Fx f;
  f.sprites_({"st"});
  auto s1 = step("t1");
  s1->set("trigger_objects", triggers({trig("st", kArrival)}));
  f.load(rootOf({{"a", tutorialOf({s1, step("t2")})}}));
  f.tm().checkTutorial("a");
  f.settle();
  REQUIRE(f.window());
  Tutorial* t = f.tm().active().get();
  f.tm().resetTutorials();  // LevelSetup
  CHECK_FALSE(f.tm().active());
  CHECK_FALSE(f.tm().tutorialInEffect());
  CHECK(t->finished());
  CHECK(f.level->sprites().exclusiveCount() == 0);
  CHECK_FALSE(f.level->sprites().softPaused());
  f.run_ms(100);  // the window sees a finished tutorial and closes
  f.wm.clearDestroyed();
  CHECK(f.window() == nullptr);
  // a stale arrival of the old step does nothing
  f.arrive("st");
  CHECK_FALSE(f.tm().active());
  // and the tutorial can be run again
  f.tm().checkTutorial("a");
  REQUIRE(f.tm().active());
  CHECK(f.stepNo() == 1);
  CHECK(f.window());
}

TEST_CASE("A tutorial that ends inside the click that advanced it, and one whose level window is gone, are safe") {
  Fx f;
  f.sprites_({"btn"});
  auto s1 = step("t1");
  s1->set("trigger_objects", triggers({trig("btn", kClick)}));
  auto again = tutorialOf({s1});
  again->set("repeatable", 1.0);
  f.load(rootOf({{"a", again}}));
  f.tm().checkTutorial("a");
  f.settle();
  f.click("btn");  // the last step: the spider drops the listener it is being called through
  CHECK(f.tm().active()->isComplete());
  f.run_ms(800);
  f.wm.clearDestroyed();
  CHECK(f.window() == nullptr);
  // the level's window goes while a tutorial runs: the manager finds no sprites and the tutorial can still end
  f.tm().checkTutorial("a");
  f.settle();
  REQUIRE(f.tm().active());
  f.wm.destroyWindow(*f.level);
  f.level = nullptr;
  CHECK(f.tm().levelSprites() == nullptr);
  CHECK(f.tm().handleMessage({ui::kButtonPress, "tutorial_skip", 0}));
  f.run_ms(800);
  CHECK_FALSE(f.tm().tutorialInEffect());
}

TEST_CASE("The world can die with a tutorial up: the window closes with the modal") {
  auto f = std::make_unique<Fx>();
  f->sprites_({"st"});
  auto s1 = step("t1");
  s1->set("trigger_objects", triggers({trig("st", kArrival)}));
  f->load(rootOf({{"a", tutorialOf({s1})}}));
  f->tm().checkTutorial("a");
  REQUIRE(f->window());
  f.reset();  // ~Fx destroys the windows with a running tutorial in the world; nothing may touch what is gone
  CHECK(true);
}

TEST_CASE("_SetState writes the game state store (the Lua side of TGameState)") {
  Fx f;
  bindGameStateNatives(f.lua.L(), f.ctx);
  CHECK(f.run("_SetState('MainMenuScreenTime', 1) _SetState('Name', 'x') _SetState('++Count', 0) _SetState('++Count', 0)") == 0);
  CHECK(f.world->gameStates().get("MainMenuScreenTime") == "1");
  CHECK(f.world->gameStates().get("Name") == "x");
  CHECK(f.world->gameStates().get("Count") == "2");
}

// The Display Case / Chocolate Disposal panel (tutorial_2_3 of tutorial_4, level 2): a 200 x 280 box in tutorial_text_style (Edwardian 15,
// margin {40,50,40,35}). VTextSprite lays the text out in 120 x 195 and clips the draw at the sprite's full height, 280.
TEST_CASE("The shipped Display Case panel text draws all of its lines under the sprite's own clip height") {
  const fs::path root = CHOCO_GREATCC_MAC_ASSETS;
  const fs::path xml = root / "strings/en/strings.xml", fontPath = root / "fonts/edwardian_medium.mvec";
  if (std::string(CHOCO_GREATCC_MAC_ASSETS).empty() || !fs::exists(xml) || !fs::exists(fontPath)) {
    MESSAGE("Great Chocolate Chase Mac assets not present, skipped");
    return;
  }
  auto slurp = [](const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  };
  const std::string doc = slurp(xml);
  const std::size_t k = doc.find(">tutorial_2_3</Data>");
  REQUIRE(k != std::string::npos);
  const std::string open = "<Data ss:Type=\"String\">";
  const std::size_t b = doc.find(open, k) + open.size(), e = doc.find("</Data>", b);
  std::string text = doc.substr(b, e - b);
  for (std::size_t p; (p = text.find("&lt;")) != std::string::npos;) text.replace(p, 4, "<");
  for (std::size_t p; (p = text.find("&gt;")) != std::string::npos;) text.replace(p, 4, ">");
  const std::string bytes = slurp(fontPath);
  std::vector<std::byte> raw(bytes.size());
  for (std::size_t i = 0; i < bytes.size(); ++i) raw[i] = static_cast<std::byte>(bytes[i]);
  std::string err;
  auto font = gfx::text::Font::fromBytes(std::move(raw), "edwardian_medium", &err);
  REQUIRE_MESSAGE(font, err);
  gfx::text::TextStyle st;
  st.font = font;
  st.size = 15;
  st.flags = 5;
  const auto lay = gfx::text::TextLayout::build(text, st, 200 - 80, 280 - 85, 280);
  const auto bb = lay.bounds();
  MESSAGE("Display Case panel: " << lay.lineCount() << " lines, text top " << bb.y << " bottom " << bb.y + bb.h << " (layout 195, clip 280)");
  CHECK(lay.lineCount() > 6);
  CHECK(lay.drawnLineCount() == lay.lineCount());
}
