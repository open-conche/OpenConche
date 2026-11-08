// Chocolatier 2 menu windows: Options, HighScores and Credits, the natives
// of ui/options.lua, ui/hiscore_local.lua and ui/credits.lua, and the Chocolatier 2 credits.txt dialect.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "games/Registry.h"
#include "ScriptHarness.h"
#include "audio/Prefs.h"
#include "sdk/Users.h"
#include "games/choc2/CreditsWindow.h"
#include "games/choc2/HighScoresWindow.h"
#include "games/choc2/OptionsWindow.h"
#include "gfx/TextureCache.h"
#include "gfx/text/TextLayout.h"
#include "script/ScriptHost.h"
#include "ui/VolumeSliders.h"
#include "ui/widgets/CreditsWindow.h"
#include "ui/widgets/InputWidgets.h"
#include "ui/widgets/TextWidgets.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
namespace fs = std::filesystem;

namespace {

std::string menuAssets() {
  const fs::path dir = fs::temp_directory_path() / "choco_test_choc2_menu";
  fs::remove_all(dir);
  fs::create_directories(dir / "ui");
  std::ofstream(dir / "ui/mainloop.lua") << "function Main() while true do WaitForMessage() end end\nreturn Main\n";
  const auto s = dir.u8string();
  return std::string(s.begin(), s.end());
}

struct MenuEnv : test::ScriptEnv {
  MenuEnv() : ScriptEnv(menuAssets()), textures(fs, nullptr), host(ctx) {
    ctx.module = games::findModule("choc2");
    ctx.module->attach(ctx);
    wm.setTextures(&textures);
    booted = host.boot();
    ctx.strings.loadRows({{"formatScore", "%1%/week"}, {"story_mode", "Story Mode"}, {"free_mode", "Free Play Mode"}});
    // A throw-away profile folder per environment, so a test never sees another one's scores or prefs.
    const fs::path dir = fs::temp_directory_path() / "choco_test_choc2_menu_users";
    fs::remove_all(dir);
    ctx.users = std::make_shared<sdk::UserStore>(dir.string());
    const script::VolumeDefaults& v = ctx.module->profile().volumeDefaults;  // as registerUserNatives does for the real store
    ctx.users->setVolumeDefaults(v.sfx, v.music, v.ambient);
  }
  void pump() {
    wm.clearDestroyed();
    wm.dispatchMessages();
  }
  ui::SliderWindow* slider(const char* name) { return dynamic_cast<ui::SliderWindow*>(wm.topModal()->find(name)); }
  ui::ButtonWindow* button(const char* name) { return dynamic_cast<ui::ButtonWindow*>(wm.topModal()->find(name)); }
  ui::TextWindow* text(const std::string& name) { return dynamic_cast<ui::TextWindow*>(wm.topModal()->find(name)); }
  // ui/options.lua's tree, minus the graphics.
  void openOptions() {
    REQUIRE(run("PushModal('dlg'); Options{x=0, y=0, w=400, h=245, "
                "Slider{name='sfxslider', x=5, y=35, w=390}, Slider{name='ambientslider', x=5, y=70, w=390}, "
                "Slider{name='musicslider', x=5, y=105, w=390}, Button{name='fullscreen', x=140, y=130, type=kToggle}, "
                "Button{name='mute', x=140, y=160, type=kToggle}}()") == 0);
    REQUIRE(lastError.empty());
  }
  // ui/hiscore_local.lua's tree: the title bits, the ten rows and the scroll buttons.
  void openScores() {
    std::string rows;
    for (int i = 1; i <= 10; ++i) {
      const std::string n = std::to_string(i);
      rows += "Group{Text{x=0, y=" + std::to_string(i * 20) + ", w=34, h=20, name='" + n + "', label='#x'}, Bitmap{x=34, y=0, w=1, h=1, "
              "name='p1_" + n + "'}, Text{x=51, y=" + std::to_string(i * 20) + ", w=300, h=20, name='name" + n + "', label='#x'}, "
              "Text{x=51, y=" + std::to_string(i * 20) + ", w=300, h=20, name='score" + n + "', label='#x'}}, ";
    }
    REQUIRE(run("PushModal('dlg'); HighScores{x=0, y=0, w=400, h=300, Text{name='mode', x=0, y=0, w=100, h=20, label='#m'}, " + rows +
                "Button{name='scrollup', x=0, y=0, w=10, h=10}, Button{name='scrolldown', x=0, y=0, w=10, h=10}}()") == 0);
    REQUIRE(lastError.empty());
  }
  void logScore(const std::string& user, int score) {
    ctx.users->create(user);
    ctx.users->setCurrent(ctx.users->count() - 1);
    REQUIRE(run("bsgLogScore(" + std::to_string(score) + ", '2-000000052-000078000', 3)") == 0);
  }
  gfx::TextureCache textures;
  script::ScriptHost host;
  bool booted = false;
};

}  // namespace

// ---------------------------------------------------------------- Options

TEST_CASE("volume sliders: the Chocolatier 2 and 3 names map to the same prefs and channels, and a saved level round trips") {
  using ui::findVolumeSlider;
  REQUIRE(findVolumeSlider("sfxslider"));
  CHECK(std::string(findVolumeSlider("sfxslider")->pref) == "soundlevel");
  CHECK(findVolumeSlider("sfxslider")->channel == 0);
  CHECK(std::string(findVolumeSlider("musicslider")->pref) == "musiclevel");
  CHECK(findVolumeSlider("musicslider")->channel == 1);
  CHECK(std::string(findVolumeSlider("ambientslider")->pref) == "ambientlevel");
  CHECK(findVolumeSlider("ambientslider")->channel == 2);
  CHECK(std::string(findVolumeSlider("sfxlevelslider")->pref) == "soundlevel");  // Chocolatier 3's names stay
  CHECK(findVolumeSlider("ambientlevelslider")->defaultPerMille == 500);
  CHECK(findVolumeSlider("mute") == nullptr);
  for (int pm : {0, 1, 250, 321, 500, 650, 750, 999, 1000})
    CHECK(audio::perMilleFromLevel(audio::levelFromPerMille(std::to_string(pm), 0)) == pm);
}

TEST_CASE("Choc2 Options: the sliders and check boxes take the user's prefs on open, and SaveVolumes stores them") {
  MenuEnv e;
  REQUIRE(e.booted);
  e.ctx.users->create("Tester");
  e.ctx.users->setCurrent(0);
  e.openOptions();
  CHECK(e.wm.topModal()->find("sfxslider")->parent()->typeName() == "Options");
  CHECK(e.slider("sfxslider")->value() == doctest::Approx(0.75f));  // the Chocolatier 2 defaults: 750 each
  CHECK(e.slider("ambientslider")->value() == doctest::Approx(0.75f));
  CHECK(e.slider("musicslider")->value() == doctest::Approx(0.75f));
  CHECK_FALSE(e.button("mute")->on());
  CHECK_FALSE(e.button("fullscreen")->on());

  // Move two sliders (quiet: the value, no message), flip the mute pref through the native, and store.
  e.slider("sfxslider")->setValue(0.25f, true);
  e.slider("musicslider")->setValue(0.9f, true);
  REQUIRE(e.run("ToggleSoundMute(); SaveVolumes()") == 0);
  CHECK(e.ctx.users->pref("soundlevel") == "250");
  CHECK(e.ctx.users->pref("musiclevel") == "900");
  CHECK(e.ctx.users->pref("ambientlevel") == "750");
  CHECK(e.ctx.users->pref("soundoff") == "1");

  // Reopen: the knobs are where they were left, the mute box is on, the full screen box follows the display.
  e.wm.popModal("dlg");
  e.pump();
  e.ctx.isFullscreen = [] { return true; };
  e.openOptions();
  CHECK(e.slider("sfxslider")->value() == doctest::Approx(0.25f));
  CHECK(e.slider("musicslider")->value() == doctest::Approx(0.9f));
  CHECK(e.slider("ambientslider")->value() == doctest::Approx(0.75f));
  CHECK(e.button("mute")->on());
  CHECK(e.button("fullscreen")->on());
}

TEST_CASE("Choc2 Options: a slider message applies the level live through the volume hook") {
  MenuEnv e;
  REQUIRE(e.booted);
  std::vector<std::pair<std::string, float>> seen;
  e.wm.setVolumeHook([&](const std::string& n, float v) { seen.emplace_back(n, v); });
  e.openOptions();
  e.slider("ambientslider")->setValue(0.4f, true);
  ui::Window* opts = e.slider("ambientslider")->parent();
  e.wm.sendMessage({ui::kSliderValChanged, "ambientslider", opts->id()});
  e.pump();
  REQUIRE(seen.size() == 1);
  CHECK(seen[0].first == "ambientslider");
  CHECK(seen[0].second == doctest::Approx(0.4f));
}

// ---------------------------------------------------------------- HighScores

TEST_CASE("Choc2 HighScores: bsgLogScore fills the local list, best first, with the unused rows hidden") {
  MenuEnv e;
  REQUIRE(e.booted);
  e.logScore("Anna", 1500);
  e.logScore("Bruno", 2400);
  e.logScore("Chloe", 900);
  e.openScores();
  game::choc2::HighScores* w = nullptr;
  for (const auto& c : e.wm.topModal()->children())
    if (auto* h = dynamic_cast<game::choc2::HighScores*>(c.get())) w = h;
  REQUIRE(w);
  CHECK(w->typeName() == "HighScores");
  CHECK(w->shownRows() == 3);
  CHECK(e.text("name1")->label() == "Bruno");
  CHECK(e.text("score1")->label() == "$2,400/week");
  CHECK(e.text("1")->label() == "1.");
  CHECK(e.text("name2")->label() == "Anna");
  CHECK(e.text("name3")->label() == "Chloe");
  CHECK(e.text("3")->label() == "3.");
  CHECK(e.text("name1")->shown());
  CHECK_FALSE(e.text("name4")->shown());
  CHECK_FALSE(e.wm.topModal()->find("p1_1")->shown());
  CHECK(e.text("mode")->label() == "Story Mode");
  CHECK_FALSE(e.wm.topModal()->find("scrolldown")->shown());  // all on one page
}

TEST_CASE("Choc2 HighScores: the local view is the top ten with the scroll buttons hidden, NextMode switches tables") {
  MenuEnv e;
  REQUIRE(e.booted);
  for (int i = 0; i < 13; ++i) e.logScore("P" + std::to_string(i), 1000 + i * 10);
  e.openScores();
  CHECK(e.text("name1")->label() == "P12");
  CHECK(e.text("name10")->label() == "P3");
  CHECK_FALSE(e.wm.topModal()->find("scrolldown")->shown());
  CHECK_FALSE(e.wm.topModal()->find("scrollup")->shown());
  REQUIRE(e.run("ScrollDown(); ViewLocal(0)") == 0);
  CHECK(e.text("name1")->label() == "P12");  // no paging locally
  CHECK_FALSE(e.wm.topModal()->find("scrollup")->shown());

  REQUIRE(e.run("NextMode()") == 0);  // Free Play: nobody has a score there
  CHECK(e.text("mode")->label() == "Free Play Mode");
  CHECK_FALSE(e.text("name1")->shown());
  REQUIRE(e.run("PreviousMode()") == 0);
  CHECK(e.text("mode")->label() == "Story Mode");
  CHECK(e.text("name1")->shown());
}

TEST_CASE("Choc2 HighScores: ViewGlobal and the category natives are offline stubs that log once; no window logs once") {
  MenuEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("ScrollUp(); ScrollUp()") == 0);  // no HighScores is open
  int noWindow = 0;
  for (const auto& l : e.log.lines())
    if (l.find("[unimplemented] ScrollUp") == 0) ++noWindow;
  CHECK(noWindow == 1);
  e.logScore("Anna", 1500);
  e.openScores();
  REQUIRE(e.run("ViewGlobal(0); ViewGlobal(0); NextCategory(); PreviousCategory()") == 0);
  int global = 0;
  for (const auto& l : e.log.lines())
    if (l.find("[unimplemented] ViewGlobal") == 0) ++global;
  CHECK(global == 1);
  CHECK(e.text("name1")->label() == "Anna");  // the local list stays
}

TEST_CASE("Choc2 submit natives: GetSubmitInfo is loadstring-able with the user's name, SubmitScore and SubmitMedals send nothing") {
  MenuEnv e;
  REQUIRE(e.booted);
  e.ctx.users->create("O'Brien");
  e.ctx.users->setCurrent(0);
  REQUIRE(e.run("f_ = loadstring(GetSubmitInfo()); f_(); n_ = gName; u_ = gUsername; r_ = gRemember; s_ = gScore") == 0);
  CHECK(e.getString("n_") == "O'Brien");
  CHECK(e.getString("u_") == "");
  CHECK(e.getString("r_") == "false");
  CHECK(e.getNumber("s_") == 0);
  // A backslash and a quote in the name survive (a newline cannot be a user name: validName rejects control characters) the round trip.
  e.ctx.users->create("a\\b'c\\");
  e.ctx.users->setCurrent(1);
  REQUIRE(e.run("f_ = loadstring(GetSubmitInfo()); f_(); n_ = gName") == 0);
  CHECK(e.getString("n_") == "a\\b'c\\");
  REQUIRE(e.run("SubmitScore('n', 'u', 'p', false); SubmitMedals(5, 'n', 'u', 'p', true); SubmitScore('n', '', '', false)") == 0);
  int submits = 0;
  for (const auto& l : e.log.lines())
    if (l.find("[unimplemented] Submit") == 0) ++submits;
  CHECK(submits == 2);  // each name once
}

// ---------------------------------------------------------------- Credits

TEST_CASE("Choc2 credits pages: '---' breaks, '+' header, '*' title, '|' cells, CRLF and blank rows kept") {
  using game::choc2::Credits;
  const auto pages = Credits::parsePages("+Big Splash\r\n\r\n*Design | Code | +Art\r\nAnn | Bob | Cy\r\n---\r\nEnd\r\n");
  REQUIRE(pages.size() == 2);
  REQUIRE(pages[0].size() == 4);
  CHECK(pages[0][0][0].text == "Big Splash");
  CHECK(pages[0][0][0].kind == Credits::Kind::Header);
  CHECK(pages[0][1].size() == 1);  // the empty line is a blank row
  CHECK(pages[0][1][0].text.empty());
  REQUIRE(pages[0][2].size() == 3);
  CHECK(pages[0][2][0].text == "Design");
  CHECK(pages[0][2][0].kind == Credits::Kind::Title);
  CHECK(pages[0][2][1].text == "Code");
  CHECK(pages[0][2][1].kind == Credits::Kind::Name);
  CHECK(pages[0][2][2].kind == Credits::Kind::Header);
  CHECK(pages[1][0][0].text == "End");  // a trailing newline adds no empty page
  // The shipped file has one page break.
  CHECK(Credits::parsePages("a\n---\nb\n---\n").size() == 2);
}

TEST_CASE("Choc2 credits pages: alpha is 0 -> 1 over fadeUp, 1 through showTime, then 1 -> 0 over fadeDown") {
  using game::choc2::Credits;
  CHECK(Credits::pageAlpha(0, 300, 4000, 500) == 0.0f);
  CHECK(Credits::pageAlpha(150, 300, 4000, 500) == doctest::Approx(0.5f));
  CHECK(Credits::pageAlpha(300, 300, 4000, 500) == 1.0f);
  CHECK(Credits::pageAlpha(4300, 300, 4000, 500) == 1.0f);
  CHECK(Credits::pageAlpha(4550, 300, 4000, 500) == doctest::Approx(0.5f));
  CHECK(Credits::pageAlpha(4800, 300, 4000, 500) == 0.0f);
}

namespace {
struct Uploader : gfx::TextureUploader {
  void* upload(const std::uint8_t*, int, int) override { return new int(0); }
  void release(void* h) override { delete static_cast<int*>(h); }
};
}  // namespace

TEST_CASE("Choc2 Credits: shows one page at a time on a 50 ms tick (300 + 4000 + 500 ms) and wraps after the last") {
  using game::choc2::Credits;
  MenuEnv e;
  REQUIRE(e.booted);
  Uploader up;
  gfx::text::TextSystem ts(nullptr, &up);
  e.wm.setTextSystem(&ts);
  e.wm.setTextFileReader([&](const std::string& n) -> std::optional<std::string> {
    if (n != "credits.txt") return std::nullopt;
    return std::string("+One\nAnn | Bob\n---\n*Two\n---\nThree\n");
  });
  e.wm.setNowMs(0);
  REQUIRE(e.run("PushModal('dlg'); Credits{x=0, y=0, w=550, h=470, font='x'}()") == 0);
  REQUIRE(e.lastError.empty());
  ui::Window* w = nullptr;
  for (const auto& c : e.wm.topModal()->children())
    if (c->typeName() == "Credits") w = c.get();
  REQUIRE(w);
  auto& credits = static_cast<Credits&>(*w);
  REQUIRE(credits.pageCount() == 3);
  CHECK(credits.pageIndex() == 0);
  CHECK(credits.alphaNow() == 0.0f);
  const auto at = [&](double ms) {
    for (double t = 10; t <= ms; t += 10) e.wm.tickWindowAnimations(t);
  };
  at(150);
  CHECK(credits.alphaNow() == doctest::Approx(0.5f).epsilon(0.05));
  at(1000);
  CHECK(credits.alphaNow() == 1.0f);
  at(4550);
  CHECK(credits.alphaNow() == doctest::Approx(0.5f).epsilon(0.1));
  CHECK(credits.pageIndex() == 0);
  at(4850);
  CHECK(credits.pageIndex() == 1);
  CHECK(credits.alphaNow() < 0.3f);
  at(4800 * 2 + 100);
  CHECK(credits.pageIndex() == 2);
  at(4800 * 3 + 100);
  CHECK(credits.pageIndex() == 0);  // wrapped
}
