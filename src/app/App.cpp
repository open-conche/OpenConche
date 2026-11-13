#include "app/App.h"

#include <SDL3/SDL_timer.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>

#include "app/Diagnostics.h"
#include "app/Finder.h"
#include "app/ScriptDriver.h"
#include "assets/AssetFS.h"
#include "assets/Content.h"
#include "audio/Audio.h"
#include "audio/Prefs.h"
#include "games/Registry.h"
#include "games/choc3/ImportCommand.h"
#include "gfx/Renderer.h"
#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"
#include "gfx/text/TextLayout.h"
#include "platform/AudioDevice.h"
#include "platform/Console.h"
#include "platform/PathUtil.h"
#include "platform/Platform.h"
#include "platform/SdlRenderer.h"
#include "platform/UserDir.h"
#include "platform/Window.h"
#include "script/AudioBridge.h"
#include "script/ScriptHost.h"
#include "ui/VolumeSliders.h"

namespace choco::app {

namespace {

using platform::Color;
using platform::Rect;

constexpr const char *kRunKey =
    "F9"; // re-runs the --run file (unused by the game)
constexpr const char *kDiagnosticsKey =
    "F10"; // writes the diagnostics report to the log
constexpr const char *kSwitchKey =
    "G"; // SDL names letter keys in capitals; with Ctrl/Cmd+Shift: the game switcher (the shifted games read no
         // such chord; the platform's injectKey names are case-insensitive, the EVENT's key is what SDL returns)
constexpr double kHeadlessFrameMs = 1000.0 / 60.0;
// The game switcher (the badge, the chord, the finder's dialogs) is a desktop feature: the web page is one game,
// chosen on the site's index page (packaging/web/index.html), and it has no folder dialog or games list.
#ifdef __EMSCRIPTEN__
constexpr bool kSwitcherAvailable = false;
#else
constexpr bool kSwitcherAvailable = true;
#endif

// The OpenConche badge (docs/design/game-finder.md section 2a): a small engine-drawn strip on top of whatever the
// game drew, naming the engine and teaching the switcher - the chord, its one sentence spells out, because the
// strip itself is transient: drawn and clickable for the first kBadgeVisibleMs of the run only (the user's
// asks), then gone forever, and Ctrl/Cmd+Shift+G is what remains. Drawn in windowed runs only
// (interactive runs; every ctest is headless), with the engine's BUILT-IN font, so no game asset is involved.
// The text wraps to two centered 14pt lines in the 320-wide strip (the windowed pixel probe verifies the fit).
constexpr gfx::RectI kBadgeRect{466, 14, 320, 40};
constexpr double kBadgeVisibleMs = 10000.0;  // the strip's lifetime, wall clock (SDL ticks) in windowed runs

// Textures for the extended test scene (--test-scene with an assets folder
// holding image/checker, quads, white).
struct SceneTextures {
  std::shared_ptr<gfx::Texture> checker, quads, white;
  bool ready() const { return checker && quads && white; }
};

// Font test scene (--test-text). Boxes are relied on by tests/check_text.py.
struct TextScene {
  static constexpr gfx::RectI kTitleBox{80, 20, 640, 120};
  static constexpr gfx::RectI kParaBox{100, 370, 600, 150};
  static constexpr gfx::RectI kLabelBox{
      260, 160, 280, 60}; // menu-label look: white fill, black outline, on red
  std::vector<std::pair<gfx::text::TextLayout, gfx::PointF>> layouts;
  void build(gfx::text::FontCache &fonts) {
    using namespace gfx::text;
    TextStyle title;
    title.font = fonts.get("fonts/choco3.mvec");
    title.size = 48;
    title.color = {1.0f, 0.86f, 0.35f, 1.0f};
    title.flags = kHAlignCenter + kVAlignCenter;
    layouts.emplace_back(
        TextLayout::build("Chocolatier", title, kTitleBox.w, kTitleBox.h),
        gfx::PointF{float(kTitleBox.x), float(kTitleBox.y)});
    TextStyle para;
    para.font = fonts.get("fonts/fertigo.mvec");
    para.fonts = &fonts;
    para.size = 24;
    para.color = {1, 1, 1, 1};
    layouts.emplace_back(
        TextLayout::build("Welcome to the <b>chocolate</b> trade, where a "
                          "<i>patient</i> baker can turn cocoa, sugar and "
                          "a little cream into a fortune. Sail from port to "
                          "port, <u>buy low</u>, sell high, and never "
                          "forget Supercalifragilisticexpialidocious-sized "
                          "words are broken per character when they do "
                          "not fit on a line at all.",
                          para, kParaBox.w, kParaBox.h),
        gfx::PointF{float(kParaBox.x), float(kParaBox.y)});
    TextStyle label = para;
    label.size = 32;
    label.flags = kHAlignCenter + kVAlignCenter;
    layouts.emplace_back(
        TextLayout::build("<outline color='000000' size=2>Story Mode</outline>",
                          label, kLabelBox.w, kLabelBox.h),
        gfx::PointF{float(kLabelBox.x), float(kLabelBox.y)});
  }
};

void fill(gfx::Renderer &r, platform::Rect rc, Color c,
          gfx::Blend b = gfx::Blend::Opaque) {
  r.fillRect({rc.x, rc.y, rc.w, rc.h},
             {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f}, b);
}

// Test scene: proves the pipeline, letterboxing and scale. Colours are relied
// on by tests/check_scene.py.
void drawTestScene(gfx::Renderer &r, const SceneTextures *tex) {
  const Color bg{24, 24, 40}, white{255, 255, 255};
  fill(r, {0, 0, platform::kLogicalWidth, platform::kLogicalHeight},
       bg);                       // the logical area only; bars stay black
  fill(r, {1, 1, 798, 1}, white); // 1px outline
  fill(r, {1, 598, 798, 1}, white);
  fill(r, {1, 1, 1, 598}, white);
  fill(r, {798, 1, 1, 598}, white);
  fill(r, {300, 250, 200, 100}, {200, 120, 40});
  fill(r, {10, 10, 50, 50}, {220, 40, 40});
  fill(r, {740, 10, 50, 50}, {40, 200, 60});
  fill(r, {10, 540, 50, 50}, {50, 90, 230});
  fill(r, {740, 540, 50, 50}, {240, 220, 40});
  if (!tex)
    return;

  // Extended scene, all below y=400 and clear of the probes above.
  if (tex->ready()) {
    const gfx::ColorF none{1, 1, 1, 1};
    r.drawTexture(*tex->checker, {}, {100, 400, 64, 64}, none,
                  gfx::Blend::Normal);
    r.drawTexture(*tex->quads, {}, {200, 400, 64, 64}, none,
                  gfx::Blend::Opaque);
    r.drawTexture(*tex->checker, {}, {100, 480, 64, 64}, none,
                  gfx::Blend::Normal, 0, {}, true, false); // flipped
    r.drawTexture(*tex->white, {}, {300, 400, 40, 40}, {1.0f, 0.5f, 0.0f, 1.0f},
                  gfx::Blend::Normal); // tinted
    r.drawTexture(*tex->white, {}, {500, 450, 40, 40},
                  {100 / 255.0f, 50 / 255.0f, 0, 1},
                  gfx::Blend::Additive); // additive texture
    r.drawTexture(*tex->white, {}, {350, 400, 40, 40}, {1, 1, 1, 0.5f},
                  gfx::Blend::Normal); // half alpha
  }
  r.fillRect({300, 450, 40, 40}, {100 / 255.0f, 50 / 255.0f, 0, 1},
             gfx::Blend::Additive);
  r.fillRect({350, 450, 40, 40}, {0.5f, 0.5f, 0.5f, 1}, gfx::Blend::Multiply);
  r.fillRect({400, 450, 40, 40}, {20 / 255.0f, 10 / 255.0f, 0, 1},
             gfx::Blend::Subtract);
  { // untextured geometry, additive: two triangles
    const gfx::ColorF c{100 / 255.0f, 50 / 255.0f, 0, 1};
    const gfx::Vertex gv[4] = {{{450, 450}, c, {}},
                               {{490, 450}, c, {}},
                               {{490, 490}, c, {}},
                               {{450, 490}, c, {}}};
    static constexpr int gi[6] = {0, 1, 2, 0, 2, 3};
    r.drawGeometry(nullptr, gv, gi, gfx::Blend::Additive);
  }
  // Two nested clips: the visible part is their intersection, (620,420) 20x20.
  r.pushClip({600, 400, 40, 40});
  r.pushClip({620, 420, 40, 40});
  r.fillRect({580, 380, 120, 120}, {0, 1, 0, 1}, gfx::Blend::Opaque);
  r.popClip();
  r.fillRect({600, 420, 20, 20}, {1, 0, 1, 1},
             gfx::Blend::Opaque); // still clipped to the outer rectangle
  r.popClip();
}

// The window title: the profile's string-table entry when it has one (read from the mounted game's own table, before the
// script host exists), else the profile's fixed title.
std::string windowTitleFor(const script::GameModule &module,
                           const assets::AssetFS *fs) {
  const script::GameProfile &p = module.profile();
  if (*p.titleString && fs) {
    assets::StringTable table;
    if (table.loadFile(*fs, p.stringsPath, assets::StringTable::Layer::Sdk,
                       nullptr)) {
      std::string title = table.sdkLookup(p.titleString);
      if (!title.empty())
        return title;
    }
  }
  return p.windowTitle;
}

bool readFile(const std::string &path, std::string &out) {
  std::ifstream f(platform::pathFromUtf8(path), std::ios::binary);
  if (!f)
    return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

bool takeScreenshot(platform::Window &w, const std::string &path,
                    bool logicalOnly) {
  platform::Image img;
  if (!(logicalOnly ? w.readLogicalPixels(img) : w.readPixels(img))) {
    diag::err("screenshot failed: %s\n", w.error().c_str());
    return false;
  }
  if (!platform::writePng(path, img)) {
    diag::err("could not write PNG '%s'\n", path.c_str());
    return false;
  }
  diag::out("[screenshot] %s %dx%d\n", path.c_str(), img.width, img.height);
  std::fflush(stdout);
  return true;
}

// One run of the engine, from the mounted game data to the exit save: run()'s state, as one object instead of
// locals, so the frame loop can be driven from outside. Native builds loop over step() themselves; the web build
// (Emscripten) hands step() to the browser, which calls it once per display frame and must get control back in
// between - nothing of the run may live on a stack frame across frames. Members are declared in the order the
// locals were, so they are destroyed in the same (reverse) order: the host first, then the windows (their
// destructors may read the context: a MapScreen lowers its world's gate), the context, the Lua state ... the
// renderer's textures before the renderer, the window before the platform, the platform last.
struct Session {
  explicit Session(Options &options) : o(options) {}
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;

  // Mounts the data, opens the window, boots the script. Returns -1 when the run goes on into the frame loop,
  // otherwise the exit code to return at once (nothing was opened that needs finish()).
  int setup();
  // One frame: the clock, input, the script's tick, the audio, the draw and the present. Clears `running` when the
  // run is over; the caller then calls finish() once.
  void step();
  // After the last frame: the exit save and the end-of-run reports. Returns the exit code.
  int finish();

  Options &o;
  const script::GameModule *gameModule = nullptr;
  std::unique_ptr<assets::AssetFS> assetFs;
  std::optional<platform::Platform> sdl; // first created, last destroyed
  std::optional<platform::Window> window;
  int exitCode = 0;
  bool running = true;
  bool reboot = false; // the switcher chose another game: App::takeReboot
  std::optional<platform::SdlRenderer> renderer;
  std::unique_ptr<gfx::TextureCache>
      textures; // after the renderer so textures die first
  std::unique_ptr<gfx::SheetCache> sheets; // after the textures, so it dies
                                           // first and lets go of their images
  std::unique_ptr<gfx::text::TextSystem>
      textSystem; // after the renderer, like the textures
  TextScene textScene;
  SceneTextures sceneTex;
  long frame = 0;
  double lastNowMs =
      0; // the clock of the current frame, for the `diag` command
  std::unique_ptr<ScriptDriver> driver;
  // The script host needs the game data. Created in ownership order: Lua
  // state, window manager (holds LuaRefs), script context, then the host.
  script::Log scriptLog;
  script::Clock scriptClock;
  script::Config scriptConfig;
  script::StubLog stubLog;
  std::uint32_t seed = 0;
  std::optional<script::Rng> rng;
  // The audio service and its device come before everything that holds a
  // pointer to them (destroyed in reverse: the device stops its callback first,
  // then the service goes after the script context).
  std::unique_ptr<const audio::EventTable> audioTable; // outlives audioSvc
  std::unique_ptr<audio::Audio> audioSvc;
  std::unique_ptr<platform::AudioDevice> audioDevice;
  std::unique_ptr<script::LuaState> luaState;
  std::unique_ptr<script::ScriptContext> scriptCtx;
  std::unique_ptr<ui::WindowManager> windowManager;
  std::unique_ptr<script::ScriptHost> host;
  // Script time starts at 0 after boot, in both modes (boot ran at clock 0, so
  // the first tick does not jump).
  std::uint64_t startTicks = 0;
  bool quitAfterTravel = false; // a close request arrived mid-flight
  bool runDone = false;
  // The badge's text, drawn over everything the game drew. The ENGINE's built-in font (a FontCache with no
  // AssetFS never touches the game's fonts), so the strip is engine-only; windowed runs only - each ctest run is
  // headless, so no baseline screenshot can see it.
  gfx::text::FontCache badgeFonts; // no AssetFS: the built-in font only
  std::optional<gfx::text::TextLayout> badgeText;
  bool foreground = true;
  int backgroundDelayMs = 0; // the original's idle sleep: 10, 20, ... 1000 ms
                             // per loop while backgrounded

 private:
  void pumpWindows();
  void routeInput(const platform::InputEvent &e);
  void openSwitcher();
  void drawBadge(bool visible);
  bool viewOnTop();
  void runFile();
};

// The command-line modes that need no window (prefs import, --add-game, --pick-game) and the launcher before the
// mount. Returns -1 when a run follows (o.assetsDir is then the game, or empty for the test scene), otherwise the
// exit code.
int preflight(Options &o) {
  diag::setLogMotion(o.logInput);
  if (!o.logFile.empty()) {
    std::string logError;
    if (!diag::openLog(o.logFile, logError)) {
      // Keep running without the log: a game that quits silently looks like a
      // crash.
      std::fprintf(stderr, "%s (continuing without a log file)\n",
                   logError.c_str());
      if (!o.headless)
        platform::showWarning("OpenConche",
                              logError +
                                  "\n\nThe game will run without a log file.");
    }
  }
  if (!o.importPrefs.empty())
    return runImportPrefs(o); // imports and exits; no window, no assets
  if (!o.addGame.empty()) {  // registers a game in the launcher's list without the folder dialog; exits
    GameLibrary library;
    std::string error;
    loadGameLibrary(library, error);  // a missing or stale file is an empty library
    // Stored absolute: the list outlives this working directory.
    const std::string folder = platform::pathToUtf8(std::filesystem::absolute(platform::pathFromUtf8(o.addGame)).lexically_normal());
    const AddGameResult r = addGameFromFolder(library, folder);
    std::fflush(stdout);
    switch (r.status) {
      case AddGameStatus::Added:
        std::printf("added %s (%s)\n", findFinderVariant(r.id)->title, r.assets.c_str());
        return 0;
      case AddGameStatus::AlreadyListed:
        std::printf("%s is already in the list\n", findFinderVariant(r.id)->title);
        return 0;
      case AddGameStatus::SaveFailed:
        diag::err("the game was recognised but the list could not be saved: %s\n", r.error.c_str());
        return 1;
      case AddGameStatus::NotFound:
        diag::err("no supported game found in '%s' (pass the install folder, the one containing 'assets')\n",
                  o.addGame.c_str());
        return 3;
    }
  }
  if (!o.pickGame.empty()) {  // the finder's probe (design game-finder.md section 4), exits
    const FinderVariant* variant = findFinderVariant(o.pickGame);
    if (!variant) {
      diag::err("unknown --pick-game '%s'\n", o.pickGame.c_str());
      return 2;
    }
    const int rc = finderPick(o, *variant, o.pickDir);
    std::fflush(stdout);
    return rc;  // 0: playable (the probe is exit-only), 3: nothing playable
  }
  if (o.noAssets) {
    diag::err("--no-assets: showing the test scene\n");
  } else if (o.assetsDir.empty()) {
    // docs/design/game-finder.md section 1: --finder (the explicit re-pick), the saved choice, then the launcher
    // for interactive runs. Automated runs keep falling to the test scene (no dialog, no library read). The
    // launcher's Exit button and its window's close are a deliberate quit: the process ends there, not in the
    // test scene - closing the only window a person sees means quit.
    if (o.finder) {
      std::string why;
      const FinderOutcome oc = runGameFinder(o, why);
      if (oc == FinderOutcome::Picked) {
        // fall through to the mount below
      } else if (oc == FinderOutcome::Failed) {
        // Not a choice: the finder could not run (an automated run, no display, no window). Exit 1, so a
        // script can tell this from a deliberate Exit.
        diag::err("[finder] %s\n", why.c_str());
        return 1;
      } else {
        if (!why.empty()) diag::out("[finder] %s\n", why.c_str());
        return 0;
      }
    } else if (!isAutomatedRun(o)) {
      // No config file, or a stale one: the launcher. Automated runs skip it AND the library, so they never
      // depend on the machine's home directory.
      if (!savedFinderChoice(o)) {
        std::string why;
        const FinderOutcome oc = runGameFinder(o, why);
        if (oc == FinderOutcome::Failed) {
          diag::err("[finder] %s\n", why.c_str());
          return 1;
        }
        if (oc != FinderOutcome::Picked) {
          diag::out("[app] exit: the launcher was closed without a pick\n");
          return 0;
        }
      }
    }
    if (o.assetsDir.empty()) {
      // Automated only - an interactive run has the launcher above and returns there on any non-pick. The
      // message box is gone with it: this line is the one check_cli greps for.
      diag::err("no game assets found: showing the test scene (an automated "
                "run never opens the launcher); pass --assets <dir>\n");
    }
  }
  return -1;
}

int Session::setup() {
  // Mount the game data. Without any assets directory the app still runs (the
  // test scene needs none).
  gameModule =
      games::modules().front(); // the detected game (choc3 until detection)
  if (!o.assetsDir.empty()) {
    assetFs = std::make_unique<assets::AssetFS>(o.assetsDir, o.userDir);
    if (!assetFs->ok()) {
      diag::err("%s\n", assetFs->error().c_str());
      if (!o.headless)
        platform::showErrorIfNoConsole("OpenConche", assetFs->error());
      return 2;
    }
    // The game decides the default user folder, so it is detected from the
    // mounted assets first (nothing touches user: before the first write).
    std::string gameWhy;
    gameModule = &games::detectGame(*assetFs, o.game, gameWhy);
    diag::out("[game] %s (%s)\n", gameModule->profile().id, gameWhy.c_str());
    if (o.userDir.empty()) {
      o.userDir = platform::defaultUserDir(
          gameModule->defaultUserLeaf(*assetFs).c_str());
      assetFs->setUserRoot(o.userDir);
    }
    diag::out("[assets] %s (%zu files), user dir %s\n", o.assetsDir.c_str(),
              assetFs->fileCount(), o.userDir.c_str());
    std::fflush(stdout);
    // The folder is not a game: the registry's last-resort fallback claims it for a module whose mainloop is
    // absent. Automated runs and the explicit scene fixtures keep running (the headless test bed: check_scene,
    // check_text, check_cli); an interactive run must never end up in the no-game boxes loop - quit instead
    // (the user's verdict on that mode: it only ever meant "I picked an invalid directory").
    if (!isAutomatedRun(o) && !o.testScene && !o.testText &&
        !assetFs->exists(gameModule->profile().mainloop)) {
      diag::err("[assets] no game in '%s' (%s is missing): quitting\n",
                o.assetsDir.c_str(), gameModule->profile().mainloop);
      platform::showErrorIfNoConsole(
          "OpenConche",
          "The folder holds no playable game ('" + o.assetsDir +
              "' has no " + gameModule->profile().mainloop + ").\n\nPick the game's install folder in the "
              "launcher (run openconche again), or pass --assets <dir>.");
      return 3;
    }
  }

  // Parse the script first so a bad script fails before any window opens.
  std::optional<ParsedScript> script;
  if (!o.scriptFile.empty()) {
    std::string text;
    if (!readFile(o.scriptFile, text)) {
      diag::err("cannot read script '%s'\n", o.scriptFile.c_str());
      if (!o.headless)
        platform::showErrorIfNoConsole("OpenConche", "cannot read script '" +
                                                          o.scriptFile + "'");
      return 2;
    }
    script = parseScript(text);
    if (!script->errors.empty()) {
      for (const auto &e : script->errors)
        diag::err("%s: %s\n", o.scriptFile.c_str(), e.c_str());
      return 2;
    }
  }

  // --screenshot without --script or --frames captures the first frame.
  if (!o.screenshotPath.empty() && !script && o.frames == 0)
    o.frames = 1;

  sdl.emplace(o.headless);
  if (!sdl->ok()) {
    diag::err("%s\n", sdl->error().c_str());
    return 1;
  }
  platform::WindowConfig cfg;
  cfg.scale = o.scale;
  cfg.fullscreen = o.fullscreen;
  cfg.headless = o.headless;
  cfg.displayWidth = o.displayWidth;
  cfg.displayHeight = o.displayHeight;
  cfg.title = windowTitleFor(*gameModule, assetFs.get());
  window.emplace(*sdl, cfg);
  if (!window->ok()) {
    diag::err("%s\n", window->error().c_str());
    return 1;
  }
  diag::out("[display] %s, scale %d, logical %dx%d\n",
            o.headless ? "headless" : "windowed", window->scale(),
            platform::kLogicalWidth, platform::kLogicalHeight);
  std::fflush(stdout);

  renderer.emplace(*window);
  if (assetFs)
    textures = std::make_unique<gfx::TextureCache>(*assetFs, &*renderer);
  if (textures)
    sheets = std::make_unique<gfx::SheetCache>(*assetFs, *textures);
  if (assetFs) {
    textSystem =
        std::make_unique<gfx::text::TextSystem>(assetFs.get(), &*renderer);
    gfx::text::TextSystem::install(textSystem.get());
    if (o.testText)
      textScene.build(textSystem->fonts);
  } else if (o.testText) {
    diag::err("[scene] --test-text needs --assets\n");
  }
  if (o.testScene) {
    if (textures) {
      sceneTex.checker = textures->load("image/checker");
      sceneTex.quads = textures->load("image/quads");
      sceneTex.white = textures->load("image/white");
    }
    if (!sceneTex.ready())
      diag::err("[scene] --test-scene textures not available (need --assets "
                "with image/checker, quads, white)\n");
  }

  if (script)
    driver =
        std::make_unique<ScriptDriver>(std::move(script->commands), *window);

  seed = o.seed ? *o.seed : (o.headless ? 1u : std::random_device{}());
  rng.emplace(seed);
  // Asset folders without a game script (the gfx test assets) run with no
  // script host. The entry script is the game's profile (ui/mainloop.lua; Diner
  // Dash 2's SDK-3.4 wrapper runs scripts/mainloop.lua).
  if (assetFs && !assetFs->exists(gameModule->profile().mainloop)) {
    diag::out("[script] no %s in the assets, running without a script host\n",
              gameModule->profile().mainloop);
  } else if (assetFs) {
    diag::out("[script] seed %u\n", seed);
    luaState = std::make_unique<script::LuaState>();
    windowManager = std::make_unique<ui::WindowManager>();
    const script::WindowQuirks &quirks = gameModule->profile().quirks;
    windowManager->setLegacyFallThroughPoint(
        quirks.fallThroughPoint); // the C2-era window system quirk
    windowManager->setLegacyPopupFader(
        quirks.popupFader); // TPopupWindow::TFader for every popup
    windowManager->setLegacyCancelDefault(
        quirks.cancelDefault); // buysell.lua: Cancel is `default`
    windowManager->setSpriteHitAlpha(
        quirks.spriteHitAlpha); // TSprite::HitTest threshold (port hover)
    windowManager->setKeepLastOverflowChar(quirks.keepLastOverflowChar);
    windowManager->setLegacyRollover(
        quirks.rollover); // TRollover: xOffset 20, rolloverSound, area
    const script::TypingSound &ts = gameModule->profile().typingSound;
    windowManager->setTypingSound(
        ts.prefix, ts.count,
        ts.suffix); // the text box's click (none for Chocolatier 2)
    windowManager->setTextures(textures.get());
    windowManager->setSheets(sheets.get());
    windowManager->setTextSystem(textSystem.get());
    scriptCtx = std::make_unique<script::ScriptContext>(
        *luaState, *assetFs, *windowManager, scriptClock, *rng, scriptConfig,
        stubLog, scriptLog);
    // Audio (docs/design/audio.md): the audio RNG is seeded from seed + 3, so
    // it never shifts a game roll. Headless runs and runs without a device
    // render into a discard buffer from the simulated clock.
    audioTable = gameModule->makeAudioEvents(*assetFs);
    audioSvc = std::make_unique<audio::Audio>(
        [fs = assetFs.get()](const std::string &path)
            -> std::optional<std::vector<std::uint8_t>> {
          auto bytes = fs->readFile(path);
          if (!bytes)
            return std::nullopt;
          const auto *p = reinterpret_cast<const std::uint8_t *>(bytes->data());
          return std::vector<std::uint8_t>(p, p + bytes->size());
        },
        audioTable ? *audioTable : audio::EventTable::empty(),
        nullptr, seed + 3);
    scriptCtx->audio = audioSvc.get();
    audioSvc->setSquaredGain(gameModule->profile().squaredGain);
    // Headless output is discarded, so music streams only count their frames
    // (same timing, no Vorbis decoding).
    audioSvc->setSkipDecode(o.headless);
    // Windowed runs decode every sound the event table can reach on one
    // low-priority worker, so the first flight finds travel_plane ready.
    // Headless runs keep decoding on first use (no threads, same output), and
    // so does the web build (no threads at all: cmake/Web.cmake).
#ifndef __EMSCRIPTEN__
    if (!o.headless) {
      audioSvc->startPrefetch([] { platform::lowerCurrentThreadPriority(); },
                              gameModule->prefetch());
    }
#endif
    if (!o.headless) {
      audioDevice = std::make_unique<platform::AudioDevice>(audioSvc->mixer());
      if (audioDevice->ok()) {
        audioSvc->setPump(false);
      } else {
        scriptLog.write("[audio] no audio device (" + audioDevice->error() +
                        "): running silent");
        audioDevice.reset();
      }
    }
    {
      bool haveCadi = false; // any CADI event file present, under cadi/audio
                             // (Reforged or a CAD container) or its fallback
      for (const auto &def : audioSvc->table().events()) {
        for (const auto &r : def.fileRes)
          haveCadi = haveCadi || assetFs->exists(r.primary) ||
                     (!r.fallback.empty() && assetFs->exists(r.fallback));
        if (haveCadi)
          break;
      }
      if (!haveCadi)
        scriptLog.write("[audio] cadi/audio/ not found (vanilla data): CADI "
                        "events are silent");
    }
    // The machine library's original-bug switch (games.cfg `fixbugs`, docs/design/greatcc.md section 7) goes in below
    // the --config overrides; automated runs never read the library, so they keep the default (on).
    if (!isAutomatedRun(o)) {
      GameLibrary library;
      std::string libraryError;
      loadGameLibrary(library, libraryError);
      scriptConfig.set("fixbugs", library.fixBugs ? "1" : "0");
    }
    for (const auto &[k, v] : o.configOverrides)
      scriptConfig.override(k, v);
    scriptCtx->module = gameModule;
    gameModule->attach(*scriptCtx);
    host = std::make_unique<script::ScriptHost>(*scriptCtx);
    gameModule->registerAppWindows(*windowManager, *scriptCtx,
                                   {sheets.get(), &*rng, &scriptLog});
    if (driver) {
      driver->setLuaEvaluator([ctx = scriptCtx.get()](const std::string &expr) {
        std::string err;
        const bool ok = script::evalLuaCondition(*ctx, expr, err);
        if (!err.empty() && ctx->warned.insert("expect-lua:" + expr).second)
          ctx->log.write("[expect-lua] " + err);
        return ok;
      });
      driver->setWindowQuery(windowManager.get());
      driver->setExtraDump([a = audioSvc.get()] { return a->dump(); });
      driver->setLuaRunner(
          [this](const std::string &code) { host->runString(code); });
    }
    scriptCtx->isFullscreen = [this] { return window->isFullscreen(); };
    // The custom cursor service: the image loads through the texture cache's
    // name resolution ("cursor/gun_aimer?simple" and friends), the platform
    // makes the cursor. The factory windows call it; headless's dummy driver
    // refuses and they log the request once instead.
    scriptCtx->setCursor = [this](const std::string &path, int hx, int hy) {
      gfx::ImageData img;
      if (!textures || !textures->loadImage(path, img))
        return false;
      platform::Window::applyCursorColourKey(
          img); // the shipped cursor art's transparency (see Window.h)
      return window->setCursor(img, hx, hy);
    };
    // The game's own boot cursor (GameProfile::bootCursor): Chocolatier 1's
    // main loop sets cursor/cursor and 2's Main image/cursor right after the
    // window title, hotspot (1,1) - the whole game runs with the skinned arrow.
    if (const char *bootCursor = gameModule->profile().bootCursor; *bootCursor)
      (void)scriptCtx->setCursor(bootCursor, gameModule->profile().bootCursorHotX,
                                 gameModule->profile().bootCursorHotY);
    scriptCtx->toggleFullscreen = [this, ctx = scriptCtx.get()] {
      if (!window->toggleFullscreen())
        return false;
      ctx->saveFullscreenPref(window->fullscreenState());  // the OS transition may still be under way
      return true;
    };
    windowManager->setLogger(
        [this](const std::string &line) { scriptLog.write(line); });
    if (diag::logOpen())
      scriptLog.setTee([](const std::string &line) { diag::logLine(line); });
    if (driver)
      driver->setDiagnostics(
          [this] { diag::writeReport(scriptCtx.get(), lastNowMs, frame); });
    // kPressAnyKey is posted unless the `overlaymouse` config value is non-zero
    // (docs/re/design-questions.md 9).
    windowManager->setOverlayMouseQuery([this] {
      return std::atoi(scriptConfig.get("overlaymouse").c_str()) != 0;
    });
    // Button click and rollover sounds and the typing clicks are direct TSound
    // files.
    windowManager->setSoundHook(
        [a = audioSvc.get()](const std::string &file, bool) {
          a->playFile(file);
        });
    // Chocolatier 1's main loop pauses a running factory mini-game on Escape
    // (the key-27 check, choc1-natives.md 8); the games' other Escape handling
    // is unchanged under it.
    windowManager->setEscapeHook(
        [ctx = scriptCtx.get()] { ctx->module->onEscapeKey(*ctx); });
    // The Options sliders apply live; SaveVolumes (script/UserNatives.cpp)
    // stores them and re-applies from the prefs.
    windowManager->setVolumeHook([a = audioSvc.get()](const std::string &slider,
                                                      float v) {
      float sfx = a->sfxLevel(), music = a->musicLevel(), env = a->envLevel();
      if (const ui::VolumeSlider *row = ui::findVolumeSlider(slider)) {
        if (row->channel == 0)
          sfx = v;
        else if (row->channel == 1)
          music = v;
        else
          env = v;
      }
      a->setLevels(sfx, music, env, a->muted());
    });
    if (!host->boot()) {
      diag::err("script host failed to boot\n");
      exitCode = 1;
      running = false;
    }
    script::applyAudioPrefs(*scriptCtx);
  }

  // Script time starts at 0 here, after boot, in both modes (boot ran at clock
  // 0, so the first tick does not jump).
  startTicks = SDL_GetTicks();
  if (!o.headless && kSwitcherAvailable) {
    using namespace gfx::text;
    TextStyle ts;
    ts.font = badgeFonts.builtin();
    ts.fonts = &badgeFonts;
    ts.size = 14;
    ts.color = {1, 1, 1, 1};
    ts.flags = kHAlignCenter + kVAlignCenter;
    badgeText = TextLayout::build(
        "OpenConche: click to switch the game, or press Ctrl+Shift+G any time", ts, kBadgeRect.w, kBadgeRect.h);
  }
  return -1;
}

// Per event and per frame (window-system design section 6): delete what was
// destroyed, then dispatch the queue.
void Session::pumpWindows() {
  if (!windowManager)
    return;
  const bool anyDestroyed = windowManager->destroyedCount() > 0;
  windowManager->clearDestroyed();
  // Big images (a port's background and scenery) are freed with their window;
  // sheets go first so they release theirs.
  if (anyDestroyed) {
    if (sheets)
      sheets->trim();
    if (textures)
      textures->trim();
  }
  windowManager->dispatchMessages();
  if (windowManager->takeDispatchOverflow())
    diag::out("[ui] message dispatch stopped after %d messages in one pass\n",
              ui::WindowManager::kMaxDispatch);
  if (windowManager->quitRequested() && running) {
    diag::out("[app] exit: kQuitNow\n");
    running = false;
  }
}

// One input event into the window tree: delete what was destroyed and
// dispatch the queue first (design section 6).
void Session::routeInput(const platform::InputEvent &e) {
  if (!windowManager)
    return;
  using T = platform::InputEvent::Type;
  ui::InputEvent in;
  in.x = e.x;
  in.y = e.y;
  in.inArea = e.inArea;
  in.button = e.button;
  in.key = {e.key, e.shift, e.ctrl, e.alt};
  switch (e.type) {
  case T::MouseMove:
    in.type = ui::InputEvent::Type::MouseMove;
    break;
  case T::MouseDown:
    in.type = ui::InputEvent::Type::MouseDown;
    break;
  case T::MouseUp:
    in.type = ui::InputEvent::Type::MouseUp;
    break;
  case T::Key:
    in.type = ui::InputEvent::Type::Key;
    break;
  case T::Text:
    in.type = ui::InputEvent::Type::Text;
    in.text = e.key;
    break;
  default:
    return;
  }
  pumpWindows();
  std::fflush(stdout);
  windowManager->handleInput(in);
  pumpWindows();
}

// The switcher (design game-finder.md section 2a): the badge's click path and the Ctrl/Cmd+Shift+G chord are one
// action. A successful pick sets the Options (so takeReboot hands the right ones to main's loop) and ends this
// run through the normal exit path - the game's exit save included - before the process reboots into the chosen
// game; Exit and window-close just close the switcher and keep playing.
void Session::openSwitcher() {
  std::string why;
  if (runGameFinder(o, why, FinderHost{&*window, &*renderer, &*renderer}) == FinderOutcome::Picked) {
    // The next boot re-resolves the chosen game's default user folder - unless --userdir was given, which
    // is a deliberate choice that must survive the switch (the boot filled a detected default in place).
    if (!o.userDirExplicit) o.userDir.clear();
    reboot = true;
    running = false;
  } else if (!why.empty()) {
    diag::out("[finder] %s%s\n", why.c_str(),
              why == "exit" ? " (the switcher closed, the game continues)" : "");
  }
}

// The OpenConche welcome strip above whatever the game drew (windowed only).
void Session::drawBadge(bool visible) {
  if (!badgeText || !visible)
    return;
  renderer->fillRect({float(kBadgeRect.x), float(kBadgeRect.y), float(kBadgeRect.w), float(kBadgeRect.h)},
                    {0, 0, 0, 0.72f}, gfx::Blend::Normal);
  badgeText->draw(*renderer, gfx::PointF{float(kBadgeRect.x), float(kBadgeRect.y)});
}

bool Session::viewOnTop() {
  if (!windowManager)
    return false;
  const std::string &path = windowManager->topModal()->scriptPath();
  for (const char *view : gameModule->profile().runViews)
    if (path == view)
      return true;
  return false;
}

// Reads the --run file afresh each time (so it can be edited while the game
// runs) and runs it in the game script. runString logs a Lua error without
// ending the game.
void Session::runFile() {
  std::string code;
  if (!readFile(o.runFile, code)) {
    diag::err("[run] cannot read %s\n", o.runFile.c_str());
    return;
  }
  diag::out("[run] %s\n", o.runFile.c_str());
  std::fflush(stdout);
  host->runString(code);
  pumpWindows();
}

void Session::step() {
  double nowMs = o.headless
                     ? static_cast<double>(frame) * kHeadlessFrameMs
                     : static_cast<double>(SDL_GetTicks() - startTicks);
  lastNowMs = nowMs;
  // The badge is a welcome strip: gone (and inert) in any frame after kBadgeVisibleMs of the run. Headless
  // never draws it; its hit test stays active regardless of script time on purpose (check_switcher drives the
  // switcher chain through it), while windowed the badge is only clickable while it is on screen.
  const bool badgeLive = kSwitcherAvailable && (o.headless || nowMs < kBadgeVisibleMs);
  if (windowManager)
    windowManager->setNowMs(
        nowMs); // modal clocks pause and start against this

  if (driver)
    driver->update(nowMs);
  pumpWindows();
  if (quitAfterTravel && host && host->mainAlive() &&
      !host->globalIsTrue(gameModule->profile().travelGlobal)) {
    quitAfterTravel = false;
    diag::out("[app] quit dialog delivered after landing\n");
    host->runString(gameModule->profile().closeRequest);
  }
  // --run: the first time the port or map view is the top modal (with a
  // dialog above it, the snippets' SwapToModal would replace the dialog, so
  // wait until it closes).
  if (!o.runFile.empty() && !runDone && host && viewOnTop()) {
    runDone = true;
    runFile();
  }

  for (const auto &e : window->pollEvents()) {
    using T = platform::InputEvent::Type;
    switch (e.type) {
    case T::Quit: {
      // A close request opens the quit dialog while Main is alive, else quits
      // directly. The user's rule, a deliberate deviation from
      // the original (which ignored repeats): ONE request asks - a SECOND
      // close request, while the quit dialog is up or a landing is still
      // pending, quits through the normal exit path, and the exit save below
      // runs ("we save anyway"). ASK ONCE: Ctrl+Cmd+Q, Ctrl+C, the close
      // box - one of them is asking, two of them is telling. AskQuit refuses
      // during a flight (the original too), but the request is remembered and
      // AskQuit is delivered once the flight has landed, so a Cmd-Q
      // mid-flight is not lost. The close global (AskQuit, QuitGame) may not
      // exist yet (ui/mainmenu.lua defines QuitGame): then quit directly.
      const script::GameProfile &gp = gameModule->profile();
      if (!(host && host->mainAlive()) || !*gp.closeRequest ||
          !host->globalIsFunction(gp.closeGlobal)) {
        running = false;
      } else if (host->globalIsTrue(gp.quitGlobal) || quitAfterTravel ||
                 (*gp.quitDialog &&
                  windowManager->topModal()->scriptPath() == gp.quitDialog)) {
        diag::out(
            "[app] close request again: quitting now (the exit save runs)\n");
        running = false;
      } else if (host->globalIsTrue(gameModule->profile().travelGlobal)) {
        diag::out("[app] close request ignored: travel active (the quit "
                  "dialog follows the landing)\n");
        quitAfterTravel = true;
      } else {
        host->runString(gameModule->profile().closeRequest);
      }
      break;
    }
    case T::MouseMove:
      diag::logInput(true, "[input] mouse-move %g %g%s\n", e.x, e.y,
                     e.inArea ? "" : " (outside)");
      routeInput(e);
      break;
    case T::MouseDown:
      diag::logInput(false, "[input] mouse-down %g %g button %d%s\n", e.x,
                     e.y, e.button, e.inArea ? "" : " (outside)");
      // The badge (design section 2a): the switcher's zero-CLI entry, on top of whatever the game drew. The
      // hit test is active even headless (the badge itself is not drawn there), so the chain is testable;
      // windowed, it lives exactly as long as the strip is on screen (the first kBadgeVisibleMs). Headless the
      // switcher can never open - it refuses, the guard goes to the log - and the click then falls through to
      // the game: scripted playtests (kitchen, devmenu) must be able to click the real UI under the badge rect.
      if (badgeLive && e.inArea && e.x >= kBadgeRect.x && e.x < kBadgeRect.x + kBadgeRect.w && e.y >= kBadgeRect.y &&
          e.y < kBadgeRect.y + kBadgeRect.h) {
        openSwitcher();
        if (!o.headless)
          break;
      }
      routeInput(e);
      break;
    case T::MouseUp:
      diag::logInput(false, "[input] mouse-up %g %g button %d%s\n", e.x, e.y,
                     e.button, e.inArea ? "" : " (outside)");
      routeInput(e);
      break;
    case T::Key:
      diag::logInput(false, "[input] key %s\n", e.key.c_str());
      if (e.key == kSwitchKey && (e.ctrl || e.gui) && e.shift) {
        // Never reaches the game (the shifted games read no such chord).
        if (!e.repeat && kSwitcherAvailable) openSwitcher();
        break;
      }
      if (e.key == kDiagnosticsKey) { // never reaches the game: F10 is unused
                                      // by the Reforged UI
        if (!e.repeat)
          diag::writeReport(scriptCtx.get(), nowMs,
                            frame); // a held key writes one report
        break;
      }
      if (e.key == kRunKey) {
        if (!e.repeat && !o.runFile.empty() && host && runDone) {
          if (viewOnTop())
            runFile();
          else
            diag::out("[run] skipped: a dialog is open\n");
        }
        break;
      }
      routeInput(e);
      break;
    case T::Text:
      diag::logInput(false, "[input] text \"%s\"\n", e.key.c_str());
      routeInput(e);
      break;
    case T::FullscreenChanged:
      if (scriptCtx)
        scriptCtx->saveFullscreenPref(e.fullscreen);
      if (e.x > 0)
        diag::out("[display] fullscreen %s, output %gx%g\n",
                  e.fullscreen ? "on" : "off", e.x, e.y);
      else
        diag::out("[display] fullscreen %s\n", e.fullscreen ? "on" : "off");
      break;
    case T::Resized:
      // The scale is SDL's own letterbox rectangle over 800, the same number the text is rasterised at.
      diag::out("[display] resized, output %gx%g, scale %.3f\n", e.x, e.y,
                renderer->outputScale());
      break;
    case T::Foreground:
      // SDL sends several events for one change (focus lost and minimised);
      // only a change is dispatched.
      if (e.foreground == foreground)
        break;
      foreground = e.foreground;
      diag::setAppState(foreground, backgroundDelayMs);
      diag::out("[app] %s\n", foreground ? "foreground" : "background");
      if (scriptCtx && scriptCtx->gameState)
        scriptCtx->gameState->setForeground(foreground, nowMs);
      pumpWindows();
      break;
    case T::RenderReset:
      diag::out("[render] %s reset\n", e.key.c_str());
      if (e.key == "device") {
        renderer->deviceReset();
        if (textures)
          textures->reuploadAll();
        if (textSystem)
          textSystem->atlas.reuploadAll();
      }
      break;
    }
  }
  std::fflush(stdout);

  if (windowManager) {
    windowManager->tickTransitions(
        nowMs); // before Main runs, so a transition started this frame first
                // steps next frame
    windowManager->tickWindowAnimations(nowMs);
    pumpWindows();
  }
  if (host) {
    host->tick(nowMs);
    if (host->mainReturned() &&
        running) { // the end of the script run ends the app
      diag::out("[app] exit: Main returned\n");
      running = false;
    }
    pumpWindows();
  }
  if (audioSvc) {
    audioSvc->update(nowMs);
    if (scriptCtx && scriptCtx->gameState)
      scriptCtx->gameState->afterAudio(*audioSvc);
  }
  if (driver && driver->quitRequested())
    running = false;

  if (windowManager) {
    gfx::RectI area;
    const bool want = windowManager->textInputWanted(area);
    if (want)
      window->setTextInput(
          true, static_cast<float>(area.x), static_cast<float>(area.y),
          static_cast<float>(area.w), static_cast<float>(area.h));
    else if (window->textInputActive())
      window->setTextInput(false);
  }

  window->beginFrame();
  // A new output scale (the window was resized) makes every cached glyph the wrong size; nothing is batched yet.
  if (textSystem)
    textSystem->atlas.setOutputScale(renderer->outputScale());
  if (host) {
    windowManager->draw(*renderer); // the window tree replaces the test scene
                                   // when a script is running
    drawBadge(badgeLive);  // the OpenConche welcome strip above whatever the game drew (windowed only)
  } else {
    drawTestScene(*renderer, o.testScene ? &sceneTex : nullptr);
    if (!textScene.layouts.empty()) {
      renderer->fillRect(
          {float(TextScene::kLabelBox.x), float(TextScene::kLabelBox.y),
           float(TextScene::kLabelBox.w), float(TextScene::kLabelBox.h)},
          {0.65f, 0.02f, 0.02f, 1.0f}, gfx::Blend::Opaque);
    }
    for (auto &[layout, at] : textScene.layouts)
      layout.draw(*renderer, at);
  }
  window->endFrame();

  if (driver && driver->pendingScreenshot()) {
    if (!takeScreenshot(*window, *driver->pendingScreenshot(),
                        driver->pendingScreenshotIsLogical())) {
      exitCode = 1;
      running = false;
    }
    driver->screenshotDone();
  }
  ++frame;
  if (o.frames > 0 && frame >= o.frames)
    running = false;
  if (driver && driver->finished())
    running = false;
  if (driver && driver->failed()) {
    exitCode = 1;
    running = false;
  }
  // The --screenshot capture is of the final frame, after any script has run.
  if (!running && exitCode == 0 && !o.screenshotPath.empty() &&
      !takeScreenshot(*window, o.screenshotPath, false)) {
    exitCode = 1;
  }
  window->present();
  // A backgrounded window keeps ticking but sleeps longer each loop; only
  // --headless runs (including scripted ones) are exempt from the throttle.
  if (!o.headless && !foreground) {
    backgroundDelayMs = std::min(backgroundDelayMs + 10, 1000);
    diag::setAppState(foreground, backgroundDelayMs);
#ifndef __EMSCRIPTEN__
    // On the web SDL_Delay is a busy-wait (no Asyncify), which pins the tab at 100%; the browser already
    // throttles requestAnimationFrame for a tab that is not visible.
    SDL_Delay(static_cast<std::uint32_t>(backgroundDelayMs));
#endif
  } else {
    backgroundDelayMs = 0;
    diag::setAppState(foreground, 0);
  }
}

int Session::finish() {
  if (driver && driver->expectPending()) {
    driver->failIfPending("the app stopped first");
    exitCode = 1;
  }
  if (windowManager && o.dumpWindows) {
    diag::outText(windowManager->dump());
    std::fflush(stdout);
  }
  // The original saves unconditionally here (docs/re/focus-and-display.md
  // section 4). We save iff a save was loaded or stored in this run, whatever
  // Main's state: a game that was played and then lost Main still saves its
  // state. The never-initialised Player (nothing loaded or stored) is not
  // saved, and SaveGameString also refuses its serialised fingerprint.
  if (host && *gameModule->profile().exitSave) {
    if (!host->gameStateTouched())
      diag::out("[save] exit save skipped: no game was loaded or saved in this "
                "run\n");
    else
      host->runString(gameModule->profile().exitSave);
  }
  if (!o.logUnimplementedFile.empty() &&
      !stubLog.writeFile(o.logUnimplementedFile)) {
    diag::err("cannot write '%s'\n", o.logUnimplementedFile.c_str());
  }
  if (textSystem) {
    const auto &a = textSystem->atlas;
    diag::out("[text] glyph atlas: %zu rasterisations, %zu uploads (%zu "
              "partial), %zu pages\n",
              a.rasterisations(), a.uploads(), a.partialUploads(),
              a.pageCount());
  }
  diag::out("[app] exit after %ld frames\n", frame);
  std::fflush(stdout);
  return exitCode;
}

} // namespace

#ifdef __EMSCRIPTEN__
// The browser owns the frame loop: emscripten_set_main_loop never returns, so the session lives on the heap and
// deletes itself after its last frame. The switcher's reboot is a desktop feature (the web page is one game).
namespace {
void webFrame(void *arg) {
  auto *s = static_cast<Session *>(arg);
  s->step();
  if (s->running)
    return;
  emscripten_cancel_main_loop();
  const int rc = s->finish();
  delete s;
  diag::out("[app] web run ended with exit code %d: back to the chooser\n", rc);
  std::fflush(stdout);
  platform::syncUserDir(true); // the exit save reaches IndexedDB, then the page goes to the chooser
}
} // namespace
#endif

int App::run() {
  Options &o = options_;
  if (const int rc = preflight(o); rc >= 0)
    return rc;
#ifdef __EMSCRIPTEN__
  auto *s = new Session(o);
  if (const int rc = s->setup(); rc >= 0) {
    delete s;
    return rc;
  }
  emscripten_set_main_loop_arg(webFrame, s, 0, 1); // 0: requestAnimationFrame; 1: unwinds, does not return
  return 0;
#else
  Session s(o);
  if (const int rc = s.setup(); rc >= 0)
    return rc;
  while (s.running)
    s.step();
  const int rc = s.finish();
  reboot_ = s.reboot;
  return rc;
#endif
}

bool App::takeReboot(Options& next) {
  if (!reboot_) return false;
  next = options_;
  return true;
}

} // namespace choco::app
