// Chocolatier 1 (docs/re/choc1-fit.md): the window quirks, the Rollover
// (docs/re/choc1-rollover-and-quirks.md) and the ChoosePlayer window with
// UpdatePlayerButtons.
#include <doctest/doctest.h>

#include <string>

#include "ScriptHarness.h"
#include "games/Registry.h"
#include "games/choc1/Choc1World.h"
#include "games/choc1/ChoosePlayer.h"
#include "games/choc1/Rollover.h"
#include "ui/Input.h"
#include "ui/widgets/TextWidgets.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
namespace f1 = choco::game::choc1;

TEST_CASE("Choc1 quirks: fall-through skew, no fader, no default cancel, no "
          "sprite hit test, TRollover keys") {
  const script::WindowQuirks &q = games::findModule("choc1")->profile().quirks;
  CHECK(q.fallThroughPoint);
  CHECK_FALSE(q.popupFader);
  CHECK_FALSE(q.cancelDefault);
  CHECK(q.spriteHitAlpha == 0);
  CHECK(q.rollover);
  // Chocolatier 3 and 2 keep theirs.
  CHECK_FALSE(games::findModule("choc3")->profile().quirks.fallThroughPoint);
  CHECK(games::findModule("choc2")->profile().quirks.popupFader);
}

TEST_CASE("Choc1 Rollover geometry: pointer mode, absolute mode needs both >= "
          "1, offsets are added, the hot rect is centred") {
  auto g = f1::rolloverGeometry(300, 200, 20, 0, -1, -1, 10, 10);
  CHECK_FALSE(g.absolute);
  CHECK(g.anchorX == 320);
  CHECK(g.anchorY == 200);
  CHECK(g.hot == gfx::RectI{295, 195, 10, 10});

  g = f1::rolloverGeometry(300, 200, 20, 0, 500, -1, 40,
                           40); // only one axis: pointer mode for both
  CHECK_FALSE(g.absolute);
  CHECK(g.anchorX == 320);
  g = f1::rolloverGeometry(300, 200, 20, 0, 0, 90, 40, 40); // a 0 is below 1
  CHECK_FALSE(g.absolute);

  g = f1::rolloverGeometry(300, 200, -35, -10, 500, 400, 60, 60);
  CHECK(g.absolute);
  CHECK(g.anchorX == 465); // absolute + offset
  CHECK(g.anchorY == 390);
  // The literal reading of the exe: the centre is the pointer mirrored about
  // the anchor (2 * 500 - 300, 2 * 400 - 200).
  CHECK(g.hot == gfx::RectI{700 - 30, 600 - 30, 60, 60});
}

TEST_CASE("Choc1 popup placement: 800x600 with no margin, pushed down 32 when "
          "pushed left") {
  using ui::PopupWindow;
  CHECK(PopupWindow::reposition(700, 100, 100, 50, 800, 600, 0) ==
        std::pair<int, int>{700, 100}); // touches the edge: stays
  CHECK(PopupWindow::reposition(750, 100, 100, 50, 800, 600, 0) ==
        std::pair<int, int>{700, 132});
  CHECK(PopupWindow::reposition(100, 580, 100, 50, 800, 600, 0) ==
        std::pair<int, int>{100, 550});
  CHECK(PopupWindow::reposition(750, 580, 100, 50, 800, 600, 0) ==
        std::pair<int, int>{700, 550}); // the +32 is seen by the bottom test
  CHECK(PopupWindow::reposition(700, 100, 100, 50, 800, 600) ==
        std::pair<int, int>{696, 132}); // the built-in's 4 px margin
}

namespace {

ui::InputEvent mouse(ui::InputEvent::Type t, float x, float y) {
  ui::InputEvent e;
  e.type = t;
  e.x = x;
  e.y = y;
  return e;
}

// A booted choc1 host over tests/assets/choc1_story (its ui/rollover.lua is a
// stand-in that records `_rollover`).
struct StoryEnv : test::ScriptEnv {
  StoryEnv()
      : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/choc1_story"), host(ctx) {
    ctx.module = games::findModule("choc1");
    ctx.module->attach(ctx);
    booted = host.boot();
    wm.pushModal("view"); // the screen the rollovers live on
  }
  f1::Choc1World &world() { return *f1::choc1WorldOf(ctx); }
  void move(float x, float y) {
    wm.handleInput(mouse(ui::InputEvent::Type::MouseMove, x, y));
  }
  void down(float x, float y) {
    wm.handleInput(mouse(ui::InputEvent::Type::MouseDown, x, y));
  }
  void up(float x, float y) {
    wm.handleInput(mouse(ui::InputEvent::Type::MouseUp, x, y));
  }
  void pump() {
    wm.clearDestroyed();
    wm.dispatchMessages();
  }
  // A Rollover in the top modal: 40x40 at (x, y) with the extra keys of `extra`
  // (Lua table fields).
  void rollover(const std::string &name, int x, int y,
                const std::string &extra) {
    REQUIRE(run("do local t = { typename = 'Rollover', name = '" + name +
                "', x = " + std::to_string(x) + ", y = " + std::to_string(y) +
                ", w = 40, h = 40, target = \"LBuilding:ByName('" + name +
                "')\", " + extra +
                " }; GenericCreate(t); PostCreate(t, 'Rollover'); PopWindow() "
                "end") == 0);
  }
  script::ScriptHost host;
  bool booted = false;
};

ui::Window *popupOf(StoryEnv &e) {
  for (const auto &c : e.wm.topModal()->children())
    if (c->typeName() == "RolloverPopup")
      return c.get();
  return nullptr;
}

} // namespace

TEST_CASE("Choc1 Rollover: the window type replaces the built-in only for "
          "Chocolatier 1") {
  StoryEnv e;
  REQUIRE(e.booted);
  e.rollover("shop", 100, 100, "");
  CHECK(dynamic_cast<f1::RolloverWindow *>(e.wm.findInTopModal("shop")));
  CHECK(e.log.contains("[unimplemented-type] Rollover") == false);
}

TEST_CASE("Choc1 Rollover: a move opens an instant popup from `target` at "
          "pointer + 20, one popup at a time") {
  StoryEnv e;
  REQUIRE(e.booted);
  e.wm.tickTransitions(1000);
  e.rollover("shop", 100, 100, "hotSize = 10");
  e.rollover("bank", 300, 100, "hotSize = 10");

  e.move(110, 110);
  ui::Window *popup = popupOf(e);
  REQUIRE(popup);
  CHECK(e.getString("gSeenRollover") ==
        "LBuilding:ByName('shop')"); // the global the script reads
  CHECK(popup->alpha() == 1.0f);     // no fade in
  CHECK(e.wm.transitionCount() == 0);
  CHECK(popup->rect().x == 130); // pointer + xOffset 20, yOffset 0
  CHECK(popup->rect().y == 110);
  CHECK(popup->rect().w == 60); // fitted to its child
  CHECK(e.wm.isMouseListener(popup->id()));
  CHECK(e.getNumber("gPopupBuilds") == 1);

  // Moves inside the 10x10 hot rect (the pointer's position at open +-5) keep
  // it; the popup swallows them, so nothing re-opens.
  e.move(114, 114);
  e.pump();
  CHECK(popupOf(e) == popup);
  CHECK(e.getNumber("gPopupBuilds") == 1);
  // The first move outside closes it and, in the same event, opens a new one at the pointer (no frame without a
  // popup while it follows the pointer).
  const ui::WindowId oldId = popup->id();
  e.move(115, 110);
  e.pump();
  CHECK_FALSE(e.wm.isMouseListener(oldId));
  CHECK(popupOf(e));
  CHECK(popupOf(e)->id() != oldId);
  CHECK(e.getNumber("gPopupBuilds") == 2);
  e.move(116, 110); // inside the new hot rect: kept
  CHECK(e.getNumber("gPopupBuilds") == 2);

  // A second Rollover opening closes the first one's popup (one at a time):
  // force it by moving there with the listener gone.
  f1::RolloverWindow *shop =
      dynamic_cast<f1::RolloverWindow *>(e.wm.findInTopModal("shop"));
  f1::RolloverWindow *bank =
      dynamic_cast<f1::RolloverWindow *>(e.wm.findInTopModal("bank"));
  REQUIRE(shop);
  REQUIRE(bank);
  CHECK(shop->popupOpen());
  CHECK(bank->onMouseMove(10, 10));
  e.pump();
  CHECK(bank->popupOpen());
  CHECK_FALSE(shop->popupOpen());
}

TEST_CASE("Choc1 Rollover: the hot size defaults to 40, hotWidth/hotHeight "
          "override, the popup keeps its place at the screen edge") {
  StoryEnv e;
  REQUIRE(e.booted);
  e.rollover(
      "wide", 700, 100,
      "script = 'ui/rollover_wide.lua'"); // 300x200 popup, default hot 40x40
  e.move(730, 130);
  ui::Window *popup = popupOf(e);
  REQUIRE(popup);
  // 730 + 20 + 300 = 1050 > 800: moved left by 260 and down 32.
  CHECK(popup->rect().x == 500);
  CHECK(popup->rect().y == 162);
  e.move(749, 149); // inside the 40x40 rect around (730, 130): x < 750, y < 150
  CHECK(popupOf(e) == popup);
  e.move(750, 130);
  CHECK(popupOf(e) == nullptr);
}

TEST_CASE("Choc1 Rollover: a mouse up closes the popup and queues "
          "DispatchRolloverClick; the area gate returns not handled") {
  StoryEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("gCommandQueue = {}; function DispatchRolloverClick() end") ==
          0);
  e.rollover("shop", 100, 100, "hotSize = 10, area = { { 0, 0, 20, 40 } }");
  auto *shop = dynamic_cast<f1::RolloverWindow *>(e.wm.findInTopModal("shop"));
  REQUIRE(shop);
  CHECK_FALSE(shop->onMouseMove(
      30, 10)); // outside the area rect: falls through to the sibling behind
  CHECK_FALSE(shop->popupOpen());
  e.move(110, 110);
  REQUIRE(popupOf(e));
  e.down(110, 110); // swallowed
  CHECK(popupOf(e));
  e.up(
      300,
      300); // any mouse up reaches the popup (it is a listener), wherever it is
  e.pump();
  CHECK(popupOf(e) == nullptr);
  REQUIRE(e.run("gQueued = table.getn(gCommandQueue); gFirst = "
                "gCommandQueue[1] == DispatchRolloverClick") == 0);
  CHECK(e.getNumber("gQueued") == 1);
  CHECK(e.getString("gFirst") == "true");
}

TEST_CASE("Choc1 Rollover: no popup while a map trip runs; the image pulses "
          "while the popup is open") {
  StoryEnv e;
  REQUIRE(e.booted);
  e.rollover("shop", 100, 100, "hotSize = 10, image = 'shop_img'");
  e.world().mapTrips = 1;
  e.move(110, 110);
  CHECK(popupOf(e) == nullptr);
  e.world().mapTrips = 0;
  e.move(110, 110);
  REQUIRE(popupOf(e));
  REQUIRE(e.world().pulses.size() == 1);
  CHECK(e.world().pulses[0].name == "shop_img");
  e.move(150, 150); // leaves the hot rect
  e.pump();
  CHECK(popupOf(e) == nullptr);
  CHECK(e.world().pulses.empty()); // the pulser ends with the popup
}

TEST_CASE("Choc1 Rollover: the popup's pulser and UtilPulseImage's are separate "
          "TImagePulsers on the same image") {
  StoryEnv e;
  REQUIRE(e.booted);
  e.rollover("shop", 100, 100, "hotSize = 10, image = 'shop_img'");
  // sim/port.lua:376 pulses "<building>_img" for 30 ticks when the port builds.
  REQUIRE(e.run("UtilPulseImage('shop_img', 30)") == 0);
  REQUIRE(e.world().pulses.size() == 1);
  CHECK_FALSE(e.world().pulses[0].popup);
  CHECK(e.world().pulses[0].count == 30);
  e.world().pulses[0].count = 22; // eight ticks in
  e.world().pulses[0].index = 8;

  e.move(110, 110); // a hover within those 3 s opens the popup
  REQUIRE(popupOf(e));
  REQUIRE(e.world().pulses.size() == 2);
  CHECK_FALSE(e.world().pulses[0].popup); // untouched
  CHECK(e.world().pulses[0].count == 22);
  CHECK(e.world().pulses[0].index == 8);
  CHECK(e.world().pulses[1].popup);
  CHECK(e.world().pulses[1].name == "shop_img");

  e.move(150, 150); // the popup closes: only its pulser stops
  e.pump();
  CHECK(popupOf(e) == nullptr);
  REQUIRE(e.world().pulses.size() == 1);
  CHECK_FALSE(e.world().pulses[0].popup);
  CHECK(e.world().pulses[0].count == 22);

  // A new UtilPulseImage restarts the Util pulser, never a popup one.
  e.move(110, 110);
  REQUIRE(e.world().pulses.size() == 2);
  REQUIRE(e.run("UtilPulseImage('shop_img', 30)") == 0);
  CHECK(e.world().pulses.size() == 2);
  CHECK(e.world().pulses[0].count == 30);
  CHECK(e.world().pulses[0].index == 0);
  CHECK(e.world().pulses[1].popup);
}

TEST_CASE("Choc1 playerLayout: newplayer off at ten users, the new-player "
          "entry follows the last user, the selected slot is on") {
  auto l = f1::playerLayout(1, 2);
  CHECK(l.newPlayerShown);
  CHECK(l.slots[0].shown);
  CHECK(l.slots[0].label == f1::PlayerSlot::Label::UserName);
  CHECK_FALSE(l.slots[0].radioOn);
  CHECK_FALSE(l.slots[0].editShown);
  CHECK(l.slots[1].radioOn);
  CHECK(l.slots[1].editShown);
  CHECK(l.slots[2].shown);
  CHECK(l.slots[2].label == f1::PlayerSlot::Label::NewPlayer);
  CHECK_FALSE(l.slots[2].radioOn);
  CHECK_FALSE(l.slots[3].shown);
  CHECK_FALSE(f1::playerLayout(0, 10).newPlayerShown);
  CHECK(f1::playerLayout(0, 10).slots[9].label ==
        f1::PlayerSlot::Label::UserName);
  CHECK_FALSE(f1::playerLayout(-1, 0).slots[0].radioOn); // nobody selected
  CHECK(f1::playerLayout(0, 0).slots[0].label ==
        f1::PlayerSlot::Label::NewPlayer);
}

TEST_CASE("Choc1 ChoosePlayer: UpdatePlayerButtons lays the slots out for the "
          "users that exist") {
  StoryEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("while GetNumUsers() > 0 do DeleteUser(0) end; "
                "CreateNewUser('Ann'); CreateNewUser('Bea')") == 0);
  REQUIRE(
      e.run("do local c = { x = 10, y = 10, w = 300, h = 340 }; "
            "for i = 0, 9 do "
            "  table.insert(c, Button{ name = 'player' .. i, type = kRadio, x "
            "= 0, y = i * 30 }); "
            "  table.insert(c, Text{ name = 'label' .. i, label = '#', x = 40, "
            "y = i * 30, w = 100, h = 20 }); "
            "  table.insert(c, Button{ name = 'edit' .. i, x = 150, y = i * 30 "
            "}); "
            "  table.insert(c, Button{ name = 'delete' .. i, x = 180, y = i * "
            "30 }); "
            "end; "
            "table.insert(c, Button{ name = 'newplayer', x = 0, y = 320 }); "
            "MakeDialog{ ChoosePlayer(c) } end") == 0);
  auto *w = f1::ChoosePlayerWindow::inChain(e.wm);
  REQUIRE(w);
  auto shown = [&](const char *n) { return e.wm.findInTopModal(n)->shown(); };
  auto on = [&](const char *n) {
    return dynamic_cast<ui::ButtonWindow *>(e.wm.findInTopModal(n))->on();
  };
  // The first layout highlights the current user (Bea, index 1).
  CHECK(shown("player0"));
  CHECK(shown("player1"));
  CHECK(shown("player2")); // the new-player entry
  CHECK_FALSE(shown("player3"));
  CHECK_FALSE(shown("label3"));
  CHECK(e.wm.findInTopModal("label0")->label() == "Ann");
  CHECK(e.wm.findInTopModal("label1")->label() == "Bea");
  CHECK_FALSE(e.wm.findInTopModal("label2")
                  ->label()
                  .empty()); // the "new player" string (or its key)
  CHECK(on("player1"));
  CHECK_FALSE(on("player0"));
  CHECK(shown("edit1"));
  CHECK(shown("delete1"));
  CHECK_FALSE(shown("edit0"));
  CHECK(shown("newplayer"));

  REQUIRE(e.run("UpdatePlayerButtons(0)") ==
          0); // the native, as the Lua calls it
  CHECK(on("player0"));
  CHECK_FALSE(on("player1"));
  CHECK(shown("edit0"));
  CHECK_FALSE(shown("edit1"));
}

TEST_CASE("Choc1 Rollover: raw x/y win, so x=-3 stays -3 with a 198x143 child "
          "(not the generic 800 - 3)") {
  StoryEnv e;
  REQUIRE(e.booted);
  REQUIRE(
      e.run("do local p = { typename = 'Window', name = 'bg', x = 0, y = 0, w = "
            "800, h = 600 }; GenericCreate(p); "
            "local t = { typename = 'Rollover', name = 'fac', x = -3, y = 53, "
            "hotSize = 10 }; GenericCreate(t); "
            "local c = { typename = 'Window', name = 'art', w = 198, h = 143 }; "
            "GenericCreate(c); PostCreate(c, 'Window'); PopWindow(); "
            "PostCreate(t, 'Rollover'); PopWindow(); PostCreate(p, 'Window'); "
            "PopWindow() end") == 0);
  ui::Window *fac = e.wm.findInTopModal("fac");
  REQUIRE(fac);
  CHECK(fac->rect() == gfx::RectI{-3, 53, 198, 143});
  // A second post-children pass (w is now non-zero, so the generic layout's
  // range test passes) must not wrap it.
  ui::MapStyle st;
  st.set("x", -3.0);
  st.set("y", 53.0);
  fac->postChildrenInit(st);
  CHECK(fac->rect() == gfx::RectI{-3, 53, 198, 143});
  // The hot test works at a negative x: the pointer at screen x 100 is inside.
  CHECK(fac->rect().x + 198 > 100);
}
