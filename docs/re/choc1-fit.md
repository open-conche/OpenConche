# Chocolatier 1 on the engine's game/module split

How Chocolatier 1 (C1) fits `GameModule` and `GameProfile` as they are now (`docs/design/engine-and-games.md`
sections 3 and 9). Read with `docs/re/choc1-natives.md` (the exe) and `docs/re/api-choc1.tsv`.

Tags: **fact** = read from bytecode, strings or decompile; **inference**; **unknown**.

## 0. Sources

- Lua: C1 ships 93 Lua 5.0 bytecode files. Each was disassembled with a workbench script (a scratch copy; line numbers
  below are the bytecode's source lines). C1 has no source Lua. GETGLOBAL census: 426 distinct global names read, 298 assigned
  by the Lua itself, 161 read but never assigned. Of those 161: 73 are natives or native-bound functions, 20 are window
  classes, about 40 are the SDK prelude (`MakeDialog`, `DoModal`, the `k*` constants, `Color` ...), and the rest are Lua
  stdlib names or probes (below).
- **The one C1 source (fact).** The original Chocolatier 1 Windows install is the only Chocolatier 1 build used. It is complete: `assets.pfp`
  has 906 entries (669 png, 93 Lua bytecode, 75 ogg, 37 jpg, 25 xml, 7 mvec), `Chocolatier.exe` is 1,306,624 bytes,
  FileVersion 1.0.0.106, **unpacked** MSVC 8 with RTTI (so it was decompiled directly), and `strings.xml` is **English**
  (ZXML; `windowtitle` = `Chocolatier`, `formatThousands` = `$%%d,%%03d` with the doubled percents as stored, `time` =
  `<month> <day>, <year>`, `month_1` = `January`, `formatScore` = `%1%/week`). `settings.xml`: `game` chocolatier,
  `gameversion` `dl,pf,1.0`, `serverversion` 3.0, `locale` en, `hiscorelocal` 0, `hiscoreanon` 1. The Lua was compiled
  2007-06-08 17:05. The engine and CTest `choc1_mainmenu` read the install's `assets/` directly (read only); the
  archive is extracted to `re/choc1/pfp_extracted` with a workbench script.

## 1. Natives C1 needs that are not Chocolatier 3's

Module hooks used: `registerStubs` (stub rows from `api-choc1.tsv`), `registerNatives` (C1 natives, bound after choc3's, so a
C1 name overrides a choc3 name), `registerCoreExtras`, `registerWindowTypes`, `registerGlobals`.

**1.1 Boot path (fact).** `ui/mainloop.lua` calls `UtilCheckCheatMode` before its first `require`, then requires
`ui/styles.lua`, `ui/standard.lua`, `ui/logoinfo.lua`, `ui/numericentry.lua`, `sim/sim.lua`, `sim/medals.lua`
(`sim/firstpeek.lua` only if `UtilCheckFirstPeek()`). `ui/mainmenu.lua` opens with `StartMusic("menu.ogg")`,
`QueueMusic("menu.ogg", 2)` twice and `SetAmbient({})`. The menu needs `GetNumUsers`, `GetUserName`, `GetCurrentUserName`,
`SetCurrentGameMode(0)`, `DisplayDialog`, `UtilEnableWindow/DisableWindow`.

**1.2 Natives that are the SDK's, shared with choc3 (presumed same; fact that the names match `api-windows.tsv`):**
`EnableWindow GetLabel SetLabel PostMessage CreateNamedMessage PushModal GetButtonToggleState SetButtonToggleState DebugOut Pause`
and the prelude (`MakeDialog DoModal DisplayDialog SwapToModal SetStyle AppendStyle Group GetTag Color ...`). Already bound.

**1.3 Same name as choc3, different meaning (must be overridden by C1; fact from `api-choc1.tsv` col `relation_to_choc3`):**

| Name | C1 | choc3 |
|---|---|---|
| `LoadGameString(mode)` / `SaveGameString(mode, data)` | per-mode pref `save_<mode>` of the current user (`0041c900`, `0041ca70`) | no mode argument, one pref |
| `SetAmbient{name=,loop=}` | takes a table (`mainmenu.lua`: `SetAmbient({})` stops) | not a CADI-era native; choc2 takes a string |
| `PlaySound(name)` | `audio/sfx/<name>.ogg`, 9 preloaded aliases | choc3 has none (uses `SoundEvent`) |
| `StartGame`, `GetState`, `ScoreAvailable`, `UpdateText`, `SaveVolumes`, `PauseTravel`, `ResumeTravel`, `SubmitToServer` | bound to the live C1 window instance (exist only while it exists) | choc3's own windows |
| `Dollars` | defined **in C1 Lua** (`sim/utility.lua`, SETGLOBAL) over `UtilDollars`, so choc3's native is shadowed when `utility.lua` loads | native |

**1.4 C1-only natives, grouped by the hook that binds them (fact: names and arity from `api-choc1.tsv`, call counts from the census):**

- *Core / util extras* (`registerNatives`, or `registerCoreExtras` for the calendar-like ones; none are dates, see 2):
  `UtilCurrentTime UtilMathFloor UtilMathRandom UtilMathSeedRandom UtilDollars UtilTimeStamp UtilNextSaveFile UtilStringToFile
  UtilStringFromFile UtilCheckCheatMode UtilCheckFirstPeek UtilCheckCrossSell UtilToClipboard` (13), and window-tree helpers
  `UtilEnableWindow UtilDisableWindow UtilSetLedgerTab UtilSetButtonOn UtilSetGlobalString UtilSetTextColor UtilPositionWindow
  UtilPulseImage` (8). `UtilTriggerAd` and `UtilTriggerCommunityOffer` **are called** from the Lua (no arguments, result ignored; `sim/building.lua:226`, `ui/map.lua:35`, `ui/portview.lua:26`, and `ui/mainmenu.lua:245`), so they need no-op stubs (`choc1-natives.md` 2: the exe sends `SendMessageA(parentWindow, 0x401, 1 or 2, 0)` to its host; inference: ad hooks of the retail launcher).
- *Users:* `GetCurrentUserMode` (0 args, string), `ChangeUserName(index, name)`; both absent from choc3's list.
- *Audio:* `StartMusic QueueMusic` (new), `PlaySound`, `SetAmbient` (overrides).
- *Money counter:* `SetUpdateRate SetMoneyDisplayTarget SetMoneyDisplay`.
- *Window-bound game natives (come with the window class, 1.5 and 6):* `UpdateDynamicWindow` (59 call sites), `UpdatePlayerButtons`,
  `RollDice StopDie`, `RestartGame GetProductCount TogglePause`, `AnimateTravel`, `SubmitMedalsToServer`, `BTSetTint/Offset/...`
  (not called by the Lua in the census; do not stub).
- *Probes, never defined, always guarded (inference from the call shape):* `FP_StartFree FP_StartStory FP_TotalTimePlayed
  cFP_CurrentTime cFP_DumpEvent` (FirstPeek demo hooks; guarded by `gFirstPeek`, which stays false while `UtilCheckFirstPeek`
  returns false), `gDevelopmentMode`, `DefaultStyle` (`styles.lua` reads it as nil), `StandardButtonFont`, `kIllegalNameChars`,
  `gEligibleScore gAccountEdit gPassEdit gNameEdit gRemember` (hiscore submit, set by the native by evaluating text).

**1.5 Reuse against Chocolatier 2 (the C2 `bsg*` natives are the C1 `Util*` ones renamed; `docs/re/choc1-natives.md` 2.1).**

| C1 native | Same as | Verdict |
|---|---|---|
| `UtilCurrentTime` | choc3 `CurrentTime` = C2 `bsgCurrentTime` | exact alias (fact) |
| `UtilStringToFile / FromFile` | `WriteToFile / ReadFromFile` = C2 `bsgWriteToFile/ReadFromFile` | exact alias (fact) |
| `UtilTimeStamp` | `nativeBsgTimeStamp` (`Choc2Natives.cpp`) | same format `%Y.%m.%d-%H.%M.%S` (fact); share by move to common |
| `UtilMathRandom(lo,hi)` | choc3 `RandRange` = C2 `bsgRandom` | same contract, formula differs (1 in 32768 gives `hi+1`); alias is safe |
| `UtilMathFloor` | C2 `bsgFloor` | truncates; `Floor` differs for negative non-integers; alias OK for the non-negative call sites |
| `UtilMathSeedRandom` | C2 `bsgRandomSeed` (no-op) | no-op fits |
| `UtilDollars` | C2 `nativeBsgDollars` | see 2 |
| `UtilNextSaveFile` | C2 `bsgNextSaveFileName` (returns "") | dev only (`dev/devfile.lua`); the choc2 stub fits |
| `UtilSetGlobalString(name,text)` | C2 `bsgSetGlobalLabel` (a stub there) | **inference:** same job (sets the label of every Text called `name`); C2's stub could be filled from this |
| `UtilCheck*` (3 natives) | C2/choc3 `CheckConfig`/`bsgCheckConfig` | C1 splits them: cheat = `IsEnabled("cheatmode")`, firstpeek = false, crosssell = `xsell/xsell.xml` exists |
| `DynamicWindow` / `UpdateDynamicWindow` | `games/common/DynamicWindow` (moved from `games/choc2`; written from `TDynamicWindow`; C2's name is `UpdateDynamicContent`) | **done**: shared class, C1 registers the type (log tag `choc1`) and `UpdateDynamicWindow(name)` over the whole modal chain |
| `DiceGame` | `choc2::DiceGame` core (C2 reused C1's timings, `DiceGame.h`) | C1 natives differ (`RollDice(bet)`, `StopDie`, 4 fixed dice); reuse the tick, new wrapper |
| `UtilEnableWindow/Disable`, `SetTextColor`, `PulseImage`, `SetButtonOn`, `SetLedgerTab`, `PositionWindow`, `StartMusic/QueueMusic`, `SetMoneyDisplay*`, user natives with a mode | none | C1 only |

Everything in the table's "exact alias / same format" rows could move to `games/common` as a `LegacyUtilNatives` file bound
by both modules under their own names. Today they live in `games/choc2/Choc2Natives.cpp` and `kAliasRows`.

**Stubs (done):** `api-choc1.tsv` has a 12th column `engine` saying how this engine binds each name: `sdk` (the SDK
or choc3 binds the same name with the same meaning: API_NAME only), `alias:<Choc3Name>` (ALIAS row), `@native` (hand-written in
`games/choc1/Choc1Natives.cpp`: API_NAME only), `-` (a logged STUB, or a placeholder WINDOW_TYPE for a window class) and `skip`
(not a native: the animation-script names `IsPlaying GetScript SetAlpha SetLayer RemoveSprite Random`, `_ALERT`, `__index`,
`pfMessageMetatable`, and `BTSetBitmap`, which the Lua never calls). `gen_stubs.py` writes `games/choc1/generated/Stubs_choc1.inc`
from it. Where choc3's native has the same name but another meaning and C1's is not written yet (`StartGame`, `SubmitToServer`),
the row is `-` so a STUB overrides it. Window classes inherited from choc3 for now (already registered when choc1's rows run):
`CreditsWindow`, `HiscoreWindow`, `XSellWindow` (and `OptionsWindow`, `MapWindow` ... which C1 does not use). No placeholders are
left: `DiceGame FactoryGame Finale Logo MapScreen OptionsDialog ChoosePlayer Rollover` are all @native.

## 2. Calendar and money

- **Dates (fact).** C1 has **no date native** (`Date`, `bsgDate`, `DayOfYear` are not read by its Lua). The date is Lua, in
  `sim/sim.lua` at lines 496-505 (a `Simulator` method taking `self`; its name was not recovered): with `time` in weeks,
  `year = floor(time / 52)`, `w = time - 52*year`, `month = floor(w * 12 / 52)` (0..11),
  `day = floor((w - floor(month * 52 / 12)) * 7)`, then `0 -> 1` and `> 28 -> 28`. It returns
  `bsutil.GetVariableString("time", {year = year + 1880, day = day, month = "month_" .. (month + 1)})`. In the Windows install's
  `strings.xml` `time` is `<month> <day>, <year>` and `month_1` is `January`. A year-only helper at `sim.lua:508-509`
  returns `floor(time / 52)`. `GetVariableString` is defined in `sim/utility.lua` (its name is in that file's constants;
  its body was not read; **inference:** it fetches a string and fills the `<key>` placeholders).
- Versus `choc2Date` (`Choc2Natives.cpp:146-165`): **the same month and day cutting, same clamps** (`inYear = w % 52`,
  `month = inYear*12/52`, `day = (inYear - month*52/12)*7 + days`, `0 -> 1`, `> 28 -> 28`), but base year **1925** (C1:
  **1880**), a second `days` argument (C1 has none) and a different string path (C2 builds `formatDate` with `%d` slots; C1 uses
  the `time` key with `<month> <day>, <year>`). **Inference:** C2's native is the C1 Lua moved into C++ and given a `days` term.
- Versus choc3 `Calendar` (`games/common/Calendar.h`, 1946 base, +171 days, 365-day years): unrelated (C3 counts days, C1 weeks).
- **Consequence:** C1 needs no calendar native, so `registerCoreExtras` stays empty for it. The only engine work is that
  `Ledger`/`dateText` in `games/common` must not be used for C1 (it has no `Ledger` class anyway, 6).
- **Money (fact).** `UtilDollars(n)` (`0041e0e0`): truncate to 64 bits, take the low 32 bits **as unsigned** (so a negative
  wraps to about 4.29e9), split into billions/millions/thousands/ones, and format with the first non-zero key of
  `formatBillions` / `formatMillions` / `formatThousands` / `formatOnes`, each a format string such as `$%%d,%%03d` (English `strings.xml`, doubled percents as stored; how the formatter resolves `%%` is
  unknown; the engine's `StringTable::sdkLookup` turns `%%` into `%`, which gives printf slots (`$%d,%03d`) and `$1,234`; inference, done in `choc1Dollars`). It does **not** read `_MONEY_SIGN` or `_NUMERIC_SEPARATOR` (neither is in the Lua census). Versus choc3
  `Dollars`: rounds, signed, globals-driven. Versus choc2 `nativeBsgDollars` (`Choc2Natives.cpp:125-136`): same truncation,
  but it still builds the text from the sign/separator globals and lacks the unsigned wrap, so it equals C1 only for
  non-negative integers (the existing TODO there). C1 should use the string keys and wrap; share one implementation in
  `games/common` with a strings-keys mode. Whether a negative amount can ever be passed is **unknown** (the ledger shows
  debts as `Dollars(-n)`? not checked).
- Money counter (fact, `choc1-natives.md` 5): `SetUpdateRate(hz)` (Lua passes `kDefaultUpdateRate = 24`, `mainloop.lua`),
  `SetMoneyDisplayTarget(n)` counts over about 3 s and plays `audio/sfx/money_earned.ogg` when rising, `SetMoneyDisplay(n)`
  snaps; they write every Text named `money`. `games/common/MoneyCounter.h` exists (choc3 rate: 1/20 of the gap per 50 ms);
  C1's 3 s linear step is a parameter, not a new class (inference).

## 3. Saves

- **Natives (fact):** `SaveGameString(mode, data)` writes pref `save_<mode>` of the current user with a flush;
  `LoadGameString(mode)` reads it. `mode` is `gSim.mode`, `"empire"` (story) or `"free"` (`sim.lua` 114, 130, 198, 285).
  `SetCurrentGameMode(0)` and `GetCurrentUserMode()` are separate (a stored mode pref). Per-user prefs live in the exe's
  `user:prefs.dat` (string `004e993c`); local scores in `user:hiscore.dat` (checksummed). The payload is the Lua text
  `return { ... }` built by `SaveGameTable` and `ConvertToString` (`sim.lua:192-198`), not a binary format.
- **Autosave (fact):** `UtilCurrentTime` + `gAutoSaveTime`; `AutoSave` compares against 30 s (`choc1-natives.md` 2).
- **It uses its own pair, so `refuseSave` does not apply.** The guard sits in choc3's `SaveGameString`
  (`UserNatives`, `refuseSave`), and C1 never calls that signature; if C1 binds its own `SaveGameString(mode, data)` the hook
  is never consulted. `Choc1Module::refuseSave` should return false (do not forward choc3's: its fingerprint is Reforged's
  `Player` shape and would be dead code, or wrong if the string ever matched). Compare C2: `bsgSaveGameString` takes
  one argument and picks a slot from the game mode (`nativeBsgSaveGameString`).
- **Storage on this engine (inference):** `sdk::UserStore::loadSave(slot)` has two slots (`save.txt`, `save_free.txt`),
  which map directly to `"empire"` and `"free"`. No store change is needed if the two native names pick the slot from
  `mode` like C2 does. `GetCurrentUserMode` can use `UserStore::pref("mode")`.
- **Exit save (fact):** when the main loop ends, the exe runs `if bsutil then bsutil.SaveGame() end` (string in the exe, main loop
  `004036d0`). `bsutil.SaveGame` (`sim/utility.lua`) logs the score and calls `gSim:SaveGame()` when a game is loaded
  (as `choc1-natives.md` 8 states; I did not re-read its body).
- **Import:** `--import-prefs` (`PrefsImport`) targets choc3's `prefs.dat`; C1's file is the same SDK container
  (**inference**, same `user:prefs.dat` name and Blowfish KVPR design) but the keys differ. Out of scope.

## 4. Audio

- **No CADI (fact).** The pfp has only `audio/music/*.ogg` (20 files: `menu gameplay1..3 storyloop minigame`, 14 port stings) and
  `audio/sfx/*.ogg` (55, including `typing_01..05`, `ui_click`/`UI_click`, `main_rollover`, `*_loop`). No `.cad`, no
  `cadi/`. The Lua calls no `SoundEvent` (census: absent). So `audioEvents()` returns **nullptr** (the app passes
  `EventTable::empty()`), and C1 must not forward choc3's table.
- **Natives (fact, `choc1-natives.md` 6):** `StartMusic(file)` plays `audio/music/<file>` once, fades the old track over
  1000 ms (100 ms ticks) and queues; `QueueMusic(file, seconds)`; `SetAmbient{name,loop}` loops `audio/sfx/<loop>.ogg`;
  `PlaySound(name)` plays one of nine aliases (`open close charopen charclose boat train airship fanfare mcfanfare`) or
  `audio/sfx/<name>.ogg`. Categories 1000 sfx, 1001 music, 1002 ambient; default levels 750 each (`soundlevel`, `musiclevel`,
  `ambientlevel`). Menu looping is Lua re-queuing (`QueueMusic("menu.ogg", 2)`).
- **Engine surface that exists (fact, `Audio.h`):** `playFile`, `playMusicFile(path, loop, fadeIn, fadeOut)`, `stopMusic`,
  `setAmbientFile`, `setLevels`. C1's queue-with-delay state machine is not there; it is a small C1-only class over these
  (inference; C2's `setMusicState` is a different model and must not be reused).
- **Paths (fact):** C1's tree has the `audio/` prefix and `ui/styles.lua` names `audio/sfx/click_2.ogg` and
  `audio/sfx/ui_click.ogg` literally; `choc1SoundPath` (`games/choc1/Choc1AudioNatives`) builds `audio/sfx/<name>.ogg` and maps the nine
  aliases. **`AssetFS` is case-insensitive (answered):** every key is lowercased at index and lookup (`AssetFS::normalize`), so the
  file `UI_click.ogg` answers `audio/sfx/ui_click.ogg`; the headless check plays `PlaySound("ui_click")` and sees no `missing file`.
- **One 48 kHz file (fact).** All 55 sfx and 19 of the 20 music files are 44.1 kHz stereo; **`audio/music/menu.ogg` is
  48 kHz**, and the engine rejected any non-44.1 kHz stream (`rejected ...: cannot stream`), so the main menu would have stayed
  silent. `OggStream` now resamples a stream at 8..96 kHz to 44100 by linear interpolation (`docs/design/audio.md` decision 1). The
  original SDK resampled in its own sound layer (not read); linear interpolation of music is an engine choice, not a fact.
- **Typing sound:** `ui/widgets/InputWidgets.cpp:155` plays `sfx/sfx_typing_NN.ogg`; C1's files are `audio/sfx/typing_0N.ogg`
  (N = 1..5), played by the exe from `TChocoSound`'s preload, not by the text edit (inference: the preload sits in the sound
  object). This is the row-21 "typing sound resolver" of the cleanup design and C1 would be its second user (needs the hook,
  see 7).
- **`prefetch()` (done, inference for the list):** the nine aliases plus `money_earned`, `ui_click`, `click_2`.
- **Implemented for audio (inference unless marked).** `games/choc1/Choc1Music` is the queue state machine over a `MusicSink` (the audio
  service through `AudioMusicSink`), owned by `Choc1World` (a `World` subclass made by `Choc1Module::attach`) and ticked from `afterAudio`
  with the script clock of the last `tick`. States as in the exe (idle, playing, fading, waiting): `StartMusic` of the playing file is a
  no-op (fact); `StartMusic("")` fades out over 1000 ms and drops the queue; a different file clears the queue, and while a track plays
  fades it and queues the new one with delay 0, so it starts when the 1000 ms are up (no overlap; the fade end is measured on the script
  clock, because `Audio` reports the track gone at once); `QueueMusic` while idle starts at once and ignores the delay, otherwise it
  appends; when a track ends on its own the next entry waits `seconds` of silence and then plays; `StartMusic` during a wait starts at
  once; a file that will not start is skipped. Not read from the exe: `StartMusic` during an *in-progress fade* (it queues the new file
  behind the fade, a guess), and whether `StartMusic("")` clears the queue (the doc says "otherwise the queue is cleared", assumed for the
  empty name too). Music is always `playMusicFile(path, loop=false, fadeIn 0, fadeOut 1000)`. `SetAmbient{name, loop}` is as the doc
  says (`audio/sfx/<loop>.ogg` looped in the ambient group; the same `name` changes nothing; an empty or missing name stops). The
  headless run logs `[audio] music file audio/music/menu.ogg once fade 0/1000` and the menu's dump shows `audio music audio/music/menu.ogg`.

## 5. Content

- **Plain Lua, no loaders (fact).** `data/ports.lua` is 43 instructions, a list of `dofile("data/<port>.lua")` calls
  (one per port; the port files are in `data/`, with `items`, `quests`, `routes`, `travelers`, `newplayer`, `empty`; how the
  last six are loaded was not checked). Port files call `DefineCharacter` etc. directly.
  The census shows **no** `LoadIngredients/LoadProducts/LoadPortFileList/LoadQuestFileList` call. So `registerContentExtras`
  is empty and C1 does not need `games/choc3/ContentLoaders`. XML is used only for `settings.xml` and `strings.xml`.
- Detection already works: `Choc1Module::claims` = `game=chocolatier` and `data/ports.lua`. It is unique: C2 has
  `ui/uihelp.lua`, C1 has `ports/` as images (174 entries) and `data/`.
- **Strings:** the `StringTable` (Excel XML) is generic. C1 `strings.xml` has `month_N`, `format*`, `time` (2). - Settings: `<gameversion>dl,pf,1.0` and `hiscoreanon` is `1` (`hiscorelocal` 0, `locale` en). The exe exposes `kGameVersion`,
  `kHiscoreLocalOnly`, `kHiscoreAnonymous` to Lua. `CheckConfig("local")` reads `hiscorelocal`, forced to 1 at boot.

## 6. Windows and UI

C1's class registry (fact, `choc1-natives.md` 1): SDK `Window Bitmap Button Text TextEdit`, plus `SimpleAnimation BitmapTint
CreditsWindow ChoosePlayer DiceGame DynamicWindow Frame FactoryGame Finale HiscoreWindow Logo MapScreen OptionsDialog Rectangle
Rollover Slider XSellWindow`. C1 has **no** `Port`, `PortWindow`, `Sprite`, `Ledger`, `Character`, `Map`, `MainMenu` class:
its port view, ledger and menu are plain DSL (`ui/portview.lua`, `ui/ledger.lua`, `ui/mainmenu.lua`).

| C1 class | Engine today | Verdict |
|---|---|---|
| `Window Bitmap Button Text TextEdit Rectangle BitmapTint Slider Rollover` | built-ins in `ui/widgets/`; `Rollover` is `choc1::RolloverWindow` | exist. `Rollover` key names differ from C2's: C1 reads `clicksound popupsound hotSize xOffset(20) yOffset xAbsolute yAbsolute area script("ui/rollover.lua") target image`; the built-in honours none of the C1-only ones, so C1 registers its own class (see "Implemented for the port view"). `Rectangle` also reads `shrink pad invisible swallow` (shared class) |
| `Frame` | `choc1::FrameWindow` (**done**) | **did not fit `choc2::FrameWindow`.** C1 `TFrame` hard-codes six images (`image/frameUL 20x24, UR 23x24, LL 23x27, LR 25x27, H 34x12, V 12x35`), reads only `x y w h color name fit` (`hollow` unused), fills `(5,5,W-10,H-10)` then tiles H/V edges and draws corners UL, edges, UR, LL, bottom edge, LR, **inside** the rect; C2's takes `corner/top/left/ornament` from the style and draws outside the rect. Needs its own class (`choc1::FrameWindow`), written from `choc1-natives.md` 3. Draw order is a fact (`choc1-natives.md` 3). The fill rectangle's last two slots: implemented as right/bottom (INFERENCE, hidden by the border art; question 6). Images missing: logged once per image, the fill alone is drawn |
| `DynamicWindow` | `choc2::DynamicWindow` | fits (`contents`, `ontext`; written from `TDynamicWindow`). Needs `UpdateDynamicWindow` alongside `UpdateDynamicContent` |
| `DiceGame`, `FactoryGame` | `choc2::DiceGame`, `choc2::FactoryGame`, choc3 `ChocolateFactory` | **do not fit as classes.** C1's `FactoryGame` is a rotating-tray shooter with its own natives (`StartGame RestartGame GetProductCount TogglePause`), different from choc2's and choc3's; only the tick math of the dice is shared. Not needed to reach the menu or a port |
| `HiscoreWindow` | `choc3::HiscoreWindow` | natives `GetState ScoreAvailable UpdateText` have the same names; **unknown** whether C1's Lua drives it identically (own `ui/hiscore.lua`). Local table only |
| `CreditsWindow`, `OptionsDialog`, `ChoosePlayer` | C2 `Credits`/`Options` (different names); choc3 `OptionsWindow`; `choc1::ChoosePlayerWindow` | C2 notes already say `TOptions` is the same class as choc3's (children `fullscreen soundoff sfxslider musicslider ambientslider`). `ChoosePlayer` (slots `player%d label%d edit%d delete%d newplayer`, max 10) is C1 only and done |
| `SimpleAnimation` | `choc1::SimpleAnimation` | keys `image anim scale animx animy xtile ytile flip path fps(10) random`; used on the main menu (`buttonhighlight.xml`, `fps=12`, and `scale=0.85`, `fps=5`). Done, see "Implemented for the main menu" below |
| `Logo`, `MapScreen`, `Finale`, `XSellWindow` | none | C1 only |

`MapScreen` is the largest gap: `AnimateTravel(table)` drives the vehicle (`FUN_00417da0`), per-week 2000 ms (inference from
C2's `WeekMover`); `choc2::Map`/`MapTravel` are a possible base (unknown). `UtilSetLedgerTab` and `UtilPulseImage` (a 100 ms
task cycling `1.0 .9 .8 .7 .6 .6 .7 .8 .9 1.0` over a BitmapTint) are tree-walking natives needing a window lookup by name
over the whole chain (`Ledger::all` style; see `AGENTS.md` on modal chains).

**Implemented for the main menu (inference unless marked).**

- **`SimpleAnimation`** (`games/choc1/SimpleAnimation`). The window is the size of the first frame of the `anim` sheet (141x141 for
  `buttonhighlight.xml`, 9 frames) or of `image`, unless the style gives a size. Frame `k = floor(elapsed * fps / 1000) mod n` on the
  modal clock from the first draw (the sheet's own frame script is ignored; `fps` is the rate, fact: period `1000/fps`). Each frame is drawn
  with its registration point at (`animx`, `animy`) of the window and scaled by `scale` around it, so the two highlights of the menu
  sit centred on the Story button (seen: the glow lines up with the button; this placement is an inference, the exe's draw was not
  decoded). `flip` mirrors horizontally (inference). **Left out:** `xtile`/`ytile`, `path` and `random`: read, and one
  `[choc1] SimpleAnimation: keys read but not drawn: ...` line was logged (since the port view fixes `path random xtile ytile` are drawn per `choc1-port-view.md` 2 and the line is gone) when a Lua call sets a non-default one (the menu sets none).
  A still `image` is drawn from the window's corner (inference; no shipped Lua uses `image`).
- **Window helpers** (`games/choc1/Choc1Windows`), all over the whole modal chain, top modal first (`forEachNamedInChain`):
  `UtilEnableWindow`/`UtilDisableWindow` set/clear the enabled bit of the first window of that name (that bit hides the window and
  its subtree; no button reset, unlike `EnableWindow`; fact from the decompile), `UtilSetButtonOn` and `UtilSetLedgerTab` (one function in the exe) set
  every Button of that name on, `UtilSetGlobalString` sets the label of every Text of that name (literal, no string-table lookup),
  `UtilSetTextColor(name, {r,g,b})` sets `(r,g,b,1.0)` with missing components 1.0, `UtilPositionWindow` looks only below the top
  modal. `UtilSetLedgerTab` therefore needs nothing port-specific here: the ledger tabs are plain buttons in `ui/standard.lua`.
- **Money counter** (`games/choc1/Choc1Money`, on `Choc1World`): `SetUpdateRate(hz)` sets the period `1000/hz` (integer; 24 gives
  41 ms; 0 stops), `SetMoneyDisplayTarget(n)` steps `max(1, trunc(|gap| / (3*hz)))` whole dollars a tick (`MoneyCounter::setTargetSteps`),
  so 72 ticks of 41 ms (2.95 s) at 24 Hz, and plays `money_earned` when `n` is above the old target and the task runs. With no
  task running (no `SetUpdateRate` yet) nothing would ever step the counter, so it snaps and plays nothing (inference: the exe's behaviour
  was not read). `SetMoneyDisplay(n)` snaps. Every change writes `choc1Dollars` of the shown amount to every Text named `money` in
  the modal chain. Due steps catch up (at most 200 a frame).
- **`UtilPulseImage(name, count)`**: a 100 ms task per name; `count` is the number of ticks (the doc: `UtilPulseImage(name, 30)` is
  3 s), not cycles. The BitmapTint gets grey `v` from `1.0 .9 .8 .7 .6 .6 .7 .8 .9 1.0` with alpha 1 (the tint's alpha is ignored when
  drawing, so the pulse dims the picture), and goes back to 1.0 when the count is used up or the window is gone. A second call with the
  same name restarts the count.
- **The main-menu buttons** (headless, `check_choc1_mainmenu.py`; script path of the dialog on top of `ui/mainmenu.lua`): Change Player
  with no user `ui/okdialog.lua`; Free Play with no user `ui/okdialog.lua`; Options `ui/options.lua` (its Credits button
  `ui/credits.lua`); Help `ui/help1.lua` (a real page, drawn); High Scores `ui/hiscore.lua`; Quit `ui/yndialog.lua`; Story with no user
  `ui/entername.lua`; and, once `CreateNewUser('Ann')` ran, Change Player `ui/chooseplayer.lua`. The main menu Lua has no Credits
  button (it is on the options dialog), and Free Play with a user starts a game, so the check clicks Free Play only without
  one. Placeholders still on screen: `okdialog`, `yndialog`, `entername` and `help1` draw properly (their frame comes from `MakeDialog`), but
  `hiscore` and `chooseplayer` draw their `Frame`, `OptionsDialog` (sliders without handlers) and `ChoosePlayer` (its ten slots drawn as a pile
  of buttons). `ui/hiscore.lua` shows the unresolved label `GAMEMODE` (the `HiscoreWindow` Lua contract, open question 7).
- **What the menu run still logs:** `[unimplemented] StartMovie` only (the SDK's intro movie, an empty name), `[unimplemented-type]`
  `OptionsDialog`, `ChoosePlayer` once the buttons are clicked, and `[gfx] texture 'splash/distributor_logo' not found`.

**Implemented for the port view (inference unless marked).**

- **Quirks:** set as in `choc1-rollover-and-quirks.md` 9 (fact there). `fallThroughPoint`, `rollover` change nothing visible on this path; `popupFader` false is what the Rollover popup's instant mode relies on.
- **`ChoosePlayer`** (`games/choc1/ChoosePlayer`): the window finds `player%d label%d edit%d delete%d newplayer` by name. `UpdatePlayerButtons(selected)`
  is always bound (the exe binds it in the constructor and clears it in the destructor); it acts on the ChoosePlayer found in the modal chain and
  logs one `[choc1]` line when there is none. "Enabled" is the shown bit (hidden). The first layout, at post-init, highlights the current user (the Lua
  starts from `gSelectedPlayer = GetCurrentUser()`); the exe's own initial call was not read. Edit/delete are shown only for the selected slot when it
  holds a user, not for the new-player entry (inference). The label of the new-player entry is the string `opt_newplayer` ("add another player").
- **`Rollover`** (`games/choc1/Rollover`): keys and rules exactly as `choc1-rollover-and-quirks.md` 2 to 4 (hot rect, `_rollover`, `dofile(script)` in the
  popup, one popup at a time, 800x600 clamp with no margin and the +32, `clicksound` after the close and the queued `DispatchRolloverClick`, `image`
  pulse every 100 ms while open, no fade). Absolute mode (both `xAbsolute` and `yAbsolute` >= 1, offsets added) uses the **literal** hot-rect reading
  (centre = 2 x anchor - pointer); the map's tags (hover over Trinidad on `ui/map.lua`) open and look right with it, which does not decide the sign
  (pointer and anchor are close). `Choc1World::mapTrips` is the `DAT_005330a0` gate; `MapScreen::animateTravel` raises it. The engine hook is `ui::openPopup`
  with `PopupOptions{instant, margin}`; the built-in `Rollover` and Chocolatier 2 and 3's popups are unchanged (goldens, `choc2_*`, `reforged_*` scenarios).
- **`Rectangle shrink`** (fact, decompile `0041a5f0`; was missing from the gap table): with `shrink` every Text child is resized to its measured text
  width + `pad` (default 5) by its height, the rectangle takes (widest child right, lowest child bottom + `pad`) and so does its parent. The Rollover's
  string popup (building names, `sim/building.lua:239`) is a `Rectangle{w=800,h=600,shrink=true}` with a Text, so without it every tooltip was a white
  800x600 sheet. `invisible` skips the fill, `swallow` consumes the mouse (default colour white).
- **What the path looks like (fact, run):** Story with a new user -> `ui/entername.lua` -> the game is created and the port entered directly: `ui/portview.lua` with
  `ui/chartalk.lua` on top. **`ui/intro.lua` is never loaded** by the shipped Lua or by a string in the exe; it is plain Lua that builds (full-screen brown
  Rectangle, text, a "Start Game" button) and its button runs `Simulator.EnterPort()` and selects the tutor place, which the test checks by loading it with `DoModal`.
  The tutor goes chartalk (No Thanks, OK) -> `ui/logobuilder.lua` (Done; its `Logo` window is a placeholder, out of scope) -> `ui/dialog.lua` (the recipe book) ->
  `ui/reward.lua`. Building hover/click (Cliff Chalet -> chartalk; Ferry Market -> `ui/buysell.lua`) works through `DispatchRolloverClick`; typing a
  quantity into a sack's TextEdit rebuilds the `receipt` DynamicWindow (`ontext`), the number pad's Buy applies it (money, message log) and the OK button
  closes. **Resuming a saved Story game opens `ui/map.lua`** (so `MapScreen`), not the port.
- **The factory mini-game (`FactoryGame`):** fully decoded in `docs/re/choc1-factory.md` (Lua contract `DoMiniGame` / `GetProductCount` -> weekly `rate`, the 6-state machine, the fixed 10 ms tick, the tray wheel and gun rules, sounds, unknowns).
- **Open question 8 answered:** `UtilSetLedgerTab` (= `UtilSetButtonOn`) must reach every ledger in the modal chain: the buy dialog carries its own
  `DynamicWindow` ledger with its own tab buttons over the port's, and the test sees both `inventory` buttons switch from one call
  (`forEachNamedInChain`, nothing port-specific). The tab *bodies* switch by the buttons' own commands (`UpdateDynamicWindow("ledgertabs")`), not by the native.

## 7. Profile and quirks

Current `Choc1Module::profile()` against the evidence:

| Field | Now | Should be | Evidence |
|---|---|---|---|
| `closeGlobal` / `closeRequest` | `""` / `""` | `"QuitGame"` / `"QuitGame()"` | fact: `mainmenu.lua:27` sets `QuitGame` (DisplayDialog `ui/yndialog.lua` body `confirm_quit`, then posts `kQuitNow` on "yes"), identical to C2; the exe's own close handler runs `if QuitGame then QuitGame() else PostMessage(...kQuitNow...) end` (`004e6768`) |
| `quitDialog` | `""` | `"ui/yndialog.lua"` | fact, same line |
| `exitSave` | `""` | `"if bsutil then bsutil.SaveGame() end"` | fact (3). C2's analogue is `gSim:SaveGame()` (inferred); C1's is stated by the exe |
| `windowTitle` | `"Chocolatier"` | same | fact: `GetString("windowtitle")` = `Chocolatier` |
| `versionFromTitle` | false | false | settings has `gameversion` (inference) |
| `pauseDialogOnFocusLoss` | false | false | fact: focus loss (message `0x11`) and Escape pause only a running factory mini-game (`00403130`), no dialog |
| `volumeDefaults` | `{750,650,500}` | `{750,750,750}` | fact: `TOptions` defaults 750 each (`choc1-natives.md` 6, 9); the user-store defaults are shared, so this field is the only lever |
| `runViews` | `ui/portview.lua`, `ui/mapview.lua` | `ui/portview.lua`, `ui/map.lua` | fact: the C1 ui folder has `map.lua` and no `mapview.lua` |
| `travelGlobal` / `quitGlobal` | `gTravelActive` / `gQuitActive` | no such globals | fact: neither name appears in C1's Lua; harmless (nil) |
| `quirks` | **done**: | `fallThroughPoint` true, `popupFader` false (plus a no-fade popup mode), `cancelDefault` false, `spriteHitAlpha` 0, `rollover` true | fact: `choc1-rollover-and-quirks.md` 6, 9 (`004225d0` fall-through skew as C2; no `TFader`; `buysell.lua` has no `default`; `TRollover::Init` `0041b960` xOffset 20); the Rollover key gaps are in its section 8 |

**What `GameModule` cannot express today (named):**
1. `GameProfile` has no way to say "the exit save runs even if the game state was untouched"; C1's runs unconditionally at
   loop end. Probably fine (`SaveGame` is guarded in Lua by `gSim`).
2. **A typing-sound resolver hook** (cleanup row 21): `InputWidgets.cpp:155` hard-codes the choc3 file name.
3. **A pause-on-focus-loss/Escape hook for one running mini-game** (`GameState::setForeground` can host the focus case; Escape
   needs a key route; both only matter once `FactoryGame` exists).
4. **A per-game sound-path prefix** (`audio/` vs none) is not a profile field; C1's natives can bake it in, C2 did.
5. Everything else (stubs, window types, natives, globals) fits the existing hooks. Specifically `attach` returning a
   `GameState` is optional for C1 until a mini-game needs the tick.
6. `registerGlobals`: C1 sets **none** of `gScreenWidth ... gRealYOffset` (fact: none appear in the exe or the Lua; it hard-codes
   800x600), so unlike C2 it must not call `registerChoc2Globals`. `ui/uiconstants.lua` does not exist in C1 (`styles.lua`
   holds a constants table with `screenWidth` 800 and `screenHeight` 600 itself).

## 8. Known gaps

- Not decoded in the exe: `TFactoryGame` scoring and speed curve, `TCredits` scroll, `TSimpleAnimation::Draw` (`xtile/ytile`, `random`),
  `TRolloverPopup` hover delay and placement, `TMapScreen` internals, the host messages behind `UtilTriggerAd`/`UtilTriggerCommunityOffer`, the Hiscore
  state enum (`Finale` decoded in `choc1-finale.md`, `TLogo` drawing in `choc1-logo-and-hiscore.md`). Online hiscore submit (`SubmitToServer`, `SubmitMedalsToServer`) and XSell (`SetupXSell`, `LaunchGameLink`) are
  out of scope: stub them, `CheckConfig("xsell")` false.
- The design cannot host: nothing structural. The two things that are not "just natives and windows" are the music queue
  (a C1-only Audio user) and `MapScreen` (a new window class with a travel animation).

**Facts learned about users and saves (decompile `re/choc1/decomp`):** `GetCurrentUserMode` (`0041c660`) returns the string pref `mode` of the
current user (pref `curuser`), default `"empire"`; who writes that pref is unknown, so the engine returns `"empire"` unless it is set.
`ChangeUserName(index, name)` (`0041c700`) sets the `name` pref of user `index`. `SetCurrentGameMode(n)` (`0041c190`) accepts 0 or 1,
keeps it in the native instance and hands a string (`chocolatier_story` ...) to its host window; it is not a pref (the engine stores the
`gamemode` pref like Chocolatier 2 so the hiscore table follows it). `UtilCheckCheatMode` is the `cheatmode` setting (`--cheatmode`), as in the exe.
`ui/mainmenu.lua` hides its `loading` overlay with `UtilDisableWindow("loading")` (line 243); the other main-menu call is `UtilEnableWindow`.

## What the engine covers, by topic

**Census on the Windows install.** The census is section 0 (two new natives, both no-op stubs). The staging
script that once copied the assets under `re/` is gone: the engine reads the install's `assets/` directly.

**Stub generation.** `api-choc1.tsv` feeds `gen_stubs.py`, which writes `Stubs_choc1.inc` (stubs, window-type placeholders, aliases);
`Choc1Module` registers them after choc3. No behaviour change for choc3/choc2 (stubs_up_to_date, boot-trace goldens).

**Profile and globals.** The table in 7 (close, quit dialog, exit save, volumes, `runViews`, no screen globals) is covered by a unit test
on the profile.

**Util natives.** A `games/common` shared file holds the `Util*` natives that equal `bsg*` (time, file, timestamp, floor, random, seed,
NextSaveFile, trigger no-ops); C1 binds `UtilCheck*` and `UtilDollars`. Boot reaches `mainloop.lua` past line 14.

**Users and saves.** `GetCurrentUserMode`, `ChangeUserName`, `SetCurrentGameMode`, per-mode `Save/LoadGameString` onto the two
`UserStore` slots; `refuseSave` returns false. Test: new user, save "empire", reload.

**Audio.** `Choc1AudioNatives.cpp` + `Choc1Music.cpp` + `Choc1World`: `StartMusic/QueueMusic` (queue, delay, 1000 ms fade),
`SetAmbient{}`, `PlaySound` with the nine aliases, `audio/` paths; `audioEvents()` null; `prefetch()`. The typing sound is
`GameProfile::typingSound`. `OggStream` resamples a non-44.1 kHz stream (the menu music is 48 kHz).

**Boot and main menu.** `choc1::SimpleAnimation`, window-tree helpers (`UtilEnableWindow` ...), money counter natives over
`MoneyCounter`. Headless test: `ui/mainmenu.lua` builds, the `loading` overlay is hidden, every button opens its dialog (details
below), screenshots.

**Frame and dialogs.** `choc1::FrameWindow` from `choc1-natives.md` 3, plus `DynamicWindow` under the C1 name `UpdateDynamicWindow`.
This unlocks every dialog (`okdialog`, `yndialog`, `entername`, `chooseplayer`). `games/choc2/DynamicWindow` lives in
`games/common/DynamicWindow` (its constructor has a log tag, `"choc2"` keeps Chocolatier 2's log lines byte-equal); `choc1::FrameWindow`
draws inside the rect from the six images (texture cache, alpha, `fillRect`); a missing image is logged once and its pieces left
out. Of the dialogs only `hiscore` and `chooseplayer` use `Frame`; `okdialog`, `yndialog`, `entername`, `help1` and `options` get
their ornate border from a `Bitmap` (`image/half`) inside `MakeDialog`, so the pixel check in `check_choc1_mainmenu.py` probes the
high-score screen. Screenshots: all borders complete and corners flush; the fill is the `color` of the Lua (maroon on hiscore and
chooseplayer).

**New game to a port.** `ChoosePlayer`, `portview.lua` with `Rollover` (C1 key names), `ledger.lua` tabs (`UtilSetLedgerTab`,
`UpdateDynamicWindow`), buy/sell dialogs, CTest `choc1_story` against the Windows install (details below).

**The map.** `MapScreen`, `AnimateTravel`/`PauseTravel`/`ResumeTravel`, `games/choc1/MapScreen`; facts in `choc1-map.md`.

**Options and credits.** `OptionsDialog` (shared `games/common/OptionsPanel`, `choc2::Options` is a subclass) and `CreditsWindow` (`choc1::CreditsWindow` over `ui::CreditsWindow`: `assets/` path, version line, scroll read), the `UtilPulseImage`/Rollover pulse split; the pause label is one line "Pause" because the exe keeps an overflowing last character (`choc1-text-flags.md`).

**The factory.** The factory mini-game (`games/choc1/FactorySim` + `FactoryGame`, the four natives; `choc1-factory.md` section 7 lists the inferences; the gun cursor and Escape are left).

**The dice game.** `games/choc1/DiceGame` + `DiceSim`, `RollDice`/`StopDie`; `choc-dice-game.md`.

**The finale.** `games/choc1/Finale` + a CTest `choc1_finale`, `choc1-finale.md`. `HiscoreWindow` and `Logo` are real.

## Open questions

1. (Answered: the Windows-install Lua calls two natives more, `UtilTriggerAd` and `UtilTriggerCommunityOffer`; the exe's registrar tables are as in `choc1-natives.md` 1.)
2. (Answered: yes, every key is lowercased; `UI_click.ogg` answers `ui_click.ogg`.)
3. (Answered: see `choc1-rollover-and-quirks.md`. No, the built-in `Rollover` honours only `x y xOffset yOffset area` (with `rollover = true`) of C1's keys; `target`, `clicksound`, `hotSize`, `image`, absolute placement and an instant popup are missing. C1 quirks: `fallThroughPoint` true, `popupFader` false (needs a no-fade mode), `cancelDefault` false, `spriteHitAlpha` 0, `rollover` true.)
4. Does `UtilDollars` ever see a negative number (the unsigned wrap would then show about `$4,294,...`)?
5. Name and body of the `Simulator` date method at `sim.lua:496` and of `bsutil.GetVariableString`.
6. `TFrame` fill rectangle slots 2 and 3: right/bottom or width/height? **Implemented as right/bottom** (fill 5,5 to W-10,H-10: 5 px short of the art on the right and bottom, where the border is at least 12 px thick, so not visible either way). Unproven, no visible difference.
7. Is the C1 `HiscoreWindow` Lua close enough to choc3's to share the window class?
8. (Answered: every Button of that name in every modal of the chain, top first; the buy dialog's own ledger and the port's under it both switch. See section 6 "Implemented for the port view".)
