# Chocolatier 1 (Windows exe 1.0.0.106): the Lua-visible natives

Source: `re/choc1/` (Ghidra decompile of the Windows install's `Chocolatier.exe`, 1,306,624 bytes, FileVersion 1.0.0.106, MSVC 8, **unpacked** (normal
`.text/.rdata/.data/.rsrc` sections, entry `000adcc7`+base, RTTI present); addresses below are VAs in that exe, e.g. `0041f210`). This complete,
English build is the only Chocolatier 1 source. Ghidra did not attach the RTTI class names to the 3,791 `FUN_` functions, so the class of a function
was found from the `vftable` symbol each constructor writes. Where the decompiler lost the floating-point arguments (all the `TFrame::Draw`
calls) the x86 listing was read directly. Lua call sites come from the Chocolatier 1 bytecode (`re/choc1/pfp_extracted/`, extracted from the install's `assets/assets.pfp`
with a workbench script; disassembled with another workbench script) and, for the `bsg*` comparisons, from `re/choc2/pfp_extracted/`.
The machine-readable list is `docs/re/api-choc1.tsv`. Chocolatier 3 comparisons use `docs/re/api-globals.tsv` and `api-windows.tsv`.

Conventions: "fact" means read from the decompile or listing; "inference" is marked. The engine's Lua is 5.0.2; the Lua C API appears
as small functions: `0042c110` = `lua_pushstring`, `0042c900` = `lua_newuserdata`, `0042c1b0` = `lua_pushcclosure`, `0042c4f0` =
`lua_settable`, `0042be10` = `lua_tonumber`, `0042be90` = `lua_tostring`, `0042c060` = `lua_pushnumber`, `0042c260` = `lua_pushboolean`,
`0042c020` = `lua_pushnil`, `0042bf50` = `lua_touserdata`. `-10001` is `LUA_GLOBALSINDEX`, `-10002` upvalue 1.

## 1. How natives are registered

Every native is a C closure with one upvalue: a userdata holding either `(fn)` (4 bytes, a static function) or `(this, memberfn)`
(8 bytes). Registration is the sequence `pushstring(name)`, `newuserdata`, fill it, `pushcclosure(thunk, 1)`, `settable(GLOBALS)`.
The **thunk** (second address in the TSV) is a small template instance that reads the Lua arguments by type (string / number to
`ftol` / table / bool), calls the target, and pushes the result. Registration happens in constructors, so the table is not one static array:

| Registrar (address) | What it registers |
|---|---|
| main init `004036d0` | the window-class registry (see below), then calls `0041f210`, then creates `TChocoSound` (`00406340`) and the money counter (`00403320`), runs `ui/mainloop.lua` |
| `0041f210` | the 22 `Util*` natives (static functions, no instance) |
| `0041d500` | 18 user/prefs natives: `ToggleFullScreen ToggleSoundMute SetCurrentGameMode GetNumUsers CreateNewUser SetCurrentUser ChangeCurrentUserName DeleteUser IsNameInUse QuitToMainMenu GetUserName GetCurrentUser GetCurrentUserName GetCurrentUserMode ChangeUserName LogScore LoadGameString SaveGameString` |
| `00406340` (`TChocoSound` ctor) | `StartMusic QueueMusic PlaySound SetAmbient`, bound to the one sound object (`DAT_00533044`) |
| `00403320` (money counter) | `SetUpdateRate SetMoneyDisplayTarget SetMoneyDisplay` |
| `00402ca0` (`TBitmapTint`) | `BTSetTint BTSetOffset BTSetBitmap BTSetFlip` (static) |
| `0041a500` (`TRectangle`) | `SetRectangleColor` |
| `00409ad0` (`TDynamicWindow` ctor) | `UpdateDynamicWindow` (static; not removed in the destructor) |
| `004075e0` / `00408fa0` / `0040e940` / `00414b30` / `00418000` / `00418ea0` (`TChoosePlayer`, `TDiceGame`, `TFactoryGame`, `THiscore`, `TMapScreen`, `TOptions` ctors) | their game natives, bound to the live instance. The destructors register `nil` over the same names (`00407070`, `00408670`, `0040ce00`, `00412c20`, `004172f0`, `00418570`) |
| `00414a30` (`THiscoreSubmit`) | `SubmitToServer SubmitMedalsToServer` |
| `004656a0` (`TWindowScript` ctor, the SDK) | 38 window-script natives (`PostCreate GetLabel SetLabel SetPosition SetCommand EnableWindow PushModal PostMessage DoFitToChildren GetString`...), then the five SDK window classes, the message metatable, and runs the embedded prelude chunk (0x2179 bytes at `00527200`, name `gScreenInit`) |
| `0042da90` / `0046ed10` | XSell hooks (`SetTopModalOnly SetupXSell LaunchGameLink`) |
| `0044ddf0` | animation-script environment (`Pause IsPlaying GetScript SetAlpha SetLayer RemoveSprite Random`) |

The consequence for a reimplementation: `RollDice`, `StopDie`, `StartGame`, `RestartGame`, `GetProductCount`, `TogglePause`,
`UpdatePlayerButtons`, `SaveVolumes`, the Hiscore and map natives exist only while their window exists. `UpdateDynamicWindow` exists from
the first `DynamicWindow` construction on (inference: the 59 Lua call sites are always after one was built).

**Window classes.** `FUN_0042d340(name, factory)` / `FUN_00465310` add `(name, factory)` to the class registry; the factory getter returns
a `CreateObject` function. Lua reaches a class through the prelude's `function %s(table) ... table.typename='%s'; return function()
DoWindow(table) end end` template (string at `004fe870`), so `Frame{...}` runs `GenericCreate` with `typename = "Frame"`. Classes:
SDK `Window Bitmap Button Text TextEdit` (`004656a0`, factories `00422530 0042fdf0 00445700 00445dc0 00457930`) and Chocolatier 1's own
`SimpleAnimation BitmapTint CreditsWindow ChoosePlayer DiceGame DynamicWindow Frame FactoryGame Finale HiscoreWindow Logo MapScreen
OptionsDialog Rectangle Rollover Slider` (factories `004015b0 00402f20 00408410 004076f0 00409490 00409cc0 00412290 00410b20 00411490
00415280 00415a60 00418520 00418fb0 0041a7f0 0041b860 0043ced0`), plus `XSellWindow`. `Group`, `MakeDialog`, `DoModal`, `DisplayDialog`... are
Lua in the prelude. For each class the virtual slots used below are: `[0]` dtor, `[5]` Init(table), `[6]` PostInit(table), `[7]` Draw.

`api-choc1.tsv` lists 142 rows: 120 functions and 22 window classes. Argument lists are from the thunk shape plus the Lua call sites;
column `signature_source` says which. SDK rows copy the Chocolatier 3 signature of the same name and are marked as presumed.

## 2. The `Util*` helpers

All registered by `0041f210`. Lua call sites are in `sim/utility.lua` (defines `Dollars`, `bsutil.floor/srand/random/StringToClipboard`,
`AutoSave`), `sim/sim.lua`, `ui/standard.lua`, `ui/mainmenu.lua`, `dev/devfile.lua` and others. Column "C3" says whether the Chocolatier 3 native of the
same job is the same function.

| Native | Target / thunk | Behaviour (fact unless marked) | C3 |
|---|---|---|---|
| `UtilDollars(n)` | `0041e0e0` / `0041f120`, thunk `0041e8e0` | `n` is truncated toward zero (FLDCW 0xC00 + FISTP) to 64 bits, then its low 32 bits are used **as unsigned**. With `b=n/1e9`, `m=(n%1e9)/1e6`, `t=(n%1e6)/1e3`, `o=n%1e3` (all unsigned): if `b!=0` use string key `formatBillions` with `(b,m,t,o)`; else if `m!=0` `formatMillions` `(m,t,o)`; else if `t!=0` `formatThousands` `(t,o)`; else `formatOnes` `(n)`. The Chocolatier 1 strings (`strings.xml` in the Chocolatier 1 install's `assets/`, ZXML-compressed) are `formatBillions` = `$%%d,%%03d,%%03d,%%03d`, `formatMillions` = `$%%d,%%03d,%%03d`, `formatThousands` = `$%%d,%%03d`, `formatOnes` = `$%%d` (fact: the doubled `%%` is in the file; comma separators). How the `%%` pairs are consumed by the formatter (`FUN_00425f20` -> `FUN_00425ea0`, not decoded) is **unknown**; the intended output is `$1,234` style (inference). Chocolatier 2's `strings.xml` has the same keys. Negative input wraps to a 10-digit unsigned value. The Lua `Dollars(n)` calls it with `n or 0`. | **Not the same** as `Dollars`: C3 rounds with `%.0f`, handles negatives with `-`, and takes separator/sign from globals. Equal only for non-negative whole numbers below 2^32 when the string table matches |
| `UtilMathFloor(x)` | `0041dbd0` / `0041f030` | `FLD x; JMP _ftol`: truncation toward zero | Equal to C `floor` only for `x >= 0`; differs for negative non-integers |
| `UtilMathRandom(lo,hi)` | `0041dbe0` / `0041f080` | `trunc(lo + rand()*(hi-lo+1)/32767.0)` with CRT `rand()` (0..32767). Inclusive; the result can be `hi+1` when `rand()==32767` (1 in 32768). Lua `bsutil.random(a,b)` = this with `(a,b)`, or `(1,a)` when `b` is nil | Same contract as `RandRange` (inclusive); different formula and generator |
| `UtilMathSeedRandom()` | `0041dc20` | `srand(Timer())`, the millisecond counter. No arguments. | none |
| `UtilCurrentTime()` | `0041e270` / `0041f160` | `TPlatform::Timer()` returned as unsigned (adds 2^32 if negative); ms since process start. `AutoSave` compares against it (30 s) | **Exact alias** of `CurrentTime` |
| `UtilTimeStamp()` | `0041e280` / `0041f1a0` | local time `strftime("%Y.%m.%d-%H.%M.%S")` (string at `004e8ef8`) | none (C3 has no such native) |
| `UtilNextSaveFile()` | `0041e5d0` / `0041f1a0` | Iterates the directory `user:` with a file-static name buffer. Skips entries whose last 4 characters are not `.csg`; returns the entry with the 5-character `user:` prefix and `.csg` removed; returns `""` when the directory is exhausted and resets. Used only by `dev/devfile.lua` (debug save points) | Same shape as `NextSaveFileName` but the extension is `.csg` here, `.choco3` in C3, `.choco2` in C2 (`dev/devmisc.lua` appends `".choco2"` itself) |
| `UtilStringToFile(name,text)` | `0041e2f0` / `0041f0e0` | writes `user:<name>`, TFile mode 3, the whole string; no extension added | **Exact alias** of `WriteToFile` |
| `UtilStringFromFile(name)` | `0041e3f0` / `0041f1c0` | reads `user:<name>` (mode 1) in 1024-byte chunks, returns the concatenation | **Exact alias** of `ReadFromFile` |
| `UtilCheckCheatMode()` | `0041db50` / `0041f1e0` | `IsEnabled("cheatmode")` (string `004e9464`, a config/settings value; `--cheat` sets it) | part of `CheckConfig("dev")` |
| `UtilCheckFirstPeek()` | `0041db40` | returns false (`xor al,al; ret`) | C3 reads a setting |
| `UtilCheckCrossSell()` | `0041dbb0` | true when the path `xsell/xsell.xml` resolves (`0044c650`; inference: a file-exists test) | `CheckConfig("xsell")` tests `xsell.lua` |
| `UtilTriggerAd()` / `UtilTriggerCommunityOffer()` | `0041db20` / `0041db30`, jump to `0043e2d0` / `0043e2f0` | `UtilTriggerAd`: `SendMessageA(DAT_005330dc, 0x401, 1, 0)`; `UtilTriggerCommunityOffer`: the same with wParam `2`. `DAT_005330dc` is `GetParent(game window)` (`0043e330`), i.e. the host/launcher window that wraps the game; nothing in this exe reacts, the meaning of 0x401/1/2 is the host's (fact for the message values, read from the listing; **unknown** what the host does). Both are called from Lua with no arguments and the result ignored: `UtilTriggerAd` at `sim/building.lua:226` (`LBuilding:LeaveBuilding`, after `UpdateStandardUI()`), `ui/map.lua:35` and `ui/portview.lua:26` (last statement of the file); `UtilTriggerCommunityOffer` at `ui/mainmenu.lua:245` (last statement, after `UtilDisableWindow("loading")`). Inference: ad / cross-promotion hooks for the retail launcher; with no host they should be no-ops | none |
| `UtilEnableWindow(name)` / `UtilDisableWindow(name)` | `0041de40` / `0041ded0` | `FUN_00421bf0(root, name, -1)` (first window of that name, whole tree below the screen root `WindowManager+0xc`), then set or clear bit `0x100` of the window flags (`+0x28`). Bit `0x100` = enabled. | similar to `EnableWindow(name,bool)`; the SDK `EnableWindow` instead starts from the top modal and, when disabling a Button, also calls its virtual `+0x48` |
| `UtilSetLedgerTab(name)` / `UtilSetButtonOn(name)` | one function `0041ee10` | spider over the whole tree; every `TButton` named `name` gets toggle state **on** (`FUN_004448b0(btn,true)`) | none |
| `UtilSetGlobalString(name,text)` | `0041ef70` | spider; every `TText` named `name` gets `SetLabel(text)` (virtual `+0x68`) | none |
| `UtilSetTextColor(name,{r,g,b})` | `0041ec50` | spider; every `TText` named `name`: colour `(t[1],t[2],t[3],1.0)`, each defaulting to 1.0 | none |
| `UtilPositionWindow(name,x,y)` | `0041e050` | top modal window (`FUN_0042da60`), find `name` below it, set position | none |
| `UtilPulseImage(name,count)` | `0041e1c0` | starts a `TImagePulser` (section 5) | none |
| `UtilToClipboard(text)` | `0041dc40` | Windows clipboard (`TPlatform` call `00426dd0`) | none (`StringToClipboard` is the window-script twin) |

### 2.1 Verdict on the Chocolatier 2 alias list

The Chocolatier 2 exe is packed, so the evidence for a `bsg*` name is its Chocolatier 1 ancestor plus the Chocolatier 2 call sites.

| Chocolatier 2 name | Verdict |
|---|---|
| `bsgCurrentTime` | **exact** alias of `CurrentTime` (and `UtilCurrentTime` is the ancestor) |
| `bsgReadFromFile`, `bsgWriteToFile` | **exact** aliases of `ReadFromFile` / `WriteToFile` (ancestors `UtilStringFromFile`/`ToFile` have the same bodies as the C3 documentation) |
| `bsgRandom(lo,hi)` | equivalent contract (inclusive range) to `RandRange`; formula differs in the 1-in-32768 `hi+1` case. Safe |
| `bsgFloor` | ancestor truncates toward zero. C2 call sites use `bsgFloor(x/y+0.5)` (non-negative), so `Floor` is safe there; inference: implement truncation to be faithful |
| `bsgDollars` | **do not alias** blindly. The ancestor truncates, wraps negatives to 32-bit unsigned and uses string-table formats (C2's `strings.xml` does define `formatThousands/Millions/Billions`). Equal to `Dollars` for non-negative integers if the C2 strings are used. Implement from section 2 |
| `bsgSaveGameString(data)`, `bsgLoadGameString()` | C2 call sites (`sim/sim.lua:329`, `:354`) take 1 and 0 arguments, which matches **Chocolatier 3**, not Chocolatier 1 (C1 takes a mode string, pref `save_<mode>`). Alias is right |
| `bsgNextSaveFileName()` | not an alias of `NextSaveFileName`: that one filters `.choco3`. Needs `.choco2` (the Lua appends it). Shape from `UtilNextSaveFile` above. Dev-only |
| `bsgTimeStamp()` | ancestor `UtilTimeStamp`: `%Y.%m.%d-%H.%M.%S`, local time, no args. No C3 equivalent. Dev-only (`dev/devmisc.lua`) |
| `bsgCheckConfig(key)` | merges `UtilCheck*`; see the key mapping above. Keys are the engine's, not read here |
| `bsgDate(weeks,days)`, `bsgShortDate`, `bsgGetMaxScroll`, `bsgSetStartLine`, `bsgSetGlobalLabel`, `bsgAddSprite`, `bsgSin/bsgCos`, `bsgMod` | **no Chocolatier 1 counterpart**: Chocolatier 1 formats dates in Lua (`sim/sim.lua` month tables), its ledger scrolls through `UpdateDynamicWindow` plus Lua, and it has no sprite-add native. These cannot be settled from this binary; keep them stubs. C2 call-site facts: `bsgDate(gSim.weeks, gSim.days)` (`sim/sim.lua:768`), `bsgDate(n, 0)` (`sim/quest_functions.lua:67,76`), `bsgShortDate(gSim.weeks - 1, 0)` (`sim/travel.lua:377`), `bsgDate(100, 2)` (`ui/ledger_badge.lua:41`) |

## 3. `TFrame` (the `Frame` window)

Class `TFrame`, vtable `004e800c`, constructor `004115a0`, Init `00411510`, Draw `00411b60`, factory `00412290`.

**Keys read from the Lua table** (Init): everything `TWindow` reads (`x y w h name fit ...`) plus `color` (a table of 4 floats, default
`1,1,1,1`; stored at `+0x68..+0x74`) and `hollow` (bool, default false; `+0x64`). Fact from the 24 Chocolatier 1 call sites: only `x y w h
color name fit` occur; `hollow` is never used. **There are no `corner`, `left`, `top`, `ornament` or `empty` keys here.** Those belong
to the Chocolatier 2 class, which is a different implementation (its images come from the style, `control/frame_*`).

**Hard-coded images** (loaded in the constructor with the texture cache, `FUN_0042aa80`, so lookups are case-insensitive): `image/frameul`
(20x24), `frameur` (23x24), `framell` (23x27), `framelr` (25x27), `frameh` (34x12), `framev` (12x35). Four distinct corner images, one
horizontal edge tile, one vertical edge tile. Nothing is mirrored. The fill inset field `+0x78` is the constant 5.

**Draw, in order.** W and H are the window's width and height from its absolute rectangle (`00421060`). Every image is drawn with
`Draw(x, y, 1.0, 1.0, 0, 0)` where `(x, y)` is the **centre** of the image in window-local coordinates (so each rectangle below is given
as top-left plus size). It is bracketed by `Begin2d`/`End2d` when not already in a 2D batch.

1. **Fill**, unless `hollow`: `FillRect` of the rectangle `(5, 5, W-10, H-10)` read as `(left, top, right, bottom)`, in `color`.
   Note: that is how the call is built (`inset`, `inset`, `W-2*inset`, `H-2*inset` stored in the four slots and passed to a routine
   that adds the window origin to slots 0 and 2); read as right/bottom it leaves a 5 px margin on the left and top and a 10 px one on
   the right and bottom. Whether the last two are width/height instead is not proven (`0041fdf0` forwards them to `0045ac10`, not read). The
   border art covers the difference either way.
2. **UL** corner at `(0, 0)`.
3. **Top edge**: `x = UL.w`; while `x < W`: draw `frameh` with its left at `x`, top at 0; `x += frameh.w` (34). **Tiled, not stretched**;
   the last tile may run past `W` (nothing clips it here; UR is drawn later over its end).
4. **Left edge**: `y = UL.h`; while `y < H`: `framev` with left at 0, top at `y`; `y += framev.h` (35).
5. **Right edge**: `y = UR.h`; while `y < H`: `framev` with its right edge at `W` (centre `W - w/2`), top at `y`; `y += framev.h`.
6. **UR** corner with its right edge at `W`, top at 0.
7. **LL** corner with its left at 0 and its bottom at `H`.
8. **Bottom edge**: `x = LL.w`; while `x < W`: `frameh` with left at `x` and bottom at `H`; `x += frameh.w`.
9. **LR** corner with right edge at `W`, bottom at `H`.

Corner order is therefore UL, (top, left, right edges), UR, LL, (bottom edge), LR. The last bottom tile can overlap LR, which is drawn after
it. It does not consume mouse events by itself (no hit-test override in the vtable; inference from the vtable having only the base
entries for those slots). For the Chocolatier 2 design: the tiling rule (advance by the tile size from the corner's size, stop at
`>= length`) and the fill-then-edges order carry over; the Chocolatier 2 corner images (19x15), `left` (19x64), `top` (64x15) and the 128x33 ornament are not in
this class, so how the single `corner` image is placed at the four corners (mirrored or rotated) is unknown here.

## 4. `TDynamicWindow` and `UpdateDynamicWindow`

Class `TDynamicWindow`, vtable `004e6f9c`, Init `00409970`, PostInit `004096f0`, rebuild `00409560`. Lua keys: `x y w h name`, `contents` (string:
a Lua file path, e.g. `ui/ledgertabs.lua`, `ui/receipt.lua`, `ui/farmreceipt.lua`) and `ontext` (bool).

- **Create**: Init stores `contents`; if `ontext` is true it makes a `DynamicWindowTextUpdater` (a `TMessageListener` registered with the window
  manager, `004097e0`). PostInit calls the base post-init, deletes children (`00422340`), then runs `contents` as a script with this window pushed as
  the current parent (`0042d2d0` = push parent, load script file, pop), then sets a window-manager dirty flag (`0042d3e0`, `+0x27 = 1`).
  So the script's `MakeDialog`/`Text`/... build **children of the dynamic window**.
- **`UpdateDynamicWindow(name)`** (`00409740`): walks the tree from the screen root with an `UpdateSpider`; every `TDynamicWindow` whose name
  equals `name` is rebuilt by the same routine (`00409560`): delete all children, re-run `contents`. It recurses into children. The Lua keeps no handle;
  59 call sites pass a name (`"ledgertabs"`, `"receipt"`, `"factory_status"`...). It returns nothing.
- **`ontext`**: the updater listens for `TMessage` type `7` and rebuilds its window whenever one arrives (`00409890`). Inference: type 7 is
  the text-edit "changed" message (the buy/sell receipt uses `ontext=true` and holds the quantity `TextEdit`). Not verified.
- The script reads Lua globals, so rebuilds always show current state. There is no diffing.
- Chocolatier 2's `UpdateDynamicContent` is the same idea (not read).

## 5. `TSimpleAnimation`, `TImagePulser`, `TRolloverPopup`

**`SimpleAnimation`** (`TSimpleAnimation`, vtable `004e6484`, Init `004016d0`, factory `004015b0`). Keys with defaults: `image` (still
bitmap), `anim` (a sprite-sheet XML such as `image/buttonhighlight.xml`; wins over `image`), `scale` (1.0), `animx` (0), `animy` (0),
`xtile` (8000), `ytile` (6000), `flip` (bool), `path` (table, becomes a path object: the exe has `LinearPath` and `CircularPath` with keys
`startAngle radius center`), `fps` (10; frame period `1000/fps` ms), `random` (bool, on: randomises the start). Size defaults to the image
size. The window records `Timer()` at creation. The per-frame draw was not read: how `xtile/ytile` wrap and what `random` randomises is unknown.
Used on the main menu (`image/buttonhighlight.xml`, `fps=12`, and `scale=0.85` with `fps=5`) and port scenes. Implemented in `games/choc1/SimpleAnimation` (what is drawn and what is left out: `docs/re/choc1-fit.md` section 6).

**`TImagePulser`** (`UtilPulseImage(name, count)`, ctor `0041ae50`, tick `0041aa30`, vtable `004e8a34`). A task scheduled every **100 ms** (repeat).
Each tick finds the `BitmapTint` named `name` and sets its tint to grey `v` with alpha 1, `v` taken from a 10-entry cycle of doubles at
`00525580`: `1.0 0.9 0.8 0.7 0.6 0.6 0.7 0.8 0.9 1.0`, index advancing mod 10 (a 1 s period). `count` is decremented each tick; at 0 (or if the
window is gone) the tint is reset to 1.0 and the task ends. With `count == 0` the task depends on a flag that this constructor leaves null (inference:
never used; the only call is `UtilPulseImage(name, 30)` in `sim/port.lua`, i.e. 3 s).

**`Rollover` / `TRolloverPopup`** (vtables `004e8afc` / `004e8a4c`, Init `0041b960`). `Rollover` keys: `x y w h image target
script ("ui/rollover.lua") clicksound popupsound hotSize (40) hotWidth (= hotSize) hotHeight (= hotSize) xOffset (20) yOffset (0)
xAbsolute (-1) yAbsolute (-1) area (table x,y,w,h)`. The two sounds are loaded as sound category 1000 (SFX). A click posts
`DispatchRolloverClick` through `gCommandQueue` (string at `004e8ab4`); the popup child's name is `<name>_rollover`. Behaviour by name
(inference, hover timing not read): hovering the hot area creates a `TRolloverPopup` that runs `script` with `target` and shows the popup offset by
`(xOffset, yOffset)` or at the absolute position when `xAbsolute/yAbsolute >= 0`, playing `popupsound`.

**Money counter** (not asked but needed by every screen): `SetUpdateRate(hz)` runs a repeating task every `1000/hz` ms (`0` stops it; Lua
passes `kDefaultUpdateRate = 24`, so 41 ms). `SetMoneyDisplayTarget(n)` plays `audio/sfx/money_earned.ogg` when `n` is above the current target
and the counter is enabled, then sets a step of `(n - shown) / (3*hz)` (at least 1) and counts toward `n`, clamping at the target. `SetMoneyDisplay(n)`
snaps. Each change re-formats with `UtilDollars` and sets every Text named `money` (via `UtilSetGlobalString`'s spider).

## 6. Audio (`TChocoSound`, vtable `004e69e8`, ctor `00406340`)

Sound categories passed to the loader (`00434360(path, flag, category)`): **1000** sound effects (flag 1), **1001** music (flag 0), **1002**
ambient (flag 0). Music volume is the user pref `musiclevel` (default **750**) divided by 1000.0, applied to category 1001 when a track
starts.

- **`PlaySound(name)`** (`004041d0`). Preloaded at construction (cached in the object): `open` = `audio/sfx/window_open.ogg`, `close` =
  `window_close.ogg`, `charopen` = `character_dlog_open.ogg`, `charclose` = `character_dlog_close.ogg`, `boat` = `ship.ogg`, `train`,
  `airship`, `fanfare` = `medal_fanfare.ogg`, `mcfanfare` = `win_game_fanfare.ogg` (the mapping follows the load order; the strings `004e691c..`
  match the names). Also preloaded: `typing_01..05.ogg` (used by the text typewriter, not by `PlaySound`). Any other name is loaded as
  `audio/sfx/<name>.ogg` each time and played once (no loop). The Lua call sites use only `open`, `fanfare`, `mcfanfare`. Misses are
  silent (a null sound object is not played). Note the `audio/` prefix: it is Chocolatier 1's archive layout (`audio/sfx`, `audio/music`).
  Chocolatier 2 calls pass bare names (`"buy"`, `"ui_click"`) and one `"sfx/lab_select.ogg"`, so its native is not this one.
- **`StartMusic(file)`** (`004062d0` -> `004061d0`). `file` is relative to `audio/music/` and includes the extension (`menu.ogg`,
  `gameplay1.ogg`, `storyloop.ogg`, `minigame.ogg`). It plays **once, not looped**. State machine at `+0x9c`: 0 idle, 1 playing, 2 fading
  out, 3 waiting for a delayed track. If state is 1 and `file` equals the playing file: nothing. Otherwise the queue is cleared; in state 0 or 3 the
  track starts immediately; in state 1 the current track is **faded out** and `file` is queued with delay 0, so the new track starts when the
  old one has stopped (no overlap crossfade). `StartMusic("")` = fade out and stop (`StopBackgroundMusic`).
- **Fade** (`TChocoMusicTicker`, `00405c40`): a task every **100 ms**; `elapsed += 100`; volume = `level - level*elapsed/1000` (a linear
  1000 ms fade of category 1001); when it reaches 0 the sound is stopped and state becomes 0.
- **`QueueMusic(file, seconds)`** (`00406100`). Idle: starts immediately and the delay is ignored. Playing/fading/waiting (states 1..3): appended to
  the queue (list at `+0xb4`) with `seconds`. When the current track ends (`TChocoMusicEndCallback` -> `00405d90`, sets state 0) or a wait finishes,
  `00405a20` pops the first entry; with a non-zero delay it waits `seconds*1000` ms (a one-shot task, state 3) and then plays; otherwise plays. So
  **Queue means "play this after the current one, after N seconds of silence"**, and looping is done by the Lua re-queuing the same file
  (`ui/mainmenu.lua`: `StartMusic("menu.ogg")` then `QueueMusic("menu.ogg", 2)` twice). Lua helpers: `BackgroundMusicNow(f)` = `StartMusic(f or
  random of 4 files)`, `BackgroundMusic(f, s)` = `QueueMusic(f or random of 3 files, s or random 6..10)`, files `gameplay1..3.ogg`, `storyloop.ogg`.
- **`SetAmbient{name=..., loop=...}`** (`00404af0`, takes a **table**). Empty or missing `name`: stop the ambient. `name` equal to the current
  ambient name (`+0x74`): do nothing. Otherwise store the name and, if `loop` is empty, stop; else load `audio/sfx/<loop>.ogg` (category 1002) and play it
  **looped**. The Lua tables are `kPortAmbient = {name="port", loop="port_loop"}`, `kShopAmbient` (`chocolate_shop_loop`), `kMarketAmbient`
  (`marketplace_loop`), `kFarmAmbient` (`farm_loop`), `kSaloonAmbient` (`saloon_loop`), `kFactorySetupAmbient`, `kMapAmbient`. Chocolatier 2's
  `SetAmbient` is called with a string (`port.ambient`) or `""`, so it is not this signature.
- **`TogglePause`**: the factory mini-game's own pause (section 10), which also pauses all sounds. There is no global pause native.
- Volume prefs per user: `soundlevel`, `musiclevel`, `ambientlevel` (all default 750) and `soundoff`; the Options screen writes them
  (section 9). `SetCurrentUser` applies a user's levels (from the C3 documentation; not re-read here).

## 7. Screen globals

Fact: the Chocolatier 1 exe sets **none** of `gScreenWidth`, `gScreenHeight`, `gRealXOffset`, `gRealYOffset`, `gRealWidth`, `gRealHeight`. None of
these strings occurs in the exe, and none of the 93 Lua files reads them (Chocolatier 1 hard-codes `800`, `600`, `kMax`, `kCenter`). They are a
Chocolatier 2 addition set by its (packed) exe, so their values and timing cannot come from here; the design's `800/600/0` is an inference from the
game's resolution (`readme`: 800x600). The globals Chocolatier 1 does provide to Lua are: the prelude constants (`kButtonPress kPressAnyKey
kQuitNow kModalClosed gReturnValue`, and from engine values `kCheatMode kComputerId kInstallKey kGameName kGameVersion kEncryptionKey
kHiscoreLocalOnly kHiscoreAnonymous`, strings at `00527444..00527548`), `gDialogWidth/gDialogHeight` (set by `MakeDialog`'s generated string `004fe8e4`),
`gCommandQueue`, `gMessageQueue`, `gMessageCommand`, `gMessageToQueue`, `gYieldResult`, `gCurrentDefaultStyle`, `pfMessageMetatable`. Not present
in the exe: `gDevelopmentMode`, `gCheatsEnabled` (Lua reads them as nil unless a dev file sets them).

## 8. The intro, `DoIntro`, `Logo`, `TLogo`

- Chocolatier 1 has **no `DoIntro` and no `MainMenu` native**. Its intro is plain Lua: `ui/intro.lua` is one `MakeDialog` with a full-screen brown
  `Rectangle` (800x600, `BrownColor`), a centred `Text` (label `intro_text`, flags centre/centre, size `kMax`) and a `Button` (`intro_button`, x
  `kCenter`, y 550, default graphics) whose `command` is a closure: `Simulator.EnterPort()` (asserting the port is `sanfrancisco`), then
  `LBuilding:ByName("sftutorplace"):UISelect()`. It ends with `PlaySound("open")`. So the intro is a static text page and a **click on the button is the only
  way out**; no timer, no fade, no auto-advance in the native code. Enter ends it only if the button counts as the dialog's default; `ui/intro.lua` does not set `default` (inference: Enter does nothing).
- `Logo`/`TLogo` (vtable `004e8504`, Init `00415a90`) is the **company-logo builder** used by `ui/logo.lua`/`logobuilder.lua`, not an intro: keys `text
  flags textX textY textW textH gx gy font fontSize fontColor dropColor background (logo/bg1) graphic (logo/logo1)`. `TLogoRefresh` redraws it.
- Close request and exit, from the main loop `004036d0`: message 3 (close) first pauses a running factory mini-game (`00403130`: the top modal named
  `minigame` holding a `TFactoryGame`), then runs the Lua text `if QuitGame then QuitGame() else PostMessage(CreateNamedMessage(kQuitNow,
  'Quit')) end` (string at `004e6768`). Key code 27 (Escape, `DAT_00525cdc`) and window deactivation (message `0x11`) also pause the mini-game.
  Message `0x12` stores the fullscreen pref (`0041c040`). When the loop ends it runs `if bsutil then bsutil.SaveGame() end` and shuts down: **Chocolatier 1
  saves on exit** (via Lua `bsutil.SaveGame`, which logs the score and calls `gSim:SaveGame()` when a game is loaded).
  The window title is `GetString("windowtitle")`; the cursor is `cursor/cursor`.

## 9. `ChoosePlayer`, `CreditsWindow`, `HiscoreWindow`, `OptionsDialog` as the Lua sees them

All four are container windows: Lua builds `x y w h` plus children and the native finds children **by name**.

- **`ChoosePlayer`** (`TChoosePlayer`, vtable `004e6bd4`; ctor registers `UpdatePlayerButtons(selected)`). Up to **10** players (slots 0..9). Child
  names: `player%d` (radio Button per slot), `label%d` (Text), `edit%d`, `delete%d`, `newplayer` (and the label string `opt_newplayer`). `UpdatePlayerButtons(sel)`
  (`00407180`): `newplayer` is disabled when `GetNumUsers() >= 10`; for slot `i`: `i < n` shows the slot with `GetUserName(i)` as label, enabled; `i == n`
  shows the "new player" entry (label `opt_newplayer`, radio off); `i > n` disabled. The selected slot's radio is set on and its `edit%d`/`delete%d` are enabled,
  the others disabled. Lua calls it 4 times with the selected index. Lua also uses `GetNumUsers GetUserName GetCurrentUser SetCurrentUser CreateNewUser
  DeleteUser ChangeUserName IsNameInUse`.
- **`CreditsWindow`** (`TCredits`, vtable `004e6cb4`, Init `00407b00`). Keys: `file` (`assets/credits.txt`), `font`, `fontsize` (26), `fontcolor` (a Color),
  `headercolor` (a Color; converted to `%02x%02x%02x%02x` for `<font color='...'>` markup), `columnwidth` and `columngap` (50 in the Lua), `time` (20000 ms in the
  Lua) and `intropause` (3000 ms in the Lua). A "Chocolatier version <version>" line is appended to the text. Scroll (read in `00408470`): waits
  `intropause` ms, then scrolls `H` pixels (H = column text height / 2 in the exe) per `time` ms, looping; see `choc1-options-credits.md`.
- **`HiscoreWindow`** (`THiscore`, vtable `004e821c`). Natives while it exists: `GetState()`, `ScoreAvailable()`, `UpdateText()`; the submit dialog (`THiscoreSubmit`)
  adds `SubmitToServer` (4 args) and `SubmitMedalsToServer`. The window drives rows by name (`name%d score%d p1_%d`), panels `leftpanel rightpanel
  rightpanelsmall`, texts `gamemode category yourrank yourrankglobalinfo congratulations topplayers medals_done eligible eligibleasterisk server error`, buttons
  `viewlocal submit scrollup scrolldown categoryleft categoryright gamemoderight gamemodeleft` and sets Lua globals by evaluating text (`gEligibleScore=%d`,
  `gAccountEdit`, `gPassEdit`, `gNameEdit`, `gRemember`, `SwitchModes(%s)`, `UpdateButtons();`). Local scores live in `user:hiscore.dat` (checksummed),
  global ones need the online server and are out of scope. State enum: see the Chocolatier 3 notes (`docs/re/hiscore.md`); not re-verified for Chocolatier 1.
- **`OptionsDialog`** (`TOptions`, vtable `004e8844`, PostInit `004186b0`; registers `SaveVolumes()`). After the children exist it initialises them from prefs of the current
  user (`curuser`): button `fullscreen` toggled to the current fullscreen state; button **`mute`** (not `soundoff`: that is the pref) toggled when pref `soundoff == 1`; sliders `sfxslider` (`soundlevel`),
  `musicslider` (`musiclevel`), `ambientslider` (`ambientlevel`), each default **750**. `SaveVolumes()` writes the three slider values back. `ToggleFullScreen()`
  returns a bool, `ToggleSoundMute()` flips `soundoff`.
- `Slider{...}` is separate (`TSlider`): keys `hslider railtop railmid railbot sliderimage sliderrollimage yoffset scale name`.

## 10. `TFactoryGame` and `TDiceGame` (summary for design)

**`DiceGame`** (`TDiceGame`, vtable `004e6de4`, ctor `00408fa0`, Init `00408810`, tick `00408b40`). Lua keys `x y w h` with children `die1..dieN` (Bitmap windows;
N found at creation, inference from the field at `+0x5c`), `win_message lose_message draw_message stop_label`, plus buttons `stop`, `leave`, `leave_label`, `intro` driven with
`UtilEnableWindow/DisableWindow`. Images `image/die%d`; sounds `audio/sfx/dice_game_roll|win|lose|tie.ogg`.
- `RollDice(betText)`: fills every die with `rand() % 6`, stores `betText`, zeroes the elapsed counter, plays the roll sound and starts a repeating task every **62 ms**
  (`0x3e`).
- Tick: `elapsed += 62`. For `elapsed <= 2000` all dice show random faces. After that one die stops every **750 ms** (`0x2ee`): `remaining = N - (elapsed - 2000) / 750`. Dice with
  index `< remaining` show a fresh random face each tick, the others show their final value. `StopDie()` (Lua "stop" button) jumps `elapsed` to the next stop time
  (`(N - (remaining-1)) * 750 + 1938`).
- When `remaining` reaches 0: stop the sound, disable the stop control, compare sums: player = dice 0 and 1 values, house = dice 2 and 3 (four dice). Win: play
  win sound and run `gSim:AdjustMoney(<bet>,'dice_logwin')`; loss: loss sound and `gSim:AdjustMoney(-<bet>,'dice_logloss')`; tie: tie sound, no money. The command is run with
  `DoString` (`0043b220`). Which two dice belong to the player is read from field offsets (`+0x60/+0x64` vs `+0x68/+0x6c`); the pairing is an inference.

**`FactoryGame`** (`TFactoryGame`, vtable `004e740c`, ctor `0040e940` (7.9 KB), Init `0040b080`, PostInit `00410830`). Lua keys: `x y w h type recipe product character firstMiss
port prodname milkbar trayCount ingredientRate (350) speedup (1.1) startspeed (1.0) maxspeed (8.0)`; children found by name `counter timer keeper gamestart gameend pause paused
pause_label`. The game is a rotating tray wheel (angles in radians, radii 225 and 50) onto which ingredient pictures (`item/<name>`) are shot with a gun cursor
(`cursor/gun_aimer?simple`, hotspot 16,16); sprite sheets `image/gun_shoot.xml`, `image/powerup.xml`; art `image/fac_*`, `image/tray%lu`, `image/traycomplete%lu`,
`image/missed_ingredient`. Sounds under `audio/sfx/`: `countdown_tick minigame_starting_bell minigame_load_launch ingredient_success_01..05 ingredient_failure_01..08
template_success template_complete factory_snatch factory_shut_down powerup warning_bell mini_game_production_over`; music `minigame.ogg`. Natives while it exists:
`StartGame()` (sets the cursor, enables the pause button, **starts a 10 ms game tick**, starts `minigame.ogg`), `RestartGame()`, `GetProductCount()`, `TogglePause()` (state 3 = paused;
state 5 = over, in which case it is ignored; pausing also pauses all sound, inference from the call `00443ea0(soundmgr, 1, -1)`). Lua also calls `SetAmbient(kFactorySetupAmbient)` and `BackgroundMusicNow`. Recipes
for the speech lines (`chartext`: `production_awesome|high|med|low|stopped`, `wasted_once`) are chosen from the production ratio. Everything else (spawn rate maths, scoring,
`GetProductCount` meaning, speed curve) was not decoded; `docs/re` has no full account of this class yet.

## Unknown

- `TFrame` fill rectangle: whether slots 2 and 3 are right/bottom or width/height (section 3), and whether the renderer clips tiles that overrun `W`/`H`.
  How the Chocolatier 2 `Frame` places its single `corner`, `left`, `top` and `ornament` images (a different class, packed exe).
- `ontext` message type 7 identity; `TRollover` hover delay and popup placement; `SimpleAnimation` draw, `xtile/ytile` wrap and `random`; `Finale`, `MapScreen` internals.
- What the host does with `SendMessageA(parent, 0x401, 1|2, 0)` (`UtilTriggerAd` / `UtilTriggerCommunityOffer`, `0043e2d0` / `0043e2f0`); whether `DAT_005330dc` is null when the exe runs without a host (then `SendMessageA` fails harmlessly); the exact test inside `0044c650` used by `UtilCheckCrossSell`.
- The Chocolatier 2 definitions of `bsgDate`, `bsgShortDate`, `bsgGetMaxScroll`, `bsgSetStartLine`, `bsgSetGlobalLabel`, `bsgAddSprite`, `bsgSin/Cos`: nothing in this binary.
- Whether Chocolatier 2's exe sets `gScreenWidth` and friends to 800/600/0 (assumed); `SetMusicState` states `factory_wait`, `resume`, `silent` (Chocolatier 2 only).
- Full decode of `FactoryGame` (tick rule, scoring); the `Hiscore` state enum for this build; `LogScore`'s argument handling beyond the 3-argument call shape.
- How the `%%` in the `format*` strings is resolved (section 2, `UtilDollars`).
