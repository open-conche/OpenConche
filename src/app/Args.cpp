#include "app/Args.h"

#include <algorithm>
#include <charconv>
#include <filesystem>

#include "app/Finder.h"
#include "games/Registry.h"
#include "platform/PathUtil.h"
#include "platform/UserDir.h"

namespace choco::app {

namespace {

// The registry's ids joined by `sep`, the last one by `lastSep`: "choc3, choc2 or choc1".
std::string gameIdsText(const char* sep, const char* lastSep) {
  const auto& mods = games::modules();
  std::string s;
  for (std::size_t i = 0; i < mods.size(); ++i) {
    if (i) s += (i + 1 == mods.size()) ? lastSep : sep;
    s += mods[i]->profile().id;
  }
  return s;
}

bool parseInt(const std::string& s, long& out) {
  if (s.empty()) return false;
  const char* end = s.data() + s.size();
  auto [p, ec] = std::from_chars(s.data(), end, out);
  return ec == std::errc() && p == end;
}

std::string pathKey(const std::string& p) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path c = fs::weakly_canonical(platform::pathFromUtf8(p), ec);
  if (ec) c = fs::absolute(platform::pathFromUtf8(p), ec);
  c = c.lexically_normal();
  if (!c.has_filename()) c = c.parent_path();  // "a/b/" and "a/b" are the same folder
  const std::u8string u = c.generic_u8string();
  std::string k(u.begin(), u.end());
#if defined(__APPLE__) || defined(_WIN32)
  for (char& ch : k)  // these file systems are case-insensitive by default
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
#endif
  return k;
}

}  // namespace

bool samePath(const std::string& a, const std::string& b) { return pathKey(a) == pathKey(b); }

bool pathInside(const std::string& child, const std::string& parent) {
  const std::string c = pathKey(child);
  std::string p = pathKey(parent);
  if (c == p) return true;
  if (p.empty() || p.back() != '/') p += '/';
  return c.compare(0, p.size(), p) == 0;
}

bool hasPathComponent(const std::string& path, const std::string& name) {
  std::string lowName = name;
  for (char& ch : lowName)
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  const std::string k = pathKey(path);
  std::size_t i = 0;
  while (i <= k.size()) {
    std::size_t e = k.find('/', i);
    if (e == std::string::npos) e = k.size();
    std::string comp = k.substr(i, e - i);
    for (char& ch : comp)
      if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    if (comp == lowName) return true;
    i = e + 1;
  }
  return false;
}

bool isAutomatedRun(const Options& o) {
  return o.headless || !o.scriptFile.empty() || o.frames > 0 || !o.screenshotPath.empty() ||
         o.dumpWindows || !o.logUnimplementedFile.empty() || o.noAssets || !o.pickGame.empty();
}

ParseResult parseArgs(const std::vector<std::string>& args) {
  ParseResult r;
  Options& o = r.options;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    auto value = [&](std::string& dst) {
      if (i + 1 >= args.size()) {
        r.error = "missing value for " + a;
        return false;
      }
      dst = args[++i];
      return true;
    };
    if (a == "--assets") { if (!value(o.assetsDir)) return r; }
    else if (a == "--userdir") { if (!value(o.userDir)) return r; o.userDirExplicit = true; }
    else if (a == "--game") {
      std::string v;
      if (!value(v)) return r;
      if (!games::findModule(v)) {
        r.error = "--game must be " + gameIdsText(", ", " or ") + ", got '" + v + "'";
        return r;
      }
      o.game = v;
    }
    else if (a == "--import-prefs") { if (!value(o.importPrefs)) return r; }
    else if (a == "--import-into-default") o.importIntoDefault = true;
    else if (a == "--script") { if (!value(o.scriptFile)) return r; }
    else if (a == "--run") { if (!value(o.runFile)) return r; }
    else if (a == "--cheatmode") o.configOverrides.emplace_back("cheatmode", "1");
    else if (a == "--screenshot") { if (!value(o.screenshotPath)) return r; }
    else if (a == "--log") { if (!value(o.logFile)) return r; }
    else if (a == "--log-input") o.logInput = true;
    else if (a == "--log-unimplemented") { if (!value(o.logUnimplementedFile)) return r; }
    else if (a == "--config") {
      std::string v;
      if (!value(v)) return r;
      auto eq = v.find('=');
      if (eq == std::string::npos || eq == 0) {
        r.error = "--config must be key=value, got '" + v + "'";
        return r;
      }
      o.configOverrides.emplace_back(v.substr(0, eq), v.substr(eq + 1));
    } else if (a == "--scale") {
      std::string v;
      long n = 0;
      if (!value(v)) return r;
      if (!parseInt(v, n) || (n != 1 && n != 2)) {
        r.error = "--scale must be 1 or 2, got '" + v + "'";
        return r;
      }
      o.scale = static_cast<int>(n);
    } else if (a == "--seed") {
      std::string v;
      if (!value(v)) return r;
      std::uint32_t sv = 0;
      auto [sp, sec] = std::from_chars(v.data(), v.data() + v.size(), sv);
      if (v.empty() || sec != std::errc() || sp != v.data() + v.size()) {
        r.error = "--seed must be an integer from 0 to 4294967295, got '" + v + "'";
        return r;
      }
      o.seed = sv;
    } else if (a == "--frames") {
      std::string v;
      if (!value(v)) return r;
      if (!parseInt(v, o.frames) || o.frames < 1) {
        r.error = "--frames must be a positive integer, got '" + v + "'";
        return r;
      }
    } else if (a == "--display-size") {
      std::string v;
      if (!value(v)) return r;
      auto x = v.find('x');
      long w = 0, h = 0;
      if (x == std::string::npos || !parseInt(v.substr(0, x), w) || !parseInt(v.substr(x + 1), h) || w < 1 || h < 1 ||
          w > 16384 || h > 16384) {
        r.error = "--display-size must be WxH, e.g. 2560x1440, got '" + v + "'";
        return r;
      }
      o.displayWidth = static_cast<int>(w);
      o.displayHeight = static_cast<int>(h);
    } else if (a == "--fullscreen") o.fullscreen = true;
    else if (a == "--headless") o.headless = true;
    else if (a == "--finder") o.finder = true;
    else if (a == "--add-game") { if (!value(o.addGame)) return r; }
    else if (a == "--pick-game") {
      if (!value(o.pickGame)) return r;
      if (!findFinderVariant(o.pickGame)) {
        r.error = "--pick-game must be choc1, choc2, choc3 or choc3-reforged, got '" + o.pickGame + "'";
        r.shortError = true;
        return r;
      }
    } else if (a == "--pick-dir") {
      if (!value(o.pickDir)) return r;
    }
    else if (a == "--test-scene") o.testScene = true;
    else if (a == "--no-assets") o.noAssets = true;
    else if (a == "--dump-windows") o.dumpWindows = true;
    else if (a == "--test-text") o.testText = true;
    else if (a == "--help" || a == "-h") o.help = true;
    else if (a == "--version") o.version = true;
    else {
      r.error = "unknown argument '" + a + "'";
      return r;
    }
  }
  if (o.noAssets && !o.assetsDir.empty()) r.error = "--no-assets conflicts with --assets";
  if (!o.pickGame.empty() &&
      (o.pickDir.empty() || (!o.scriptFile.empty() || o.frames > 0 || !o.screenshotPath.empty())))
    r.error = "--pick-game <variant> --pick-dir <folder> is a finder probe: it needs --pick-dir and no other automated flags";
  if (r.error.empty() && o.finder && o.noAssets)
    r.error = "--finder conflicts with --no-assets";
  // --import-prefs is a Chocolatier 3 command, so "the default" here is that game's folder.
  const std::string choc3Default = platform::defaultUserDir(games::findModule("choc3")->profile().userDirName);
  if (r.error.empty() && !o.help && !o.version) {
    const std::string def = choc3Default;
    if (o.importIntoDefault && o.importPrefs.empty()) {
      r.error = "--import-into-default only makes sense with --import-prefs";
      r.shortError = true;
    } else if (!o.importPrefs.empty() && !o.importIntoDefault && (o.userDir.empty() || samePath(o.userDir, def))) {
      r.error = "--import-prefs writes into the user folder, so it needs --userdir <dir> other than the default (" + def +
                "); to import into your real profile pass --import-into-default as well";
      r.shortError = true;
    } else if (!o.importPrefs.empty()) {
      const std::string target = o.userDir.empty() ? def : o.userDir;
      std::filesystem::path srcParent = platform::pathFromUtf8(o.importPrefs).parent_path();
      if (srcParent.empty()) srcParent = ".";
      if (!target.empty() && pathInside(target, platform::pathToUtf8(srcParent)))
        r.error = "--userdir " + target + " is the folder of the file being imported (or inside it): the import would write "
                  "next to the source; use a different folder";
      else if (!target.empty() && hasPathComponent(target, "PlayFirst"))
        r.error = "--userdir " + target + " is inside a PlayFirst folder, which holds the original game's files: use a different folder";
      r.shortError = !r.error.empty();
    }
  }
  if (r.error.empty() && !o.help && !o.version && isAutomatedRun(o)) {
    const std::string def = choc3Default;
    r.shortError = true;
    if (o.userDir.empty())
      r.error = "automated runs must not use the default user folder (" + def + "): pass --userdir <scratch dir>";
    else {
      r.shortError = false;
      // The default folder of every game: --game or detection may pick any of them.
      for (const script::GameModule* m : games::modules()) {
        const std::string d = platform::defaultUserDir(m->profile().userDirName);
        if (d.empty() || !samePath(o.userDir, d)) continue;
        r.shortError = true;
        r.error = "--userdir " + o.userDir + " is the default user folder (" + d +
                  "), which automated runs must not use: pass --userdir <scratch dir>";
        break;
      }
    }
  }
  return r;
}

std::string versionText() { return std::string("openconche ") + CHOCO_VERSION + "\n"; }

std::string usageText() {
  // The --game column is 27 wide, as for the other options.
  std::string gameOpt = "--game " + gameIdsText("|", "|");
  gameOpt.resize(std::max<std::size_t>(gameOpt.size() + 1, 27), ' ');
  return versionText() + std::string("usage: openconche [options]\n"
         "  --no-assets                skip the default game-data search and show the test scene\n"
         "  --assets <dir>             game data (default: the launcher's saved choice; without one, interactive runs\n"
         "                             show the game launcher, automated runs the test scene)\n"
         "  --finder                   show the game finder: pick a game on the banner, then its folder in the OS picker\n"
         "                             (the first launch shows it when nothing is found; the choice is saved)\n"
         "  --add-game <folder>        add the game in <folder> to the launcher's list (no window), then exit; for systems\n"
         "                             without a folder dialog. Exit 0 added or already listed, 3 no supported game found\n"
         "  --pick-game <variant>     with --pick-dir: the finder's check without the window, then exit (automated):\n"
         "  --pick-dir <folder>        choc1, choc2, choc3 or choc3-reforged; exit 0 for a playable folder, 3 if not\n"
         "  ") + gameOpt + std::string("which game the assets are (default: detected from settings.xml and the scripts)\n"
         "  --userdir <dir>            folder for settings and saves (required with --headless, --script, --frames,\n"
         "                             --screenshot, --dump-windows, --log-unimplemented, --no-assets or --pick-game;\n"
         "                             must not be the default)\n"
         "  --import-prefs <file>      import a COPY of the original game's prefs.dat (users, prefs, saves) into --userdir after backing\n"
         "                             it up to <userdir>-backup-<timestamp>, print a summary and exit; never touches the file\n"
         "  --import-into-default      allow --import-prefs to write into the default user folder (your real profile)\n"
         "  --scale 1|2                initial window size 800x600 or 1600x1200 (falls back if it does not fit); the window\n"
         "                             is resizable and the picture is letterboxed to whatever size it gets\n"
         "  --fullscreen               borderless desktop fullscreen, 4:3 letterboxed (Alt+Enter, F11 or the OS control toggle it)\n"
         "  --headless                 no display: dummy video driver, software renderer, simulated 60 Hz clock\n"
         "  --test-scene               also draw the textured, blended and clipped test scene (needs image/checker, quads, white)\n"
         "  --test-text                also draw the font test scene (needs fonts/choco3.mvec and fonts/fertigo.mvec)\n"
         "  --display-size WxH         emulated desktop size for --headless fullscreen (default 2560x1440)\n"
         "  --script <file>            run a test script (see AGENTS.md)\n"
         "  --run <file.lua>           play-testing: run the Lua file when the port or map view first shows (F9 runs it again)\n"
         "  --cheatmode                the same as --config cheatmode=1: the games' own cheat mode (Chocolatier 3's dev toolbar,\n"
         "                             Chocolatier 1's cheat bar and menu cheats; Chocolatier 2's scripts never read it)\n"
         "  --screenshot <path>        write a PNG of the last frame (after the script, or after --frames n; default 1 frame)\n"
         "  --dump-windows             print the window tree with rects at exit\n"
         "  --config key=value         set a config value over settings.xml (repeatable), e.g. cheatmode=1 enables debug logging\n"
         "  --log <file>               also write every log line to the file with a ms timestamp; F10 writes a diagnostics report\n"
         "  --log-input                with --log, also log mouse-move lines (dropped by default; [input] lines never go to the console)\n"
         "  --log-unimplemented <file> write stubbed calls with counts at exit\n"
         "  --seed <n>                 seed for RandRange (default 1 when headless, time-based otherwise)\n"
         "  --frames <n>               quit after n frames\n");
}

}  // namespace choco::app
