# Chocolatier 3 global Lua API (engine-registered functions and constants)

Source: Mac binary decompile in `re/mac/decomp/`, plus the precompiled Lua chunks embedded in the Mac binary, which I decoded directly from the Chocolatier 3 Mac app bundle's `Contents/MacOS/chocolatier-decadence-design` (read only). Machine-readable version: `docs/re/api-globals.tsv` (name, kind, args, returns, source address, note). Window methods (TWindowScript natives and the window DSL) are another analyst's job; they are only cross-referenced here.

Addresses are Ghidra/vm addresses (file offset + 0x1000).

## 1. How things get into Lua

Everything lives in one Lua 5.0 state owned by `TWindowManager::GetScript()` (script object, `lua_State*` at +0x18). Lua 5.0 is confirmed by `lua_dobuffer`, `luaL_openlib`, `lua_newuserdata` closures and the bytecode header (version 0x50).

Order seen in `ChocoThreeMain` (0001001c):
1. `BSGStringTable` ctor registers `bsgLoadStringFile`, `bsgStringExists`, `bsgGetString`.
2. `Content::Initialize()`.
3. `RegisterBSGLuaHelpers()` (00004494) registers the 19 free functions in section 2 (math, label helpers, save/prefs, file helpers).
4. `SetGlobalNumber` for `kScreenWidth` 800, `kScreenHeight` 600, `kGameWidth` 800, `kGameHeight` 600, `kGameScale` 1; `gVersionString` = "ver " + the config value `kGameVersion`.
5. Registers `SetMood`, `GrabLedger`, `ReleaseLedger`, `Transition`.
6. `AddWindowType` for the window classes (BitmapTint, BSGWindow, CharWindow, ChocolateFactory, CoffeeFactory, CratesGame, CreditsWindow, CurveEditor, DookieDropper, FireworksWindow, MapWindow, PortWindow, RecipeCreator, RecipeDesigner, Rectangle, Rollover, SignEditor, SlotMachine, TightText, OptionsWindow, HiscoreWindow) and `TServerSubmit::Register` (adds window type `SubmitWindow`, registers `GetLuaServerSubmitSetupVars`).
7. `BSGSoundInit()` -> `CadiInit`, registers `SoundEvent`.
8. `TSettings` is created lazily (`TSettings::GetInstance`), its ctor registers the user/settings functions.
9. `ui/mainloop.lua` is run; the main loop then pumps events (event 3 -> runs Lua `AskQuit()`; 0x12 foreground toggle; 0x13 `UpdateFullScreen`). On exit it runs `if Player then Player:SaveGame() end`.

Where the `TScript` is created (`TScript::LuaInit`, 00079e50): opens the base, string and table libs only (no `math`, no `io`/`os`; `luaopen_math` is called only for particle scripts), registers `_ALERT`, `DebugOut`, `toparams`, and compiles a Lua prelude that defines `DumpTable`.

Important registration style: many functions are bound to a **C++ object** (`lua_pushdirectclosure<Class, Ret(Class::*)(Args)>(L, this, member)`), registered in that object's constructor. The Lua global therefore does not exist until that window/object has been constructed, and when a second instance is made it overwrites the first (a stub generator should model these as "available after the owning window type is created"). Argument/return typing comes from the template signature: `str`=Lua string, `double`/`float`/`int`/`unsigned int`=number, `bool`=boolean, `TLuaTable*`=table (ownership released after the call). Return `void` returns nothing. Wrong argument types: not verified (LPCD `Get<T>` normally uses `lua_tonumber`/`lua_tostring`, so a nil number likely becomes 0 and a nil string likely ""; inference).

Three precompiled Lua chunks are embedded in the binary (big-endian Lua 5.0 bytecode, produced on a PowerPC host; opcode set is Lua 5.0 with 8 compare/jump ops):

| chunk | vm address | length | loaded by | defines |
|---|---|---|---|---|
| `@pflib/src/core/ScreenInit.lua` | 0x1db7e0 | 0x3aa2 | `TWindowScript` ctor | all `k*` UI constants, `Color`, `FColor`, `MakeDialog`, `DoModal`, `Group` ... (section 7) |
| `@pflib/src/core/particles.lua` | 0x1d9700 | 0x20d2 | `TLuaParticleSystem` | `kBlend*`, `Init`, `Anim`, `fGreater`, `CreateValue` ... |
| `@pflib/src/core/animationscript.lua` | 0x1d8420 | 0xd93 | `TAnimatedSprite::NewScript` | per-sprite animation script functions (own Lua state, not the global one) |

(Chunk addresses: file offsets 0x1da7e0/0x1d8700/0x1d7420, +0x1000.) The original the original install's `assets/ui/style.lua` comments out `kDefaultcontrolSound`, `buttonHighlightFont` etc., so they are nil in the original.

## 2. Function tables, grouped by subsystem

### Maths and formatting

| name | args | returns | address | notes / Reforged callers |
|---|---|---|---|---|
| `Date` | number weeks | string | 000032a4 | Formats a game-week count as a date via string-table key date_format (args: month name from month_N, day, year). days=weeks*7+171; year=1946+days/365 (365-day years, no leap days in the year count); month by walking _daysPerMonth; Feb length picked by a leap test on the year OFFSET (days/365), not the printed year. Reforged ui/helpers.lua redefines Date in Lua, so the native one is shadowed there. Reforged: not used by Reforged (or defined there) |
| `DayOfYear` | number weeks | number | 00004dc4 | (uint)(weeks*7) mod 365 (no +171 offset). Negative clamps to 0. Reforged: not used by Reforged (or defined there) |
| `Dollars` | number | string | 00003680 | Rounds with %.0f, inserts _NUMERIC_SEPARATOR every 3 digits, prefixes _MONEY_SIGN, leading - for negatives. |x|<0.5 returns literal "$0". Separator and sign are engine globals set by locale (values not determined). Reforged: 135 uses; dev/dev_money.lua;dev/dev_order_detail.lua;dev/dev_quests_content.lua;sim/building.lua... |
| `Mod` | number a, number b | number | 000031bc | Unsigned integer remainder: both args clamped to [0,4294967295] and truncated to uint, returns a%b (disassembly shows divl then edx; the Ghidra C shows / wrongly). Negative -> 0. b==0 would be a hardware divide fault (no guard). Reforged files define a local Mod so rarely hit. Reforged: not used by Reforged (or defined there) |
| `RandRange` | number lo, number hi | number | 0000317c | Inclusive random integer: lo + (int)((hi-lo+1)*RandDouble()) using the global randomizer _GlobalRandomizer. Args truncated to int. Reforged: 104 uses; dev/dev_order_detail.lua;dev/dev_tips.lua;sim/building.lua;sim/character.lua... |
| `Floor` | number | number | 00003290 | C floor(). Reforged: 170 uses; community/api.lua;community/cloud.lua;community/creation.lua;community/hub.lua... |
| `Pow` | number x, number y | number | 0000329a | C pow(). Reforged: 1 uses; sim/port.lua |
| `CurrentTime` | (none) | number | 00004338 | TPlatform::Timer() as unsigned int: milliseconds since process start, clamped to [0, 4294967295] (same as `GetTimer`). Reforged: 8 uses; community/cloud.lua;sim/player.lua |

### Windows and labels (global helpers, not TWindowScript)

| name | args | returns | address | notes / Reforged callers |
|---|---|---|---|---|
| `SetLayeredLabel` | string windowName, string text | (none) | 0000434e | Finds named TLayeredWindow under the top modal window; for layers 0..3 sets that layer current and calls SetText on child label<N> if present; restores the original layer. Missing window: no-op. Reforged: 1 uses; ui/ui_questlog.lua |
| `SetAlpha` | string windowName, number alpha0to255 | (none) | 000042be | Looks up a child by name from the root window (WindowManager+0xc, recursive depth -1) and sets alpha=value/255. Missing: no-op. Not used by Reforged. Reforged: not used by Reforged (or defined there) |
| `FillWindow` | string windowName, string image | (none) | 00004984 | Looks up child from the root window and calls FillWindowPtr(window,image). Missing window: no-op. Reforged: 45 uses; dev/dev_quests.lua;ui/catalogue_character_detail.lua;ui/catalogue_character_list.lua;ui/catalogue_detail_common.lua... |
| `SetWindowPos` | string windowName, number x, number y | (none) | 00004a0e | Child lookup from root; TWindow::SetWindowPos((int)x,(int)y). Not used by Reforged. Reforged: not used by Reforged (or defined there) |
| `Transition` | table t | (none) | 000394ee | Animated window transition. t[1]=type: center_fadein | center | fadein | fadeout | swipe | path | zoomin; t.window=child name looked up under the top modal window (missing -> returns, onend NOT called). Fields: time (ms, default 100, clamped to uint), onend (Lua function), alpha (default 1; fadein/fadeout/center_fadein), startx/starty/endx/endy (swipe, default = window pos), path (table for TCurve; if curve creation fails nothing starts but onend is released, not called). center: sets window x=(rootW-winW)/2 keeping y. fadein sets alpha 0 first; center_fadein also hides the window first. Unknown type: onend released, never called. Reforged wraps it (ui/helpers.lua DoTransition etc). Reforged: 6 uses; sim/building.lua;ui/helpers.lua |
| `SetMood` | string characterName, number mood | (none) | 00007866 | Character::SetCharacterMood: for every visible Character whose name equals arg1, calls SetMood(mood). Reforged: 2 uses; sim/character.lua |
| `GrabLedger` | (none) | (none) | 0002901a | LedgerWindow::Grab: creates the singleton ledger on first use (runs ui/ledger.lua, FitToChildren, 50ms anim task) else detaches it from its parent; then adds it to the top modal window and sets depth 1. Reforged: 2 uses; ui/mapview.lua;ui/portview.lua |
| `ReleaseLedger` | (none) | (none) | 0002784e | LedgerWindow::Release: detaches singleton from its parent if exists (registered in ChocoThreeMain as PTR_Release). Reforged: 5 uses; dev/dev_buildings.lua;dev/dev_ports.lua;sim/port.lua;ui/mapview.lua... |
| `RealUpdateLedger` | string what, string arg2 | (none) | 000278a0 (registered in LedgerWindow ctor 00028e54 under the Lua name RealUpdateLedger) | LedgerWindow::UpdateLedger(param,param): reads Player.rank, handles "newplayer"/"all"/"time"... update modes. Full mode list not decoded. Reforged ui calls it via LedgerUpdateDisplay wrapper. Reforged: 2 uses; ui/helpers.lua |
| `SetLedgerQuestIndicator` | string mode | (none) | 0002887c | Sets image on child ledger_questgoals: "on", "flash" (complete badge image/badge_button_indicator_complete, plays sound quest_requirements_met once, 1500ms), else off. Other modes not decoded. Reforged: 2 uses; ui/helpers.lua |
| `AnimateProduction` | (none) | (none) | LedgerWindow ctor 00028e54 | LedgerWindow::AnimateProduction; body not read. Reforged: 1 uses; sim/sim.lua |
| `PausePortAnimations` | bool pause | (none) | 0002ec2a | PortWindow member: stores flag and pauses/resumes port sprite animations. Registered from PortWindow::Init (0002f47e) so only after a PortWindow is initialised. Reforged: 2 uses; sim/product.lua |
| `InitiateTravel` | table t | (none) | 0002b31c | MapWindow member. t.path (curve) or t.dest{mapx,mapy}; starts TravelTask on the map. No-op if a travel is already active (this+0x6c != 0). Other fields not decoded. Reforged: 1 uses; sim/port.lua |
| `PauseTravel` | (none) | (none) | MapWindow ctor 0002a914 | MapWindow member; pauses travel task. Reforged: 16 uses; dev/dev_buildings.lua;sim/port.lua;ui/ledger.lua;ui/ui_pause.lua |
| `ResumeTravel` | (none) | (none) | MapWindow ctor 0002a914 | MapWindow member; resumes travel task. Reforged: 16 uses; sim/port.lua;ui/ledger.lua;ui/ui_pause.lua |

### Sound

| name | args | returns | address | notes / Reforged callers |
|---|---|---|---|---|
| `SoundEvent` | string key | (none) | 00005166 (registered by BSGSoundInit 0000500a) | "ui_click" plays cadi/ui_click.ogg (cached under key ui_click). Keys containing "firework" go to _CadiEvent(key,0,0) (procedural CADI sound). Otherwise loads sfx/<key>.ogg via TSound::Get(path,1,1000), caches by key, plays. Missing file: silent, nothing raised. Registered only after BSGSoundInit during ChocoThreeMain. Reforged: 75 uses; dev/dev_buildings.lua;dev/dev_ports.lua;sim/building.lua;sim/player.lua... |

### Save, prefs, files, config

| name | args | returns | address | notes / Reforged callers |
|---|---|---|---|---|
| `LoadGameString` | (none) | string | 00004aa4 | Prefs str "savegame" for current user; "" if no current user (-1). Reforged: 1 uses; sim/player.lua |
| `SaveGameString` | string | (none) | 00003a36 | Sets prefs "savegame" for current user (written, flush flag 1); no-op if no user. Reforged: 1 uses; sim/player.lua |
| `WriteToFile` | string name, string data | (none) | 00003de0 | Prefixes name with "user:" (virtual user dir) and writes data with TFile open mode 3. No error reporting. Reforged: 9 uses; community/account.lua;community/identity.lua;community/ownership.lua;community/transport.lua... |
| `ReadFromFile` | string name | string | 00003ede | Prefixes "user:", opens mode 1, reads in 1024-byte chunks until EOF, returns concatenation; nonexistent file presumably returns "" (open failure behaviour not verified). Reforged: 7 uses; community/account.lua;community/identity.lua;community/ownership.lua;community/transport.lua... |
| `NextSaveFileName` | (none) | string | 000040a2 | Iterator over the user: directory: each call advances TFile::GetNextFile; returns the next file name ending in ".choco3" with that 7-char extension stripped (inferred from substr); returns "" when exhausted and resets iteration. Reforged: 2 uses; dev/dev_saves.lua |
| `CheckConfig` | string key | bool | 00003b24 | "local"->IsEnabled(hiscorelocal), "anonymous"->IsEnabled(hiscoreanon), "firstpeek"->IsEnabled(kFirstPeek), "xsell"-> file xsell/xsell.lua exists, "dev"-> IsEnabled(cheatmode); anything else false (so debug/cheat/console/debug_objects keys in Reforged are always false). IsEnabled = GetConfig(key,"") converted by to_int != 0. Reforged: 12 uses; dev/dev_menu.lua;sim/player.lua;ui/catalogue_character_detail.lua;ui/catalogue_ingredient_detail.lua... |
| `LogScore` | number score, string a, string b | (none) | 00003ad6 | TPfHiscores::LogScore(score, true, a, b) on the settings hiscores object; the arguments are passed in order, and the score is truncated to int. Reforged: 1 uses; sim/player.lua |
| `ToggleFullScreen` | (none) | bool | 00035928 | Toggle via TPlatform::SetFullscreen; on success stores prefs fullscreen=1/0 and returns true; false on failure. Reforged: 1 uses; ui/ui_options.lua |
| `ToggleSoundMute` | (none) | (none) | 00035a1e | Flips per-user prefs soundoff, sets game-state kMute, calls BSGSoundMute(QueryMuteGame). Reforged: 1 uses; ui/ui_options.lua |
| `SaveVolumes` | (none) | (none) | 0002d678 | TOptions member (registered when OptionsWindow constructed); reads the option sliders and stores soundlevel/musiclevel/ambientlevel prefs. Details not decoded. Reforged: 1 uses; ui/ui_options.lua |

### Users (TSettings, registered in TSettings ctor 0003649a)

| name | args | returns | address | notes / Reforged callers |
|---|---|---|---|---|
| `SetCurrentGameMode` | number mode | (none) | 00035b6e | Only acts when mode==0: stores 0, sets hiscore property 2 to the game-mode name. Reforged: 5 uses; sim/player.lua |
| `GetNumUsers` | (none) | number | 00035e0e | TPrefs::GetNumUsers. Reforged: 24 uses; community/cloud.lua;community/local_scores.lua;sim/player.lua;ui/community_cloud_saves.lua... |
| `CreateNewUser` | string name | (none) | 000361dc | New user index = current count (0-based). Stores name, copies sound/music/ambient/soundoff prefs from the current user, then makes it current. No duplicate check (use IsNameInUse). Reforged: 5 uses; community/cloud.lua;community/local_scores.lua;ui/mainmenu.lua;ui/ui_changeplayer.lua |
| `SetCurrentUser` | number index | (none) | 000360ec | Ignored if index >= user count. Stores prefs curuser, sets hiscore property 1 to the user name, applies that user sound levels. -1 allowed (no user). Reforged: 9 uses; community/cloud.lua;community/local_scores.lua;ui/ui_changeplayer.lua |
| `ChangeCurrentUserName` | string | (none) | 00035bd8 | Sets prefs name for the current user (index defaulting to 0 if curuser unset). Reforged: 6 uses; community/local_scores.lua;ui/ui_changeplayer.lua |
| `DeleteUser` | number index | (none) | 00036aea | Deletes via TPrefs then selects index-1 (or last user if that is <0). Reforged: 5 uses; community/cloud.lua;community/local_scores.lua;ui/ui_changeplayer.lua |
| `IsNameInUse` | string | bool | 00036b2c | Exact (case-sensitive) comparison against every stored user name. Reforged: 1 uses; ui/ui_entername.lua |
| `QuitToMainMenu` | (none) | (none) | 0003606c | Pops modal windows from the top while the top has a parent-less condition (loop stops at first top modal whose +4 field is null); details approximate. Reforged: not used by Reforged (or defined there) |
| `GetCurrentUser` | (none) | number | 000357f8 | prefs curuser, default -1. Reforged: 6 uses; community/cloud.lua;sim/player.lua;ui/ui_changeplayer.lua |
| `GetCurrentUserName` | (none) | string | 00035ca6 | prefs name of current user ("" default). Reforged: 17 uses; community/cloud.lua;community/local_scores.lua;sim/player.lua;ui/mainmenu.lua... |
| `GetUserName` | number index | string | 00035d7c | prefs name for index ("" default). Reforged: 7 uses; community/cloud.lua;community/local_scores.lua;ui/ui_changeplayer.lua |

### Strings and localisation

| name | args | returns | address | notes / Reforged callers |
|---|---|---|---|---|
| `bsgLoadStringFile` | string path | (none) | 000060aa | BSGStringTable::LoadStringFile on singleton. Not used by Reforged. Reforged: not used by Reforged (or defined there) |
| `bsgStringExists` | string key | bool | 00006316 | BSGStringTable::StringExists. Reforged: not used by Reforged (or defined there) |
| `bsgGetString` | string key | string | 00005ccc | BSGStringTable::GetString. Reforged: not used by Reforged (or defined there) |

### Server, hiscore and community stubs

| name | args | returns | address | notes / Reforged callers |
|---|---|---|---|---|
| `GetState` | (none) | number | 0016e87c | NAME COLLISION: THiscore::GetState (registered when HiscoreWindow ctor runs, 0002339e) returns its state field; TGameState also registers a GetState at 00073e60. Last constructed wins. Reforged: 7 uses; ui/hiscore.lua |
| `ScoreAvailable` | (none) | bool | 00022e38 | THiscore: TPfHiscores::GetUserBestScore(0,...) result. Reforged: 1 uses; ui/hiscore.lua |
| `UpdateText` | (none) | (none) | 000247ea | THiscore: refreshes score table text. Reforged: not used by Reforged (or defined there) |
| `LaunchPrivacyPolicy` | (none) | (none) | 00022e78 | Opens http://www.playfirst.com/privacypolicy.html in the browser. Reforged: not used by Reforged (or defined there) |
| `SubmissionDone` | bool success | (none) | 000253ec | THiscore: sets state=1, requests category info, records result, refreshes text and buttons. Reforged: 2 uses; ui/hiscore.lua |
| `SubmitToServer` | string user, string pass/acct, string x, bool, bool | (none) | 000354ce | TServerSubmit (registered in its ctor 00035084). Arg roles inferred: if arg3 empty, stores arg2... (anonymous path: saves prefs _anonusername=arg1 and submits anonymously); else submits with arg3/arg2. Then switches to waiting mode and starts a 20-step window animation. Needs the original PlayFirst server, so a stub should just call SubmissionDone-equivalent message. Reforged: 1 uses; ui/serversubmit.lua |
| `GetLuaServerSubmitSetupVars` | bool medalsMode | string (Lua source) | 00035168 (registered by TServerSubmit::Register) | Returns a Lua statement string to be executed: gNameEdit='<_anonusername pref>'; then gAccountEdit/gPassEdit/gRemember (remembered login or empty/false), then gMedalsMode=true; or gMedalsMode=false;gEligibleScore=<best score>. Explains gNameEdit/gEligibleScore/gMedalsMode in the tsv. Reforged: 1 uses; ui/hiscore.lua |
| `GetScore` | (none) | number | ChocolateFactory ctor 00008ea8 / CoffeeFactory ctor 00011e3a | Factory mini-game score so far (unsigned). Registered by whichever factory window was constructed last. Reforged: 1 uses; ui/chocolate_gameover.lua |

### Content loading

| name | args | returns | address | notes / Reforged callers |
|---|---|---|---|---|
| `LoadPortFileList` | table out | (none) | 0016cc20 | Reads all ports.xml files (found across asset dirs via BuildFileList sorted), for each <port> child takes <name> child or its own content, strips ".lua", tries "<dir>/<name>.lua" then "<dir>.lua"; if the file exists appends the path to out (1-based). Reforged: 1 uses; sim/port.lua |
| `LoadQuestFileList` | table out | (none) | 0016cb9c | Same with quests.xml and <file> children. Reforged: 2 uses; sim/player.lua;sim/quest.lua |
| `LoadIngredients` | (none) | (none) | 0016cd0c | Reads every ingredients.xml; each <ingredient> becomes a Lua call CreateIngredient {tag=value,...} (SimpleXmlToLua: leaf text always quoted as string, nested elements become sub-tables); registers asset key items/<child>. Reforged: 1 uses; ui/mainloop.lua |
| `LoadProducts` | (none) | (none) | 0016ccbc | Each <product> in products.xml -> Lua call CreateProductFromXML {...}. Reforged: 1 uses; ui/mainloop.lua |

### Mini-games and factories (class members registered as globals)

| name | args | returns | address | notes / Reforged callers |
|---|---|---|---|---|
| `StartGame` | (none) | (none) | ChocolateFactory ctor 00008ea8 / CoffeeFactory ctor 00011e3a | Starts the factory mini-game of the last-constructed factory window. Reforged: 3 uses; ui/chocolate_factory.lua;ui/chocolate_gameover.lua;ui/coffee_factory.lua |
| `PauseGame` | string | (none) | same | Pauses; string argument semantic not decoded. Reforged: 2 uses; ui/chocolate_factory.lua;ui/coffee_factory.lua |
| `ResumeGame` | (none) | (none) | same | Resumes. Reforged: 3 uses; ui/chocolate_factory.lua;ui/coffee_factory.lua;ui/game_pause.lua |
| `PrepareGame` | number | number | 0003cdc6 | TBonusGame (Crates): computes per-crate prize amounts from the _PrizeFactor table scaled by arg, rounded to hundreds (min 100 if factor>0), shuffles with rand(); returns total/uint (exact value not decoded). Reforged: 1 uses; ui/ui_crates.lua |
| `SelectPrize` | number index, bool | number | 0003cfea | TBonusGame::Select: reveals crate index; plays crate_click; whammy_devil image and crate_no_money if the crate holds the bomb. Reforged: 2 uses; ui/ui_crates.lua |
| `StartSlots` | number,number,number,number, number speed | (none) | 00039f16 | SlotMachine::StartMachine: starts the four wheels on symbols 1-4 args and stores speed, 50ms animation. Reforged: 1 uses; ui/ui_slotmachine.lua |
| `StopSlots` | (none) | (none) | SlotMachine ctor 0003af00 | SlotMachine::StopOne: stops next wheel. Reforged: not used by Reforged (or defined there) |
| `StartFireworks` | (none) | (none) | TFireworksWindow ctor 0003e7c2 | Starts fireworks. Reforged: 1 uses; ui/ui_medals.lua |
| `StopFireworks` | (none) | (none) | same | Stops fireworks. Reforged: not used by Reforged (or defined there) |
| `RandomDesign` | (none) | (none) | RecipeDesigner ctor 00032b96 | RecipeDesigner member. Reforged: 2 uses; ui/ui_kitchen.lua |
| `SetRecipeType` | string | (none) | same | RecipeDesigner member. Reforged: 2 uses; ui/ui_kitchen.lua |
| `SetLayer` | number layer, number value | number | same | RecipeDesigner::SetLayer(int,int)->int. Reforged: 5 uses; ui/ui_kitchen.lua |
| `GetLayerImage` | number layer | string | same | RecipeDesigner member. Reforged: 2 uses; ui/ui_kitchen.lua |
| `SetBackground` | string | (none) | SignEditor ctor 00037a64 | SignEditor member. Reforged: 2 uses; ui/sign_basic.lua |
| `SetLogo` | string | (none) | same | SignEditor member. Reforged: 2 uses; ui/sign_basic.lua |
| `SetFont` | string | (none) | same | SignEditor member. Reforged: 2 uses; ui/sign_basic.lua |
| `SetText` | string | (none) | same | SignEditor member (NOT the window SetText). Reforged: 1 uses; ui/sign_basic.lua |
| `SetAnimScale` | number | (none) | TCurveEditor::Init 0001ed18 | Dev curve editor. Reforged: not used by Reforged (or defined there) |
| `UpdateAnimTime` | (none) | (none) | same | Dev curve editor. Reforged: 2 uses; dev/dev_animation.lua |
| `UpdateAnimLayer` | (none) | (none) | same | Dev curve editor. Reforged: 1 uses; dev/dev_animation.lua |
| `SetAnimSprite` | string | (none) | same | Dev curve editor. Reforged: 1 uses; dev/dev_animation.lua |

**Reachability of `SignEditor` and `CurveEditor`:** neither window is constructed by the shipped data.

`ui/sign_basic.lua` (the only user of `SignEditor` and of `SetBackground`/`SetLogo`/`SetFont`/`SetText`) opens only through
`sim/quest_functions.lua`'s `_AwardDialog` dispatch, on a quest award with the key `"sign"` (methods 192/193): the shipped quests
(vanilla `quests/*.lua` and every Reforged quest file - scanned) award no such key, and the vanilla script's own missing-text
default is literally `"#BASIC SIGN DESIGNER: NOT YET IMPLEMENTED"`. `CurveEditor` is a dev tool used only by `dev/dev_animation.lua`,
which retail never runs. The engine keeps both as documented `PlaceholderWindow` types: any future third-party quest awarding
`"sign"` shows the logged placeholder instead of a crash; implementing the SignEditor faithfully still needs reading
`SignEditor::SetLogo`/`SetBackground`/`ctor`/`PostChildrenInit` from the disassembly (the Ghidra decompile of those failed) - a
separate piece of work, recorded under Open questions.
| `BTSetTint` | string windowName, table color | (none) | 0003b8b6 (ctor 0003b672) | SetBitmapTint free function. Reforged: 2 uses; ui/ui_kitchen.lua |
| `BTSetOffset` | string windowName, number x, number y | (none) | TBitmapTint ctor 0003b672 | SetBitmapOffset. Reforged: not used by Reforged (or defined there) |
| `BTSetFlip` | string windowName, bool | (none) | same | SetBitmapFlip. Reforged: not used by Reforged (or defined there) |
| `SetRectangleColor` | string windowName, table color | (none) | 0003c926 (TRectangle ctor 0003c7a2) | Sets the colour of a Rectangle window. Reforged: 1 uses; ui/ui_kitchen.lua |

### Engine-set Lua environment

| name | args | returns | address | notes / Reforged callers |
|---|---|---|---|---|
| `DebugOut` | string | (none) | TScript::LuaInit 00079e50 -> LuaDebugOut 00079c2c | Registered through LPCD with one str; LuaDebugOut body is 5 bytes (no-op) in the shipped build. Extra Lua args ignored. Reforged: not used by Reforged (or defined there) |
| `DumpTable` | table | (none) | TScript::LuaInit prelude string | Lua source in the binary: recursive dump via DebugOut (no visible effect). Reforged: not used by Reforged (or defined there) |
| `toparams` | table | values | ParamsFromTable 0007b300 | Returns `t[1]..t[n]` with `n = lua_getn(t)`, i.e. `unpack(t)`. Not used by Reforged. |
| `_ALERT` | string | (none) | LuaError 00079bf2 | Writes `Lua error: <msg>` plus newline to `std::cerr` via `WriteDbg`; returns nothing. Used as the errfunc of the engine's `lua_pcall`s. Not used by Reforged. |
| `_SetState` | ? | ? | TGameState ctor 00073e60 | Registered free function; TGameState::SetState wrapper (Lua pause/mute states keyed by kPause/kMute etc). Reforged: not used by Reforged (or defined there) |
| `QueryMuteGame` | (none) | bool | TGameState ctor | TGameState member. Reforged: not used by Reforged (or defined there) |
| `QueryRestartMode` | (none) | bool | TGameState ctor | TGameState member. Reforged: not used by Reforged (or defined there) |
| `QueryJumpToMenu` | (none) | bool | TGameState ctor | TGameState member. Reforged: not used by Reforged (or defined there) |

### Constants (engine C++, ChocoThreeMain 0001001c)

| name | args | returns | address | notes / Reforged callers |
|---|---|---|---|---|
| `kScreenWidth` | | 800 | 0001001c | SetGlobalNumber double 0x40890000 Reforged: 8 uses; dev/dev_animation.lua;dev/dev_console.lua;ui/mainmenu.lua;ui/mapview.lua... |
| `kScreenHeight` | | 600 | 0001001c | 0x4082c000 Reforged: 8 uses; dev/dev_animation.lua;dev/dev_console.lua;ui/mainmenu.lua;ui/mapview.lua... |
| `kGameWidth` | | 800 | 0001001c |  Reforged: 2 uses; dev/dev_animation.lua;ui/style.lua |
| `kGameHeight` | | 600 | 0001001c |  Reforged: 2 uses; dev/dev_animation.lua;ui/style.lua |
| `kGameScale` | | 1 | 0001001c |   |
| `gVersionString` | | "ver " + config kGameVersion value | 0001001c | string  |

### ScreenInit.lua precompiled bytecode (file offset 0x1da7e0 = vm 0x1db7e0, length 0x3aa2), run by TWindowScript ctor 000a2a66

| name | args | returns | address | notes / Reforged callers |
|---|---|---|---|---|
| `kHAlignLeft` | | 0 | ScreenInit.lua |  Reforged: 234 uses; dev/dev_animation.lua;dev/dev_console.lua;dev/dev_order_detail.lua;dev/dev_quest_detail.lua... |
| `kHAlignCenter` | | 1 | ScreenInit.lua |  Reforged: 306 uses; dev/dev_enter_amount.lua;dev/dev_tips.lua;sim/port.lua;ui/catalogue_character_detail.lua... |
| `kHAlignRight` | | 2 | ScreenInit.lua |  Reforged: 30 uses; dev/dev_tips.lua;ui/community_cookbook_results.lua;ui/community_creation_details.lua;ui/community_hub.lua... |
| `kVAlignTop` | | 0 | ScreenInit.lua |  Reforged: 157 uses; dev/dev_animation.lua;dev/dev_console.lua;dev/dev_order_detail.lua;dev/dev_quest_detail.lua... |
| `kVAlignCenter` | | 4 | ScreenInit.lua |  Reforged: 403 uses; dev/dev_animation.lua;dev/dev_enter_amount.lua;dev/dev_order_detail.lua;dev/dev_quest_detail.lua... |
| `kVAlignBottom` | | 8 | ScreenInit.lua |  Reforged: 3 uses; ui/mainmenu.lua;ui/ui_factory_content.lua |
| `kPush` | | 0 | ScreenInit.lua |  Reforged: 20 uses; dev/dev_console.lua;ui/community_cloud_saves.lua;ui/community_cookbook_results.lua;ui/community_scores_results.lua... |
| `kToggle` | | 1 | ScreenInit.lua |  Reforged: 10 uses; dev/dev_inventory.lua;ui/community_creation_rating.lua;ui/community_creation_upload.lua;ui/style.lua... |
| `kRadio` | | 2 | ScreenInit.lua |  Reforged: 11 uses; ui/catalogue_history_list.lua;ui/community_cookbook.lua;ui/style.lua;ui/ui_catalogue.lua... |
| `kAllLayers` | | -1 | ScreenInit.lua |  Reforged: 8 uses; ui/catalogue_history_list.lua;ui/style.lua |
| `kCenter` | | 80000 | ScreenInit.lua |  Reforged: 151 uses; dev/dev_console.lua;dev/dev_enter_amount.lua;dev/dev_menu.lua;ui/catalogue_history_document.lua... |
| `kMax` | | 160000 | ScreenInit.lua |  Reforged: 190 uses; dev/dev_animation.lua;dev/dev_console.lua;dev/dev_enter_amount.lua;dev/dev_ports.lua... |
| `kDefault` | | 128 | ScreenInit.lua |   |
| `kPushButtonAlignment` | | kHAlignCenter + <unresolved global>; inferred 5 (=Center+VCenter) | ScreenInit.lua |  Reforged: 2 uses; ui/style.lua |
| `kRadioButtonAlignment` | | kHAlignLeft + <unresolved>; inferred 4 | ScreenInit.lua |  Reforged: 2 uses; ui/style.lua |
| `kToggleButtonAlignment` | | kHAlignCenter + <unresolved>; inferred 5 | ScreenInit.lua |  Reforged: 2 uses; ui/style.lua |
| `kGeneric` | | 0 | ScreenInit.lua |   |
| `kCloseWindow` | | 1 | ScreenInit.lua |   |
| `kDefaultAction` | | 2 | ScreenInit.lua |   |
| `kButtonPress` | | 3 | ScreenInit.lua |   |
| `kPressAnyKey` | | 4 | ScreenInit.lua |   |
| `kQuitNow` | | 5 | ScreenInit.lua |  Reforged: 1 uses; ui/mainloop.lua |
| `kModalClosed` | | 6 | ScreenInit.lua |   |
| `kTextEditChanged` | | 7 | ScreenInit.lua |   |
| `kCommandOnly` | | 8 | ScreenInit.lua |   |
| `kSliderValChanged` | | 9 | ScreenInit.lua |   |
| `kSliderMouseUp` | | 10 | ScreenInit.lua |   |
| `kSliderPageUp` | | 11 | ScreenInit.lua |   |
| `kSliderPageDown` | | 12 | ScreenInit.lua |   |
| `kUserMessageBase` | | 1000 | ScreenInit.lua |   |
| `kCmdLine` | | "cmdline" | ScreenInit.lua |   |
| `kLevel` | | "level" | ScreenInit.lua |   |
| `kSubLevel` | | "sublevel" | ScreenInit.lua |   |
| `kPause` | | "pause" | ScreenInit.lua |   |
| `kMute` | | "mute" | ScreenInit.lua |   |
| `kBreakNow` | | "breaknow" | ScreenInit.lua |   |
| `kScore` | | "score" | ScreenInit.lua |   |
| `kGameMode` | | "gamemode" | ScreenInit.lua |   |
| `kGameOver` | | "gameover" | ScreenInit.lua |   |
| `kNoMoreContent` | | "nomorecontent" | ScreenInit.lua |   |
| `kDownloadButton` | | "downloadbutton" | ScreenInit.lua |   |
| `kUpsellFrequency` | | "upsellfrequency" | ScreenInit.lua |   |
| `kRestartButton` | | "restartbutton" | ScreenInit.lua |   |
| `kSystemMode` | | "systemmode" | ScreenInit.lua |   |
| `kMaxLevel` | | "maxlevel" | ScreenInit.lua |   |
| `kCheatMode` | | "cheatmode" | ScreenInit.lua |   |
| `kComputerId` | | "computerid" | ScreenInit.lua |   |
| `kInstallKey` | | "installkey" | ScreenInit.lua |   |
| `kGameName` | | "gamename" | ScreenInit.lua |   |
| `kGameVersion` | | "version" | ScreenInit.lua |   |
| `kEncryptionKey` | | "encryptionkey" | ScreenInit.lua |   |
| `kHiscoreLocalOnly` | | "hiscorelocal" | ScreenInit.lua |  Reforged: 1 uses; ui/hiscore.lua |
| `kHiscoreAnonymous` | | "hiscoreanon" | ScreenInit.lua |   |
| `gReturnValue` | | nil | ScreenInit.lua |   |
| `gCurID` | | 100 | ScreenInit.lua |   |
| `gBeginGroup` | | 0 | ScreenInit.lua |   |
| `idtable` | | {} (empty table) | ScreenInit.lua | global table  |
| `gParent` | | {} (empty table) | ScreenInit.lua | global table  |
| `gCurrentDefaultStyle` | | {} (empty table) | ScreenInit.lua | global table  |
| `gClosedWindows` | | {} (empty table) | ScreenInit.lua | global table  |
| `gCommandQueue` | | {} (empty table) | ScreenInit.lua | global table Reforged: 1 uses; ui/helpers.lua |
| `gMessageQueue` | | {} (empty table) | ScreenInit.lua | global table  |
| `gDialogTable` | | {} (empty table) | ScreenInit.lua | global table Reforged: 182 uses; dev/dev_buildings.lua;dev/dev_enter_amount.lua;dev/dev_ingredients.lua;dev/dev_inventory.lua... |

### particles.lua bytecode (file 0x1d8700, len 0x20d2), run by TLuaParticleSystem

| name | args | returns | address | notes / Reforged callers |
|---|---|---|---|---|
| `kBlendNormal` | | 0 | particles.lua |  Reforged: 2 uses; glitz/firework.lua;glitz/slotpayout.lua |
| `kBlendOpaque` | | 1 | particles.lua |   |
| `kBlendAdditiveAlpha` | | 2 | particles.lua |   |
| `kBlendSubtractive` | | 3 | particles.lua |   |
| `kBlendMultiplicative` | | 4 | particles.lua |   |
| `kBlendMode` | | "blendmode" | particles.lua |   |

### Functions defined in Lua by embedded ScreenInit.lua (window DSL; covered by the window analyst, listed for completeness)

| name | args | returns | address | notes / Reforged callers |
|---|---|---|---|---|
| `Color` | r,g,b[,a] ints 0-255 -> {r/255,g/255,b/255,a/255 or 1} |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 516 uses; community/render.lua;dev/dev_buildings.lua;dev/dev_quests.lua;dev/dev_quests_content.lua... |
| `FColor` | r,g,b[,a] floats -> {r,g,b,a or 1} |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |
| `Push` | value |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |
| `Pop` |  |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |
| `DoWindow` | table |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 10 uses; ui/catalogue_history_list.lua;ui/style.lua |
| `DoDialogCommand` | a,b |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |
| `MakeDialog` | table |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 128 uses; dev/dev_animation.lua;dev/dev_buildings.lua;dev/dev_console.lua;dev/dev_enter_amount.lua... |
| `Creating` | a,b |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |
| `SetBeginGroup` | v |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |
| `BeginGroup` |  |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 16 uses; ui/catalogue_history_list.lua;ui/community_account_auth.lua;ui/community_cookbook.lua;ui/community_creation_details.lua... |
| `GetTag` | a,... |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 60 uses; ui/catalogue_history_list.lua;ui/style.lua |
| `SetDefaultStyle` | t |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 1 uses; ui/style.lua |
| `SetStyle` | t |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 199 uses; dev/dev_animation.lua;dev/dev_buildings.lua;dev/dev_console.lua;dev/dev_enter_amount.lua... |
| `AppendStyle` | t |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 41 uses; dev/dev_animation.lua;sim/ingredient.lua;sim/product.lua;ui/catalogue_history_list.lua... |
| `SetFocus` | name |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 10 uses; dev/dev_enter_amount.lua;ui/community_account_auth.lua;ui/community_profile.lua;ui/serversubmit.lua... |
| `SetDefault` | a |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |
| `DistributeX` | t |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |
| `DistributeY` | t |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |
| `WaitForCloseMessage` | a |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |
| `DoModal` | script |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 6 uses; dev/dev_utils.lua;ui/hiscore.lua;ui/mainmenu.lua;ui/ui_options.lua |
| `DoMainWindow` | script |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 1 uses; ui/mainloop.lua |
| `ModalReturn` | v |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |
| `DisplaySplash` | a,b,c |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 6 uses; ui/help_general.lua;ui/mainloop.lua;ui/ui_options.lua |
| `FlushMessages` |  |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |
| `WaitForMessage` | a,b |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |
| `Yield` |  |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |
| `ReadFile` | path (dofile "assets/"..path) |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |
| `CloseWindow` | value |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 73 uses; dev/dev_animation.lua;dev/dev_buildings.lua;dev/dev_inventory.lua;dev/dev_money.lua... |
| `CustomCreator` | a |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |
| `GetString` | up to 6 args -> _GetString |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |
| `SetBitmap` | name,image,x |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 44 uses; community/render.lua;ui/community_public_profile.lua;ui/hiscore.lua;ui/recipe_category.lua... |
| `PopModal` | a |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 3 uses; dev/dev_buildings.lua;dev/dev_ports.lua;ui/helpers.lua |
| `SwapToModal` | script |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 16 uses; dev/dev_buildings.lua;dev/dev_ports.lua;dev/dev_utils.lua;sim/player.lua... |
| `DisplayDialog` | script (warns "DisplayDialog called with empty script; aborting") |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 236 uses; dev/dev_buildings.lua;dev/dev_console.lua;dev/dev_inventory.lua;dev/dev_menu.lua... |
| `SelectLayer` | n |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 35 uses; ui/catalogue_history_list.lua;ui/style.lua |
| `Group` | t |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 77 uses; community/render.lua;dev/dev_buildings.lua;dev/dev_console.lua;dev/dev_ingredients.lua... |
| `FitToChildren` |  |  | ScreenInit.lua | Lua closure, bytecode only Reforged: 2 uses; ui/style.lua |
| `Pause` | ms,.. |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |
| `SetState` | key,bool|number |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |
| `RegisterHotkey` | key,cmd |  | ScreenInit.lua | Lua closure, bytecode only Reforged: not used by Reforged (or defined there) |

## 3. Port scenery and building names (`cap_car1`, `zurich`, ...)

393 of the tsv names are lowercase game objects, not engine functions. They are created by Lua, not by the engine:
- `Port:Create(name)` (sim/port.lua) does `_G[name] = t`, so `capetown`, `zurich` ... exist once `ports/<n>/<n>.lua` has run.
- `CreateSprite("cap_car1", port)` (sim/port.lua:559) sets `_G[name] = { image = "ports/<port.name>/<name>" }` and appends it to `port.sprites`. Properties are then assigned (`cap_car1.path = ...`). Covers 307 names such as `*_car*`, `*_cloud*`, `*_mist*`, `*_wave*`, `*_boat*`, masks.
- `EmptyBuilding("bal_temple", bali)` / `Building:Create(name, port)` (sim/building.lua) set `_G[name]`. Covers `tor_hotel`, `bog_hotel`, `cap_building3`, `bag_buildingreddoor`, `bel_ruins`, `dou_emptybuilding1`, `_travelers`, `_empty` etc.
- The engine's role is only the file list: `LoadPortFileList` (ports.xml -> `ports/<dir>/<name>.lua`, section 2), after which Lua `dofile`s each one. The engine does not create these globals. If a name in the tsv is used before its `CreateSprite`/`EmptyBuilding` line runs it is nil.
- `b01` (ui/help_shop.lua) is an ingredient created by `CreateIngredient` from ingredients.xml (`LoadIngredients`), global by tag.

## 4. Particle environment (TLuaParticleSystem)

`TLuaParticleSystem` creates its own Lua state (`luaopen_math` called, line 2176) and runs `particles.lua`. Names seen in `glitz/firework.lua` and `glitz/slotpayout.lua`: `CreateParticles`, `SetNumParticles`, `SetBlendMode`, `SetTexture` (native `_SetTexture`), `Allocate(n)` (n floats; a prelude defines `pPosition=Allocate(2)`, `pUp=Allocate(2)`, `pScale=Allocate(1)`, `pColor=Allocate(4)`, `pFrame=Allocate(1)`), `Vec2`, `fRange`, `fTimeScale`, `fAge`, `fExpire`, `fFade`, `f2dRotation`, `f2dRadius`, `fDistance`, `fPick`, `fOpenCycle`, `fClosedCycle`, `fAlpha`, `fDebug`, `fGreater`, `fLess`, `fLessEqual`, `RandomRange`, `RandomFloat`, `SetRendererOption1..4`, `kBlendMode`; member parts are assembled by `CompileParam` (09764c, 3539 bytes) into per-frame code strings `Update%d(%f);` and `Update(%f);`. Parameter objects have `:Init(value)` and `:Anim(expr)` (particles.lua `ParticleMemberMT`), with metamethods `__add __sub __mul __div __unm __lt __le __index`. I did not decode the semantics of each f* function; treat them as the particle expression DSL, which the particle analyst should cover. Fireworks window also sets Lua globals `gSparkleTime`, `gSparkleDensity`, `gSparkleTexture`, `gSparkleColorStart/End` (strings at 0015a5a4..0015a748; set via DoLuaString; not decoded).

## 5. Animation script state (TAnimatedSprite::NewScript, 0007634a)

Each animated sprite gets its own `TScript` and runs animationscript.lua, which defines Lua globals `alpha`, `alphadelta`, `alphacount`, `delay`, `pause`, `showFrame`, `fadeout`, `fadein`, `setAlpha`, `frames`, `loop`, `run`, `die`, `halt`, `isPlaying`, `endReferences`, `stop`, `random`. Native functions registered there: `SetCurrentFrame`, `WaitLuaNextFrame`, `Die`, `Pause`, `IsPlaying`, `Load`, `GetScript`, `SetAlpha`, `SetLayer`, `RemoveSprite`, `Random` (names resolved from PIC string offsets; per-name argument types not decoded). These are NOT in the global script and are not used by the Reforged files in the tsv (except that `SetLayer` also exists as a RecipeDesigner member).

## 6. Names in the tsv I could not account for (as engine globals)

- `SaveCreationToFile` (ui/ui_kitchen.lua:1056): no string, no registration in the Mac binary. Probably a Reforged/Windows engine addition. Calls to it will hit a nil global in a stock engine.
- `LedgerUpdateDisplay` (ui/ui_buysell.lua): guarded by `if LedgerUpdateDisplay then`, so optional; not registered in the Mac binary (the engine name is `RealUpdateLedger`; a Lua wrapper may exist in the original install's assets).
- `GetTopModalWindow` (ui/helpers.lua:1040): not registered as a global in the Mac binary (it is a C++ method `TWindowManager::GetTopModalWindow` only). Unregistered in the Mac engine; the Reforged call site (`CloseAllModals`) would fail there unless the Windows engine adds it.
- `continue` (sim/port.lua:605): not a Lua 5.0 keyword; it is a call to a nil global (Reforged bug, only fails if reached).
- `arg`: Lua 5.0 vararg table, not an engine global (ui/helpers.lua notes it).
- `RankNames` (ui/community_scores_results.lua): guarded `RankNames and ...`; nothing defines it.
- `RedColor`, `kDefaultcontrolSound`, `kDefaultButtonSound`, `buttonHighlightFont`: nil in the original (commented out in the original install's `assets/ui/style.lua`); `sound = nil` styles.
- `gSparkleTexture`: probably set by the fireworks script; see section 4 (unverified).

## 7. Lua-defined (ScreenInit.lua) window functions

These are Lua closures inside the embedded bytecode, listed in the tables above under "Functions defined in Lua by embedded ScreenInit.lua". Facts decoded from their constants: `ReadFile(p)` is `dofile("assets/"..p)`; `Yield()` is `coroutine.yield()`; `GetTag`, `SetStyle`, `AppendStyle` operate on `gCurrentDefaultStyle`/`parent`; `DisplayDialog` aborts with a DebugOut message on an empty script; `CloseWindow(v)` posts a close message and sets `gReturnValue`; `Pause(ms)` waits using `GetTimer` and returns early on `kPressAnyKey`; `Color(r,g,b,a)` divides by 255 and defaults alpha to 1 (the 4th argument branch is inferred from bytecode shape).

Window DSL type names that appear in the tsv (`Text`, `Bitmap`, `Window`, `Rectangle`, `Slider`, `TextEdit`, `Rollover`, `Anim`, `BSGWindow`, `BitmapTint`, `CharWindow`, plus the factory/mini-game windows) are window types registered with `AddWindowType`; the window analyst covers their constructors. TWindowScript natives in the tsv: `EnableWindow`, `SetLabel`, `GetLabel`, `SetButtonToggleState`, `CreateNamedMessage`, `IsEnabled`, `PostMessage`, `SetCommand` (all confirmed registered in the TWindowScript ctor 000a2a66, with `PostCreate`, `TagHotKey`, `SetPosition`, `GetTimer`, `GetConfig`, `StartMovie`, `IsMoviePlaying`, `StopMovie`, `StringToClipboard`, `PushModal`, `GetWindowDepth`, `DoFitToChildren`, `DoSelectLayer` etc.).

## 8. Unknown

- Exact values of `_NUMERIC_SEPARATOR` / `_MONEY_SIGN` (locale globals). (The unit of `CurrentTime` is milliseconds, see `design-questions.md` section 4.)
- The unresolved global added in `kPushButtonAlignment`, `kRadioButtonAlignment`, `kToggleButtonAlignment` (bytecode constant index lost in my decode); the values 5/4/5 are inferred as `kVAlignCenter` additions.
- Behaviour of `ReadFromFile` on a missing file; `NextSaveFileName` exact substring arithmetic.
- `SubmitToServer` argument roles, `PauseGame(str)` string meaning, `PrepareGame` return value, `QuitToMainMenu` exact loop condition, `RealUpdateLedger` mode list, `InitiateTravel` full field set, `SaveVolumes` details, all of the RecipeDesigner/SignEditor/CurveEditor member bodies.
- Bad-argument handling inside LPCD (nil/wrong type) and Lua error text.
- `_SetState` semantics (TGameState::SetState wrappers). (`toparams` and `_ALERT`: see `design-questions.md` section 2.)
- Whether the Windows/Reforged engine registers extra functions (`SaveCreationToFile`, `GetTopModalWindow`, community functions) beyond the Mac binary; Mac decompile cannot answer this. The `GetState` name is registered by both THiscore and TGameState.
- Per-name signatures of the animation-script natives and the particle DSL.
