#include "app/Finder.h"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>

#include "app/Diagnostics.h"
#include "assets/AssetFS.h"
#include "games/Registry.h"
#include "gfx/text/TextLayout.h"
#include "platform/Console.h"
#include "platform/PathUtil.h"
#include "platform/Platform.h"
#include "platform/SdlRenderer.h"
#include "platform/UserDir.h"
#include "platform/Window.h"
#include "script/GameModule.h"

namespace choco::app {

namespace fs = std::filesystem;

const FinderVariant kFinderVariants[] = {
    {"choc1", "choc1", "Chocolatier", "Chocolatier", false},
    {"choc2", "choc2", "Chocolatier 2", "Chocolatier 2", false},
    {"choc3", "choc3", "Chocolatier 3", "Chocolatier 3", false},
    {"choc3-reforged", "choc3", "Chocolatier 3 (Reforged)", "Chocolatier 3 - Reforged", true},
};
const int kFinderVariantCount = 4;

const FinderVariant* findFinderVariant(const std::string& id) {
  for (int i = 0; i < kFinderVariantCount; ++i)
    if (id == kFinderVariants[i].id)
      return &kFinderVariants[i];
  return nullptr;
}

std::vector<std::string> resolveAssetCandidates(const std::string& picked) {
  std::vector<std::string> out;
  std::error_code ec;
  const fs::path root = platform::pathFromUtf8(picked);
  if (picked.empty() || !fs::is_directory(root, ec)) return out;
  auto add = [&](const fs::path& p) {
    if (fs::is_directory(p, ec)) out.push_back(platform::pathToUtf8(p.lexically_normal()));
  };
  add(root / "assets");
  add(root / "Contents" / "Resources" / "assets");
  // One level down, so picking the folder that holds the install (or the .app, on the Applications folder) works.
  // directory_iterator's order is unspecified, so the children are sorted by name first: the scan is stable
  // across platforms and ADD GAME's "first match names the game" is deterministic. Dot folders are skipped.
  // (filename() goes through pathToUtf8: MSVC's path::string() is the ANSI code page and throws on a name it
  // cannot represent.)
  std::vector<fs::path> children;
  for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string name = platform::pathToUtf8(it->path().filename());
    if (!name.empty() && name[0] == '.') continue;
    children.push_back(it->path());
  }
  std::sort(children.begin(), children.end());
  for (const fs::path& child : children) add(child / "assets");
  for (const fs::path& child : children) add(child / "Contents" / "Resources" / "assets");
  add(root);  // the assets folder itself, picked directly; last, so an install root wins
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

namespace {

// What a folder is, from an index of it already built: empty if it is `variant`, else why not. Detection does not
// depend on the variant, so a caller trying several variants builds the index once.
std::string matchVariant(const FinderVariant& variant, const assets::AssetFS& fsAssets, const std::string& assetsDir) {
  std::string why;
  const script::GameModule& module = games::detectGame(fsAssets, {}, why);
  const bool isReforged = fsAssets.exists("community/account.lua");
  if (std::string(module.profile().id) != variant.moduleId)
    return "this folder holds " + std::string(module.profile().windowTitle) + ", not " + variant.title +
           " (" + why + ")";
  if (module.profile().id == std::string("choc3") && isReforged != variant.reforged)
    return std::string(variant.reforged ? "this is the original Chocolatier 3, not the Reforged tree"
                                        : "this is the Reforged community tree, not the original Chocolatier 3");
  if (!fsAssets.exists(module.profile().mainloop))
    return "no game scripts in '" + assetsDir + "' (" + std::string(module.profile().mainloop) + " is missing)";
  return {};
}

}  // namespace

std::string validateAssetsForVariant(const FinderVariant& variant, const std::string& assetsDir) {
  std::error_code ec;
  if (!fs::is_directory(platform::pathFromUtf8(assetsDir), ec))
    return "no game found: '" + assetsDir + "' is not a folder";
  const assets::AssetFS fsAssets(assetsDir, {}, assets::AssetFS::Mode::Probe);
  if (!fsAssets.ok())
    return "no game found: " + fsAssets.error();
  return matchVariant(variant, fsAssets, assetsDir);
}

namespace {

// The engine's own preference folder (OpenConche; docs/design/game-finder.md section 3). One level, exactly where
// SDL_GetPrefPath would put it on each platform, because the per-game user folders (Chocolatier3 and friends)
// already use platform::defaultUserDir and SDL's (org, app) pair would add a nesting level no one asked for.
const char* kEngineLeaf = "OpenConche";
const char* kLibraryFile = "games.cfg";

// Parses `key = value`/`key=value` (no quoting; one space pair around '=' survives). Returns false on no '='.
bool parseLine(const std::string& line, std::string& key, std::string& value) {
  const auto eq = line.find('=');
  if (eq == std::string::npos) return false;
  key = line.substr(0, eq);
  value = line.substr(eq + 1);
  if (value.starts_with(' ')) value.erase(0, 1);
  if (key.ends_with(' ')) key.erase(key.size() - 1);
  return true;
}

std::string engineDir() { return platform::defaultUserDir(kEngineLeaf); }

}  // namespace

bool loadGameLibrary(GameLibrary& out, std::string& error) {
  out = GameLibrary{};
  out.path = engineDir() + "/" + kLibraryFile;
  std::ifstream in(platform::pathFromUtf8(out.path), std::ios::binary);
  if (!in.is_open()) {
    // An earlier finder wrote ChocolatierEngine/finder.txt; such a file migrates here.
    const std::string oldPath = platform::defaultUserDir("ChocolatierEngine") + "/finder.txt";
    std::ifstream old(platform::pathFromUtf8(oldPath), std::ios::binary);
    if (!old.is_open()) return true;  // never written: the finder is simply shown
    std::string line, key, value, variant;
    while (std::getline(old, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (!parseLine(line, key, value)) continue;
      if (key == "variant") variant = value;
      if (variant.empty()) continue;  // dir/assets before the variant line would make a nameless entry
      if (key == "dir") out.entries[variant].dir = value;
      if (key == "assets") out.entries[variant].assets = value;
    }
    out.last = variant;
    return true;
  }
  std::string line, key, value;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!parseLine(line, key, value)) continue;
    if (key == "last") {
      out.last = value;
      continue;
    }
    // "<variant>.dir" / "<variant>.assets" for each FinderVariant id.
    for (int i = 0; i < kFinderVariantCount; ++i) {
      const std::string id = kFinderVariants[i].id;
      if (key == id + ".dir") out.entries[id].dir = value;
      if (key == id + ".assets") out.entries[id].assets = value;
    }
  }
  if (!out.last.empty() && !out.entries.count(out.last)) {
    error = "games.cfg: last = '" + out.last + "' has no entry";
    return false;
  }
  return true;
}

bool saveGameLibrary(const GameLibrary& library, std::string& error) {
  const std::string dirPath = engineDir();
  if (dirPath.empty()) {
    error = "no preference folder (no home directory?)";
    return false;
  }
  std::error_code ec;
  fs::create_directories(platform::pathFromUtf8(dirPath), ec);
  if (ec) {
    error = "cannot create '" + dirPath + "': " + ec.message();
    return false;
  }
  const fs::path file = platform::pathFromUtf8(dirPath) / kLibraryFile;
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    error = "cannot write '" + platform::pathToUtf8(file) + "'";
    return false;
  }
  out << "# OpenConche game library, written by the game finder (docs/design/game-finder.md)\n";
  if (!library.last.empty()) out << "last = " << library.last << "\n";
  for (int i = 0; i < kFinderVariantCount; ++i) {
    const auto it = library.entries.find(kFinderVariants[i].id);
    if (it == library.entries.end()) continue;
    out << kFinderVariants[i].id << ".dir = " << it->second.dir << "\n"
        << kFinderVariants[i].id << ".assets = " << it->second.assets << "\n";
  }
  return out.good();
}

int finderPick(Options& o, const FinderVariant& variant, const std::string& picked) {
  const std::vector<std::string> candidates = resolveAssetCandidates(picked);
  if (candidates.empty()) {
    diag::out("[finder] no playable folder found inside '%s'\n", picked.c_str());
    return 3;
  }
  for (const std::string& cand : candidates) {
    const std::string why = validateAssetsForVariant(variant, cand);
    if (!why.empty()) {
      diag::out("[finder] %s: %s\n", cand.c_str(), why.c_str());
      continue;
    }
    o.assetsDir = cand;
    o.game = variant.moduleId;
    diag::out("[finder] variant %s, assets %s\n", variant.id, cand.c_str());
    return 0;
  }
  return 3;
}

AddGameResult addGameFromFolder(GameLibrary& library, const std::string& folder) {
  AddGameResult r;
  for (const std::string& cand : resolveAssetCandidates(folder)) {
    const assets::AssetFS probeFs(cand, {}, assets::AssetFS::Mode::Probe);  // once per folder, not per game
    if (!probeFs.ok()) continue;
    for (int v = 0; v < kFinderVariantCount && r.id.empty(); ++v)
      if (matchVariant(kFinderVariants[v], probeFs, cand).empty()) {
        r.id = kFinderVariants[v].id;
        r.assets = cand;
      }
    if (!r.id.empty()) break;
  }
  if (r.id.empty()) return r;  // NotFound
  const auto prev = library.entries.find(r.id);
  // Already registered: change nothing. (The same game from a DIFFERENT folder is not a duplicate - the entry
  // re-points, exactly as re-picking a saved choice always did.)
  if (prev != library.entries.end() && (prev->second.dir == folder || prev->second.assets == r.assets)) {
    r.status = AddGameStatus::AlreadyListed;
    return r;
  }
  library.entries[r.id] = {folder, r.assets};
  r.status = AddGameStatus::Added;
  if (!saveGameLibrary(library, r.error)) r.status = AddGameStatus::SaveFailed;
  diag::out("[finder] added %s: assets %s\n", r.id.c_str(), r.assets.c_str());
  return r;
}

bool bootFromLibrary(const FinderVariant& variant, const GameLibrary& library, Options& o, std::string& why) {
  why.clear();
  const auto it = library.entries.find(variant.id);
  if (it == library.entries.end() || it->second.dir.empty() || it->second.assets.empty()) {
    why = "no library entry";
    return false;
  }
  // A moved install: re-resolve from the folder the picker returned, then take the saved assets path as-is.
  std::vector<std::string> candidates = resolveAssetCandidates(it->second.dir);
  if (std::find(candidates.begin(), candidates.end(), it->second.assets) == candidates.end())
    candidates.insert(candidates.begin(), it->second.assets);
  for (const std::string& cand : candidates) {
    if (validateAssetsForVariant(variant, cand).empty()) {
      o.assetsDir = cand;
      o.game = variant.moduleId;
      return true;
    }
  }
  why = "the library entry no longer points at a playable folder";
  return false;
}

bool savedFinderChoice(Options& o) {
  GameLibrary library;
  std::string error;
  loadGameLibrary(library, error);
  if (!error.empty()) diag::out("[finder] %s (%s)\n", error.c_str(), library.path.c_str());
  const FinderVariant* variant = findFinderVariant(library.last);
  if (!variant) {
    if (!library.last.empty())
      diag::out("[finder] saved choice '%s' is not a known variant\n", library.last.c_str());
    return false;
  }
  std::string why;
  if (!bootFromLibrary(*variant, library, o, why)) {
    diag::out("[finder] saved choice is not playable (variant '%s': %s)\n", variant->id, why.c_str());
    return false;
  }
  diag::out("[finder] saved choice: variant %s, assets %s\n", variant->id, o.assetsDir.c_str());
  return true;
}

FinderOutcome runGameFinder(Options& o, std::string& why, const FinderHost& host) {
  why.clear();
  if (isAutomatedRun(o)) {
    // Not only --headless: any scripted run. The screen reads the machine's library and opens an OS dialog,
    // neither of which an automated run may do (the README's promise: they never depend on the home folder).
    // (The "the finder needs a display" prefix is what check_switcher greps for.)
    why = "the finder needs a display and a person (an automated run: pass --pick-game and --pick-dir, --assets, "
          "or --no-assets)";
    return FinderOutcome::Failed;
  }
  if (host.window) {
    diag::out("[finder] showing the game switcher\n");  // in-game: the App asked, something was found already
  } else if (platform::hasConsoleOutput()) {
    diag::out("[finder] no assets found: showing the game finder\n");
  }
  // Standalone: the finder owns its SDL stack. In-game (the switcher): the host's window and renderer are used,
  // and we must not create a second SdlRenderer - it would steal the window's flush hook from the game's renderer
  // and clear it on destruction, so the game would present stale batches after the switcher closes.
  std::unique_ptr<platform::Platform> sdl;
  std::unique_ptr<platform::Window> ownedWindow;
  std::unique_ptr<platform::SdlRenderer> ownedRenderer;
  platform::Window* window = host.window;
  gfx::Renderer* renderer = host.renderer;
  gfx::TextureUploader* uploader = host.uploader;
  if (!window) {
    sdl = std::make_unique<platform::Platform>(false);
    if (!sdl->ok()) {
      why = sdl->error();
      return FinderOutcome::Failed;
    }
    platform::WindowConfig cfg;
    cfg.scale = o.scale;
    cfg.fullscreen = o.fullscreen;
    cfg.title = "OpenConche";
    ownedWindow = std::make_unique<platform::Window>(*sdl, cfg);
    if (!ownedWindow->ok()) {
      why = ownedWindow->error();
      return FinderOutcome::Failed;
    }
    ownedRenderer = std::make_unique<platform::SdlRenderer>(*ownedWindow);
    window = ownedWindow.get();
    renderer = ownedRenderer.get();
    uploader = ownedRenderer.get();
  }
  // The folder-dialog callbacks below capture this frame's locals by reference. Whatever way this function
  // returns, the window forgets them first, so a dialog still open (the OS sheet cannot be closed from here)
  // completes into nothing instead of a dead stack frame. In game that is the window the game keeps using.
  struct ForgetPick {
    platform::Window* w;
    ~ForgetPick() { w->cancelFolderPick(); }
  } forgetPick{window};
  // The screen is fully PAINTED at runtime - no embedded PNG, no committed or generated data file (the user's
  // ask: the binary alone must be enough to distribute; the launcher appears before any game asset exists).
  // Backdrop one solid colour, cards and buttons plain squares (a body fill inside a one-pixel lighter border),
  // every word drawn LIVE in the engine's built-in font (a FontCache with no AssetFS, like the in-game badge).
  // Each control is this code's OWN rect - drawn, hovered and hit-tested by the same numbers - and it says
  // what the click will do: "Play" in gold when the variant's library entry still validates (click boots it),
  // "Browse" when it does not (the click opens the folder dialog), `<u>`-underlined on hover. A game going
  // stale flips its own label on the next open. The fifth button says "Exit" (standalone: the process ends)
  // or "Back to game" (the in-game switcher: it just closes). One card per game,
  // its state in `games` below.
  const bool inGame = host.window != nullptr;
  // One CARD per INSTALLED game (the redesigned screen's extension): the list shows only games the user has
  // registered - no empty placeholders - each card carrying the name as its primary element (16pt ink), the
  // configured directory small and dim beneath it (elided to the box; the full form reads in the footer while
  // the card is hovered), the PLAY button on the right, and a small X that UNREGISTERS the game: a games.cfg
  // erase, never a disk change. The four supported-but-not-installed games are not buttons - they live in the
  // information-only SUPPORTED GAMES line at the bottom. The ONE global ADD GAME button below the installed
  // list asks for a folder and lets the EXISTING detection name the game (the user never has to know which
  // game they are picking); adding does NOT launch anything - the card appears, `last` stays as it was, and
  // the user keeps choosing: Play, another add, or Exit.
  constexpr gfx::ColorF kBackdrop{28 / 255.0f, 15 / 255.0f, 10 / 255.0f, 1};
  constexpr gfx::ColorF kCardFill{44 / 255.0f, 26 / 255.0f, 16 / 255.0f, 1};
  constexpr gfx::ColorF kButtonBody{58 / 255.0f, 48 / 255.0f, 45 / 255.0f, 1};
  constexpr gfx::ColorF kButtonEdge{149 / 255.0f, 143 / 255.0f, 140 / 255.0f, 1};
  GameLibrary library;
  std::string libraryError;  // a stale file is tolerated; a successful pick below rewrites it whole
  loadGameLibrary(library, libraryError);
  struct CardUi {
    int variant = -1;  // which kFinderVariants entry this card shows
    gfx::RectI card{}, button{}, cross{};
    std::string dir;  // what the folder picker returned
    bool playable = false;
    gfx::text::TextLayout name, path, action, actionHover, crossDot;
  };
  CardUi games[4];  // games[0..count-1] are the installed list, in kFinderVariants order
  int count = 0;
  constexpr gfx::RectI kExitRect{660, 30, 108, 28};  // tucked top right, out of the list's way
  gfx::RectI addRect{};  // below the installed list: the one way a game is added
  // The live text, all of it engine-owned: sizes and colours only (no bold, no italics, no second face).
  gfx::text::FontCache uiFonts;
  gfx::text::GlyphAtlas uiAtlas(uploader);
  const gfx::ColorF kInk{1, 1, 1, 1};
  const gfx::ColorF kDim{0.72f, 0.62f, 0.50f, 1};
  const gfx::ColorF kGold{1.0f, 0.86f, 0.35f, 1};
  const auto makeStyle = [&](float size, const gfx::ColorF& color, bool center) {
    gfx::text::TextStyle st;
    st.font = uiFonts.builtin();
    st.fonts = &uiFonts;
    st.size = size;
    st.color = color;
    st.flags = (center ? gfx::text::kHAlignCenter : gfx::text::kHAlignLeft) + gfx::text::kVAlignTop;
    return st;
  };
  const auto makeText = [&](const std::string& s, float size, const gfx::ColorF& color, int tw, int th,
                            bool center = false) {
    return gfx::text::TextLayout::build(s, makeStyle(size, color, center), tw, th);
  };
  const auto widthAt = [&](const std::string& s, float size, const gfx::ColorF& color) {
    return gfx::text::TextLayout::measure(s, makeStyle(size, color, false), 4000, 40).w;
  };
  // The built-in font has no backslash, so a Windows path would draw with gaps. Shown with forward slashes (which
  // Windows accepts); the stored path is untouched.
  const auto shownPath = [](std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
  };
  // A directory too long for its box keeps its TAIL (a path's end carries the information - the folders'
  // names, not the /Users prefix): "..." plus the longest tail that fits; the full form still reads in the
  // footer while the card is hovered.
  // Tails start on a UTF-8 character boundary (never inside a multibyte sequence); a path is at most a few
  // hundred bytes and this runs once per list rebuild, so the linear search is fine.
  const auto elide = [&](const std::string& s, float size, const gfx::ColorF& color, int tw) {
    if (widthAt(s, size, color) <= tw) return s;
    for (std::size_t j = s.size(); j >= 1; --j) {
      if ((static_cast<unsigned char>(s[s.size() - j]) & 0xC0) == 0x80) continue;  // a continuation byte
      const std::string t = "..." + s.substr(s.size() - j);
      if (widthAt(t, size, color) <= tw) return t;
    }
    return std::string("...");
  };
  // The whole installed list, top to bottom, in kFinderVariants order - one layout per registered game, the
  // deck rebuilt whenever a game is added or unregistered. The same helper the click path uses decides
  // playable (so a moved install reads as a white Play whose click asks where the game went).
  const auto refreshList = [&]() {
    count = 0;
    for (int v = 0; v < kFinderVariantCount; ++v) {
      const auto it = library.entries.find(kFinderVariants[v].id);
      if (it == library.entries.end() || it->second.dir.empty() || it->second.assets.empty()) continue;
      CardUi& g = games[count++];
      g.variant = v;
      g.dir = it->second.dir;
      g.card = {60, 106 + (count - 1) * 86, 680, 80};
      g.button = {600, g.card.y + 44, 128, 32};
      g.cross = {706, g.card.y + 7, 22, 22};
      Options probe;
      std::string probeWhy;
      g.playable = bootFromLibrary(kFinderVariants[v], library, probe, probeWhy);
      g.name = makeText(kFinderVariants[v].label, 16, kInk, 480, 18);
      g.path = makeText(elide(shownPath(g.dir), 10, kDim, 480), 10, kDim, 480, 12);
      g.action = makeText("Play", 13, g.playable ? kGold : kInk, 128, 14, true);
      g.actionHover = makeText("<u>Play</u>", 13, g.playable ? kGold : kInk, 128, 14, true);
      g.crossDot = makeText("X", 10, kInk, 22, 14, true);
    }
    // The primary action sits right under the list - or, with nothing installed yet, under the empty-state
    // message - prominent either way.
    addRect = {60, count == 0 ? 150 : 106 + count * 86 + 2, 160, 36};
  };
  refreshList();
  // The chrome texts. The footer is contextual (the redesign spec's two forms: nothing chosen yet, or a
  // remembered game) and doubles as the path tooltip: while a card with a directory is hovered, it shows that
  // directory in full instead. The supported-games line is information only - every game OpenConche can run,
  // readable at a glance, never a button.
  const gfx::text::TextLayout title = makeText("OpenConche", 26, kInk, 580, 34, true);
  const gfx::text::TextLayout subtitle = makeText("an engine for the Chocolatier games", 12, kDim, 580, 16, true);
  const gfx::text::TextLayout heading = makeText("SELECT A GAME", 12, kInk, 680, 14);
  const gfx::text::TextLayout emptyMessage = makeText("No games added yet.", 12, kDim, 680, 14, true);
  const gfx::text::TextLayout supported = makeText(
      "SUPPORTED GAMES: Chocolatier / Chocolatier 2 / Chocolatier 3 / Chocolatier 3 - Reforged",
      10, kDim, 680, 12, true);
  const char* const exitText = inGame ? "Back to game" : "Exit";
  const gfx::text::TextLayout exitLabel = makeText(exitText, 12, kInk, 108, 14, true);
  const gfx::text::TextLayout exitHover =
      makeText(inGame ? "<u>Back to game</u>" : "<u>Exit</u>", 12, kInk, 108, 14, true);
  const gfx::text::TextLayout addLabel = makeText("+ ADD GAME", 13, kGold, 160, 14, true);
  const gfx::text::TextLayout addHover = makeText("<u>+ ADD GAME</u>", 13, kGold, 160, 14, true);
  const auto contextMessage = [&]() {
    std::string msg =
        "Select a game to get started. Your choice will be remembered and launched automatically next time.";
    if (const FinderVariant* last = findFinderVariant(library.last)) {
      Options probe;
      std::string probeWhy;
      if (bootFromLibrary(*last, library, probe, probeWhy))
        msg = std::string(last->label) +
              " will launch automatically next time. Press Ctrl+Shift+G or click the in-game "
              "badge to choose another game.";
    }
    return msg;
  };
  // The OS folder dialog could not be shown at all (not a cancel): say why, and what to do instead.
  const auto noDialog = [&](const std::string& reason) {
    if (reason.empty()) return;
    diag::err("[finder] no folder dialog: %s\n", reason.c_str());
    std::string msg = "No folder dialog is available on this system (" + reason +
                      ").\n\nAdd a game from the command line instead:\n  openconche --add-game <folder>";
#if defined(__linux__)
    msg += "\n\nOn Linux the dialog needs the desktop portal or zenity (Ubuntu: sudo apt install zenity).";
#endif
    platform::showWarning("OpenConche", msg);
  };
  gfx::text::TextLayout footer;
  int footerFor = -2;  // -1: the contextual line; 0..3: that card's full directory (rebuilt on change)
  int picked = -1;              // the variant whose Play clicked a stale card: its re-locate folder dialog
  bool dialogOpen = false;    // that re-locate folder dialog is up (not the ADD GAME one)
  bool dialogResolved = false;  // the re-locate dialog's callback fired
  std::string dialogPath;     // "" = cancelled
  bool addOpen = false;         // the ADD GAME folder dialog is up
  bool addResolved = false;
  std::string addPath;
  bool cancelled = false;
  bool exitArmed = false;  // Exit/Back pressed; it fires on the release, so the game never sees a stray MouseUp
  int hoveredCard = -1;  // the card under the mouse (the footer shows its full directory, if it has one)
  int hot = -1;          // the hovered control: i card i's Play, 4 Exit/Back, 100+i an X, 200 ADD GAME
  const auto inRect = [&](const gfx::RectI& r, double x, double y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
  };
  const auto saveChosen = [&](const FinderVariant& variant, const std::string& dir, const std::string& assets) {
    library.last = variant.id;
    library.entries[variant.id] = {dir, assets};
    if (!saveGameLibrary(library, libraryError))
      platform::showWarning("OpenConche", "The choice runs now but could not be saved: " + libraryError);
    return true;
  };
  while (true) {
    for (const platform::InputEvent& e : window->pollEvents()) {
      using T = platform::InputEvent::Type;
      if (e.type == T::Quit) {
        cancelled = true;  // also with a folder dialog open: ForgetPick above drops its late answer
      } else if (e.type == T::MouseUp) {
        // "Exit" / "Back to game" fires on the release over the button (a press elsewhere, or a drag off,
        // disarms it). Returning on the release means the game under the switcher does not inherit a MouseUp
        // for a press it never saw.
        if (exitArmed && e.inArea && inRect(kExitRect, e.x, e.y)) {
          why = "exit";
          return FinderOutcome::Exit;
        }
        exitArmed = false;
      } else if (e.type == T::MouseMove) {
        // Alive: the hovered action underlines its word, the X fills over, ADD GAME underlines, and the
        // footer follows the card.
        hoveredCard = -1;
        hot = -1;
        if (e.inArea) {
          for (int i = 0; i < count; ++i)
            if (inRect(games[i].card, e.x, e.y)) hoveredCard = i;
          for (int i = 0; i < count && hot < 0; ++i) {
            if (inRect(games[i].button, e.x, e.y)) hot = i;
            if (inRect(games[i].cross, e.x, e.y)) hot = 100 + i;
          }
          if (hot < 0 && inRect(addRect, e.x, e.y)) hot = 200;
          if (hot < 0 && inRect(kExitRect, e.x, e.y)) hot = 4;
        }
      } else if (e.type == T::MouseDown && e.inArea && !dialogOpen && !addOpen && picked < 0) {
        if (inRect(kExitRect, e.x, e.y)) {  // "Exit" / "Back to game": fires on the MouseUp above
          exitArmed = true;
          continue;
        }
        if (inRect(addRect, e.x, e.y)) {
          // The ONE way a game is added: ask for a folder and let the existing detection name the game.
          // Nothing launches from here - the card appears, `last` stays as it was, and the user keeps
          // choosing (Play, another add, or Exit).
          addOpen = true;
          window->pickFolder([&](const std::optional<std::string>& folder) {
            addPath = folder ? *folder : std::string();
            addResolved = true;
            if (!folder) noDialog(window->folderPickError());
          });
          diag::out("[finder] adding a game\n");
        }
        for (int i = 0; i < count; ++i) {
          if (!inRect(games[i].cross, e.x, e.y)) continue;
          // The X unregisters the game (a games.cfg erase - nothing on disk); if it was the automatic
          // launch, that goes with it. The list rebuilds in place.
          const FinderVariant& variant = kFinderVariants[games[i].variant];
          library.entries.erase(variant.id);
          if (library.last == variant.id) library.last.clear();
          if (!saveGameLibrary(library, libraryError))
            platform::showWarning("OpenConche", "The entry is cleared but could not be saved: " + libraryError);
          diag::out("[finder] cleared the %s entry\n", variant.id);
          refreshList();
          footerFor = -2;  // the contextual line may have named this game
        }
        int pickCard = -1;
        for (int i = 0; i < count; ++i)
          if (inRect(games[i].button, e.x, e.y)) pickCard = i;
        if (pickCard >= 0) {
          picked = games[pickCard].variant;
          const FinderVariant& variant = kFinderVariants[picked];
          std::string bootWhy;
          if (Options boot; bootFromLibrary(variant, library, boot, bootWhy)) {
            // A registered game that still validates: one click boots it, and `last` remembers it for the
            // automatic launch. A stale entry falls through to its re-locate folder dialog below - the
            // click still reads "Play" (the games are gone somewhere: the picker asks where they went).
            saveChosen(variant, library.entries[variant.id].dir, library.entries[variant.id].assets);
            // Only the game changes: --scale, --fullscreen, --userdir, --log and the rest of `o` stay as the
            // command line set them (a whole-struct assignment here once reset them all).
            o.assetsDir = boot.assetsDir;
            o.game = boot.game;
            diag::out("[finder] chose %s: assets %s\n", variant.id, o.assetsDir.c_str());
            return FinderOutcome::Picked;
          }
          dialogOpen = true;
          window->pickFolder([&](const std::optional<std::string>& folder) {
            dialogPath = folder ? *folder : std::string();
            dialogResolved = true;
            if (!folder) noDialog(window->folderPickError());
          });
          diag::out("[finder] picking a folder for %s\n", variant.title);
        }
      }
    }
    if (dialogResolved) {
      dialogResolved = false;
      dialogOpen = false;
      if (dialogPath.empty()) {  // cancelled: back to the banner
        picked = -1;
      } else if (picked >= 0) {
        const FinderVariant& variant = kFinderVariants[picked];
        std::string assets;
        std::string failWhy;
        for (const std::string& cand : resolveAssetCandidates(dialogPath)) {
          const std::string why2 = validateAssetsForVariant(variant, cand);
          if (!why2.empty()) {
            if (failWhy.empty())
              failWhy = why2;
            continue;
          }
          assets = cand;
          break;
        }
        // Error message: the first candidate's reason, or that nothing was found inside. Then back to the banner.
        if (assets.empty()) {
          const std::string msg =
              resolveAssetCandidates(dialogPath).empty()
                  ? "The folder holds no game: pick the game's install folder (the one containing 'assets')."
                  : failWhy;
          platform::showWarning("OpenConche", msg + "\n\n" +
                                             (inGame
                                                  ? "Pick again, or close the window to return to the game."
                                                  : "Pick again, or close the window to quit."));
        } else {
          saveChosen(variant, dialogPath, assets);
          o.assetsDir = assets;
          o.game = variant.moduleId;
          diag::out("[finder] chose %s: assets %s\n", variant.id, assets.c_str());
          return FinderOutcome::Picked;
        }
        picked = -1;
      }
    }
    if (addResolved) {
      addResolved = false;
      addOpen = false;
      if (!addPath.empty()) {
        // Detection at work: the first variant that validates over the folder's candidates names the game, and
        // the user never had to. Cancelling the picker is not an outcome; an empty folder is the message box.
        const AddGameResult added = addGameFromFolder(library, addPath);
        if (added.status == AddGameStatus::NotFound) {
          platform::showWarning("OpenConche",
                               "No supported game was found in '" + addPath +
                                   "'.\n\nThe supported games are listed at the bottom of the screen.");
        } else if (added.status == AddGameStatus::AlreadyListed) {
          platform::showWarning("OpenConche",
                                std::string(findFinderVariant(added.id)->title) + " is already in the list.");
        } else {
          if (added.status == AddGameStatus::SaveFailed)
            platform::showWarning("OpenConche", "The game is added but could not be saved: " + added.error);
          refreshList();
          footerFor = -2;  // the contextual line may need its verdict again
        }
      }
    }
    if (cancelled) {
      why = "the finder was closed without a choice";
      return FinderOutcome::Cancelled;
    }
    window->beginFrame();
    renderer->fillRect({0, 0, 800, 600}, kBackdrop, gfx::Blend::Opaque);
    // The header (Exit/Back tucked top right, out of the content's way), the heading, the installed list or
    // its empty state, the ADD GAME action under it, the information-only supported-games line, the footer.
    // The hit tests use the very rects drawn here.
    title.draw(*renderer, uiAtlas, {110, 26});
    subtitle.draw(*renderer, uiAtlas, {110, 64});
    renderer->fillRect({float(kExitRect.x), float(kExitRect.y), float(kExitRect.w), float(kExitRect.h)},
                       kButtonEdge, gfx::Blend::Opaque);
    renderer->fillRect({float(kExitRect.x + 1), float(kExitRect.y + 1), float(kExitRect.w - 2),
                        float(kExitRect.h - 2)},
                        kButtonBody, gfx::Blend::Opaque);
    (hot == 4 ? exitHover : exitLabel).draw(*renderer, uiAtlas,
                                            {float(kExitRect.x), float(kExitRect.y + 7)});
    heading.draw(*renderer, uiAtlas, {60, 90});
    if (count == 0)
      emptyMessage.draw(*renderer, uiAtlas, {60, 118});
    for (int i = 0; i < count; ++i) {
      const CardUi& g = games[i];
      renderer->fillRect({float(g.card.x), float(g.card.y), float(g.card.w), float(g.card.h)}, kCardFill,
                         gfx::Blend::Opaque);
      g.name.draw(*renderer, uiAtlas, {84, float(g.card.y + 6)});
      g.path.draw(*renderer, uiAtlas, {84, float(g.card.y + 28)});
      const bool xHot = hot == 100 + i;
      renderer->fillRect({float(g.cross.x), float(g.cross.y), float(g.cross.w), float(g.cross.h)},
                         kButtonEdge, gfx::Blend::Opaque);
      if (!xHot)
        renderer->fillRect({float(g.cross.x + 1), float(g.cross.y + 1), float(g.cross.w - 2),
                            float(g.cross.h - 2)},
                            kButtonBody, gfx::Blend::Opaque);
      g.crossDot.draw(*renderer, uiAtlas, {float(g.cross.x), float(g.cross.y + 5)});
      renderer->fillRect({float(g.button.x), float(g.button.y), float(g.button.w), float(g.button.h)},
                         kButtonEdge, gfx::Blend::Opaque);
      renderer->fillRect({float(g.button.x + 1), float(g.button.y + 1), float(g.button.w - 2),
                          float(g.button.h - 2)},
                          kButtonBody, gfx::Blend::Opaque);
      (hot == i ? g.actionHover : g.action).draw(*renderer, uiAtlas,
                                                 {float(g.button.x), float(g.button.y + 9)});
    }
    // ADD GAME: the primary action, gold on the wider button, underlined on hover.
    renderer->fillRect({float(addRect.x), float(addRect.y), float(addRect.w), float(addRect.h)},
                       kButtonEdge, gfx::Blend::Opaque);
    renderer->fillRect({float(addRect.x + 1), float(addRect.y + 1), float(addRect.w - 2),
                        float(addRect.h - 2)},
                        kButtonBody, gfx::Blend::Opaque);
    (hot == 200 ? addHover : addLabel).draw(*renderer, uiAtlas, {float(addRect.x), float(addRect.y + 11)});
    supported.draw(*renderer, uiAtlas, {60, 502});
    // The footer: the contextual line, or the hovered card's full directory while one is hovered.
    const int footerWant = (hoveredCard >= 0 && !games[hoveredCard].dir.empty()) ? hoveredCard : -1;
    if (footerWant != footerFor) {
      footerFor = footerWant;
      footer = footerWant >= 0 ? makeText(shownPath(games[footerWant].dir), 10, kDim, 680, 28, true)
                                : makeText(contextMessage(), 11, kDim, 680, 30, true);
    }
    footer.draw(*renderer, uiAtlas, {60, 552});
    window->endFrame();
    window->present();
    // The finder has no work to do between frames; a small sleep keeps it off the CPU (vsync may already do this).
    SDL_Delay(10);
  }
}

}  // namespace choco::app
