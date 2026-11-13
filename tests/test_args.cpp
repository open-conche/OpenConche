#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>

#include "app/Args.h"
#include "app/Diagnostics.h"
#include "platform/UserDir.h"

using choco::app::parseArgs;

TEST_CASE("defaults") {
  auto r = parseArgs({});
  CHECK(r.ok());
  CHECK(r.options.scale == 1);
  CHECK_FALSE(r.options.fullscreen);
  CHECK_FALSE(r.options.headless);
  CHECK(r.options.frames == 0);
  CHECK(r.options.displayWidth == 2560);
  CHECK(r.options.displayHeight == 1440);
}

TEST_CASE("all options") {
  auto r = parseArgs({"--assets", "a", "--userdir", "u", "--scale", "2", "--fullscreen", "--headless", "--script", "s.txt",
                      "--screenshot", "o.png", "--log-unimplemented", "l.txt", "--frames", "30", "--display-size", "1920x1080"});
  REQUIRE(r.ok());
  CHECK(r.options.assetsDir == "a");
  CHECK(r.options.userDir == "u");
  CHECK(r.options.scale == 2);
  CHECK(r.options.fullscreen);
  CHECK(r.options.headless);
  CHECK(r.options.scriptFile == "s.txt");
  CHECK(r.options.screenshotPath == "o.png");
  CHECK(r.options.logUnimplementedFile == "l.txt");
  CHECK(r.options.frames == 30);
  CHECK(r.options.displayWidth == 1920);
  CHECK(r.options.displayHeight == 1080);
}

TEST_CASE("automated runs need a non-default --userdir") {
  for (const char* flag : {"--headless", "--no-assets", "--dump-windows"}) {
    auto r = parseArgs({flag});
    CHECK_FALSE(r.ok());
    CHECK(r.shortError);
    CHECK(r.error.find("--userdir <scratch dir>") != std::string::npos);
    CHECK(r.error.find(choco::platform::defaultUserDir("Chocolatier3")) != std::string::npos);
  }
  for (std::vector<std::string> a : {std::vector<std::string>{"--script", "s"}, {"--frames", "3"},
                                     {"--screenshot", "o.png"}, {"--log-unimplemented", "l"}})
    CHECK_FALSE(parseArgs(a).ok());
  CHECK(parseArgs({"--headless", "--userdir", "scratch"}).ok());
  CHECK(parseArgs({"--headless", "--help"}).ok());
  CHECK(parseArgs({"--headless", "--version"}).ok());  // like --help: exits before anything is written
  CHECK(parseArgs({"--run", "r.lua"}).ok());  // human play-testing keeps the default profile
  auto d = parseArgs({"--headless", "--userdir", choco::platform::defaultUserDir("Chocolatier3")});
  CHECK_FALSE(d.ok());
  CHECK(d.error.find("default user folder") != std::string::npos);
  CHECK(parseArgs({}).ok());  // a human windowed run keeps the default
  CHECK(parseArgs({"--userdir", choco::platform::defaultUserDir("Chocolatier3")}).ok());
  CHECK(parseArgs({"--fullscreen", "--scale", "2"}).ok());
}

TEST_CASE("bad input is rejected") {
  CHECK(parseArgs({"--scale", "3"}).ok());
  CHECK(parseArgs({"--scale", "1.5"}).options.scale == 1.5);
  CHECK(parseArgs({"--scale", "1.25"}).options.scale == 1.25);
  CHECK_FALSE(parseArgs({"--scale", "0.5"}).ok());
  CHECK_FALSE(parseArgs({"--scale", "9"}).ok());
  CHECK_FALSE(parseArgs({"--scale", "nan"}).ok());
  CHECK_FALSE(parseArgs({"--scale", "1e0"}).ok());
  CHECK_FALSE(parseArgs({"--scale", "1,5"}).ok());
  CHECK_FALSE(parseArgs({"--scale", "1.5x"}).ok());
  CHECK_FALSE(parseArgs({"--scale", "x"}).ok());
  CHECK_FALSE(parseArgs({"--scale"}).ok());
  CHECK_FALSE(parseArgs({"--frames", "0"}).ok());
  CHECK_FALSE(parseArgs({"--frames", "-4"}).ok());
  CHECK_FALSE(parseArgs({"--nope"}).ok());
  CHECK_FALSE(parseArgs({"--script"}).ok());
}

TEST_CASE("display size must be WxH") {
  CHECK_FALSE(parseArgs({"--display-size", "2560"}).ok());
  CHECK_FALSE(parseArgs({"--display-size", "0x600"}).ok());
  CHECK_FALSE(parseArgs({"--display-size", "ax5"}).ok());
  CHECK_FALSE(parseArgs({"--display-size"}).ok());
  CHECK(parseArgs({"--display-size", "800x600"}).ok());
}

TEST_CASE("--seed") {
  CHECK_FALSE(parseArgs({}).options.seed.has_value());
  CHECK(parseArgs({"--seed", "42"}).options.seed == 42);
  CHECK(parseArgs({"--seed", "0"}).options.seed == 0);
  CHECK(parseArgs({"--seed", "4294967295"}).options.seed == 4294967295u);
  CHECK_FALSE(parseArgs({"--seed", "4294967296"}).ok());
  CHECK_FALSE(parseArgs({"--seed", "-1"}).ok());
  CHECK_FALSE(parseArgs({"--seed", "x"}).ok());
  CHECK_FALSE(parseArgs({"--seed"}).ok());
}

TEST_CASE("--dump-windows and --config") {
  CHECK_FALSE(parseArgs({}).options.dumpWindows);
  CHECK(parseArgs({"--dump-windows"}).options.dumpWindows);
  auto r = parseArgs({"--config", "cheatmode=1", "--config", "a=b=c"});
  REQUIRE(r.ok());
  REQUIRE(r.options.configOverrides.size() == 2);
  CHECK(r.options.configOverrides[0] == std::make_pair(std::string("cheatmode"), std::string("1")));
  CHECK(r.options.configOverrides[1].second == "b=c");
  CHECK_FALSE(parseArgs({"--config", "novalue"}).ok());
  CHECK_FALSE(parseArgs({"--config", "=x"}).ok());
  CHECK_FALSE(parseArgs({"--config"}).ok());
}

TEST_CASE("--no-assets conflicts with --assets") {
  using choco::app::parseArgs;
  CHECK(parseArgs({"--no-assets", "--userdir", "u"}).ok());
  auto r = parseArgs({"--assets", "x", "--no-assets"});
  CHECK_FALSE(r.ok());
  CHECK(r.error.find("--no-assets") != std::string::npos);
  CHECK_FALSE(parseArgs({"--no-assets", "--assets", "x"}).ok());
}

TEST_CASE("--log takes a file and is off by default") {
  using namespace choco::app;
  CHECK(parseArgs({}).options.logFile.empty());
  auto r = parseArgs({"--scale", "2", "--log", "choco.log"});
  REQUIRE(r.ok());
  CHECK(r.options.logFile == "choco.log");
  CHECK_FALSE(parseArgs({"--log"}).ok());
}

TEST_CASE("--log-input is off by default") {
  using namespace choco::app;
  CHECK_FALSE(parseArgs({}).options.logInput);
  auto r = parseArgs({"--log", "c.log", "--log-input"});
  REQUIRE(r.ok());
  CHECK(r.options.logInput);
}

TEST_CASE("input log filter drops mouse-move unless enabled") {
  namespace fs = std::filesystem;
  const std::string path = (fs::temp_directory_path() / "choco_inputlog_test.log").string();
  std::string err;
  REQUIRE(choco::diag::openLog(path, err));
  auto run = [&](bool motion) {
    choco::diag::setLogMotion(motion);
    choco::diag::logInput(true, "[input] mouse-move %g %g", 1.0, 2.0);
    choco::diag::logInput(false, "[input] mouse-down %g %g button %d", 3.0, 4.0, 1);
    choco::diag::logInput(false, "[input] mouse-up %g %g button %d", 3.0, 4.0, 1);
    choco::diag::logInput(false, "[input] key %s", "A");
    choco::diag::logInput(false, "[input] text \"%s\"", "a");
  };
  run(false);
  choco::diag::setLogMotion(true);
  choco::diag::logInput(true, "[input] mouse-move %g %g", 5.0, 6.0);
  choco::diag::setLogMotion(false);
  choco::diag::closeLog();
  std::ifstream f(path);
  const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  CHECK(text.find("[input] mouse-move 1 2") == std::string::npos);
  CHECK(text.find("[input] mouse-move 5 6\n") != std::string::npos);
  CHECK(text.find("[input] mouse-down 3 4 button 1\n") != std::string::npos);
  CHECK(text.find("[input] mouse-up 3 4 button 1\n") != std::string::npos);
  CHECK(text.find("[input] key A\n") != std::string::npos);
  CHECK(text.find("[input] text \"a\"\n") != std::string::npos);
  f.close();  // Windows refuses to remove an open file
  fs::remove(path);
}

TEST_CASE("--cheatmode is --config cheatmode=1 and --run takes a file") {
  auto r = parseArgs({"--cheatmode", "--run", "x.lua", "--userdir", "u"});
  REQUIRE(r.ok());
  REQUIRE(r.options.configOverrides.size() == 1);
  CHECK(r.options.configOverrides[0] == std::make_pair(std::string("cheatmode"), std::string("1")));
  CHECK(r.options.runFile == "x.lua");
  CHECK(parseArgs({}).options.runFile.empty());
  CHECK_FALSE(parseArgs({"--run"}).ok());
}

TEST_CASE("--game picks a game; the other games' default user folders are refused for automated runs too") {
  CHECK(parseArgs({}).options.game.empty());
#ifdef CHOCO_GAME_CHOC2
  CHECK(parseArgs({"--game", "choc2"}).options.game == "choc2");
#endif
#ifdef CHOCO_GAME_CHOC1
  CHECK(parseArgs({"--game", "choc1"}).options.game == "choc1");
#endif
  CHECK(parseArgs({"--game", "choc3"}).options.game == "choc3");
  auto bad = parseArgs({"--game", "choc9"});
  CHECK_FALSE(bad.ok());
  CHECK(bad.error.find("--game must be") != std::string::npos);
  CHECK_FALSE(parseArgs({"--game"}).ok());
  // Only the games in the build are refused: a left-out game's folder is just a folder.
  std::vector<const char*> leaves;
#ifdef CHOCO_GAME_CHOC2
  leaves.push_back("Chocolatier2");
#endif
#ifdef CHOCO_GAME_CHOC1
  leaves.push_back("Chocolatier1");
#endif
  for (const char* leaf : leaves) {
    const std::string d = choco::platform::defaultUserDir(leaf);
    if (d.empty()) continue;
    CHECK_FALSE(parseArgs({"--headless", "--userdir", d}).ok());
    CHECK(parseArgs({"--userdir", d}).ok());  // a human windowed run may use it
  }
  CHECK(choco::platform::defaultUserDir("Chocolatier2") != choco::platform::defaultUserDir("Chocolatier3"));
}

TEST_CASE("--version names the engine and the build's version") {
  auto r = parseArgs({"--version"});
  REQUIRE(r.ok());
  CHECK(r.options.version);
  const std::string v = choco::app::versionText();
  CHECK(v.rfind("openconche ", 0) == 0);
  CHECK(v.size() > std::string("openconche \n").size());
  CHECK(v.back() == '\n');
  CHECK(choco::app::usageText().rfind(v, 0) == 0);  // --help opens with the same line
}
