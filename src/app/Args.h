#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>


namespace choco::app {

struct Options {
  std::string assetsDir;
  std::string userDir;  // empty = the detected game's default folder
  bool userDirExplicit = false;  // --userdir was given: the switcher keeps it across a game change
  std::string game;  // --game <module id>: skips detection (empty = detect)
  int scale = 1;
  bool fullscreen = false;
  bool headless = false;
  std::vector<std::pair<std::string, std::string>> configOverrides;  // --config key=value, over settings.xml
  bool dumpWindows = false;  // print the window tree with rects at exit
  bool testScene = false;  // draw the textured/blended/clipped test scene as well
  bool noAssets = false;   // never look for game data: always show the test scene
  bool testText = false;   // draw the font test scene (needs fonts/choco3.mvec and fonts/fertigo.mvec in --assets)
  int displayWidth = 2560;  // emulated desktop for --headless
  int displayHeight = 1440;
  std::string scriptFile;
  std::string runFile;  // --run: Lua run once when the port or map view first shows; F9 runs it again
  std::string screenshotPath;
  std::string logUnimplementedFile;
  bool logInput = false;  // --log-input: also log mouse-move lines
  std::string logFile;  // --log: every logged line also goes here with a ms timestamp
  long frames = 0;  // 0 = run until quit
  std::optional<std::uint32_t> seed;  // --seed; unset = 1 when headless, random otherwise
  std::string importPrefs;  // --import-prefs: copy of an original prefs.dat to import into --userdir, then exit
  bool importIntoDefault = false;  // --import-into-default: allow the import to target the default user folder
  bool finder = false;  // --finder: show the game finder (overrides the saved choice and the asset search)
  std::string addGame;   // --add-game <folder>: register the game in <folder> in the launcher's list, then exit
  std::string pickGame;  // --pick-game <variant>: run the finder's validation headless, then exit
  std::string pickDir;   // --pick-dir <folder>: the folder --pick-game resolves for the user
  bool help = false;
  bool version = false;  // --version: print the engine name and version, then exit
};

struct ParseResult {
  Options options;
  std::string error;  // empty on success
  bool shortError = false;  // the error is self-explanatory: print it without the usage text
  bool ok() const { return error.empty(); }
};

// args excludes argv[0]. The default assets directory is applied later, by App, when it exists.
ParseResult parseArgs(const std::vector<std::string>& args);

// True for flags that mean "not a human playing": --headless, --script, --frames, --screenshot, --dump-windows,
// --log-unimplemented, --no-assets, --pick-game. parseArgs rejects these without a --userdir other than the default
// user folder.
bool isAutomatedRun(const Options& o);

// Path comparisons after weakly_canonical + normalisation; case-insensitive on macOS and Windows (also for folders that do not
// exist yet). pathInside is true for equal paths too.
bool samePath(const std::string& a, const std::string& b);
bool pathInside(const std::string& child, const std::string& parent);
bool hasPathComponent(const std::string& path, const std::string& name);

std::string usageText();
// "openconche <version>\n": the version is CHOCO_VERSION, set from the project version at build time.
std::string versionText();

}  // namespace choco::app
