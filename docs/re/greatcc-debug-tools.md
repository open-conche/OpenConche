# Great Chocolate Chase: developer and debug tooling

Scope: everything in the original that exists only for the developers: the Ctrl+C console (`scripts/console.lua`, `ShopWindow::DoCommand`), the game-screen Debug button (`ShopWindow::Debug`), the main-menu debug button (`TMainMenu::MainDebug`, `VAchievementManager::AcquireAll`), the achievement cheat window (`VChooseAchievements`, `scripts/cheats_achievements.lua`), the Ctrl+T / Ctrl+S sprite overlays (`VSpriteManager::OnKeyDown`, `DrawDebug`), `VSprite::DrawRect` and the `zones_debug` drawing. Out of scope: online hiscore submission, upsell/xsell links, `DoMetrics` and the `*ScreenTime` states. `DoCheatGame` / `DoCheatMap` are built already and are not repeated.

Sources: `re/greatcc/decomp/*.c` (Ghidra of the Mac i386 binary), `re/greatcc/index.tsv`, the binary itself (`otool -tV`, vtables read from `__const` as `base + 8 + slot`, because the vtable symbol points 8 bytes before the first slot), the Mac `assets/scripts/*.lua`, the Windows build (the Windows install, `settings.xml` decoded: ZXML header of 12 bytes then zlib). Existing fact files extended, not repeated: `greatcc-boot-and-natives.md`, `greatcc-sprites-and-zones.md`, `greatcc-meta-flow.md`, `greatcc-shop-simulation.md`, `window-system.md`.

Legend: **F** fact (decompile, disassembly or file bytes; function or `file:line` cited), **I** inference, **U** unknown. Addresses are Mac virtual addresses, "+0xNN" are Mac i386 object offsets. `lua:N` = line in the Mac plain-text script of that name under `assets/scripts/`.

## 0. Summary table

| Piece | Reachable in the shipped game? | Gate | Worth building |
|---|---|---|---|
| Console (Ctrl+C -> `console.lua` -> `DoCommand`) | yes, in the shop only | **none** (no cheatmode, no `game_debug`) | yes: small, 10 commands, no output |
| `game_debug` button -> `DoDebug` | only with config `game_debug` == `"1"` | string compare in Lua | cheap; effect is only a zones reload |
| `mainmenu_debug_button` -> `MainDebug` | **no**: the button is hidden unconditionally and its Lua command is `DoModal("scripts/upsell.lua")`; nothing in any script calls `MainDebug` | n/a | no (keep the native registered) |
| `VChooseAchievements` window | yes, with `cheatmode`=1: Cheats button -> "Choose Achievements" | `IsEnabled(kCheatMode)` | moderate |
| Ctrl+T / Ctrl+S overlays | yes, shop and world map | **none** | Ctrl+S easy; Ctrl+T needs texture accounting the engine lacks |
| `VSprite::DrawRect` / +0x17c rect | **no**: no caller, nothing ever writes the rect | n/a | no |
| `zones_debug` drawing | yes, with config `zones_debug` == 1 exactly | read once at `ZoneController` construction | cheap (spec already in `greatcc-sprites-and-zones.md` 6.4) |

## 1. Developer console

### 1.1 How it opens (F)

* `ShopWindow::OnKeyDown(char c, uint mods)` (0x3f7ce, 257 bytes). If `c` is `'c'` or `'C'` **and** `mods & 2` (the ctrl bit, `window-system.md` 6.2: bit0 shift, bit1 ctrl, bit2 alt) it opens the console and returns true. Any other key, or `c` without the ctrl bit, is forwarded to `VSpriteWindow::OnKeyDown` (which feeds the Ctrl+T/S overlays, section 3). Extra modifiers (shift, alt) do not prevent it. There is **no** `cheatmode`, `game_debug` or any other test. It is a C++ key handler; no Lua opens the console (grep of all scripts: only `console.lua` itself mentions `console`).
* Order of operations: allocate a bare `TWindow` (size 0x30), `TWindowManager::RunScript(thatWindow, "scripts/console.lua")`, look up child `console_window` in it (unlimited depth), `FindParentModal(shop)`. If both the modal and the child exist: the child is **orphaned from the temporary window** (vtable +0x28, `TWindow::OrphanChild`) and **adopted by the shop's modal** (modal vtable +0x24, `TWindow::AdoptChild(child, true)`). In every case the temporary window is then deleted (vtable +0xc). So the console is a plain child of the modal that holds the shop (the game modal), not a new modal: the shop keeps drawing and updating behind it. The console is not exclusive and does not pause anything (I: the game clock runs; the shop `OnTaskAnimate` is not stopped by this code).
* Nothing checks for an existing console: pressing Ctrl+C again builds a second `console_window` (I: both would be children with the same name).
* Keyboard focus (F/I): `console.lua` ends with `SetFocus("command_textedit")`. `TWindowScript::SetFocus` (0xda40e) resolves the name under the **script's window-stack top**, which during `RunScript(tmpWindow, ...)` is the temporary window, so the manager focus (+0x38) is set to the TextEdit while it is still in the temporary window, and it stays pointing at the same object after the adoption. So typing works immediately (F for the lookup root; I for the result). `TTextEdit` sets window flag 0x2 (focusable, `TTextEdit.c:504,536`) so a mouse click on it also focuses it.
* Because focus moves to the TextEdit, the shop's own `OnKeyDown` (Ctrl+C, Ctrl+T, Ctrl+S) stops receiving keys until focus returns (I). What `SafeDestroyWindow` does to the manager focus when the focused TextEdit is destroyed (closing the console) was not traced: **U** whether the shop regains key focus after the console closes (if not, Ctrl+C would work only once per modal focus change). The modal's default focus is the shop (`ShopWindow` ctor 0x42a04 calls `TModalWindow::SetDefaultFocus(topModal, this)`, F; `TModalWindow::OnSetFocus` forwards to the default focus child, F), so a modal push/pop that re-focuses the modal brings it back (I).
* **Not usable outside the shop (F)**: Ctrl+C is handled only by `ShopWindow::OnKeyDown`; `DoCommand` is a Lua global bound to the `ShopWindow` instance by its constructor (`lua_pushdirectclosure<ShopWindow, void (ShopWindow::*)(str)>`, 0x42a04 region) and is nil once the shop is destroyed (`greatcc-boot-and-natives.md`). The world map (`VWorldMap`), main menu, prelevel (`VPrelevelWindow::OnKeyDown` overrides and only handles up/down) have no console. A script that calls `DoCommand` with no shop gets a nil call (script error).
* Also reachable while the shop is **not** in a level: the key handler does not look at level state. The shop is only the keyboard focus window while it is the visible game screen (pre-level, running, end-level before their modals take over), so in practice this means "during play".

### 1.2 `scripts/console.lua` layout (F, `console.lua` Mac text; the Windows bytecode carries the same constants)

`require`s `scripts/style.lua`, `chocshopstyle.lua`, `transitions.lua`, then one `MakeDialog` and `SetFocus("command_textedit")` (:96).

| element | keys |
|---|---|
| `RWindow name="console_window"` (:9) | x=10, y=10, w=350, h=150, `alpha=true`. Parent-relative, so after adoption it sits at (10,10) of the game modal (top-left of the screen), over the shop |
| `KSplit9Image` background | image `ui/backgrounds/split9_dialog_small`, x/y `kCenter`, w=350, h=150, x1=y1=x2=y2=40 |
| `RShrinkText` title (:29) | x=12, y=8, w=`kMax-16`, h=40, font `Dialog_TitleFont`, flags `kVAlignCenter+kHAlignCenter`, label `"#Console"` (literal text "Console") |
| `KSplit9Image` field frame (:41) | image `gfx/textfield`, x=`kCenter-5`, y=`kCenter-20`, w=310 (`350-40`), h=32, slices 10/10/10/10 |
| `TextEdit name="command_textedit"` (:53) | x=3, y=5, w=`kMax-3`, h=`kMax`, font `{edwardian_medium, 18, BlackColor}`, `length=200` (max chars), `label = playername` (:61), `disable = "ok"` (:64); the `ignore` line is commented out (:63) |
| OK button (:69-81) | style `CSOvalGreenButtonStyle`, x=`kCenter-60`, y=`kMax-80`, label key `app_ok`, `default=true` (Enter presses it), command (:77) `DoCommand(GetLabel("command_textedit"))` (:79) |
| Close button (:83-93) | style `CSOvalRedButtonStyle`, x=`kCenter+60`, y=`kMax-80`, label key `app_close`, command `DoCommand("closeconsole")` (:91) |

There are **no history lines and no output lines**: the dialog has exactly one input field, a title and two buttons. Nothing is echoed and `DoCommand` returns nothing (F: `void ShopWindow::DoCommand(str)`, and the body contains no log, message or Lua-return call). Pressing OK runs the command and leaves the console open with the text still in the field (nothing clears the field or closes the window); only "closeconsole" closes it.

Details of the three oddities asked about:
* **`playername` (:61)** is a stray global read. It is not set by any script (the only `playername` definitions are `local playername = gEnterNameValue` in `entername.lua:4` and `choosedifficulty.lua:5`, locals), by the prelude, or by C++ (`playername` is not in `strings.tsv`; it is listed as an undefined global in `lua-undefined-globals.tsv:155`). It evaluates to nil, so the field starts with no `label` (I: the TextEdit treats a missing label as empty, as every other TextEdit without one). It was copy-pasted from `entername.lua:192`.
* **`disable = "ok"` (:64)** is inert here (I, with F support): the console uses the plain `TextEdit` type, whereas `entername.lua:184` uses `RTextEditDisable`, the only class that reads `disable` (`greatcc-ui-library.md` 5.3); `TTextEdit.c` has no `disable` key. Also no button in the console is named `ok` (its OK button has label `app_ok` and no `name`). So the OK button is never disabled.
* **:79** `DoCommand(GetLabel("command_textedit"))` sends the raw field text (case preserved) to the shop. The commented-out line above it (:78) shows an older `DoValueCommand` that no longer exists. **:91** `DoCommand("closeconsole")` is the Close button: it is the same native, handled as a command word.

### 1.3 `ShopWindow::DoCommand(str)` (0x43280, 3025 bytes) (F)

**Tokenisation** (F, disassembly 0x4329f-0x433da):
1. An empty string returns at once.
2. Find the first space. The **command word** is the substring from 0 up to that space (the whole string if there is no space), converted with `str::downcase`. Only the command word is lower-cased; arguments keep their case.
3. Every following run of non-space characters is pushed into an argument vector in order; empty pieces (double spaces, trailing space) are dropped. Separator is a single space character only (tabs are not separators; **commas are not**: the command is `choctint <layer> r g b`).
4. Numbers are read with `str::to_int` / `str::to_float` (I: atoi/atof style, non-numeric gives 0; the implementations were not opened).
5. Dispatch is by exact comparison of the command word, tested in this order: `closeconsole`, `choctint`, `setvalue`, `setpos`, `setlayer`, `setvisible`, `getupgrade`, `setdelay`, `setscale`, `addminutes`. An unknown word does nothing. A known word with too few arguments does nothing. Extra arguments are ignored. Nothing is ever reported back.
6. "Sprite" lookups use `VSpriteManager::FindSpriteByName(shopManager (this+0xec), name)`: the shop's own manager only, depth-first, first match, exact (case-sensitive) name, top-level sprites and then their children (`greatcc-sprites-and-zones.md` 2.6). A name that is not found silently does nothing. (Station sprites are named `station01`..`station17` lowercase, F from the shop doc; I: customers and the player have their own names.)

**Commands** (`<>` = required argument, `[]` = optional):

| command | args needed | effect (F unless marked) |
|---|---|---|
| `closeconsole` | 0 | `FindParentModal(shop)`; if present, `GetChildWindow(modal, "console_window", unlimited)`, `OrphanChild(it)` on the modal, then `TWindowManager::SafeDestroyWindow(it)`. With no console present the pointer passed on is null (**U** whether that is safe; not reachable from the UI because the buttons live in the console). |
| `choctint <layer> [r [g [b]]]` | 1 | `type = VChocolateLayer::GetTypeFromString(layer)` (names in `greatcc-machines-and-chocolates.md` section 1: `bucket`/`chocolate`/`ingredient`/`topping`/`infusion` with optional `layer` suffix and optional `k` prefix, case-insensitive; unknown gives -1 and still creates a map entry). Then global `mChocTint[type] = (r, g, b, 1.0)`; missing components default to **1.0** (arg 1 = r, 2 = g, 3 = b; alpha always 1.0). Takes effect only for layers created later (`VChocolate::AddLayer` copies the tint, `greatcc-machines-and-chocolates.md` line 63); existing chocolates are not recoloured. |
| `setvalue <name> <number>` | 2 | `VValueManager::SetValue(name, (float)number, save=false)`: clamps to the value's min/max if it exists; creates a missing value with base = number (`greatcc-boot-and-natives.md` 4.2). Not saved. |
| `setpos <sprite> <x> <y>` | 3 | `VSprite::SetPosition(TVec2(x, y))` (vtable +0x90) then `AttachToAnchor("")` (vtable +0x4c): the sprite is detached from any anchor. |
| `setlayer <sprite> <n>` | 2 | If the sprite has **no parent** (a top-level sprite of the manager) it writes the depth field (+0xa0) directly (the manager sorts top-level sprites by depth each draw); with a parent it calls virtual `SetLayer(n)` (re-sorts the parent's children). |
| `setvisible <sprite> <n>` | 2 | `TSprite::SetVisible(n != 0)`. |
| `getupgrade <name> <category>` | 2 | `VUpgradeManager::EnableUpgrade(name, category, save=true, null spider)`: the permanent grant (state 3) written to the per-location `upgrades_<loc>` block (`greatcc-machines-and-chocolates.md` section 7). Both strings are lower-cased inside. A missing upgrade does nothing. No `VChocolateVendor::UpdateCapability` or map refresh is called afterwards (**I**: the new capability is picked up the next time something refreshes it, e.g. `UpgradeComplete`). |
| `setdelay <sprite> <n>` | 2 | virtual `VSprite::SetDelay(n, true)` (vtable +0xc4: delayed start of the sprite's animation, `greatcc-sprites-and-zones.md` `Update`). |
| `setscale <sprite> <x> <y>` | 3 | virtual `VSprite::SetScale(x, y)` (vtable +0x84). |
| `addminutes <n>` | 1 | Only if the level `RTimer` exists (`this+0x1ac`): its elapsed-ms field (+0x54) `+= to_int(n) * 60000`. Negative values subtract (the field is added as a signed int; I: a result below zero would wrap or show a negative time, untested). No clamp, no end-of-level check here; the next `OnTaskAnimate` tick sees the new elapsed time (I). |

Not present (F, the string table of the binary has no others): no `help`, no `list`, no `win`/`lose`, no money command (money is a cheat button, not a console command).

Output: **none**, in the console or the log (F: the function has no calls to any logging, message-posting or Lua push).

### 1.4 What `DoCommand`'s callers expect (F)

`console.lua:79,91` ignore any return value. Nothing else calls `DoCommand` (`lua-undefined-globals.tsv:19`: 2 uses, both in `console.lua`).

## 2. Debug menu and buttons

### 2.1 `game_debug` button -> `DoDebug` (`ShopWindow::Debug`, 0x3f8ee) (F)

* Lua: `game.lua:212-221` builds `Button{name="game_debug", x=5, y=kMax-100, flags=kVAlignCenter+kHAlignCenter, label="#Debug", style CSMainMenuButton, command = DoDebug()}` as a child of the `ShopWindow`; `game.lua:239` `EnableWindow("game_debug", GetConfig("game_debug") == "1")`. `EnableWindow(false)` clears the enabled=visible bit, so without `game_debug=1` the button is invisible and unclickable. `--config game_debug=1` reaches this (settings.xml ships `<game_debug>0</game_debug>`, Mac and Windows copies identical, F). The string must be exactly `"1"`.
* The native does exactly one thing: `ZoneController::InitializeZones(shop.zoneController (this+0x188), "zones/zones.xml")`. It does not rebuild the level, the sprites, the values, the stations or the HUD. If the controller pointer is null it would crash (it exists after the first level load, F `OneTimeInit`); **U** what happens before the first level load, though the button only exists on the game screen.
* Effects of the reload (F: `InitializeZones` 0x653e8, `Zone::AddPoint` 0x640a4, `ZoneController::AddZone` 0x65280):
  * the **current path list** (controller +0x18) is cleared first. An avatar mid-walk keeps its own current target point but its following hops come from `GetNextPoint()` on that shared list, so it stops being routed (I: it arrives at its next node and has nothing queued).
  * zones are matched **by name** and reused (new names are added), points are matched by name inside the zone and reused (coordinates, entrance/exit membership are updated, pathfinding state reset). So `ZonePoint` pointers held by avatars, stations and customers stay valid and see the new coordinates. Points or zones removed from the file are **not** removed.
  * each zone's connections are cleared and rebuilt from the file; `<crosszone>` connections are re-added.
  * the `disabled` flag on a point is **not** reset by a reload (`AddPoint` calls only `ResetPathfinding`; `ShopWindow::LoadStations` disables point `middle_center` of zone `avatar` once at level load, 0x40fc4 line ~1169; that stays).
  * the file is read through the asset layer (`TXmlNode::ParseFile("zones/zones.xml")`), so it only matters if the file can change between presses (a loose-file Mac install, not the Windows `assets.pfp`).
* The reload does not toggle the drawing: whether zones are drawn is the `zones_debug` flag (section 3.5).

### 2.2 `mainmenu_debug_button` -> `MainDebug` (0x26f54) (F)

* `mainmenu.lua:166-175`: `Button{x=0, y=300, name="mainmenu_debug_button", font={edwardian_medium,18,WhiteColor}, label="#DEBUG", command = function() DoModal("scripts/upsell.lua") end}`. **The Lua command does not call `MainDebug`.** No script anywhere calls `MainDebug` (grep of the Mac scripts; the Windows bytecode of `mainmenu.lua` also has no `MainDebug` constant, F via `strings`, and `MainDebug` occurs only in the binary's native registration). `TMainMenu` registers the native (0x26c66 region, `lua_pushdirectclosure<TMainMenu, void (TMainMenu::*)()>`) and it is simply never invoked.
* `TMainMenu::PostChildrenInit` (0x26dd6) calls `TWindow::SetFlags(child "mainmenu_debug_button", 0)` **unconditionally** (no config check): `SetFlags` only touches bits 8-15, so this clears the enabled=visible bit. Nothing re-shows it: no `EnableWindow("mainmenu_debug_button")` in any script and no other reference to the name in the binary (`decomp/TMainMenu.c:302` is the only one). So the button is never visible, and even a click would open `upsell.lua`. `mainmenu.lua:396-399` only enables the firstpeek/endless/scores buttons.
* `TMainMenu::MainDebug` itself = `VAchievementManager::GetInstance()->AcquireAll()`.
* `VAchievementManager::AcquireAll` (0x3d54): for every category (outer map, string order) and every achievement in it (inner map, string order) calls `AcquireAchievement(name, category)` (0x3cd2): if the achievement exists, `VAchievement::SetAcquired(true)` (0x761ca): store `acquired` (+0x34) = 1, then `UpdateCurrentUser` (0x76114): if the current user's `RState` exists, get the binary block named `"achievements"` (`GetBlock`, 0x7609c: `TSettings` instance +0xc is the user state), write `block[achievementIndex(+0x20)] = acquired`, and call the block's vtable +0x18 on the achievement (I: the block's save/flush hook). So it **is persisted** immediately to the current user's save. It does **not** show a notification, does not touch the backing value (`value`, e.g. `num_bars`), does not play a sound and does **not** add to the manager's "newly acquired" list (+0x30) that `ShowNewAchievements` reads at end of level, so the end-level screen will not announce them. `CheckAllAchievements` afterwards skips them (they are already acquired).
* `ReleaseAll` (0x4082) / `ReleaseAchievement` (0x3f6e) (used by the cheat window): for each achievement, if it has a backing `value` name it sets that `VValueManager` value to **0 and saves** (`SetValue(valueName, 0, true)`), then clears `acquired` and runs `UpdateCurrentUser`. Releasing therefore also **zeroes the saved statistic** (e.g. `total_worth`, `num_served`) so the next check does not re-award it.

### 2.3 `VChooseAchievements` (`scripts/cheats_achievements.lua`) (F)

Class: `VChooseAchievements : VSpriteWindow` (vtable 0x2866e0, ctor 0x8110a; window-type name `"VChooseAchievements"`). Fields: +0xf8 `map<str, pair<str,str>>` (button name -> (achievement name, category)), +0x110 `map<str, shared_ptr<VButton>>` (button name -> toggle). Being a `VSpriteWindow` it also owns a `VSpriteManager` of its own that is unused (the toggles are added to the inner `sprite_window`).

Lua (`cheats_achievements.lua`):
* :4-9 constants `kCheatFail..kCheatHighest` (duplicated from `cheats.lua`), :11-16 `CheatSmallFont`/`CheatButtonStyle`, :17-31 `CheatCheckStyle` = `{baseDir="ui/buttons/", font={edwardian_medium,14,WhiteColor}, flags=kHAlignLeft+kVAlignCenter, type=kToggle, scale=0.80, assets.default = {{state="default", file="cs_check2_off"}, {state="on", file="cs_check2_on"}}}`.
* :32-95 `MakeDialog{name="cheats_achievements_dialog", VChooseAchievements{checkstyle=CheatCheckStyle, x=273, y=5, w=415, h=500, ...}}`: a `KSplit9Image` (`split9_dialog_small`, 415x500, slices 40) background; `RScrollable name="scroller"` x=5, y=5, w=`kMax-10`, h=`kMax-75`, containing `VSpriteWindow name="sprite_window" w=405` and `TSlider name="achievement_slider"` (style `App_SliderStyle`, x=`kMax-30`, y=2, w=25, h=`kMax-2`); then four `Button`s (style `CSOvalLightBlueButtonStyle`), all at y=`kMax-75`: `btn_acquire` x=5 label `#Acquire All` (command posts `CreateNamedMessage(kButtonPress, "btn_acquire")`), `btn_release` x=105 label `#Release All`, `btn_notify` x=200 label `#Test Notify`, and an unnamed Close x=`kMax-115` label `#Close` (`CloseWindow()`). :96 `SetFocus("cheats_achievements_dialog")`.
* `btn_release` and `btn_notify` have **no Lua command**; they work purely through the automatic `kButtonPress` message every `TButton` press sends (target = the button's parent, here the `VChooseAchievements`). `btn_acquire`'s command additionally posts an untargeted `kButtonPress`; that one goes to the focus window (the dialog's modal) and ends in the script queue (`window-system.md` 4.2), so it does not reach the class a second time (I). Net effect: each button acts once.

`PostChildrenInit(style)` (0x815ee):
1. Finds children `sprite_window` (must be a `VSpriteWindow`) and `scroller`; nothing happens if `sprite_window` is missing.
2. `VAchievementManager::GetAllAchievements` (category order `adventure`, `endless` (the `endgame` category is commented out in `achievements.xml`), then achievement name order inside each: 47 + 4 = 51 entries).
3. The style table `checkstyle` of the window is adopted as a `VLuaStyle` (`VLuaStyle::Adopt`).
4. For achievement index `i` (0-based): create `VButton::Create(0, "")`, `LoadFromTable(checkstyle)` (vtable +0x104), `Init()` (vtable +0x28), `SetText(<achievement name>)` (vtable +0x124: the **raw achievement id** such as `delivery10` is the label, not the localized title), set the sprite name (+0xb0) to **`btn_achieve_<name>`**, `SetPosition` (vtable +0x90) to `x = (i % 2) * ((W - 40) / 2) + 20`, `y = (i / 2) * 25 + 20` where `W = right - left` of the `VChooseAchievements` window rect (= 415 with the Lua above, so columns at x = 20 and 207; I for the rect field order, which is `left, top, right, bottom` at +0x10, per `window-system.md`), `SetSelected(acquired)` (vtable +0x12c) so the checkbox starts on for acquired achievements, `AddChild` to `sprite_window` (vtable +0x80), and record (`btn_achieve_<name>` -> button) and (-> (name, category)).
5. `sprite_window.SetWindowSize(W, (count / 2) * 25 + 30)` (integer division: with an odd count the last row sits just inside the extra 30 px), then `FitToChildren` on the scroller's interior window (`scroller +0xec`).

`OnMessage(msg)` (0x81180): handles only `msg.type == 3` (`kButtonPress`), returns true when it handled it, false otherwise; compares `msg.name`:
* `btn_notify`: `GetAcquiredAchievements(list)` then `ShowNotification(list, 3000)`: runs `scripts/rewardnotify.lua`, builds one item per acquired achievement stacked in a single `VRewardNotify` (`greatcc-meta-flow.md` section 6), close-condition argument 3000 (I: milliseconds; the same parameter that is 0 for the end-level "wait for an external flag" case).
* `btn_release`: `ReleaseAll()` then `SetSelected(false)` on every toggle in the map.
* `btn_acquire`: `AcquireAll()` then `SetSelected(true)` on every toggle.
* otherwise: look the name up in the (name, category) map; if found and the toggle exists: a toggle `VButton` flips itself in `VButton::OnClick` (0x6c1c: type 1 or 2 toggles `selected` +0x225 before posting `kButtonPress` with its own name; the message target is the owning `VSpriteWindow`, found through `VSpriteManager::GetParentWindow` by name from the top modal, and the message climbs to the `VChooseAchievements`) and then `OnMessage` reads the **new** state: selected -> `AcquireAchievement(name, category)`, not selected -> `ReleaseAchievement(name, category)` (which also zeroes the saved stat, 2.2).
None of these shows anything in the toggles besides their own state; no end-level "new achievement" list is touched (Acquire never adds to +0x30).

Who opens it (F): only `cheats.lua:137-147`, the `#Choose Achievements` button (x=40, y=225, w=187, h=32, `App_LongButtonGraphics`) of the cheats dialog, with `DoModal("scripts/cheats_achievements.lua")`. The cheats dialog (`scripts/cheats.lua`) is opened by `DisplayDialog{"scripts/cheats.lua", context="game"}` from the `game_cheats` button (`game.lua:224-232`, x=5, y=kMax-270, label `#Cheats`) and `context="map"` from `worldmap.lua:270-281` (x=5, y=5, wraps `SetState("MetaMapScreenTime", 0/1)`). Both are gated by `EnableWindow("game_cheats", IsEnabled(kCheatMode))` (`game.lua:238`, `worldmap.lua:306`); `kCheatMode` = config key `cheatmode`, `IsEnabled` = non-zero integer (`api-windows.md`). So `--cheatmode` / `--config cheatmode=1` is the only gate; works from both the world map and the game screen. Note `cheats.lua:28-31` uses `checkstyle = CheatCheckStyle` on its root `Bitmap`; `cheats.lua` itself does not define that global (it is nil until `cheats_achievements.lua` has run once), harmless.

### 2.4 Other debug-only entry points still not served by the engine (F)

Searched all scripts for `debug`, `cheat`, `console`, `RegisterHotkey`, `GetConfig`, `IsEnabled`, `DebugOut`:
* `console.lua` via Ctrl+C (section 1); `DoDebug` (2.1); `MainDebug` (2.2, unreachable); `VChooseAchievements` (2.3). `DoCheatGame`/`DoCheatMap` done.
* **No `RegisterHotkey` anywhere** in the scripts: no Lua-level debug hotkeys.
* `DebugOut` (`hiscoresubmit.lua:71,73`): SDK log function in the hiscore-submit path, out of scope.
* `cheats.lua` buttons beyond `DoCheat*`: `Choose Achievements` only.
* `worldmap.lua`/`mainmenu.lua`/`game.lua` have no other cheat or debug controls.
* Config keys that exist only for tooling: `cheatmode`, `game_debug`, `zones_debug` (settings.xml, all `0`). `firstpeek` is a demo-mode switch, not debug.

## 3. Debug overlays

### 3.1 Key handling (F)

`VSpriteManager::OnKeyDown(char c, uint mods)` (0x4d8c4, 78 bytes), manager flag word at **+0xc** (zeroed by `VSpriteManager::Reset`, which the constructor calls, `VSpriteManager.c:168`):
* Only if `mods & 2` (ctrl). Then, **two independent tests**: if `c` is `'t'` or `'T'` toggle bit `0x1`; if `c` is `'s'` or `'S'` toggle bit `0x2`. Returns 0 always (the event is not consumed). No `cheatmode`, no setting, no build flag: they work in the shipped game. Extra modifiers are allowed.
* `VSpriteWindow::OnKeyDown` (0x4f3da) forwards `(c, mods)` to its manager (vtable +0x1c) and returns 0. A `VSpriteWindow` therefore handles it only if it receives key events, i.e. is the manager focus window or an ancestor of it (`window-system.md` 6.5: key events go to the focus window and then up the parent chain). Which windows that is:
  * `ShopWindow` (a `VSpriteWindow`): forwards non-`c` keys to `VSpriteWindow::OnKeyDown`, and is the default focus of its modal (F). **Works during play.**
  * `VWorldMap` (a `VSpriteWindow`, no `OnKeyDown` override): sets itself as its modal's default focus in `PostChildrenInit` (`VWorldMap.c:398`, F). **Works on the world map.**
  * `VPrelevelWindow` overrides `OnKeyDown` (up/down scroll only), so no overlay there. `RGame` is never created. `VChooseAchievements`: its inner sprite window is not the focus (the dialog modal is), so no (I).
* The flag is **per manager**, i.e. per sprite window: the shop and the world map each have their own state and toggles are not shared. The flags are not saved and start off whenever a window is created (the shop window lives for the whole session, so they persist across levels, F from `greatcc-shop-simulation.md` section 1).
* Both bits can be set at the same time, but only one text is shown (3.4).

### 3.2 Where it is drawn (F)

`VSpriteManager::Draw` (0x4f0da): `VAssetManager::ClearUsed()`, sort, `Begin2d`, draw all top-level sprites, then `DrawDebug()` through the manager vtable +0x24, then `End2d`. `DrawDebug` (0x4ed9a, 726 bytes) therefore draws above every sprite of that manager, inside the same 2D pass, with no translation (window origin: for the shop and the world map, which fill the screen, the screen's top-left). It runs every frame for every manager but does nothing when the flag word is 0. It is **above** the sprites, **below** later windows drawn after the sprite window, and below `ZoneController::Draw` (3.5).

### 3.3 Ctrl+T: asset status (bit 0x1) (F)

Text = `VAssetManager::GetInstance()->GetStatus(true)` (0x837b2):

```
SystemRam: <mem>
Current VRam: <mem>
Max VRam: <mem>
Peak VRam: <mem>
<w> x <w> : <count>        (one line per texture size, ascending <w>)
```
* Four fixed lines each ending `\n`, then one line per entry of the size histogram (`map<uint,int>` at VAssetManager +0x50), each `"%i x %i : %i\n"` printing the **same key twice** (the decompile passes the key for both width and height; sizes are square power-of-two internal sizes) and then the count.
* `<mem>` = `TexInfo::MemoryToString(bytes)` (0x19a948): `< 1024` -> `"%i bytes"`, `< 1 MiB` -> `"%0.2f kB"` (bytes * 0.0009765625), otherwise `"%0.2f MB"` (bytes * 0.0009765625 squared).
* The four numbers (F, `VAssetManager` fields): `SystemRam` = +0x48, `Current VRam` = +0x40, `Max VRam` = +0x44, `Peak VRam` = +0x4c. Their meaning, F from the update sites:
  * `SetLoaded(name, true)` (0x83a42, first load of an asset's texture, tracked per name in a `map<str, TexInfo>` at +0x68 with a load count at TexInfo +0x24): `+0x48 += sysBytes + vramBytes`, `+0x44 += vramBytes`; unloading the last reference subtracts both. `TexInfo::SetTexture` (0x19aac0): `sysBytes = width * height * 4`, `vramBytes = internalW * internalH * 4` (internal = the GL texture size from `TTexture::GetInternalSize`). So **"Max VRam" is really the running total of loaded texture VRAM** (label vs. meaning, I), and SystemRam counts sys + vram bytes (I: the author's quirk, F for the arithmetic).
  * **The "used" flag**: `SetUsed(name, true)` (0x84c36) is called after drawing from `VObject::Draw` (0x2886f region, `VObject.c:29`) and `VTiledObject` (`VTiledObject.c:374`) with the object's current asset name (+0x198 / asset +0x1bc). If the named asset is loaded and its per-frame use count (TexInfo +0x20) is 0, it adds its vramBytes to `Current VRam` (+0x40), increments the histogram bucket for its internal width, and raises `Peak VRam` (+0x4c) to `max(Peak, Current)`; every call increments the use count. `SetUsed(.., false)` would decrement (no shipped caller). **`ClearUsed()` (0x83974), called at the start of every `VSpriteManager::Draw`, zeroes +0x40, the use count of every TexInfo, and the histogram.** So "Current VRam" and the histogram show only the textures of `VObject`/`VTiledObject` sprites drawn since the clear in this manager's frame (text sprites, `Bitmap` windows and other `VSprite`s are not counted), while Peak, Max and SystemRam persist.
* Needs a texture-memory ledger (`TexInfo`, `SetLoaded` accounting); the engine has none (`docs/design/greatcc.md` section 9 lists `TexInfo` as not reproduced). A reimplementation could substitute the engine's own texture accounting; the labels and format above are what to match (I).

### 3.4 Ctrl+S: mouse and trace sprite (bit 0x2) (F)

Text = `"x: %i\ny: %i\n"` with the manager's last mouse position (+0xd0, +0xd4; `OnMouseMove` stores the incoming point; `Reset` sets both to -10000, `VSpriteManager.c:174-175`, so before the first mouse move the text reads `x: -10000` / `y: -10000`) followed, if the trace sprite (+0xf4, a `shared_ptr<VSprite>`) is non-null, by `"\nTraceSprite: %s"` with the sprite's name (+0xb0). So two lines, then a blank line and the third.
* The point is the **screen/window mouse position** as passed to the sprite manager (the same space hit tests use), not relative to a sprite.
* The trace sprite is set in `VSprite::GetChildAt` (0x4b72c, `VSprite.c:1681`): when a sprite's `GetChildAt` finds a descendant, it stores **itself** (`GetRef()` of `this`) in its manager's +0xf4. Recursion is inner first, so the final value after a hit test is the **outermost** sprite that had a hit descendant under the pointer (the top-level sprite whose subtree was hit); a hit directly on a top-level sprite's own image does not set it (I: from the recursion order). It is updated only when a hit test runs, never cleared (it keeps its sprite alive and can name a sprite that is no longer under the mouse or already removed). No gating by `mouse`/`leaf`.
* **Both bits set**: the Ctrl+S text is assigned with `operator=` after the Ctrl+T text, so it **replaces** it: with both on, only the Ctrl+S text shows. Ctrl+T alone shows the asset text.

### 3.5 Drawing the text (shared) (F)

If the final string is non-empty: `TTextGraphic::Create(text, 300, 400, 0, "fonts/myriad_pro.mvec", 12, color)` (box 300 x 400, flags 0, font `fonts/myriad_pro.mvec`, size **12**) with color **(1, 0, 0, 1) red**. `GetTextBounds(rect, 1.0)` measures it. Then:
1. `TRenderer::FillRect` of `TURect(0, 0, textW + 10, textH + 10)` where `textW = rect.right - rect.left`, `textH = rect.bottom - rect.top`, colour **(0, 0, 0, 1) opaque black**, no texture. A black panel at the top-left of the window.
2. `TTextGraphic::Draw(rect, 1.0, 0, 1.0, null texture)` with a rect (left=5, top=5, right=`textW`, bottom=`textH + 5`) (that is, text starts 5 px in; the right edge is `textW`, not `textW+5`, as in the code; I: harmless for left-aligned text), scale 1.0, int arg 0, float arg 1.0 (the meanings of the last three arguments were not traced; **U**).
3. `TTextGraphic::Destroy`.
The graphic is created and destroyed every frame.

### 3.6 `VSprite::DrawRect` and the +0x17c debug rect (F)

`VSprite::DrawRect(spec)` (0x15c59c, virtual slot +0xb8): converts `spec` to a relative spec, `RUtil::DrawRect(rect at +0x17c, colour (1,0,0,1) red, spec)`, **zeroes the rect** (+0x17c..+0x188), then recurses into every visible child with the same call. `RUtil::DrawRect` (0x7db50) draws a rectangle **outline** as a 5-vertex strip (primitive type 3, i.e. a line strip; I: 1-pixel lines, no width parameter exists) through the spec's transform, via its own `Begin2d` when the renderer is not already in 2D. The rect is (left, top, right, bottom) ints.
* **Nothing ever calls it**: no direct call, no vtable-slot-0xb8 call outside `DrawRect`'s own child recursion, and `VSprite::DoDraw` / `VSpriteManager::Draw` do not reference it (grep over all of `decomp/`).
* **Nothing ever sets the rect**: the only writes to `VSprite` +0x17c..0x188 are the constructor and `DrawRect` zeroing it (the other +0x17c hits in `ShopWindow.c`/`VCustomerVendor.c` are different classes).
* Verdict: dead code in the shipped binary; the previous note "draws red debug rectangles" in `greatcc-sprites-and-zones.md` is true of the function but it never runs. **Not worth building.**

### 3.7 `zones_debug` and `ZoneController::Draw` (F)

* `ZoneController` constructor (0x6445c) reads config `zones_debug` through `TSettings::GetIntConfig(key, 0)` once and stores `value == 1` (so **only 1**; 2 or `true` do not enable it) at +0xc. The controller is created once by the shop's first level load (`OneTimeInit`), so changing the setting later has no effect on that session. It is the same config store as `cheatmode` and `game_debug`, so `--config zones_debug=1` reaches it (the engine's config overrides sit over `settings.xml`; I for the engine, F that the original's `GetConfig` path is shared by all three keys).
* `ShopWindow::Draw` (0x3ff3c, 54 bytes): `VSpriteWindow::Draw` (= `RWindow::Draw`, then the sprite manager draws all sprites, `DrawDebug`, `End2d`), **then**, if the controller exists, `ZoneController::Draw` (0x647d0) through the controller's vtable slot 0. So the zone overlay is drawn **after and above all sprites and after the Ctrl+T/S text panel**. `ZoneController::Draw` itself returns immediately unless +0xc is set. Colours, point squares and lines: `greatcc-sprites-and-zones.md` 6.4 (not repeated). Nothing else calls it (`ZoneController.c` has no other caller).

## 4. Unknown / not determined

* **U** Whether manager focus returns to the shop after the console closes (SafeDestroyWindow / `OnDestroyWindow` behaviour for the focused TextEdit); relevant for a second Ctrl+C.
* **U** What character and modifier bits the Mac and Windows platform layers pass for Ctrl+C / Ctrl+T / Ctrl+S (the tests are `'c'`/`'C'` etc. with `mods & 2`; on a platform that delivers control characters (0x03) the shortcuts would not fire). Windows exe was only string-checked (all the strings exist) and not disassembled.
* **U** `str::to_int` / `to_float` failure behaviour (assumed atoi/atof).
* **U** Whether an existing, still-running console is closed by the first or last of several `console_window` children (`GetChildWindow` order).
* **U** `TTextGraphic::Draw`'s int and second float argument meaning (`0`, `1.0`).
* **U** How the dispatch of key events treats sibling windows (the `TWindow::HandleEvent` sibling-by-point search is also reached for key events with whatever x,y the event carries); `window-system.md` states keys bubble to parents only. This matters only for the unclaimed Ctrl+T/S case when the focus window is not an ancestor of a `VSpriteWindow`.
* **U** Exact time-semantics of `ShowNotification(list, 3000)`'s second argument.
* **U** Interplay of `addminutes` with a negative result or with the level-complete check.

## 5. Implementation notes for OpenConche

* Everything here is small and independent; none of it touches simulation state except `setvalue`, `getupgrade`, `addminutes`, `choctint`.
* Console: a key handler in the shop window (`ShopWindow::OnKeyDown` equivalent: `c`/`C` with ctrl) that runs `scripts/console.lua`, finds `console_window`, re-parents it into the shop's modal, destroys the temp window; native `DoCommand(str)` with the tokenisation in 1.3 (command word lower-cased, args case-preserved, empty tokens dropped) and no output. `closeconsole` removes the first `console_window` under the modal. The shipped Lua needs `playername` to be nil (do not define it) and plain `TextEdit` to ignore `disable`. Keep it ungated (the original is) or gate behind cheatmode as a deliberate, documented deviation (**decision for the owner**).
* `DoDebug`: call the existing zone loader again with `zones/zones.xml` after clearing the path list; keep zones/points by name, clear connections; do not reset `disabled`.
* `MainDebug`: keep registered as a native that acquires everything (nothing calls it). Not worth wiring a button.
* `VChooseAchievements`: needs the `VAchievementManager` list in category/name order, `GetAcquiredAchievements`, `AcquireAchievement`, `ReleaseAchievement` (zeroing saved values), a `VButton` toggle (type 1) with `SetSelected`, and a reward-notification call for `btn_notify`. Acquire must not add to the end-level "new" list.
* Overlays: per-manager flag word with bits 1/2 toggled by ctrl+T/S in the `VSpriteWindow` key hook (return false), drawn at the end of the manager's draw pass: red 12 pt `fonts/myriad_pro.mvec` text at (5,5) on an opaque black rect `(0,0,w+10,h+10)`. Ctrl+S text is simple and has all its inputs already (last mouse position, trace sprite = the outermost sprite whose `GetChildAt` found a descendant). Ctrl+T needs a texture ledger; either skip it or print the engine's own totals in the same format.
* Do not implement `VSprite::DrawRect`.
