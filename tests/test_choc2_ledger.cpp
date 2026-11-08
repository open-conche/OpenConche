// Chocolatier 2: DynamicWindow, the ledger and its natives, the Text start line and script::runFileInto
// (docs/design/choc2-port-and-ledger.md 2.3 and section 4).
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "games/Registry.h"
#include "ScriptHarness.h"
#include "games/common/MoneyCounter.h"
#include "games/common/DynamicWindow.h"
#include "games/choc2/LedgerWindow.h"
#include "games/choc2/Session.h"
#include "script/RunFile.h"
#include "ui/widgets/TextWidgets.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
using game::DynamicWindow;
using game::choc2::Ledger;

namespace {

struct LedgerEnv : test::ScriptEnv {
  LedgerEnv() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/choc2_ledger"), host(ctx) {
    ctx.module = games::findModule("choc2");
    ctx.module->attach(ctx);
    booted = host.boot();
    ctx.strings.loadRows({{"formatScore", "%1%/week"}});
  }
  // A sub-window of the top modal.
  ui::Window* win(const char* name) { return wm.topModal()->find(name); }
  template <class T = ui::Window>
  T* get(const char* name) {
    return dynamic_cast<T*>(win(name));
  }
  std::string label(const char* name) {
    ui::Window* w = win(name);
    return w ? w->label() : "<missing>";
  }
  // Steps the 50 ms window animations through `ticks` ticks from `*t`.
  void tick(double* t, int ticks) {
    for (int i = 0; i < ticks; ++i) {
      *t += 50;
      clock.nowMs = *t;
      wm.setNowMs(*t);
      wm.tickWindowAnimations(*t);
      wm.tickTransitions(*t);
    }
  }
  // A ledger as the port view builds it, with gSim.
  void buildLedger(const char* gSim = "{money = 1000, rank = 1, score = 250}", const char* extra = "") {
    REQUIRE(run(std::string("gSim = ") + gSim + "; " + extra + " PushModal('port'); dofile('ui/ledger_tree.lua')") == 0);
    REQUIRE(lastError.empty());
  }
  bool shown(const char* name) {
    ui::Window* w = win(name);
    return w && w->shown();
  }
  script::ScriptHost host;
  bool booted = false;
};

bool on(ui::Window* w) {
  auto* b = dynamic_cast<ui::ButtonWindow*>(w);
  return b && b->on();
}

}  // namespace

TEST_CASE("DynamicWindow builds at init, not with init=false, and a rebuild replaces the children") {
  LedgerEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('t');"
                "DynamicWindow{name = 'a', x = 0, y = 0, w = 100, h = 50, contents = 'ui/dyn_counter.lua'}();"
                "DynamicWindow{name = 'b', x = 0, y = 60, w = 100, h = 50, contents = 'ui/dyn_counter.lua', init = false}()") == 0);
  CHECK_FALSE(e.log.contains("[unimplemented-type] DynamicWindow"));
  auto* a = e.get<DynamicWindow>("a");
  auto* b = e.get<DynamicWindow>("b");
  REQUIRE((a && b));
  CHECK(a->typeName() == "DynamicWindow");
  CHECK(e.getNumber("dynBuilds") == 1);  // only a
  CHECK(a->children().size() == 1);
  CHECK(b->children().empty());
  CHECK(a->rect() == gfx::RectI{0, 0, 100, 50});  // x y w h given: the children (100x20) are inside, so FitToChildren (growth only) leaves it
  const ui::WindowId old = a->children().front()->id();
  CHECK(e.label("dyn_text") == "build 1");
  REQUIRE(a->rebuild());
  CHECK(e.getNumber("dynBuilds") == 2);
  CHECK(a->children().size() == 1);
  CHECK(e.wm.window(old) == nullptr);  // the old child is detached at once, destroyed at the next sweep
  CHECK(e.label("dyn_text") == "build 2");
  CHECK(a->builds() == 2);
  // The first build of b is its own rebuild.
  REQUIRE(b->rebuild());
  CHECK(b->children().size() == 1);
  CHECK(e.getNumber("dynBuilds") == 3);
  CHECK(e.wm.windowAnimationCount() == 0);
}

TEST_CASE("DynamicWindow: a rebuild from a child button's own command is safe") {
  LedgerEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('t'); DynamicWindow{name = 'self', x = 0, y = 0, w = 100, h = 50, contents = 'ui/dyn_button.lua'}()") == 0);
  auto* d = e.get<DynamicWindow>("self");
  auto* button = e.get<ui::ButtonWindow>("rebuilder");
  REQUIRE((d && button));
  const ui::WindowId old = button->id();
  button->press();  // its command calls UpdateDynamicContent('self') and so destroys the button that is running
  CHECK(e.getNumber("buttonBuilds") == 2);
  CHECK(e.wm.window(old) == nullptr);
  e.wm.clearDestroyed();
  auto* fresh = e.get<ui::ButtonWindow>("rebuilder");
  REQUIRE(fresh);
  CHECK(fresh->id() != old);
  CHECK_FALSE(e.log.contains("[lua-error]"));
}

TEST_CASE("DynamicWindow: a nested rebuild is ignored and logged") {
  LedgerEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('t'); DynamicWindow{name = 'nested', x = 0, y = 0, w = 100, h = 50, contents = 'ui/dyn_nested.lua'}()") == 0);
  CHECK(e.getNumber("nestedBuilds") == 1);  // the request made during the build did nothing
  CHECK(e.log.contains("[choc2] DynamicWindow 'nested': rebuild ignored"));
  auto* d = e.get<DynamicWindow>("nested");
  REQUIRE(d);
  CHECK_FALSE(d->building());
  CHECK(d->children().size() == 1);
}

TEST_CASE("UpdateDynamicContent rebuilds every DynamicWindow with that name, in every modal") {
  LedgerEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('one');"
                "DynamicWindow{name = 'same', x = 0, y = 0, w = 10, h = 10, contents = 'ui/dyn_counter.lua'}();"
                "Window{x = 0, y = 20, w = 100, h = 100,"
                "  DynamicWindow{name = 'same', x = 0, y = 0, w = 10, h = 10, contents = 'ui/dyn_counter.lua'}}();"
                "DynamicWindow{name = 'other', x = 0, y = 50, w = 10, h = 10, contents = 'ui/dyn_counter.lua'}();"
                "PushModal('two');"
                "DynamicWindow{name = 'same', x = 0, y = 0, w = 10, h = 10, contents = 'ui/dyn_counter.lua'}()") == 0);
  const double before = e.getNumber("dynBuilds");
  CHECK(before == 4);
  REQUIRE(e.run("UpdateDynamicContent('same'); UpdateDynamicContent('nothing')") == 0);
  CHECK(e.getNumber("dynBuilds") == before + 3);  // two in the first modal (one inside a Window), one in the second
  CHECK(DynamicWindow::updateNamed(e.wm, "same") == 3);
}

TEST_CASE("runFileInto: the window is the one being built, the stacks are restored, an error is logged") {
  LedgerEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('t'); Window{name = 'target', x = 0, y = 0, w = 100, h = 50}()") == 0);
  ui::Window* target = e.win("target");
  REQUIRE(target);
  const std::size_t depth = e.wm.creationDepth();
  const int top = lua_gettop(e.lua.L());
  script::runFileInto(e.ctx, e.lua.L(), *target, "ui/dyn_counter.lua");
  CHECK(target->children().size() == 1);
  CHECK(e.wm.creationDepth() == depth);
  CHECK(lua_gettop(e.lua.L()) == top);
  // A script that raises: the error is logged, the creation stack and the Lua stack still come back.
  script::runFileInto(e.ctx, e.lua.L(), *target, "ui/dyn_error.lua");
  CHECK(e.getNumber("dynError") == 1);
  CHECK(e.log.contains("boom"));
  CHECK(e.wm.creationDepth() == depth);
  CHECK(lua_gettop(e.lua.L()) == top);
  // A file that does not exist is an error too, not a crash.
  script::runFileInto(e.ctx, e.lua.L(), *target, "ui/missing.lua");
  CHECK(e.wm.creationDepth() == depth);
}

TEST_CASE("MoneyCounter: the 1/20 step, never overshoots, snap") {
  game::MoneyCounter c;
  c.snap(1000);
  CHECK(c.display == 1000);
  CHECK_FALSE(c.step());
  CHECK_FALSE(c.setTarget(1000));
  CHECK(c.setTarget(3000));
  CHECK(c.delta == 100);
  for (int i = 1; i <= 19; ++i) {
    CHECK(c.step());
    CHECK(c.display == 1000 + 100 * i);
  }
  CHECK(c.step());
  CHECK(c.display == 3000);
  CHECK_FALSE(c.moving());
  CHECK_FALSE(c.step());
  // Down, with a gap that does not divide: the last step lands on the target.
  CHECK(c.setTarget(2990));
  int steps = 0;
  while (c.step()) ++steps;
  CHECK(steps == 20);
  CHECK(c.display == 2990);
  // A step size below the float resolution still ends.
  c.snap(0);
  CHECK(c.setTarget(0.5));
  while (c.step()) {}
  CHECK(c.display == 0.5);
  c.snap(7);
  CHECK(c.display == 7);
  CHECK(c.target == 7);
}

TEST_CASE("Ledger: built with the factories tab shown, its body built, the radio set, money from data") {
  LedgerEnv e;
  REQUIRE(e.booted);
  e.buildLedger();
  auto* l = e.get<Ledger>("ledger");
  REQUIRE(l);
  CHECK_FALSE(e.log.contains("[unimplemented-type] Ledger"));
  CHECK(l->shownTab() == "factories");  // an empty remembered tab opens the factories
  CHECK(e.shown("factories"));
  CHECK_FALSE(e.shown("messages"));
  CHECK_FALSE(e.shown("inventory"));
  CHECK(e.getNumber("builds_messages") == 0);
  CHECK(e.getNumber("builds_factories") == 1);
  CHECK(e.getNumber("builds_badge") == 1);
  CHECK(on(e.win("btn_factories")));
  CHECK(game::choc2::sessionOf(e.ctx)->ledgerTab == "factories");  // recorded by the ledger that showed it
  CHECK(l->counter().display == 1000);
  CHECK(e.label("money").find("1,000") != std::string::npos);
  CHECK(l->detail().find("tab=\"factories\"") != std::string::npos);
  CHECK(e.wm.windowAnimationCount() == 1);
  // No quest text: the bar is hidden.
  CHECK(e.win("quest")->rect().y == 53);
  // The natives find this ledger through the modal chain, also from under a dialog.
  REQUIRE(e.run("PushModal('dialog')") == 0);
  REQUIRE(e.run("SetLedgerContents('inventory')") == 0);
  CHECK(l->shownTab() == "inventory");
  CHECK_FALSE(e.log.contains("[native-warning]"));
}

TEST_CASE("SetLedgerContents shows and rebuilds the tab and sets the radio; the tab outlives the ledger") {
  LedgerEnv e;
  REQUIRE(e.booted);
  e.buildLedger();
  REQUIRE(e.run("SetLedgerContents('messages')") == 0);
  CHECK(e.shown("messages"));
  CHECK_FALSE(e.shown("factories"));
  CHECK(on(e.win("btn_messages")));
  CHECK_FALSE(on(e.win("btn_factories")));
  CHECK(e.getNumber("builds_messages") == 1);
  CHECK(game::choc2::sessionOf(e.ctx)->ledgerTab == "messages");
  // Showing it again rebuilds it again: it is always current.
  REQUIRE(e.run("SetLedgerContents('messages')") == 0);
  CHECK(e.getNumber("builds_messages") == 2);
  // A tab button's own command does the same.
  REQUIRE(e.run("SetLedgerContents('inventory')") == 0);
  CHECK(on(e.win("btn_inventory")));
  CHECK_FALSE(on(e.win("btn_messages")));
  // A bad name is logged and changes nothing.
  REQUIRE(e.run("SetLedgerContents('bogus')") == 0);
  CHECK(e.log.contains("'bogus' is not a ledger tab"));
  CHECK(game::choc2::sessionOf(e.ctx)->ledgerTab == "inventory");
  // A map swap builds a new ledger: it opens on the remembered tab.
  REQUIRE(e.wm.popModal("port"));
  e.wm.clearDestroyed();
  e.buildLedger();
  auto* l2 = e.get<Ledger>("ledger");
  REQUIRE(l2);
  CHECK(l2->shownTab() == "inventory");
  CHECK(on(e.win("btn_inventory")));
  CHECK(e.getNumber("builds_inventory") == 2);  // once when chosen, once when the new ledger opened on it
}

TEST_CASE("The ledger natives with no ledger do nothing, and the tab is not recorded") {
  LedgerEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('menu'); SetLedgerContents('factories'); UpdateLedgerContents('factories'); SetLedgerMode('map');"
                "ResetLedger(5); UpdateBadge({money = 1}); SetQuestHint('x')") == 0);
  CHECK(game::choc2::sessionOf(e.ctx)->ledgerTab.empty());
  CHECK_FALSE(e.log.contains("[unimplemented]"));
  CHECK_FALSE(e.log.contains("[native-warning]"));
  CHECK_FALSE(e.log.contains("[native-error]"));
  REQUIRE(e.run("SetLedgerContents('inventory')") == 0);
  CHECK(game::choc2::sessionOf(e.ctx)->ledgerTab.empty());
  e.buildLedger();
  CHECK(e.get<Ledger>("ledger")->shownTab() == "factories");
}

TEST_CASE("UpdateLedgerContents rebuilds only the shown tab") {
  LedgerEnv e;
  REQUIRE(e.booted);
  e.buildLedger();
  REQUIRE(e.run("UpdateLedgerContents('messages')") == 0);  // hidden: not rebuilt
  CHECK(e.getNumber("builds_messages") == 0);
  REQUIRE(e.run("UpdateLedgerContents('factories')") == 0);  // shown
  CHECK(e.getNumber("builds_factories") == 2);
  REQUIRE(e.run("SetLedgerContents('messages'); UpdateLedgerContents('messages'); UpdateLedgerContents('factories')") == 0);
  CHECK(e.getNumber("builds_messages") == 2);
  CHECK(e.getNumber("builds_factories") == 2);
  REQUIRE(e.run("UpdateLedgerContents('nonsense')") == 0);
  CHECK(e.log.contains("'nonsense' is not a ledger tab"));
}

TEST_CASE("SetLedgerMode toggles to_map/to_port and their labels, which are siblings of the ledger") {
  LedgerEnv e;
  REQUIRE(e.booted);
  e.buildLedger();
  REQUIRE(e.run("SetLedgerMode('port')") == 0);
  CHECK(e.shown("to_map"));
  CHECK(e.shown("to_map_label"));
  CHECK_FALSE(e.shown("to_port"));
  CHECK_FALSE(e.shown("to_port_label"));
  REQUIRE(e.run("SetLedgerMode('map')") == 0);
  CHECK_FALSE(e.shown("to_map"));
  CHECK_FALSE(e.shown("to_map_label"));
  CHECK(e.shown("to_port"));
  CHECK(e.shown("to_port_label"));
  REQUIRE(e.run("SetLedgerMode('sideways')") == 0);
  CHECK(e.log.contains("'sideways' is not a mode"));
  CHECK(e.shown("to_port"));
}

TEST_CASE("MoneyCounter::setTargetChoc2: a fixed step of max(1, gap / 7), negative is 0") {
  game::MoneyCounter c;
  c.snap(0);
  CHECK(c.setTargetChoc2(5000));
  CHECK(c.delta == 714);
  int steps = 0;
  while (c.step()) ++steps;
  CHECK(steps == 8);  // seven steps of 714, then the snap
  CHECK(c.display == 5000);
  CHECK(c.setTargetChoc2(4995));
  CHECK(c.delta == -1);  // gap 5 / 7 = 0, at least 1
  CHECK(c.setTargetChoc2(-40));
  CHECK(c.target == 0);
  CHECK_FALSE(c.setTargetChoc2(0));
}

TEST_CASE("ResetLedger forgets the tab, so the next ledger opens on the factories; the quest bar starts hidden") {
  LedgerEnv e;
  REQUIRE(e.booted);
  e.buildLedger("{money = 5}", "gQuestLabel = '#Bake a cake';");
  REQUIRE(e.run("SetLedgerContents('inventory')") == 0);
  CHECK(game::choc2::sessionOf(e.ctx)->ledgerTab == "inventory");
  REQUIRE(e.run("ResetLedger(7)") == 0);
  CHECK(game::choc2::sessionOf(e.ctx)->ledgerTab.empty());
  CHECK(game::choc2::sessionOf(e.ctx)->questY == game::choc2::Session::kQuestUnset);
  CHECK(e.label("quest_text").empty());
  REQUIRE(e.wm.popModal("port"));
  e.wm.clearDestroyed();
  e.buildLedger("{money = 7}", "gQuestLabel = '#Bake a cake';");
  CHECK(e.get<Ledger>("ledger")->shownTab() == "factories");
  CHECK(e.win("quest")->rect().y == 53);  // starts hidden; the ticks slide it in (SetQuestHint test)
}

TEST_CASE("ResetLedger snaps the counter and rewrites the money") {
  LedgerEnv e;
  REQUIRE(e.booted);
  e.buildLedger();
  auto* l = e.get<Ledger>("ledger");
  REQUIRE(l);
  REQUIRE(e.run("ResetLedger(25000)") == 0);
  CHECK(l->counter().display == 25000);
  CHECK(l->counter().target == 25000);
  CHECK(e.label("money").find("25,000") != std::string::npos);
  double t = 0;
  e.tick(&t, 3);
  CHECK(l->counter().display == 25000);  // no count-up
}

TEST_CASE("UpdateBadge does not rebuild the badge; it counts the money up in 100 ms steps and writes the score line") {
  LedgerEnv e;
  REQUIRE(e.booted);
  e.buildLedger();
  auto* l = e.get<Ledger>("ledger");
  REQUIRE(l);
  CHECK(e.label("score") == "$250/week");  // written at build from the table
  REQUIRE(e.run("gSim.money = 3000; gSim.rank = 2; gSim.score = 1234.7; UpdateBadge(gSim)") == 0);
  CHECK(e.getNumber("builds_badge") == 1);
  REQUIRE(e.run("UpdateDynamicContent('badge')") == 0);
  CHECK(e.getNumber("builds_badge") == 2);
  CHECK(l->counter().target == 3000);
  CHECK(l->counter().display == 1000);
  CHECK(e.label("money").find("1,000") != std::string::npos);  // the new Text shows what is displayed, not the target
  CHECK(e.label("score") == "$1,234/week");                     // whole dollars (bsgDollars truncates)
  double t = 0;
  e.tick(&t, 2);  // one 100 ms tick: the step is (3000 - 1000) / 7 = 285
  CHECK(l->counter().display == 1285);
  e.tick(&t, 14);
  CHECK(l->counter().display == 3000);  // 7 steps and the snap
  CHECK(e.label("money").find("3,000") != std::string::npos);
  // Negative money shows $0; a billion shrinks the digits.
  REQUIRE(e.run("gSim.money = -5; UpdateBadge(gSim)") == 0);
  CHECK(l->counter().target == 0);
  REQUIRE(e.run("ResetLedger(2000000000)") == 0);
  CHECK(e.label("money").find("<font size='25'>") != std::string::npos);
  REQUIRE(e.run("ResetLedger(5)") == 0);
  CHECK(e.label("money").find("<font") == std::string::npos);
  // At rank 0 the score line is empty.
  REQUIRE(e.run("gSim.rank = 0; UpdateBadge(gSim)") == 0);
  CHECK(e.label("score").empty());
  // A non-table is ignored.
  REQUIRE(e.run("UpdateBadge(5)") == 0);
}

TEST_CASE("SetQuestHint writes quest_text and the bar slides 26 px per 100 ms tick: 53, 27, 1, 0") {
  LedgerEnv e;
  REQUIRE(e.booted);
  e.buildLedger();
  ui::Window* quest = e.win("quest");
  REQUIRE(quest);
  CHECK(quest->rect().y == 53);
  REQUIRE(e.run("SetQuestHint('Find a recipe')") == 0);
  CHECK(e.label("quest_text") == "Find a recipe");
  CHECK(quest->rect().y == 53);  // nothing moves before a tick
  double t = 0;
  e.tick(&t, 2);
  CHECK(quest->rect().y == 27);
  e.tick(&t, 2);
  CHECK(quest->rect().y == 1);
  e.tick(&t, 2);
  CHECK(quest->rect().y == 0);
  CHECK(quest->rect().x == 235);
  REQUIRE(e.run("SetQuestHint('')") == 0);
  CHECK(e.label("quest_text").empty());
  e.tick(&t, 2);
  CHECK(quest->rect().y == 26);
  e.tick(&t, 4);
  CHECK(quest->rect().y == 53);
}

TEST_CASE("A ledger built while the quest bar is shown starts shown") {
  LedgerEnv e;
  REQUIRE(e.booted);
  e.buildLedger("{money = 5}", "gQuestLabel = '#Bake a cake';");
  double t = 0;
  e.tick(&t, 6);
  CHECK(e.win("quest")->rect().y == 0);
  REQUIRE(e.wm.popModal("port"));
  e.wm.clearDestroyed();
  e.buildLedger("{money = 5}", "gQuestLabel = '#Bake a cake';");
  CHECK(e.win("quest")->rect().y == 0);
}

TEST_CASE("bsgGetMaxScroll and bsgSetStartLine scroll a Text") {
  LedgerEnv e;
  REQUIRE(e.booted);
  e.buildLedger();
  REQUIRE(e.run("SetLedgerContents('messages')") == 0);
  auto* t = e.get<ui::TextWindow>("messageText");
  REQUIRE(t);
  CHECK(t->layout().lineCount() == 10);
  CHECK(t->maxStartLine() == 6);  // 10 lines in a box of four
  REQUIRE(e.run("max = bsgGetMaxScroll('messageText'); missing = bsgGetMaxScroll('nothing'); notText = bsgGetMaxScroll('ledger')") == 0);
  CHECK(e.getNumber("max") == 6);
  CHECK(e.getNumber("missing") == -1);
  CHECK(e.getNumber("notText") == -1);
  CHECK(t->startLine() == 0);
  REQUIRE(e.run("bsgSetStartLine('messageText', 3)") == 0);
  CHECK(t->startLine() == 3);  // stored unchanged, like the binary (no "- 1")
  REQUIRE(e.run("bsgSetStartLine('messageText', 99)") == 0);
  CHECK(t->startLine() == 99);  // not clamped; the draw shows nothing past the end
  REQUIRE(e.run("bsgSetStartLine('messageText', 0)") == 0);
  CHECK(t->startLine() == 0);
  REQUIRE(e.run("bsgSetStartLine('nothing', 3)") == 0);
  // A short text has nothing to scroll.
  t->setLabel("one\ntwo");
  CHECK(t->maxStartLine() == 0);
}

TEST_CASE("Messages scroll: top shows the first message, bottom starts the last page one line late") {
  LedgerEnv e;
  REQUIRE(e.booted);
  e.buildLedger();
  REQUIRE(e.run("SetLedgerContents('messages')") == 0);
  auto* t = e.get<ui::TextWindow>("messageText");
  REQUIRE(t);
  // The label as TText::Init leaves it for "#\nm1\n...": a leading empty line, then the messages (10 lines in a box of four).
  t->setLabel("\nm1\nm2\nm3\nm4\nm5\nm6\nm7\nm8\nm9");
  REQUIRE(t->layout().lineCount() == 10);
  REQUIRE(t->maxStartLine() == 6);
  // Top: the Lua's messageTop = 1 starts at line 1, the first message (line 0 is the empty line).
  REQUIRE(e.run("bsgSetStartLine('messageText', 1)") == 0);
  CHECK(t->drawnStartLine() == 1);
  auto glyphs = t->layout().glyphPositions();
  REQUIRE_FALSE(glyphs.empty());
  CHECK(glyphs.front().line == 1);  // 'm' of m1 is the first glyph of the first drawn line
  // Bottom: messageTop = max + 1 starts at line max + 1 = 7: the true last page (lines 6..9, k = 4) loses its first line: k - 1 = 3 lines.
  REQUIRE(e.run("bsgSetStartLine('messageText', bsgGetMaxScroll('messageText') + 1)") == 0);
  CHECK(t->drawnStartLine() == 7);
  CHECK(t->layout().lineCount() - t->drawnStartLine() == 3);  // 3 lines drawn in a box of four: a blank row under them
}

TEST_CASE("Ledger: a hidden tab body is emptied, so the shown body's buttons are the ones found by name") {
  LedgerEnv e;
  REQUIRE(e.booted);
  e.buildLedger();
  REQUIRE(e.run("SetLedgerContents('messages')") == 0);
  CHECK(e.win("messageText"));
  REQUIRE(e.run("SetLedgerContents('inventory')") == 0);
  CHECK(e.get<DynamicWindow>("messages")->children().empty());  // hidden: emptied
  CHECK(e.win("messageText") == nullptr);
  CHECK(e.win("inventory_text"));
  REQUIRE(e.run("SetLedgerContents('messages')") == 0);
  CHECK(e.get<DynamicWindow>("inventory")->children().empty());
  CHECK(e.win("inventory_text") == nullptr);
  CHECK(e.win("messageText"));
  CHECK(e.getNumber("builds_messages") == 2);
}

TEST_CASE("SetQuestHint twice in one frame: the last one wins") {
  LedgerEnv e;
  REQUIRE(e.booted);
  e.buildLedger("{money = 5}", "gQuestLabel = '#Bake a cake';");
  ui::Window* quest = e.win("quest");
  double t = 0;
  e.tick(&t, 6);  // the bar is shown (y 0)
  REQUIRE(e.run("SetQuestHint(''); SetQuestHint('again')") == 0);
  e.tick(&t, 6);
  CHECK(quest->rect().y == 0);
  CHECK(e.label("quest_text") == "again");
}

TEST_CASE("Ledger natives reach every ledger in the modal chain, so the port view's is not stale under a dialog") {
  LedgerEnv e;
  REQUIRE(e.booted);
  e.buildLedger();
  REQUIRE(e.run("PushModal('market'); dofile('ui/ledger_tree.lua')") == 0);
  const auto all = Ledger::all(e.wm);
  REQUIRE(all.size() == 2);
  REQUIRE(e.run("gSim.money = 3000; UpdateBadge(gSim)") == 0);
  CHECK(all[0]->counter().target == 3000);
  CHECK(all[0]->counter().display == 1000);  // the top one counts up
  CHECK(all[1]->counter().target == 3000);
  CHECK(all[1]->counter().display == 1000);  // the same shared count, not ahead of it
  CHECK(&all[0]->counter() == &all[1]->counter());
  REQUIRE(e.run("SetLedgerContents('inventory')") == 0);
  CHECK(all[0]->shownTab() == "inventory");
  CHECK(all[1]->shownTab() == "inventory");
}

namespace {
// The money digits of a ledger's money Text, as a number.
long long moneyOf(Ledger* l) {
  ui::Window* m = l->find("money");
  long long v = 0;
  if (m)
    for (char c : m->label())
      if (c >= '0' && c <= '9') v = v * 10 + (c - '0');
  return v;
}

// A port ledger with a dialog ledger over it; returns the two (top first).
std::vector<Ledger*> stackLedgers(LedgerEnv& e) {
  e.buildLedger();
  REQUIRE(e.run("PushModal('market'); dofile('ui/ledger_tree.lua')") == 0);
  auto all = Ledger::all(e.wm);
  REQUIRE(all.size() == 2);
  return all;
}
}  // namespace

TEST_CASE("Ledger: a dialog's ledger over the port's shows the same money at every moment of a count-up") {
  LedgerEnv e;
  REQUIRE(e.booted);
  const auto all = stackLedgers(e);
  REQUIRE(e.run("gSim.money = 3000; UpdateBadge(gSim)") == 0);
  double t = 0;
  long long last = 1000;
  for (int i = 0; i < 20; ++i) {
    e.tick(&t, 1);
    CHECK(moneyOf(all[0]) == moneyOf(all[1]));
    CHECK(all[0]->counter().display == all[1]->counter().display);
    CHECK(moneyOf(all[1]) >= last);
    last = moneyOf(all[1]);
  }
  CHECK(last == 3000);
}

TEST_CASE("Ledger: the count-up takes the same ticks with one ledger or two") {
  auto ticksToTarget = [](bool two) {
    LedgerEnv e;
    REQUIRE(e.booted);
    e.buildLedger();
    if (two) REQUIRE(e.run("PushModal('market'); dofile('ui/ledger_tree.lua')") == 0);
    REQUIRE(e.run("gSim.money = 3000; UpdateBadge(gSim)") == 0);
    double t = 0;
    int ticks = 0;
    auto* l = Ledger::current(e.wm);
    while (l->counter().display != 3000 && ticks < 40) {
      e.tick(&t, 2);
      ++ticks;
    }
    return ticks;
  };
  CHECK(ticksToTarget(false) == 8);  // 7 steps of 285 and the snap
  CHECK(ticksToTarget(true) == 8);
}

TEST_CASE("Ledger: two tasks in one manager tick step the shared money once") {
  LedgerEnv e;
  REQUIRE(e.booted);
  const auto all = stackLedgers(e);
  REQUIRE(e.run("gSim.money = 3000; UpdateBadge(gSim)") == 0);
  e.wm.setNowMs(500);
  all[0]->onAnimate();
  all[1]->onAnimate();
  CHECK(all[0]->counter().display == 1285);
  e.wm.setNowMs(600);
  all[1]->onAnimate();
  CHECK(all[0]->counter().display == 1570);
}

TEST_CASE("Ledger: tab and mode set through the top ledger are shown by the covered one when uncovered") {
  LedgerEnv e;
  REQUIRE(e.booted);
  const auto all = stackLedgers(e);
  Ledger* top = all[0];
  Ledger* port = all[1];
  REQUIRE(e.run("SetLedgerContents('inventory'); SetLedgerMode('map')") == 0);
  CHECK(top->shownTab() == "inventory");
  CHECK(port->shownTab() == "inventory");
  CHECK(game::choc2::sessionOf(e.ctx)->mode == "map");
  // The covered ledger's own sibling buttons, not the dialog's.
  ui::Window* portModal = port;
  while (portModal && !dynamic_cast<ui::ModalWindow*>(portModal)) portModal = portModal->parent();
  REQUIRE(portModal);
  REQUIRE(portModal->find("to_map"));
  CHECK_FALSE(portModal->find("to_map")->shown());
  CHECK(portModal->find("to_port")->shown());
  REQUIRE(e.wm.popModal("market"));
  e.wm.clearDestroyed();
  REQUIRE(Ledger::current(e.wm) == port);
  CHECK(port->shownTab() == "inventory");
  CHECK(e.shown("to_port"));
  CHECK_FALSE(e.shown("to_map"));
}

TEST_CASE("Ledger: destroying the top ledger mid count-up leaves the one below counting to the target") {
  LedgerEnv e;
  REQUIRE(e.booted);
  const auto all = stackLedgers(e);
  Ledger* port = all[1];
  REQUIRE(e.run("gSim.money = 3000; UpdateBadge(gSim)") == 0);
  double t = 0;
  e.tick(&t, 4);
  CHECK(port->counter().display == 1570);
  REQUIRE(e.wm.popModal("market"));
  e.wm.clearDestroyed();
  REQUIRE(Ledger::all(e.wm).size() == 1);
  CHECK(moneyOf(port) == 1570);  // no snap on return
  e.tick(&t, 20);
  CHECK(port->counter().display == 3000);
  CHECK(moneyOf(port) == 3000);
}

TEST_CASE("Ledger: SetQuestHint and the bar slide are shared by every ledger") {
  LedgerEnv e;
  REQUIRE(e.booted);
  const auto all = stackLedgers(e);
  REQUIRE(e.run("SetQuestHint('Find a recipe')") == 0);
  CHECK(game::choc2::sessionOf(e.ctx)->questHint == "Find a recipe");
  CHECK(all[0]->find("quest_text")->label() == "Find a recipe");
  CHECK(all[1]->find("quest_text")->label() == "Find a recipe");
  double t = 0;
  for (int i = 0; i < 6; ++i) {
    e.tick(&t, 1);
    CHECK(all[0]->find("quest")->rect().y == all[1]->find("quest")->rect().y);
  }
  CHECK(all[1]->find("quest")->rect().y == 0);
  // A ledger built over them starts with the shared hint.
  REQUIRE(e.run("PushModal('shop'); dofile('ui/ledger_tree.lua')") == 0);
  CHECK(Ledger::current(e.wm)->find("quest_text")->label() == "Find a recipe");
  REQUIRE(e.run("SetQuestHint('')") == 0);
  e.tick(&t, 8);
  for (Ledger* l : Ledger::all(e.wm)) CHECK(l->find("quest")->rect().y == 53);
}
