// Chocolatier 1 (docs/re/choc1-options-credits.md): OptionsDialog
// (TOptions) and CreditsWindow (TCredits).
#include <doctest/doctest.h>

#include <string>

#include "ScriptHarness.h"
#include "games/Registry.h"
#include "games/choc1/CreditsWindow.h"
#include "sdk/Users.h"
#include "ui/widgets/CreditsWindow.h"
#include "ui/widgets/InputWidgets.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
namespace f1 = choco::game::choc1;

namespace {

struct OptionsEnv : test::ScriptEnv {
  OptionsEnv()
      : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/choc1_story"), host(ctx) {
    ctx.module = games::findModule("choc1");
    ctx.module->attach(ctx);
    booted = host.boot();
    // A private profile folder, so no other test's prefs leak in; the defaults
    // are the profile's, as registerUserNatives sets them.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                      "choco_test_choc1_options_users";
    std::filesystem::remove_all(dir);
    ctx.users = std::make_shared<sdk::UserStore>(dir.string());
    const script::VolumeDefaults &v = ctx.module->profile().volumeDefaults;
    ctx.users->setVolumeDefaults(v.sfx, v.music, v.ambient);
  }
  ui::SliderWindow *slider(const char *n) {
    return dynamic_cast<ui::SliderWindow *>(wm.topModal()->find(n));
  }
  ui::ButtonWindow *button(const char *n) {
    return dynamic_cast<ui::ButtonWindow *>(wm.topModal()->find(n));
  }
  // ui/options.lua's tree, minus the graphics and the text.
  void open() {
    REQUIRE(run("PushModal('options'); OptionsDialog{x=43, y=33, w=330, h=152, "
                "Slider{name='sfxslider', x=5, y=10, w=320}, "
                "Slider{name='ambientslider', x=5, y=45, w=320}, "
                "Slider{name='musicslider', x=5, y=80, w=320}, "
                "Button{name='fullscreen', x=45, y=110, type=kToggle}, "
                "Button{name='mute', x=175, y=110, type=kToggle}}()") == 0);
    REQUIRE(lastError.empty());
  }
  script::ScriptHost host;
  bool booted = false;
};

} // namespace

TEST_CASE("Choc1 OptionsDialog: the shared options panel under the Lua type "
          "name, defaults 750 each") {
  OptionsEnv e;
  REQUIRE(e.booted);
  e.ctx.users->create("Tester");
  e.ctx.users->setCurrent(0);
  e.open();
  CHECK(e.slider("sfxslider")->parent()->typeName() == "OptionsDialog");
  for (const char *n : {"sfxslider", "ambientslider", "musicslider"})
    CHECK(e.slider(n)->value() == doctest::Approx(0.75f));
  CHECK_FALSE(e.button("mute")->on());
  CHECK_FALSE(e.button("fullscreen")->on());
}

TEST_CASE("Choc1 OptionsDialog: stored levels show, a moved slider is stored "
          "by SaveVolumes only, toggles follow the state") {
  OptionsEnv e;
  REQUIRE(e.booted);
  e.ctx.users->create("Tester");
  e.ctx.users->setCurrent(0);
  e.ctx.users->setPref("musiclevel", "300");
  e.ctx.users->setPref("soundoff", "1");
  e.ctx.isFullscreen = [] { return true; };
  e.open();
  CHECK(e.slider("musicslider")->value() == doctest::Approx(0.3f));
  CHECK(e.slider("sfxslider")->value() == doctest::Approx(0.75f));
  CHECK(e.button("mute")->on());
  CHECK(e.button("fullscreen")->on());

  e.slider("sfxslider")->setValue(0.2f, true);
  e.slider("ambientslider")->setValue(0.9f, true);
  CHECK(e.ctx.users->pref("soundlevel") == "750"); // not stored yet
  REQUIRE(e.run("SaveVolumes()") == 0);
  CHECK(e.ctx.users->pref("soundlevel") == "200");
  CHECK(e.ctx.users->pref("ambientlevel") == "900");
  CHECK(e.ctx.users->pref("musiclevel") == "300");

  e.wm.popModal("options");
  e.wm.clearDestroyed();
  e.open();
  CHECK(e.slider("sfxslider")->value() == doctest::Approx(0.2f));
  CHECK(e.slider("ambientslider")->value() == doctest::Approx(0.9f));
}

TEST_CASE("Choc1 OptionsDialog: a slider message applies the level live") {
  OptionsEnv e;
  REQUIRE(e.booted);
  std::vector<std::pair<std::string, float>> seen;
  e.wm.setVolumeHook(
      [&](const std::string &n, float v) { seen.emplace_back(n, v); });
  e.open();
  e.slider("musicslider")->setValue(0.4f, true);
  e.wm.sendMessage({ui::kSliderValChanged, "musicslider",
                    e.slider("musicslider")->parent()->id()});
  e.wm.dispatchMessages();
  REQUIRE(seen.size() == 1);
  CHECK(seen[0].first == "musicslider");
  CHECK(seen[0].second == doctest::Approx(0.4f));
}

TEST_CASE("Choc1 CreditsWindow: the asset path, the version line and the "
          "scroll timing") {
  CHECK(f1::CreditsWindow::assetPath("assets/credits.txt") == "credits.txt");
  CHECK(f1::CreditsWindow::assetPath("credits.txt") == "credits.txt");
  // The version line joins the file as one more full-width line (blank in the
  // side columns).
  CHECK(f1::CreditsWindow::versionOf("dl,pf,1.0") == "1.0");
  CHECK(f1::CreditsWindow::versionOf("2.5") == "2.5");
  const std::string t =
      f1::CreditsWindow::withVersionLine("[Art]\nAnn\n", "1.0.2");
  CHECK(t == "[Art]\nAnn\nChocolatier version 1.0.2\n");
  // No final newline: the exe's line reader still loses the last character of
  // that line.
  CHECK(f1::CreditsWindow::withVersionLine("Bob", "9") ==
        "Bo"
        "\nChocolatier version 9\n");
  const ui::CreditsColumns c = ui::parseCredits(t, "00000000", 300, 26);
  CHECK(c.centre.find("Chocolatier version 1.0.2\n") != std::string::npos);
  CHECK(c.left.substr(0, 3) == "\n\n\n"); // three full-width lines: the side
                                          // columns are blank in those rows
}

TEST_CASE("Choc1 credits scroll: nothing during intropause, then H over "
          "`time`, looping without a new pause") {
  const int intro = 3000, time = 20000, H = 1000;
  CHECK(ui::creditsScrollPx(0, intro, time, H) == 0);
  CHECK(ui::creditsScrollPx(2999, intro, time, H) == 0);
  CHECK(ui::creditsScrollPx(3000, intro, time, H) == 0);
  CHECK(ui::creditsScrollPx(3000 + 10000, intro, time, H) ==
        doctest::Approx(500));
  CHECK(ui::creditsScrollPx(3000 + 19999, intro, time, H) ==
        doctest::Approx(999.95));
  CHECK(ui::creditsScrollPx(3000 + 20000, intro, time, H) ==
        doctest::Approx(0)); // wraps
  CHECK(ui::creditsScrollPx(3000 + 25000, intro, time, H) ==
        doctest::Approx(250));
}
