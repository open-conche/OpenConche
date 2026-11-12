// The Great Chocolate Chase, slice 16 (docs/design/greatcc.md section 6): the upgrade manager and the pre-level shop windows. The model
// is tested with synthetic upgrade files; the windows with tests/assets/greatcc/upgrades (stand-ins of prelevel.lua and upgrade.lua) on
// the module's own window types.
#include <doctest/doctest.h>

#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "games/Registry.h"
#include "games/greatcc/GreatCCNatives.h"
#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/core/UpgradeManager.h"
#include "games/greatcc/ui/RDisableButton.h"
#include "games/greatcc/upgrade/VPrelevelWindow.h"
#include "games/greatcc/upgrade/VUpgradeList.h"
#include "games/greatcc/upgrade/VUpgradeTab.h"
#include "script/RunFile.h"
#include "script/ScriptHost.h"
#include "ScriptHarness.h"
#include "sdk/Users.h"

using namespace choco;
using namespace choco::games::greatcc;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const char* name) {
  const fs::path p = fs::temp_directory_path() / name;
  std::error_code ec;
  fs::remove_all(p, ec);
  fs::create_directories(p, ec);
  return p;
}

// A progress file with a current player.
struct Saved {
  explicit Saved(const char* name = "choco_greatcc_upgrades_store") : users(std::make_shared<sdk::UserStore>(scratch(name).string())) {
    users->create("Zed");
    progress.attach(users);
  }
  std::shared_ptr<sdk::UserStore> users;
  Progress progress;
};

const char* kXml =
    "<upgrades>"
    "<upgradetype type='Aesthetic'>"
    "<upgrade name='kFloor1' hidden='False'><minlevel>4</minlevel><maxlevel>40</maxlevel><cost>100</cost><image>img/floor</image>"
    "<header>hdr_floor</header><content>txt_floor</content><object name='bg' effect='false'/><object name='tab1' recursive='false'/></upgrade>"
    "<upgrade name='kFloor2'><minlevel>7</minlevel><cost>250.5</cost><requires>kFloor1</requires></upgrade>"
    "<upgrade name='kHideMe' hidden='true'><enabled>6</enabled></upgrade>"
    "</upgradetype>"
    "<upgradetype type='station'>"
    "<upgrade name='kMixer1' hidden='true'><enabled>1</enabled></upgrade>"
    "<upgrade name='kMixer2' hidden='true'><enabled>4</enabled></upgrade>"
    "<upgrade name='kSauce2'><minlevel>1</minlevel><cost>150</cost></upgrade>"
    "<upgrade name='kLater'><enabled>1</enabled><requires>kZ</requires></upgrade>"
    "<upgrade name='kZ'><enabled>1</enabled></upgrade>"
    "<upgrade name=''><cost>1</cost></upgrade>"
    "</upgradetype>"
    "<upgradetype type='gameplay'>"
    "<upgrade name='kMove1'><minlevel>1</minlevel><cost>100</cost></upgrade>"
    "<upgrade name='kMove2'><minlevel>1</minlevel><cost>300</cost><requires>kMove1</requires></upgrade>"
    "</upgradetype>"
    "</upgrades>";

struct Sprites : UpgradeSprites {
  std::set<std::string> present{"bg", "tab1"};
  std::vector<std::string> calls;
  bool exists(const std::string& n) override { return present.count(n) != 0; }
  void init(const std::string& n) override { calls.push_back("init " + n); }
  void switchAsset(const std::string& n, const std::string& a, const std::string& s) override { calls.push_back("asset " + n + " " + a + " " + s); }
  void switchState(const std::string& n, const std::string& s) override { calls.push_back("state " + n + " " + s); }
};

struct Spider : UpgradeSpider {
  std::vector<std::string> seen;
  void process(const Upgrade& u, const BoundObject& b) override {
    seen.push_back(u.name + ">" + b.name + (b.recursive ? "+" : "-") + (u.isActive() ? " on" : " off"));
  }
};

}  // namespace

// ================================================================ the model

TEST_CASE("Upgrade: the states 0-4 and what the predicates say of them") {
  Upgrade u;
  CHECK(u.state == 0);
  CHECK_FALSE(u.isActive());
  CHECK_FALSE(u.isAvailable());
  u.setState(kUpgradeUnlocked);
  CHECK(u.isAvailable());
  CHECK_FALSE(u.isActive());
  u.setState(kUpgradeSelected);
  CHECK(u.isAvailable());  // 2 is available and not active
  CHECK_FALSE(u.isActive());
  u.setState(kUpgradeOwned);
  CHECK(u.isActive());
  CHECK_FALSE(u.isAvailable());  // owned is not available to buy
  u.setState(kUpgradeUnlocked);
  u.setState(kUpgradePreview);
  CHECK(u.isActive());     // the preview counts as on
  CHECK(u.isAvailable());  // and as available
  CHECK(u.previousState == kUpgradeUnlocked);
  u.setState(kUpgradeLocked);  // leaving 4 does not remember it
  CHECK(u.previousState == kUpgradeUnlocked);
  u.setState(kUpgradeOwned);
  CHECK(u.previousState == kUpgradeLocked);
  u.minLevel = 3;
  u.maxLevel = 5;
  CHECK_FALSE(u.inRange(2));
  CHECK(u.inRange(3));
  CHECK(u.inRange(5));
  CHECK_FALSE(u.inRange(6));
}

TEST_CASE("UpgradeManager::loadUpgrades: names lower case, an index in file order across the types, attributes, requires and bound objects") {
  Saved s;
  UpgradeManager m(s.progress);
  REQUIRE(m.loadUpgrades(kXml));
  CHECK(m.size() == 10);  // the one with no name is skipped
  const Upgrade* f1 = m.getUpgrade("KFLOOR1", "AESTHETIC");  // both names are lower-cased
  REQUIRE(f1);
  CHECK(f1->name == "kfloor1");
  CHECK(f1->category == "aesthetic");
  CHECK(f1->index == 0);
  CHECK(f1->minLevel == 4);
  CHECK(f1->maxLevel == 40);
  CHECK(f1->cost == 100.0f);
  CHECK(f1->enabled == 0x7fffffff);
  CHECK_FALSE(f1->hidden);  // compared with "true" after lower-casing
  CHECK(f1->attribute("image") == "img/floor");
  CHECK(f1->attribute("header") == "hdr_floor");
  CHECK(f1->attribute("nothing") == "");
  REQUIRE(f1->bound.size() == 2);
  CHECK_FALSE(f1->bound.at("bg").effect);
  CHECK(f1->bound.at("bg").recursive);
  CHECK(f1->bound.at("tab1").effect);
  CHECK_FALSE(f1->bound.at("tab1").recursive);
  CHECK(m.getUpgrade("kFloor2", "aesthetic")->cost == 250.5f);
  CHECK(m.getUpgrade("kFloor2", "aesthetic")->requires_ == std::set<std::string>{"kfloor1"});
  CHECK(m.getUpgrade("kHideMe", "aesthetic")->hidden);
  CHECK(m.getUpgrade("kHideMe", "aesthetic")->index == 2);  // hidden ones count
  CHECK(m.getUpgrade("kMixer1", "station")->index == 3);
  CHECK(m.getUpgrade("kMove2", "gameplay")->index == 9);
  CHECK(m.getUpgrade("kMixer1", "aesthetic") == nullptr);
  CHECK(m.getUpgrade("kNothing", "station") == nullptr);
  CHECK(m.typeMap("nothing").empty());
  CHECK(m.typeMap("STATION").size() == 5);
  // a text that does not parse leaves none
  CHECK_FALSE(m.loadUpgrades("<upgrades><upgradetype"));
  CHECK(m.loadFailed());
  CHECK(m.size() == 0);
  // an <upgrade> straight under <upgrades> has the category ""
  REQUIRE(m.loadUpgrades("<upgrades><upgrade name='kA'/></upgrades>"));
  CHECK_FALSE(m.loadFailed());
  CHECK(m.getUpgrade("kA", "") != nullptr);
}

TEST_CASE("UpgradeManager::setCurrentLevel: the grant by level, the unlock by range, in category then name order, one pass") {
  Saved s;
  UpgradeManager m(s.progress);
  m.setCurrentLevel("iquitos", 1, std::string_view(kXml), -1, 1);
  // level 1: kMixer1 and kZ are granted (state 3); kLater requires kZ, which sorts after it, so it has to wait for the next call
  CHECK(m.isActive("kMixer1", "station"));
  CHECK(m.isActive("kZ", "station"));
  CHECK_FALSE(m.isActive("kLater", "station"));
  CHECK_FALSE(m.isActive("kMixer2", "station"));  // enabled at 4
  CHECK(m.getUpgrade("kLater", "station")->state == kUpgradeLocked);
  // in range and not granted: unlocked (state 1); out of range stays 0
  CHECK(m.getUpgrade("kSauce2", "station")->state == kUpgradeUnlocked);
  CHECK(m.getUpgrade("kMove1", "gameplay")->state == kUpgradeUnlocked);
  CHECK(m.getUpgrade("kFloor1", "aesthetic")->state == kUpgradeLocked);  // minlevel 4
  // the requirement is not met for these: nothing happens to them
  CHECK(m.getUpgrade("kMove2", "gameplay")->state == kUpgradeLocked);
  CHECK(m.getUpgrade("kFloor2", "aesthetic")->state == kUpgradeLocked);
  // the next call (the file is not read again) completes the chain
  m.setCurrentLevel(1, std::nullopt, -1, 1);
  CHECK(m.isActive("kLater", "station"));
  // level 4: the second mix is granted, the first floor is on offer, its requirement keeps the second one locked
  m.setCurrentLevel(4, std::nullopt, -1, 4);
  CHECK(m.isActive("kMixer2", "station"));
  CHECK(m.getUpgrade("kFloor1", "aesthetic")->state == kUpgradeUnlocked);
  CHECK(m.getUpgrade("kFloor2", "aesthetic")->state == kUpgradeLocked);
  // the purchases of earlier levels stay, and a level that is out of the range of the upgrade locks it again
  CHECK(m.enableUpgrade("kFloor1", "aesthetic", true));
  m.setCurrentLevel(7, std::nullopt, -1, 7);
  CHECK(m.getUpgrade("kFloor2", "aesthetic")->state == kUpgradeUnlocked);  // requires met, minlevel 7 reached
  CHECK(m.isActive("kHideMe", "aesthetic"));                               // enabled at 6
  // p5 limits the grant by the player's own level, p4 the unlock: a call for level 9 with p5 = 5 grants only what is enabled at 5 or less
  Saved t("choco_greatcc_upgrades_store2");
  UpgradeManager n(t.progress);
  n.setCurrentLevel("iquitos", 9, std::string_view(kXml), 2, 5);
  CHECK(n.isActive("kMixer2", "station"));       // 4 <= 5 and 4 <= 9
  CHECK_FALSE(n.isActive("kHideMe", "aesthetic"));  // 6 > 5
  CHECK(n.getUpgrade("kHideMe", "aesthetic")->state == kUpgradeUnlocked);  // it is unlocked instead: minlevel 0 <= p4 and the level 9 is in range
}

TEST_CASE("UpgradeManager::setCurrentLevel with a level: the location names the saved block, the level's own grants are not saved, endless saves nothing") {
  Saved s;
  UpgradeManager m(s.progress);
  m.setCurrentLevel("namche", 1, std::string_view(kXml), -1, 1, true, {{"kFloor2", "Aesthetic"}, {"kUnknown", "station"}});
  CHECK(m.location() == "namche");
  CHECK(m.isActive("kFloor2", "aesthetic"));  // the level's own grant, whatever its requirements
  CHECK(s.progress.get("upgrade.namche.kmixer1") == "1");   // the grant by level is saved
  CHECK(s.progress.get("upgrade.namche.kfloor2") == "");    // the level's own grant is not
  CHECK(s.progress.get("upgrade.iquitos.kmixer1") == "");   // per location
  // without the flag the list is not applied
  UpgradeManager n(s.progress);
  n.setCurrentLevel("namche", 1, std::string_view(kXml), -1, 1, false, {{"kFloor2", "aesthetic"}});
  CHECK_FALSE(n.isActive("kFloor2", "aesthetic"));
  // endless: no save and no load
  Saved t("choco_greatcc_upgrades_store3");
  t.progress.setInt("upgrade.namche.ksauce2", 1);
  UpgradeManager e(t.progress);
  e.setNoSave(true);
  e.setCurrentLevel("namche", 1, std::string_view(kXml), -1, 1);
  CHECK_FALSE(e.isActive("kSauce2", "station"));  // the saved purchase is not read
  CHECK(e.isActive("kMixer1", "station"));        // the level still grants
  CHECK(t.progress.get("upgrade.namche.kmixer1") == "");
  e.enableUpgrade("kSauce2", "station", true);
  CHECK(e.isActive("kSauce2", "station"));
  CHECK(t.progress.getInt("upgrade.namche.ksauce2", 0) == 1);  // untouched: the key was there already, and nothing was written
}

TEST_CASE("UpgradeManager: what the player owns is kept per location and per player, and read back in range") {
  Saved s;
  {
    UpgradeManager m(s.progress);
    m.setCurrentLevel("iquitos", 4, std::string_view(kXml), -1, 4);
    REQUIRE(m.getUpgrade("kFloor1", "aesthetic")->state == kUpgradeUnlocked);
    CHECK(m.enableUpgrade("kFloor1", "aesthetic", true));
    CHECK(m.enableUpgrade("kMove1", "gameplay", true));
    CHECK(s.progress.get("upgrade.iquitos.kfloor1") == "1");
    CHECK(s.progress.get("upgrade.iquitos.kmove1") == "1");
    CHECK_FALSE(m.enableUpgrade("kNothing", "gameplay", true));
  }
  // a fresh manager, as after a restart: the purchases are back before the level grants run
  {
    UpgradeManager m(s.progress);
    m.setCurrentLevel("iquitos", 4, std::string_view(kXml), -1, 4);
    CHECK(m.isActive("kFloor1", "aesthetic"));
    CHECK(m.isActive("kMove1", "gameplay"));
    CHECK(m.getUpgrade("kMove2", "gameplay")->state == kUpgradeUnlocked);  // its requirement is owned now
    CHECK(m.getUpgrade("kFloor1", "aesthetic")->state == kUpgradeOwned);
  }
  // another location has its own: nothing is owned there
  {
    UpgradeManager m(s.progress);
    m.setCurrentLevel("namche", 4, std::string_view(kXml), -1, 4);
    CHECK_FALSE(m.isActive("kFloor1", "aesthetic"));
    CHECK_FALSE(m.isActive("kMove1", "gameplay"));
  }
  // kFloor1 is in range 4..40: out of range, the saved flag is not read
  {
    UpgradeManager m(s.progress);
    m.setCurrentLevel("iquitos", 41, std::string_view(kXml), -1, 41);
    CHECK_FALSE(m.isActive("kFloor1", "aesthetic"));
    CHECK(m.isActive("kMove1", "gameplay"));
  }
  // the progress file written: another player starts with nothing
  s.progress.flush();
  s.users->create("Other");
  Progress other;
  other.attach(s.users);
  other.load();
  UpgradeManager m(other);
  m.setCurrentLevel("iquitos", 4, std::string_view(kXml), -1, 4);
  CHECK_FALSE(m.isActive("kFloor1", "aesthetic"));
}

TEST_CASE("UpgradeManager: the temporary preview is state 4, not saved, undone by DisableTempUpgrades with its filters; requirements gate IsAvailable") {
  Saved s;
  UpgradeManager m(s.progress);
  m.setCurrentLevel("iquitos", 4, std::string_view(kXml), -1, 4);
  CHECK(m.isAvailable("kFloor1", "aesthetic"));
  CHECK_FALSE(m.isAvailable("kFloor2", "aesthetic"));  // locked: state 0
  CHECK_FALSE(m.isAvailable("kMixer1", "station"));    // owned is not available to buy
  CHECK_FALSE(m.isAvailable("kNothing", "station"));
  m.enableTempUpgrade("kFloor2", "aesthetic");  // not available: nothing
  CHECK(m.previewCount() == 0);
  m.enableTempUpgrade("kFloor1", "aesthetic");
  m.enableTempUpgrade("kSauce2", "station");
  CHECK(m.previewCount() == 2);
  CHECK(m.getUpgrade("kFloor1", "aesthetic")->state == kUpgradePreview);
  CHECK(m.isActive("kFloor1", "aesthetic"));
  CHECK(s.progress.get("upgrade.iquitos.kfloor1") == "");  // nothing is written
  // requirement: kFloor2 becomes available when kFloor1 is the preview too (active), once its own state is unlocked
  m.getUpgrade("kFloor2", "aesthetic")->setState(kUpgradeUnlocked);
  CHECK(m.isAvailable("kFloor2", "aesthetic"));
  m.disableTempUpgrades("", "station");  // by category
  CHECK(m.previewCount() == 1);
  CHECK(m.getUpgrade("kSauce2", "station")->state == kUpgradeUnlocked);
  CHECK(m.isActive("kFloor1", "aesthetic"));
  CHECK(m.isAvailable("kFloor2", "aesthetic"));  // kFloor1 is still a preview, and a preview is active
  m.disableTempUpgrades("KFLOOR1", "");  // by name, any case
  CHECK(m.previewCount() == 0);
  CHECK(m.getUpgrade("kFloor1", "aesthetic")->state == kUpgradeUnlocked);
  CHECK_FALSE(m.isAvailable("kFloor2", "aesthetic"));  // its requirement is not active any more
  // buying a previewed upgrade takes it out of the set
  m.enableTempUpgrade("kFloor1", "aesthetic");
  CHECK(m.enableUpgrade("kFloor1", "aesthetic", true));
  CHECK(m.previewCount() == 0);
  CHECK(m.getUpgrade("kFloor1", "aesthetic")->state == kUpgradeOwned);
  m.disableTempUpgrades("", "");
  CHECK(m.getUpgrade("kFloor1", "aesthetic")->state == kUpgradeOwned);
}

TEST_CASE("UpgradeManager: how many offers a category has left, and RevealUpgrade") {
  Saved s;
  UpgradeManager m(s.progress);
  m.setCurrentLevel("iquitos", 4, std::string_view(kXml), -1, 4);
  CHECK(m.visibleOfType("aesthetic") == 2);  // kFloor1 and kFloor2; kHideMe is hidden
  CHECK(m.visibleOfType("station") == 2);    // kSauce2 and kLater (which waits for the next pass); the mixes are hidden, kZ is owned
  CHECK(m.visibleOfType("gameplay") == 2);
  CHECK(m.visibleOfType("nothing") == 0);
  m.enableUpgrade("kFloor1", "aesthetic", false);
  CHECK(m.visibleOfType("aesthetic") == 1);  // owned ones are not counted
  m.revealUpgrade("kHideMe", "aesthetic");
  CHECK(m.visibleOfType("aesthetic") == 2);  // kHideMe (unlocked, not owned before level 6) shows once it is revealed
}

TEST_CASE("UpgradeManager: the sprites an upgrade is bound to are looked for once, re-initialised when it changes either way, and the spider is told") {
  Saved s;
  UpgradeManager m(s.progress);
  auto sprites = std::make_shared<Sprites>();
  m.setSprites(sprites);
  m.setCurrentLevel("iquitos", 4, std::string_view(kXml), -1, 4);
  CHECK(sprites->calls.empty());  // nothing was resolved yet: the grants of the level touched no sprite
  m.resolveBoundObjects();
  CHECK(m.getUpgrade("kFloor1", "aesthetic")->bound.at("bg").resolved);
  sprites->present.erase("tab1");  // gone later: resolved earlier, so still looked at (the sprite lookup is the shop's)
  Spider spider;
  m.enableTempUpgrade("kFloor1", "aesthetic", &spider);
  CHECK(sprites->calls == std::vector<std::string>{"init bg", "init tab1"});
  // the spider only for the bound objects that have the effect: tab1 (bg has effect=false)
  CHECK(spider.seen == std::vector<std::string>{"kfloor1>tab1- on"});
  sprites->calls.clear();
  spider.seen.clear();
  m.disableTempUpgrades("", "", &spider);
  CHECK(sprites->calls == std::vector<std::string>{"init bg", "init tab1"});
  CHECK(spider.seen == std::vector<std::string>{"kfloor1>tab1- off"});
  // without a spider nothing is told
  sprites->calls.clear();
  m.enableUpgrade("kFloor1", "aesthetic", true);
  CHECK(sprites->calls.size() == 2);
  // an object that did not exist at resolve time is skipped
  Saved t("choco_greatcc_upgrades_store4");
  UpgradeManager n(t.progress);
  auto sp2 = std::make_shared<Sprites>();
  sp2->present = {"bg"};
  n.setSprites(sp2);
  n.setCurrentLevel("iquitos", 4, std::string_view(kXml), -1, 4);
  n.resolveBoundObjects();
  CHECK(n.getUpgrade("kFloor1", "aesthetic")->bound.at("bg").resolved);
  CHECK_FALSE(n.getUpgrade("kFloor1", "aesthetic")->bound.at("tab1").resolved);
  n.enableUpgrade("kFloor1", "aesthetic", false);
  CHECK(sp2->calls == std::vector<std::string>{"init bg"});
  // an object that names an asset and a state switches instead of re-initialising (the shipped files have none)
  UpgradeManager o(t.progress);
  auto sp3 = std::make_shared<Sprites>();
  o.setSprites(sp3);
  REQUIRE(o.loadUpgrades("<upgrades><upgradetype type='x'><upgrade name='a'><object name='bg' asset='floor' state='s2'/>"
                         "<object name='tab1' state='only'/></upgrade></upgradetype></upgrades>"));
  o.resolveBoundObjects();
  o.enableUpgrade("a", "x", false);
  CHECK(sp3->calls == std::vector<std::string>{"asset bg floor s2", "state tab1 only"});
  sp3->calls.clear();
  o.disableTempUpgrades("", "");  // not a preview: nothing to undo
  CHECK(sp3->calls.empty());
}

// ================================================================ the windows

namespace {

struct Env : test::ScriptEnv {
  Env() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/greatcc/upgrades", scratch("choco_greatcc_upgrades_user").string()), host(ctx) {
    ctx.module = games::findModule("greatcc");
    ctx.module->attach(ctx);
    booted = host.boot();
    world = worldOf(ctx);
    REQUIRE(booted);
    REQUIRE(world);
    ctx.users = std::make_shared<sdk::UserStore>(scratch("choco_greatcc_upgrades_user_store").string());
    world->progress().attach(ctx.users);
    ctx.users->create("Zed");
    world->values().load(
        "<values><value name='current_worth' value='0'/><value name='current_level_total' value='0'/>"
        "<value name='num_char_upgrades' value='0' saved='true'/></values>");
    ctx.strings.loadRows({{"hdr_floor", "Floor"}, {"txt_floor", "A better floor"}});
    world->levels().setCurrentLevel(4);
    world->levels().updateCurrentScore(600);
    world->values().setValue("current_worth", 600, false);
    world->upgrades().setCurrentLevel("iquitos", 4, std::string_view(kXml), -1, 4);
    wm.pushModal("dialog");
  }
  ~Env() { wm.destroyAll(); }

  // The shop is made into the dialog the way FillWindow does it.
  void open() {
    script::runFileInto(ctx, lua.L(), *wm.topModal(), "scripts/prelevel.lua");
    window = dynamic_cast<VPrelevelWindow*>(wm.topModal()->find("prelevel_window"));
    REQUIRE(window);
    list = window->list();
    REQUIRE(list);
  }
  void frames(int n = 3) {
    for (int i = 0; i < n; ++i) {
      wm.tickWindowAnimations(now += 20);
      wm.clearDestroyed();
      wm.dispatchMessages();
    }
  }
  std::vector<std::string> names() const {
    std::vector<std::string> out;
    for (VUpgradeTab* t : list->tabs()) out.push_back(t->upgrade()->name);
    return out;
  }
  VUpgradeTab* tab(const std::string& name) const {
    for (VUpgradeTab* t : list->tabs())
      if (t->upgrade()->name == name) return t;
    return nullptr;
  }
  RDisableButton* button(const char* name) const { return dynamic_cast<RDisableButton*>(wm.topModal()->find(name)); }

  script::ScriptHost host;
  bool booted = false;
  GreatCCWorld* world = nullptr;
  VPrelevelWindow* window = nullptr;
  VUpgradeList* list = nullptr;
  double now = 1000;
};

}  // namespace

TEST_CASE("VPrelevelWindow::initUpgrades lists Machines first, then Decoration, then Power-ups, and disables a category with nothing to buy") {
  {
    Env e;
    e.open();
    // the Machines list (category station) is the first with an offer
    CHECK(e.window->category() == "station");
    CHECK(e.list->category() == "station");
    CHECK(e.names() == std::vector<std::string>{"ksauce2", "klater"});  // the offer first, the locked one (it waits for kZ) after
    CHECK(e.button("toggle_upgrades_gameplay")->on());
    CHECK_FALSE(e.button("toggle_upgrades_gameplay")->disabled());
    CHECK_FALSE(e.button("toggle_upgrades_aesthetic")->disabled());
    CHECK_FALSE(e.button("toggle_upgrades_player")->disabled());
  }
  {
    Env f;
    for (const char* n : {"kSauce2", "kLater"}) f.world->upgrades().enableUpgrade(n, "station", false);  // nothing is left in Machines
    f.open();
    CHECK(f.window->category() == "aesthetic");
    CHECK(f.button("toggle_upgrades_gameplay")->disabled());
    CHECK(f.button("toggle_upgrades_aesthetic")->on());
  }
  {
    Env g;
    for (const char* n : {"kSauce2", "kLater"}) g.world->upgrades().enableUpgrade(n, "station", false);
    for (const char* n : {"kFloor1", "kFloor2"}) g.world->upgrades().enableUpgrade(n, "aesthetic", false);
    g.open();
    CHECK(g.window->category() == "gameplay");
    CHECK(g.button("toggle_upgrades_gameplay")->disabled());
    CHECK(g.button("toggle_upgrades_aesthetic")->disabled());
    CHECK(g.button("toggle_upgrades_player")->on());
  }
  {
    // nothing left anywhere: nothing is listed
    Env h;
    for (const char* n : {"kSauce2", "kLater"}) h.world->upgrades().enableUpgrade(n, "station", false);
    for (const char* n : {"kFloor1", "kFloor2"}) h.world->upgrades().enableUpgrade(n, "aesthetic", false);
    for (const char* n : {"kMove1", "kMove2"}) h.world->upgrades().enableUpgrade(n, "gameplay", false);
    h.open();
    CHECK(h.window->category().empty());
    CHECK(h.list->empty());
    CHECK(h.button("toggle_upgrades_player")->disabled());
  }
}

TEST_CASE("VUpgradeList: the offers of a category, the available first and then by price, stacked from the top; a category button lists its category") {
  Env e;
  e.open();
  e.frames();
  CHECK(e.window->ready());
  // the first frame made the window ready: the category buttons list their categories (UI name aesthetic -> aesthetic, gameplay -> station,
  // player -> gameplay)
  e.window->onMessage({ui::kButtonPress, "toggle_upgrades_aesthetic", 0});
  CHECK(e.list->category() == "aesthetic");
  CHECK(e.names() == std::vector<std::string>{"kfloor1", "kfloor2"});  // the offer first, the locked one after
  CHECK(e.tab("kfloor1")->tabState() == VUpgradeTab::kAvailable);
  CHECK(e.tab("kfloor2")->tabState() == VUpgradeTab::kLocked);
  CHECK(e.tab("kfloor1")->rect().y == 0);
  CHECK(e.tab("kfloor2")->rect().y == e.tab("kfloor1")->rect().h);
  CHECK(e.tab("kfloor1")->name() == "upgrade_tab_kfloor1");
  CHECK(e.tab("kfloor1")->find("txt_header")->label() == "Floor");
  CHECK(e.tab("kfloor1")->find("txt_content")->label() == "A better floor");
  CHECK(e.tab("kfloor1")->find("txt_price")->label() == "$100");
  CHECK(e.tab("kfloor2")->find("txt_price")->label() == "$250");  // "$%i" of the cost: the fraction goes
  CHECK(e.tab("kfloor1")->currentLayer() == VUpgradeTab::kAvailable);
  CHECK(e.tab("kfloor2")->currentLayer() == VUpgradeTab::kLocked);
  // the buy button is not shown on a locked offer
  CHECK_FALSE(e.tab("kfloor2")->find("btn_buy_upgrade")->shown());
  CHECK(e.tab("kfloor1")->find("btn_buy_upgrade")->shown());
  // the same category again: the list is left alone (the tabs are the same windows)
  const ui::WindowId first = e.tab("kfloor1")->id();
  e.window->onMessage({ui::kButtonPress, "toggle_upgrades_aesthetic", 0});
  CHECK(e.tab("kfloor1")->id() == first);
  e.window->onMessage({ui::kButtonPress, "toggle_upgrades_player", 0});
  CHECK(e.list->category() == "gameplay");
  CHECK(e.names() == std::vector<std::string>{"kmove1", "kmove2"});
  e.window->onMessage({ui::kButtonPress, "toggle_upgrades_gameplay", 0});
  CHECK(e.list->category() == "station");
  // an unaffordable offer is the same look but its button is disabled
  e.world->values().setValue("current_worth", 50, false);
  e.frames();
  CHECK(e.tab("ksauce2")->tabState() == VUpgradeTab::kAvailable);
  CHECK(dynamic_cast<RDisableButton*>(e.tab("ksauce2")->find("btn_buy_upgrade"))->disabled());
  e.world->values().setValue("current_worth", 600, false);
  e.frames();
  CHECK_FALSE(dynamic_cast<RDisableButton*>(e.tab("ksauce2")->find("btn_buy_upgrade"))->disabled());
}

TEST_CASE("VUpgradeTab: the pointer on an offer previews it, leaving ends the preview; an offer that cannot be paid is not previewed") {
  Env e;
  e.open();
  e.frames();
  e.window->onMessage({ui::kButtonPress, "toggle_upgrades_aesthetic", 0});
  VUpgradeTab* t = e.tab("kfloor1");
  REQUIRE(t);
  CHECK_FALSE(t->pointerInside());
  CHECK(t->onMouseMove(10, 10));  // the move goes on to a child (the contents window)
  CHECK(t->pointerInside());
  CHECK(t->active());
  CHECK(t->tabState() == VUpgradeTab::kOver);
  CHECK(e.world->upgrades().getUpgrade("kFloor1", "aesthetic")->state == kUpgradePreview);
  CHECK(e.wm.isMouseListener(t->id()));
  e.frames();
  CHECK(t->tabState() == VUpgradeTab::kOver);  // UpdateState keeps it while the pointer is in
  t->onMouseLeave();
  CHECK_FALSE(t->pointerInside());
  CHECK_FALSE(t->active());
  CHECK(t->tabState() == VUpgradeTab::kAvailable);
  CHECK(e.world->upgrades().getUpgrade("kFloor1", "aesthetic")->state == kUpgradeUnlocked);
  CHECK_FALSE(e.wm.isMouseListener(t->id()));
  // a move outside the list's own rectangle leaves
  t->onMouseMove(10, 10);
  REQUIRE(t->pointerInside());
  CHECK_FALSE(t->onMouseMove(10, -300));
  CHECK_FALSE(t->pointerInside());
  CHECK(e.world->upgrades().previewCount() == 0);
  // too poor: no preview
  e.world->values().setValue("current_worth", 99, false);
  t->onMouseMove(10, 10);
  CHECK(t->pointerInside());
  CHECK_FALSE(t->active());
  CHECK(t->tabState() != VUpgradeTab::kOver);
  CHECK(e.world->upgrades().previewCount() == 0);
  t->onMouseLeave();
  // a locked offer is not previewed either (it is not available)
  VUpgradeTab* locked = e.tab("kfloor2");
  e.world->values().setValue("current_worth", 600, false);
  locked->onMouseMove(10, 10);
  CHECK_FALSE(locked->active());
  CHECK(e.world->upgrades().previewCount() == 0);
  locked->onMouseLeave();
  // a change of category ends the preview that is on
  t->onMouseMove(10, 10);
  REQUIRE(t->active());
  e.window->onMessage({ui::kButtonPress, "toggle_upgrades_player", 0});
  CHECK(e.world->upgrades().previewCount() == 0);
  CHECK(e.world->upgrades().getUpgrade("kFloor1", "aesthetic")->state == kUpgradeUnlocked);
}

TEST_CASE("VUpgradeTab: buying an offer pays, saves, counts a power-up, and the list closes the gap") {
  Env e;
  e.open();
  e.frames();
  e.window->onMessage({ui::kButtonPress, "toggle_upgrades_player", 0});
  e.button("toggle_upgrades_player")->setOn(true);  // (the press that sent the message)
  REQUIRE(e.names() == std::vector<std::string>{"kmove1", "kmove2"});
  VUpgradeTab* move1 = e.tab("kmove1");
  const ui::WindowId move2 = e.tab("kmove2")->id();
  CHECK(e.tab("kmove2")->tabState() == VUpgradeTab::kLocked);  // it requires kMove1
  CHECK_FALSE(e.tab("kmove2")->purchaseUpgrade());  // not available: nothing happens
  CHECK(e.world->values().getReal("current_worth", 0) == 600);
  // hover then press the price button: the message goes up from the button to the tab
  move1->onMouseMove(10, 10);
  REQUIRE(move1->active());
  e.wm.sendMessage({ui::kButtonPress, "btn_buy_upgrade", move1->find("btn_buy_upgrade")->id()});
  CHECK(move1->tabState() == VUpgradeTab::kPurchased);
  CHECK_FALSE(move1->active());  // the preview ended first
  CHECK(e.world->upgrades().getUpgrade("kMove1", "gameplay")->state == kUpgradeOwned);
  CHECK(e.world->values().getReal("current_worth", 0) == 500);
  CHECK(e.world->levels().currentScore() == 500);   // the location's score goes down with the cash
  CHECK(e.world->progress().get("upgrade.iquitos.kmove1") == "1");
  CHECK(e.world->values().getReal("num_char_upgrades", 0) == 1);  // a gameplay (Power-ups) upgrade
  CHECK(e.world->gameStates().get("end-status") == "UpgradeChosenStory");
  CHECK(e.world->upgrades().previewCount() == 0);
  // the next frames remove the bought tab; the one below keeps its place until it has slid up ten pixels a frame
  const int h = move1->rect().h;
  e.frames(1);
  CHECK(e.names() == std::vector<std::string>{"kmove2"});
  VUpgradeTab* m2 = dynamic_cast<VUpgradeTab*>(e.wm.window(move2));
  REQUIRE(m2);
  CHECK(m2->rect().y == h - 10);
  e.frames(20);
  CHECK(m2->rect().y == 0);
  // the second one is now available (its requirement is owned) once the level is set again; the shop does that at the next level
  // too poor, or a second press on the bought one: nothing more is paid
  CHECK(e.world->values().getReal("current_worth", 0) == 500);
  // an offer costing more than the cash is not bought
  e.world->upgrades().getUpgrade("kMove2", "gameplay")->setState(kUpgradeUnlocked);
  e.frames(2);
  e.world->values().setValue("current_worth", 299, false);
  e.frames(2);
  CHECK_FALSE(m2->purchaseUpgrade());
  e.world->values().setValue("current_worth", 300, false);
  e.frames(2);
  CHECK(m2->purchaseUpgrade());
  CHECK(e.world->values().getReal("current_worth", 0) == 0);
  CHECK(e.world->values().getReal("num_char_upgrades", 0) == 2);
  // the Power-ups list is now empty: the selected button is disabled by the next frame
  e.frames(3);
  CHECK(e.list->empty());
  CHECK(e.button("toggle_upgrades_player")->disabled());
  CHECK_FALSE(e.button("toggle_upgrades_aesthetic")->disabled());  // the others have offers
}

TEST_CASE("VUpgradeTab: an endless run pays from current_level_total and leaves the location's score alone") {
  Env e;
  e.world->settings().setMode(1);
  e.world->values().setValue("current_level_total", 400, false);
  e.open();
  e.frames();
  e.window->onMessage({ui::kButtonPress, "toggle_upgrades_player", 0});
  VUpgradeTab* t = e.tab("kmove1");
  REQUIRE(t);
  CHECK(t->canPurchase());
  e.world->values().setValue("current_level_total", 99, false);
  CHECK_FALSE(t->canPurchase());  // current_worth (600) is not what an endless run spends
  e.world->values().setValue("current_level_total", 400, false);
  CHECK(t->purchaseUpgrade());
  CHECK(e.world->values().getReal("current_level_total", 0) == 300);
  CHECK(e.world->values().getReal("current_worth", 0) == 600);
  CHECK(e.world->levels().currentScore() == 600);
  CHECK(e.world->gameStates().get("end-status") == "");  // the metrics are story only
}

TEST_CASE("VPrelevelWindow: Close() ends the previews, the keys scroll the list, the buttons of the slider scroll it, the global goes with the window") {
  Env e;
  e.open();
  CHECK_FALSE(e.isNil("Close"));
  e.frames();
  e.window->onMessage({ui::kButtonPress, "toggle_upgrades_aesthetic", 0});
  VUpgradeTab* t = e.tab("kfloor1");
  t->onMouseMove(10, 10);
  REQUIRE(e.world->upgrades().previewCount() == 1);
  REQUIRE(e.run("Close()") == 0);
  CHECK(e.world->upgrades().previewCount() == 0);
  CHECK(e.world->upgrades().getUpgrade("kFloor1", "aesthetic")->state == kUpgradeUnlocked);
  // the keys: Down and Up scroll by a tick when the list is longer than its view; with nothing to scroll they are still taken
  ui::KeyEvent down;
  down.name = "Down";
  ui::KeyEvent up;
  up.name = "Up";
  ui::KeyEvent other;
  other.name = "x";
  CHECK(e.window->onKeyDown(down));
  CHECK(e.window->onKeyDown(up));
  CHECK_FALSE(e.window->onKeyDown(other));
  // a long list scrolls: make the interior longer than the view
  ui::Window* in = e.list->interior();
  in->setSize(in->rect().w, 700);
  e.list->setInteriorPosition(0);
  CHECK(e.window->onKeyDown(down));
  CHECK(e.list->position() == doctest::Approx(43.0f / (700 - 277)));
  e.window->onMessage({ui::kButtonPress, "slider_end", 0});
  CHECK(e.list->position() == doctest::Approx(2 * 43.0f / (700 - 277)));
  e.window->onMessage({ui::kButtonPress, "slider_start", 0});
  e.window->onKeyDown(up);
  CHECK(e.list->position() == doctest::Approx(0.0f));
}

TEST_CASE("VPrelevelWindow: before the first frame it does not react to the category buttons; the Close global goes with the window") {
  Env g;
  g.open();
  CHECK_FALSE(g.window->ready());
  g.window->onMessage({ui::kButtonPress, "toggle_upgrades_aesthetic", 0});
  CHECK(g.list->category() == "station");
  g.frames(1);
  CHECK(g.window->ready());
  g.window->onMessage({ui::kButtonPress, "toggle_upgrades_aesthetic", 0});
  CHECK(g.list->category() == "aesthetic");
  CHECK_FALSE(g.isNil("Close"));
  g.wm.destroyAll();
  CHECK(g.isNil("Close"));
}

TEST_CASE("the value modifiers that name an upgrade ask the upgrade manager: owned or previewed, not otherwise") {
  Env e;
  e.world->values().load(
      "<values><value name='walkspeed' value='4.0'><modify value='0.2' upgrade='kMove1' category='gameplay'/>"
      "<modify value='0.2' upgrade='kMove2' category='gameplay'/></value>"
      "<value name='sauce_cooldown' value='15000'><modify value='-0.5' upgrade='kSauce2' category='station'/></value></values>");
  GreatCCWorld& w = *e.world;
  CHECK(w.values().getReal("walkspeed", 0) == doctest::Approx(4.0));
  CHECK(w.values().getReal("sauce_cooldown", 0) == doctest::Approx(15000.0));
  w.upgrades().enableUpgrade("kMove1", "gameplay", false);
  CHECK(w.values().getReal("walkspeed", 0) == doctest::Approx(4.8));
  w.upgrades().enableTempUpgrade("kMove2", "gameplay");  // locked (it requires kMove1, which is owned only now): not available, no effect
  CHECK(w.values().getReal("walkspeed", 0) == doctest::Approx(4.8));
  w.upgrades().getUpgrade("kMove2", "gameplay")->setState(kUpgradeUnlocked);
  w.upgrades().enableTempUpgrade("kMove2", "gameplay");  // the preview counts
  CHECK(w.values().getReal("walkspeed", 0) == doctest::Approx(5.6));
  w.upgrades().disableTempUpgrades("", "");
  CHECK(w.values().getReal("walkspeed", 0) == doctest::Approx(4.8));
  // the category must match
  w.upgrades().enableUpgrade("kSauce2", "station", false);
  CHECK(w.values().getReal("sauce_cooldown", 0) == doctest::Approx(7500.0));
  // a reload of the values (each level load does one) keeps asking the manager
  w.values().reload();
  CHECK(w.values().getReal("walkspeed", 0) == doctest::Approx(4.8));
  // a new player: the manager is cleared with the rest of the state
  w.reloadState(e.ctx);
  CHECK(w.values().getReal("walkspeed", 0) == doctest::Approx(4.0));
}
