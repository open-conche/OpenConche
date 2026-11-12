// The Great Chocolate Chase, slice 4 (docs/design/greatcc.md section 6): the comparison helpers, the value, level and
// achievement models, the per-user progress file and the natives over them.
#include <doctest/doctest.h>

#include <cfloat>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "games/Registry.h"
#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/core/AchievementManager.h"
#include "games/greatcc/core/Compare.h"
#include "games/greatcc/core/LevelManager.h"
#include "games/greatcc/core/Progress.h"
#include "games/greatcc/core/ValueManager.h"
#include "ScriptHarness.h"

using namespace choco;
using namespace choco::games::greatcc;

namespace {

namespace fs = std::filesystem;
const std::string kState = std::string(CHOCO_TEST_ASSETS) + "/greatcc/state";

std::string slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// A fresh, empty user directory per test case.
std::string scratch(const std::string& tag) {
  const fs::path p = fs::temp_directory_path() / ("choco_greatcc_state_" + tag);
  std::error_code ec;
  fs::remove_all(p, ec);
  fs::create_directories(p, ec);
  return p.string();
}

// The pure models over a store in a scratch directory, with the fixture's data files.
struct Models {
  explicit Models(const std::string& tag, bool withUser = true)
      : users(std::make_shared<sdk::UserStore>(scratch(tag))), progress(users), values(progress), levels(progress, values),
        achievements(progress, values) {
    if (withUser) users->create("Ada");
    progress.load();
    values.load(slurp(kState + "/values.xml"));
    levels.loadGoals(slurp(kState + "/levels/goals.xml"));
    achievements.load(slurp(kState + "/achievements.xml"));
  }
  std::shared_ptr<sdk::UserStore> users;
  Progress progress;
  ValueManager values;
  LevelManager levels;
  AchievementManager achievements;
};

// A booted GREATCC context over the fixture, with its own user directory.
struct StateEnv : test::ScriptEnv {
  explicit StateEnv(const std::string& userDir) : ScriptEnv(kState, userDir), host(ctx) {
    ctx.module = games::findModule("greatcc");
    ctx.module->attach(ctx);
    booted = host.boot();
  }
  ~StateEnv() { wm.destroyAll(); }
  GreatCCWorld& world() { return *worldOf(ctx); }
  double num(const std::string& expr) {
    REQUIRE(run("tmp_ = " + expr) == 0);
    return getNumber("tmp_");
  }
  script::ScriptHost host;
  bool booted = false;
};

}  // namespace

TEST_CASE("CompareValue: single bits, combined masks and the quirks") {
  CHECK(parseComparison(">=") == 12);
  CHECK(parseComparison("greaterthan + equal") == 12);
  CHECK(parseComparison("GreaterThan+Equal") == 12);
  CHECK(parseComparison("lessthan") == 2);
  CHECK(parseComparison("<") == 2);
  CHECK(parseComparison("!=") == 9);
  CHECK(parseComparison("not + equal") == 9);
  CHECK(parseComparison("lessthan + greaterthan + equal") == 14);
  CHECK(parseComparison("") == 0);

  CHECK(compareValue(1, 2, kCmpLess));
  CHECK_FALSE(compareValue(2, 2, kCmpLess));
  CHECK(compareValue(3, 2, kCmpGreater));
  CHECK_FALSE(compareValue(2, 2, kCmpGreater));
  CHECK(compareValue(2, 2, kCmpEqual));
  CHECK(compareValue(2, 2, kCmpGreater | kCmpEqual));
  CHECK(compareValue(3, 2, kCmpGreater | kCmpEqual));
  CHECK_FALSE(compareValue(1, 2, kCmpGreater | kCmpEqual));
  CHECK(compareValue(2, 2, kCmpLess | kCmpEqual));
  CHECK(compareValue(1, 2, kCmpLess | kCmpEqual));
  // less + greater without equal is false, and stays false with not (the original returns before applying it).
  CHECK_FALSE(compareValue(1, 2, kCmpLess | kCmpGreater));
  CHECK_FALSE(compareValue(3, 2, kCmpLess | kCmpGreater));
  CHECK_FALSE(compareValue(1, 2, kCmpLess | kCmpGreater | kCmpNot));
  // Both sides are floats in the original: a total a hair under the threshold still reaches it.
  CHECK(compareValue(99.99999999999999, 100, kCmpGreater | kCmpEqual));
  // The quirk: all three bits evaluate a <= b, whatever the greater half says.
  CHECK(compareValue(1, 2, kCmpLess | kCmpGreater | kCmpEqual));
  CHECK(compareValue(2, 2, kCmpLess | kCmpGreater | kCmpEqual));
  CHECK_FALSE(compareValue(3, 2, kCmpLess | kCmpGreater | kCmpEqual));
  // not equal.
  CHECK(compareValue(1, 2, kCmpNot | kCmpEqual));
  CHECK_FALSE(compareValue(2, 2, kCmpNot | kCmpEqual));
}

TEST_CASE("GetValue: modifiers in reverse order, base-multiplied, clamped max then min") {
  Models m("values");
  ValueManager& v = m.values;
  CHECK(v.exists("speed"));
  CHECK_FALSE(v.exists(""));  // an empty name is skipped
  CHECK(v.getReal("plain", 0) == 10);

  // No upgrade active: the base.
  CHECK(v.getReal("speed", 0) == 4.0);
  // Active upgrades add base * amount each: 4 + 4*0.5 (kFast) + 4*0.25 (kFaster), never compounding.
  v.setUpgradeActive([](const std::string& u, const std::string& c) { return c == "gameplay" && u == "kFast"; });
  CHECK(v.getReal("speed", 0) == 6.0);
  v.setUpgradeActive([](const std::string& u, const std::string&) { return u == "kFast" || u == "kFaster"; });
  CHECK(v.getReal("speed", 0) == 7.0);
  // The category is passed through.
  std::string seen;
  v.setUpgradeActive([&](const std::string& u, const std::string& c) {
    seen += u + "/" + c + ";";
    return false;
  });
  v.getReal("speed", 0);
  CHECK(seen == "kFaster/gameplay;kFast/gameplay;");  // the last <modify> is asked first

  // An unconditional modifier.
  CHECK(v.getReal("always", 0) == 12.0);
  // byval: the amount is replaced by the named value (0.5), whatever the file says (1).
  CHECK(v.getReal("scaled", 0) == 15.0);
  v.setUpgradeActive([](const std::string& u, const std::string&) { return u == "kBig"; });
  CHECK(v.getReal("scaled", 0) == 16.0);
  v.setValue("diff_modifier", 1.0, false);
  CHECK(v.getReal("scaled", 0) == 21.0);
  // A plot modifier asks the plot callback.
  CHECK(v.getReal("plotted", 0) == 10.0);
  v.setPlotActive([](const std::string& item) { return item == "kPlot"; });
  CHECK(v.getReal("plotted", 0) == doctest::Approx(13.0));
  // A value that names itself ends.
  CHECK(std::isfinite(v.getReal("selfref", 0)));

  // Clamp: 10 + 10 = 20 -> max 12.
  CHECK(v.getReal("clamped", 0) == 12.0);
  v.setValue("clamped", 100, false);  // SetValue clamps the base too
  CHECK(v.getReal("clamped", 0) == 12.0);
  v.setValue("clamped", 1, false);
  CHECK(v.getReal("clamped", 0) == 10.0);  // base 5 (the min), + 5
  // max is applied first and min second, so with min > max the min wins.
  CHECK(v.getReal("floored", 0) == 20.0);
  CHECK(v.minOf("floored") == 20.0);
  CHECK(v.maxOf("floored") == 15.0);
  CHECK(v.minOf("nope") == doctest::Approx(FLT_MIN));
  CHECK(v.maxOf("nope") == doctest::Approx(FLT_MAX));
}

TEST_CASE("values: lazy creation, int and uint rules, Add and Subtract") {
  Models m("lazy");
  ValueManager& v = m.values;
  CHECK_FALSE(v.exists("fresh"));
  CHECK(v.getInt("fresh", 7) == 7);
  CHECK(v.exists("fresh"));
  CHECK(v.getInt("fresh", 99) == 7);  // the first default is the base

  v.setValue("made", 3.75, false);  // creates it with base 3.75 and no clamp
  CHECK(v.getReal("made", 0) == 3.75);
  CHECK(v.getInt("made", 0) == 3);
  v.setValue("made", -2.5, false);
  CHECK(v.getInt("made", 0) == -3);  // floor, not truncation
  CHECK(v.getUint("made", 0) == 0);
  v.setValue("made", 1e12, false);
  CHECK(v.getUint("made", 0) == 4294967295u);

  v.addToValue("tally", 2, 10, false);  // starts from the default
  CHECK(v.getReal("tally", 0) == 12);
  v.subtractFromValue("tally", 5, 0, false);
  CHECK(v.getReal("tally", 0) == 7);
  v.addToValue("highest_level", 500, 0, false);  // Add does not clamp the base; reading clamps (max is 60)
  CHECK(v.getReal("highest_level", 0) == 60);
  v.addToValue("highest_level", -490, 0, false);
  CHECK(v.getReal("highest_level", 0) == 10);
}

TEST_CASE("values: saved ones go to Progress as exact doubles, only with a user and only on request") {
  Models m("saved");
  ValueManager& v = m.values;
  const double odd = 1234.5678901234567;
  v.setValue("total_worth", odd, false);
  CHECK_FALSE(m.progress.has("value.total_worth"));  // not asked to save
  v.setValue("total_worth", odd, true);
  CHECK(m.progress.getDouble("value.total_worth", 0) == odd);
  v.setValue("plain", 5, true);  // not a saved value: never stored
  CHECK_FALSE(m.progress.has("value.plain"));
  v.addToValue("num_bars", 0.1, 0, true);
  v.addToValue("num_bars", 0.2, 0, true);
  CHECK(m.progress.getDouble("value.num_bars", 0) == 0.1 + 0.2);

  // A reload rebuilds from the file: saved values come back, runtime ones return to the schema.
  v.setValue("current_worth", 77, false);
  m.progress.flush();
  m.progress.load();
  v.reload();
  CHECK(v.getReal("total_worth", 0) == odd);
  CHECK(v.getReal("current_worth", 0) == 0);

  // SaveValues writes all of them.
  v.setValue("highest_level", 4, false);
  v.saveAll();
  CHECK(m.progress.getDouble("value.highest_level", 0) == 4);

  // With no user nothing is stored.
  Models none("saved_nouser", false);
  none.values.setValue("total_worth", 9, true);
  CHECK_FALSE(none.progress.has("value.total_worth"));
  CHECK(none.values.getReal("total_worth", 0) == 9);  // held in memory for now
}

TEST_CASE("levels: goals, LevelComplete result and credit, status and best score") {
  Models m("levels");
  LevelManager& lv = m.levels;
  CHECK(lv.goal(1) == 100);
  CHECK(lv.mastery(1) == 160);
  CHECK(lv.goal(4) == 0);  // no row
  CHECK(lv.goal(0) == 0);
  CHECK(lv.goal(61) == 0);
  CHECK(lv.mastery(60) == 9500);

  // The thresholds are inclusive: 100 passes, 99 does not, 160 masters.
  CHECK(lv.levelComplete(1, 99).result == 0);
  CHECK(lv.status(1) == 0);
  CHECK(lv.bestScore(1) == 99);  // a failed run still keeps its best
  auto first = lv.levelComplete(1, 100);
  CHECK(first.result == 1);
  CHECK(first.delta == 100);  // status was 0: the whole score, not the improvement over the failed 99
  CHECK(lv.status(1) == 1);
  CHECK(lv.bestScore(1) == 100);
  // A replay pays only the improvement over the stored best, never negative.
  auto again = lv.levelComplete(1, 130);
  CHECK(again.result == 1);
  CHECK(again.delta == 30);
  CHECK(lv.levelComplete(1, 120).delta == 0);
  CHECK(lv.bestScore(1) == 130);
  // Mastery raises the status and never lowers it.
  auto master = lv.levelComplete(1, 160);
  CHECK(master.result == 2);
  CHECK(master.delta == 30);
  CHECK(lv.status(1) == 2);
  CHECK(lv.levelComplete(1, 100).result == 1);
  CHECK(lv.status(1) == 2);
  // No level object: nothing changes.
  auto none = lv.levelComplete(0, 5000);
  CHECK(none.result == 0);
  CHECK(none.delta == 0);
  CHECK(lv.levelComplete(61, 5000).result == 0);
}

TEST_CASE("levels: SetHighestComplete, IncrementLevel, current level and per-location cash") {
  Models m("highest");
  LevelManager& lv = m.levels;
  CHECK(lv.currentLevel() == 0);
  lv.setCurrentLevel(3);
  CHECK(lv.currentLevel() == 3);
  lv.setCurrentLevel(500);  // clamped to the value's max, 61
  CHECK(lv.currentLevel() == 61);
  lv.setCurrentLevel(3);

  lv.setHighestComplete(4, false);
  CHECK(lv.highestComplete() == 4);
  for (int l = 1; l <= 4; ++l) CHECK(lv.status(l) == 1);  // levels up to it that had no status get one
  CHECK(lv.status(5) == 0);
  lv.levelComplete(2, 170);
  CHECK(lv.status(2) == 2);
  // Without force it only raises.
  lv.setHighestComplete(2, false);
  CHECK(lv.highestComplete() == 4);
  CHECK(lv.status(3) == 1);
  // With force it can lower: levels above lose their status, levels below keep theirs.
  lv.setHighestComplete(2, true);
  CHECK(lv.highestComplete() == 2);
  CHECK(lv.status(2) == 2);
  CHECK(lv.status(3) == 0);
  CHECK(lv.status(4) == 0);
  CHECK(lv.bestScore(2) == 170);  // the best score is not touched
  lv.setHighestComplete(70, true);  // the saved value is clamped to 60
  CHECK(lv.highestComplete() == 60);

  // IncrementLevel needs a status on the current level.
  Models n("increment");
  n.levels.setCurrentLevel(2);
  n.levels.incrementLevel();
  CHECK(n.levels.currentLevel() == 2);
  CHECK(n.levels.highestComplete() == 0);
  n.levels.levelComplete(2, 130);
  n.levels.incrementLevel();
  CHECK(n.levels.currentLevel() == 3);
  CHECK(n.levels.highestComplete() == 2);

  // One cash slot per location: levels 1-10 share one, 11 starts another.
  Models c("cash");
  CHECK(std::string(LevelManager::locationOf(1)) == "iquitos");
  CHECK(std::string(LevelManager::locationOf(10)) == "iquitos");
  CHECK(std::string(LevelManager::locationOf(11)) == "grytviken");
  CHECK(std::string(LevelManager::locationOf(21)) == "namche");
  CHECK(std::string(LevelManager::locationOf(31)) == "krakatoa");
  CHECK(std::string(LevelManager::locationOf(41)) == "kamchatka");
  CHECK(std::string(LevelManager::locationOf(60)) == "gaberoun");
  c.levels.setCurrentLevel(3);
  CHECK(c.levels.currentScore() == 0);
  c.levels.updateCurrentScore(120.5);
  CHECK(c.progress.getDouble("cash.iquitos", -1) == 120.5);
  c.levels.setCurrentLevel(10);
  CHECK(c.levels.currentScore() == 120.5);
  c.levels.setCurrentLevel(11);
  CHECK(c.levels.currentScore() == 0);
  c.levels.updateCurrentScore(7);
  c.levels.setCurrentLevel(1);
  CHECK(c.levels.currentScore() == 120.5);
  CHECK(c.levels.locationCash(11) == 7);
}

TEST_CASE("achievements: file order ordinals, check order, comparison masks and the acquired set") {
  Models m("ach");
  AchievementManager& a = m.achievements;
  CHECK(a.size() == 5);
  REQUIRE(a.find("zed"));  // names are lowercased
  CHECK(a.find("zed")->ordinal == 0);
  CHECK(a.find("b")->ordinal == 1);
  CHECK(a.find("a")->ordinal == 2);
  CHECK(a.find("loose")->ordinal == 4);
  CHECK(a.find("zed")->mask == 12);
  CHECK(a.find("med")->medal == "chase_iquitos");
  CHECK(a.find("loose")->category.empty());

  // total_worth 0 < 100 satisfies the bare achievement; the counters are still 0.
  auto first = a.checkAll("");
  REQUIRE(first.size() == 1);
  CHECK(first[0] == "loose");
  CHECK(a.isAcquired("loose"));
  CHECK(m.progress.getInt("achievement.loose", 0) == 1);
  CHECK(a.checkAll("").empty());  // already acquired: not again

  // Categories in string order (alpha, then zeta), names in string order: a, b, med, then zed.
  m.values.setValue("num_bars", 5, false);
  m.values.setValue("completed_iquitos", 1, false);
  auto order = a.checkAll("");
  REQUIRE(order.size() == 4);
  CHECK(order[0] == "a");
  CHECK(order[1] == "b");
  CHECK(order[2] == "med");
  CHECK(order[3] == "zed");
  CHECK(a.pending().size() == 5);  // the queue keeps acquisition order across calls
  CHECK(a.pending()[0] == "loose");
  CHECK(a.takePending().size() == 5);
  CHECK(a.pending().empty());
  CHECK(a.medalXml() == "<medal name=\"chase_iquitos\" per=\"type\"/>\r\n");

  // A category filter checks only that category; "greaterthan" is strict.
  Models f("ach_filter");
  f.values.setValue("num_bars", 2, false);
  CHECK(f.achievements.checkAll("alpha").empty());  // a needs > 2, b needs >= 5, med needs a completed level
  f.values.setValue("num_bars", 3, false);
  auto got = f.achievements.checkAll("alpha");
  REQUIRE(got.size() == 1);
  CHECK(got[0] == "a");
  CHECK_FALSE(f.achievements.isAcquired("zed"));  // another category: not looked at
  CHECK(f.achievements.checkAll("nonesuch").empty());

  // Reading an unknown value key creates it (GetReal's lazy creation).
  CHECK_FALSE(f.values.exists("totally_new"));

  // Release clears the byte and zeroes the saved value; Acquire sets it.
  CHECK(f.achievements.release("alpha", "a"));
  CHECK_FALSE(f.achievements.isAcquired("a"));
  CHECK(f.values.getReal("num_bars", -1) == 0);
  CHECK(f.achievements.acquire("zeta", "zed"));
  CHECK(f.achievements.isAcquired("zed"));
  CHECK_FALSE(f.achievements.acquire("zeta", "nonesuch"));
  f.achievements.acquireAll();
  CHECK(f.achievements.acquiredNames().size() == 5);
}

TEST_CASE("progress: file round trip with exact doubles, escaping, dirty flag and one write per flush") {
  const std::string dir = scratch("progress");
  auto users = std::make_shared<sdk::UserStore>(dir);
  users->create("Ada");
  Progress p(users);
  p.load();
  CHECK_FALSE(p.dirty());
  CHECK(p.showTutorials());  // on by default

  p.setDouble("value.x", 0.1 + 0.2);
  p.setDouble("value.big", 1e300);
  p.setDouble("value.neg", -1234567.0000000002);
  p.set("name.odd", "tab\there\nnewline % percent");
  p.setInt("level.3", 42);
  p.setShowTutorials(false);
  CHECK(p.dirty());
  CHECK(p.writes() == 0);
  CHECK(p.flush());
  CHECK(p.writes() == 1);
  CHECK_FALSE(p.dirty());
  CHECK(p.flush());  // nothing changed: no second write
  CHECK(p.writes() == 1);
  p.set("level.3", "42");  // the same value again does not dirty
  CHECK_FALSE(p.dirty());

  const std::string file = dir + "/profiles/" + users->folder(0) + "/greatcc-progress.txt";
  CHECK(fs::exists(file));
  CHECK(slurp(file).rfind("greatcc-progress 1\n", 0) == 0);

  Progress q(users);
  q.load();
  CHECK(q.getDouble("value.x", 0) == 0.1 + 0.2);
  CHECK(q.getDouble("value.big", 0) == 1e300);
  CHECK(q.getDouble("value.neg", 0) == -1234567.0000000002);
  CHECK(q.get("name.odd") == "tab\there\nnewline % percent");
  CHECK(q.getInt("level.3", 0) == 42);
  CHECK_FALSE(q.showTutorials());
  CHECK(q.keysWithPrefix("value.").size() == 3);
  CHECK(q.getInt("missing", -5) == -5);
  q.set("bad=key", "x");  // a key with '=' cannot be written
  CHECK_FALSE(q.has("bad=key"));

  // Another user has its own file.
  users->create("Bo");
  Progress r(users);
  r.load();
  CHECK_FALSE(r.has("value.x"));
  CHECK(r.showTutorials());
}

TEST_CASE("progress: a file with a foreign header is kept aside, not overwritten") {
  const std::string dir = scratch("badheader");
  auto users = std::make_shared<sdk::UserStore>(dir);
  users->create("Ada");
  const std::string rel = "profiles/" + users->folder(users->current()) + "/greatcc-progress.txt";
  REQUIRE(users->writeFile(rel, "greatcc-progress 2\nlevel.3=9\n"));
  Progress p(users);
  CHECK_FALSE(p.load());
  CHECK(p.getInt("level.3", 0) == 0);  // defaults
  p.setInt("level.1", 1);
  CHECK(p.flush());
  const auto kept = users->readFile(rel + ".bad");
  REQUIRE(kept);
  CHECK(*kept == "greatcc-progress 2\nlevel.3=9\n");
  CHECK(p.load());
  CHECK(p.getInt("level.1", 0) == 1);
}

TEST_CASE("progress: a world over a scratch folder boots to defaults with no user") {
  StateEnv e(scratch("nouser"));
  REQUIRE(e.booted);
  GreatCCWorld& w = e.world();
  CHECK(e.ctx.users->current() == -1);
  CHECK(w.values().size() > 10);
  CHECK(w.levels().goal(1) == 100);
  CHECK(w.achievements().size() == 5);
  CHECK(w.progress().showTutorials());
  CHECK(e.num("GetCurrentLevel()") == 0);
  CHECK(e.num("GetHighestComplete()") == 0);
  CHECK(e.num("GetCurrentGameMode()") == 0);
  w.values().setValue("total_worth", 5, true);
  CHECK_FALSE(w.progress().dirty());  // nothing is written without a user
  w.tick(16);
  CHECK(w.progress().writes() == 0);
  CHECK(e.num("GetManagedInt('total_worth', 3)") == 5);
}

TEST_CASE("progress: one write per frame when dirty, and the state survives a new world on the same folder") {
  const std::string dir = scratch("frames");
  {
    StateEnv e(dir);
    REQUIRE(e.booted);
    e.run("CreateNewUser('Ada')");
    GreatCCWorld& w = e.world();
    CHECK(e.ctx.users->current() == 0);
    const int base = w.progress().writes();
    w.tick(16);
    CHECK(w.progress().writes() == base);  // nothing changed
    e.run("SetManagedValue('total_worth', 0.1 + 0.2, true)");
    e.run("SetManagedValue('num_bars', 7, true)");
    e.run("SetCurrentLevel(5)");
    e.run("SetManagedValue('plain', 99, false)");
    CHECK(w.progress().writes() == base);  // changes alone do not write
    w.tick(32);
    CHECK(w.progress().writes() == base + 1);  // one write for all of them
    w.tick(48);
    CHECK(w.progress().writes() == base + 1);
    w.levels().levelComplete(1, 120);
    w.levels().updateCurrentScore(55.25);
    w.progress().setShowTutorials(false);
    // Not flushed by a tick: the destructor writes the last frame's changes.
  }
  {
    StateEnv e(dir);
    REQUIRE(e.booted);
    GreatCCWorld& w = e.world();
    CHECK(e.ctx.users->currentName() == "Ada");
    CHECK(w.values().getReal("total_worth", 0) == 0.1 + 0.2);
    CHECK(e.num("GetManagedInt('num_bars', 0)") == 7);
    CHECK(e.num("GetCurrentLevel()") == 5);
    CHECK(e.num("GetManagedInt('plain', 0)") == 10);  // unsaved: the schema value again
    CHECK(w.levels().status(1) == 1);
    CHECK(w.levels().bestScore(1) == 120);
    CHECK(w.levels().currentScore() == 55.25);
    CHECK_FALSE(w.progress().showTutorials());
  }
}

TEST_CASE("users: each user keeps their own progress through switch, create, rename and delete") {
  const std::string dir = scratch("users");
  StateEnv e(dir);
  REQUIRE(e.booted);
  GreatCCWorld& w = e.world();
  e.run("CreateNewUser('Ada')");
  e.run("SetManagedValue('num_bars', 11, true)");
  e.run("SetCurrentLevel(4)");
  w.progress().setShowTutorials(false);
  // No tick in between: the switch itself must write the outgoing user's changes.
  e.run("CreateNewUser('Bo')");
  CHECK(e.num("GetCurrentUser()") == 1);
  CHECK(e.num("GetNumUsers()") == 2);
  CHECK(e.num("GetManagedInt('num_bars', 0)") == 0);  // defaults for the new user
  CHECK(e.num("GetCurrentLevel()") == 0);
  CHECK(w.progress().showTutorials());
  e.run("SetManagedValue('num_bars', 22, true)");
  e.run("SetCurrentLevel(9)");
  e.run("SetCurrentUser(0)");
  CHECK(e.num("GetManagedInt('num_bars', 0)") == 11);
  CHECK(e.num("GetCurrentLevel()") == 4);
  CHECK_FALSE(w.progress().showTutorials());
  e.run("SetCurrentUser(1)");
  CHECK(e.num("GetManagedInt('num_bars', 0)") == 22);
  CHECK(e.num("GetCurrentLevel()") == 9);
  // Runtime values do not follow a user.
  e.run("SetManagedValue('current_worth', 5, false)");
  e.run("SetCurrentUser(0)");
  CHECK(e.num("GetManagedInt('current_worth', 0)") == 0);

  // An index past the end is ignored and keeps the state; -1 selects nobody and shows defaults.
  e.run("SetCurrentUser(7)");
  CHECK(e.num("GetCurrentUser()") == 0);
  CHECK(e.num("GetManagedInt('num_bars', 0)") == 11);
  e.run("SetCurrentUser(-1)");
  CHECK(e.num("GetCurrentUser()") == -1);
  CHECK(e.num("GetManagedInt('num_bars', 0)") == 0);
  e.run("SetCurrentUser(0)");

  // Rename keeps the progress; the file belongs to the folder, not the name.
  e.run("ChangeCurrentUserName('Ada Lovelace')");
  CHECK(e.ctx.users->currentName() == "Ada Lovelace");
  CHECK(e.num("GetManagedInt('num_bars', 0)") == 11);
  e.run("SetCurrentUser(1)");
  e.run("SetCurrentUser(0)");
  CHECK(e.num("GetManagedInt('num_bars', 0)") == 11);

  // Delete the second user: the first is selected and keeps its state; the second's file is gone.
  const std::string bosFolder = e.ctx.users->folder(1);
  e.run("DeleteUser(1)");
  CHECK(e.num("GetNumUsers()") == 1);
  CHECK(e.num("GetCurrentUser()") == 0);
  CHECK(e.num("GetManagedInt('num_bars', 0)") == 11);
  CHECK_FALSE(fs::exists(dir + "/profiles/" + bosFolder));
  // Delete the current (only) user: nobody is selected and the state is the defaults.
  e.run("DeleteUser(0)");
  CHECK(e.num("GetNumUsers()") == 0);
  CHECK(e.num("GetCurrentUser()") == -1);
  CHECK(e.num("GetManagedInt('num_bars', 0)") == 0);
  // A new user afterwards starts clean.
  e.run("CreateNewUser('Cy')");
  CHECK(e.num("GetManagedInt('num_bars', 0)") == 0);
  CHECK(e.num("GetCurrentLevel()") == 0);
}

TEST_CASE("settings: mode and difficulty rules, gamemode is a global pref") {
  const std::string dir = scratch("mode");
  {
    StateEnv e(dir);
    REQUIRE(e.booted);
    CHECK(e.num("GetCurrentGameMode()") == 0);
    e.run("SetCurrentGameMode(4)");  // out of range: ignored
    CHECK(e.num("GetCurrentGameMode()") == 0);
    e.run("SetCurrentGameMode(-1)");
    CHECK(e.num("GetCurrentGameMode()") == 0);
    e.run("SetCurrentDifficulty(3)");
    CHECK(e.num("GetCurrentDifficulty()") == 0);
    // Story mode: the difficulty is stored without moving the mode.
    e.run("SetCurrentDifficulty(2)");
    CHECK(e.num("GetCurrentDifficulty()") == 2);
    CHECK(e.num("GetCurrentGameMode()") == 0);
    // Setting a mode zeroes the difficulty.
    e.run("SetCurrentGameMode(1)");
    CHECK(e.num("GetCurrentGameMode()") == 1);
    CHECK(e.num("GetCurrentDifficulty()") == 0);
    // In an endless mode the difficulty selects mode d + 1.
    e.run("SetCurrentDifficulty(2)");
    CHECK(e.num("GetCurrentGameMode()") == 3);
    CHECK(e.num("GetCurrentDifficulty()") == 2);
    CHECK(e.ctx.users->globalPref("gamemode") == "3");
    // Per user prefs do not carry it, so it survives a user change.
    e.run("CreateNewUser('Ada')");
    CHECK(e.ctx.users->pref("gamemode", "none") == "none");  // nothing is kept per user
    CHECK(e.num("GetCurrentGameMode()") == 3);
    e.run("CreateNewUser('Bo')");
    e.run("SetCurrentUser(0)");
    CHECK(e.num("GetCurrentGameMode()") == 3);
    CHECK(e.world().settings().hiscoreMode() == std::string("greatchocolatechase_endlesshard"));
  }
  {
    StateEnv e(dir);  // a new run reads it back
    REQUIRE(e.booted);
    CHECK(e.num("GetCurrentGameMode()") == 3);
    CHECK(e.num("GetCurrentDifficulty()") == 0);  // the difficulty is not persisted
  }
}

TEST_CASE("settings: flags, level natives, random and rem") {
  StateEnv e(scratch("flags"));
  REQUIRE(e.booted);
  e.run("a_ = AreInsideGame(); b_ = IsGameLoaded(); c_ = AreQuitting()");
  CHECK(e.getString("a_") == "false");
  CHECK(e.getString("b_") == "false");
  CHECK(e.getString("c_") == "false");
  e.run("InsideGame(true); SetQuitting(true)");
  e.run("a_ = AreInsideGame(); c_ = AreQuitting()");
  CHECK(e.getString("a_") == "true");
  CHECK(e.getString("c_") == "true");
  e.world().settings().gameLoaded = true;
  e.run("b_ = IsGameLoaded()");
  CHECK(e.getString("b_") == "true");

  CHECK(e.num("GetGoalScore(1)") == 100);
  CHECK(e.num("GetMasteryScore(1)") == 160);
  CHECK(e.num("GetGoalScore(4)") == 0);
  CHECK(e.num("GetMasteryScore(999)") == 0);
  e.run("SetCurrentLevel(2)");
  CHECK(e.num("GetCurrentLevel()") == 2);
  CHECK(e.num("GetCurrentResult()") == 0);
  e.world().levels().levelComplete(2, 135);
  CHECK(e.num("GetCurrentResult()") == 1);
  e.world().levels().setHighestComplete(2, false);
  CHECK(e.num("GetHighestComplete()") == 2);
  CHECK(e.num("GetLastLevelCompleted()") == 2);
  CHECK(e.num("GetScore()") == 0);

  CHECK(e.num("GetManagedInt('plain', 0)") == 10);
  CHECK(e.num("GetManagedReal('speed', 0)") == 4.0);
  CHECK(e.num("GetManagedUint('plain', 0)") == 10);
  CHECK(e.num("GetManagedInt('never_seen', 6)") == 6);
  e.run("SetManagedValue('clamped', 1000, false)");
  CHECK(e.num("GetManagedInt('clamped', 0)") == 12);

  for (int i = 0; i < 50; ++i) {
    const double r = e.num("random(6)");
    CHECK(r >= 1);
    CHECK(r <= 6);
  }
  CHECK(e.num("random(1)") == 1);
  CHECK(e.num("random(0)") == 1);  // the original divides by zero
  CHECK(e.num("rem(7, 3)") == 1);
  CHECK(e.num("rem(-1, 3)") == 2);
  CHECK(e.num("rem(5, 0)") == 0);

  // Achievements: the native checks and queues; the windows are not built yet.
  e.run("SetManagedValue('num_bars', 3, false)");
  e.run("CheckAllAchievements('alpha', false, 0)");
  CHECK(e.world().achievements().isAcquired("a"));
  CHECK_FALSE(e.world().achievements().isAcquired("b"));
  CHECK(e.world().achievements().pending().size() == 1);
}
