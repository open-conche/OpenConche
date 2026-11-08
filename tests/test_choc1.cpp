// Chocolatier 1 on the engine (docs/re/choc1-fit.md): profile, util natives, users and saves.
#include <doctest/doctest.h>

#include <string>

#include "games/Registry.h"
#include "games/choc1/Choc1Natives.h"
#include "ScriptHarness.h"
#include "sdk/Users.h"

using namespace choco;

TEST_CASE("Choc1 profile: close handling, quit dialog, exit save, volumes, run views") {
  const script::GameProfile& p = games::findModule("choc1")->profile();
  CHECK(std::string(p.id) == "choc1");
  CHECK(std::string(p.closeGlobal) == "QuitGame");
  CHECK(std::string(p.closeRequest) == "QuitGame()");
  CHECK(std::string(p.quitDialog) == "ui/yndialog.lua");
  CHECK(std::string(p.exitSave) == "if bsutil then bsutil.SaveGame() end");
  CHECK(std::string(p.windowTitle) == "Chocolatier");
  CHECK_FALSE(p.pauseDialogOnFocusLoss);
  CHECK(p.volumeDefaults.sfx == 750);
  CHECK(p.volumeDefaults.music == 750);
  CHECK(p.volumeDefaults.ambient == 750);
  REQUIRE(p.runViews.size() == 2);
  CHECK(std::string(p.runViews[0]) == "ui/portview.lua");
  CHECK(std::string(p.runViews[1]) == "ui/map.lua");
  CHECK(std::string(p.travelGlobal).empty());
  CHECK(std::string(p.quitGlobal).empty());
}

namespace {

struct Choc1Env : test::ScriptEnv {
  Choc1Env() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ui/modal"), host(ctx) {
    ctx.module = games::findModule("choc1");
    ctx.module->attach(ctx);
    booted = host.boot();
  }
  script::ScriptHost host;
  bool booted = false;
};

// The English strings.xml money formats as the file stores them (doubled percents).
assets::StringTable moneyStrings() {
  assets::StringTable t;
  t.loadRows({{"formatBillions", "$%%d,%%03d,%%03d,%%03d"},
              {"formatMillions", "$%%d,%%03d,%%03d"},
              {"formatThousands", "$%%d,%%03d"},
              {"formatOnes", "$%%d"}});
  return t;
}

}  // namespace

TEST_CASE("UtilDollars: string-table formats, truncation, unsigned 32-bit wrap") {
  const assets::StringTable t = moneyStrings();
  CHECK(script::choc1Dollars(t, 0) == "$0");
  CHECK(script::choc1Dollars(t, 999) == "$999");
  CHECK(script::choc1Dollars(t, 1234) == "$1,234");
  CHECK(script::choc1Dollars(t, 1000) == "$1,000");
  CHECK(script::choc1Dollars(t, 1234567) == "$1,234,567");
  CHECK(script::choc1Dollars(t, 1234567890) == "$1,234,567,890");
  CHECK(script::choc1Dollars(t, 2.7) == "$2");
  CHECK(script::choc1Dollars(t, 1234.9) == "$1,234");
  CHECK(script::choc1Dollars(t, -2.7) == "$4,294,967,294");  // truncates to -2, which wraps
  CHECK(script::choc1Dollars(t, -1) == "$4,294,967,295");
  CHECK(script::choc1Dollars(t, 4294967296.0) == "$0");  // 2^32 wraps to 0
  // Without the keys the same English formats apply.
  CHECK(script::choc1Dollars(assets::StringTable{}, 1234567) == "$1,234,567");
}

TEST_CASE("Choc1 util natives: floor truncates, aliases, no-ops, timestamp, checks") {
  Choc1Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("f1 = UtilMathFloor(2.9); f2 = UtilMathFloor(-2.9); d = UtilDollars(1234); r = UtilMathRandom(5, 5);"
                "UtilMathSeedRandom(); UtilTriggerAd(); UtilTriggerCommunityOffer(); ts = UtilTimeStamp(); nx = UtilNextSaveFile();"
                "cheat = UtilCheckCheatMode(); peek = UtilCheckFirstPeek(); xs = UtilCheckCrossSell(); t = UtilCurrentTime()") == 0);
  CHECK(e.getNumber("f1") == 2);
  CHECK(e.getNumber("f2") == -2);
  CHECK(e.getString("d") == "$1,234");
  CHECK(e.getNumber("r") == 5);
  CHECK(e.getString("nx") == "");
  CHECK(e.getString("cheat") == "false");
  CHECK(e.getString("peek") == "false");
  CHECK(e.getString("xs") == "false");
  const std::string ts = e.getString("ts");
  CHECK(ts.size() == 19);
  CHECK((ts[4] == '.' && ts[7] == '.' && ts[10] == '-' && ts[13] == '.' && ts[16] == '.'));
  for (const char* n : {"UtilMathSeedRandom", "UtilTriggerAd", "UtilTriggerCommunityOffer", "UtilMathFloor", "UtilCheckCheatMode"})
    CHECK(e.stubs.count(n) == 0);
}

TEST_CASE("UtilMathRandom: the CRT formula of Util natives (0041dbe0) on the shared stream") {
  Choc1Env e;
  REQUIRE(e.booted);
  // One crtRand draw per call: truncTowardZero(lo + r*(hi-lo+1.0)/32767.0). With (0, 32766) the result is the raw
  // draw, so the unseeded stream shows the classic MSVC seed-1 sequence 41, 18467, 6334 (the stream the factory
  // and dice games share; UtilMathSeedRandom stays a no-op so this stays reproducible).
  REQUIRE(e.run("a = UtilMathRandom(0, 32766); b = UtilMathRandom(0, 32766); c = UtilMathRandom(0, 32766)") == 0);
  CHECK(e.getNumber("a") == 41);
  CHECK(e.getNumber("b") == 18467);
  CHECK(e.getNumber("c") == 6334);
  // hi - lo + 1 == 1 can only ever produce lo.
  REQUIRE(e.run("for i = 1, 20 do if UtilMathRandom(5, 5) ~= 5 then bad = 1 end end") == 0);
  CHECK(e.getNumber("bad") == 0);
  CHECK(e.stubs.count("UtilMathRandom") == 0);
}

TEST_CASE("Choc1: every API_NAME of api-choc1.tsv is a function after boot") {
  Choc1Env e;
  REQUIRE(e.booted);
  std::size_t n = 0;
  const char* const* names = script::choc1ApiNames(n);
  CHECK(n > 80);
  lua_State* L = e.lua.L();
  for (std::size_t i = 0; i < n; ++i) {
    lua_getglobal(L, names[i]);
    INFO(names[i]);
    CHECK(lua_isfunction(L, -1));
    lua_pop(L, 1);
  }
}

TEST_CASE("Choc1 boot: no Chocolatier 2 screen globals") {
  struct Env : test::ScriptEnv {
    Env() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ui/modal"), host(ctx) {
      ctx.module = games::findModule("choc1");
      ctx.module->attach(ctx);
      booted = host.boot();
    }
    script::ScriptHost host;
    bool booted = false;
  };
  Env e;
  REQUIRE(e.booted);
  for (const char* g : {"gScreenWidth", "gScreenHeight", "gRealXOffset", "gRealYOffset", "gRealWidth", "gRealHeight"})
    CHECK(e.isNil(g));
  CHECK_FALSE(e.host.globalIsTrue(""));
}

TEST_CASE("Choc1 users and saves: empire and free are separate slots of the current user") {
  Choc1Env e;
  REQUIRE(e.booted);
  // An in-memory store: the harness's user dir is shared with other tests, which read the current user's prefs.
  e.ctx.users = std::make_shared<sdk::UserStore>("");
  REQUIRE(e.run("CreateNewUser('Ann'); me = GetCurrentUser(); SetCurrentGameMode(1); m0 = GetCurrentUserMode(); e0 = LoadGameString('empire');"
                "SaveGameString('empire', 'return {story=1}'); f0 = LoadGameString('free'); e1 = LoadGameString('empire');"
                "SaveGameString('free', 'return {free=2}'); f1 = LoadGameString('free'); e2 = LoadGameString('empire');"
                "ChangeUserName(me, 'Bea'); n = GetUserName(me); CreateNewUser('Cy'); other = LoadGameString('empire');"
                "SetCurrentUser(me); back = LoadGameString('free')") == 0);
  CHECK(e.getString("m0") == "empire");
  CHECK(e.getString("e0") == "");
  CHECK(e.getString("f0") == "");  // saving "empire" left "free" alone
  CHECK(e.getString("e1") == "return {story=1}");
  CHECK(e.getString("f1") == "return {free=2}");
  CHECK(e.getString("e2") == "return {story=1}");  // and the other way round
  CHECK(e.getString("n") == "Bea");
  CHECK(e.getString("other") == "");  // a second user has no saves
  CHECK(e.getString("back") == "return {free=2}");
  CHECK(e.ctx.gameStateTouched);
  CHECK(e.log.contains("[save] saved free"));
  e.run("SetCurrentUser(me)");
  CHECK(e.ctx.users->hiscoreMode() == "1");  // SetCurrentGameMode stored the mode like Chocolatier 2's (in-memory store)
}
