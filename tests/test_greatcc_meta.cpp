// The Great Chocolate Chase, slice 17 (docs/design/greatcc.md section 6): the quests, the diary and the achievement windows. The model
// is tested on synthetic tables and files; the windows on tests/assets/greatcc/meta (a stand-in of rewardnotify.lua) and on tables
// built here.
#include <doctest/doctest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "assets/Content.h"
#include "games/Registry.h"
#include "games/greatcc/GreatCCNatives.h"
#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/core/Compare.h"
#include "games/greatcc/core/QuestManager.h"
#include "games/greatcc/meta/PlotItem.h"
#include "games/greatcc/scene/Effects.h"
#include "games/greatcc/meta/PlotManager.h"
#include "games/greatcc/meta/RewardNotifications.h"
#include "games/greatcc/meta/VPlotWindow.h"
#include "games/greatcc/meta/VQuestWindow.h"
#include "games/greatcc/meta/VRewardNotify.h"
#include "games/greatcc/meta/VRewardsWindow.h"
#include "script/ScriptHost.h"
#include "ScriptHarness.h"
#include "sdk/Users.h"
#include "ui/Style.h"
#include "ui/widgets/TextWidgets.h"

using namespace choco;
using namespace choco::games::greatcc;
namespace fs = std::filesystem;

namespace {

using MS = ui::MapStyle;
using Tbl = std::shared_ptr<MS>;
Tbl tbl() { return std::make_shared<MS>(); }

fs::path scratch(const char* name) {
  const fs::path p = fs::temp_directory_path() / name;
  std::error_code ec;
  fs::remove_all(p, ec);
  fs::create_directories(p, ec);
  return p;
}

// ================================================================ quests

const char* kUpgrades =
    "<upgrades><upgradetype type='station'>"
    "<upgrade name='kMixerQ1' hidden='true'><header>hdr_mq1</header><content>txt_mq1</content></upgrade>"
    "<upgrade name='kIngQ2' hidden='true'><header>hdr_iq2</header><content>txt_iq2</content></upgrade>"
    "<upgrade name='kRevealed' hidden='true'><header>hdr_r</header></upgrade>"
    "</upgradetype></upgrades>";

Tbl quest(const std::string& name, int level, const std::string& compare, const std::string& value, double threshold,
          const std::string& reward, const std::string& type = "", int display = 0) {
  auto q = tbl();
  q->set("name", name).set("minlevel", static_cast<double>(level)).set("maxlevel", static_cast<double>(level));
  q->set("compare", compare).set("compvalue", value).set("threshold", threshold).set("display", static_cast<double>(display));
  q->set("units", "quest_served");
  if (!reward.empty()) q->set("reward", reward);
  if (!type.empty()) q->set("rewardtype", type);
  q->set("rewardcategory", "Station");
  return q;
}

struct QuestFx {
  QuestFx()
      : users(std::make_shared<sdk::UserStore>(scratch("choco_greatcc_meta_store").string())),
        values(progress),
        upgrades(progress),
        quests(progress, values, upgrades, [this](const std::string& k, const std::vector<std::string>& a) { return strings.sdkLookup(k, a); }) {
    users->create("Zed");
    progress.attach(users);
    strings.loadRows({{"quest_complete", "Complete"},
                      {"quest_failed", "Failed"},
                      {"quest_pending", "In Progress"},
                      {"quest_served", "served"},
                      {"quest_title_3", "Customer Appreciation"},
                      {"quest_title_4", "Clean Plates"},
                      {"quest_complete_info", "Done %1%|%2%|%3%"},
                      {"quest_failed_info", "Failed %1%"},
                      {"hdr_mq1", "Mixer"},
                      {"txt_mq1", "A better mixer"},
                      {"hdr_iq2", "Beans"},
                      {"txt_iq2", "Better beans"}});
    REQUIRE(upgrades.loadUpgrades(kUpgrades));
    upgrades.setCurrentLevel("iquitos", 8, std::nullopt, -1, 8);
  }
  void load(std::vector<std::pair<std::string, Tbl>> list) {
    auto root = tbl();
    root->set("kProgressDisplay", 0.0);  // a plain entry among the tables, as in the file
    for (auto& [k, t] : list) root->set(k, t);
    quests.loadFromTable(*root);
  }
  std::shared_ptr<sdk::UserStore> users;
  Progress progress;
  assets::StringTable strings;
  ValueManager values;
  UpgradeManager upgrades;
  QuestManager quests;
};

}  // namespace

TEST_CASE("QuestManager::loadFromTable: every table entry is a quest, keys and rewards lower-cased, the units looked up") {
  QuestFx f;
  f.load({{"Quest8", quest("quest_title_3", 8, ">=", "served_this_level", 16, "kIngQ2")}, {"quest4", quest("quest_title_4", 4, "<", "disposed_this_level", 1, "kMixerQ1")}});
  CHECK(f.quests.size() == 2);
  Quest* q = f.quests.find("quest8");
  REQUIRE(q);
  CHECK(q->key == "quest8");
  CHECK(q->reward == "kingq2");
  CHECK(q->category == "station");
  CHECK(q->rewardType == "activate");  // the default
  CHECK(q->units == "served");
  CHECK(q->threshold == 16.0f);
  CHECK(q->mask == (kCmpGreater | kCmpEqual));
  CHECK(q->minLevel == 8);
  CHECK(f.quests.name(*q) == "Customer Appreciation");
  CHECK(f.quests.find("Quest8") == q);  // a lookup is case-blind
  CHECK(f.quests.find("nothing") == nullptr);
}

TEST_CASE("QuestManager::setCurrentLevel: the active quests are the ones in range that are not yet complete; the text starts empty") {
  QuestFx f;
  f.load({{"quest8", quest("quest_title_3", 8, ">=", "served_this_level", 16, "kIngQ2")}, {"quest4", quest("quest_title_4", 4, "<", "disposed_this_level", 1, "kMixerQ1")}});
  f.quests.setCurrentLevel(8);
  REQUIRE(f.quests.activeQuests().size() == 1);
  CHECK(f.quests.activeQuests()[0]->key == "quest8");
  f.quests.setCurrentLevel(5);
  CHECK(f.quests.activeQuests().empty());
  f.quests.find("quest8")->status = kQuestComplete;
  f.quests.setCurrentLevel(8);
  CHECK(f.quests.activeQuests().empty());  // done quests are not offered again
  f.quests.find("quest8")->status = kQuestFailed;
  f.quests.setCurrentLevel(8);
  CHECK(f.quests.activeQuests().size() == 1);  // a failed one comes back at every replay
}

TEST_CASE("Quest status while the level runs: complete when the comparison holds and the value has reached the threshold, failed when it has reached it and fails") {
  QuestFx f;
  f.load({{"served", quest("quest_title_3", 8, ">=", "served_this_level", 16, "")},
          {"clean", quest("quest_title_4", 8, "<", "angry_this_level", 1, "")}});
  Quest& served = *f.quests.find("served");
  Quest& clean = *f.quests.find("clean");
  f.values.setValue("served_this_level", 15, false);
  f.values.setValue("angry_this_level", 0, false);
  CHECK(f.quests.currentStatus(served) == kQuestPending);
  CHECK(f.quests.currentStatus(clean) == kQuestPending);  // "< 1" holds, but the value has not reached 1
  f.values.setValue("served_this_level", 16, false);
  f.values.setValue("angry_this_level", 1, false);
  CHECK(f.quests.currentStatus(served) == kQuestComplete);
  CHECK(f.quests.currentStatus(clean) == kQuestFailed);  // reached 1 and "< 1" fails
}

TEST_CASE("Quest status text: progress, goal (with currency) and boolean displays, and complete or failed") {
  QuestFx f;
  f.load({{"p", quest("quest_title_3", 8, ">=", "served_this_level", 16, "", "", kProgressDisplay)},
          {"g", quest("quest_title_3", 8, ">=", "current_level_total", 750, "", "", kGoalDisplay)},
          {"b", quest("quest_title_3", 8, "<", "angry_this_level", 1, "", "", kBooleanDisplay)}});
  f.quests.find("g")->format = "currency";
  f.values.setValue("served_this_level", 7, false);
  CHECK(f.quests.statusInfo(*f.quests.find("p")) == "7/16 served");
  CHECK(f.quests.statusInfo(*f.quests.find("g")) == "$750 served");
  f.quests.find("g")->format = "";
  CHECK(f.quests.statusInfo(*f.quests.find("g")) == "750 served");
  CHECK(f.quests.statusInfo(*f.quests.find("b")) == "In Progress");
  f.values.setValue("served_this_level", 16, false);
  CHECK(f.quests.statusInfo(*f.quests.find("p")) == "Complete");
  f.values.setValue("angry_this_level", 2, false);
  CHECK(f.quests.statusInfo(*f.quests.find("b")) == "Failed");
}

TEST_CASE("QuestManager::checkQuest: activate enables the upgrade (saved), reveal shows it, money adds to current_worth") {
  QuestFx f;
  f.load({{"a", quest("quest_title_3", 8, ">=", "served_this_level", 16, "kIngQ2")},
          {"r", quest("quest_title_3", 8, ">=", "served_this_level", 16, "kRevealed", "reveal")},
          {"m", quest("quest_title_3", 8, ">=", "served_this_level", 16, "1000", "money")},
          {"n", quest("quest_title_3", 8, ">=", "served_this_level", 16, "kMixerQ1", "other")}});
  f.values.setValue("served_this_level", 16, false);
  f.values.setValue("current_worth", 50, false);
  REQUIRE(f.upgrades.getUpgrade("kRevealed", "station")->hidden);
  for (const char* k : {"a", "r", "m", "n"}) f.quests.checkQuest(*f.quests.find(k));
  CHECK(f.quests.find("a")->status == kQuestComplete);
  CHECK(f.upgrades.isActive("kIngQ2", "station"));
  CHECK(f.progress.get("upgrade.iquitos.kingq2") == "1");
  CHECK_FALSE(f.upgrades.getUpgrade("kRevealed", "station")->hidden);
  CHECK(f.values.getReal("current_worth", 0) == 1050);
  CHECK_FALSE(f.upgrades.isActive("kMixerQ1", "station"));  // a reward of another type does nothing
  CHECK(f.quests.find("n")->status == kQuestComplete);
}

TEST_CASE("QuestManager::checkQuest: a miss fails the quest and gives nothing; a quest already complete is not looked at again; money must be positive") {
  QuestFx f;
  f.load({{"a", quest("quest_title_3", 8, ">=", "served_this_level", 16, "kIngQ2")},
          {"m", quest("quest_title_3", 8, ">=", "served_this_level", 1, "-5", "money")}});
  f.values.setValue("served_this_level", 3, false);
  f.values.setValue("current_worth", 10, false);
  f.quests.checkQuest(*f.quests.find("a"));
  CHECK(f.quests.find("a")->status == kQuestFailed);
  CHECK_FALSE(f.upgrades.isActive("kIngQ2", "station"));
  f.quests.checkQuest(*f.quests.find("m"));
  CHECK(f.quests.find("m")->status == kQuestComplete);
  CHECK(f.values.getReal("current_worth", 0) == 10);  // -5 is not an award
  f.values.setValue("served_this_level", 0, false);
  f.quests.checkQuest(*f.quests.find("m"));
  CHECK(f.quests.find("m")->status == kQuestComplete);  // not re-checked
  // the retry of a failed quest
  f.values.setValue("served_this_level", 16, false);
  f.quests.checkQuest(*f.quests.find("a"));
  CHECK(f.quests.find("a")->status == kQuestComplete);
}

TEST_CASE("QuestManager::postLevelProcess: the messages of the active quests, one per line, and the statuses saved for the next game") {
  QuestFx f;
  f.load({{"a", quest("quest_title_3", 8, ">=", "served_this_level", 16, "kIngQ2")},
          {"b", quest("quest_title_4", 8, "<", "angry_this_level", 1, "1000", "money")}});
  f.quests.setCurrentLevel(8);
  CHECK(f.quests.postLevelText().empty());
  f.values.setValue("served_this_level", 20, false);
  f.values.setValue("angry_this_level", 4, false);
  const std::string text = f.quests.postLevelProcess();
  CHECK(text == "Done Customer Appreciation|Beans|Better beans\nFailed Clean Plates");
  CHECK(f.quests.postLevelText() == text);
  CHECK(f.progress.get("quest.a") == "1");
  CHECK(f.progress.get("quest.b") == "2");
  // the next game: a is done for good, b is offered again
  QuestManager again(f.progress, f.values, f.upgrades, [&](const std::string& k, const std::vector<std::string>& a) { return f.strings.sdkLookup(k, a); });
  auto root = tbl();
  root->set("a", quest("quest_title_3", 8, ">=", "served_this_level", 16, "kIngQ2"));
  root->set("b", quest("quest_title_4", 8, "<", "angry_this_level", 1, "1000", "money"));
  again.loadFromTable(*root);
  again.setCurrentLevel(8);
  REQUIRE(again.activeQuests().size() == 1);
  CHECK(again.activeQuests()[0]->key == "b");
  // setCurrentLevel empties the text
  f.quests.setCurrentLevel(8);
  CHECK(f.quests.postLevelText().empty());
}

TEST_CASE("QuestManager: a money quest's message names the amount as written, and a reward the upgrade table does not know has no message") {
  QuestFx f;
  f.load({{"m", quest("quest_title_3", 8, ">=", "served_this_level", 1, "1000", "money")},
          {"x", quest("quest_title_4", 8, ">=", "served_this_level", 1, "kNoSuchThing")}});
  f.values.setValue("served_this_level", 5, false);
  f.quests.setCurrentLevel(8);
  const std::string text = f.quests.postLevelProcess();
  CHECK(text == "Done Customer Appreciation|$1000|");  // the second quest completed but says nothing
}

// ================================================================ notification stacking

TEST_CASE("stackedNotificationTop: each notification it overlaps pushes it down by that one's height, the pushes add up, touching counts") {
  const gfx::RectI fresh{525, 0, 275, 150};
  CHECK(stackedNotificationTop(fresh, {}) == 0);
  CHECK(stackedNotificationTop(fresh, {{525, 0, 275, 150}}) == 150);
  CHECK(stackedNotificationTop(fresh, {{525, 0, 275, 150}, {525, 150, 275, 230}}) == 380);
  // one that is too far to the left does not count; the edges do
  CHECK(stackedNotificationTop(fresh, {{100, 0, 200, 150}}) == 0);
  CHECK(stackedNotificationTop(fresh, {{325, 0, 200, 150}}) == 150);  // its right edge is at 525: they touch
  CHECK(stackedNotificationTop(fresh, {{525, 150, 275, 80}}) == 80);  // below, touching at y = 150
  CHECK(stackedNotificationTop(fresh, {{525, 151, 275, 80}}) == 0);
  // a list in an order that stacks only partly: the second is below the first push, so it is no longer reached
  CHECK(stackedNotificationTop(fresh, {{525, 0, 275, 150}, {525, 400, 275, 100}}) == 150);
}

// ================================================================ the windows

namespace {

const char* kAchievements =
    "<achievements><achievementcategory name='adventure'>"
    "<achievement name='served10'><title>t_served10</title><description>d_served10</description><threshold>10</threshold><compare>&gt;=</compare><value>num_served</value></achievement>"
    "<achievement name='bars10'><title>t_bars10</title><description>d_bars10</description><threshold>10</threshold><compare>&gt;=</compare><value>num_bars</value></achievement>"
    "</achievementcategory></achievements>";

struct Env : test::ScriptEnv {
  Env() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/greatcc/meta", scratch("choco_greatcc_meta_user").string()), host(ctx) {
    ctx.module = games::findModule("greatcc");
    ctx.module->attach(ctx);
    booted = host.boot();
    world = worldOf(ctx);
    REQUIRE(booted);
    REQUIRE(world);
    ctx.users = std::make_shared<sdk::UserStore>(scratch("choco_greatcc_meta_user_store").string());
    world->progress().attach(ctx.users);
    ctx.users->create("Zed");
    world->values().load("<values><value name='num_served' value='0' saved='true'/><value name='num_bars' value='0' saved='true'/></values>");
    REQUIRE(world->achievements().load(kAchievements));
    ctx.strings.loadRows({{"t_served10", "Friendly Service"}, {"d_served10", "You have served 10 customers!"}, {"t_bars10", "Bar Baron"},
                          {"d_bars10", "You have made 10 bars."}, {"page_a", "Page A"}, {"page_b", "Page B"}, {"app_ok", "OK"}});
    wm.setNowMs(now);
    wm.pushModal("dialog");
  }
  ~Env() { wm.destroyAll(); }

  void frames(int n, double step = 16) {
    for (int i = 0; i < n; ++i) {
      wm.tickWindowAnimations(now += step);
      wm.clearDestroyed();
      wm.dispatchMessages();
    }
  }
  VRewardNotify* notify(std::size_t i = 0) const {
    const auto& v = world->notices().visible();
    return i < v.size() ? dynamic_cast<VRewardNotify*>(const_cast<Env*>(this)->wm.window(v[i])) : nullptr;
  }
  int queued() {
    lua_State* L = lua.L();
    lua_getglobal(L, "gCommandQueue");
    const int n = lua_istable(L, -1) ? luaL_getn(L, -1) : -1;
    lua_pop(L, 1);
    return n;
  }

  script::ScriptHost host;
  bool booted = false;
  GreatCCWorld* world = nullptr;
  double now = 1000;
};

}  // namespace

TEST_CASE("RewardNotifications::show: the window comes up at the top right with an item per achievement, sized to its text") {
  Env e;
  e.world->achievements().acquire("adventure", "served10");
  e.world->achievements().acquire("adventure", "bars10");
  REQUIRE(e.world->notices().show({"served10", "bars10", "nothing"}, 0));
  VRewardNotify* n = e.notify();
  REQUIRE(n);
  CHECK(n->rect().x == 525);
  CHECK(n->rect().y == 0);
  CHECK(n->itemCount() == 2);  // an unknown name is skipped
  REQUIRE(n->background());
  CHECK(n->rect().h == n->background()->height());
  CHECK(n->background()->height() >= 2 * 55 + 10 + 2 * 40 - 10);  // at least two minimum items, the spacing and the two margins
  CHECK(n->holdMs() == 0);  // a close condition of 0 sets the hold to 0 and waits for HideNotification
  CHECK(e.world->notices().pendingFlags() == 1);
}

TEST_CASE("RewardNotifications::show: an item is at most six, and an empty list shows nothing") {
  Env e;
  CHECK_FALSE(e.world->notices().show({}, 0));
  CHECK(e.world->notices().visible().empty());
  std::vector<std::string> many(8, "served10");
  REQUIRE(e.world->notices().show(many, 0));
  CHECK(e.notify()->itemCount() == 6);
}

TEST_CASE("VRewardNotify: slides in (speeding up, so it is in well before the transition time), holds until HideNotification, slides out and closes") {
  Env e;
  REQUIRE(e.world->notices().show({"served10"}, 0));
  VRewardNotify* n = e.notify();
  REQUIRE(n);
  const auto boxX = [&] { return n->background()->position().x; };
  CHECK(boxX() == 275);  // off the right edge
  e.frames(2);
  CHECK(boxX() < 275);
  CHECK(n->phase() == 0);
  e.frames(10);
  // the step is (time since the phase began) x width / transition, so the box is home in about eight frames, not 500 ms
  CHECK(boxX() == 0);
  e.frames(2);
  CHECK(n->phase() == 1);
  // a flag that is not set: it holds as long as it likes
  e.frames(600);
  CHECK(n->phase() == 1);
  e.world->notices().hide();
  CHECK(e.world->notices().pendingFlags() == 0);
  e.frames(2);
  CHECK(n->phase() == 2);
  const ui::WindowId id = n->id();
  e.frames(3);
  CHECK(boxX() > 0);  // on its way out
  for (int i = 0; i < 40 && e.wm.window(id); ++i) e.frames(1);
  CHECK(e.wm.window(id) == nullptr);
  CHECK(e.world->notices().visible().empty());
}

TEST_CASE("VRewardNotify: a close condition other than 0 is a hold time in milliseconds and makes no flag") {
  Env e;
  REQUIRE(e.world->notices().show({"served10"}, 1000));
  VRewardNotify* n = e.notify();
  REQUIRE(n);
  CHECK(e.world->notices().pendingFlags() == 0);
  CHECK(n->holdMs() == 1000);
  e.frames(20);
  CHECK(n->phase() == 1);
  e.frames(40);  // 640 ms
  CHECK(n->phase() == 1);
  const ui::WindowId id = n->id();
  e.frames(40);  // 1280 ms: past the hold, so it has slid out and gone
  CHECK(e.wm.window(id) == nullptr);
}

TEST_CASE("RewardNotifications: a second notification goes under the first; one that is gone no longer pushes") {
  Env e;
  REQUIRE(e.world->notices().show({"served10"}, 0));
  REQUIRE(e.world->notices().show({"bars10"}, 0));
  VRewardNotify* a = e.notify(0);
  VRewardNotify* b = e.notify(1);
  REQUIRE(a);
  REQUIRE(b);
  CHECK(a->rect().y == 0);
  CHECK(b->rect().y == a->rect().h);
  e.world->notices().hide();
  e.frames(60);
  CHECK(e.world->notices().visible().empty());
  REQUIRE(e.world->notices().show({"served10"}, 0));
  CHECK(e.notify()->rect().y == 0);
}

TEST_CASE("AchievementManager: ShowNewAchievements shows what was acquired and not yet shown, once") {
  Env e;
  e.world->values().setValue("num_served", 12, false);
  e.world->achievements().checkAll("adventure");
  REQUIRE(e.world->achievements().pending().size() == 1);
  CHECK(e.world->notices().showNew(0));
  CHECK(e.world->achievements().pending().empty());
  CHECK(e.notify()->itemCount() == 1);
  CHECK_FALSE(e.world->notices().showNew(0));  // the queue was cleared
  CHECK(e.world->notices().visible().size() == 1);
}

TEST_CASE("The natives ShowNewAchievements, HideNotification and CheckAllAchievements(show) drive the notification windows") {
  Env e;
  e.world->values().setValue("num_served", 12, false);
  REQUIRE(e.run("CheckAllAchievements('adventure', false, 0)") == 0);
  CHECK(e.world->notices().visible().empty());  // not shown
  REQUIRE(e.run("ShowNewAchievements(0)") == 0);
  REQUIRE(e.world->notices().visible().size() == 1);
  REQUIRE(e.run("HideNotification()") == 0);
  CHECK(e.world->notices().pendingFlags() == 0);
  e.world->values().setValue("num_bars", 12, false);
  REQUIRE(e.run("CheckAllAchievements('adventure', true, 5000)") == 0);
  CHECK(e.world->notices().visible().size() == 2);
  CHECK(e.notify(1)->holdMs() == 5000);
}

// ---------------------------------------------------------------- the diary

namespace {

Tbl text(const std::string& label) {
  auto t = tbl();
  t->set("x", 70.0).set("y", 285.0).set("w", 300.0).set("h", 300.0).set("label", label).set("layer", 2.0);
  return t;
}
Tbl page(const std::string& label, const std::string& type = "") {
  auto step = tbl();
  auto texts = tbl();
  texts->set("diary", text(label));
  step->set("text", texts);
  auto item = tbl();
  item->set("nextStep", step);
  if (!type.empty()) item->set("type", type);
  return item;
}
Tbl plotTable(std::vector<std::pair<std::string, Tbl>> items) {
  auto root = tbl();
  auto button = tbl();
  button->set("label", "app_ok").set("x", 585.0).set("y", 500.0);
  root->set("default_button", button);
  root->set("baseDir", "anim/portraits/");
  for (auto& [k, t] : items) root->set(k, t);
  return root;
}

}  // namespace

TEST_CASE("PlotManager::loadFromTable: the story_ entries become items by lower-cased key, anything else is ignored; a quest page is a plain page") {
  Env e;
  PlotManager& pm = e.world->plots();
  pm.loadFromTable(*plotTable({{"Story_Iquitos_1", page("page_a")}, {"story_iquitos_8", page("page_b", "Quest")}, {"other_thing", page("page_a")}}));
  CHECK(pm.itemCount() == 2);
  REQUIRE(pm.item("story_iquitos_1"));
  REQUIRE(pm.item("story_iquitos_8"));
  CHECK(std::string(pm.item("story_iquitos_8")->kind()) == "VQuestItem");
  CHECK(std::string(pm.item("story_iquitos_1")->kind()) == "VPlotItem");
  CHECK(pm.item("other_thing") == nullptr);
  CHECK(pm.item("story_iquitos_8")->plotType() == "Quest");
  CHECK(pm.item("story_iquitos_1")->slot() != pm.item("story_iquitos_8")->slot());
  pm.clear();
  CHECK(pm.itemCount() == 0);
}

TEST_CASE("PlotManager::setCurrentLevel: the level's pages are the active list, each once; IsPlotItemActive and the value modifiers ask it") {
  Env e;
  PlotManager& pm = e.world->plots();
  pm.loadFromTable(*plotTable({{"story_a", page("page_a")}, {"story_b", page("page_b")}}));
  pm.setCurrentLevel({"story_b", "story_a", "story_b"});
  CHECK(pm.activeItems() == std::vector<std::string>{"story_b", "story_a"});
  CHECK(pm.isPlotItemActive("story_a"));
  CHECK_FALSE(pm.isPlotItemActive("story_c"));
  e.world->values().load("<values><value name='v' value='10'><modify plot='story_a' value='0.5'/></value></values>");
  CHECK(e.world->values().getReal("v", 0) == 15);  // the page is active
  pm.setCurrentLevel({"story_b"});
  CHECK(e.world->values().getReal("v", 0) == 10);
}

TEST_CASE("PlotManager::launchPlotDialog: nothing without an active page; with one the dialog is queued on Main") {
  Env e;
  PlotManager& pm = e.world->plots();
  pm.loadFromTable(*plotTable({{"story_a", page("page_a")}}));
  pm.setCurrentLevel({});
  const int before = e.queued();
  CHECK_FALSE(pm.launchPlotDialog());
  CHECK(e.queued() == before);
  pm.setCurrentLevel({"story_a"});
  CHECK(pm.launchPlotDialog());
  CHECK(e.queued() == before + 1);
}

TEST_CASE("PlotItem: the first Advance starts the page (the step fades in over 500 ms); the click on its button completes the item and fades the step out") {
  Env e;
  PlotManager& pm = e.world->plots();
  pm.loadFromTable(*plotTable({{"story_a", page("page_a")}}));
  pm.setCurrentLevel({"story_a"});
  auto item = pm.item("story_a");
  REQUIRE(item);
  REQUIRE(item->firstStep());
  REQUIRE(item->firstStep()->button());
  CHECK_FALSE(item->isComplete());
  CHECK(item->currentStep() == nullptr);
  auto listener = std::make_shared<PlotListener>(e.ctx);
  listener->setPlotItem("story_a");
  item->setListener(listener);
  SpriteManager sm(e.ctx);
  sm.addObject(item);
  CHECK_FALSE(item->advance());
  sm.setNow(1000);
  sm.updateObjects();  // the listener on the button takes effect at the next update
  PlotStep* step = item->currentStep();
  REQUIRE(step);
  CHECK(step == item->firstStep());
  CHECK(step->begun());
  CHECK_FALSE(step->advanced());
  CHECK(step->parent() == item.get());
  CHECK(step->effectCount() == 1);  // the fade in
  // the click on the button goes through the listener to the manager
  step->button()->doEvent(SpriteEvent::Click, {});
  CHECK(step->advanced());
  CHECK(item->isComplete());     // at once, while the step still fades out
  CHECK_FALSE(step->isComplete());
  sm.setNow(1000);
  sm.updateObjects();
  for (double t = 1016; t < 1700; t += 16) {
    sm.setNow(t);
    sm.updateObjects();
  }
  CHECK(step->isComplete());
  CHECK(step->parent() == nullptr);  // the faded step left the item
  // nothing was saved: the original never writes the completed flag
  for (const std::string& k : e.world->progress().keysWithPrefix("plot")) FAIL("a plot key was written: " << k);
  sm.removeObject(*item);
}

TEST_CASE("PlotItem::chooseResponse and Advance after the item is complete: Advance is false, a response keeps it complete") {
  Env e;
  PlotManager& pm = e.world->plots();
  pm.loadFromTable(*plotTable({{"story_a", page("page_a")}}));
  pm.setCurrentLevel({"story_a"});
  auto item = pm.item("story_a");
  item->advance();
  CHECK(item->chooseResponse(""));
  CHECK(item->isComplete());
  CHECK_FALSE(item->advance());
  CHECK(item->chooseResponse("x"));
  // an item with no step at all is complete at the first response
  auto empty = PlotItem::create(e.ctx, "none", 0);
  CHECK_FALSE(empty->isComplete());
  CHECK(empty->chooseResponse(""));
}

TEST_CASE("VPlotWindow: the pages of the level one after the other; the dialog closes when the last is done, once") {
  Env e;
  PlotManager& pm = e.world->plots();
  pm.loadFromTable(*plotTable({{"story_a", page("page_a")}, {"story_b", page("page_b")}}));
  pm.setCurrentLevel({"story_a", "story_b"});
  MS style;
  style.set("x", 0.0).set("y", 0.0).set("w", 800.0).set("h", 600.0);
  auto* w = static_cast<VPlotWindow*>(&e.wm.initWindow(std::make_unique<VPlotWindow>(e.wm, e.ctx), style));
  w->setName("plot_window");
  const int before = e.queued();
  w->postChildrenInit(style);
  e.wm.popCreation();
  REQUIRE(w->current());
  CHECK(w->current()->name() == "story_a");
  CHECK_FALSE(w->closed());
  CHECK(e.world->gameStates().get("DiaryTime_" + std::to_string(e.world->gameStates().playCounter())) == "1");
  e.frames(5);
  CHECK(w->current()->name() == "story_a");  // the page waits for its click
  w->current()->currentStep()->button()->doEvent(SpriteEvent::Click, {});
  e.frames(2);
  REQUIRE(w->current());
  CHECK(w->current()->name() == "story_b");  // the next page replaced it
  CHECK_FALSE(w->closed());
  w->current()->currentStep()->button()->doEvent(SpriteEvent::Click, {});
  e.frames(2);
  CHECK(w->closed());
  CHECK(e.queued() == before + 1);  // the bounce back and the pop were queued once
  e.frames(5);
  CHECK(e.queued() == before + 1);
  CHECK(e.world->gameStates().get("DiaryTime_" + std::to_string(e.world->gameStates().playCounter())) == "0");
}

TEST_CASE("VPlotWindow: with no active page the dialog closes at once; a page the table lacks ends the sequence") {
  Env e;
  PlotManager& pm = e.world->plots();
  pm.loadFromTable(*plotTable({{"story_a", page("page_a")}}));
  MS style;
  style.set("x", 0.0).set("y", 0.0).set("w", 800.0).set("h", 600.0);
  {
    pm.setCurrentLevel({});
    auto* w = static_cast<VPlotWindow*>(&e.wm.initWindow(std::make_unique<VPlotWindow>(e.wm, e.ctx), style));
    w->postChildrenInit(style);
    e.wm.popCreation();
    CHECK(w->closed());
    CHECK(w->current() == nullptr);
  }
  {
    pm.setCurrentLevel({"story_missing", "story_a"});
    auto* w = static_cast<VPlotWindow*>(&e.wm.initWindow(std::make_unique<VPlotWindow>(e.wm, e.ctx), style));
    w->postChildrenInit(style);
    e.wm.popCreation();
    CHECK(w->closed());  // the original gives up at the first name that has no page
    CHECK(w->current() == nullptr);
  }
}

// ---------------------------------------------------------------- the quest panel

TEST_CASE("VQuestWindow: shown only while a quest is active, one coloured line each, the live status in the colour of its state; the background ends under the text") {
  Env e;
  e.ctx.strings.loadRows({{"quest_title_3", "Customer Appreciation"}, {"quest_served", "served"}, {"quest_complete", "Complete"}, {"quest_failed", "Failed"}});
  auto root = tbl();
  root->set("quest8", quest("quest_title_3", 8, ">=", "served_this_level", 16, ""));
  e.world->quests().loadFromTable(*root);
  MS style;
  style.set("x", 0.0).set("y", 0.0).set("w", 245.0).set("h", 250.0).set("marginY", 15.0);
  auto colors = tbl();
  for (auto c : {std::vector<double>{1, 1, 0}, {0, 1, 0}, {1, 0, 0}}) {
    auto one = tbl();
    for (double v : c) one->append(v);
    colors->append(one);
  }
  style.set("colors", colors);
  auto* w = static_cast<VQuestWindow*>(&e.wm.initWindow(std::make_unique<VQuestWindow>(e.wm, e.ctx), style));
  w->setName("quest_window");
  // its children: the two texts and the background
  MS child;
  child.set("x", 10.0).set("y", 18.0).set("w", 218.0).set("h", 230.0);
  ui::Window& names = e.wm.initWindow(std::make_unique<ui::TextWindow>(e.wm), child);
  names.setName("txt_quest_names");
  e.wm.popCreation();
  ui::Window& status = e.wm.initWindow(std::make_unique<ui::TextWindow>(e.wm), child);
  status.setName("txt_quest_status");
  e.wm.popCreation();
  ui::Window& bg = e.wm.initWindow(std::make_unique<ui::Window>(e.wm), child);
  bg.setName("background");
  bg.setPos(0, -290);
  e.wm.popCreation();
  w->postChildrenInit(style);
  e.wm.popCreation();
  // no quest active: hidden
  e.world->quests().setCurrentLevel(3);
  w->levelInit();
  CHECK_FALSE(w->questsShown());
  CHECK_FALSE(w->shown());
  // the quest is active
  e.world->quests().setCurrentLevel(8);
  e.world->values().setValue("served_this_level", 7, false);
  w->levelInit();
  CHECK(w->questsShown());
  CHECK(w->shown());
  CHECK(names.label() == "<font color='ffff00'>Customer Appreciation</font>\n");
  CHECK(status.label() == "<font color='ffff00'>7/16 served</font>\n");
  // the panel's bottom is just under the text: the background's top is (text height + the text's top + marginY) - the window's height
  CHECK(bg.rect().y < 0);
  CHECK(bg.rect().y > -250);
  // the live status changes the colour, on the next 300 ms update
  e.world->values().setValue("served_this_level", 16, false);
  e.frames(1, 301);
  CHECK(names.label() == "<font color='00ff00'>Customer Appreciation</font>\n");
  CHECK(status.label() == "<font color='00ff00'>Complete</font>\n");
}

// ---------------------------------------------------------------- the rewards window

namespace {

Tbl rewardEntry(const std::string& name, double boxX, double boxY) {
  auto r = tbl();
  r->set("name", name).set("boxX", boxX).set("boxY", boxY).set("category", "adventure");
  return r;
}

Tbl rewardsTable() {
  auto root = tbl();
  auto box = tbl();
  box->set("w", 200.0).set("h", 85.0).set("x1", 30.0).set("y1", 32.0).set("x2", 37.0).set("y2", 40.0).set("mouse", 0.0);
  root->set("box", box);
  root->set("headerStyle", tbl());
  root->set("contentStyle", tbl());
  root->set("reward_served10", rewardEntry("served10", 536, 100));
  root->set("reward_bars10", rewardEntry("bars10", 575, 150));
  root->set("not_a_reward", rewardEntry("bars10", 0, 0));
  return root;
}

}  // namespace

TEST_CASE("VRewardsWindow: a VReward per reward_ entry, the ones not earned drawn at alpha 0.2 tinted dark; the lid moves on RewardWindowReady after a second") {
  Env e;
  e.world->achievements().acquire("adventure", "served10");
  auto style = tbl();
  style->set("x", 0.0).set("y", 0.0).set("w", 800.0).set("h", 600.0);
  auto lid = tbl();
  lid->set("x", 0.0).set("y", 0.0).set("spriteLayer", 100.0);
  style->set("box_top", lid);
  style->set("rewards", rewardsTable());
  auto* w = static_cast<VRewardsWindow*>(&e.wm.initWindow(std::make_unique<VRewardsWindow>(e.wm, e.ctx), *style));
  w->postChildrenInit(*style);
  e.wm.popCreation();
  REQUIRE(w->rewards().size() == 2);  // `not_a_reward` is not one
  VReward* earned = w->rewards().at("reward_served10").get();
  VReward* grey = w->rewards().at("reward_bars10").get();
  CHECK(earned->visible());
  CHECK(earned->alpha() == 1.0f);
  CHECK(earned->tint().r == 1.0f);
  CHECK(grey->visible());
  CHECK(grey->alpha() == doctest::Approx(0.2f));
  CHECK(grey->tint().r == doctest::Approx(0.2f));
  CHECK(grey->category() == "adventure");
  REQUIRE(grey->box());
  CHECK_FALSE(grey->boxVisible());
  CHECK(grey->box()->position().x == 575);
  CHECK(grey->box()->position().y == 150);
  REQUIRE(w->lid());
  // RewardWindowReady is bound while the window lives
  REQUIRE(e.run("RewardWindowReady()") == 0);
  REQUIRE(w->lid()->effectCount() == 1);
  auto* move = dynamic_cast<SpriteMove*>(w->lid()->effectList()[0].get());
  REQUIRE(move);
  for (int i = 0; i < 40; ++i) e.wm.tickWindowAnimations(e.now += 16);  // 640 ms
  CHECK(move->eased() == 0.0f);  // the one-second delay has not passed
  for (int i = 0; i < 80; ++i) e.wm.tickWindowAnimations(e.now += 16);
  CHECK(move->eased() == 1.0f);
  // the native goes with the window
  e.wm.destroyWindow(*w);
  e.wm.clearDestroyed();
  lua_getglobal(e.lua.L(), "RewardWindowReady");
  CHECK(lua_isnil(e.lua.L(), -1));
  lua_pop(e.lua.L(), 1);
}

TEST_CASE("VReward: the pointer shows the box (fading in), leaving fades it out in 100 ms and then hides it") {
  Env e;
  auto style = tbl();
  style->set("x", 0.0).set("y", 0.0).set("w", 800.0).set("h", 600.0);
  style->set("rewards", rewardsTable());
  auto* w = static_cast<VRewardsWindow*>(&e.wm.initWindow(std::make_unique<VRewardsWindow>(e.wm, e.ctx), *style));
  w->postChildrenInit(*style);
  e.wm.popCreation();
  VReward* r = w->rewards().at("reward_served10").get();
  REQUIRE(r->box());
  auto fade = [&]() -> AlphaEffect* {
    const auto list = r->box()->effectList();
    return list.empty() ? nullptr : dynamic_cast<AlphaEffect*>(list.back().get());
  };
  r->onMouseEnter();
  CHECK(r->boxVisible());
  REQUIRE(fade());
  CHECK(fade()->fadeIn());
  CHECK(fade()->alpha() == 0.0f);  // it starts from nothing
  e.frames(10);
  CHECK(fade()->alpha() > 0.4f);
  e.frames(30);
  CHECK(fade()->alpha() == 1.0f);
  CHECK(r->boxVisible());  // the box stays while the pointer does
  r->box()->setAlpha(1.0f);  // the fade in writes this alpha when the box is drawn; nothing draws here
  r->onMouseLeave();
  REQUIRE(fade());
  CHECK_FALSE(fade()->fadeIn());
  e.frames(3);
  CHECK(r->boxVisible());
  e.frames(10);
  CHECK_FALSE(r->boxVisible());  // 100 ms later it is hidden by the update
  // entering again while it fades out takes the fade out away
  r->onMouseEnter();
  r->onMouseLeave();
  r->onMouseEnter();
  CHECK(r->boxVisible());
  REQUIRE(fade());
  CHECK(fade()->fadeIn());
  CHECK(r->box()->effectCount() == 1);
  e.frames(40);
  CHECK(r->boxVisible());
}
