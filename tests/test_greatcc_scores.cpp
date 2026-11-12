// The Great Chocolate Chase, slice 19 (docs/design/greatcc.md section 6): the four local high-score tables, their persistence, the
// rename that logs the user's best rows again, and the HiscoreWindow over them.
#include <doctest/doctest.h>

#include <filesystem>
#include <memory>
#include <string>

#include "games/Registry.h"
#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/core/Settings.h"
#include "games/greatcc/ui/HiscoreWindow.h"
#include "games/greatcc/ui/RShrinkText.h"
#include "sdk/Users.h"
#include "ui/widgets/TextWidgets.h"
#include "ScriptHarness.h"

using namespace choco;
using namespace choco::games::greatcc;

namespace {

namespace fs = std::filesystem;
const std::string kState = std::string(CHOCO_TEST_ASSETS) + "/greatcc/state";

std::string scratch(const std::string& tag) {
  const fs::path p = fs::temp_directory_path() / ("choco_greatcc_scores_" + tag);
  std::error_code ec;
  fs::remove_all(p, ec);
  fs::create_directories(p, ec);
  return p.string();
}

// A booted GREATCC context over the fixture, with its own user directory.
struct Env : test::ScriptEnv {
  explicit Env(const std::string& tag) : ScriptEnv(kState, scratch(tag)), host(ctx) {
    ctx.module = games::findModule("greatcc");
    ctx.module->attach(ctx);
    booted = host.boot();
  }
  ~Env() { wm.destroyAll(); }
  GreatCCWorld& world() { return *worldOf(ctx); }
  const std::vector<sdk::HiscoreRow>& rows(int mode) { return ctx.users->hiscores().rows(kModeNames[mode]); }
  script::ScriptHost host;
  bool booted = false;
};

ui::MapStyle box(double x, double y, double w, double h) {
  ui::MapStyle s;
  s.set("x", x).set("y", y).set("w", w).set("h", h);
  return s;
}

}  // namespace

TEST_CASE("hiscores: a table per mode, best first, ties share a rank, one row per user, the table keeps 500") {
  Env e("tables");
  REQUIRE(e.booted);
  GreatCCWorld& w = e.world();
  e.ctx.users->create("Ada");
  e.ctx.users->create("Bob");
  e.ctx.users->setCurrent(0);
  w.setGameMode(e.ctx, 0);
  w.logScore(e.ctx, 500, "3", "");
  w.logScore(e.ctx, 300, "2", "");  // below Ada's own row: nothing changes
  REQUIRE(e.rows(0).size() == 1);
  CHECK(e.rows(0)[0].score == 500);
  CHECK(e.rows(0)[0].a == "3");
  e.ctx.users->setCurrent(1);
  w.logScore(e.ctx, 500, "4", "");
  w.logScore(e.ctx, 800, "5", "");  // better: replaces Bob's row
  e.ctx.users->setCurrent(0);
  w.logScore(e.ctx, 650, "6", "");  // replaces Ada's
  REQUIRE(e.rows(0).size() == 2);
  CHECK(e.rows(0)[0].user == "Bob");
  CHECK(e.rows(0)[0].score == 800);
  CHECK(e.rows(0)[1].user == "Ada");
  CHECK(e.rows(0)[1].score == 650);
  // the other modes are their own tables: the mode picks the table, and the log does not touch the others
  w.setGameMode(e.ctx, 2);
  w.logScore(e.ctx, 650, "1000", "");
  CHECK(e.rows(0).size() == 2);
  CHECK(e.rows(1).empty());
  REQUIRE(e.rows(2).size() == 1);
  CHECK(e.rows(2)[0].a == "1000");
  e.ctx.users->setCurrent(1);
  w.logScore(e.ctx, 650, "900", "");  // a tie goes after the first one and shares its rank
  REQUIRE(e.rows(2).size() == 2);
  CHECK(e.rows(2)[0].user == "Ada");
  CHECK(e.rows(2)[1].user == "Bob");
  CHECK(e.rows(2)[0].rank == 1);
  CHECK(e.rows(2)[1].rank == 1);
  // no user: nothing is logged
  e.ctx.users->remove(1);
  e.ctx.users->remove(0);
  w.logScore(e.ctx, 99999, "1", "");
  CHECK(e.rows(2).size() == 2);
}

TEST_CASE("hiscores: the tables are kept per game mode name and survive a restart") {
  const std::string dir = scratch("persist");
  {
    Env e("persist");
    REQUIRE(e.booted);
    e.ctx.users->create("Ada");
    e.world().setGameMode(e.ctx, 3);
    e.world().logScore(e.ctx, 1234, "77", "");
    e.world().setGameMode(e.ctx, 0);
    e.world().logScore(e.ctx, 99, "1", "<medals/>");
  }
  sdk::UserStore reopened(dir);
  const auto& hard = reopened.hiscores().rows("greatchocolatechase_endlesshard");
  REQUIRE(hard.size() == 1);
  CHECK(hard[0].score == 1234);
  CHECK(hard[0].user == "Ada");
  CHECK(hard[0].a == "77");
  const auto& story = reopened.hiscores().rows("greatchocolatechase_storymode");
  REQUIRE(story.size() == 1);
  CHECK(story[0].b == "<medals/>");
  CHECK(reopened.hiscores().rows("greatchocolatechase_endlesseasy").empty());
}

TEST_CASE("ChangeCurrentUserName: the best row of each mode is logged again under the new name, the old rows stay") {
  Env e("rename");
  REQUIRE(e.booted);
  GreatCCWorld& w = e.world();
  e.ctx.users->create("Ada");
  w.setGameMode(e.ctx, 0);
  w.logScore(e.ctx, 500, "3", "");
  w.setGameMode(e.ctx, 3);
  w.logScore(e.ctx, 40, "120", "");
  w.setGameMode(e.ctx, 1);  // nothing in the easy table
  CHECK(e.run("ChangeCurrentUserName('Grace')") == 0);
  CHECK(e.ctx.users->currentName() == "Grace");
  REQUIRE(e.rows(0).size() == 2);
  CHECK(e.rows(0)[0].user == "Ada");
  CHECK(e.rows(0)[1].user == "Grace");
  CHECK(e.rows(0)[1].score == 500);
  CHECK(e.rows(0)[1].a == "3");
  REQUIRE(e.rows(3).size() == 2);
  CHECK(e.rows(3)[1].user == "Grace");
  CHECK(e.rows(3)[1].a == "120");
  CHECK(e.rows(1).empty());
  CHECK(e.rows(2).empty());
  // a name the store refuses changes nothing and logs nothing
  CHECK(e.run("ChangeCurrentUserName('')") == 0);
  CHECK(e.ctx.users->currentName() == "Grace");
  CHECK(e.rows(0).size() == 2);
}

TEST_CASE("HiscoreWindow: the current mode's heading and rows, the panel move, and the arrows step the game mode") {
  Env e("window");
  REQUIRE(e.booted);
  GreatCCWorld& w = e.world();
  e.ctx.users->create("Ada");
  e.ctx.users->create("A very long name that cannot fit its box");
  w.setGameMode(e.ctx, 0);
  e.ctx.users->setCurrent(0);
  w.logScore(e.ctx, 500, "3", "");
  e.ctx.users->setCurrent(1);
  w.logScore(e.ctx, 700, "4", "");
  e.ctx.users->setCurrent(0);
  w.setGameMode(e.ctx, 3);
  w.logScore(e.ctx, 40, "120", "");
  w.setGameMode(e.ctx, 0);
  e.ctx.config.set("hiscorelocal", "1");
  e.wm.pushModal("m");
  auto root = box(0, 0, 800, 600);
  auto& win = static_cast<HiscoreWindow&>(e.wm.initWindow(std::make_unique<HiscoreWindow>(e.wm, e.ctx), root));
  auto addText = [&](const char* name, double x, double y, double w2, double h, bool shrink = false) {
    auto st = box(x, y, w2, h);
    st.set("label", "x");
    ui::Window& t = shrink ? e.wm.initWindow(std::make_unique<RShrinkText>(e.wm), st)
                           : e.wm.initWindow(std::make_unique<ui::TextWindow>(e.wm), st);
    t.setName(name);
    e.wm.popCreation();
  };
  addText("gamemode", 0, 138, 800, 40);
  addText("topplayers", 0, 10, 300, 20);
  addText("local", 0, 10, 300, 20);
  for (int i = 1; i <= 10; ++i) {
    const std::string n = std::to_string(i);
    addText(n.c_str(), 0, 56.0 + 21 * (i - 1), 35, 20);
    addText(("name" + n).c_str(), 52, 56.0 + 21 * (i - 1), 80, 21, true);
    addText(("score" + n).c_str(), 0, 56.0 + 21 * (i - 1), 286, 20);
  }
  {
    auto lp = box(122, 175, 300, 300);
    ui::Window& left = e.wm.initWindow(std::make_unique<ui::Window>(e.wm), lp);
    left.setName("leftpanel");
    e.wm.popCreation();
  }
  win.postChildrenInit(root);
  e.wm.popCreation();
  auto shown = [&](const std::string& n) { return win.find(n)->shown(); };
  auto label = [&](const std::string& n) { return win.find(n)->label(); };
  // local-only: the table's panel moves 130 px right; "topplayers" shows and "local" does not
  CHECK(win.find("leftpanel")->rect().x == 122 + 130);
  CHECK(shown("topplayers"));
  CHECK_FALSE(shown("local"));
  CHECK(win.state() == 0);
  // the story table: two rows, best first, rank "1.", the score as a plain number
  CHECK(win.shownRows() == 2);
  CHECK(shown("name1"));
  CHECK(label("name1") == "A very long name that cannot fit its box");
  CHECK(label("score1") == "700");
  CHECK(label("1") == "1.");
  CHECK(label("name2") == "Ada");
  CHECK(label("score2") == "500");
  CHECK(label("2") == "2.");
  CHECK_FALSE(shown("name3"));
  CHECK_FALSE(shown("score3"));
  CHECK(label("gamemode").find("<outline color='000000' size='1'>") == 0);
  // the left arrow wraps from the story mode to the hard mode and the table follows
  ui::Message left;
  left.type = ui::kButtonPress;
  left.name = "gamemodeleft";
  CHECK(win.onMessage(left));
  CHECK(w.settings().mode == 3);
  CHECK(win.shownRows() == 1);
  CHECK(label("score1") == "40");
  ui::Message right;
  right.type = ui::kButtonPress;
  right.name = "gamemoderight";
  CHECK(win.onMessage(right));
  CHECK(w.settings().mode == 0);  // wraps back
  CHECK(win.shownRows() == 2);
  CHECK(e.world().settings().mode == std::atoi(e.ctx.users->globalPref("gamemode", "9").c_str()));  // the global pref follows
  // an empty table shows no row
  CHECK(win.onMessage(right));
  CHECK(w.settings().mode == 1);
  CHECK(win.shownRows() == 0);
  CHECK_FALSE(shown("name1"));
}
