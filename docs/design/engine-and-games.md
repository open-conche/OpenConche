# Design: a generic engine with separate game modules

OpenConche is a Playground-SDK-style engine plus one module per game. The engine core knows nothing about
Chocolatier content; each game module says what that game adds. This document describes the layering, what a module
provides, how the layering is enforced, and what stays in the engine core on purpose.

## 1. Layers

| Layer | Directories | Holds |
|---|---|---|
| Engine core | `src/{platform,gfx,audio,assets,ui,sdk,script,app}` | the window system, graphics, audio, asset mounting, the Lua host and the SDK natives |
| SDK classes | `src/sdk/` | the Playground SDK classes the games build on: the sprite tree (`TSprite`, `TTextSprite`, the animation scripts), `Ticker`, users and hiscores (`TPfUsers`, `TPfHiscores`). Namespace `choco::sdk` |
| Shared gameplay | `src/games/common/` | code two or more games use directly: `World`, `Ledger`, the port, map and travel windows, crates, the shared factory parts, `DynamicWindow`, the calendar. Namespace `choco::game` |
| Modules | `src/games/choc3/`, `src/games/choc2/`, `src/games/choc1/` | one `GameModule` per game, with its natives, windows, generated stub tables and tests |
| Registry | `src/games/Registry.{h,cpp}` | the ordered module list and game detection |

`app/` is the composition root: it is the only engine directory that may name games. The SDK natives (`Core`, `Window`,
`Content`, `Text`, `User`, `Audio`) stay in `script/` and the engine registers them, because the prelude calls them and
every game needs them.

**Rule for gameplay code:** code used directly by two games goes to `common/`. Code one module registers goes to that
module, even if a derived module inherits its natives. Chocolatier 2 and 1 inherit all of Chocolatier 3's natives and
window classes on purpose (`docs/re/choc1-fit.md`), so they depend on `games/choc3/`, and only choc2 and choc1 can be
left out of a build.

## 2. Include rules

Checked on `#include "..."` lines under `src/` and `tests/`.

| Includer | May include from `games/` |
|---|---|
| `platform gfx audio assets ui sdk script` | nothing |
| `app/` | `games/Registry.h` and `games/choc3/ImportCommand.h` only |
| `games/common/` | no `games/choc*` module |
| `games/choc3/` | no choc2 or choc1 |
| `games/choc2/`, `games/choc1/` | choc3 (and common); not each other |
| `tests/` | anything: choc3 and common are always built |

The old paths `game/` and `script/games/` are gone and may not be included. The engine core also never names
`games::` (a forward declaration could otherwise reach a module without an include).

## 3. What a `GameModule` provides

`src/script/GameModule.h` includes no game header. A module is a stateless function-local static returned by
`games::<id>::module()`; it holds no pointers to runtime objects, so nothing can outlive it. Its parts:

- **`profile()`**: plain data. `id` (the `--game` value), `userDirName`, `windowTitle`, `closeGlobal`, `closeRequest`,
  `quitDialog`, `exitSave`, `travelGlobal`, `quitGlobal`, `runViews`, `versionFromTitle` (`gVersionString` falls back to
  the `gametitle` string), `pauseDialogOnFocusLoss`, `bootCursor`, `mainloop`, `volumeDefaults`, `typingSound` and the
  window `quirks` (`WindowQuirks`: fall-through point, popup fader, cancel-default, sprite hit alpha, rollover, last
  overflow character). The window quirks are set on the `WindowManager`; they describe the SDK generation the game
  was built on.
- **`claims(fs, settingsGame, why)`**: whether the mounted assets are this game's, with the reason for the `[game]` log
  line.
- **Registration hooks**, called by `ScriptHost::boot` (section 6): `registerStubs`, `registerNatives`,
  `registerCoreExtras`, `registerContentExtras`, `registerWindowTypes`, `registerGlobals`.
- **`attach(ctx)`**: makes the per-context `GameState` (section 5).
- **`registerAppWindows(wm, ctx, env)`** (builtin window types that need app services such as the sheet cache) and
  **`appendDiagnostics`** (the game's lines in the F10 / `diag` report).
- **`defaultUserLeaf(fs)`**: the per-game default `--userdir` leaf, `profile().userDirName` unless overridden. choc3 returns
  `Chocolatier3-Vanilla` unless the tree has `community/account.lua`, because the community and shipped content sets
  have different ingredient code tables and a player made with one dies in the other's Lua.
- **`closeRequest` / `closeGlobal` / `quitDialog`**: the script the App runs on a window close while Main is alive; it
  quits directly when `closeGlobal` is not a function. `exitSave` is the script run at exit when the game state was touched.
- **`onEscapeKey(ctx)`**: the Escape key before the windows see it (choc1 pauses a running factory game).
- **`prefetch()`**: sounds decoded ahead on a worker in windowed runs (choc2's ambient beds, choc1's aliases).
- **`audioEvents()`**: the `SoundEvent` table; null means `audio::EventTable::empty()`. Only choc3 has one, because no
  Chocolatier 2 or 1 Lua calls `SoundEvent` (`docs/re/audio.md`, cleanup questions).
- **`refuseSave(data, why)`**: `SaveGameString` drops the data and logs `[save] refused: <why>` when it returns true.
  Only choc3 implements it (Reforged's never-initialised `Player`); Chocolatier 2 saves through its own
  `bsgSaveGameString`, which has no guard.

How the modules fill these in:

- **choc3** implements everything. Natives: `registerPortNatives`, `Game`, `Map`, `Crates`, `Factory`, in that order; the
  core extras bind `Date` and `DayOfYear`, the content extras the four content loaders.
- **choc2** calls choc3's stubs, core extras, content extras, natives, window types, `registerAppWindows` and
  `appendDiagnostics`, then adds `registerChoc2Natives`, `registerChoc2WindowTypes` and `registerChoc2Globals`. It attaches
  a `Choc2World`.
- **choc1** forwards the same choc3 hooks and adds `registerChoc1Natives` (a Chocolatier 1 name overrides a choc3 one)
  and `registerChoc1WindowTypes` (placeholders only for names still free). It sets no globals and attaches a `Choc1World`.

The money formatting is shared: `script::moneyFormat` and `script::dollars` in `script/Formats.{h,cpp}` read
`_MONEY_SIGN` and `_NUMERIC_SEPARATOR`. choc2's `bsgDollars` stays a separate native because it truncates.

## 4. Registry and detection

`games::modules()` is a static ordered list: choc3, choc2, choc1, each behind `#if CHOCO_GAME_<ID>` (choc3 is always
present). `findModule(id)` looks one up. `detectGame(fs, forcedId, why)`:

1. A forced valid id wins (`--game <id>`).
2. With no `settings.xml`, the first module that claims the tree by files alone wins (none of the shipped modules does),
   else choc3 with `no settings.xml`.
3. Otherwise the first module, in list order, whose `claims()` is true wins: choc3 on `<game>` = `chocolatier3`; choc2 on
   `chocolatier` and `ui/uihelp.lua`; choc1 on `chocolatier` and `data/ports.lua`.
4. If none claims, choc3 with a `WARNING:` text.

There is no self-registration: a static library drops objects nothing references, on MSVC as well.

## 5. Ownership and lifetimes

- `ScriptContext::module` (`const GameModule*`) is set by the App (and by the test harness `ScriptEnv`, default choc3) and
  read-only after that. `ScriptHost::boot` with a null module logs `[engine-error] no game module` and fails.
- `ScriptContext::gameState` (`std::unique_ptr<GameState>`) is made by `module->attach(ctx)` right after the context is
  built and before the `ScriptHost`. The `WindowManager` outlives it, which matters for a detached ledger.
- The engine calls only the `GameState` base: `tick(nowMs)` from `ScriptHost::tick`, `setForeground` from the App's
  focus handler, and `afterAudio(Audio&)` from the App right after `Audio::update` each frame (with an audio service).
- `game::World` derives from `GameState`; `Choc2World` adds a `Session` and a `Choc2Music`. Callers reach the derived
  object through `game::worldOf(ctx)` and `game::choc2::sessionOf(ctx)`: a per-call `dynamic_cast` that returns null
  when absent. Nothing caches it beyond what windows do (`PortWindow` and `MapEnv` hold a `World*` from construction).
- `Choc2Music` (the `SetMusicState` state machine) stores no pointer to `Audio`; `Audio` is passed on every call, so no
  callback can dangle. The music tick runs through `afterAudio` because it must run in the same place in the frame
  as `Audio::update`'s reap: polling it from `GameState::tick` would run in the next frame, after the driver and input Lua.

## 6. Boot and App order

`ScriptHost::boot`:

1. `registerStubs` (module).
2. Loaders, `coroutine.yield`, logging, message type.
3. SDK natives: Core, then `registerCoreExtras`; Window; Content, then `registerContentExtras`; Text; User; Audio. The
   extras hooks sit at those points so a game's natives keep a fixed place in the registration order (the boot trace
   goldens pin it).
4. `registerNatives` (module), then `HandleGetMessage`. A game's natives bind over the SDK ones; `HandleGetMessage` is
   bound after them because no Chocolatier 2 or 1 script traverses globals (no `_G`, `getfenv`, `setfenv` or `rawget`
   in either archive's bytecode).
5. `registerWindowTypes` (module).
6. Prelude and `host.lua`; `settings.xml`, `hiscorelocal`, `strings.xml`, the `k*` constants.
7. `registerGlobals` (module); `gVersionString`, honouring `versionFromTitle`.
8. `profile().mainloop` (default `ui/mainloop.lua`).

The App: mount the assets; `games::detectGame` and log `[game] <id> (<why>)`; take the user dir
(`defaultUserLeaf`), title, quirks and typing sound from the module; build the audio service with the module's event
table and `prefetch()`; build the context, set `module`, `attach`; build the `ScriptHost`; `registerAppWindows`; boot.

## 7. Build

One `choco_core` static library and one `openconche` executable. Sources are assembled from lists, with no GLOB for
project code and no whole-archive:

- `CHOCO_ENGINE_SOURCES`: everything outside `games/` except `games/Registry.cpp`, which is listed with them.
- `CHOCO_COMMON_SOURCES`: `games/common/`, always built.
- Per module, `src/games/<id>/module.cmake` holds one `choco_game_module(<id> [REQUIRES ...] SOURCES ... TESTS ...)` call
  with explicit file lists. The function skips the module when `CHOCO_GAME_<ID>` is OFF (choc3 has no option), fails if a
  `REQUIRES` module is off, appends the sources and unit tests, and defines `CHOCO_GAME_<ID>=1` for the registry and the
  tests. choc2 and choc1 `REQUIRE` choc3.
- `CHOCO_GAME_CHOC2` and `CHOCO_GAME_CHOC1` default ON; the `choc2_*` and `choc1_*` CTests are conditional on them. With
  one off, the build answers the missing game's `--game` with a one-line refusal.

Generated stub tables live with their game: `src/games/<id>/generated/Stubs_<id>.inc`, produced by
`tools/gen_stubs.py` from `docs/re/api-*.tsv`, which stay the source of truth.

## 8. Enforcement

`tools/check_layering.py` runs as the CTest `layering` (`--root <repo root>`). Beyond the include rules of section 2 it checks:

- **No `games::` in engine code**, and no old-path includes.
- **No generated game tables in an engine directory**: no `generated/` directory may exist under an engine directory,
  except those in `GENERATED_ALLOWED` (empty).
- **`BANNED_TOKENS`**: strings that name Chocolatier content (`1946`, `date_format`, `month_`, `ingredients.xml`,
  `products.xml`, `ports.xml`, `quests.xml`, `CreateIngredient`, `CreateProductFromXML`, `knownRecipes`, `AudioEvents.inc`,
  `EventTable::builtin`, `ui_click`, `firework`, `Chocolatier3`, `setMusicState`, `C2Mode`, `gameplay`, `minigame`,
  `factory_wait`, `sfx/sfx_typing`) must not appear in the code of an engine directory. It checks `.h`, `.cpp` and `.inc`
  files after stripping comments, so comments that cite evidence stay. A moved item adds its token here so it cannot drift back.
- **The text of the old `Game` enum values** occurs nowhere in the tree.

The check covers `src/`, `tests/`, `tools/`, `lua/` and `cmake/`; `docs/` and `third_party/` are not subject to it.

## 9. What stays in the engine core, and why

The test for a move: code that names Chocolatier content (constants such as the calendar, file or XML element names, Lua
callback names, the shape of a save, event and resource lists) belongs to a module. Mechanism any game could call stays,
even where the original registered it from game code (`RegisterBSGLuaHelpers` registers `Mod`, `Floor` and `Dollars`
along with `Date`; `docs/re/startup-and-assets.md`).

- **Moved to modules:** the calendar (`games/common/Calendar`, the `Date` and `DayOfYear` natives in choc3); the content
  loaders and their natives (choc3: the Lua callbacks are Chocolatier 3 script names); the save fingerprint
  (`isUninitialisedPlayerSave`, behind `refuseSave`); the `SoundEvent` table, its data files and the generated
  `AudioEvents.inc` (`games/choc3/audio/`, behind `audioEvents`); the Chocolatier 2 music state machine
  (`Choc2Music`); the text-box typing sound (`GameProfile::typingSound`: it is `TCustomTextEdit`, Chocolatier 3 code,
  `docs/re/audio.md`); the user-dir leaf (no default argument in `defaultUserDir`; callers pass the profile's).
- **`Dollars`, `Mod`, `formatDollars`**: no game data. Sign and separator come from Lua globals; the `"$0"` literal
  is the native's documented quirk (`api-globals.tsv`). Both games' C++ call them.
- **`StringTable`, `loadSettings`, `readExcelRows`, `KeyValues`; `GetConfig`, `IsEnabled`, `CheckConfig`, the string
  natives**: generic config and strings; Chocolatier 2's Lua calls `bsgLoadStringFile` and `bsgGetString`.
- **`script/Loaders`**: Lua 5.0 `dofile`, `loadfile`, `require` over `AssetFS`, not content loaders.
- **`audio/EventTable` mechanism** (kinds, groups, music roles, aliases, `resolve`): the CADI event model belongs to the
  middleware. The table is data (`EventTableData`) a module supplies. The CADI runtime (`cadiPath`, `resourceAt`'s default
  `cadi/audio/<basename>`, the mixer's `cadi` flag) is the layout the engine serves. `ui_click` and `firework*` have table rows
  instead of special cases; no Lua of any game sends a `firework*` name.
- **`assets/AssetSource`: the CAD container and `kReforgedAlias`** (two Chocolatier 3 file names). Assets are mounted
  before detection, so no module is known yet; the aliases only add served names, and `SoundCache`'s de-duplication by
  primary path relies on them.
- **`app/`**: `findDefaultAssets`, the test-scene text, dialog titles and usage text. The composition root may know
  games. The default asset directory comes from the repo layout; a profile field could not choose it, since detection
  needs the assets mounted first.
- **`platform/Window.h`'s default title**: always overwritten from `profile().windowTitle`.
- **The `WindowManager` generation flags** are SDK-generation behaviour, set from `profile().quirks`.
- **Comments citing Reforged or Chocolatier as evidence**: they are evidence, not knowledge.
