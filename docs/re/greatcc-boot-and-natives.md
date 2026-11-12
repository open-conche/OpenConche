# Great Chocolate Chase: boot, Lua native API, data and persistence managers

Game: The Great Chocolate Chase (PlayFirst 2008, settings.xml `game` = `chocolateshop`, internal name `greatchocolatechase`),
Playground SDK 4.0 like Chocolatier 3. Scope of this note: startup, every Lua native the binary registers, window-type
registration, `VLuaStyle`, `VValueManager`, `VAssetManager`, the block classes, `Rand`/`RUtil`/`CSUtil`, persistence and the
achievement manager. Window classes themselves belong to the window analysts; where a native lives on one, only the class is named.

Sources: Mac i386 Ghidra decompile `re/greatcc/decomp/` (addresses below are those of `re/greatcc/index.tsv`, i.e. real VAs), the
Mach-O (`otool`) for string/array reads, the shipped Lua/XML (Mac text copy; the Windows `assets.pfp` XML is byte-identical for
`values.xml`, `achievements.xml`, `assets.xml` and `settings.xml`), and our existing notes. Tags: **F** fact (read from
code/bytes), **I** inference, **U** unknown.
The companion table `docs/re/api-greatcc.tsv` classifies every name (rows 1-158 are `re/greatcc/lua-undefined-globals.tsv` in its order).

## 0. Headlines for the implementer

* **F** The ScreenInit Lua prelude is semantically identical to Chocolatier 3's (same 0x3aa2-byte chunk length, all 111 `G[...]`
  names equal, only source line numbers differ; extracted with `workbench/re/lua50_chunks.py`, scratch under
  `re/greatcc/scratch/boot/lua/`). Our `lua/prelude.lua` therefore already covers the whole DSL (`MakeDialog`, `DoModal`, `SetStyle`,
  `DoTransition` is NOT in it, see 2.4).
* **F** Of the 158 undefined globals: 79 already exist in our engine (class a), 9 are SDK-shared but missing or different (b),
  33 are game natives (c), 35 are game window types (d), 2 are stray nils (e). One new engine alias is needed: `TSlider`.
* **F** The Blowfish key of the player file differs from Chocolatier 3: `_ENCRYPTION_KEY` = `z/aM/Sm6YAAA` (section 5.1).
* **F** Persistence is NOT a Lua save string. All progress lives in `prefs.dat` per-user sections as ints and raw binary
  blobs (`RIntBlock`, `VBinaryBlock`, saved `VValue`s). There is no `SaveGameString`/`.choco3`.
* **F** The `locale` setting is ignored: `InitLangugeForCurrentUser` always loads `strings/en/strings.xml`.
* **F** Many natives exist only while their owner window/manager exists (ShopWindow, VWorldMap, THiscore... re-register in the
  ctor and set the global to nil in the dtor). Order of registration matters (2.1).

## 1. Startup

### 1.1 `Main(TPlatform*, const char*)` (0x2539e) order of operations (F)

1. Config: `kGameName` ("gamename") = `greatchocolatechase`; `kVsyncWindowedMode` = `1`. (`PlaygroundInit` 0x246d0, called from the
   C runtime init before `Main`, sets: publisher `Playfirst`, game name, `kEncryptionKey` = `_ENCRYPTION_KEY`, `kPFGameHandle` =
   `greatchocolatechase`, `pfgamemodename1..4` = the four hiscore mode names, `pfgamemedalname1..7` = the seven medal names;
   see 5.5.)
2. `InitializeGameState()` (0x249d2): creates the `TGameState` singleton (registers GetState/_SetState/QueryMuteGame/
   QueryRestartMode/QueryJumpToMenu) and declares ~20 telemetry states (2.5). No gameplay effect.
3. Renderer options all set to `"1"`: `new_subtractive`, `new_render_blend`, `texture_rectangle`.
4. `TSettings::CreateSettings()`: new `TSettings` (0x18 bytes). Its ctor, in this order: creates `TPrefs` (loads `prefs.dat`,
   which decrypts with the key above), `TPfHiscores`; registers the 24 TSettings natives (2.2); `DeleteIncompatableUsers`;
   reads global pref `gamemode` (default 0) and calls `SetCurrentGameMode(it)`; reads global `curuser` (default -1) and calls
   `SetCurrentUser(it)` (a no-op for -1; for a valid user this loads all per-user state, see 5.4).
5. `TSettings::InitGameToSettings()`: `SetDisplayForCurrentUser` (global pref `fullscreen`, default 1, then
   `TPlatform::SetDisplay(800, 600, fullscreen == 1)`), `SetSoundForCurrentUser`, then `LoadStateForCurrentUser` **unconditionally**
   (with `curuser = -1` the blocks stay at their defaults, because every block was created with the "allow without user" flag off;
   `values.xml` is still parsed).
6. Window title from string-table key `app_title`; `RTransitionManager::CreateTransitionManager()`;
   `VSoundManager::CreateSoundManager()`; mouse cursor texture `cursor/chocolate_cursor_2`, hotspot (0,0).
7. `AddWindowType` for 46 window types (section 3.1).
8. `TScript::RunScript("scripts/mainloop.lua")`: the file returns `Main`, which `RunFunction` starts as the coroutine body right
   away (same mechanism as Chocolatier 3, `docs/re/window-system.md` near line 468).
9. A `TAssetMap` pre-registers three UI textures (`ui/buttons/oval_lt_blue_up`, `..._down`, `ui/backgrounds/split9_dialog_small`)
   (I: a preload that is never freed, nothing reads it afterwards).
10. Event loop until event type 4 (quit); then `TSettings::DeleteSettings`, `RTransitionManager::Delete...`.

### 1.2 Event loop (F, event numbers are the SDK `TEvent` type ids; their names are I)

Each pass: `TPlatform::GetEvent`, then
* type 3 (window-close request): if `TSettings` quitting flag (this+0x16) is false, `RunScript("scripts/quitverify.lua")`.
* type 0x12 with `arg != 1` (I: lost focus) while the "inside game" flag (this+0x14) is set: `InsideGame(false)` then queue
  `DoModal('scripts/pause.lua');` through `RUtil::DoLuaString`.
* type 0xb (key down) with key == `kEscape`, same condition: same pause path. (`RGame::OnKeyDown` does the same for Escape
  when an `RGame` window has focus; no shipped Lua creates an `RGame`.)
* type 0x13 (I: display change): `TSettings::UpdateFullScreen` (writes global pref `fullscreen`).
* Then `RTransitionManager::HandleEvent` (vtable +0xc) gets the first chance; only if it returns false, `TWindowManager::HandleEvent`.

### 1.3 What `mainloop.lua` / `mainmenu.lua` expect (F)

* `scripts/mainloop.lua`: `Main()` = three `DisplaySplash(movie, graphic, 4000)` (publisher, developer, distributor) then
  `while true do DoMainWindow("scripts/mainmenu.lua") end`; ends with `return Main`. It needs the ScreenInit globals only.
* `mainmenu.lua` requires `scripts/style.lua` and `scripts/chocshopstyle.lua`, defines `CheckUsers()` (`GetNumUsers() > 0`, else
  `gEnterNameValue = ""; name = DoModal("scripts/entername.lua"); CreateNewUser(name)`), builds `mainmenu_dialog` containing a
  `TMainMenu{ fit = true, RWindow{...} }` (labels use `GetCurrentUserName()` at construction), Adventure button:
  `SetState("MainMenuScreenTime",0); SetCurrentGameMode(0); if CheckUsers() then DoTransition(kFadeToBlack); SwapToModal('scripts/worldmap.lua')`,
  Endless: same with `SetCurrentGameMode(1)`. Quit: `DisplayDialog{"scripts/yesno.lua",...}` then
  `PostMessage(CreateNamedMessage(kQuitNow, "Quit"))`. On load it posts `kFadeFromBlack` and enables the firstpeek buttons from
  `GetConfig("firstpeek") == "1"`.
* `TMainMenu::PostChildrenInit` plays music `audio/music/theme.ogg` (loop, flags 1,1), hides the child `mainmenu_debug_button`
  (flags = 0), calls `SetWelcomeText`, and clears flag bit 8 of child `moregames` ("moregames" id) when `xsell/xsell.lua`
  does not exist (it does not ship), I: hides the "more games" button.

### 1.4 `settings.xml` / config keys read (F)

| key | consumer | value shipped |
|---|---|---|
| `game` | engine game detection (our `Registry.cpp`); the binary never reads it | `chocolateshop` |
| `gameversion` | `InitializeGameState`: `add-value` telemetry state (config `kGameVersion` = `"version"`; I: the settings.xml `gameversion` is what `kGameVersion` maps to) | `dl,pf,1.0` |
| `serverversion`, `locale` | SDK; **`locale` is not used for strings**: `InitLangugeForCurrentUser` hard-codes `en` (`strings/en/strings.xml`) | `3.0`, `en` |
| `hiscorelocal`, `hiscoreanon` | `kHiscoreLocalOnly`/`kHiscoreAnonymous` globals; THiscore | `0`, `1` |
| `firstpeek` | Lua `GetConfig("firstpeek") == "1"` (endlevel/worldmap/mainmenu), plus the SDK `firstpeek.txt` check | `0` |
| `cheatmode` | `IsEnabled(kCheatMode)` in game.lua/worldmap.lua enables the `game_cheats` button | `0` |
| `zones_debug` | `ZoneController` ctor: `GetIntConfig("zones_debug", 0) == 1` sets a debug flag (this+0xc) | `0` |
| `game_debug` | Lua only: `EnableWindow("game_debug", GetConfig("game_debug") == "1")` (game.lua:239) | `0` |

`TSettings::GetIntConfig(key, def)` = `atoi` of the config string, `def` when empty (F).

### 1.5 Detecting this game in our engine (F/I)

`src/games/Registry.cpp` already reads `settings.xml` `game`; a new module's `claims()` should return true for
`settingsGame == "chocolateshop"` (both Mac and Windows trees ship that `settings.xml`, F). A file-based fallback (no settings.xml)
could key on `scripts/mainloop.lua` + `locations/defines.lua` + `values.xml` (I). Run-time identity keys: user-dir leaf
`greatchocolatechase` (I, from `kGameName`, section 5.1).

### 1.6 `TGameState` (telemetry/state store) (F)

`TGameState` (shared SDK class) is a string-to-string map with observer lists. `GetState(key)` returns the stored string ("" if
none) after letting observers rewrite it. `SetState(key, value)`: a key starting with `++` increments the integer state of the
rest and stores that; observers may consume the change; the key `mute` additionally saves/zeroes/restores the master volume.
`InitializeGameState` only declares telemetry timers/sessions: `SessionTime`, `SessionData` (begin/end), timers
`StoryModePlayTime`, `MainMenuScreenTime`, `MetaMapScreenTime`, `HelpScreenTime`, `UpgradeScreenTime`,
`SweetRewardsScreenTime`, and elapsed-bucket probes `Progress-15/30/45/59` over `LatestLevelStory`. The Lua `SetState("X", 0|1)`
calls start/stop those timers; there is no gameplay reading except `SweetRewardsEarnedID` (6.4). A reimplementation can store
the strings and ignore the timers (I).

## 2. The Lua native API

### 2.1 Registration mechanism and order (F)

Every native is installed as `lua_pushstring(L, name); lua_pushdirectclosure<Class, Sig>(L, instance, &Class::method, 0);
lua_settable(L, LUA_GLOBALSINDEX)` on the **window manager's main script state** (`TWindowManager::GetInstance()->GetScript()`),
i.e. a plain global. `lua_pushdirectclosure` (LuaPlus `LPCD`) stores `{this, member-fn-ptr pair}` in a userdata upvalue.
Argument marshalling is the unchecked LuaPlus one: args start at stack index 1, `int`/`float`/`double` = `lua_tonumber` (missing
or non-number = 0), `unsigned int` = number saturated to [0, 4294967295], `bool` = `lua_toboolean`, `str` = `lua_tostring` (nil =
empty), returns: `bool` pushboolean, numbers pushnumber (enums as numbers), `str` pushstring, `void` zero results.
No arity or type errors are raised.

Lifetime: window/dialog natives (ShopWindow, THiscore, THiscoreSubmit, VWorldMap, VRewardsWindow, VUpsellWindow, TChoosePlayer,
TMainMenu, TOptions, VHelpWindow, VPrelevelWindow, VLevel) are registered in the window's ctor. The destructors of ShopWindow (all 7 names),
THiscore (`GetState`, `ScoreAvailable`, `UpdateText` only), THiscoreSubmit (`SubmitToServer` only; F, the pushnil is visible in the decompile), VWorldMap (4),
VRewardsWindow (1), VUpsellWindow (2) set globals to nil (pushstring name, pushnil, settable). For the last four the name subset is I, derived
from the `pushstring` counts of the two emitted dtor variants. The manager singletons (VValueManager, VLevelManager,
VAchievementManager, VSoundManager, RTransitionManager, TGameState) register once at first `GetInstance`.
Order at boot: TGameState (Main step 2), the 24 TSettings natives (step 4), then, from inside `SetCurrentUser`/
`LoadStateForCurrentUser` (step 4/5): VValueManager, VLevelManager, VAchievementManager, VTutorialManager, VQuestManager singletons
(natives registered in that order, files loaded: `values.xml`, `levels/goals.xml`, `achievements.xml`), then RTransitionManager,
VSoundManager (step 6).
Natives on a short-lived window are therefore **not callable from outside that window's lifetime** (e.g. `LevelSetup` outside a
ShopWindow); Lua never does (I).

Anim scripts (`.anm`) run in their own Lua state per sprite (`TAnimatedSprite::NewScript` 0xa0dc8 registers the SDK set:
SetCurrentFrame, WaitLuaNextFrame, Die, Pause, IsPlaying, Load, GetScript, SetAlpha, SetLayer, RemoveSprite, Random; plus the
embedded `animationscript.lua`, bytecode identical to Chocolatier 3's). Game additions to those states:
`VSprite::NewScript` (0x49c3a) adds `getdelay()` (-> `VSprite::GetDelay`, uint) and `playsound(file, loop)` (-> `VSoundManager::PlaySFX`);
`VPress::Init` adds `unlock()` to the `anim_largeweight` sprite's state and `drop()` to the `anim_door` sprite's state
(`VPress::Unlock/Drop`); `VInfuse::Init` does the same with `VInfuse::Unlock/Drop` (names from the pushstring/pushdirectclosure
pairs, targets F; which sprite names VInfuse uses: U). They are called from text inside `.anm` files
(e.g. `playsound("audio/sfx/macaw.ogg", false)`).

### 2.2 TSettings natives (ctor 0x3eae8), 24 names (F)

All take/return as in the table; `T` = `TSettings*`. Field map: +0 difficulty int, +4 `TPrefs*`, +8 `TPfHiscores*`, +0xc `RState*`,
+0x10 game mode int, +0x14 insideGame, +0x15 gameLoaded, +0x16 quitting (size 0x18).

| Lua | C++ | behaviour |
|---|---|---|
| `ToggleFullScreen()` -> bool | `ToggleFullScreen` 0x3c1ae | `TPlatform::SetFullscreen(!IsFullscreen)`; on success `UpdateFullScreen` (global pref `fullscreen` = 1/0); returns success |
| `ToggleSoundMute()` | 0x3c1f6 | per-user `soundoff`: stores `old != 1`; master volume 0.0 when now muted else 1.0 |
| `SetCurrentGameMode(n)` | 0x3c2d4 | `n` unsigned < 4, else ignored. Stores mode, **zeroes difficulty**, hiscore property 2 := `_PFGAMEMODENAMES[n]`, global pref `gamemode` := n (flush), telemetry `StoryModeSessionTime` := (n == 0) |
| `GetCurrentGameMode()` -> n | 0x17ade0 | the stored 0..3 |
| `SetCurrentDifficulty(d)` | 0x3c406 | `d` unsigned < 3 else ignored; if mode > 0 first `SetCurrentGameMode(d+1)` (zeroing difficulty) then stores `d`. Not persisted |
| `GetCurrentDifficulty()` -> d | 0x17adec | stored difficulty (not called by Lua) |
| `GetNumUsers()` | 0x3c568 | `TPrefs::GetNumUsers` |
| `CreateNewUser(name)` | 0x3e83e | 5.3 |
| `SetCurrentUser(i)` | 0x3e700 | 5.3 |
| `GetCurrentUserName()` | 0x3c438 | per-user pref `name` ("" default) |
| `ChangeCurrentUserName(name)` | 0x3ce90 | 5.3 |
| `DeleteUser(i)` | 0x3e7fa | 5.3 |
| `IsNameInUse(name)` | 0x3cc4a | exact string compare against all user names |
| `QuitToMainMenu()` | 0x3c57a | while the top modal exists and has a parent: `PopModal(id = "%8x" of top modal's id)`; then `VSoundManager::PurgeSounds`. Every modal but the Screen has a parent, the one `DoMainWindow` pushed included (I), so `Main` then pushes a new main menu |
| `InsideGame(b)` | 0x3c726 | story mode: telemetry timer `StoryModePlayTime` := b; sets flag; `b` and app not foreground -> queue the pause dialog |
| `AreInsideGame()` | 0x17add4 | flag |
| `GetLastLevelCompleted()` | 0x3ce0a | `RIntBlock "highest_level"`, 0 if none |
| `IsGameLoaded()` | 0x17adc8 | flag +0x15: set in `ShopWindow::PostChildrenInit`, cleared in both `~ShopWindow` |
| `SetQuitting(b)` / `AreQuitting()` | 0x17adf8 / 0x17ae08 | flag +0x16 |
| `GetScore()` | 0x3cb14 | `RIntBlock "score"`, 0 if none |
| `random(n)` | `LuaRandom` 0x3cbd4 | `TPlatform::Rand() % n + 1` (n = 0 divides by zero) |
| `rem(a, b)` | `LuaRem` 0x3cbf2 | `(int)(a - b * floor(a / b))` doubles |
| `LaunchURL(url)` | 0x3cbaa | `TPlatform::OpenBrowser` |

`InsideGame(true)` is the last statement of `game.lua`; `IsGameLoaded()` guards the debug/cheat buttons (cheats.lua, worldmap.lua:59).

### 2.3 Natives on other owners (F; arguments from the template signature, call sites from the shipped Lua)

Full list with notes: `docs/re/api-greatcc.tsv`. By owner:

* **VValueManager** (ctor 0x601a4): `GetManagedInt(name, default)`, `GetManagedUint`, `GetManagedReal`, `SetManagedValue(name, value, save)` (section 4.2).
* **VLevelManager** (ctor 0x2123e): `GetMasteryScore(level)`, `GetGoalScore(level)`, `GetCurrentResult()`, `GetHighestComplete()`,
  `GetCurrentLevel()`, `SetCurrentLevel(n)` (section 5.6).
* **VAchievementManager** (ctor 0x4922): `CheckAllAchievements(category, show, closeCond)`, `ShowNewAchievements(closeCond)`,
  `HideNotification()` (section 6).
* **VSoundManager** (ctor 0x7e85c): `PurgeSounds`, `PurgeVOQueue`, `PlaySFX(file, loop)`, `PlayMusic(file, loop, bool)`, `PlayVO(file, bool, bool)`,
  `PauseSounds(pause, group)`. Shipped Lua calls only `PauseSounds(true|false, kSFXSoundGroup)` (pause.lua, prelevel.lua). Audio analyst owns the rest.
* **ShopWindow** (ctor 0x42a04, nil in dtor): `DoDebug` (C++ `Debug`), `LevelSetup`, `ReadyForPrelevel`, `DoCheatGame(type, param)` -> string (C++ `DoCheat`),
  `GetEndlevelText(status)` -> string, `DoCommand(str)`, `DoMetrics(int, bool)`.
* **VWorldMap** (ctor 0x60958): `ResetMap`, `DoCheatMap(type, param)` -> string, `PlayTransition(locationName)`, `ShowCurrentLocation`.
* **TChoosePlayer** (0xd9ac): `SetPlayerChoices`, `GetSelectedUserIndex`, `GetSelectedUserName`, `SetNameField(buttonName, text)` (5.3).
* **TMainMenu** (0x26c66): `SetWelcomeText`, `MainDebug` (= `VAchievementManager::AcquireAll`).
* **TOptions** (0x2b7f8): `SaveVolumes` (reads sliders `options_sfxlevel_slider`/`options_musiclevel_slider`, stores `int(value * 1000)` as per-user `soundlevel`/`musiclevel`; `PostChildrenInit` seeds the sliders from the same prefs, default 750).
* **THiscore** (0x19fc4, nil in dtor): `GetState` (returns the tab state int), `ScoreAvailable`, `UpdateText`, `LaunchPrivacyPolicy`.
  **THiscoreSubmit** (0x1b348): `SubmitToServer(account, pass, name, remember)`, `InitUserScore` (runs a Lua line that sets `gNameEdit`, `gAccountEdit`, `gPassEdit`, `gRemember`).
* **VHelpWindow**: `SetPage(n)`. **VPrelevelWindow**: `Close()`. **VRewardsWindow**: `RewardWindowReady()` (C++ `Ready`). **VUpsellWindow**: `SetupUpsell()`, `LaunchGameLink(n)`.
  **VPlotManager**: `GetPlotMessage()` (C++ `GetPostLevelMessage`, unused by Lua). **VLevel** (per level object): `NeedPrelevel()`, `NeedPlot()` (virtual slots +0x10/+0x14, unused by Lua).
* `TGameState`: `GetState(key)` (name collision with THiscore's `GetState`; the later registrant wins, `docs/re/api-globals.tsv`), `_SetState`, `QueryMuteGame`, `QueryRestartMode`, `QueryJumpToMenu`.
* `TWindowManager::AttachWindowScript`: `SetTopModalOnly(bool)`. `TScript::LuaInit`: `DebugOut(str)` (+ `_ALERT` handler). `TXSellWindow` ctor (compiled in, never constructed by Lua): `SetupXSell`, `LaunchGameLink(int)`.
* `TWindowScript` ctor (0xdc4a0): 42 underscore/plain natives, unchanged from Chocolatier 3 (`PostCreate GetLabel _RegisterHotkey SetLabel TagHotKey _SetBitmap SetPosition SetCommand StripKeyFromLabel SetWindowName FillRect GetTimer GetConfig IsEnabled DoSelectLayer EnableWindow GetButtonToggleState SetButtonToggleState PopWindow RealSetFocus StringToClipboard UpdateWindow PushModal ClearModalChildren GetWindowDepth DoPopModal HandleGetMessage GenericCreate CreateNamedMessage CreateCloseMessage PostMessage PostMessageToParent _GetString StartMovie IsMoviePlaying StopMovie ImageLoaded DestroyWindow DoFitToChildren`, plus the `pfMessageMetatable`). Documented in `docs/re/api-windows.md`.

### 2.4 `DoTransition` (F)

Not a plain native. `RTransitionManager`'s ctor (0x73edc) registers the functor `Lua_DoTransition(type)` and runs a Lua definition:
`function DoTransition( type ) local ret = Lua_DoTransition( type ); while( ret ~= true and ret ~= false ) do ret = Yield(); end return ret; end`.
`Lua_DoTransition` (0x73a9c): if the argument is a number, looks up the transition object (`GetTransition(type, "")`), stores the Lua state in it,
starts it and **yields the coroutine**; if the type is unknown it returns `false` immediately. The transition ends by resuming the coroutine
with a boolean. Transition ids are Lua constants (`scripts/transitions.lua`: `kFadeToBlack` 1001 ... `kTintOff` 1011). Behaviour of the transitions: RTransitionManager analyst.

### 2.5 Reconciliation of the 158 undefined globals (F)

| class | count | what |
|---|---|---|
| (a) in our engine | 79 | ScreenInit-defined DSL functions/constants (45), TWindowScript natives (9), TSettings/Users natives (CreateNewUser, SetCurrentUser, DeleteUser, ChangeCurrentUserName, GetNumUsers, GetCurrentUserName, IsNameInUse, ToggleFullScreen, ToggleSoundMute, SetCurrentGameMode, GetCurrentGameMode = 11), SaveVolumes, hiscore natives (ScoreAvailable, LaunchPrivacyPolicy, GetState, SubmitToServer; choc3 only), `DebugOut`, `HiscoreWindow`, `Window/Text/Bitmap/TextEdit`, `require/table/tostring` |
| (b) SDK-shared, missing/different | 9 | `QuitToMainMenu`, `LaunchGameLink`, `SetPlayerChoices`, `InitUserScore`, `gAccountEdit/gNameEdit/gPassEdit/gRemember`, `TSlider` (alias of `Slider`) |
| (c) game native | 33 | TSettings extras, VValueManager/VLevelManager/VAchievementManager natives, ShopWindow/VWorldMap/V*Window natives, `PauseSounds`, `DoTransition`, `random`... |
| (d) game window type | 35 | RWindow family, K*, V*, ShopWindow, TMainMenu/TChoosePlayer/TOptions/TCredits... |
| (e) stray | 2 | `playername` (console.lua:61 reads an unset global; entername.lua has a *local* of that name), `kDefaultButtonSoundOut` (never defined, styles get `sound = nil`) |

Not in the list but registered (extras in the tsv): `GetManagedReal/Uint`, `NeedPrelevel/NeedPlot`, `GetPlotMessage`, `UpdateText`, `MainDebug`, `ResetMap`, `GetCurrentResult`, sound natives other than `PauseSounds`, `GetScore`, `rem`, `LaunchURL`, `AreQuitting`, `SetupXSell`, the Query* natives, `SetTopModalOnly`, and the anim-script globals.
Names the Lua uses that are defined in Lua files rather than natives (so absent from the engine side): `kSFXSoundGroup` and the style constants (locations/defines.lua, scripts/chocshopstyle.lua; I, not verified one by one).

## 3. Window-type registration

### 3.1 Mechanism (F)

* Every window class has a static `ClassId()` that returns the address of its `XDynamicCreate()` factory (e.g. `RWindowDynamicCreate`
  0x6ed4e = `new RWindow` (0xec bytes)). The "class id" is that function pointer; `IsKindOf(id)` compares to the class's own id then the
  parent's. `TWindow::CreateFromId(id)` (0x91558) calls the factory and returns null if it hands back the id itself.
* `TWindowManager::AddWindowType(name, id)` (0x8c6e0) forwards to `TWindowScript::AddWindowType` (0xdb512), which (1) formats and
  `lua_dostring`s the constructor wrapper (format at 0x152e44):
  `function NAME( table ) Creating( 'NAME', table ); table.typename='NAME'; return function() DoWindow( table ) end end;`
  and (2) stores name -> id in a `std::map<str,int>` (this+0x38), overwriting an existing name.
* `GenericCreate` / `TWindowScript::CreateCustomWindow` (0xdc05a) reads `typename` through `TWindowStyle`, looks it up (a missing name
  inserts 0 and creates nothing, no error), calls `CreateFromId`, then `InitWindow`.
* The `TWindowScript` ctor itself registers six built-ins: `Slider` (TSlider), `Bitmap` (TImage), `Button`, `Text`, `TextEdit`, `Window`
  (the call order is `TSlider, TImage, TButton, TText, TTextEdit, TWindow`).

Registered by `Main` (all F, in this order): `TMainMenu TOptions TSlider TChoosePlayer TCredits RGame RScorer RTimer RDisableButton RSpriteLayer
RAdapt RWindow RParentButton RScrollable RBounceWindow RButtonedSlider RJustified RAlert RTextEditDisable RActiveTextLayer RShrinkText KSplit9Image
RSplit9Text RSplit9Expander KBar KSweepDial RCursor KDial ShopWindow VPrelevelWindow VUpgradeList VUpgradeTab VSpriteWindow VPlotWindow VWorldMap
VRegister VShopHud VRewardsWindow VRewardNotify VChooseAchievements VTintWindow VObjectWindow VHelpWindow BLayeredWindow VQuestWindow
VUpsellWindow HiscoreWindow` (= `THiscore`). Note `TSlider` is the same class as the built-in `Slider`, registered under a second name.
Not used by shipped Lua though registered: RGame, RScorer (only in a comment in hud.lua), RParentButton, RButtonedSlider, RJustified, RAlert,
RSplit9Text, RSplit9Expander, KBar, RCursor.

### 3.2 Style keys (F for what is cited, else see window docs)

* Common to every type: the `TWindow::Init/SizeAndPositionFromStyle/PostChildrenInit` keys in `docs/re/api-windows.md` section 4
  (`name x y w h align raw`, `kCenter`/`kMax` sentinels, `image`, `mask`, ...), looked up in the table, then `gCurrentDefaultStyle`, then its `parent` chain. Same SDK source in this binary (I; `TWindow` functions are the shared ones).
* `RWindow::PostChildrenInit` (0x6f1a0), applied to every `RWindow`-derived type: `imagealpha` (float, default -1; applied via `TImage::SetAlpha` if >= 0),
  `disable` (bool; true clears the window flags so it ignores input), `bgcolor` (colour, default current; calls `SetBgColor` only on change),
  `effect` (int, default 1000; if != 1000 posts `TMessage(effect, luastring, 0)` with the string key `luastring`), and, when no parent modal exists
  but a top modal does, re-parents the window to the top modal and starts window animation `0x10`. Other classes' keys: their analysts.
* `TMainMenu` and the other R/K/V windows call `TWindow::PostChildrenInit` first, so all common keys apply.

## 4. Utility and data classes

### 4.1 `VLuaStyle` (F)

A `VLuaStyle` (0x30 bytes) wraps a Lua table (`TLuaTable*` at +0x18, owning) and layers lookups:
1. the table itself (`HasKey` = `PushValue` succeeded);
2. the sub-table stored under the key `style` (`GetCurrentStyle`, held at +0x1c) searched with depth 0 (its own table only);
3. the parent style (weak ptr at +0x24, set by `Adopt/Create`), searched recursively with depth `d-1`; the default depth is `-1`
   (= unlimited, `SetDefaultSearchDepth` +0x2c), an explicit depth of 0 disables the parent step.
Accessors: `GetString(key, default[, depth])`, `GetBoolean`, `GetNumber<float|int|unsigned>` (unsigned saturates to [0, 2^32-1]),
`GetColor(key, default r,g,b,a[, depth])`, `GetTable(key, depth)` -> new style, `GetStyle(key)`, `GetNext`/`GetSize` (iterate the own table),
`Exists`, `HasKey`, `Create(table, parent)`, `Adopt`, `SetStyle`. `GetTable/GetStyle` child styles inherit the script reference (+0x10) from the parent.
Used for the `locations/**/*.lua` data tables (`VAsset::LoadFromTable`, VObject/VCustomer/VStation ... definitions); the per-class key sets are in the owning analysts' notes.
Our `src/script/LuaStyle.*` is the existing equivalent (I: only the default-depth semantics need checking).

### 4.2 `VValueManager` / `VValue` and `values.xml` (F)

* `VValueManager` singleton (0x30 bytes): `std::map<str, shared_ptr<VValue>>` at +0x18; ctor registers the four natives then `LoadValues("values.xml")`.
  `Reload()` (called by `LoadStateForCurrentUser`) clears the map and parses `values.xml` again (so saved values are re-read for the new user).
* Schema: `<values>` root, children `<value name=... [value=] [min=] [max=] [saved="true"]>` (empty name skipped), optional children
  `<modify .../>`. `saved` is true only for the exact string `true`. `value` default 0 (empty attribute -> `to_float("")` = 0); the
  initial value is also the "default" used for reloads. `min`/`max` apply only when the attribute is non-empty; defaults are
  -FLT_MAX / +FLT_MAX (0xff7fffff / 0x7f7fffff). 95 values ship; 21 are saved (current_level 0..61, highest_level 0..60, game_intro,
  total_worth, the 9 counters `num_bars num_sauce_sold num_infusions num_truffles num_deliveries num_disposed num_served num_angry endless_time`, `completed_<6 locations>`, num_levels_mastered, num_char_upgrades).
* `<modify>` (attribute `value`, float, default 0): exactly one of the following target attributes selects the type, tried in this
  order: `upgrade` (type 0, plus `category`), `plot` (type 1; I: name from the strings table, no use in the shipped file), `byval` (type 2),
  none (type 3, unconditional). `category` is read for every modifier. 36 shipped: 25 `byval="endless_diff_modifier"`, 11 `upgrade`+`category` (`gameplay`, `station`).
* `VValue::GetValue()` (0x5f116): `r = stored`; for each modifier **in reverse list order** (last `<modify>` first): active if type 3; type 0 active when
  `VUpgradeManager::IsActive(upgradeName, category)`; type 1 when `VPlotManager::IsPlotItemActive(name)`; type 2 always, and first
  **overwrites the modifier's amount with `VValueManager::GetReal(name, 0)`**. If active, `r += stored * amount` (the multiplier is the
  stored base, not the running total; so modifiers are additive percentages of the base). Finally clamp to [min, max] (max first, then min).
* `SetValue(v, save)`: clamp to [min, max], store, optional `SaveToPrefs`. `AddToValue`/`SubtractFromValue(delta, save)`: unclamped.
* Persistence: `SaveToPrefs` only if `saved` and a current user exists (`curuser != -1`): per-user pref key = value name,
  value = `(int)stored` (truncation toward zero, decimal text), flush. `LoadFromPrefs` (at the end of `LoadFromNode`) reads that key
  with default `(int)defaultValue`. So a saved fractional value does not round-trip.
* Manager API: `GetInt(name, def)` = `floorf(GetValue)` as int; `GetUint` floors, clamps to [0, 2^32-1]; `GetReal` unfloored; all three create a missing
  value (unsaved, base = `def`, no limits) so later lookups see it; `SetValue(name, v, save)` creates a missing value with base `v` (no clamp)
  and saves only if asked; `AddToValue/SubtractFromValue(name, d, def, save)`; `GetMin/GetMax(name)` (FLT_MIN 1.17549e-38 / FLT_MAX when unknown;
  FLT_MIN is the smallest positive float, not -FLT_MAX); `GetSum/GetProduct(name, x, clamp)`; `SaveValues()` saves every saved value.
  Runtime-only values (no entry or unsaved) used by Lua: `do_map_transition`, `current_level_total`, `served_this_level`, `angry_this_level`, `current_worth`.

### 4.3 `VAssetManager` / `VAsset` (F)

* `VAssetManager` (singleton; maps name -> `TAsset`, texture memory accounting `TexInfo`): loaded per level, not at boot.
  `ShopWindow` (around 0x4401..) calls `BeginLoad()`, `LoadAssetFile("assets.xml", false)` ... `EndLoad()`; `~ShopWindow` calls `ReleaseAssets()`;
  `VSpriteManager` calls `ClearUsed()`.
* `assets.xml` schema (parsed by `LoadFromNode` 0x842c0): root `<assets>`; elements `<sound file= type= memory=>` (int `type`; `memory="true"` = decode in memory;
  shipped: 31 sounds, type 1000 = SFX group, all `memory="true"`), `<texture file= [attr2] [attr3]>` (with a second attribute: `TTexture::GetMerged(file, attr2, int attr3)`; without: `TTexture::Get(file, int, 0)`),
  `<animatedtexture file= a= b=>` -> `TAnimatedTexture::Get(file, a, b)`. The exact texture/animatedtexture attribute names are U (the shipped file only has `<sound>`).
  Unknown element names are ignored. `AddAsset` registers the object under the `file` name and, for textures, accounts VRAM.
* `VAsset` (0xe8 bytes) = a sprite's art set, built by `VAsset::LoadFromTable(styleTable, preload)` from a `locations/**.lua` table: the table optionally has `baseDir` (prefix
  for every `file`/`mask`); every table-valued entry is one **state**: `state` (default `"default"`), `file` (skipped if empty), `mask`,
  `anim` (bool, default true, search depth 2), `regX`/`regY` (float, depth 1; stored only if not both ~0), `base` (int, default 0, depth 1), `layer`
  (int, default INT_MIN = "unset", depth 3), `play` (string, depth 1). The asset name is lowercased. `GetTexture(state)`: unknown state falls back to
  `default`; `anim == false` -> `TTexture::Get/GetMerged(file, mask)`, `anim == true` -> `TAnimatedTexture::Get(file, "", mask)` (the `.anm`);
  textures are cached per state and registered with the manager. `UnloadAsset("all"|state)`, `PreLoadAsset("all"|state)`. For our engine the VRAM accounting and `TexInfo`
  can be dropped; only the table parsing and the state fallback matter.

### 4.4 Blocks: `RBlock`, `RIntBlock`, `VBinaryBlock`, `RState` (F)

* `RBlock` base: `name` (+0xc), unused `-1` (+0x10), `allowWithoutUser` flag (+0x14, always 0 in this game). `ArePrefsValid` = prefs object exists and
  (current user index >= 0 or the flag). Vtable slots: +0x10 `Reset`, +0x14 `Load`, +0x18 `Save`. The key of a block in prefs **is its name**, in the current user's section.
* `RIntBlock(name, default, flag)` (0x20 bytes, default +0x18, value +0x1c): `Load` = `GetInt(name, default, user)`, `Save` = `SetInt(name, value, user, flush)`, `Reset` = default.
* `VBinaryBlock(name, size, fillByte, flag)` (0x28 bytes, data ptr +0x20, length +0x24): `Reset` re-allocs to `size` filled with `fillByte`; `Load` asks `GetBinary(name, null, 0, user)`
  for the stored length (-1/<=0 -> `Reset`), resizes to the **stored length** (not the default) and copies; `Save` = `SetBinary(name, data, length, user, flush)`.
  `Set(fill, off, n)` grows the buffer when `off+n >= length`.
* `RState` = list of `shared_ptr<RBlock>`: `AdoptBlock`, `GetBlock(name)`, `GetIntBlock`, `GetBinaryBlock`, `SaveBlocks`, `ResetBlocks(save)`, `LoadBlocks` (= `ResetBlocks(false)` then each `Load`).
* `RBoolBlock`, `RFloatBlock`, `RStringBlock`, `RMapBlock` exist in the binary but nothing here constructs them (F by absence of any caller).

### 4.5 `Rand`, `RUtil`, `CSUtil` (F)

* `Rand` (singleton `Rand::Get`, 0x9d0 bytes, derives `TRandom` = MT19937 as documented in `docs/re/trade-and-save.md`, seeded from `TPlatform::Rand()`):
  `RandInRange(lo, hi)` = `lo + (float)(Rand32() * 2^-32) * (hi - lo)` (constant 2.3283063671497593e-10, float arithmetic, half-open). Used by BZeppelin etc.
* `RUtil`: `DoLuaString(s)` runs `table.insert( gCommandQueue, function() <s> end );` in the main state (so the command executes later from the main loop; also bumps a debug counter);
  `Trim(s)` strips spaces at both ends (only the space character; a result that is all spaces becomes ""); `FillRect(rect, color)`, `DrawRect(rect, color, drawSpec)`
  (2D quad / line loop), `MouseDispatch(window, point, eventType)` (hit-test a child, convert coordinates, dispatch `TWindow::HandleEvent`, re-entrancy guarded by a set),
  `TextToTextureWithAlpha`, `WindowToTexture(window, hasAlpha, color)` (render a window into a texture).
* `CSUtil::ParseComparisonString(s)` -> bit mask: lowercase `s`; tokens separated by `+` with spaces removed: `not` = 1, `lessthan` = 2, `greaterthan` = 4, `equal` = 8;
  then any occurrence of the characters `>` (4), `<` (2), `=` (8), `!` (1) also sets bits. `CSUtil::CompareValue(a, b, mask)`: `less = a < b` if bit 2 and not bit 4, `greater = a > b` if bit 4 and not bit 2
  (both bits: false unless bit 8, in which case it computes `a < b`), `|= a == b` if bit 8, `^= 1` if bit 1. So `>=` = 12, `greaterthan + equal` = 12.
  (Quirk: `lessthan + greaterthan + equal` evaluates `a <= b`.)

## 5. Persistence

### 5.1 Container (shared SDK, F where cited)

`TPrefs` (0x34 bytes) is the SDK class documented in `docs/re/prefs-and-saves.md`: `prefs.dat` is a chunk file (`IKEY`/`USER`/`KVPR`), all non-`USER` chunk payloads
Blowfish-ECB encrypted with the key from config `kEncryptionKey`. Differences here:
* **Key string** `_ENCRYPTION_KEY` = `z/aM/Sm6YAAA` (data 0x2150d0 -> string at 0x14e604; also in `GreatChocolateChase.exe`). Using the derivation of the other note
  (12 Base64 chars, last 3 forced to `AAA`) the 9 key bytes are `cf f6 8c fd 29 ba 60 00 00` (I: derived by hand, not verified against a real file; Chocolatier 3's `H32kwmdbCAAA`
  gives `1f 7d a4 c2 67 5b 08 00 00`, matching that note's value, so the derivation code is the same).
* **Identity key** (`IKEY`, this+0x28): config `kGameName` = `greatchocolatechase` (I; `kPFGameHandle` has the same string). User dir leaf: I: `greatchocolatechase`
  (`%APPDATA%\PlayFirst\greatchocolatechase\prefs.dat` on Windows, by analogy with the Chocolatier 3 note; U for Mac).
* Binary values (`SetBinary/GetBinary`, 0xcfc2c / 0xcf968) are stored as raw byte vectors under the key in the user's section, no NUL terminator and no length prefix
  other than the KVPR chunk length; `GetBinary(key, null, ...)` returns the length, -1 when the key is missing, 0 on success. Every setter with flush rewrites the whole file.

### 5.2 Global (section 0) keys (F)

`curuser` int (default -1: no user), `gamemode` int 0..3 (default 0), `fullscreen` int (default 1). Plus SDK keys listed in `prefs-and-saves.md` section 5 (not game specific).

### 5.3 Users (F)

User `i` is section `i+1`; `GetNumUsers` = sections - 1.
* `CreateNewUser(name)` (0x3e83e): `n = GetNumUsers()`; writes `name`; copies `soundlevel` (read default 750), `musiclevel` (default **750**, not 650), `soundoff` (default 0) from the
  current user (`curuser`, possibly -1 -> global section) into the new section; writes `userversionnumber` = `1.0` (`TSettings::USER_VERSION_NUMBER`); `SetCurrentUser(n)`.
* `SetCurrentUser(i)` (0x3e700): only if `i < GetNumUsers()` (signed; -1 passes). Writes global `curuser`. If `i >= 0`: hiscore property 1 := user name, `SetSoundForCurrentUser`, `LoadStateForCurrentUser`.
  `SetCurrentUser(-1)` does not reload state (the previous user's blocks stay in memory, and `ArePrefsValid` is false so nothing is written).
* `SetSoundForCurrentUser` (0x3c866): `soundlevel` (default 750) drives sound-type 1000 and type 0, `musiclevel` (default 750) drives type 1001, each as `level/1000`;
  `soundoff == 0` -> master volume 1.0, otherwise 0.0 (I: type 1000 = SFX group, 1001 = music; assets.xml sounds are type 1000).
* `DeleteUser(i)` (0x3e7fa): `TPrefs::DeleteUser(i)` (erases the section, later users shift down), then `SetCurrentUser(i-1)`, or `numUsers-1` when `i-1 < 0`.
* `DeleteIncompatableUsers` (ctor): iterates users `0..N-1` (N read once) and `TPrefs::DeleteUser`s each whose `userversionnumber` (default `"0"`) differs from `1.0`; because it deletes while
  indexing, the user after a deleted one is skipped (quirk; harmless for single-version files).
* `ChangeCurrentUserName(name)` (0x3ce90): if hiscores exist, for each of the 4 mode names re-logs the user's best row under the new name (using the medal XML of acquired achievements), restores properties, then writes `name`.
* `IsNameInUse`: exact compare. `GetUserName(i)` = pref `name` of section `i+1`.
* `TChoosePlayer` (0xd5c0..): `SetPlayerChoices` runs `UpdatePlayerChoices( <curuser>, <n>, {'name0','name1',...} )` (at most 10 names, no escaping); `PostChildrenInit` calls it;
  `GetSelectedUserIndex` scans children `chooseplayer_button_0..9` for the first with its toggle bit on, 0 if none; the Lua keeps `kMaxUsers = 10`.
* Difficulty / modes (2.2): mode 0 = story ("adventure"), 1..3 = endless easy/medium/hard; the difficulty is only a UI value (`SetCurrentDifficulty(d)` maps to mode `d+1`), not stored except as `gamemode`.

### 5.4 Per-user state loaded by `LoadStateForCurrentUser` (0x3d1c6) (F)

Deletes the old `RState`, makes a new one and adopts these blocks **in this order** (all flag 0), then `RState::LoadBlocks`, `VValueManager::Reload`, `VLevelManager::LoadCurrentUser`,
`VAchievementManager::LoadCurrentUser`, `VTutorialManager::LoadCurrentUser`, `VQuestManager::LoadCurrentUser`, `InitLangugeForCurrentUser`:

| prefs key (per-user section) | type | size / default | owner |
|---|---|---|---|
| `score` | int | 0 | `GetScore` |
| `level` | int | 0 | (unused by Lua) |
| `highest_level` | int | 0 | `GetLastLevelCompleted`; **same key as the saved VValue `highest_level`** |
| `level_status` | binary | 60 bytes, fill 0; byte `L-1` = status of level L | VLevelManager |
| `level_best_score` | binary | 240 bytes = 60 x int32 (native little endian), fill 0; slot `L-1` | VLevelManager |
| `level_score` | binary | 24 bytes = 6 x int32, fill 0; slot `(L-1)/10` (one per location, 10 levels each) | VLevelManager |
| `show_tutorials` | int | 1 | VTutorialManager / options checkbox |
| `tutorials` | binary | 50 bytes, fill 0 | VTutorialManager |
| `achievements` | binary | 100 bytes, fill 0; byte per achievement ordinal, 1 = acquired | VAchievement |
| `upgrades_grytviken`, `_namche`, `_krakatoa`, `_kamchatka`, `_iquitos`, `_gaberoun` | binary | 75 bytes each, **fill 0xFF** | VUpgradeManager |
| `plot_value_<same six>` | binary | 240 bytes each, fill 0 | VPlotManager |
| `plot_<same six>` | binary | 60 bytes each, fill 0 | VPlotManager |
| `quest_block` | binary | 100 bytes, fill 0 | created lazily by `VQuestManager::GetBlock`, not part of `RState` |

Saved `VValue`s (4.2) write their own int keys into the same section (`current_level`, `highest_level`, `game_intro`, `total_worth`, the `num_*` counters, `endless_time`,
`completed_*`, `num_levels_mastered`, `num_char_upgrades`). Plus `name`, `userversionnumber`, `soundlevel`, `musiclevel`, `soundoff`.
Order of the six locations everywhere: grytviken, namche, krakatoa, kamchatka, iquitos, gaberoun (the order the code adopts them; the medal array uses iquitos first, 5.5).
Layouts of the plot/upgrade/tutorial blobs belong to the plot/upgrade/tutorial analysts (U here).

Comparison with `docs/re/prefs-and-saves.md`: same container, cipher, chunk framing, user sections and `curuser`; different: encryption key, no `savegame` pref / `.choco3` / `SaveGameString`,
per-user state is dozens of keys with binary blobs (there they are only strings), a per-user `userversionnumber` that deletes mismatching users at startup, music default 750 and no `ambientlevel`.

### 5.5 Hiscores and medals (F)

`TPfHiscores` (shared) logs with property 1 = user name, property 2 = mode name. Mode names (`_PFGAMEMODENAMES`, 0x215124): `greatchocolatechase_storymode`, `_endlesseasy`,
`_endlessmedium`, `_endlesshard`; config `pfgamemodename1..4`. Medal names (`_PFGAMEMEDALNAMES`, 0x2150a0), config `pfgamemedalname1..7`: `greatchocolatechase_iquitos`, `_grytviken`, `_namche`, `_krakatoa`, `_kamchatka`,
`_gaberoun`, `_endless15`. `hiscore.dat` format: `docs/re/hiscore.md`.

### 5.6 Level progress (F; VLevel/ShopWindow semantics belong to others)

`VLevelManager` (levels 1..60 plus current_level up to 61): per level `VLevelInfo {bestScore, goal, mastery, status}`; goals from `levels/goals.xml`
(`<levelgoals><levelgoal ...>` with the level number, goal and mastery attributes; 60 entries, U for the three attribute names); loaded in the ctor. `GetHighestComplete`/`GetCurrentLevel` read the VValues `highest_level`/`current_level`
(`GetInt(..., 0)`). `SetCurrentLevel(n)`: stores n and VValue `current_level` (saved). `SetHighestComplete(n, force)`: unless `force`, ignore if `n <= highest_level`; set VValue `highest_level` := n (saved);
then for L = 1..max(highest_level)=60: statuses of levels `<= n` that are 0 become 1, statuses of levels `> n` that are non-zero become 0 (only reachable with force). `IncrementLevel`: if the current level's status != 0:
`SetHighestComplete(current)` and `SetCurrentLevel(current+1)`. `LevelComplete(score, &gain)` (needs a level object): `result = score < mastery ? (score < goal ? 0 : 1) : 2`;
`gain = (status == 0) ? score : max(0, score - bestScore)`; if stored status < result -> `UpdateState(level, result)` (writes byte, saves `level_status`); `UpdateBestScore` keeps the max and saves `level_best_score`.
Status values: 0 none/failed, 1 goal reached, 2 mastered (I from the compare structure).

## 6. Achievements (manager only)

### 6.1 `achievements.xml` (F)

Root `<achievements>`; children `<achievementcategory name=...>` (name kept as written) containing `<achievement name=...>`, or bare `<achievement>` (category ""). Child elements (tag names compared lowercase):
`<title>` and `<description>` (string-table keys), `<compare>` (comparison string, 4.5), `<threshold>` (float), `<value>` (the VValue key whose current `GetReal` value is compared), `<medal>` (hiscore medal name).
The attribute `name` is lowercased. Each achievement gets an **ordinal** (0-based, in file order across all categories); it is the byte offset in the per-user `achievements` block. 51 ship in categories `adventure`, `endless`, `endgame`; 7 have `<medal>`: the six `complete_<location>` ones (threshold 10 levels each, the value key `completed_<location>`) and `endless3` (`greatchocolatechase_endless15`).
Storage: `std::map<category, std::map<name, VAchievement>>` (string order = the iteration order below).

### 6.2 Behaviour (F)

* `VAchievement`: `LoadCurrentUser` = block byte == 1; `SetAcquired(b)` writes the byte and saves the block (flush); `CompareValue(x)` = `CSUtil::CompareValue(x, threshold, compareMask)`.
* `AcquireAchievement(category, name)`, `AcquireAll`, `IsAcquired`; `ReleaseAchievement` sets its `<value>` VValue to 0 (saved) and clears the byte; `ReleaseAll`; `GetAcquiredAchievements`, `GetAllAchievements` (lists).
  The debug windows (`VChooseAchievements`, `cheats_achievements.lua`) and `TMainMenu::MainDebug` use these.
* `CheckAllAchievements(category, show, closeCond)`: for each category (alphabetical) whose name equals `category` (or all if `category` is ""), for each achievement (alphabetical):
  `value = VValueManager::GetReal(achievement.valueKey, 0)`; `CheckAchievement(value, ...)`: if not yet acquired and `CompareValue(value)` is true it is acquired immediately and appended to the "new" queue.
  If the queue is non-empty the telemetry state `SweetRewardsEarnedID` is updated: if the current state is `"-1"` it restarts from an empty string, otherwise appends `,`; the achievement names are appended comma-separated.
  Then, if `show` is true, `ShowNewAchievements(closeCond)`.
* `ShowNewAchievements(closeCond)`: `ShowNotification(newQueue, closeCond)` then clears the queue. `ShowNotification`: builds a window from `scripts/rewardnotify.lua`, gets its child `reward_notify` as `VRewardNotify`, `AddAchievements(list)`,
  places it at its own rect, then shifts it down by the height of each already-visible notification whose rect it overlaps, adds it to the top modal, remembers it, and (for `closeCond == 0`, I) creates a shared close flag that `HideNotification()` later sets.
* `HideNotification()`: sets every pending flag and forgets them (endlevel.lua:179, 218). `NotifyComplete(window)` removes a window from the visible list.
* `GetMedalXml()`: for each acquired achievement with a medal: `<medal name="%s" per="type"/>\r\n` concatenated; used by `ChangeCurrentUserName` and the hiscore submit (ShopWindow around 0x1420).
* Callers (C++): `ShopWindow` calls `CheckAllAchievements` with `"endgame"` (when the finished level is above 59 and a flag is set), `"adventure"` (story) and `"endless"` (endless) at level end; the Lua calls only `ShowNewAchievements(0)` and `HideNotification()`.

## Unknown / not determined

* Real `prefs.dat` of this game: none available, so the key derivation, identity key and Windows/Mac user directory are inference (5.1).
* Exact `<texture>`/`<animatedtexture>` attribute names of `assets.xml` and the three `levelgoal` attribute names (attribute strings are in the data section, calls go through `GetAttribute(const char*)` thunks); only the ordering is known.
* Default arguments of the four C++ `CheckAllAchievements` calls in `ShopWindow` (the compiler passed them through registers the decompile lost): whether `show` is true at level end is U.
* `closeCond` semantics of `ShowNotification` beyond "0 creates a flag" (the rest is in `VRewardNotify::SetCloseCondition`, owner: the window analysts).
* Which `VUpgradeManager`/`VPlotManager` blob layouts the 75/240/60-byte blocks hold (other analysts).
* Meaning of `TMessage`/event type ids 0x12 and 0x13 beyond how Main uses them (I only).
* Whether `PlayMusic`'s and `PlayVO`'s extra bool parameters are `loop`/`queue`/... (audio analyst).
* `SetLayer` in the anim-script native set: target not resolved (inline thunk).
* Windows build: Lua is 5.0 bytecode in the pfp; no differences were looked for beyond the four XML files and `settings.xml` being identical.

## Implementation notes for OpenConche

* **Game module**: add a `chocolateshop` module (like `src/games/choc3/Choc3Module.cpp`): `claims()` on `settingsGame == "chocolateshop"`; register the 35 game window types (3.1) and the `@native` rows of `api-greatcc.tsv`; `Slider` needs a second registration `TSlider`.
* **Prelude**: `lua/prelude.lua` already matches the embedded chunk; nothing to add for the DSL. `DoTransition` is defined by the RTransitionManager object (2.4): our existing transition support (choc2 `ui/transitions`) may already define a different one; the GREATCC loop needs the yield-until-boolean behaviour.
* **UserStore** (`src/sdk/Users.h`) keeps per-user prefs as strings with escaping, so ints map directly (`pref(key)`); binary blocks need a string encoding (hex or base64, I) if stored that way. Three details differ from the Choc3 behaviour implemented there:
  default music level 750, no ambient level, and `userversionnumber` bookkeeping (`1.0`; the delete-on-mismatch rule is only needed to import real files).
  `curuser`/`gamemode` are **global** prefs in GREATCC; our `UserStore::hiscoreMode()` reads `gamemode` as a per-user pref - the GREATCC module should keep `gamemode` global and map mode 0..3 to the four table names in 5.5.
* **`values.xml`**: implement `VValueManager` as a name -> {base, default, min, max, saved, modifiers} map with the GetValue formula of 4.2; saved values go through the per-user prefs as truncated ints at set time.
  `VUpgradeManager::IsActive` / `VPlotManager::IsPlotItemActive` are the two callbacks it needs.
* **Natives that exist only while a window lives** (2.1): bind them in the window's constructor and unbind (set nil) in its destructor, or keep them global but guard (the Lua never calls them outside).
* **Start-up call order** (1.1) matters only for: natives registered before `mainloop.lua` runs, state loaded before the first dialog (`CheckUsers`), and `LoadStateForCurrentUser` running even with no user.
* **`strings/en/strings.xml` is always loaded**, even if the engine picks another locale for the other games.
* `RUtil::DoLuaString` -> our command queue (`gCommandQueue` in the prelude); `Rand::RandInRange` -> any uniform float RNG (sequence reproduction needs `docs/re/trade-and-save.md`'s MT19937 recipe).
