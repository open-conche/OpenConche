#pragma once

// The engine's view of a game (docs/design/engine-and-games.md section 3). A
// module is a stateless static that says which natives, window types and
// globals its game adds to the SDK ones, and a few flags the engine reads. This
// header includes no game header. Later slices add more profile fields and
// hooks.

#include <string>
#include <memory>
#include <string_view>
#include <vector>

#include "audio/EventTable.h"
#include "audio/SoundCache.h"

struct lua_State;

namespace choco::gfx {
class SheetCache;
}
namespace choco::ui {
class WindowManager;
}

namespace choco::assets {
class AssetFS;
}

namespace choco::audio {
class Audio;
} // namespace choco::audio

namespace choco::script {

struct ScriptContext;
class Log;
class Rng;

// App-owned services a game's builtin window types may need
// (GameModule::registerAppWindows).
struct AppWindowEnv {
  gfx::SheetCache *sheets = nullptr;
  Rng *rng = nullptr;
  Log *log = nullptr;
};

// WindowManager generation quirks: the Chocolatier 2-era window system differs
// from Chocolatier 3's in these ways.
struct WindowQuirks {
  bool fallThroughPoint = false; // setLegacyFallThroughPoint
  bool popupFader =
      false; // setLegacyPopupFader: TPopupWindow::TFader for every popup
  bool cancelDefault =
      false; // setLegacyCancelDefault: buysell.lua marks Cancel as `default`
  int spriteHitAlpha =
      0; // setSpriteHitAlpha: TSprite::HitTest threshold (port hover)
  bool rollover =
      false; // setLegacyRollover: TRollover xOffset 20, rolloverSound, area
  bool keepLastOverflowChar =
      false; // setKeepLastOverflowChar: the TText wrap keeps an overflowing
             // last character on its line
};

// The default sound levels, per mille, of a user with no stored preference
// (what the Options sliders open on).
struct VolumeDefaults {
  int sfx = 750, music = 650, ambient = 500;
};

// The click a text box plays per accepted key: prefix + two-digit number
// (1..count) + suffix. count 0 = silent.
struct TypingSound {
  const char *prefix = "";
  unsigned count = 0;
  const char *suffix = "";
};

// Plain per-game data the engine and the app read.
struct GameProfile {
  const char *id;           // the --game value: "choc3", "choc2", "choc1"
  const char *userDirName;  // leaf of the default --userdir ("Chocolatier3";
                            // lower-cased on Linux)
  const char *closeGlobal;  // the Lua global the close request calls; the app
                            // quits directly when it is not a function
  const char *closeRequest; // Lua run on a close request while Main is alive;
                            // empty = quit directly
  const char *quitDialog; // script path of the quit dialog, so a repeated close
                          // request is ignored while it is up (or "")
  const char *
      exitSave; // Lua run at exit when the game state was touched; empty = none
  const char *windowTitle;
  bool versionFromTitle = false; // gVersionString falls back to strings
                                 // "gametitle" when settings has no version
  bool pauseDialogOnFocusLoss =
      false; // World::setForeground opens ui/ui_pause.lua
  WindowQuirks quirks;
  const char *travelGlobal; // Lua global true while a trip is in flight (a
                            // close request waits for the landing)
  const char *quitGlobal;   // Lua global true while the quit dialog is up
  std::vector<const char *>
      runViews; // script paths of the views --run waits for as the top modal
  VolumeDefaults
      volumeDefaults; // Chocolatier 3 and 1: 750/650/500; Chocolatier 2: 750
                      // each (docs/re/choc2-map-binary.md 3.4)
  TypingSound
      typingSound; // last, so a profile that lists nothing for it is silent
  // The cursor image set once at boot through ScriptContext::setCursor, hotspot
  // (1,1): all three games load their arrow picture at boot - Chocolatier 1's
  // main loop right after the window title (004036d0), Chocolatier 2's Main
  // (image/cursor) and Chocolatier 3's ChocoThreeMain (cursor/cursor through
  // Content::LoadCursor) just before the sounds start. So the whole game runs
  // with the skinned arrow; the factories swap gun vs arrow through the same
  // setter.
  const char *bootCursor = "";
  // The entry script the host runs in Main's coroutine (last, so the
  // Chocolatier profiles can stay as they are). Chocolatier's SDK opens with
  // ui/mainloop.lua (ChocoThreeMain, 000101a0); Diner Dash 2's SDK-3.4 wrapper
  // (Main, 0x35648) runs scripts/mainloop.lua - so every other game is the
  // field's default.
  const char *mainloop = "ui/mainloop.lua";
  // The SDK string table the host loads at boot (every game before Great Chocolate Chase has it at the root).
  const char *stringsPath = "strings.xml";
  // The string-table key of the window title; empty = `windowTitle` as is (`windowTitle` stays the fallback when the table
  // or the key is missing).
  const char *titleString = "";
  // The hotspot of `bootCursor`.
  int bootCursorHotX = 1, bootCursorHotY = 1;
  // The gain a sound plays at is (master x level)^2 instead of the level itself (Great Chocolate Chase,
  // docs/re/greatcc-audio.md section 4).
  bool squaredGain = false;
};

// The per-context game state (design section 4): made by GameModule::attach,
// owned by ScriptContext::gameState, and the only part of it the engine itself
// calls. Games reach their derived type through worldOf / sessionOf.
class GameState {
public:
  virtual ~GameState() = default;
  virtual void tick(double nowMs) = 0; // once per frame with the script clock,
                                       // from ScriptHost::tick
  virtual void
  setForeground(bool fg, double nowMs) = 0; // the app lost (false) or regained
                                            // (true) the foreground
  // Once per frame, from the App right after Audio::update (only with an audio
  // service). Default: nothing.
  virtual void afterAudio(audio::Audio &) {}
};

class GameModule {
public:
  virtual ~GameModule() = default;
  virtual const GameProfile &profile() const = 0;
  // Whether the mounted assets are this game's. `settingsGame` is
  // settings.xml's <game> value (the registry only asks when settings.xml
  // exists); on a claim, `why` receives the one-line reason for the [game] log
  // line.
  virtual bool claims(const assets::AssetFS &, const std::string &settingsGame,
                      std::string &why) const = 0;
  virtual void registerStubs(lua_State *,
                             ScriptContext &) const = 0; // boot step 1
  virtual void registerNatives(lua_State *,
                               ScriptContext &) const = 0; // boot step 4
  // Called in ScriptHost::boot right after registerCoreNatives, so natives a
  // game adds to that group keep today's registration order (boot trace).
  // choc3: Date, DayOfYear.
  virtual void registerCoreExtras(lua_State *, ScriptContext &) const {}
  // Called in ScriptHost::boot right after registerContentNatives, for the same
  // reason. choc3: LoadIngredients, LoadProducts, LoadPortFileList,
  // LoadQuestFileList.
  virtual void registerContentExtras(lua_State *, ScriptContext &) const {}
  // SaveGameString drops `data` when this returns true and logs "[save]
  // refused: " + why. choc3: Reforged's bare Player.
  virtual bool refuseSave(std::string_view /*data*/,
                          std::string & /*why*/) const {
    return false;
  }
  // The SoundEvent table, built once at startup from the game's assets; null =
  // none (the app passes audio::EventTable::empty()). The app owns the result.
  virtual std::unique_ptr<const audio::EventTable>
  makeAudioEvents(const assets::AssetFS &) const {
    return nullptr;
  }
  virtual void registerWindowTypes(lua_State *,
                                   ScriptContext &) const = 0; // boot step 6
  // The Escape key, before the windows see it. Chocolatier 1's main loop checks
  // the key at the loop level (choc1-natives.md section 8) and pauses a running
  // factory mini-game whatever has the focus; the key then continues into the
  // normal processing. Other games return quietly.
  virtual void onEscapeKey(ScriptContext &) const {}
  virtual void registerGlobals(lua_State *, ScriptContext &) const {
  } // boot step 9
  // The default --userdir leaf for the mounted assets: the profile's
  // userDirName. A game whose content sets are incompatible overrides it
  // (Chocolatier 3: the community tree's players cannot safely load under the
  // shipped content's tables and vice versa).
  virtual std::string defaultUserLeaf(const assets::AssetFS &) const {
    return profile().userDirName;
  }
  // Sounds the app decodes ahead on a worker in windowed runs, beyond the event
  // table's own.
  virtual std::vector<audio::SoundCache::Request> prefetch() const {
    return {};
  }
  // Builtin window types that need app services (boot step 6 of the app, after
  // the ScriptHost exists).
  virtual void registerAppWindows(ui::WindowManager &, ScriptContext &,
                                  const AppWindowEnv &) const {}
  // Appends the game's lines to the F10 / `diag` report, between the window
  // manager section and the window tree.
  virtual void appendDiagnostics(ScriptContext &, std::string &) const {}
  // Makes ctx.gameState, right after the context is built and before the
  // ScriptHost.
  virtual void attach(ScriptContext &) const = 0;
};

} // namespace choco::script
