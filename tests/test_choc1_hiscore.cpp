// Chocolatier 1's HiscoreWindow (THiscore; docs/re/choc1-logo-and-hiscore.md): the shared offline screen with
// the score column through `formatScore` (FUN_0041e0e0's money tables and a 999,999,999 cap) and the gamemode
// label (`empire_mode` / `free_mode`).
#include <doctest/doctest.h>

#include <memory>
#include <string>

#include "ScriptHarness.h"
#include "games/choc1/HiscoreWindow.h"
#include "sdk/Users.h"
#include "ui/Style.h"
#include "ui/widgets/TextWidgets.h"

using namespace choco;

namespace {

struct Choc1Fixture : test::ScriptEnv {
  Choc1Fixture() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ledger") {
    ctx.strings.loadRows({{"formatScore", "%1%/week"},
                          {"formatOnes", "$%d"},
                          {"formatThousands", "$%d,%03d"},
                          {"formatMillions", "$%d,%03d,%03d"},
                          {"empire_mode", "Story Mode"},
                          {"free_mode", "Free Play Mode"}});
    config.set("hiscorelocal", "1");
    ctx.users = std::make_shared<sdk::UserStore>("");
    modal = &wm.pushModal("m");
    modal->setRect({0, 0, 800, 600});
    auto owned = std::make_unique<game::choc1::HiscoreWindow>(wm, ctx);
    win = static_cast<game::choc1::HiscoreWindow*>(&modal->adoptFront(std::move(owned)));
    win->setRect({0, 0, 800, 600});
    for (int i = 1; i <= 10; ++i) {
      const std::string n = std::to_string(i);
      addText("name" + n);
      addText("score" + n);
      addText(n);
    }
    addText("gamemode");
    addText("local");
  }
  void addText(std::string name) {
    auto t = std::make_unique<ui::TextWindow>(wm);
    t->setSize(300, 24);
    t->setName(std::move(name));
    win->adoptFront(std::move(t));
  }
  void open() { win->postChildrenInit(ui::MapStyle{}); }
  ui::Window* child(const std::string& n) { return win->find(n); }
  ui::Window* modal = nullptr;
  game::choc1::HiscoreWindow* win = nullptr;
};

}  // namespace

TEST_CASE("choc1 hiscore: the score column reads formatScore with the score money-formatted and capped") {
  Choc1Fixture f;
  f.ctx.users->create("Ann");
  f.ctx.users->logScore(2000, "", "");
  f.ctx.users->create("Bob");
  f.ctx.users->logScore(1000, "", "");
  f.open();
  CHECK(f.child("name1")->label() == "Ann");
  CHECK(f.child("score1")->label() == "$2,000/week");  // formatScore + the money tables formats
  CHECK(f.child("1")->label() == "1.");
  CHECK(f.child("score2")->label() == "$1,000/week");
  CHECK(f.child("score3")->shown() == false);
}

TEST_CASE("choc1 hiscore: the cap of 999,999,999 applies before the format") {
  Choc1Fixture f;
  f.ctx.users->create("Ann");
  f.ctx.users->logScore(2345000000, "", "");
  f.open();
  CHECK(f.child("score1")->label() == "$999,999,999/week");
}

TEST_CASE("choc1 hiscore: the gamemode label follows the current user's mode") {
  Choc1Fixture f;
  f.ctx.users->create("Ann");
  f.ctx.users->logScore(200, "", "");
  f.open();
  CHECK(f.child("gamemode")->label() == "Story Mode");  // empire_mode: pref gamemode 0
  f.ctx.users->setPref("gamemode", "1");
  f.open();
  CHECK(f.child("gamemode")->label() == "Free Play Mode");  // free_mode
}
