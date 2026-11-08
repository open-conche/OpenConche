#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "ScriptHarness.h"
#include "sdk/HiscoreStore.h"
#include "games/choc3/HiscoreWindow.h"
#include "sdk/Users.h"
#include "ui/Style.h"
#include "ui/widgets/TextWidgets.h"

using namespace choco;
using sdk::HiscoreRow;
using sdk::HiscoreStore;
namespace fs = std::filesystem;

namespace {

struct TempDir {
  explicit TempDir(const char* tag) {
    path = fs::temp_directory_path() / (std::string("choco_hiscore_") + tag);
    fs::remove_all(path);
    fs::create_directories(path);
  }
  ~TempDir() { fs::remove_all(path); }
  fs::path path;
};

}  // namespace

TEST_CASE("hiscore store: sorted descending, ties share a rank and the next rank skips") {
  HiscoreStore s;
  CHECK(s.record("0", "ann", 100, "a1", "b1"));
  CHECK(s.record("0", "bob", 300, "a2", "b2"));
  CHECK(s.record("0", "cy", 100, "a3", "b3"));  // ties rank below the older equal score in position
  CHECK(s.record("0", "di", 200, "a4", "b4"));
  const auto& r = s.rows("0");
  REQUIRE(r.size() == 4);
  CHECK(r[0].user == "bob");
  CHECK(r[1].user == "di");
  CHECK(r[2].user == "ann");
  CHECK(r[3].user == "cy");
  CHECK(r[0].rank == 1);
  CHECK(r[1].rank == 2);
  CHECK(r[2].rank == 3);
  CHECK(r[3].rank == 3);  // shares the rank of the tied score
  s.record("0", "ed", 100, "", "");
  CHECK(s.rows("0")[4].rank == 3);
  s.record("0", "fay", 50, "", "");
  CHECK(s.rows("0")[5].rank == 6);  // 1, 2, 3, 3, 3, 6
}

TEST_CASE("hiscore store: one row per user, replaced only by a higher score") {
  HiscoreStore s;
  CHECK(s.record("0", "ann", 100, "old", "ob"));
  CHECK_FALSE(s.record("0", "ann", 100, "same", ""));  // an equal score changes nothing
  CHECK_FALSE(s.record("0", "ann", 40, "lower", ""));
  CHECK(s.rows("0").size() == 1);
  CHECK(s.rows("0")[0].a == "old");
  CHECK(s.record("0", "bob", 80, "", ""));
  CHECK(s.record("0", "ann", 150.9, "new", "nb"));  // truncated to 150
  REQUIRE(s.rows("0").size() == 2);
  CHECK(s.rows("0")[0].user == "ann");
  CHECK(s.rows("0")[0].score == 150);
  CHECK(s.rows("0")[0].a == "new");
  CHECK(s.rows("0")[0].b == "nb");
  CHECK(s.hasUser("0", "bob"));
  CHECK_FALSE(s.hasUser("0", "zed"));
  CHECK_FALSE(s.hasUser("1", "ann"));  // another game mode has its own table
}

TEST_CASE("hiscore store: scores below 1 are skipped") {
  HiscoreStore s;
  CHECK_FALSE(s.record("0", "ann", 0, "", ""));
  CHECK_FALSE(s.record("0", "ann", 0.9, "", ""));
  CHECK_FALSE(s.record("0", "ann", -5, "", ""));
  CHECK(s.rows("0").empty());
  CHECK_FALSE(s.hasUser("0", "ann"));
}

TEST_CASE("hiscore store: a table holds 500 rows, the lowest drop off") {
  HiscoreStore s;
  for (int i = 1; i <= 500; ++i) CHECK(s.record("0", "u" + std::to_string(i), i * 10, "", ""));
  REQUIRE(s.rows("0").size() == 500);
  CHECK_FALSE(s.record("0", "low", 5, "", ""));  // below the 500th
  CHECK(s.rows("0").size() == 500);
  CHECK(s.record("0", "mid", 2505, "", ""));  // pushes the lowest (10) out
  REQUIRE(s.rows("0").size() == 500);
  CHECK_FALSE(s.hasUser("0", "u1"));
  CHECK(s.hasUser("0", "mid"));
  CHECK(s.rows("0").back().score == 20);
  CHECK(s.rows("0").front().score == 5000);
}

TEST_CASE("hiscore store: serialize and parse round trip, with awkward text") {
  HiscoreStore s;
  s.record("0", "Zo\xC3\xAB\ttab", 300, "3-000000120-000001234567", "<medal name='x' per='game' />\nline");
  s.record("0", "bob", 300, "a", "b");
  s.record("2", "cy", 7, "", "");
  HiscoreStore r;
  r.parse(s.serialize());
  CHECK(r.serialize() == s.serialize());
  REQUIRE(r.rows("0").size() == 2);
  CHECK(r.rows("0")[0].user == "Zo\xC3\xAB\ttab");
  CHECK(r.rows("0")[0].b == "<medal name='x' per='game' />\nline");
  CHECK(r.rows("0")[1].rank == 1);
  CHECK(r.rows("2")[0].score == 7);
  HiscoreStore bad;
  bad.parse("not the header\n0\t5\tx\ta\tb\n");
  CHECK(bad.rows("0").empty());
}

TEST_CASE("hiscore store: UserStore.logScore keeps it in hiscore.dat across a reload") {
  TempDir d("persist");
  {
    sdk::UserStore u(d.path.string());
    u.create("Ann");
    CHECK(u.logScore(1500.7, "1-000000010-000000000100", "<m/>"));
    CHECK(u.logScore(0, "x", "y"));  // skipped
    u.create("Bob");
    CHECK(u.logScore(900, "2-000000020-000000000200", ""));
    CHECK(u.logScore(100, "ignored", ""));  // lower than Bob's own
    CHECK(fs::exists(d.path / "hiscore.dat"));
  }
  sdk::UserStore r(d.path.string());
  const auto& rows = r.hiscores().rows("0");
  REQUIRE(rows.size() == 2);
  CHECK(rows[0].user == "Ann");
  CHECK(rows[0].score == 1500);
  CHECK(rows[1].user == "Bob");
  CHECK(r.scoreAvailable());  // Bob is current
  r.setCurrent(0);
  CHECK(r.scoreAvailable());
  r.setPref("gamemode", "1");
  CHECK_FALSE(r.scoreAvailable());  // the other mode's table is empty
}

TEST_CASE("hiscore store: a user store without a folder keeps the scores in memory") {
  sdk::UserStore u("");
  u.create("Ann");
  CHECK(u.logScore(10, "a", "b"));
  CHECK(u.hiscores().rows("0").size() == 1);
}

namespace {

struct HiscoreFixture : test::ScriptEnv {
  HiscoreFixture() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ledger") {
    ctx.strings.loadRows({{"rank_1", "Rookie"}, {"rank_4", "CEO"}, {"score_format", "%1%/week"},
                          {"score_info_format", "%1%: %2% in %3% weeks"}});
    config.set("hiscorelocal", "1");
    ctx.users = std::make_shared<sdk::UserStore>("");
    modal = &wm.pushModal("m");
    modal->setRect({0, 0, 800, 600});
    auto owned = std::make_unique<game::HiscoreWindow>(wm, ctx);
    win = static_cast<game::HiscoreWindow*>(&modal->adoptFront(std::move(owned)));
    win->setRect({0, 0, 800, 600});
    for (int i = 1; i <= 10; ++i) {
      const std::string n = std::to_string(i);
      addText("name" + n, 300, 24);
      addText("score" + n, 100, 24);
      addText("info" + n, 300, 24);
      addText(n, 30, 24);
      addPlain("p1_" + n, "Bitmap");
    }
    for (const char* n : {"rightpanel", "rightpanelsmall", "scrollup", "scrolldown", "server", "local", "global"})
      addPlain(n, "Window");
  }
  void addText(std::string name, int w, int h) {
    auto t = std::make_unique<ui::TextWindow>(wm);
    t->setSize(w, h);
    t->setName(std::move(name));
    win->adoptFront(std::move(t));
  }
  void addPlain(std::string name, const char* type) {
    auto p = std::make_unique<ui::PlaceholderWindow>(wm, type);
    p->setName(std::move(name));
    win->adoptFront(std::move(p));
  }
  void open() { win->postChildrenInit(ui::MapStyle{}); }
  ui::Window* child(const std::string& n) { return win->find(n); }
  ui::Window* modal = nullptr;
  game::HiscoreWindow* win = nullptr;
};

}  // namespace

TEST_CASE("hiscore text: ParseGameData and the score column use the game's formats") {
  HiscoreFixture f;
  CHECK(game::HiscoreWindow::parseGameData(f.ctx, "4-000000120-000001234567") == "CEO: $1,234,567 in 120 weeks");
  CHECK(game::HiscoreWindow::parseGameData(f.ctx, "1-000012345-000000000000") == "Rookie: $0 in 12,345 weeks");
  CHECK(game::HiscoreWindow::scoreText(f.ctx, 1234567) == "$1,234,567/week");
}

TEST_CASE("hiscore window: no scores hides every row and the online panels") {
  HiscoreFixture f;
  f.open();
  for (int i = 1; i <= 10; ++i) {
    const std::string n = std::to_string(i);
    CHECK_FALSE(f.child("name" + n)->shown());
    CHECK_FALSE(f.child(n)->shown());
    CHECK(f.child("name" + n)->label() == "");
  }
  for (const char* n : {"rightpanel", "rightpanelsmall", "scrollup", "scrolldown", "server", "global"})
    CHECK_FALSE(f.child(n)->shown());
  CHECK(f.child("local")->shown());
  CHECK(f.win->state() == 0);
}

TEST_CASE("hiscore window: with hiscorelocal off the panels follow the state and the list stays in the error state") {
  HiscoreFixture f;
  f.config.set("hiscorelocal", "0");
  for (const char* n : {"eligible", "info"}) f.addPlain(n, "Text");
  f.addText("error", 200, 40);
  f.ctx.users->create("Ann");
  f.ctx.users->logScore(2000, "1-000000001-000000000001", "");
  f.open();
  CHECK(f.child("rightpanel")->shown());
  CHECK_FALSE(f.child("rightpanelsmall")->shown());
  CHECK(f.child("info")->shown());
  CHECK(f.child("eligible")->shown());  // Ann has a stored score
  CHECK_FALSE(f.child("server")->shown());
  CHECK(f.win->onMessage({ui::kButtonPress, "view", 0}));
  CHECK(f.child("server")->shown());
  CHECK_FALSE(f.child("rightpanel")->shown());
  CHECK(f.child("rightpanelsmall")->shown());
  f.wm.tickWindowAnimations(1000);
  f.wm.tickWindowAnimations(1100);
  CHECK(f.win->state() == 5);
  CHECK_FALSE(f.child("server")->shown());
  CHECK(f.win->find("error")->shown());
  CHECK(f.win->find("error")->label().find("Unable to connect") == 0);
  CHECK(f.child("name1")->shown());  // the local list still shows
  CHECK(f.child("name1")->label() == "Ann");
}

TEST_CASE("hiscore store: a file with two rows for one user keeps one") {
  HiscoreStore s;
  s.parse("choco3-hiscores 1\n0\t500\tAnn\ta\tb\n0\t900\tAnn\tc\td\n0\t100\tBob\te\tf\n");
  CHECK(s.rows("0").size() == 2);
}

TEST_CASE("hiscore window: rows hold the name, rank, score and info text, and rows past the list are hidden") {
  HiscoreFixture f;
  f.ctx.users->create("Ann");
  f.ctx.users->logScore(2000, "4-000000120-000001234567", "");
  f.ctx.users->create("Bob");
  f.ctx.users->logScore(1000, "1-000000010-000000000500", "");
  f.open();
  CHECK(f.child("name1")->label() == "Ann");
  CHECK(f.child("score1")->label() == "$2,000/week");
  CHECK(f.child("1")->label() == "1.");
  CHECK(f.child("info1")->label() == "CEO: $1,234,567 in 120 weeks");
  CHECK(f.child("name2")->label() == "Bob");
  CHECK(f.child("2")->label() == "2.");
  CHECK(f.child("info2")->label() == "Rookie: $500 in 10 weeks");
  CHECK(f.child("name2")->shown());
  CHECK_FALSE(f.child("name3")->shown());
  CHECK_FALSE(f.child("p1_1")->shown());
  CHECK(f.child("name3")->label() == "");
}

TEST_CASE("hiscore window: only the ten best are listed") {
  HiscoreFixture f;
  for (int i = 1; i <= 12; ++i) {
    f.ctx.users->create("u" + std::to_string(i));
    f.ctx.users->logScore(i * 100, "1-000000001-000000000001", "");
  }
  f.open();
  CHECK(f.child("name1")->label() == "u12");
  CHECK(f.child("name10")->label() == "u3");
  CHECK(f.child("10")->label() == "10.");
}

TEST_CASE("hiscore window: a long name shrinks its line height until it fits, but not below 2") {
  HiscoreFixture f;
  f.ctx.users->create(std::string(60, 'W'));
  f.ctx.users->logScore(10, "1-000000001-000000000001", "");
  f.open();
  auto* t = dynamic_cast<ui::TextWindow*>(f.child("name1"));
  REQUIRE(t);
  CHECK(t->textStyle().size < 20);
  CHECK(t->textStyle().size >= 2);
  CHECK((t->textFits() || t->textStyle().size == 2));
  auto* short1 = dynamic_cast<ui::TextWindow*>(f.child("name2"));
  CHECK(short1->textStyle().size == 20);  // an empty row is not shrunk
}

TEST_CASE("hiscore window: online paths end in the error state and viewlocal goes back") {
  HiscoreFixture f;
  f.open();
  f.win->submissionDone(true);
  CHECK(f.win->state() == 1);
  f.wm.tickWindowAnimations(1000);
  f.wm.tickWindowAnimations(1100);
  CHECK(f.win->state() == 5);
  CHECK(f.win->onMessage({ui::kButtonPress, "viewlocal", 0}));
  CHECK(f.win->state() == 0);
  CHECK(f.win->onMessage({ui::kButtonPress, "view", 0}));
  CHECK(f.win->state() == 1);
  CHECK_FALSE(f.win->onMessage({ui::kButtonPress, "somethingelse", 0}));
}
