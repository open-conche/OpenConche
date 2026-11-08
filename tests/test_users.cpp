#include <doctest/doctest.h>

#include <filesystem>
#include <algorithm>
#include <fstream>
#include <sstream>

#include "ScriptHarness.h"
#include "games/Registry.h"
#include "games/choc3/PlayerSave.h"
#include "sdk/Users.h"
#include "script/ScriptHost.h"

using choco::sdk::UserStore;
namespace fs = std::filesystem;

namespace {

struct TempDir {
  explicit TempDir(const char* tag) {
    path = fs::temp_directory_path() / (std::string("choco_users_") + tag);
    fs::remove_all(path);
    fs::create_directories(path);
  }
  ~TempDir() { fs::remove_all(path); }
  std::string str() const { return path.string(); }
  fs::path path;
};

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

TEST_CASE("users: create, rename, delete and the current index") {
  TempDir d("basic");
  UserStore s(d.str());
  CHECK(s.count() == 0);
  CHECK(s.current() == -1);
  CHECK(s.currentName() == "");
  CHECK(s.name(0) == "");
  CHECK(s.loadSave() == "");
  CHECK_FALSE(s.saveSave("x"));  // no user: nothing written

  CHECK(s.create("Ann") == 0);
  CHECK(s.create("Bob") == 1);
  CHECK(s.create("Cy") == 2);
  CHECK(s.current() == 2);  // a new user becomes current
  CHECK(s.nameInUse("Bob"));
  CHECK_FALSE(s.nameInUse("bob"));  // case-sensitive

  CHECK_FALSE(s.setCurrent(3));  // ignored
  CHECK(s.current() == 2);
  CHECK(s.setCurrent(0));
  CHECK(s.setCurrent(-1));
  CHECK(s.current() == -1);
  CHECK(s.setCurrent(1));

  CHECK(s.rename(1, "Robert"));
  CHECK(s.name(1) == "Robert");
  CHECK_FALSE(s.nameInUse("Bob"));

  CHECK(s.remove(1));  // selects index - 1
  CHECK(s.count() == 2);
  CHECK(s.current() == 0);
  CHECK(s.name(1) == "Cy");
  CHECK(s.remove(0));  // index - 1 < 0 selects the last user
  CHECK(s.count() == 1);
  CHECK(s.current() == 0);
  CHECK(s.remove(0));
  CHECK(s.count() == 0);
  CHECK(s.current() == -1);
  CHECK_FALSE(s.remove(0));
}

TEST_CASE("users: persistence across a reload, saves and prefs") {
  TempDir d("persist");
  {
    UserStore s(d.str());
    s.create("Zo\xC3\xAB");
    CHECK(s.create("Tab\tSafe%") == -1);  // control character
    CHECK(s.count() == 1);
    s.create("Ann & Bob");
    CHECK(s.saveSave("line1\nline2\t\"quoted\" \xE2\x82\xAC"));
    s.setCurrent(0);
    CHECK(s.saveSave("zoe save"));
    CHECK(s.setPref("soundlevel", "321"));
    CHECK(s.setGlobalPref("language", "fr"));
    CHECK(s.logScore(1234.4, "data", "<xml/>"));
  }
  UserStore r(d.str());
  CHECK(r.count() == 2);
  CHECK(r.current() == 0);
  CHECK(r.name(0) == "Zo\xC3\xAB");
  CHECK(r.name(1) == "Ann & Bob");
  CHECK(r.loadSave() == "zoe save");
  r.setCurrent(1);
  CHECK(r.loadSave() == "line1\nline2\t\"quoted\" \xE2\x82\xAC");
  CHECK(r.pref("soundlevel") == "750");  // copied from the creator when it still had the default
  r.setCurrent(0);
  CHECK(r.pref("soundlevel") == "321");
  CHECK(r.globalPref("language") == "fr");
  CHECK(slurp(d.path / "hiscore.dat") == "choco3-hiscores 1\n0\t1234\tZo\xC3\xAB\tdata\t<xml/>\n");
  REQUIRE(r.hiscores().rows("0").size() == 1);
  CHECK(r.hiscores().rows("0")[0].user == "Zo\xC3\xAB");
}

TEST_CASE("users: default volumes are copied to a new user") {
  UserStore s("");  // in memory
  CHECK(s.pref("soundlevel") == "750");
  CHECK(s.pref("musiclevel") == "650");
  CHECK(s.pref("ambientlevel") == "500");
  CHECK(s.pref("soundoff") == "0");
  CHECK(s.pref("nothing", "d") == "d");
  s.create("A");
  s.setPref("musiclevel", "100");
  s.setPref("soundoff", "1");
  s.create("B");
  CHECK(s.pref("musiclevel") == "100");
  CHECK(s.pref("soundoff") == "1");
  CHECK(s.saveSave("mem"));
  s.setCurrent(0);
  CHECK(s.loadSave() == "");
  s.setCurrent(1);
  CHECK(s.loadSave() == "mem");
}

TEST_CASE("users: bad names and safe folder names") {
  CHECK_FALSE(UserStore::validName(""));
  CHECK_FALSE(UserStore::validName("a\nb"));
  CHECK_FALSE(UserStore::validName("\xFF\xFE"));
  CHECK_FALSE(UserStore::validName("\xE2\x82"));  // truncated sequence
  CHECK_FALSE(UserStore::validName(std::string(256, 'a')));
  CHECK(UserStore::validName("Ren\xC3\xA9" "e"));

  CHECK(UserStore::sanitiseFolder("a/b\\c:d*e?") == "a_b_c_d_e_");
  CHECK(UserStore::sanitiseFolder("..") == "user");
  CHECK(UserStore::sanitiseFolder("trail. . ") == "trail");
  CHECK(UserStore::sanitiseFolder("CON") == "CON_");
  CHECK(UserStore::sanitiseFolder("nul.txt") == "nul_.txt");
  CHECK(UserStore::sanitiseFolder("Con.Air") == "Con_.Air");
  CHECK(UserStore::sanitiseFolder("CONIN$") == "CONIN$_");
  CHECK(UserStore::sanitiseFolder("conout$.x") == "conout$_.x");
  CHECK(UserStore::sanitiseFolder(std::string(60, 'x')).size() == 40);
  // Truncation never splits a UTF-8 sequence.
  const std::string long2 = std::string(39, 'x') + "\xC3\xA9\xC3\xA9";
  CHECK(UserStore::validName(UserStore::sanitiseFolder(long2)));

  TempDir d("names");
  UserStore s(d.str());
  CHECK(s.create("../../evil") == 0);
  CHECK(s.create("Evil") == 1);
  CHECK(s.create("EVIL") == 2);  // same folder up to case
  CHECK(s.create("") == -1);
  CHECK(s.count() == 3);
  CHECK(s.folder(1) != s.folder(2));
  for (int i = 0; i < 3; ++i) CHECK(fs::exists(d.path / "profiles" / s.folder(i)));
  CHECK(s.folder(0).find('/') == std::string::npos);
  CHECK(s.name(0) == "../../evil");  // the display name is kept as given
}

TEST_CASE("users: saves are written through a temporary file") {
  TempDir d("atomic");
  UserStore s(d.str());
  s.create("A");
  CHECK(s.saveSave("first"));
  CHECK(s.saveSave("second"));
  const fs::path dir = d.path / "profiles" / s.folder(0);
  CHECK(slurp(dir / "save.txt") == "second");
  for (auto& e : fs::directory_iterator(dir)) CHECK(e.path().extension() != ".tmp");
  for (auto& e : fs::directory_iterator(d.path)) CHECK(e.path().extension() != ".tmp");
  // A stale temp file from a crash does not matter.
  { std::ofstream(dir / "save.txt.tmp") << "junk"; }
  CHECK(s.saveSave("third"));
  UserStore r(d.str());
  CHECK(r.loadSave() == "third");
}

TEST_CASE("users: a name that looks like the header line survives a reload") {
  TempDir d("header");
  {
    UserStore s(d.str());
    s.create("current champ");
    s.create("current 5");
    s.setCurrent(0);
  }
  UserStore r(d.str());
  REQUIRE(r.count() == 2);
  CHECK(r.name(0) == "current champ");
  CHECK(r.name(1) == "current 5");
  CHECK(r.current() == 0);
}

TEST_CASE("users: a new user copies the global sound prefs when there is no current user") {
  TempDir d("globalcopy");
  UserStore s(d.str());
  CHECK(s.setPref("soundoff", "1"));  // no user: the global slot
  CHECK(s.setPref("musiclevel", "42"));
  s.create("Quiet");
  CHECK(s.pref("soundoff") == "1");
  CHECK(s.pref("musiclevel") == "42");
  UserStore r(d.str());
  CHECK(r.pref("soundoff") == "1");
}

TEST_CASE("users: a failed write keeps the old file") {
  TempDir d("failwrite");
  UserStore s(d.str());
  s.create("A");
  CHECK(s.saveSave("old"));
  const fs::path dir = d.path / "profiles" / s.folder(0);
  // A directory where the temporary file must go makes the write fail.
  fs::create_directory(dir / "save.txt.tmp");
  CHECK_FALSE(s.saveSave("new"));
  CHECK(slurp(dir / "save.txt") == "old");
  CHECK(s.loadSave() == "old");
  fs::remove(dir / "save.txt.tmp");
  CHECK(s.saveSave("new"));
  CHECK(s.loadSave() == "new");
  // The same path serves user files.
  CHECK(s.writeFile("notes.txt", "one"));
  fs::create_directory(d.path / "notes.txt.tmp");
  CHECK_FALSE(s.writeFile("notes.txt", "two"));
  CHECK(slurp(d.path / "notes.txt") == "one");
}

TEST_CASE("users: a damaged index is survived") {
  TempDir d("damaged");
  { std::ofstream(d.path / "users.txt") << "choco3-users 1\ncurrent 9\n../x\tEvil\nok\tFine\ngarbage\n"; }
  UserStore s(d.str());
  CHECK(s.count() == 2);
  CHECK(s.current() == 0);  // out of range falls back to the first user
  CHECK(s.folder(0).find("..") == std::string::npos);
  { std::ofstream(d.path / "users.txt") << "not an index"; }
  UserStore t(d.str());
  CHECK(t.count() == 0);
  CHECK(t.current() == -1);
}

namespace {

// Script natives over a user directory. The test assets directory has no game files; only the natives are needed.
struct UserEnv {
  explicit UserEnv(const std::string& userDir)
      : fs(CHOCO_TEST_ASSETS, userDir), ctx(lua, fs, wm, clock, rng, config, stubs, log) {
    log.setEcho(false);
    log.setKeep(true);
    choco::script::registerUserNatives(lua.L(), ctx);
  }
  int run(const std::string& code) {
    lua_State* L = lua.L();
    int status = luaL_loadbuffer(L, code.data(), code.size(), "=t");
    if (status == 0) status = lua_pcall(L, 0, 0, 0);
    if (status != 0) err = lua_tostring(L, -1);
    lua_settop(L, 0);
    return status;
  }
  std::string str(const char* g) {
    lua_State* L = lua.L();
    lua_getglobal(L, g);
    std::string s = lua_isnil(L, -1) ? "<nil>" : lua_isboolean(L, -1) ? (lua_toboolean(L, -1) ? "true" : "false") : lua_tostring(L, -1) ? lua_tostring(L, -1) : "?";
    lua_pop(L, 1);
    return s;
  }
  choco::script::LuaState lua;
  choco::assets::AssetFS fs;
  choco::ui::WindowManager wm;
  choco::script::Clock clock;
  choco::script::Rng rng{1};
  choco::script::Config config;
  choco::script::StubLog stubs;
  choco::script::Log log;
  choco::script::ScriptContext ctx;
  std::string err;
};

}  // namespace

TEST_CASE("user natives: create, save, restart and load") {
  TempDir d("natives");
  {
    UserEnv e(d.str());
    REQUIRE(e.run(R"(
      n0 = GetNumUsers(); c0 = GetCurrentUser(); s0 = LoadGameString(); nm0 = GetCurrentUserName()
      SaveGameString("ignored")            -- no user: no-op
      CreateNewUser("Ann")
      n1 = GetNumUsers(); c1 = GetCurrentUser(); nm1 = GetCurrentUserName()
      SaveGameString("the save {1,2,3}")
      CreateNewUser("Bob")
      u0 = GetUserName(0); u1 = GetUserName(1); u9 = GetUserName(9)
      used = IsNameInUse("Ann"); unused = IsNameInUse("Cy")
      SetCurrentUser(7); afterBad = GetCurrentUser()
      SetCurrentUser(0); cur0 = GetCurrentUser()
      ChangeCurrentUserName("Anna"); renamed = GetCurrentUserName()
      WriteToFile("notes.txt", "hello"); back = ReadFromFile("notes.txt"); missing = ReadFromFile("nope.txt")
      escape = WriteToFile("../escape.txt", "x"); escapeRead = ReadFromFile("../escape.txt")
      reserved = WriteToFile("users.txt", "x")
      WriteToFile("one.choco3", "a"); WriteToFile("two.choco3", "b"); WriteToFile("sub/deep.choco3", "c")
      colon = WriteToFile("C:/foo.txt", "x"); colon2 = WriteToFile("a:b", "x"); abs = WriteToFile("/tmp/choco_abs.txt", "x")
      colonRead = ReadFromFile("C:/foo.txt")
      f1 = NextSaveFileName(); f2 = NextSaveFileName(); f3 = NextSaveFileName(); f4 = NextSaveFileName()
      LogScore(500, "g", "<m/>")
    )") == 0);
    CHECK(e.err == "");
    CHECK(e.str("n0") == "0");
    CHECK(e.str("c0") == "-1");
    CHECK(e.str("s0") == "");
    CHECK(e.str("nm0") == "");
    CHECK(e.str("n1") == "1");
    CHECK(e.str("c1") == "0");
    CHECK(e.str("nm1") == "Ann");
    CHECK(e.str("u0") == "Ann");
    CHECK(e.str("u1") == "Bob");
    CHECK(e.str("u9") == "");
    CHECK(e.str("used") == "true");
    CHECK(e.str("unused") == "false");
    CHECK(e.str("afterBad") == "1");
    CHECK(e.str("cur0") == "0");
    CHECK(e.str("renamed") == "Anna");
    CHECK(e.str("back") == "hello");
    CHECK(e.str("missing") == "");
    CHECK(e.str("escape") == "false");
    CHECK(e.str("escapeRead") == "");
    CHECK(e.str("reserved") == "false");
    CHECK(e.str("colon") == "false");
    CHECK(e.str("colon2") == "false");
    CHECK(e.str("abs") == "false");
    CHECK(e.str("colonRead") == "");
    CHECK_FALSE(fs::exists("/tmp/choco_abs.txt"));
    CHECK(e.str("f1") == "one");
    CHECK(e.str("f2") == "two");
    CHECK(e.str("f3") == "");
    CHECK(e.str("f4") == "one");  // exhausted resets
    CHECK(e.stubs.count("GetNumUsers") == 0);
  }
  {
    UserEnv e(d.str());  // a restarted host on the same userdir
    REQUIRE(e.run(R"(
      n = GetNumUsers(); c = GetCurrentUser(); name = GetCurrentUserName(); sv = LoadGameString()
      SetCurrentUser(1); bobSave = LoadGameString()
      SetCurrentUser(0)
      DeleteUser(0); afterDel = GetNumUsers(); curAfter = GetCurrentUser(); who = GetCurrentUserName()
      DeleteUser(5); stillOne = GetNumUsers(); curAfterBad = GetCurrentUser()
      DeleteUser(0); none = GetCurrentUser()
      notes = ReadFromFile("notes.txt")
    )") == 0);
    CHECK(e.str("n") == "2");
    CHECK(e.str("c") == "0");
    CHECK(e.str("name") == "Anna");
    CHECK(e.str("sv") == "the save {1,2,3}");
    CHECK(e.str("bobSave") == "");
    CHECK(e.str("afterDel") == "1");
    CHECK(e.str("curAfter") == "0");
    CHECK(e.str("who") == "Bob");
    CHECK(e.str("stillOne") == "1");
    CHECK(e.str("curAfterBad") == "0");  // DeleteUser(5) ran SetCurrentUser(4), which is ignored
    CHECK(e.str("none") == "-1");
    CHECK(e.str("notes") == "hello");
  }
}

TEST_CASE("users: names with dot runs keep their folder and save across reloads") {
  for (const char* nm : {"Bob..Smith", "Bob...Smith"}) {
    TempDir d("dots");
    std::string folder;
    {
      UserStore s(d.str());
      s.create(nm);
      folder = s.folder(0);
      CHECK(folder.find("..") == std::string::npos);
      CHECK(s.saveSave("kept"));
    }
    for (int i = 0; i < 2; ++i) {
      UserStore r(d.str());
      CHECK(r.count() == 1);
      CHECK(r.folder(0) == folder);
      CHECK(r.loadSave() == "kept");
    }
    int dirs = 0;
    for (const auto& e : std::filesystem::directory_iterator(d.path / "profiles")) { (void)e; ++dirs; }
    CHECK(dirs == 1);
  }
}

TEST_CASE("user natives: ToggleFullScreen flips the stored pref, or asks the platform hook") {
  TempDir d("togglefs");
  UserEnv e(d.str());
  REQUIRE(e.run("a = ToggleFullScreen(); b = ToggleFullScreen()") == 0);
  CHECK(e.str("a") == "true");
  CHECK(e.str("b") == "true");
  CHECK(e.stubs.count("ToggleFullScreen") == 0);  // a real native, not the stub
  CHECK(e.ctx.users->globalPref("fullscreen") == "0");  // flipped twice: back to the start
  REQUIRE(e.run("ToggleFullScreen()") == 0);
  CHECK(e.ctx.users->globalPref("fullscreen") == "1");
  UserStore reload(d.str());
  CHECK(reload.globalPref("fullscreen") == "1");  // persisted

  int calls = 0;
  bool answer = false;
  e.ctx.toggleFullscreen = [&] {
    ++calls;
    return answer;
  };
  REQUIRE(e.run("c = ToggleFullScreen()") == 0);
  CHECK(e.str("c") == "false");  // the platform refused: Options shows screenswitchfailed
  answer = true;
  REQUIRE(e.run("c = ToggleFullScreen()") == 0);
  CHECK(e.str("c") == "true");
  CHECK(calls == 2);
  CHECK(e.ctx.users->globalPref("fullscreen") == "1");  // the hook owns the pref when set
}

namespace {

// Captured from Reforged: Player:BuildSaveGameString() with Main dead before Reset/LoadGame. It carries an integrity table
// (status legacy_unverified), nested money= keys inside stats, but no top-level money/time and knownRecipes={}.
std::string bareSave() {
  return std::string(R"BARE(return {highScoreNationality="",categoryCount={},factoryTopProduct={},medals={},characterMobilityVersion=0,storyCreationLibrary={prices={},appearance={},descriptions={},retained={},recipes={},machinery={},names={},},pendingAftermaths={},itemAppearance={},activeTips={},questsWaiting={},lastSeenPrice={},supply={},mode="story",integrity={version=1,status="legacy_unverified",devSessions=0,},firstBuy={},questHintCooldowns={},buildingsOwned={},buildingCharacters={},itemDescriptions={},catalogue={},stats={largestExpense=0,ingredientSpend=0,pbHistory={{companyValue=0,difficultyMultiplier=1,scoreVersion=1,ingredientSpend=0,portsVisited=0,companyScore=4000,trackingStartedWeek=1,campaignMode="story",medals=0,lifetimeSpend=0,baseScore=4000,specialOrderRevenue=0,customRecipes=0,specialOrdersCompleted=0,questsCompleted=0,money=0,largestSpecialOrder=0,specialOrdersFailed=0,week=1,valuePoints=0,progressionPoints=4000,shops=0,efficiencyPoints=0,highestCash=0,masteryPoints=0,travelTrips=0,recipesMade=0,factorySpend=0,factories=0,travelSpend=0,salesRevenue=0,productValue=0,commercePoints=0,operationsPoints=0,machineryValue=0,largestSale=0,ingredientValue=0,lifetimeRevenue=0,expansionPoints=0,casesMade=0,machinerySpend=0,statsLegacyBaseline=false,casesSold=0,snapshotVersion=1,difficulty=1,recipesKnown=0,earningsPace=0,salesTransactions=0,rank=1,},},trackingStartedWeek=1,currentSnapshot={companyValue=0,difficultyMultiplier=1,scoreVersion=1,ingredientSpend=0,portsVisited=0,companyScore=4000,trackingStartedWeek=1,campaignMode="story",medals=0,lifetimeSpend=0,baseScore=4000,specialOrderRevenue=0,customRecipes=0,specialOrdersCompleted=0,questsCompleted=0,money=0,largestSpecialOrder=0,specialOrdersFailed=0,week=1,valuePoints=0,progressionPoints=4000,shops=0,efficiencyPoints=0,highestCash=0,masteryPoints=0,travelTrips=0,recipesMade=0,factorySpend=0,factories=0,travelSpend=0,salesRevenue=0,productValue=0,commercePoints=0,operationsPoints=0,machineryValue=0,largestSale=0,ingredientValue=0,lifetimeRevenue=0,expansionPoints=0,casesMade=0,machinerySpend=0,statsLegacyBaseline=false,casesSold=0,snapshotVersion=1,difficulty=1,recipesKnown=0,earningsPace=0,salesTransactions=0,rank=1,},salesTransactions=0,specialOrderRevenue=0,specialOrdersCompleted=0,questsCompleted=0,personalBest={companyValue=0,difficultyMultiplier=1,scoreVersion=1,ingredientSpend=0,portsVisited=0,companyScore=4000,trackingStartedWeek=1,campaignMode="story",medals=0,lifetimeSpend=0,baseScore=4000,specialOrderRevenue=0,customRecipes=0,specialOrdersCompleted=0,questsCompleted=0,money=0,largestSpecialOrder=0,specialOrdersFailed=0,week=1,valuePoints=0,progressionPoints=4000,shops=0,efficiencyPoints=0,highestCash=0,masteryPoints=0,travelTrips=0,recipesMade=0,factorySpend=0,factories=0,travelSpend=0,salesRevenue=0,productValue=0,commercePoints=0,operationsPoints=0,machineryValue=0,largestSale=0,ingredientValue=0,lifetimeRevenue=0,expansionPoints=0,casesMade=0,machinerySpend=0,statsLegacyBaseline=false,casesSold=0,snapshotVersion=1,difficulty=1,recipesKnown=0,earningsPace=0,salesTransactions=0,rank=1,},largestSpecialOrder=0,specialOrdersFailed=0,version=1,largestSale=0,ingredientUnitsBought=0,highestCash=0,otherSpend=0,travelTrips=0,factorySpend=0,rankReachedWeek={1,},travelSpend=0,moneyIn=0,salesRevenue=0,questRevenue=0,moneyOut=0,gamblingSpend=0,machinerySpend=0,largestIncome=0,gamblingRevenue=0,travelWeeks=0,otherRevenue=0,legacyBaseline=false,},firstEverBuy={},portsVisited={},itemMachinery={},factoryAcquiredTime={},questOfferText={},orderBannedBuildings={},questsActive={},firstEverSell={},pendingAnnouncements={},lastSeenPort={},stringTable={},products={},portsAvailable={},ingredients={},pendingSpecialOrders={},freePlayVersion=0,buildingsEnabled={},highScoreDisplayName="",lowPrice={},buildingsVisited={},portsCost={},highPrice={},lastVisitTime={},questsComplete={},itemNames={},orderEligibleChars={},needs={},difficulty=1,characterLocations={},rank=1,categoryMadeCount={},firstSellCategory={},questStarters={},currentHolidays={},labIngredients={},portVisitCount=0,options={},itemsSold={},charHappiness={},knownRecipes={},useTimes={},questsDeferred={},haggleDisable={},shopOrderData={},firstSell={},powerups={},buildingLastVisitTime={},factoryTotalProduction={},itemsMade={},factories={},buildingsBlocked={},itemRecipes={},orderBannedChars={},labFirstUse={},holidayAnnouncements={},questDifficulty={},deliveryDeadlineVersion=1,itemPrices={},questLocations={},questsAcceptedEver={},ingredientsAvailable={},charHappinessTime={},freePlayProfileId="",questVariables={},characterLocks={},deliveries={}})BARE");
}

}  // namespace

TEST_CASE("users: SaveGameString through the choc3 module refuses the bare Player and writes a real save") {
  TempDir d("refuse");
  UserEnv e(d.str());
  e.ctx.module = choco::games::findModule("choc3");
  const std::string bare = bareSave();
  lua_pushstring(e.lua.L(), bare.c_str());
  lua_setglobal(e.lua.L(), "BARE");
  REQUIRE(e.run("CreateNewUser('Ann'); SaveGameString(BARE); refusedBack = LoadGameString()") == 0);
  CHECK(e.str("refusedBack") == "");
  const auto& lines = e.log.lines();
  CHECK(std::count(lines.begin(), lines.end(), "[save] refused: Player was never initialised") == 1);
  REQUIRE(e.run("SaveGameString('return {knownRecipes={b01=true,},money=5,}'); realBack = LoadGameString()") == 0);
  CHECK(e.str("realBack") == "return {knownRecipes={b01=true,},money=5,}");
  CHECK(std::count(lines.begin(), lines.end(), "[save] refused: Player was never initialised") == 1);
}

TEST_CASE("users: the never-reset Player fingerprint is refused, real saves are not") {
  using choco::game::isUninitialisedPlayerSave;
  const std::string bare = bareSave();
  CHECK(isUninitialisedPlayerSave(bare));
  CHECK(bare.find("integrity={version=1,status=\"legacy_unverified\"") != std::string::npos);
  // The same string with a top-level money or time (what Reset writes) is a real save.
  CHECK_FALSE(isUninitialisedPlayerSave(bare.substr(0, bare.size() - 1) + ",money=0}"));
  CHECK_FALSE(isUninitialisedPlayerSave(bare.substr(0, bare.size() - 1) + ",time=1}"));
  CHECK_FALSE(isUninitialisedPlayerSave("return {knownRecipes={b01=true,},categoryCount={bar=12,},}"));
  CHECK_FALSE(isUninitialisedPlayerSave("return {knownRecipes={},money=5,}"));
  // Nested keys and string contents do not count as top-level: a money= in a sub-table does not rescue the bare literal,
  // and a string holding ",money=1," or a brace does not confuse the depth.
  CHECK(isUninitialisedPlayerSave("return {stats={money=3,},name=\"a},money=1,\",knownRecipes={},}"));
  CHECK_FALSE(isUninitialisedPlayerSave("return {name=\"x{\",knownRecipes={},money=1,}"));
  CHECK_FALSE(isUninitialisedPlayerSave(""));
}

TEST_CASE("users: save slot 1 is save_free.txt, separate from save.txt, and survives a reload") {
  TempDir d("slots");
  std::string folder;
  {
    UserStore s(d.str());
    s.create("Ann");
    folder = s.folder(0);
    CHECK(s.saveSave("story"));
    CHECK(s.saveSave("free", 1));
    CHECK(s.loadSave(0) == "story");
    CHECK(s.loadSave(1) == "free");
  }
  CHECK(slurp(d.path / "profiles" / folder / "save.txt") == "story");
  CHECK(slurp(d.path / "profiles" / folder / "save_free.txt") == "free");
  UserStore r(d.str());
  CHECK(r.loadSave() == "story");
  CHECK(r.loadSave(1) == "free");
  UserStore mem("");  // in memory: the slots are separate there too
  mem.create("Bob");
  CHECK(mem.saveSave("a", 1));
  CHECK(mem.loadSave(0) == "");
  CHECK(mem.loadSave(1) == "a");
}
