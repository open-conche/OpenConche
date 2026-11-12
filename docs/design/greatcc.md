# Design: The Great Chocolate Chase module

*The Great Chocolate Chase: A Chocolatier Twist* (PlayFirst 2008, settings `game` = `chocolateshop`, Playground SDK 4.0)
runs as a fourth game module, `src/games/greatcc/`, with its shipped Lua and data unchanged. Unlike the Chocolatier games, its
gameplay is about 150 native C++ classes; the Lua is the menu/HUD shell and the data tables. This module therefore
reimplements a game, not just a platform: the RE facts are `docs/re/greatcc-*.md` and `docs/re/api-greatcc.tsv`, cited below as
*boot*, *ui*, *sprites*, *shop*, *machines*, *meta*, *audio*, *debug* (`greatcc-debug-tools.md`) with their section numbers.

Fixed constraints: no compatibility with the original `prefs.dat`; in-game behaviour is reproduced, quirks that change play
are reproduced, fixed behind the switch of section 7, or listed as decisions in section 8; the Windows install (`assets.pfp` + loose files) and the Mac bundle's loose `assets/`
both boot; dead and online-only screens are stubs.

## 1. What the scripts depend on

- **Boot** (*boot* 1.1, 1.3): `scripts/mainloop.lua` returns `Main`; three `DisplaySplash` calls, then
  `DoMainWindow("scripts/mainmenu.lua")` for ever. The prelude is Chocolatier 3's (identical chunk). Strings are always
  `strings/en/strings.xml`. The cursor is `cursor/chocolate_cursor_2`, hotspot (0,0); the title is string `app_title`.
- **Natives**: 79 of the 158 globals the Lua reads already exist; 9 SDK names are missing or differ (`QuitToMainMenu`,
  `TSlider`, `SetPlayerChoices`, ...); 33 game natives; 35 game window types (*boot* 2.5). Many natives exist only while
  their window lives: the window's constructor binds them, its destructor sets them to nil (*boot* 2.1).
- **`DoTransition`** is Lua text the transition manager defines; it yields until `Lua_DoTransition` resumes the coroutine
  with a boolean (*ui* 3.1). Nearly every dialog closes with `PostMessage(kTintOff); DoTransition(kBounceBack); CloseWindow()`.
- **Window close and pause** (*boot* 1.2): a close request runs `scripts/quitverify.lua` unless the quitting flag is set;
  Escape or focus loss while `InsideGame` opens `scripts/pause.lua`.
- **State the Lua reads**: `GetCurrentGameMode`, `IsGameLoaded`, `AreInsideGame`, managed values (`GetManagedInt`,
  `SetManagedValue`), level progress (`GetHighestComplete`, `GetGoalScore`, ...), `GetEndlevelText`. The level, its
  customers, machines, scoring, tutorials, plot, quests, upgrades and the world map are all native.

## 2. Fit with the engine

### 2.1 Reused as is

`AssetFS` and the pfp container, `Xml` (with its ZXML reader), the Lua 5.0 host and bytecode loading, `lua/prelude.lua`, the
window system (`Window`, `ModalWindow`, `ModalClock`, window animations, message queue, the built-in `Bitmap`, `Text`,
`Button`, `TextEdit`, `Slider`, `Window`), `.mvec` fonts and text layout, `TextureCache` (mask as alpha), `SheetCache`
(the `file` -> `.xml`/`.anm` fallback and the image/mask overrides *sprites* 1.2 asks for), `Renderer` (`drawGeometry`,
`Blend::Additive`, clip stack, render targets), `UserStore` users and prefs, `HiscoreStore`, the script driver and
`--seed`.

### 2.2 Engine extensions (generic; no GREATCC names in engine directories)

| File | Change | For |
|---|---|---|
| `gfx/AnmSheet.cpp` | branch on the header's second u32: 1 = today's layout; 2 = IMGN 4 u32, frames of 13 words (`..., scale, xform[6]`). For v2 the IMGN words are not a registration point (X11). Store per-frame `scale` and `xform` in `SheetFrame` (applied per X9) | all 250 GREATCC `.anm` |
| `sdk/SpriteScript.{h,cpp}` | the owner becomes an interface `ScriptTarget` (frame count, set frame, alpha, layer, die, pause) that `sdk::Sprite` implements; `addNative(name, fn)` before `play` for extra globals; `stop()` and `runChunk(text)` (a chunk run in the script state, for `SetDelay`) | VSprite scripts with `getdelay`, `playsound`, `unlock`, `drop` |
| `ui/Style.h`, `script/LuaStyle` | `forEachEntry(key, table view or text)`: the string-keyed entries of a table, for tables whose keys name things (`children`, `assets`, `objects`) | the `VObject` loaders |
| `script/ScriptContext.h` | `Rng::next32()`: one raw word of the game stream | every `Rand32 % n` of *shop* 10 |
| `script/ScriptHost` | a `Suspension` handle (registry ref to the yielding coroutine) and `resume(handle, bool)` | `Lua_DoTransition` |
| `ui/WindowManager` | an event filter that sees input first and may consume it; a consuming message hook for given message types; an overlay drawer called after the modal stack | `RTransitionManager` (*ui* 3.1, 3.3) |
| `ui/widgets/InputWidgets` | `TextEditWindow` pre-edit hook `(type, char, accepted)` called with the text before the key | `RTextEditDisable` (*ui* 5.3) |
| `audio/Audio` | voices with handles: `playVoice(path, group, loop)`, `pauseVoice`, `setVoiceGain`, `killVoice`, `voiceDone` | `VSoundManager` (section 3.6) |
| `script/GameModule.h` | profile fields `stringsPath` (default today's), `titleString` (title from the string table), cursor hotspot | boot |
| `tools/gen_stubs.py` | read `docs/re/api-greatcc.tsv` (`=`, `@native`, `@window`, `-`) into `games/greatcc/generated/Stubs_greatcc.inc` | stubs and placeholders |
| `tools/check_layering.py` | `games/greatcc` includes no other module (only `common`, `sdk`, engine) | layering |
| `app/Finder.{h,cpp}`, `app/App.cpp` | `GameLibrary::fixBugs` read from and written back to `games.cfg` (`fixbugs = 0\|1`); the App puts it into `Config` as `fixbugs` below any `--config` override | the original-bug switch (section 7) |

### 2.3 New: the sprite layer

**GREATCC's `VSprite` gets its own scene in the module; it does not sit on `sdk::Sprite`.** `sdk::Sprite` is a unique-owner
tree with origin/scale/rotation specs, hflip only, no effects, no listeners and an update model the three Chocolatier games'
goldens pin. `VSprite` needs a 3x3 matrix with rotation about a point and vflip, blend modes, effect pre/post-render and
pre/post-draw hooks that nest by id, listener sets, a per-sprite elapsed clock clamped at 3000 ms, depth+y sorting of the
top level, the mouse event machine with capture, `leaf` redirection, lockdown and an exclusive set, anchors re-applied every
update, and `Play`/`Stop`/loop/`OnAnimComplete` semantics (*sprites* 2). Bending `sdk::Sprite` to that would put every
Chocolatier screen at risk for no shared benefit. What is shared: `SheetCache`/`AnmSheet` for sheets, `TextureCache`, the
`Renderer`, and `SpriteScript` through the `ScriptTarget` interface, so the anim-script host (coroutines, `frames`, `delay`,
`random`, `WaitLuaNextFrame`, the instruction limit) is one implementation. `VSprite::newScript` adds `getdelay()` and
`playsound(file, loop)`; `VPress`/`VInfuse::init` add `unlock()`/`drop()` to the named children's scripts (*sprites* 1.3,
*machines* 5.3/5.4). `SetDelay` runs `delay(ms);` in the script state, as the original.

## 3. Module architecture

### 3.1 Layout

```
src/games/greatcc/
  module.cmake  GreatCCModule.{h,cpp}  GreatCCWorld.{h,cpp}  generated/Stubs_greatcc.inc
  core/    Settings, Progress (persistence), ValueManager, LevelManager, Compare (CSUtil), CustomerLogic (moods, patience, payout, combo
           chain), UpgradeManager (VUpgradeManager + VUpgrade: the location's upgrades, their states, the level grant, purchases,
           the preview; it reaches the scene through `UpgradeSprites` and `UpgradeSpider`), AchievementManager, QuestManager, Chocolate model (ChocolateSpec, ChocolatePools, OrderInfo), DisplayInfluence (the display
           case's option sets), GameStateStore (TGameState's strings), ZoneGraph
  natives/ SettingsNatives, ValueNatives, LevelNatives, AchievementNatives, SoundNatives, TransitionNatives,
           ScopedNatives (bind in a window's ctor, nil in its dtor)
  ui/      Transitions (manager + overlays), RWindow, RBounceWindow, RShrinkText, RDisableButton, RTextEditDisable,
           RScrollable, RActiveTextLayer, RTimer, KSplit9Image, KDial, KSweepDial, BLayeredWindow, VTintWindow,
           TMainMenu, TChoosePlayer, TOptions, TCredits, HiscoreWindow, VHelpWindow, VUpsellWindow (inert)
  scene/   Mat3, StyleChain (VLuaStyle's layered lookup), VSprite, SpriteManager, VObject (+ VAsset), Effects, TextSprite,
           TiledObject, Digit, VSpriteWindow (+ VObjectWindow, VRegister), AnimScriptNatives
  shop/    ShopWindow, ShopHost, ShopTick, VSuspendable, VCharacter, VPlayer, VCarriable, VChocolate (+ layer and vendor), VCustomer,
           CustomerVendor, ThoughtBubble, VStation, VMachine (+ VMachineButton) +
           Mixer/Ingredient/Press/Infuse/Topping/SauceDrink/Delivery/Zeppelin, Disposal, Display, Conveyor,
           VShopHud, FloatingTexts
  upgrade/ VPrelevelWindow, VUpgradeList (+ UpgradeSelect, the preview glow), VUpgradeTab
  meta/    TutorialManager (+ Tutorial, Step, Spider, TutorialWindow), PlotManager + VPlotWindow, VQuestWindow,
           VWorldMap + PushPin, VRewardNotify, VRewardsWindow
  audio/   GreatCCSound (VSoundManager)
```

`core/` has no rendering and no Lua: it is unit-tested directly. Shop and meta classes that are sprites in the original
stay sprites here (the per-frame update order is behaviour), but each delegates its arithmetic to a `core/` type
(`VChocolate` holds a `ChocolateSpec`; `VCustomer`'s patience and mood are a `CustomerState` struct; zone search is
`ZoneGraph`).

### 3.2 Ownership and lifetimes

- **`GreatCCModule`**: stateless static, as the other modules.
- **`GreatCCWorld : GameState`** (made by `attach`, owned by `ScriptContext::gameState`) owns what the original keeps as
  singletons: `Settings` (mode, difficulty, insideGame, gameLoaded, quitting), `Progress`, `ValueManager`, `LevelManager`,
  `UpgradeManager`, `AchievementManager`, `TutorialManager` (model), `QuestManager`, `PlotManager` (model),
  `TransitionManager`, `GreatCCSound`. It dies **before** the window tree (engine-and-games 5), so:
- **Rule: nothing in the window or sprite tree stores a pointer into `GreatCCWorld`.** Windows fetch `greatcc::worldOf(ctx)` per
  call (null-safe, as `game::worldOf`). The `ShopWindow` builds a `ShopEnv` (references to the managers, the RNG, the
  modal-clock time) at the top of each `onAnimate`/event and passes it down to every sprite call; sprites never keep it.
  Destructors touch no manager (`~ShopWindow` clears `gameLoaded` through `worldOf`, which is null at shutdown).
- **Sprites**: a `VSpriteWindow` owns its `SpriteManager`, which owns the top-level sprites (`unique_ptr`); each sprite owns
  its children. Moving a carriable (player arm, machine, display, belt) is `release` + `adopt`. Every non-owning reference
  (hand slots, a station's customer, the hover/pressed target, the exclusive set, tutorial lookups, `BZeppelin`'s machine)
  is a `SpriteHandle` (an id) resolved through the window's `SpriteRegistry`, which is declared before the manager and so
  outlives every sprite; a dead id resolves to null. Sprites not yet in the scene (customer prototypes, pending customers,
  chocolate prototypes) are owned by `CustomerVendor`, `ShopWindow` and `Vendor` members declared after the registry.
- **Effects** are `shared_ptr<Effect>` because the original shares one effect between sprites (the tutorial dim is a global
  effect); an effect holds no pointer to a sprite: hooks receive the sprite. A listener holds a `weak_ptr` to its effect or
  a handle to its tutorial.
- **Lua**: per-window natives hold the window's `WindowId` as an upvalue and resolve it per call; `ScopedNatives` (a window
  member) sets each name to nil in the window destructor. The Lua state outlives the window tree (script-host.md 2);
  slice 1 asserts it.
- **Transitions**: `TransitionManager` owns the current overlay, the FIFO and the retired list; an overlay holds the
  `WindowId`s it acts on and an optional `Suspension`. Construction side effects happen in the overlay constructors
  (*ui* 3.3).

### 3.3 Registration and detection

- `claims`: `settingsGame == "chocolateshop"`. Without `settings.xml`, the files `scripts/mainloop.lua` +
  `locations/defines.lua` + `values.xml`. `Registry` lists it after choc1 behind `#if CHOCO_GAME_GREATCC`.
- Profile: id `greatcc`, user dir `GreatChocolateChase`, `mainloop` `scripts/mainloop.lua`, `stringsPath`
  `strings/en/strings.xml`, `titleString` `app_title`, cursor `cursor/chocolate_cursor_2` hotspot (0,0), no window quirks
  (SDK 4.0 = Chocolatier 3's generation), `volumeDefaults` 750/750, close global `AreQuitting`, close request
  `if AreQuitting() then PostMessage(CreateNamedMessage(kQuitNow, "quit")) else table.insert(gCommandQueue, assert(loadfile("scripts/quitverify.lua"))) end`
  (the loaded chunk runs as Lua on Main's coroutine, so its `DisplayDialog` can yield; a second close request while the
  dialog is up quits), no exit save (progress is committed as it changes).
- `onEscapeKey`: if inside a game, `InsideGame(false)` and queue `DoModal('scripts/pause.lua')`. `GreatCCWorld::setForeground(false)`
  does the same, then the audio focus pause.
- Natives: `registerStubs` from `Stubs_greatcc.inc` (every row, placeholders for the 12 dead window types of *ui* 0.2), then
  `registerNatives` binds the `@native` rows in the original's order (TSettings 24, then values, levels, achievements,
  tutorials/quests, transitions, sound; *boot* 2.1); `TSlider` is a second name of `Slider`; `registerGlobals` runs the
  `DoTransition` text verbatim. Window-scoped natives (`ShopWindow`, `VWorldMap`, `TChoosePlayer`, `TMainMenu`, `TOptions`,
  `HiscoreWindow`, `VHelpWindow`, `VPrelevelWindow`, `VRewardsWindow`, `VUpsellWindow`) use `ScopedNatives`.
  `NeedPrelevel`/`NeedPlot`/`GetPlotMessage`/`Query*`/`SetTopModalOnly` are logged stubs (no Lua caller).
- GREATCC does not inherit choc3: no Chocolatier native or stub is visible to its Lua.

### 3.4 Persistence (the engine's own)

Users, names and the per-user `soundlevel`/`musiclevel`/`soundoff` stay in `UserStore`. `curuser` is `UserStore`'s
current user; `gamemode` and `fullscreen` are **global** prefs (*boot* 5.2), and the hiscore mode name is passed explicitly
instead of `UserStore::hiscoreMode()`. Everything else is `Progress`: one file per user, `greatcc-progress.txt`, `key=value`
lines (`UserStore::escape`), written atomically through `UserStore::writeFile`. Keys are names, never indices:

| Key | Holds |
|---|---|
| `value.<name>` | saved `values.xml` values, exact double |
| `level.<n>` | `status,best` for levels 1..60 |
| `cash.<location>` | the location's spendable cash |
| `upgrade.<location>.<name>` | 1 when owned (state 3) |
| `tutorials.show`, `tutorial.<name>` | the Options flag; a tutorial's state |
| `plot.<location>.<item>`, `quest.<name>` | plot done/value; quest status 0/1/2 |
| `achievement.<name>` | 1 when acquired |

Changes mark `Progress` dirty; `GreatCCWorld::tick` writes once per frame when dirty (the original flushes on every set, so
at most one frame of difference). `SetCurrentUser(i)` reloads every manager from the new user's file;
`LoadStateForCurrentUser` also runs at boot with no user (defaults). Not reproduced: `userversionnumber` and
`DeleteIncompatableUsers`, int truncation of saved values, slot numbering by Lua iteration order, keeping the previous
user's state after `SetCurrentUser(-1)`. The shared tutorial state is fix F4 (section 7): with fixes off every tutorial
reads and writes the one key `tutorial.shared`.

### 3.5 RNG

Every gameplay draw is `rng.next32()` with the original reduction (`% n`, `& 1`, `* 2^-32`; *shop* 10), in the order the
facts list. `random(n)` = `next32() % n + 1` (n = 0: returns 1 and logs, instead of dividing by zero). Sprite scripts keep
`Rng`'s side stream. `--seed` makes runs reproducible; the original sequence is not reproduced (it is unseeded).

### 3.6 Audio

`GreatCCSound` (owned by `GreatCCWorld`, ticked from `afterAudio`) is `VSoundManager` over `audio::Audio` (*audio* 2-3): an ACTIVE
list of `{voice, group, done}`, CURMUSIC, the VO queue and CURVO, reaped every frame in *audio* 2's order. Groups 1000
(SFX, VO) and 0 (UI buttons) follow the SFX level, 1001 the music level; mute sets the master only.

| Native | Mapping |
|---|---|
| `PlaySFX`, sprite `playsound` | `playVoice(path, sfx, loop)`; a path without `.ogg` or a missing file is silent (logged once); no dedupe |
| `PlayMusic(path, loop, fade)` | same name as CURMUSIC: nothing; load failure: old track keeps playing; else crossfade 1000 ms through the voice gains |
| `PlayVO`, `PurgeVOQueue` | the queue of *audio* 3 (no caller ships) |
| `PurgeSounds` | drops ACTIVE references only (*audio* 3) |
| `PauseSounds(p, g)` | pauses the ACTIVE voices of group `g`; music keeps playing |
| focus loss | `Audio::setForeground(false)` (everything, as `AppActivate`) |

`assets.xml` sounds are decoded at level load. The level/slider-to-gain curve is squared (decision D1).

## 4. Order of work per frame

Within the engine's frame (window-system.md), GREATCC adds:

1. Input: the transition manager's filter first (consumes mouse and key events while an overlay is current or pending),
   then the window tree. Sprite windows forward mouse events to their `SpriteManager` (*sprites* 2.5).
2. Lua host tick (command queue, coroutines).
3. Window animations on modal clocks: `ShopWindow::onAnimate` every 16 ms in *shop* 2.3's order (start gate and lockdown;
   `UpdateCustomers`; `UpdateHud`; pending prelevel/plot; end detection; after the 2000 ms delay `DoEndLevel` and stop;
   else `SpriteManager::updateObjects`: hit test if dirty, removals, `update` of each top-level sprite (children
   recursively, anchors re-applied, effects collected, listeners cleaned), `updateEffects`, hover timer). Other sprite
   windows (map, tutorial, plot) run `updateObjects` once per drawn frame. Sprite scripts tick with the same modal time.
4. `TransitionManager::monitor` every 16 ms: destroy retired overlays, finish the current one (queue its Lua string, resume
   its coroutine with `true`), start the next.
5. `GreatCCSound::update` (from `afterAudio`): start the next VO, reap done voices, advance fades. `Progress` commit.
6. Draw: windows; sprite managers sort and draw inside their window's `draw`; overlays last.

## 5. Cross-check of the RE files

Each item: the conflict or gap, the slice it blocks, the resolution. "Re-analyst" items are one targeted question each.

| # | Finding | Blocks | Resolution |
|---|---|---|---|
| X1 | Station click: *shop* 6.2 and *machines* 4.3 say `VStation::OnClick` calls `VPlayer::TravelTo`; *sprites* 5.2 says it calls `QueueMovePoint` (clicks queue). `OnClick` (0x4ff32) calls player vtable +0x94 | 10 | Resolved: slot +0x94 is `VSprite::GetPosition` (used to find the nearest player point); the call is slot +0x13c = `VCharacter::QueueMovePoint`. A click appends to the player's move queue, never replaces or cancels the current trip, and the queue is never cleared; the pop is `VCharacter::Update` (idle) or `AtDestination` (chain) -> `GotoNextPoint` -> `VPlayer::TravelTo`. `QueueMovePoint` is called by `VStation::OnClick` only. During a tutorial the queue de-duplicates (same point as the current destination or the queue's last entry is ignored). *sprites* 5.1/5.2 state it; *shop* 6.2 and *machines* 4.3 agree. |
| X2 | Soft pause: *sprites* 2.6 "nothing calls `SoftPause`"; *shop* 2.2/2.3 call it at level start (ShopWindow.c:3588 calls manager vfunc +0x30) and *meta* 1.4 per tutorial step (`pause=true`). Which updates it gates (customers are skipped; player walking during a paused step with a `kArrival` trigger?) is open | 9, 14 | Resolved: vfunc +0x30 = `VSpriteManager::SoftPause` (set at `LevelSetup` for the 1000 ms pre-level phase, cleared at level start, set per tutorial step with `step.pause && tutorial.pause`). Readers: `VCustomer` (patience decay), `VDelivery` (next-order countdown only), `VChocolate` (decay), `VDisplay` (decayed-item removal), `VSuspendable` (shifts suspension start after the pause), `ShopWindow::UpdateCustomers` (no arrivals), `VShopHud::UpdateHud` (level timer stops). `VCharacter`/`VPlayer`, `VMachine` family, `VConveyor`, `BZeppelin`, effects and sprite scripts are not gated. So in a paused step the avatar can walk, machines run, seated customers keep patience, the level timer stops (it also stops in any tutorial step). Table in *sprites* 2.6. |
| X3 | Lockdown scale: *meta* 1.6 assumes `PercentElapsed` in 0..1 (level 1 locked throughout); *ui* 6.3 reads it as `min(100, elapsed/duration*100)` (F), i.e. locked for the first 10 % (3 s) | 14 | Resolved: `OnTaskAnimate` passes `RTimer::PercentElapsed()`, which is `min(100, elapsed/duration*100)` (0..100), compared inclusively with the raw `min`/`max` floats. Level 1 (`min=0 max=10`, 30 s) is locked for the first 10 % = 3 s of counted level time (plus the pre-level phase), not the whole level. Follow *ui* 6.3; *meta* 1.6 and *shop* 2.3 now say so. |
| X4 | Where lockdown acts: *shop* 6.1 "no reader found"; *sprites* 2.5 and *meta* 1.5 cite `VSpriteManager::IsValidSprite` (F) | - | Resolved: `IsValidSprite` |
| X5 | Flag at +0xd9: *shop* 6.1 calls it "clickable"; *sprites* 2.1 has +0xd9 `mouse`, +0xda `clickable` (Click on Up) | 10, 13 | Resolved: both callers write `+0xd9`, which is `mouse` (hit-test enable); `+0xda` `clickable` is set true by the `VStation`/`VMachine` constructors. `AttachCustomer` sets `mouse` true, `DetachCustomer` sets it to `machineCount > 0`. Implement with *sprites*' names (*shop* 6.1 updated). |
| X6 | `GetVolume` of an unset group: *audio* 4 says master (Chocolatier 3's Mac decompile has the same shape); `docs/re/audio.md` 4 says 0.0. GREATCC always sets groups 0/1000/1001, so GREATCC is unaffected | - | Flag to the Chocolatier 3 audio owner; not a GREATCC blocker |
| X7 | `playsound` use: *audio* 1/5 say no shipped caller and `macaw.ogg`/`phonograph.ogg` unused; *sprites* 1.3 counts 4 calls. Checked: `anim/iquitos/background_01..03.anm` play `macaw.ogg`, `anim/machines/phono_anim.anm` plays `phonograph.ogg` | - | Resolved: implement `playsound` (group 1000) |
| X8 | `levels/level28.xml`: two `<order>`s are never closed, the yachtsman's second (customer index 27) and the last customer's second (index 29); pugixml stops with an end-element mismatch, so `Xml::parse` fails (*shop* 9) | 15 (level 28, fixes off) | Fixes on: F1 repairs it (level 28 has its 30 customers). Fixes off: the tree pugixml had built when it stopped is used (28 of the 30 customers; the yachtsman in progress keeps both orders but its `station` is read as a child of the open order and lost) and the load logs "does not parse". The original's parser is hand-rolled (`TXmlNode::Parse`, `ParseTag`, `pfGetToken`; `re/greatcc/decomp/TXmlNode.c`): `ParseTag` does not compare end-tag names, so any `</x>` ends the innermost open node. At each unclosed order the `<station>` is swallowed as the order's child and the customer's `</customer>` closes the order, which leaves the customer node open; the customers after the yachtsman therefore nest under it, and nodes still open at the end of input are left. `VLevel::LoadLevel` walks the direct children of the root only (F), so the nested customers are never visited: 28 top-level customers, the yachtsman without a `station`, customers 28 and 29 lost (I: the `TGetChar` flags at +4, +0x10 and +0x11 were not traced, so the count of 28 is inferred from the tree shape) |
| X9 | `.anm` frame `scale`/`xform`: only `machines/sauce/sauce_base.anm` (scale 0.5, identity xform), `ui/upsell/anim_puff.anm` (scale 0.75/0.5/0.25, identity xform) and `machines/ingredient/ingred_base.anm` (translate -10,-17) carry real values; how they are applied is unknown (*sprites* U) | 12, 15 | Resolved (`TAnimatedTexture::DrawSprite(spec)` 0x9898c): per-frame `scale` multiplies the draw **alpha** (spec +0x3c -> vertex alpha), not geometry (sauce_base frame 27 = 0.5 = half opacity). `xform` words are a row-vector 3x3 `(x0 x1 0 / x2 x3 0 / x4 x5 1)`; the draw matrix becomes `xform * spriteMatrix`, i.e. the xform acts on the frame quad relative to the registration point first, then the sprite's own transform. Sentinel -1e7 in word 0 = none. Only ingred_base frames 3-5 (pure translations (-10,-17), (-16,-27), (-15,-14)) matter; sauce_base xforms are identity. Bounding boxes and hit tests ignore both. *sprites* 1.5. |
| X10 | `.anm` mask polarity (grey palette PNG; white opaque assumed) | 7 | Decide by eye: a walk frame and a machine frame headless against an original screenshot |
| X11 | IMGN words: *sprites* 1.1 says `(a, b, 0, 0)` are unused source-sheet sizes; *machines* 14 calls them "reg x, reg y, w, h"; our `AnmSheet` uses the first two as a sheet-level registration point (correct for v1 sheets) | 6 | Resolved: the four IMGN words are write-only (parsers and constructor only; nothing in the load or draw path reads them). Registration is per frame (`regX/regY`). v2 sheets get no sheet-level registration; do not use the words. *sprites* 1.1. |
| X12 | `curuser`/`gamemode`: global prefs (*boot* 5.2) but `UserStore::hiscoreMode()` reads a per-user `gamemode` | 4 | Resolved in section 3.4 |
| X13 | Prelevel then plot: *meta* 0 says shop then diary (F); *shop* 2.1 says showing prelevel does not set the plot flag, and who calls `ShopWindow::UpgradeComplete` is unknown (*shop* 12) | 16, 17 | The Lua global `Close()` is `VPrelevelWindow::Close` (bound by the window's constructor): it ends the previews and calls `ShopWindow::UpgradeComplete`, which sets the plot flag (+0x1dd) when the vendor exists, so the diary follows the shop; showing the shop sets nothing (*meta* 5.1, *machines* 7.3) |
| X14 | Does the diary show on every entry of a level (item re-inits each `SetCurrentLevel`, *meta* 2.3, I)? | 17 | Resolved: yes, every entry. `VLevel::NeedPlot` is only "the level's plot list is non-empty" and `LaunchPlotDialog` only tests that the active list is non-empty; neither reads completion. `ShopWindow::LoadPlotItems` (every level setup) clears the items, rebuilds them from the level's `plot` table and calls `LoadCurrentUser`, and the save (`UpdateCurrentUser`) has no caller in the plot, quest and shop code, so no page is ever stored as done. A replayed level shows its diary again. *meta* 2.3. |
| X15 | Tutorial state: every tutorial reads and writes byte 0 of one block (*meta* 1.3, F) | - | Fix F4 |
| X16 | Plot, quest and achievement slots follow Lua table or file order (*meta* 2.1, 3.1) | - | Not reproduced: keyed by name (section 3.4) |
| X17 | Escape to pause: *shop* 2.1/12 unknown; *boot* 1.2 (F): the main loop handles Escape while inside a game | - | Resolved: `onEscapeKey` |
| X18 | `VShopHud` `SetFlags(0)` on `extension` (story) and `level_name` (endless): *shop* 8 unknown effect; *ui* 0.4 (F) says it hides. Hiding `extension` hides the HUD Upgrade button in story, which *shop* 2.1 step 6 describes as usable | 9 | Resolved: `TWindow::SetFlags(0)` clears the 0x100 visible bit, which `TWindow::DoDraw` and the child hit test require, so the window and its descendants are hidden and unclickable. Story `LevelInit` hides `extension` (and with it the HUD Upgrade button, which therefore exists only in endless mode); endless `PostChildrenInit` hides `level_name`. Nothing re-shows them. *shop* 2.1 step 6 and 8 updated. |
| X19 | Achievements at level end: whether `ShopWindow` passes `show=true` (*boot* 6) or only `endlevel.lua`'s `ShowNewAchievements(0)` shows them | 17 | Resolved: `ShopWindow` calls `CheckAllAchievements` at all four sites (end of level, adventure/endless/endgame) with `show = false` and close condition `0`, so the call only marks achievements acquired; the notification is opened only by `endlevel.lua`'s `ShowNewAchievements(0)`. *meta* 6. |
| X20 | `credits/credits.txt` on Windows (*meta* 7 unknown) | - | Resolved: it is a loose file in the Windows install |
| X21 | `RTransitionManager::HandleEvent`'s hook point and whether it stops the event (*ui* 3.3) | 2 | Decide: the filter consumes mouse and key events while a transition is current or queued |
| X22 | `RWindow` re-parent branch ownership (*ui* 1) | - | Not built: our windows are always created inside a modal; revisit if a placeholder shows up |
| X23 | `VTiledObject::Draw` not decoded (*sprites* 4.5); used by the tutorial panel, the map score box and reward notifications | 8, 14, 17 | Resolved: `VTiledObject::Draw` is a stretching nine-slice, never tiled: `x1/x2/y1/y2` = left/right/top/bottom insets, corners at native size, edges stretched along one axis, centre stretched on both, size `w` x `h` changeable after load. Same semantics as `KSplit9Image`'s nine pieces (see *sprites* 4.5 for the exact source rects, centres and scales); use the same nine-slice code with a size setter. |
| X24 | `VPushPin::SetOpen` move arguments and `VPushPin::Update` (hover, score box) not decoded | 8 | Resolved: `SetOpen` gives every **text sprite** of the pin (`name`, `loc`) a `VSpriteMove`: closing = (stored position, 200 ms, accel 0, delay 0); opening = (current position + `(movex,movey)`, 200 ms, accel 0, delay 450 ms); closing adds a `VHoverGlow(0.15, false, true, true)` on the pin, opening stops it and closes the other pins. The art, the box and the globes do not move. `Update` is only the DoAnim/ReverseAnim/globe-cascade state machine (24/25 ms steps, 300 ms alpha, glows); hover and the score box live in `VPinListener` -> `SetScoreInfo` (200 ms fade-in; 500 ms fade-out after 500 ms). *meta* 4.2. |
| X25 | `TChoosePlayer`, `TOptions`, `TCredits`, `THiscore` internals only partly decoded (*meta* 7) | 3, 19 | Resolved: `THiscore` is decoded (*meta* 7.1): ten rows of name, score, rank and p1 icon, the `gamemode` heading and two arrows that step the game mode through the four tables, one local table per mode (the mode's `scores_gamemodeN` string in an outline), and the online parts (`view`, categories, `submit`, the right panels) that `hiscorelocal` switches off. It is not Chocolatier 3's `HiscoreWindow`: no info column, the score is the plain number, the game mode is a pref of the whole game. The other three are built from their natives and keys and were checked by eye |
| X26 | `VCustomer` vtable +0x188 (conveyor hand-off) and its refusal path (*machines* 5.9) | 15 | Resolved: +0x188 is `ComplimentaryService(shared_ptr<VObject>)` (+0x184 `ServeCustomer`, +0x18c `GenerateOrders`); a refusal leaves the item riding with the zone recorded (*machines* 5.9) |
| X27 | `VChocolate::AddLayer`'s bucket-removal calls and `GetChocolate`'s extra-layer branch are partly inferred (*machines* 2.2, 2.6) | 11, 13 | Resolved. `AddLayer` (disassembly): the filling layer is `AddLayer(layer.barType, 1, ingredientId)`, the infusion bar's shape `AddLayer(layer.barType, 2, mixOption)` (*machines* 2.2). `GetChocolate` (decompile and disassembly): *machines* 2.6 step 5 gives the whole branch (the leftover budget buys a random filling, with a coin when one layer is left, and a random topping, over whatever the slots hold; a pinned order gets nothing extra) and `FindOrderWithLayer` is *machines* 4.4 (the first counter order that fits, else `c0`, else `c1`). |
| X28 | `TravelTo` to the point the player stands on: does `OnArrival` fire (pressing a machine button while standing at it)? | 10 | Resolved: yes. The path for `start == dest` is `[P, P]`; `SetDestination` finds the sprite exactly on `P` and calls `AtNode` immediately, twice, ending in `AtDestination` -> `VStation::OnArrival` within the same call, with no walk and no delay (it happens in the `VCharacter::Update` after the click, which pops the queued point). Pressing a machine button while standing at that machine acts at once. *sprites* 5.1. |
| X29 | Walk cycle after arrival keeps running until the 500 ms turn-around (*sprites* 5.2, I) | 10 | Resolved by tracing the code: an arrival at a station's point sets its facing in the same update, which stops the cycle at once; only a trip that ends at a point no station owns, or with queued points left, keeps the cycle going until the heading changes or the 500 ms turn-around sets front (*sprites* 5.2). Seen by eye on the avatar in level 1. |
| X30 | `VWipeEffect` direction pairing, `VSpriteMove` sign, `VGlowEffect` with `accel != 0` (*sprites* 3.2) | 7 | Decide by eye on the mixer wipe and the map pins |
| X31 | Windows audio gain curve (squared on the Mac build, *audio* 4) | - | Resolved by decision D1: squared on both |

## 6. Slices

Each slice is one engine-coder pass plus review, ends green on targeted tests, and touches only what it names. Scenario
tests are `tests/check_greatcc_*.py`, added to CTest only when `CHOCO_GREATCC_ASSETS` (a Windows install's `assets/`, with
`assets.pfp`) exists; the boot slice also runs against `CHOCO_GREATCC_MAC_ASSETS` (the Mac bundle's `assets/`). Every scenario
runs `--headless --userdir <build scratch> --seed 1` and asserts no `[lua-error]` and an unimplemented set equal to its
allow-list. Unit tests go in `tests/test_greatcc_*.cpp` with synthetic fixtures under `tests/assets/greatcc/`. Fixes of section 7
land in the slice that builds the code they touch: the switch itself in slice 1, F3 in 7, F2 in 9, F4 in 14, F1 in 15.

| # | Slice | Implements | Engine files touched / new files | Tests | Risks | Size |
|---|---|---|---|---|---|---|
| 1 | **Module skeleton, boot to main menu** | *boot* 1, 2.1, 3.1, 1.5; section 3.3 | `Registry`, `CMakeLists.txt` (`CHOCO_GAME_GREATCC`, `CHOCO_GREATCC_ASSETS`, `CHOCO_GREATCC_MAC_ASSETS`), `GameModule.h` (3 profile fields), `gen_stubs.py`, `check_layering.py`; new `GreatCCModule`, `GreatCCWorld` (empty), `Stubs_greatcc.inc` | unit: detection on fake trees; every `api-greatcc.tsv` name is bound after boot; `TSlider` = `Slider`; a scoped native is nil after its window dies; `fixbugs` from `games.cfg` survives a finder rewrite and `--config fixbugs=0` overrides it. Scenario `greatcc_boot` (both asset sets): splashes, `expect-window scripts/mainmenu.lua`, screenshot not uniform | pfp variant or Mac `splash/publisher` without a picture | M |
| 2 | **Transitions and dialog motion** | *ui* 2, 3, 7 | `ScriptHost` (`Suspension`), `WindowManager` (filter, consuming hook, overlay drawer); new `ui/Transitions`, `RBounceWindow`, `VTintWindow`, `RWindow`, `TransitionNatives` | unit: queue/retire timing, resume with `true`, unknown id returns false, `RBounceOff` side and modes, bounce curve table, tint ramp and `TintOff` toggle, input blocked while active. Scenario `greatcc_dialogs`: Quit opens `yesno.lua` which slides in, No closes it, main menu remains | coroutine resume from outside Lua | M |
| 3 | **Widget library and menu screens** | *ui* 4-9; *meta* 7 | `InputWidgets` (pre-edit hook); new `RShrinkText`, `RDisableButton`, `RTextEditDisable`, `KSplit9Image`, `BLayeredWindow`, `RScrollable`, `TMainMenu`, `TChoosePlayer`, `TOptions`, `TCredits`, `VHelpWindow`, `VUpsellWindow` (inert) | unit: shrink loop strict `<=`, disable rule truth table, nine-slice rects. Scenario `greatcc_menus`: first run enters a name, each main-menu button opens its screen and returns; help pages step; options checkboxes toggle | X25 layouts by eye | L |
| 4 | **Settings, users, progress** | *boot* 2.2, 4.2, 4.5, 5; section 3.4 | `UserNatives` hooks where GREATCC differs (or GREATCC bindings over them); new `core/Settings`, `Progress`, `ValueManager`, `Compare`, `LevelManager`, `AchievementManager` (model), their natives | unit: `GetValue` formula (reverse modifier order, base-multiplied, clamp max then min), `byval`, lazy creation, `CompareValue` incl. `lessthan+greaterthan+equal`, `LevelComplete`/`delta`/`SetHighestComplete`, achievement check order, progress round trip and reload on user switch. Scenario `greatcc_users`: two users, switch, rename, delete; gamemode global | none beyond facts | M |
| 5 | **Audio** | *audio* 1-8; section 3.6 | `audio/Audio` (voices); new `audio/GreatCCSound`, `SoundNatives` | unit: reap order, same-track shortcut, failed load keeps the old track, crossfade gains, squared gain curve, group pause leaves music, mute master only, volumes from prefs. Scenario `greatcc_audio`: menu logs `theme.ogg`; options sliders change levels; mute | none beyond facts | M |
| 6 | **`.anm` v2 and anim-script natives** | *sprites* 1 | `AnmSheet`, `SpriteSheet`, `SpriteScript` (`ScriptTarget`, `addNative`) | unit: synthetic v1 and v2 fixtures, anchors, mask name, SCRP; `sdk::Sprite` behaviour unchanged (existing tests). Scenario `greatcc_anm`: every `.anm` in the install parses (250), each script compiles | X9, X11 | S |
| 7 | **Scene core** | *sprites* 2.1-2.4, 2.6-2.7, 3, 4, 7 | new `scene/*` | unit: matrix (rotation about a point, flips), layer and depth+y order, `Play`/`Stop`/loop restart/`OnAnimComplete`, delayed play, elapsed clamp, anchors with flips, `VObject` keys incl. `scale[1]` only and alphabetical first asset, each effect's curve, `VDigit` roll, `VRegister` contexts | X10, X30 by eye | L |
| 8 | **Scene input and the world map** | *sprites* 2.5, 4.6; *meta* 4 | mouse dispatch in `SpriteManager`/`VSprite` (hit test, capture, lockdown, exclusive set), `VObject::CopyTo`; new `VButton`, `PushPin`, `VWorldMap`; `choosedifficulty.lua`/`intro.lua` paths | unit: hit capture, Enter/Leave/Move/Down/Up/Click, `leaf` redirect, lockdown + exclusive set, pixel hit threshold 1. Scenario `greatcc_worldmap`: new user, Adventure, intro, Iquitos open with globe 1 current, globe 2 dead; click globe 1 swaps to `scripts/game.lua` | X23, X24 | L |
| 9 | **Shop shell: an empty level** | *shop* 1, 2.1-2.4, 2.7, 2.8, 8; *ui* 6.2, 6.3, 9 | new `ShopWindow`, `ShopTick`, `VBackground`, `VStation` (static), `VShopHud`, `KDial`, `KSweepDial`, `RTimer`, `RActiveTextLayer`, level/goals loaders, pause/escape/focus, `QuitToMainMenu` | unit: level XML parse (percent starts, order attributes, `kTruffler` -> unspecified), end detection, 2000 ms delay, `GetEndlevelText`. Scenario `greatcc_level_shell`: level 1 loads, timer 0:30 counts down (customers off), end dialog `endlevel_fail`, Restart, Map, Escape pause | X2, X18 | L |
| 10 | **Player and zones** | *sprites* 5, 6; *shop* 6.2 | new `ZoneGraph`, `VCharacter`, `VPlayer`, station click/arrival dispatch | unit: Dijkstra quirks (front insertion, int cost, tie order, duplicated destination, unreachable straight walk), facing thresholds, turn rate per update, perspective scale, hand slots. Scenario `greatcc_walk`: clicks on stations move the avatar along the expected nodes (dump) | X1, X5, X28, X29 | M |
| 11 | **Chocolates, mixer, press** | *machines* 2, 3, 4, 5.1, 5.3 | new `core/Chocolate*`, `VChocolate`, `Vendor`, `VMachine` + sequencer, `VMachineButton`, `VMixer`, `VPress` (`unlock`/`drop`) | unit: `CanAddLayer`, `Compare`, `CompareWithLayer`, `GetCost`, pools, sequence timings (mixer 2880/1840 ms, press 1230/450, steam), option lifetime, the suspended station, reload mid-process. The upgrades the machines read come from `UpgradeManager` (slice 16). Scenario `greatcc_machines`: level 1 with real clicks: dark bucket, press, bar in hand | X27 | L |
| 12 | **Sauce/drink, display, disposal** | *machines* 5.6, 5.8, 5.10; *sprites* 7.2 | new `VSauceDrink`, `VDisplay`, `VDisposal` | unit: cooldown and rescale, refill sequence (930/960/930 ms), display capacity and influence sets (the kept set quirk), disposal counters and game states, reload mid-process. `VStation::Create` makes the disposal and the display, and `VDisplay::Init` shows the case from `kDisplay1` (level 2 in Iquitos), so the avatar's walk round the table is real. Scenario `greatcc_stations`: take a sauce, trash it, the cooldown refill, a reload in the middle of it; level 2 with the table: the walk round it, a bottle and a bar put on it and taken back | X9 | M |
| 13 | **Customers and scoring: level 1 playable** | *shop* 3-5, 7, 8 (floating texts); *machines* 2.6 | new `VCustomer`, `CustomerVendor`, `ThoughtBubble`, money pile, `OrderComplete`, combos, goal popups | unit: state machine, decay through states 1-3, mood bands, serve refusal leaves item, payout sprite count, combo first-event-no-bonus, tip formula, `GetChocolate` with scripted words, `GenerateOrders`. Scenario `greatcc_level1`: tutorials off, scripted play of level 1 to `endlevel_pass`, `current_level` 2, cash saved and present after restart | X27, timing of scripted clicks | XL |
| 14 | **Tutorials and level 1 lockdown** | *meta* 1 | new `TutorialManager`, `Tutorial`, `Step`, `Spider`, `TutorialWindow`; Options checkbox | unit: `ShouldShow`, `clickAny` rule, duration/delay gating, skip collapse, trigger kinds, `HandleMouse` with `kIgnore`. Scenario `greatcc_tutorial1`: fresh user, `tutorial_1` at `tut_cust_1`'s arrival, a click outside the exclusive set does nothing, the steps advance by the listed triggers, skip turns tutorials off | X2, X3, X23 | L |
| 15 | **Remaining machines, decay, all levels load** | *machines* 5.2, 5.4, 5.5, 5.7, 5.9, 2.2 decay; *meta* 4.6 | new `VIngredient`, `VTopping`, `VInfuse`, `VDelivery`, `BZeppelin`, `VConveyor` | unit: conveyor spacing and serve-once, delivery timer and patience, decay once. Scenario `greatcc_machines` (cheat level jumps): levels 3, 12, 14, 51 run with no error; every `levels/levelNN.xml` loads with the expected customer count | X8, X9, X26 | L |
| 16 | **Upgrades and the prelevel shop** | *machines* 7; *meta* 5 | new `core/UpgradeManager`, `VPrelevelWindow`, `VUpgradeList`, `VUpgradeTab`; `VBackground`/`VStation` upgrade states; value modifiers wired | unit: states 0-4, `SetCurrentLevel` auto-grant/unlock order, temp preview, requires, per-location persistence. Scenario `greatcc_prelevel`: level 4, `tutorial_upgrades` runs, buy one, cash lower, owned after restart, the scene shows it | X13 | L |
| 17 | **Plot, quests, achievement windows, endgame** | *meta* 2, 3, 6; *boot* 6 | new `PlotManager`, `VPlotWindow`, `QuestManager`, `VQuestWindow`, `VRewardNotify`, `VRewardsWindow`, `VReward`; `endgame.lua` path | unit: quest compare and rewards (activate, reveal, money), post-level text, notify stacking. Scenario `greatcc_meta`: diary at level 1 start, level 8 quest rewards `kIngredientQ2`, an achievement notification at level end, rewards window | X13, X14, X19, X23 | L |
| 18 | **Endless mode** | *shop* 2.5, 2.6, 5.6; *meta* 1.7 endless tutorials | `ShopTick` endless branch, difficulty, `UpdateEndlessMode`, money decay | unit: spawn schedule, threshold steps, difficulty-scaled values. Scenario `greatcc_endless`: seed 1, no input until five angry customers end the run, `endlevel_endless`, score logged | none beyond facts | M |
| 19 | **Hiscores, cheats, finish** | *meta* 7 hiscores; *shop* 7.4 cheats | new `HiscoreWindow` (local, 4 modes), `ChangeCurrentUserName` re-log, `DoCheatGame`/`DoCheatMap` under `cheatmode`, F10 lines, game finder entry | Scenario `greatcc_scores`; `greatcc_story_smoke`: cheats through the first level of each location, map transitions, endgame and credits | X25 | M |
| 20 | **Debug tools** | *debug* 1-3 | `debug/DebugCommand`, `shop/ShopDebug.cpp` (Ctrl+C console, `DoCommand`, `DoDebug`, `zones_debug` drawing), `ui/VChooseAchievements`, `TMainMenu::MainDebug` and the cheatmode Ctrl+D chord, Ctrl+T / Ctrl+S overlays in `SpriteManager`, the `choctint` map in `GreatCCWorld` | unit `test_greatcc_debug.cpp`: command parsing and every command, zone reload, AcquireAll/release, the window's toggles and messages, overlay flags and text, Ctrl+D gating. Scenario `greatcc_debug`: console in a level, overlays, zones_debug and the Debug button, Choose Achievements, Ctrl+D with and without cheatmode | none beyond facts | M |

Size: 1 S, 7 M, 10 L, 1 XL. Honestly, this is **three to four times the Chocolatier 2 port**. Chocolatier 2 added about
5,400 lines on top of a Chocolatier 3 engine whose port, map, ledger and factories it reused; GREATCC reuses only the SDK layer
and reimplements about 1,500 game methods, so expect 20,000 to 30,000 lines and as many review rounds. Level 1 is
playable after slice 13, with its tutorial after 14.

## 7. Original-bug fixes

One switch, default on: **fixes on** repairs the clear defects below; **off** is strict original behaviour. It is an
engine-level setting (only GREATCC reads it today), spelled `fixbugs`:

- `games.cfg` (the engine library, game-finder.md 3) may hold `fixbugs = 0`; a missing line means on. `GameLibrary`
  keeps the value so the finder's rewrite does not drop it. It is edited by hand; there is no UI.
- The App copies it into `Config` as `fixbugs`; `--config fixbugs=0|1` overrides it, which is how scripted runs choose
  (they never read the machine's library, so they default to on).
- `GreatCCWorld` reads it once at `attach` into `Settings::fixBugs`; each fix is one `if` at its site, named `F<n>` in a
  comment, and logs `[fix] F<n> ...` the first time it changes something.
- Every fix has a test run both ways: the unit test with both flag values, and the scenario that reaches it once with
  `--config fixbugs=0` asserting the original outcome.

Only plain defects with an evident intent are fixed. Fixed by default:

| # | Defect | Fix | Confidence in the intent | Slice |
|---|---|---|---|---|
| F1 | `levels/level28.xml`: two `<order>`s are never closed, the yachtsman's second (customer index 27) and the last customer's second (index 29, `station06`); each swallows the `<station>` that follows (*shop* 9) | close an open `<order>` where the next element is not one of its `<layer>`s, or at another element's end tag, so both customers keep their orders and `station` and every later customer loads | high: every other customer in the 60 files has this shape | 15 |
| F2 | `level26.xml` has one order with shape `kTruffler` (a typo), which parses as "unspecified", i.e. a random shape (*shop* 3.1) | read `kTruffler` as `kTruffle` | high | 9 |
| F3 | `VObject` reads `scale[1]` for both axes (*sprites* 4.2); `locations/machines.lua:158` gives `{0.96, 1.0}` | `scale[2]`, when present, is the y scale | medium-high: the only non-uniform pair; every other table repeats one value | 7 |
| F5 | The zone graph is made once and keeps its route across level loads, so a walk around the centre table cut off by the level's end is finished by the next level's new avatar (*sprites*, zones) | drop the route when the avatar is made | high: the new avatar starts on her own point with an empty queue | 10 |
| F4 | The tutorial manager gives every tutorial persistence slot 0 (*meta* 1.3), so after a reload one finished tutorial suppresses `tutorial_upgrades` and the endless tutorials | each tutorial keeps its own state | high: the slot argument exists only to be distinct | 14 |

Left as original (not fixed; to revisit after the user has played):

| Behaviour | Why it stays |
|---|---|
| Grytviken levels 11-20 order only infusion bars (`kPress1` is enabled at 21 there) | the user is unsure it is a bug; it may be the location's theme |
| `kConveyor2` (turnaround) is sold but no C++ reads it | the user's call; fixing would mean inventing the turnaround |
| Chocolate decay exists only in levels 51-60 | the user's call; reads as a deliberate late-game rule |
| Inert `values.xml` entries (`drink_patience_bonus`, `complimentary_pickup_prob`, `endless_patience_penalty`, `sauce_combobonus`) | the user's call; the file itself marks one as unused |
| Customers with `start` 135 (levels 29, 30, 49, 50) never spawn | intent unknown (a late spawn? disabled on purpose?); any fix invents a time |
| Customers with `start` exactly 100 never spawn (spawning needs elapsed < total) | probably meant "at the bell", but spawning them lengthens the level; not evident enough |
| `hud.lua` names `ui/button/hud_menu_overlay` but the file is `ui/buttons/hud_menu_overlay.png`, so the overlay never shows | a script typo, but showing art the original never showed needs a look at it in the HUD first |
| A pressed sprite fires Click on any mouse-up, inside or not (*sprites* 2.5) | may be deliberate leniency in a time-management game; subclasses may check +0xd4 |
| `hud.lua`'s `needle_scale = 1/6` and `startingPos` are never read by `KDial` | not a defect: applying 1/6 would shrink the needle art, and `startingPos` is the default 0 |
| The display case's influence sets drop an option when one of two equal chocolates leaves (*machines* 2.4) | only shifts a 20-30 % order bias; not visible as a bug |
| `kDefaultButtonSoundOut` is undefined, so two pause buttons are silent | the intended sound is unknown |
| `tutorial_6` is for level 61, which never plays | the intended level is unknown |
| Achievement names whose thresholds differ (`sauce1000` is 500, ...) | the XML values are the game's rules |

## 8. Decisions

| # | Choice | Decision |
|---|---|---|
| D1 | Volume curve: the Mac build squares `master x level` (0.75 -> 0.56 gain); the engine is linear for the other games | Squared, as a GREATCC profile flag. The Windows build is taken to do the same: its scripts and assets are identical to the Mac build's |
| D2 | `CHOCO_GAME_GREATCC` default and visibility in the launcher | ON (level 1 plays start to finish); the game finder lists GREATCC only when built |
| D3 | High scores | A local table per mode (slice 19); no online submit |
| D4 | Saved values: exact, or truncated to int on save as the original (lifetime `total_worth` loses cents on reload) | Exact |
| D5 | Whether F3 (non-uniform `scale`) counts as a clear bug: it is a 4 % horizontal difference on one machine part | Fixed (section 7); dropped if it looks wrong next to the original |

## 9. Not built

Stubs: `RAdapt*`, `RSpriteLayer`, `RMatch3*`, `RGame`, `RScorer`, `RCursor`, `RButtonedSlider`, `RJustified`, `RAlert`,
`RSplit9Text`, `RSplit9Expander`, `KBar`, `RParentButton` (placeholders); `TXSellWindow`, `VUpsellWindow` links, `LaunchURL`,
`SubmitToServer`, `THiscoreSubmit`; `TGameState` timers and `DoMetrics` (strings stored, no effect); `VSprite::DrawRect` (dead code in the
original). The developer tools are built (*debug*): the Ctrl+C console (ungated, as in the original), `DoDebug`, `VChooseAchievements`,
`MainDebug`, the Ctrl+T / Ctrl+S overlays on the shop and the world map, and `zones_debug` drawing.

Deliberate differences of the developer tools:
- **Ctrl+T** has no texture ledger behind it (`TexInfo` is not reproduced). The text keeps the format of `GetStatus`: "Current VRam" is
  the bytes (`w * h * 4`) of the textures the manager's sprites drew this frame, "Max VRam" those of every texture the manager has
  drawn, "Peak VRam" the largest "Current" seen, `SystemRam` is always `0 bytes`, and the size lines count the used textures by their
  power-of-two width. Only sprites that draw an image count (text sprites do not), as in the original.
- **Ctrl+D on the main menu** is an engine addition: with `cheatmode` on and the main menu the top modal it runs `MainDebug` (every
  achievement acquired and saved, no notification) and logs one line. The original has no way to reach `MainDebug`. The native itself
  is ungated, as in the original; without `cheatmode` the chord does nothing.
- **Key focus**: the engine's modal push and pop leave the key focus on the modal, whereas the original's modal hands it to its default
  focus. The shop, the world map and the main menu therefore take the focus back each frame while they are their modal's default focus
  and the modal is on top (`claimKeyFocus`), so Ctrl+C / T / S / D reach them. Closing the console returns the focus to the shop (the
  original's behaviour here was not traced).
- `addminutes` never takes the level clock below 0 or past `int`; bad numbers read as 0 and a non-finite float as 0 (the original's
  `to_int` / `to_float` were not opened). `DoDebug` before the first level load does nothing (the original would use a null controller).
  A second Ctrl+C builds a second console, as in the original.
- Choose Achievements lists 50 toggles from the shipped `achievements.xml` (the RE note's 51 counts the commented-out `endgame` entry).

Not reproduced: a score of 0 in a
high-score table (the shared `HiscoreStore` skips scores below 1; the original's `RecordScore` has no such test), the original RNG
sequence, `prefs.dat` import, the fade-in's stale target volume, the ZXML-only
asset-memory accounting (`TexInfo`), VO playback beyond the queue (no VO ships).
