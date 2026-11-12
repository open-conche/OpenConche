// The Great Chocolate Chase module, slice 1 (docs/design/greatcc.md section 6): detection, the profile, the table of natives,
// TSlider, scoped natives, and the fixbugs switch.
#include <doctest/doctest.h>

#include <memory>
#include <string>

#include "games/Registry.h"
#include "games/greatcc/GreatCCNatives.h"
#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/natives/ScopedNatives.h"
#include "ScriptHarness.h"
#include "ui/Window.h"

using namespace choco;

namespace {

const std::string kBoot = std::string(CHOCO_TEST_ASSETS) + "/greatcc/boot";
const std::string kByFiles = std::string(CHOCO_TEST_ASSETS) + "/greatcc/by_files";

struct GreatCCEnv : test::ScriptEnv {
  explicit GreatCCEnv(const std::string& fixbugs = "") : ScriptEnv(kBoot), host(ctx) {
    if (!fixbugs.empty()) config.override("fixbugs", fixbugs);
    ctx.module = games::findModule("greatcc");
    ctx.module->attach(ctx);
    booted = host.boot();
  }
  script::ScriptHost host;
  bool booted = false;
};

std::string detect(const std::string& dir, const std::string& forced, std::string* whyOut = nullptr) {
  assets::AssetFS a(dir, "");
  REQUIRE(a.ok());
  std::string why;
  const std::string id = games::detectGame(a, forced, why).profile().id;
  if (whyOut) *whyOut = why;
  return id;
}

}  // namespace

TEST_CASE("greatcc detection: settings.xml game=chocolateshop, or its three files without settings.xml") {
  std::string why;
  CHECK(detect(kBoot, "", &why) == "greatcc");
  CHECK(why.find("chocolateshop") != std::string::npos);
  CHECK(detect(kByFiles, "", &why) == "greatcc");  // no settings.xml: mainloop.lua + locations/defines.lua + values.xml
  CHECK(why.find("values.xml") != std::string::npos);
  CHECK(detect(std::string(CHOCO_TEST_ASSETS) + "/ui/modal", "") == "choc3");  // not claimed: the usual fallback
  CHECK(detect(std::string(CHOCO_TEST_ASSETS) + "/ui/modal", "greatcc") == "greatcc");  // --game wins
  CHECK(games::findModule("greatcc") != nullptr);
}

TEST_CASE("greatcc profile: boot data of design section 3.3") {
  const script::GameProfile& p = games::findModule("greatcc")->profile();
  CHECK(std::string(p.id) == "greatcc");
  CHECK(std::string(p.userDirName) == "GreatChocolateChase");
  CHECK(std::string(p.mainloop) == "scripts/mainloop.lua");
  CHECK(std::string(p.stringsPath) == "strings/en/strings.xml");
  CHECK(std::string(p.titleString) == "app_title");
  CHECK(std::string(p.bootCursor) == "cursor/chocolate_cursor_2");
  CHECK(p.bootCursorHotX == 0);
  CHECK(p.bootCursorHotY == 0);
  CHECK(std::string(p.closeGlobal) == "AreQuitting");
  CHECK(std::string(p.closeRequest).find("quitverify.lua") != std::string::npos);
  CHECK(std::string(p.exitSave).empty());
  CHECK(p.volumeDefaults.sfx == 750);
  // The other games keep the old defaults of the new fields.
  CHECK(std::string(games::findModule("choc3")->profile().stringsPath) == "strings.xml");
  CHECK(std::string(games::findModule("choc3")->profile().titleString).empty());
  CHECK(games::findModule("choc3")->profile().bootCursorHotX == 1);
}

TEST_CASE("greatcc boot: the strings come from strings/en and the entry script starts") {
  GreatCCEnv e;
  REQUIRE(e.booted);
  CHECK(e.ctx.strings.sdkLookup("app_title") == "Synthetic Chase");
  CHECK(e.host.mainAlive());
}

TEST_CASE("greatcc: every API_NAME of api-greatcc.tsv is a function after boot") {
  GreatCCEnv e;
  REQUIRE(e.booted);
  std::size_t n = 0;
  const char* const* names = script::greatccApiNames(n);
  CHECK(n > 100);
  lua_State* L = e.lua.L();
  for (std::size_t i = 0; i < n; ++i) {
    lua_getglobal(L, names[i]);
    INFO(names[i]);
    CHECK(lua_isfunction(L, -1));
    lua_pop(L, 1);
  }
  // The animation scripts' own globals are not in the main state.
  for (const char* g : {"getdelay", "playsound", "unlock", "drop"}) CHECK(e.isNil(g));
}

TEST_CASE("greatcc: nothing of Chocolatier's is visible to its Lua") {
  GreatCCEnv e;
  REQUIRE(e.booted);
  for (const char* g : {"LoadIngredients", "LoadProducts", "Date", "DayOfYear", "GetPortName"}) {
    INFO(g);
    CHECK(e.isNil(g));
  }
}

TEST_CASE("greatcc: DoTransition is Lua text over the real Lua_DoTransition and returns its boolean") {
  GreatCCEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("r = DoTransition(9999)") == 0);
  CHECK(e.getString("r") == "false");  // an unknown id: no overlay, no yield
  CHECK(e.stubs.count("Lua_DoTransition") == 0);  // bound for real since slice 2 (the yield itself: test_greatcc_transitions)
}

TEST_CASE("greatcc: TSlider is a second name of Slider") {
  GreatCCEnv e;
  REQUIRE(e.booted);
  auto slider = e.wm.createWindow("Slider");
  auto tslider = e.wm.createWindow("TSlider");
  CHECK(std::string(slider->typeName()) == "Slider");
  CHECK(std::string(tslider->typeName()) == "Slider");
  const ui::Window& a = *slider;
  const ui::Window& b = *tslider;
  CHECK(typeid(a) == typeid(b));
  CHECK_FALSE(e.isNil("TSlider"));  // the Lua constructor exists
  // A placeholder type stays a placeholder.
  auto menu = e.wm.createWindow("KDial");
  const ui::Window& m = *menu;
  CHECK(typeid(m) != typeid(a));
}

namespace {

int nativeSeven(lua_State* L) {
  lua_pushnumber(L, 7);
  return 1;
}

// A window that owns scoped natives, as ShopWindow and the other owners do.
class OwnerWindow : public ui::Window {
 public:
  OwnerWindow(ui::WindowManager& wm, script::ScriptContext& ctx) : ui::Window(wm), natives_(ctx) {
    natives_.bindRaw("ScopedSeven", &nativeSeven);
  }

 private:
  games::greatcc::ScopedNatives natives_;
};

}  // namespace

TEST_CASE("ScopedNatives: bound while the owner window lives, nil when it dies") {
  GreatCCEnv e;
  REQUIRE(e.booted);
  CHECK(e.isNil("ScopedSeven"));
  const size_t namesBefore = e.ctx.nativeNames.size();
  auto w = std::make_unique<OwnerWindow>(e.wm, e.ctx);
  REQUIRE(e.run("v = ScopedSeven()") == 0);
  CHECK(e.getNumber("v") == 7);
  w.reset();
  CHECK(e.isNil("ScopedSeven"));
  // A successor created before its predecessor dies keeps the name; the native-name list holds it once.
  auto a = std::make_unique<OwnerWindow>(e.wm, e.ctx);
  auto b = std::make_unique<OwnerWindow>(e.wm, e.ctx);
  a.reset();
  CHECK_FALSE(e.isNil("ScopedSeven"));
  b.reset();
  CHECK(e.isNil("ScopedSeven"));
  CHECK(e.ctx.nativeNames.size() == namesBefore + 1);
}

TEST_CASE("fixbugs: on by default, --config fixbugs=0 turns it off, the log says which") {
  {
    GreatCCEnv e;
    REQUIRE(e.booted);
    REQUIRE(games::greatcc::worldOf(e.ctx));
    CHECK(games::greatcc::worldOf(e.ctx)->settings().fixBugs);
  }
  {
    GreatCCEnv e("0");
    REQUIRE(e.booted);
    CHECK_FALSE(games::greatcc::worldOf(e.ctx)->settings().fixBugs);
  }
  {
    GreatCCEnv e("1");
    REQUIRE(e.booted);
    CHECK(games::greatcc::worldOf(e.ctx)->settings().fixBugs);
  }
  // The App copies the library's value in with set() and --config over it with override(): the override wins.
  script::Config c;
  c.set("fixbugs", "1");
  c.override("fixbugs", "0");
  c.set("fixbugs", "1");
  CHECK(c.get("fixbugs") == "0");
}
