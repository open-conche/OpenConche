#include <doctest/doctest.h>

#include <filesystem>
#include <cmath>
#include <fstream>
#include <functional>
#include <vector>
#include <string>

#include "platform/Png.h"

#include "audio/EventTable.h"
#include "games/Registry.h"
#include "ScriptHarness.h"
#include "games/common/PortWindow.h"
#include "games/common/sprite/PulseSprite.h"
#include "games/choc2/Character.h"
#include "games/choc2/Port.h"
#include "games/choc2/PortScene.h"

#include "games/choc2/Character.h"
#include "games/choc2/FrameWindow.h"
#include "games/choc2/MainMenuWindow.h"
#include "games/choc2/Port.h"
#include "games/choc2/PortScene.h"
#include "games/common/PortWindow.h"
#include "games/common/sprite/PulseSprite.h"
#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"
#include "script/LuaStyle.h"
#include "ui/widgets/InputWidgets.h"
#include "ui/widgets/Widgets.h"
#include "gfx/Curve.h"
#include "gfx/RecordingRenderer.h"
#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"
#include "script/LuaStyle.h"
#include "ui/DrawContext.h"
#include "games/choc2/Choc2Natives.h"
#include "script/Stubs.h"
#include "games/choc3/Choc3Api.h"

using namespace choco;
namespace fs = std::filesystem;

namespace {

fs::path makeTemp(const char* name) {
  fs::path p = fs::temp_directory_path() / (std::string("choco_test_game_") + name);
  fs::remove_all(p);
  fs::create_directories(p);
  return p;
}

void put(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

std::string u8(const fs::path& p) {
  const auto s = p.u8string();
  return std::string(s.begin(), s.end());
}

// A fake asset tree: settings.xml with the given <game> (none if empty and `settings` is false) plus empty marker files.
std::string detect(const char* name, const std::string& gameValue, bool settings, std::initializer_list<const char*> files,
                   const std::string& forced, std::string* whyOut = nullptr) {
  const fs::path root = makeTemp(name);
  if (settings) put(root / "settings.xml", "<settings><game>" + gameValue + "</game></settings>");
  for (const char* f : files) put(root / f, "");
  assets::AssetFS a(u8(root), "");
  REQUIRE(a.ok());
  std::string why;
  const std::string g = games::detectGame(a, forced, why).profile().id;
  if (whyOut) *whyOut = why;
  return g;
}

}  // namespace

TEST_CASE("detectGame: settings.xml game plus the per-game marker file, --game overrides") {
  std::string why;
  CHECK(detect("c3", "chocolatier3", true, {}, "", &why) == "choc3");
  CHECK(why.find("chocolatier3") != std::string::npos);
  // The marker files are ignored for chocolatier3.
  CHECK(detect("c3b", "chocolatier3", true, {"ui/uihelp.lua"}, "") == "choc3");
  CHECK(detect("c2", "chocolatier", true, {"ui/uihelp.lua"}, "", &why) == "choc2");
  CHECK(why.find("ui/uihelp.lua") != std::string::npos);
  CHECK(detect("c1", "chocolatier", true, {"data/ports.lua"}, "", &why) == "choc1");
  CHECK(why.find("data/ports.lua") != std::string::npos);
  CHECK(detect("both", "chocolatier", true, {"ui/uihelp.lua", "data/ports.lua"}, "") == "choc2");
  // Unknown: Choc3 plus a warning.
  CHECK(detect("unk", "chocolatier", true, {}, "", &why) == "choc3");
  CHECK(why.find("WARNING") != std::string::npos);
  CHECK(detect("unk2", "mystery", true, {"ui/uihelp.lua"}, "", &why) == "choc3");
  CHECK(why.find("WARNING") != std::string::npos);
  // Missing settings.xml is Choc3.
  CHECK(detect("none", "", false, {"ui/uihelp.lua"}, "") == "choc3");
  // Forced.
  CHECK(detect("f1", "chocolatier3", true, {}, "choc2", &why) == "choc2");
  CHECK(why.find("--game choc2") != std::string::npos);
  CHECK(detect("f2", "chocolatier", true, {"ui/uihelp.lua"}, "choc3") == "choc3");
}

TEST_CASE("Choc3 default user leaf: the community tree keeps Chocolatier3, the shipped content gets its own") {
  // Chocolatier 3's content sets are save-incompatible (the community tree extends the ingredient code tables from 60
  // to 100 codes; a player made with one crashes the shipped Lua under the other, sim/recipe.lua:23), so the default
  // --userdir must separate them. --userdir always overrides.
  const fs::path root = fs::temp_directory_path() / "choco_test_choc3_leaf";
  fs::remove_all(root);
  fs::create_directories(root);
  {
    put(root / "settings.xml", "<game>chocolatier3</game>");
    assets::AssetFS shipped(u8(root), "");
    CHECK(games::findModule("choc3")->defaultUserLeaf(shipped) == "Chocolatier3-Vanilla");
    CHECK(games::findModule("choc1")->defaultUserLeaf(shipped) == "Chocolatier1");  // only choc3 splits
    CHECK(games::findModule("choc2")->defaultUserLeaf(shipped) == "Chocolatier2");
  }
  fs::create_directories(root / "community");
  put(root / "community" / "account.lua", "");  // AssetFS indexes files, not directories
  {
    assets::AssetFS community(u8(root), "");
    CHECK(games::findModule("choc3")->defaultUserLeaf(community) == "Chocolatier3");  // the historical default stays
  }
  fs::remove_all(root);
}

TEST_CASE("game profiles and findModule") {
  CHECK(games::findModule("choc2") != nullptr);
  CHECK(games::findModule("choc1") != nullptr);
  CHECK(games::findModule("choc3") != nullptr);
  CHECK(games::findModule("choc4") == nullptr);
  CHECK(std::string(games::findModule("choc3")->profile().closeRequest) == "AskQuit()");
  CHECK(std::string(games::findModule("choc2")->profile().closeRequest) == "QuitGame()");
  CHECK(std::string(games::findModule("choc2")->profile().exitSave) == "if gSim then gSim:SaveGame() end");
  CHECK(std::string(games::findModule("choc3")->profile().exitSave) == "if Player then Player:SaveGame() end");
  CHECK(std::string(games::findModule("choc2")->profile().userDirName) == "Chocolatier2");
  CHECK(std::string(games::findModule("choc3")->profile().userDirName) == "Chocolatier3");
  CHECK(std::string(games::findModule("choc2")->profile().windowTitle) == "Chocolatier 2");
}

TEST_CASE("choc2 and choc1 have no event table and no save guard; choc3 keeps both") {
  // No Chocolatier 2 or 1 Lua calls SoundEvent or SaveGameString with a Chocolatier 3 Player (engine-and-games.md section 3, audioEvents and refuseSave).
  assets::AssetFS fs(CHOCO_TEST_ASSETS, "");
  CHECK(games::findModule("choc2")->makeAudioEvents(fs) == nullptr);
  CHECK(games::findModule("choc1")->makeAudioEvents(fs) == nullptr);
  CHECK(games::findModule("choc3")->makeAudioEvents(fs) != nullptr);
  // A bare Player save: enough of the shape that choc3's fingerprint matches (sdk-less copy of the test_users literal head).
  std::string why;
  const std::string bare = "return {mode=\"story\",integrity={version=1,status=\"legacy_unverified\",devSessions=0,},stats={money=0,},knownRecipes={},}";
  CHECK(games::findModule("choc3")->refuseSave(bare, why));
  CHECK(why == "Player was never initialised");
  CHECK_FALSE(games::findModule("choc2")->refuseSave(bare, why));
  CHECK_FALSE(games::findModule("choc1")->refuseSave(bare, why));
}

TEST_CASE("aliases bind the target's function under the new name; an alias of a stub stays a logged stub") {
  test::ScriptEnv e(std::string(CHOCO_TEST_ASSETS) + "/ui/modal");
  lua_State* L = e.lua.L();
  script::registerStubs(L, e.ctx);
  script::registerCoreNatives(L, e.ctx);  // Floor is real; LoadSound-like names stay stubs
  const script::AliasRow rows[] = {{"myFloor", "Floor"}, {"myStub", "NoSuchNative"}, {"mySound", "GetTimer2"}};
  CHECK(script::registerAliasRows(L, e.ctx, rows, 3) == 1);
  REQUIRE(e.run("a = myFloor(3.7)") == 0);
  CHECK(e.getNumber("a") == 3);
  // The same C function, a different closure (its name upvalue is the alias).
  REQUIRE(e.run("fa = myFloor; fb = Floor") == 0);
  lua_getglobal(L, "fa");
  lua_getglobal(L, "fb");
  CHECK(lua_tocfunction(L, -1) == lua_tocfunction(L, -2));
  lua_pop(L, 2);
  // Argument warnings name the function the script called.
  REQUIRE(e.run("myFloor({})") == 0);
  CHECK(e.log.contains("[lua-arg] myFloor arg 1"));
  // A non-real target leaves a stub under the alias's own name.
  REQUIRE(e.run("b = myStub(1)") == 0);
  CHECK(e.log.contains("[unimplemented] myStub"));
  CHECK(e.stubs.count("myStub") == 1);
}

TEST_CASE("Choc2 boot: screen globals, aliases, real Window, Frame, MainMenu, Options, HighScores") {
  struct Env : test::ScriptEnv {
    explicit Env(const char* g) : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ui/modal"), host(ctx) {
      ctx.module = games::findModule(g);
      ctx.module->attach(ctx);
      booted = host.boot();
    }
    script::ScriptHost host;
    bool booted = false;
  };
  {
    Env e("choc3");
    REQUIRE(e.booted);
    for (const char* g : {"gScreenWidth", "gScreenHeight", "gRealXOffset", "gRealYOffset", "gRealWidth", "gRealHeight"})
      CHECK(e.isNil(g));
    CHECK(e.isNil("bsgFloor"));
    CHECK(e.isNil("Frame"));  // a Chocolatier 2 class
  }
  Env e("choc2");
  REQUIRE(e.booted);
  CHECK(e.getNumber("gScreenWidth") == 800);
  CHECK(e.getNumber("gScreenHeight") == 600);
  CHECK(e.getNumber("gRealWidth") == 800);
  CHECK(e.getNumber("gRealHeight") == 600);
  CHECK(e.getNumber("gRealXOffset") == 0);
  CHECK(e.getNumber("gRealYOffset") == 0);
  CHECK(e.isNil("gChocoSplash"));
  CHECK(e.isNil("gAOLFlag"));
  REQUIRE(e.run("f = bsgFloor(2.9); d = bsgDollars(1234); m = bsgMod(7, 4); r = bsgRandom(5, 5)") == 0);
  CHECK(e.getNumber("f") == 2);
  CHECK(e.getNumber("m") == 3);
  CHECK(e.getNumber("r") == 5);
  CHECK(e.getString("d") != "<nil>");
  // Unsafe ones stay stubs; the offline no-ops do nothing; the mode is 0 until set.
  REQUIRE(e.run("bsgTriggerOffer(); bsgTriggerAd(); OpenBrowser('x'); bsgRandomSeed(); gm = GetCurrentGameMode()") == 0);
  CHECK(e.stubs.count("bsgTriggerOffer") == 0);
  CHECK(e.getNumber("gm") == 0);
  // Window and the real classes make no placeholder log line.
  REQUIRE(e.run("PushModal('t'); Window{name='w', x=1, y=2, w=30, h=40}(); MainMenu{name='mm', w=800, h=600}();"
                "Options{name='op'}(); HighScores{name='hs'}(); Frame{name='fr', x=3, y=4, w=100, h=50}(); Port{name='po'}()") == 0);
  CHECK_FALSE(e.log.contains("[unimplemented-type] Window"));
  for (const char* t : {"Frame", "MainMenu", "Options", "HighScores", "Port"})
    CHECK_FALSE(e.log.contains(std::string("[unimplemented-type] ") + t));
  CHECK_FALSE(e.log.contains("[unimplemented-type] Port"));
  const ui::Window* top = e.wm.topModal();
  REQUIRE(top->find("mm"));
  CHECK(top->find("mm")->typeName() == "MainMenu");
  CHECK(top->find("mm")->rect() == gfx::RectI{0, 0, 800, 600});
  CHECK(top->find("fr")->typeName() == "Frame");
  CHECK(top->find("fr")->rect() == gfx::RectI{3, 4, 100, 50});
  CHECK(top->find("w")->typeName() == "Window");
  CHECK(top->find("w")->rect() == gfx::RectI{1, 2, 30, 40});
}

TEST_CASE("Frame layout: window coordinates, edges cut at the window, ornament bottom-aligned, fill = window rect") {
  using Image = game::choc2::FramePiece::Image;
  using game::choc2::FramePiece;
  // The Chocolatier 2 default style's images: corner 19x15, left 19x64, top 64x15, ornament 128x33.
  const auto p = game::choc2::layoutFrame(400, 200, {19, 15}, {19, 64}, {64, 15}, {128, 33});
  // Top/bottom tiles start at window x 0 and continue while the start is <= 400 (0..384: 7 tiles); left/right from y 0 while
  // <= 200 (0, 64, 128, 192: 4 tiles); 4 corners and the ornament.
  REQUIRE(p.size() == 7 * 2 + 4 * 2 + 4 + 1);
  CHECK(p[0] == FramePiece{Image::Top, {0, -15, 64, 15}, false, false, {0, -15, 400, 15}});
  CHECK(p[6] == FramePiece{Image::Top, {384, -15, 64, 15}, false, false, {0, -15, 400, 15}});  // overruns x = 400: clipped there
  CHECK(p[7] == FramePiece{Image::Top, {0, 200, 64, 15}, false, true, {0, 200, 400, 15}});
  CHECK(p[14] == FramePiece{Image::Left, {-19, 0, 19, 64}, false, false, {-19, 0, 19, 200}});
  CHECK(p[17] == FramePiece{Image::Left, {-19, 192, 19, 64}, false, false, {-19, 0, 19, 200}});
  CHECK(p[18] == FramePiece{Image::Left, {400, 0, 19, 64}, true, false, {400, 0, 19, 200}});
  CHECK(p[22] == FramePiece{Image::Corner, {-19, -15, 19, 15}, false, false});
  CHECK(p[23] == FramePiece{Image::Corner, {400, -15, 19, 15}, true, false});
  CHECK(p[24] == FramePiece{Image::Corner, {-19, 200, 19, 15}, false, true});
  CHECK(p[25] == FramePiece{Image::Corner, {400, 200, 19, 15}, true, true});
  CHECK(p[26].image == Image::Ornament);
  // INFERENCE (origin): the ornament's bottom touches the window's top edge: it occupies the 33 px above it, centred in the
  // outer width 438.
  CHECK(p[26].rect == gfx::RectI{-19 + (438 - 128) / 2, -33, 128, 33});
  CHECK(game::choc2::frameFillRect(400, 200) == gfx::RectI{0, 0, 400, 200});
  // Rollover style: 4 px images, no ornament: 100x50: tiles at x = 0, 64 (2+2), y = 0 (1+1), 4 corners.
  const auto r = game::choc2::layoutFrame(100, 50, {4, 4}, {4, 64}, {64, 4}, {0, 0});
  CHECK(r.size() == 2 + 2 + 1 + 1 + 4);
  for (const auto& piece : r) CHECK(piece.image != Image::Ornament);
  CHECK(game::choc2::layoutFrame(100, 50, {}, {}, {}, {}).empty());
}

TEST_CASE("Frame draws its fill (the window rect) unless empty") {
  using Kind = gfx::RecordingRenderer::Call::Kind;
  auto fills = [](bool empty) {
    test::ScriptEnv e(std::string(CHOCO_TEST_ASSETS) + "/ui/modal");
    e.ctx.module = games::findModule("choc2");
    e.ctx.module->attach(e.ctx);
    script::ScriptHost host(e.ctx);
    REQUIRE(host.boot());
    REQUIRE(e.run(std::string("PushModal('t'); Frame{name='fr', x=10, y=20, w=100, h=60, color={1,0,0,1}, empty=") +
                  (empty ? "true" : "false") + "}()") == 0);
    auto* f = dynamic_cast<game::choc2::FrameWindow*>(e.wm.topModal()->find("fr"));
    REQUIRE(f);
    CHECK(f->empty() == empty);
    gfx::RecordingRenderer rr;
    e.wm.draw(rr);
    std::vector<gfx::RectF> out;
    for (const auto& c : rr.calls())
      if (c.kind == Kind::FillRect) out.push_back(c.dst);
    return out;
  };
  const auto filled = fills(false);
  REQUIRE(filled.size() == 1);
  CHECK(filled[0] == gfx::RectF{10, 20, 100, 60});  // exactly the window rect
  CHECK(fills(true).empty());
}

TEST_CASE("Choc2 SetCurrentGameMode stores any mode; Chocolatier 3's stores only 0") {
  struct Env : test::ScriptEnv {
    explicit Env(const char* g) : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ui/modal"), host(ctx) {
      ctx.module = games::findModule(g);
      ctx.module->attach(ctx);
      booted = host.boot();
    }
    script::ScriptHost host;
    bool booted = false;
  };
  Env e("choc2");
  REQUIRE(e.booted);
  REQUIRE(e.run("CreateNewUser('Tester'); m0 = GetCurrentGameMode(); SetCurrentGameMode(1); m1 = GetCurrentGameMode();"
                "SetCurrentGameMode(0); m2 = GetCurrentGameMode(); SetCurrentGameMode(2); m3 = GetCurrentGameMode()") == 0);
  CHECK(e.getNumber("m0") == 0);
  CHECK(e.getNumber("m1") == 1);
  CHECK(e.getNumber("m2") == 0);
  CHECK(e.getNumber("m3") == 2);
  // bsgDollars truncates (Dollars rounds); bsgTimeStamp has the strftime shape.
  REQUIRE(e.run("d1 = bsgDollars(1234.9); d2 = Dollars(1234.9); ts = bsgTimeStamp()") == 0);
  CHECK(e.getString("d1") == "$1,234");
  CHECK(e.getString("d2") == "$1,235");
  const std::string ts = e.getString("ts");
  CHECK(ts.size() == 19);
  CHECK((ts[4] == '.' && ts[7] == '.' && ts[10] == '-' && ts[13] == '.' && ts[16] == '.'));
  Env c3("choc3");
  REQUIRE(c3.booted);
  REQUIRE(c3.run("CreateNewUser('T'); SetCurrentGameMode(1)") == 0);
  CHECK(c3.isNil("GetCurrentGameMode"));
}

TEST_CASE("Choc2: every API_NAME of api-choc2.tsv is a function after boot") {
  test::ScriptEnv e(std::string(CHOCO_TEST_ASSETS) + "/ui/modal");
  e.ctx.module = games::findModule("choc2");
  e.ctx.module->attach(e.ctx);
  script::ScriptHost host(e.ctx);
  REQUIRE(host.boot());
  std::size_t n = 0;
  const char* const* names = script::choc2ApiNames(n);
  CHECK(n > 50);
  lua_State* L = e.lua.L();
  for (std::size_t i = 0; i < n; ++i) {
    lua_getglobal(L, names[i]);
    INFO(names[i]);
    CHECK(lua_isfunction(L, -1));
    lua_pop(L, 1);
  }
}

namespace {

struct Choc2Env : test::ScriptEnv {
  Choc2Env() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ui/modal"), host(ctx) {
    ctx.module = games::findModule("choc2");
    ctx.module->attach(ctx);
    booted = host.boot();
  }
  script::ScriptHost host;
  bool booted = false;
};

// A MainMenu as ui/mainmenu.lua builds it: the full-screen default button (hidden by the Lua) holding the crawl text.
const char* kIntroTree =
    "fired = 0; PushModal('t');"
    "MainMenu{x=0, y=0, w=800, h=600,"
    "  Button{name='intro_button', x=0, y=0, w=800, h=600, graphics={}, default=true, command=function() fired = fired + 1 end,"
    "    Text{name='intro_text', x=100, y=0, w=600, h=500, label='crawl'}}}();"
    "EnableWindow('intro_button', false)";

// Steps the window animations from `from` to `to` ms in 20 ms frames (a frame runs at most a few due ticks).
void advance(Choc2Env& e, double from, double to) {
  for (double t = from; t <= to; t += 20) e.wm.tickWindowAnimations(t);
}

}  // namespace

namespace {

// ui/mainmenu.lua's shape: the MainMenu and its siblings (`splash_title`, the button windows) under one parent bitmap.
const char* kIntroFullTree =
    "fired = 0; PushModal('t');"
    "Window{x=0, y=0, w=800, h=600,"
    "  MainMenu{x=0, y=0, w=800, h=600,"
    "    Button{name='intro_button', x=0, y=0, w=800, h=600, graphics={}, default=true,"
    "      command=function() fired = fired + 1 end, Text{name='intro_text', x=100, y=0, w=600, h=500, label='crawl'}}},"
    "  Text{name='splash_title', x=0, y=0, w=100, h=20, label='title'},"
    "  Window{name='fade_buttons', x=0, y=0, w=800, h=600, Text{name='fb', x=0, y=0, w=100, h=20, label='x'}}}();"
    "EnableWindow('intro_button', false)";

}  // namespace

TEST_CASE("DoIntro: fade out 1 s, text in 1 s, hold 22 s, out 2 s, then the command once; the text never moves") {
  using game::choc2::MainMenuWindow;
  Choc2Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run(kIntroFullTree) == 0);
  ui::Window* button = e.wm.topModal()->find("intro_button");
  ui::Window* text = e.wm.topModal()->find("intro_text");
  ui::Window* title = e.wm.topModal()->find("splash_title");
  ui::Window* fb = e.wm.topModal()->find("fb");
  REQUIRE((button && text && title && fb));
  auto* mm = dynamic_cast<MainMenuWindow*>(button->parent());
  REQUIRE(mm);
  CHECK_FALSE(button->shown());
  e.wm.setNowMs(1000);
  REQUIRE(e.run("DoIntro()") == 0);
  CHECK_FALSE(e.log.contains("[native-warning] DoIntro"));
  CHECK(mm->state() == MainMenuWindow::State::FadeOut);
  const int y0 = text->rect().y;
  // State 1: everything under the bitmap (the title and the button art too) fades out over 1000 ms; the button stays hidden.
  advance(e, 1020, 1500);
  CHECK(title->alpha() == doctest::Approx(0.5f).epsilon(0.1));
  CHECK(fb->alpha() == doctest::Approx(0.5f).epsilon(0.1));
  CHECK_FALSE(button->shown());
  advance(e, 1500, 2100);
  CHECK(title->alpha() == 0.0f);
  CHECK(button->shown());
  CHECK(mm->state() == MainMenuWindow::State::FadeIn);
  // State 2: the text fades in over 1000 ms (the title stays gone).
  advance(e, 2100, 2500);
  CHECK(text->alpha() == doctest::Approx(0.4f).epsilon(0.2));
  CHECK(title->alpha() == 0.0f);
  advance(e, 2500, 3200);
  CHECK(text->alpha() == 1.0f);
  CHECK(mm->state() == MainMenuWindow::State::Hold);
  // State 3: static for 22 s.
  advance(e, 3200, 24000);
  CHECK(mm->state() == MainMenuWindow::State::Hold);
  CHECK(text->alpha() == 1.0f);
  CHECK(text->rect().y == y0);
  CHECK(text->rect().x == 100);
  advance(e, 24000, 25200);
  CHECK(mm->state() == MainMenuWindow::State::FadeEnd);
  // State 4 (from about 25040): out over 2000 ms, then the command, once. In total 1 + 1 + 22 + 2 s from DoIntro.
  advance(e, 25200, 26040);
  CHECK(text->alpha() == doctest::Approx(0.5f).epsilon(0.15));
  CHECK(e.getNumber("fired") == 0);
  advance(e, 26040, 1000 + MainMenuWindow::kDurationMs - 200);
  CHECK(e.getNumber("fired") == 0);
  advance(e, 1000 + MainMenuWindow::kDurationMs - 200, 1000 + MainMenuWindow::kDurationMs + 200);
  CHECK(e.getNumber("fired") == 1);
  CHECK(text->rect().y == y0);
  advance(e, 1000 + MainMenuWindow::kDurationMs + 200, 1000 + 2 * MainMenuWindow::kDurationMs);
  CHECK(e.getNumber("fired") == 1);
}

TEST_CASE("DoIntro: the intro pauses under a dialog and resumes with it") {
  Choc2Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run(kIntroFullTree) == 0);
  ui::Window* title = e.wm.topModal()->find("splash_title");
  REQUIRE(title);
  e.wm.setNowMs(0);
  REQUIRE(e.run("DoIntro()") == 0);
  advance(e, 20, 500);
  const float a = title->alpha();
  CHECK(a < 1.0f);
  REQUIRE(e.run("PushModal('dlg')") == 0);
  advance(e, 520, 90000);
  CHECK(title->alpha() == a);  // 90 s of dialog time did not move the fade
  CHECK(e.getNumber("fired") == 0);
  e.wm.popModal("dlg");
  advance(e, 90020, 92000);
  CHECK(title->alpha() == 0.0f);  // it resumes
  CHECK(e.getNumber("fired") == 0);
}

TEST_CASE("MainMenu art: beam angle is a triangle wave over the clock and swings to `up` during the intro fade") {
  using game::choc2::MainMenuWindow;
  const auto& b = MainMenuWindow::kBeamTable[0];  // pivot (790,560), 290..355, up 355, half period 5400
  CHECK(MainMenuWindow::beamAngle(b, 0, 0) == doctest::Approx(290));
  CHECK(MainMenuWindow::beamAngle(b, 5400, 0) == doctest::Approx(355));
  CHECK(MainMenuWindow::beamAngle(b, 2700, 0) == doctest::Approx(322.5));
  CHECK(MainMenuWindow::beamAngle(b, 10800, 0) == doctest::Approx(290));  // folded back
  CHECK(MainMenuWindow::beamAngle(b, 8100, 0) == doctest::Approx(322.5));
  CHECK(MainMenuWindow::beamAngle(b, 0, 0.5) == doctest::Approx(322.5));  // halfway from 290 to up
  CHECK(MainMenuWindow::beamAngle(b, 0, 1) == doctest::Approx(355));
  CHECK(MainMenuWindow::beamAngle(MainMenuWindow::kBeamTable[2], 123, 1) == doctest::Approx(10));
  CHECK(MainMenuWindow::blimpX(0, 800) == doctest::Approx(-250));
  CHECK(MainMenuWindow::blimpX(12500, 800) == doctest::Approx(0.5 * 1300 - 250));
  CHECK(MainMenuWindow::blimpX(25000, 800) == doctest::Approx(-250));
}

TEST_CASE("DoIntro: Enter on the default button ends it without a second command at the end") {
  using game::choc2::MainMenuWindow;
  Choc2Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run(kIntroTree) == 0);
  e.wm.setNowMs(0);
  REQUIRE(e.run("DoIntro()") == 0);
  advance(e, 20, 1100);  // the button is shown at the end of the first fade
  ui::InputEvent ev;
  ev.type = ui::InputEvent::Type::Key;
  ev.key.name = "Return";
  e.wm.handleInput(ev);
  CHECK(e.getNumber("fired") == 1);
  e.wm.dispatchMessages();  // the button's kButtonPress reaches the MainMenu: the intro is over
  auto* mm = dynamic_cast<MainMenuWindow*>(e.wm.topModal()->find("intro_button")->parent());
  REQUIRE(mm);
  CHECK_FALSE(mm->introRunning());
  advance(e, 20, 40000);  // the command did not leave the screen: the end of the crawl must not run it again
  CHECK(e.getNumber("fired") == 1);
}

TEST_CASE("DoIntro: a click ends it early, and a window destroyed mid-intro cancels the task") {
  Choc2Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run(kIntroTree) == 0);
  e.wm.setNowMs(0);
  REQUIRE(e.run("DoIntro()") == 0);
  advance(e, 20, 1100);
  auto* button = dynamic_cast<ui::ButtonWindow*>(e.wm.topModal()->find("intro_button"));
  REQUIRE(button);
  button->press();  // a click (or Enter on the default button)
  CHECK(e.getNumber("fired") == 1);
  // The command leaves for the port in the real Lua; here the menu is destroyed instead.
  ui::Window* mm = button->parent();
  REQUIRE(mm->typeName() == "MainMenu");
  e.wm.destroyWindow(*mm);
  advance(e, 20, 40000);
  CHECK(e.getNumber("fired") == 1);  // the task did not fire the command again
  // With no MainMenu in the top modal DoIntro says so instead of doing nothing silently.
  REQUIRE(e.run("DoIntro()") == 0);
  CHECK(e.log.contains("[native-warning] DoIntro"));
}

TEST_CASE("Choc2 saves: one slot per user and mode; bsgNextSaveFileName is empty") {
  Choc2Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("CreateNewUser('Tester'); SetCurrentGameMode(0); bsgSaveGameString('story'); SetCurrentGameMode(1);"
                "free0 = bsgLoadGameString(); bsgSaveGameString('free'); free1 = bsgLoadGameString();"
                "SetCurrentGameMode(0); story = bsgLoadGameString(); same = LoadGameString(); nx = bsgNextSaveFileName()") == 0);
  CHECK(e.getString("free0") == "");  // a Free Play game does not see (or overwrite) the Story save
  CHECK(e.getString("free1") == "free");
  CHECK(e.getString("story") == "story");
  CHECK(e.getString("same") == "story");  // mode 0 is the Chocolatier 3 save slot
  CHECK(e.getString("nx") == "");
}

// ---------------------------------------------------------------- the port scene, buildings and characters

namespace {

using game::PathSprite;
using game::choc2::AddedSprite;

std::string portAssets() {
  const fs::path dir = fs::temp_directory_path() / "choco_test_choc2_port";
  fs::remove_all(dir);
  auto png = [&](const char* name, int w, int h) {
    const fs::path p = dir / name;
    fs::create_directories(p.parent_path());
    std::vector<unsigned char> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 255);
    REQUIRE(choco::platform::writePng(p.string(), w, h, px.data()));
  };
  put(dir / "ui/mainloop.lua", "function Main() while true do WaitForMessage() end end\nreturn Main\n");
  png("ports/cloud.png", 800, 20);
  png("ports/bld_a.png", 60, 40);
  png("ports/bld_b.png", 60, 40);
  png("characters/kay.png", 172, 240);
  png("characters/kay_displeased.png", 100, 80);
  png("characters/kay_neutral.png", 100, 80);
  png("characters/kay_pleased.png", 100, 80);
  png("characters/solo.png", 172, 240);
  png("characters/half.png", 172, 240);
  png("characters/half_pleased.png", 100, 80);
  return u8(dir);
}

struct PortEnv : test::ScriptEnv {
  PortEnv() : ScriptEnv(portAssets()), textures(fs, nullptr), sheets(fs, textures), host(ctx) {
    ctx.module = games::findModule("choc2");
    ctx.module->attach(ctx);
    wm.setLegacyPopupFader(true);
    wm.setTextures(&textures);
    wm.setSheets(&sheets);
    booted = host.boot();
  }
  game::choc2::Port* port() {
    for (const auto& c : wm.topModal()->children())
      if (auto* p = dynamic_cast<game::choc2::Port*>(c.get())) return p;
    return nullptr;
  }
  gfx::TextureCache textures;
  gfx::SheetCache sheets;
  script::ScriptHost host;
  bool booted = false;
};

// fromAddSprite over a Lua table literal.
AddedSprite adapt(test::ScriptEnv& e, const std::string& table, int order, int imageWidth = 0) {
  lua_State* L = e.lua.L();
  const int top = lua_gettop(L);
  const std::string code = "return " + table;
  REQUIRE(luaL_loadbuffer(L, code.data(), code.size(), "=table") == 0);
  REQUIRE(lua_pcall(L, 0, 1, 0) == 0);
  AddedSprite out;
  {
    const script::LuaStyle view = script::LuaStyle::plainView(L, lua_gettop(L), &e.lua);
    out = game::choc2::fromAddSprite(view, order, [imageWidth](const std::string&) { return imageWidth; });
  }
  lua_settop(L, top);
  return out;
}

}  // namespace

TEST_CASE("fromAddSprite: a cloud strip, a bouncing car with a sheet, perspective keys and a still sprite") {
  test::ScriptEnv e(std::string(CHOCO_TEST_ASSETS) + "/ui/modal");
  // The cairo cloud: loop, htile = true (the image's width, 800), a 4-point path.
  AddedSprite cloud = adapt(e, "{image='ports/cloud', x=400, y=60, path={{-100,60},{300,60},{500,60},{900,60}}, time=200000, loop=true, htile=true}", 3, 800);
  REQUIRE(cloud.kind == AddedSprite::Kind::Sprite);
  const game::SpriteDef& c = cloud.sprite;
  CHECK(c.image == "ports/cloud");
  CHECK(c.hasPath);
  CHECK(c.path.size() == 4);
  CHECK(c.path[3].x == 900);
  CHECK(c.mode == PathSprite::Mode::Loop);
  CHECK(c.htile == 800);
  CHECK(c.frequency == 100);
  CHECK(c.random);
  CHECK(c.hold == 0);
  CHECK(c.time == 200000);
  CHECK(c.layer == 3);  // the call order
  CHECK(c.x == 400);    // x, y are the centre, as in Chocolatier 3
  CHECK(c.scale == -1);
  CHECK(c.headingFrames);
  CHECK(cloud.notes.empty());

  // The cairo car: bounce, hold, scale and a sheet (a walk cycle, so no heading frames).
  AddedSprite car = adapt(e, "{image='ports/car_01.xml', x=10, y=20, path={{0,0},{1,0},{2,0},{3,0}}, time=16000, bounce=true, hold=4000, scale=.6}", 4);
  REQUIRE(car.kind == AddedSprite::Kind::Sprite);
  CHECK(car.sprite.mode == PathSprite::Mode::Bounce);
  CHECK(car.sprite.hold == 4000);
  CHECK(car.sprite.scale == doctest::Approx(0.6));
  CHECK_FALSE(car.sprite.headingFrames);
  CHECK(car.sprite.htile == 0);

  // Perspective: far, near, farscale and nearscale are renamed; unset ones stay unset (the PortDef defaults apply).
  AddedSprite persp = adapt(e, "{image='ports/car_07', path={{0,0},{1,0},{2,0},{3,0}}, time=9000, far=200, near=600, farscale=.4}", 5);
  CHECK(*persp.sprite.yFar == 200);
  CHECK(*persp.sprite.yNear == 600);
  CHECK(*persp.sprite.scaleFar == doctest::Approx(0.4));
  CHECK_FALSE(persp.sprite.scaleNear.has_value());

  // A still sprite: no path, scale 1.
  AddedSprite still = adapt(e, "{image='ports/sf_bridge_mask', x=155, y=173}", 6);
  CHECK_FALSE(still.sprite.hasPath);
  CHECK(still.sprite.scale == 1);
}

TEST_CASE("fromAddSprite: static sprites get the near/far perspective too") {
  PortEnv e;
  REQUIRE(e.booted);
  // Defaults near 400, far 0: halfway down the range, halfway between the scales.
  CHECK(adapt(e, "{image='ports/a', x=1, y=200, farscale=.2, nearscale=1}", 0).sprite.scale == doctest::Approx(0.6));
  // Explicit near/far win over `scale`, which is only the default of the one not given.
  CHECK(adapt(e, "{image='ports/a', x=1, y=200, scale=.3, farscale=.2, nearscale=1}", 0).sprite.scale == doctest::Approx(0.6));
  CHECK(adapt(e, "{image='ports/a', x=1, y=100, scale=.5, nearscale=1, far=0, near=200}", 0).sprite.scale == doctest::Approx(0.75));
  // Equal scales: that scale, wherever the sprite is. Only `scale`: unchanged.
  CHECK(adapt(e, "{image='ports/a', x=1, y=900, scale=.4}", 0).sprite.scale == doctest::Approx(0.4));
  CHECK(adapt(e, "{image='ports/a', x=1, y=900, nearscale=.7, farscale=.7}", 0).sprite.scale == doctest::Approx(0.7));
  // Clamped at 0 on the far side, not clamped above.
  CHECK(adapt(e, "{image='ports/a', x=1, y=-400, farscale=.2, nearscale=1}", 0).sprite.scale == doctest::Approx(0));
  CHECK(adapt(e, "{image='ports/a', x=1, y=800, farscale=.2, nearscale=1}", 0).sprite.scale == doctest::Approx(1.8));
  // The Port's offset is not part of the formula: it is added after (Port::addSprite).
  CHECK(adapt(e, "{image='ports/a', x=1, y=0, farscale=.2, nearscale=1, far=0, near=0}", 0).sprite.scale == doctest::Approx(1));
}

TEST_CASE("fromAddSprite: a path sprite without time gets the default and one note; .LOGO is skipped") {
  test::ScriptEnv e(std::string(CHOCO_TEST_ASSETS) + "/ui/modal");
  AddedSprite a = adapt(e, "{image='ports/car_07', path={{0,0},{1,0},{2,0},{3,0}}}", 0);
  REQUIRE(a.kind == AddedSprite::Kind::Sprite);
  CHECK(a.sprite.time == 10000);
  CHECK(a.notes.empty());
  CHECK(a.noteKey == "ports/car_07");

  // `wait` is not read; `hflip` is.
  AddedSprite ignored = adapt(e, "{image='ports/waterfall.xml', x=1, y=2, hflip=true, wait=20000}", 0);
  CHECK(ignored.notes.size() == 1);
  CHECK(ignored.sprite.hflip);

  AddedSprite logo = adapt(e, "{image='.LOGO', x=1, y=2, contents='PlayerLogoRollover()', command=function() end}", 1);
  CHECK(logo.kind == AddedSprite::Kind::Skipped);
  REQUIRE(logo.notes.size() == 1);
  CHECK(logo.notes[0].find(".LOGO") != std::string::npos);
}

TEST_CASE("fromAddSprite: buildings get the perspective scale of TPort::Draw too") {
  // docs/re/choc2-port-binary.md 3.1 (FACT): the perspective block runs for every sprite with a texture, buildings
  // included, and `scale` is only the default of nearscale/farscale (an explicit `scale` is honoured, against the old
  // engine's hard-coded 1). No shipped building uses the keys - the scan of all port layouts found name/character/
  // ambient/x/y/labelx/labely only - so this is faithfulness, not a shipped-data change.
  test::ScriptEnv e(std::string(CHOCO_TEST_ASSETS) + "/ui/modal");
  const std::string TABLE = "contents='NameRollover()'";  // any contents string makes the table a building
  // Defaults: no keys at all - scale 1, as every shipped building.
  CHECK(adapt(e, "{image='ports/sf_market', x=100, y=212, " + TABLE + ", command=function() end}", 0)
            .building.scale == doctest::Approx(1.0));
  // An explicit scale wins over the old hard-coded 1 (the default of both ends).
  CHECK(adapt(e, "{image='ports/sf_market', x=100, y=212, scale=.5, " + TABLE + ", command=function() end}", 0)
            .building.scale == doctest::Approx(0.5));
  // The perspective formula, the same one as the static sprites (defaults near 400, far 0).
  CHECK(adapt(e, "{image='ports/sf_market', x=100, y=200, farscale=.2, nearscale=1, " + std::string(TABLE) +
                     ", command=function() end}", 0)
            .building.scale == doctest::Approx(0.6));
  // Clamped at 0 below, unclamped above, explicit near/far honoured.
  CHECK(adapt(e, "{image='ports/sf_market', x=100, y=-400, farscale=.2, nearscale=1, " + std::string(TABLE) +
                     ", command=function() end}", 0)
            .building.scale == doctest::Approx(0));
  CHECK(adapt(e, "{image='ports/sf_market', x=100, y=100, scale=.5, nearscale=1, far=0, near=200, " + std::string(TABLE) +
                     ", command=function() end}", 0)
            .building.scale == doctest::Approx(0.75));
}

TEST_CASE("fromAddSprite: a table with command and contents is a building") {
  test::ScriptEnv e(std::string(CHOCO_TEST_ASSETS) + "/ui/modal");
  AddedSprite b = adapt(e,
      "{image='ports/sf_market', x=575, y=194, labelx=525, labely=185, contents=\"(LBuilding:ByName('sf_market')):NameRollover()\","
      " command=function() end}", 9);
  REQUIRE(b.kind == AddedSprite::Kind::Building);
  CHECK(b.building.name == "sf_market");
  CHECK(b.building.image == "ports/sf_market");
  CHECK(b.building.x == 575);
  CHECK(b.building.layer == 9);
  CHECK(*b.building.labelX == 525);
  CHECK(*b.building.labelY == 185);
  CHECK(b.building.contents.find("NameRollover") != std::string::npos);
  CHECK(b.building.command.valid());
  // No ByName in the contents: the image's name.
  AddedSprite n = adapt(e, "{image='ports/sf_shop.xml', x=1, y=2, command=function() end}", 0);
  CHECK(n.building.name == "sf_shop");
}

TEST_CASE("pathPhase: four phases with a hold; with no hold it is the old formula") {
  using M = PathSprite::Mode;
  // Loop, 1000 ms run + 500 ms hold.
  CHECK(PathSprite::pathPhase(0, 1000, 500, M::Loop) == doctest::Approx(0));
  CHECK(PathSprite::pathPhase(500, 1000, 500, M::Loop) == doctest::Approx(0.5));
  CHECK(PathSprite::pathPhase(1000, 1000, 500, M::Loop) == doctest::Approx(1));
  CHECK(PathSprite::pathPhase(1499, 1000, 500, M::Loop) == doctest::Approx(1));     // parked at the end
  CHECK(PathSprite::pathPhase(1750, 1000, 500, M::Loop) == doctest::Approx(0.25));  // the next run
  // Bounce: forward, park at the end, back, park at the start (period 3000).
  CHECK(PathSprite::pathPhase(500, 1000, 500, M::Bounce) == doctest::Approx(0.5));
  CHECK(PathSprite::pathPhase(1250, 1000, 500, M::Bounce) == doctest::Approx(1));
  CHECK(PathSprite::pathPhase(2000, 1000, 500, M::Bounce) == doctest::Approx(0.5));  // on the way back
  CHECK(PathSprite::pathPhase(2750, 1000, 500, M::Bounce) == doctest::Approx(0));    // parked at the start
  CHECK(PathSprite::pathPhase(3500, 1000, 500, M::Bounce) == doctest::Approx(0.5));  // the cycle repeats
  CHECK(PathSprite::pathPhase(5, 0, 500, M::Loop) == 0);                             // no duration stays at the start
  // hold = 0: exactly the previous arithmetic at 20 sample times.
  for (int i = 0; i < 20; ++i) {
    const float t = 137.0f * static_cast<float>(i) + 11, d = 1000;
    float bounce = std::fmod(t / d, 2.0f);
    if (bounce >= 1.0f) bounce = 2.0f - bounce;
    CHECK(PathSprite::pathPhase(t, d, 0, M::Loop) == std::fmod(t / d, 1.0f));
    CHECK(PathSprite::pathPhase(t, d, 0, M::Bounce) == bounce);
  }
}

TEST_CASE("Choc2 path timing: bounce is a round trip in one `time`; hold is a wait at the start of each cycle") {
  using P = PathSprite;
  // Plain loop, time 1000, hold 250 (period 1250): waits at the start first.
  CHECK(P::choc2Phase(0, 1000, 250, false) == doctest::Approx(0));
  CHECK(P::choc2Phase(249, 1000, 250, false) == doctest::Approx(0));
  CHECK(P::choc2Phase(750, 1000, 250, false) == doctest::Approx(0.5));
  CHECK(P::choc2Phase(1249, 1000, 250, false) == doctest::Approx(0.999));
  CHECK(P::choc2Phase(1250 + 750, 1000, 250, false) == doctest::Approx(0.5));  // the next cycle waits again
  // Bounce: out in time/2, back in time/2, then the hold.
  CHECK(P::choc2Phase(250, 1000, 0, true) == doctest::Approx(0.5));
  CHECK(P::choc2Phase(500, 1000, 0, true) == doctest::Approx(1));
  CHECK(P::choc2Phase(750, 1000, 0, true) == doctest::Approx(0.5));
  CHECK(P::choc2Phase(999, 1000, 0, true) == doctest::Approx(0).epsilon(0.01));
  CHECK(P::choc2Phase(1250, 1000, 250, true) == doctest::Approx(0));
  CHECK(P::choc2Phase(5, 0, 0, true) == 0);

  PathSprite p;
  p.choc2Timing = true;
  p.setCurve(gfx::Curve::fromPoints({{0, 0}, {100, 0}, {200, 0}, {300, 0}}), 1000, PathSprite::Mode::Bounce);
  for (int i = 0; i < 10; ++i) p.update(50);  // 500 ms: the far end already
  CHECK(p.pos.x == doctest::Approx(300).epsilon(0.01));
  for (int i = 0; i < 10; ++i) p.update(50);  // 1000 ms: home again
  CHECK(p.pos.x == doctest::Approx(0).epsilon(0.05));
}

TEST_CASE("Choc2 perspective: explicit nearscale/farscale win over scale; scale defaults them; hflip is kept; clamped at 0") {
  test::ScriptEnv e(std::string(CHOCO_TEST_ASSETS) + "/ui/modal");
  AddedSprite air = adapt(e, "{image='ports/airship', path={{0,0},{1,0},{2,0},{3,0}}, scale=.3, nearscale=1, farscale=.2}", 0);
  CHECK(*air.sprite.scaleNear == doctest::Approx(1));
  CHECK(*air.sprite.scaleFar == doctest::Approx(0.2));
  AddedSprite plain = adapt(e, "{image='ports/car', path={{0,0},{1,0},{2,0},{3,0}}, scale=.5}", 0);
  CHECK(*plain.sprite.scaleNear == doctest::Approx(0.5));
  CHECK(*plain.sprite.scaleFar == doctest::Approx(0.5));
  CHECK(plain.sprite.choc2);
  AddedSprite none = adapt(e, "{image='ports/car', path={{0,0},{1,0},{2,0},{3,0}}}", 0);
  CHECK_FALSE(none.sprite.scaleNear.has_value());  // the PortDef default (1)

  PathSprite p;
  p.choc2Timing = true;
  p.forceHflip = true;
  p.setCurve(gfx::Curve::fromPoints({{0, -100}, {0, -100}, {0, -100}, {0, -100}}), 1000, PathSprite::Mode::Loop);
  p.setScaling(0, 400, 0, 1);
  p.update(50);
  CHECK(p.scale == 0);  // y = -100 is above far: the linear value is negative, clamped
  CHECK(p.hflip);
}

TEST_CASE("PathSprite: a hold parks the sprite at the end, and a walk-cycle sheet keeps its own frame") {
  PathSprite p;
  p.setCurve(gfx::Curve::fromPoints({{0, 0}, {100, 0}, {200, 0}, {300, 0}}), 1000, PathSprite::Mode::Loop);
  p.setHold(500);
  for (int i = 0; i < 20; ++i) p.update(50);  // 1000 ms: the end
  CHECK(p.pos.x == doctest::Approx(300));
  for (int i = 0; i < 9; ++i) p.update(50);  // 1450 ms: still parked
  CHECK(p.pos.x == doctest::Approx(300));
  for (int i = 0; i < 3; ++i) p.update(50);  // 1600 ms: the second run has begun
  CHECK(p.pos.x < 100);

  auto sheet = std::make_shared<gfx::SpriteSheet>();
  sheet->frames.resize(3);
  PathSprite heading, walker;
  for (PathSprite* s : {&heading, &walker}) {
    s->sheet = sheet;
    s->setCurve(gfx::Curve::fromPoints({{0, 0}, {100, 0}, {200, 0}, {300, 0}}), 1000, PathSprite::Mode::Loop);
  }
  walker.headingFrames = false;
  heading.update(50);
  walker.update(50);
  CHECK(heading.frame == 1);  // facing right: a heading frame
  CHECK(walker.frame == 0);   // its own script plays instead
}

TEST_CASE("Choc2 Port: bsgAddSprite builds the scene; a click runs the command; hover opens and closes the contents popup") {
  PortEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run(
      "gClicks = 0; gClicksB = 0; PushModal('t');"
      "Port{ x=0, y=0, w=800, h=600, xOffset=0, yOffset=0,"
      "  Group({"
      "    function() bsgAddSprite{image='ports/cloud', x=400, y=60, path={{-100,60},{300,60},{500,60},{900,60}}, time=1000, loop=true} end,"
      "    function() bsgAddSprite{image='ports/bld_a', x=100, y=100, labelx=300, labely=300,"
      "      contents=\"Window{name='pophold', w=40, h=18}()\", command=function() gClicks = gClicks + 1 end} end,"
      "    function() bsgAddSprite{image='ports/bld_b', x=120, y=100, command=function() gClicksB = gClicksB + 1 end} end,"
      "  }),"
      "  Window{name='buildinglabels', x=0, y=0, w=800, h=600} }()") == 0);
  game::choc2::Port* port = e.port();
  REQUIRE(port);
  CHECK(port->typeName() == "Port");
  REQUIRE(port->buildings().size() == 2);
  CHECK(port->buildings()[0].name == "bld_a");
  CHECK(port->pathSprites().size() == 1);
  CHECK(port->spriteRoot()->tint.r == 1);  // no scene fade in Chocolatier 2
  CHECK_FALSE(e.log.contains("[unimplemented-type] Port"));
  // The later of two overlapping buildings wins the hit test.
  CHECK(port->findBuilding({75, 100}) == 0);
  CHECK(port->findBuilding({125, 100}) == 1);

  // Real routing: every pointer event goes through the window manager (the popup's mouse listener sees it first).
  auto pointer = [&](ui::InputEvent::Type type, float x, float y) {
    ui::InputEvent ev;
    ev.type = type;
    ev.x = x;
    ev.y = y;
    e.wm.handleInput(ev);
  };
  using T = ui::InputEvent::Type;
  // Hover building a: one popup holding the contents' window.
  pointer(T::MouseMove, 75, 100);
  const ui::WindowId popup = port->popup();
  REQUIRE(popup != 0);
  auto* pw = dynamic_cast<ui::PopupWindow*>(e.wm.window(popup));
  REQUIRE(pw);
  CHECK(pw->find("pophold"));
  int popups = 0;
  for (const auto& c : e.wm.topModal()->children()) popups += dynamic_cast<ui::PopupWindow*>(c.get()) ? 1 : 0;
  CHECK(popups == 1);
  // Moving inside the same building keeps the popup.
  pointer(T::MouseMove, 80, 100);
  CHECK(port->popup() == popup);
  // Moving from a into the overlap a-and-b: the hover switches to b (the later), the popup closes, and a click runs b's command.
  pointer(T::MouseMove, 125, 100);
  CHECK(port->target() == 1);
  CHECK(port->popup() == 0);
  CHECK((e.wm.window(popup) == nullptr || e.wm.window(popup)->isDestroyed()));
  pointer(T::MouseDown, 125, 100);
  pointer(T::MouseUp, 125, 100);
  e.host.tick(10);
  e.host.tick(20);
  CHECK(e.getNumber("gClicksB") == 1);
  CHECK(e.getNumber("gClicks") == 0);
  // Back to a alone: its popup opens again, and a click on a runs a's closure.
  pointer(T::MouseMove, 500, 500);
  pointer(T::MouseMove, 75, 100);
  CHECK(port->popup() != 0);
  pointer(T::MouseDown, 75, 100);
  pointer(T::MouseUp, 75, 100);
  e.host.tick(30);
  e.host.tick(40);
  CHECK(e.getNumber("gClicks") == 1);

  // The building labels stay until the first 40 ms tick at or after 3900 ms, then hide at once (no fade).
  ui::Window* labels = port->find("buildinglabels");
  REQUIRE(labels);
  CHECK(labels->shown());
  CHECK(port->introPulsing());
  double t = 0;
  while (port->sceneMs() < 3880) e.wm.tickWindowAnimations(t += 10);
  CHECK(labels->shown());
  CHECK(labels->alpha() == doctest::Approx(1));
  CHECK(port->introPulsing());
  while (port->sceneMs() < 3900) e.wm.tickWindowAnimations(t += 10);
  CHECK(port->sceneMs() == doctest::Approx(3920));
  CHECK_FALSE(labels->shown());
  CHECK_FALSE(port->introPulsing());
}

TEST_CASE("Choc2 Port: every building with a command pulses in sync until StopPulsing; a hover popup calling it keeps the hovered one") {
  PortEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run(
      "PushModal('t');"
      "Port{ x=0, y=0, w=800, h=600, xOffset=0, yOffset=0,"
      "  Group({"
      "    function() bsgAddSprite{image='ports/bld_a', x=100, y=100, contents=\"StopPulsing(); Window{name='pophold', w=40, h=18}()\","
      "      command=function() end} end,"
      "    function() bsgAddSprite{image='ports/bld_b', x=400, y=100, command=function() end} end,"
      "    function() bsgAddSprite{image='ports/bld_c', x=600, y=100, contents=\"Window{name='pc', w=4, h=4}()\"} end,"
      "  }),"
      "  Window{name='buildinglabels', x=0, y=0, w=800, h=600} }()") == 0);
  game::choc2::Port* port = e.port();
  REQUIRE(port);
  REQUIRE(port->buildings().size() == 3);
  auto pulse = [&](int i) { return dynamic_cast<game::PulseSprite*>(port->buildings()[static_cast<std::size_t>(i)].highlight); };
  REQUIRE(pulse(0));
  CHECK(pulse(0)->visible);
  CHECK(pulse(1)->visible);
  CHECK_FALSE(pulse(2)->visible);  // no command: never pulses
  double t = 0;
  while (port->sceneMs() < 650) e.wm.tickWindowAnimations(t += 10);
  CHECK(pulse(0)->phase == doctest::Approx(game::PortWindow::choc2PulsePhase(port->sceneMs())));
  CHECK(pulse(0)->phase > 0.9f);  // near the peak of the 1300 ms triangle
  CHECK(pulse(1)->phase == doctest::Approx(pulse(0)->phase));
  // Hovering a: its contents call StopPulsing, then a is the hovered one and keeps pulsing; b stops, the labels are gone.
  ui::InputEvent ev;
  ev.type = ui::InputEvent::Type::MouseMove;
  ev.x = 100;
  ev.y = 100;
  e.wm.handleInput(ev);
  CHECK_FALSE(port->introPulsing());
  CHECK_FALSE(port->find("buildinglabels")->shown());
  CHECK(port->pulseHover() == 0);
  CHECK(pulse(0)->visible);
  CHECK_FALSE(pulse(1)->visible);
  // Leaving the building ends its pulse; StopPulsing() from Lua with the pointer elsewhere does nothing further.
  ev.x = 700;
  ev.y = 500;
  e.wm.handleInput(ev);
  CHECK(port->pulseHover() == -1);
  CHECK_FALSE(pulse(0)->visible);
}

TEST_CASE("Choc2 pulse arithmetic: 1300 ms triangle, +20 px at the peak") {
  using game::PortWindow;
  using game::PulseSprite;
  CHECK(PortWindow::choc2PulsePhase(0) == doctest::Approx(0));
  CHECK(PortWindow::choc2PulsePhase(325) == doctest::Approx(0.5));
  CHECK(PortWindow::choc2PulsePhase(650) == doctest::Approx(1));
  CHECK(PortWindow::choc2PulsePhase(975) == doctest::Approx(0.5));
  CHECK(PortWindow::choc2PulsePhase(1300) == doctest::Approx(0));
  CHECK(PortWindow::choc2PulsePhase(1300 + 650) == doctest::Approx(1));
  // 100 px wide at scale 1: 120 px at the peak.
  CHECK(100 * PulseSprite::stretch(1, 100, 1) == doctest::Approx(120));
  CHECK(100 * PulseSprite::stretch(0.5f, 100, 1) == doctest::Approx(110));
  CHECK(200 * 0.5f * PulseSprite::stretch(1, 200, 0.5f) == doctest::Approx(120));  // the scale is part of the size
}

namespace {
struct CapturingUploader : gfx::TextureUploader {
  std::vector<std::uint8_t> last;
  void* upload(const std::uint8_t* rgba, int w, int h) override {
    last.assign(rgba, rgba + w * h * 4);
    return new int(1);
  }
  void release(void* h) override { delete static_cast<int*>(h); }
};
}  // namespace

TEST_CASE("Choc2 pulse picture: white RGB with the building's alpha") {
  const fs::path dir = fs::temp_directory_path() / "choco_test_choc2_pulse_sil";
  fs::remove_all(dir);
  fs::create_directories(dir);
  // 2x1: a red half-transparent pixel and a fully transparent green one.
  const unsigned char px[8] = {200, 10, 10, 128, 0, 200, 0, 0};
  REQUIRE(choco::platform::writePng((dir / "b.png").string(), 2, 1, px));
  assets::AssetFS afs(u8(dir), "");
  CapturingUploader up;
  gfx::TextureCache cache(afs, &up);
  auto src = cache.load("b");
  REQUIRE(src);
  auto sil = game::PulseSprite::makeSilhouette(cache, *src);
  REQUIRE(sil);
  CHECK(sil->width() == 2);
  CHECK(sil->height() == 1);
  REQUIRE(up.last.size() == 8);
  CHECK(up.last[0] == 255);
  CHECK(up.last[1] == 255);
  CHECK(up.last[2] == 255);
  CHECK(up.last[3] == 128);
  CHECK(up.last[7] == 0);
  CHECK(game::PulseSprite::makeSilhouette(cache, *src) == sil);  // cached per picture
}

TEST_CASE("Choc2 pulse picture: an in-place pixel edit refreshes the mask and the silhouette") {
  // The logo sign edits its texture through makeFromPixels in place; the pulse silhouette must follow (recorded:
  // "a logo sign edited in place keeps a stale one" gap, closed by evicting the derived alpha on every pixels write).
  const fs::path dir = fs::temp_directory_path() / "choco_test_choc2_pulse_edit";
  fs::remove_all(dir);
  fs::create_directories(dir);
  const unsigned char px[8] = {200, 10, 10, 128, 0, 200, 0, 0};
  REQUIRE(choco::platform::writePng((dir / "b.png").string(), 2, 1, px));
  assets::AssetFS afs(u8(dir), "");
  CapturingUploader up;
  gfx::TextureCache cache(afs, &up);
  auto src = cache.load("b");
  REQUIRE(src);
  auto sil = game::PulseSprite::makeSilhouette(cache, *src);
  REQUIRE(sil);
  REQUIRE(up.last.size() == 8);
  REQUIRE(up.last[3] == 128);
  // The sign's pixels change under the same key (fully opaque first pixel now).
  gfx::ImageData img;
  img.width = 2;
  img.height = 1;
  img.rgba.assign(px, px + 8);
  img.rgba[3] = 255;
  CHECK(cache.makeFromPixels("b.png", img) == src);  // the resolved path of the load, so the same entry, updated in place
  auto sil2 = game::PulseSprite::makeSilhouette(cache, *src);
  CHECK(sil2 == sil);  // the silhouette texture updates in place too
  REQUIRE(up.last.size() == 8);
  CHECK(up.last[3] == 255);  // the new alpha reached the silhouette (the stale mask would still say 128)
}

TEST_CASE("Choc2 port: each building's pulse uses a silhouette texture, not the picture") {
  PortEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run(
      "PushModal('t');"
      "Port{ x=0, y=0, w=800, h=600, xOffset=0, yOffset=0,"
      "  Group({ function() bsgAddSprite{image='ports/bld_a', x=100, y=100, command=function() end} end }) }()") == 0);
  game::choc2::Port* port = e.port();
  REQUIRE(port);
  const auto& b = port->buildings()[0];
  REQUIRE(b.highlight->texture);
  CHECK(b.highlight->texture != b.sprite->texture);
  CHECK(b.highlight->texture->path().rfind("$pulse:", 0) == 0);
  CHECK(b.highlight->texture->width() == b.sprite->texture->width());
}

TEST_CASE("Choc2 popups cap a Rectangle's fade at 0.8") {
  PortEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run(
      "PushModal('t');"
      "Port{ x=0, y=0, w=800, h=600, xOffset=0, yOffset=0,"
      "  Group({ function() bsgAddSprite{image='ports/bld_a', x=100, y=100,"
      "    contents=\"Rectangle{name='bg', w=40, h=18, alpha=1}()\", command=function() end} end }) }()") == 0);
  game::choc2::Port* port = e.port();
  REQUIRE(port);
  e.wm.setNowMs(0);
  ui::InputEvent ev;
  ev.type = ui::InputEvent::Type::MouseMove;
  ev.x = 100;
  ev.y = 100;
  e.wm.handleInput(ev);
  auto* pw = dynamic_cast<ui::PopupWindow*>(e.wm.window(port->popup()));
  REQUIRE(pw);
  e.wm.tickTransitions(500);
  ui::Window* bg = pw->find("bg");
  REQUIRE(bg);
  CHECK(bg->alpha() == doctest::Approx(0.8));
}

TEST_CASE("Choc2: bsgAddSprite outside a Port logs and does not crash") {
  PortEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('t'); bsgAddSprite{image='ports/cloud', x=1, y=2}") == 0);
  CHECK(e.log.contains("bsgAddSprite outside a Port"));
}

TEST_CASE("Choc2 StopPulsing and ResetColorMap are implemented: silent and not stubs without a Port or Map") {
  PortEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("StopPulsing(); ResetColorMap()") == 0);
  CHECK(e.log.count("StopPulsing") == 0);  // with no Port it just does nothing
  CHECK(e.log.count("ResetColorMap") == 0);  // it only clears the Map's class statics
  for (const char* n : {"StopPulsing", "ResetColorMap"}) CHECK(e.stubs.count(n) == 0);
}

TEST_CASE("Choc2 Character: the face for a mood, an instant swap, and a character without the three faces") {
  using game::choc2::Character;
  CHECK(Character::faceFor(0) == Character::Displeased);
  CHECK(Character::faceFor(25) == Character::Displeased);
  CHECK(Character::faceFor(26) == Character::Neutral);
  CHECK(Character::faceFor(49) == Character::Neutral);
  CHECK(Character::faceFor(50) == Character::Pleased);
  CHECK(Character::faceFor(70) == Character::Pleased);
  CHECK(Character::faceFor(100) == Character::Pleased);

  PortEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('t');"
                "Character{x=8, y=0, w=191, h=240, char={name='kay', mood=50}}();"
                "Character{x=300, y=0, w=191, h=240, char={name='solo', mood=90}}();"
                "Character{x=500, y=0, w=191, h=240, char={name='half', mood=90}}()") == 0);
  Character* kay = nullptr;
  Character* solo = nullptr;
  Character* half = nullptr;
  for (const auto& c : e.wm.topModal()->children())
    if (auto* ch = dynamic_cast<Character*>(c.get()))
      (ch->characterName() == "kay" ? kay : ch->characterName() == "solo" ? solo : half) = ch;
  REQUIRE((kay && solo && half));
  CHECK(kay->hasFaces());
  CHECK(kay->shownFace() == Character::Pleased);  // every character starts at mood 50, which is pleased
  CHECK(kay->rect().h == 240);
  CHECK_FALSE(solo->hasFaces());
  CHECK_FALSE(half->hasFaces());  // one of three faces is not a set

  gfx::RecordingRenderer rr;
  ui::DrawContext dc{rr, {0, 0}, 1};
  kay->draw(dc);
  CHECK(rr.count(gfx::RecordingRenderer::Call::Kind::DrawTexture) == 2);  // the body and the pleased face
  CHECK(rr.calls()[1].dst.x == 0);  // the face's top-left is the body's top-left
  CHECK(rr.calls()[1].dst.w == 100);
  rr.clear();
  solo->draw(dc);
  CHECK(rr.count(gfx::RecordingRenderer::Call::Kind::DrawTexture) == 1);  // only the body
  rr.clear();

  REQUIRE(e.run("SetCharacterMood('kay', 30); SetCharacterMood('solo', 5); SetCharacterMood('nobody', 5)") == 0);
  CHECK(kay->shownFace() == Character::Neutral);  // at once: no fade, no animation
  CHECK(e.wm.windowAnimationCount() == 0);
  rr.clear();
  kay->draw(dc);
  CHECK(rr.count(gfx::RecordingRenderer::Call::Kind::DrawTexture) == 2);  // the body and one face
  REQUIRE(e.run("SetCharacterMood('kay', 25)") == 0);
  CHECK(kay->shownFace() == Character::Displeased);
  REQUIRE(e.run("SetCharacterMood('kay', 50)") == 0);
  CHECK(kay->shownFace() == Character::Pleased);
}

TEST_CASE("Choc2 bsgDate and bsgShortDate: weeks and days to a date, month names from the string table") {
  assets::StringTable st;
  const char* const months[12] = {"January", "February", "March",     "April",   "May",      "June",
                                  "July",    "August",   "September", "October", "November", "December"};
  std::vector<std::pair<std::string, std::string>> rows{{"formatDate", "%1% %%d, %%d"}};
  for (int i = 0; i < 12; ++i) rows.emplace_back("month_" + std::to_string(i + 1), months[i]);
  st.loadRows(rows);
  struct Row {
    double weeks, days;
    const char *longForm, *shortForm;
  };
  const Row table[] = {
      {0, 0, "January 1, 1925", "1 Jan 1925"},  // day 0 becomes 1
      {0, 3, "January 3, 1925", "3 Jan 1925"},
      {1, 0, "January 7, 1925", "7 Jan 1925"},  // each week into the month is 7 days
      {4, 0, "January 28, 1925", "28 Jan 1925"},
      {5, 0, "February 7, 1925", "7 Feb 1925"},  // week 5 * 12 / 52 = month 1
      {8, 5, "February 28, 1925", "28 Feb 1925"},
      {13, 40, "April 28, 1925", "28 Apr 1925"},  // capped at 28
      {26, 0, "July 1, 1925", "1 Jul 1925"},
      {51, 6, "December 28, 1925", "28 Dec 1925"},
      {52, 0, "January 1, 1926", "1 Jan 1926"},  // the next year
      {100, 2, "December 9, 1926", "9 Dec 1926"},  // ui/help9.lua's bsgDate(100, 2)
      {104, 0, "January 1, 1927", "1 Jan 1927"},
  };
  for (const Row& r : table) {
    CHECK(script::choc2Date(st, r.weeks, r.days, false) == r.longForm);
    CHECK(script::choc2Date(st, r.weeks, r.days, true) == r.shortForm);
  }
  CHECK(script::choc2Date(st, -3, -1, false) == "January 1, 1925");  // negative arguments clamp to 0

  // The Lua natives are the same function over the context's string table.
  PortEnv e;
  REQUIRE(e.booted);
  e.ctx.strings.loadRows(rows);
  REQUIRE(e.run("d = bsgDate(100, 2); s = bsgShortDate(100, 2)") == 0);
  CHECK(e.getString("d") == "December 9, 1926");
  CHECK(e.getString("s") == "9 Dec 1926");
  CHECK_FALSE(e.log.contains("[unimplemented] bsgDate"));
}

TEST_CASE("Choc2 Character: the table's appearance names the pictures, and name stays the SetCharacterMood key") {
  using game::choc2::Character;
  PortEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('t');"
                "Character{x=8, y=0, w=191, h=240, char={name='ny_gambler01b', appearance='kay', mood=50}}();"
                "Character{x=300, y=0, w=191, h=240, char={name='ny_gambler09', mood=50}}()") == 0);
  Character* with = nullptr;
  Character* without = nullptr;
  for (const auto& c : e.wm.topModal()->children())
    if (auto* ch = dynamic_cast<Character*>(c.get())) (ch->characterName() == "ny_gambler01b" ? with : without) = ch;
  REQUIRE((with && without));
  CHECK(with->appearance() == "kay");
  CHECK(with->hasFaces());  // characters/kay.png and its three faces
  CHECK(with->rect().h == 240);
  CHECK(without->appearance() == "ny_gambler09");  // no appearance: the name
  CHECK_FALSE(without->hasFaces());
  REQUIRE(e.run("SetCharacterMood('ny_gambler01b', 90)") == 0);
  CHECK(with->shownFace() == Character::Pleased);
  gfx::RecordingRenderer rr;
  ui::DrawContext dc{rr, {0, 0}, 1};
  without->draw(dc);
  CHECK(rr.count(gfx::RecordingRenderer::Call::Kind::DrawTexture) == 0);  // no body for a missing picture
}

TEST_CASE("Chocolatier 2 path sprites: art that faces left is mirrored while it moves right, and on a bounce's way back") {
  PathSprite right, left, bounce, plain;
  right.setCurve(gfx::Curve::fromPoints({{0, 0}, {100, 0}, {200, 0}, {300, 0}}), 1000, PathSprite::Mode::Loop);
  left.setCurve(gfx::Curve::fromPoints({{300, 0}, {200, 0}, {100, 0}, {0, 0}}), 1000, PathSprite::Mode::Loop);
  bounce.setCurve(gfx::Curve::fromPoints({{0, 0}, {100, 0}, {200, 0}, {300, 0}}), 1000, PathSprite::Mode::Bounce);
  plain.setCurve(gfx::Curve::fromPoints({{0, 0}, {100, 0}, {200, 0}, {300, 0}}), 1000, PathSprite::Mode::Loop);
  for (PathSprite* p : {&right, &left, &bounce}) p->artFacesLeft = true;
  for (PathSprite* p : {&right, &left, &bounce, &plain}) p->update(100);
  CHECK(right.hflip);
  CHECK_FALSE(left.hflip);
  CHECK(bounce.hflip);
  CHECK_FALSE(plain.hflip);  // art that is not marked is never mirrored
  for (int i = 0; i < 12; ++i) bounce.update(100);  // t = 1300 ms: on the way back
  CHECK_FALSE(bounce.hflip);
  for (int i = 0; i < 10; ++i) bounce.update(100);  // t = 2300 ms: forward again
  CHECK(bounce.hflip);
}
