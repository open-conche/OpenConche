# The ledger and the remaining game-level natives

Sources, in order of trust: the Mac i386 decompile (`re/mac/decomp/`, addresses below are Mach-O VM addresses), the Mac app bundle's `Contents/Frameworks/` headers, then the Lua. Facts are what the code does. "(inference)" marks something I did not see directly. The companion table is `docs/re/ledger-and-game-natives.tsv` (columns: name, kind, args, returns, address, note). Read `api-globals.md`, `api-windows.md` and `design-questions.md` first; this file only adds to them.

Scope note: online play is out of scope for the engine (the original workbench plan), audio stays silent, and the factory/particle natives are left to Phase 4. Where a native belongs to one of those I give only the signature and what the Lua needs.

## 0. Summary of the surprises

* The "ledger" is **not a book with tabs and pages**. It is the permanent badge-and-dashboard panel at the bottom of the map and port screens: money, date, rank, score per week, the primary-quest line, six factory "dookie droppers", and nine round buttons. The inventory, recipe book, quest log, medals and catalogue are separate Lua dialogs opened by those buttons. There is no page turning.
* The native class `LedgerWindow` draws nothing of its own (`Draw` is a straight call to `TBSGWindow::Draw`). It owns four things: the money counter animation, the quest indicator image and its flash, the list of `DookieDropper` children, and the update dispatch behind `RealUpdateLedger`.
* The ledger is a **process-wide singleton** that outlives screens. `GrabLedger` moves it into the current top modal, `ReleaseLedger` only detaches it. It must be detached before its parent modal is destroyed.
* Of the sound natives, only `SoundEvent`, `ToggleSoundMute`, `SaveVolumes` and `ToggleFullScreen` are Lua-visible. `BSGSetSoundLevel/MusicLevel/AmbientLevel` are C++ functions only.
* Several natives in the stub list are never called by the shipped Lua (`QuitToMainMenu`, `StartMovie`/`StopMovie`/`IsMoviePlaying` apart from the prelude's `DisplaySplash`, `GetTopModalWindow`, `SaveCreationToFile`, `RankNames`, `LedgerUpdateDisplay`). Section 9 lists them.

## 1. What `LedgerWindow` is

Class `LedgerWindow : TBSGWindow` (header `LedgerWindow.h` in the Mac app bundle's `Contents/Frameworks/`), size 0x84 bytes. Members as laid out in the decompile:

| Offset | Member | Notes |
|---|---|---|
| 0x50 | `pMoneyWindow` (`TText*`) | child named `money`, found in `PrepareInstance` |
| 0x54 | `displayMoney` (double) | what the text currently shows |
| 0x5c | `targetMoney` (double) | where the counter is heading |
| 0x64 | `deltaMoney` (double) | per-tick step |
| 0x6c | `pQuestWindow` | child named `quest`. No such child exists in `ledger.lua`, so it is always null and never used |
| 0x70 | `droppers` | `std::list<DookieDropper*>`, filled by `DookieCollector` (a `TWindowSpider` that walks all descendants and pushes every `DookieDropper`) |
| 0x78 | `flashQuest` (bool) | **constructor sets it to true** |
| 0x7c | `flashTimer` (uint ms) | constructor sets 0 |
| 0x80 | `pTask` | `TLedgerAnimTask` |

It is **not** a DSL window type: `AddWindowType` is never called for it (the list in `api-globals.md` section 1 has no `LedgerWindow`). It is created only by `Grab`.

### 1.1 Construction and registration

`LedgerWindow::LedgerWindow` (0x28e54): zero the members above, set `flashQuest=true`, `flashTimer=0`, set the static `pInstance=this`, then register three globals into the script's global table: `RealUpdateLedger` (member `UpdateLedger(str,str)`), `SetLedgerQuestIndicator` (member `SetQuestIndicator(str)`) and `AnimateProduction` (member `AnimateProduction()`). The destructor (0x28d54 / 0x2919c) sets `pInstance=0` and **sets those three globals to nil**.

Consequences for the engine:

* Before the first `GrabLedger`, `AnimateProduction` is nil. `sim/sim.lua` `TickSim` calls it unguarded, so a tick before the ledger exists is a Lua error in the original. The Lua wrappers `UpdateLedger` and `UpdateActiveQuestGoalsComplete` guard with `if RealUpdateLedger then` / `if SetLedgerQuestIndicator then`. Engine: all three are registered always, as no-ops while no ledger exists (a safe superset of the original's register-at-first-`Grab`, clear-on-destruction).
* `Grab` calls `new LedgerWindow` and then `RunScript`, so the three globals appear before `ledger.lua` runs.

### 1.2 `GrabLedger` (0x2901a, `LedgerWindow::Grab`)

Order of operations:

1. If `pInstance` is null (first call):
   1. `new LedgerWindow` (registers the three globals).
   2. `TWindowManager::RunScript(ledger, "ui/ledger.lua")`: the ledger is pushed as the script's window-stack top, so `MakeDialog{ name="ledger", ... }` calls `SetWindowName("ledger")` on the ledger itself, and every top-level element in the file (the background `Bitmap`, then the nine buttons) becomes a **child of the ledger**.
   3. `TWindow::FitToChildren(ledger)`: the ledger's rectangle becomes the union of its children.
   4. `PrepareInstance()` (section 1.3).
   5. Create a `TLedgerAnimTask`, `SetDelay(50, true, true, false)`, and hand it to `TPlatform::AdoptTask`. It ticks every **50 ms** and calls `Animate()` while `pInstance` is non-null; when `pInstance` is null its `Animate` returns false and the task ends.
2. Else (already exists): if it has a parent, remove it from that parent (parent vtable slot 0x28, `RemoveChild`; it does not delete the window).
3. Then in both cases: add the ledger to `TWindowManager::GetTopModalWindow()` (top modal vtable slot 0x24, `AddChild(window, 1)`, the `1` is an unnamed second argument; (inference) a "keep/own" flag) and `TWindow::SetWindowDepth(ledger, 1)`, which moves it to the **front** of its siblings (`window-system.md`, `SetWindowDepth`: 1 = front).

`Grab` does not call any update. The Lua screens call `UpdateLedger("portview")` / `UpdateLedger("mapview")` right after (`ui/portview.lua` lines 107-108 in Reforged, 57-58 in the original; `ui/mapview.lua` likewise).

**Build-time lookups (answered in `game-windows-questions.md` 8):** `ui/ledger.lua` looks up no window by name while it is being built; the only deferred work is the final `QueueCommand(function() UpdateLedger("newplayer") end)`. Attaching the ledger to the top modal before running the file is therefore safe; `FitToChildren` and `PrepareInstance` must still run after the file.

Geometry: `kLedgerPositionX = kScreenCenterX + 3 = 3`, `kLedgerPositionY = kScreenCenterY + 329 = 329` (both centre constants are 0 because screen and game size are both 800x600). `image/badge_and_ledger.png` is 793x271, so the panel covers x 3..796, y 329..600 and sits flush on the bottom edge. Children use ledger-local coordinates; (inference) because `FitToChildren` shifts the origin to the union's top-left, the buttons placed at `kLedgerPositionX/Y + n` end at the same absolute position as the background.

### 1.3 `PrepareInstance` (0x286c0)

1. Read `Player.money` (default 0) into both `displayMoney` and `targetMoney` after a **float32 round trip** (`(double)(float)money`).
2. `pMoneyWindow` = child `money`; `pQuestWindow` = child `quest` (null).
3. Run a `DookieCollector` over the descendants, filling `droppers` in child (document) order.
4. `UpdateLedger("all", "")`.

### 1.4 `ReleaseLedger` (0x2784e) and lifetime

If `pInstance` and its parent are non-null, call the parent's `RemoveChild(ledger)`. Nothing else. The window and its anim task keep running; `GrabLedger` reuses them. `ChocoThreeMain` registers `Grab` and `Release` as plain functions (the singleton is static, no `this`).

Where Lua calls them:

| Call | Where |
|---|---|
| `GrabLedger()` | last statements of `ui/portview.lua` and `ui/mapview.lua`, each followed by `UpdateLedger("portview"/"mapview")` |
| `ReleaseLedger()` | `SwapMapPortScreens` (portview.lua, before `SwapToModal("ui/mapview.lua")`), the exit paths in `ui/mapview.lua` (line 10 original / 20 Reforged), `sim/port.lua` (137 original / 200 Reforged), and dev scripts |

The rule the Lua relies on: **release before the screen that holds the ledger is destroyed**. Destroying a parent modal runs `DestroyAllChildren`, which would delete the ledger; `~LedgerWindow` then nils the three globals and `pInstance`. In that case the next `GrabLedger` builds a fresh ledger (re-running `ledger.lua`), starting a new anim task. An engine should (a) detach on `ReleaseLedger`, (b) tolerate destruction (null `pInstance`, three globals nil), and (c) never destroy the ledger on detach. `LedgerWindow::CleanUp` appears in the header but has no function in the Mac binary (Unknown, section 11).

`LedgerWindow::ToggleForeground` (0x274bc) is called from the platform event loop (event 0x12): if the app lost foreground the anim task is paused; on regaining it, `SetDelay(50, true, true, false)` is applied again. For us: pause the 50 ms task when the window is unfocused if convenient; it has no gameplay effect.

## 2. `RealUpdateLedger(mode, data)` (0x278a0, `UpdateLedger(str,str)`)

Lua signature: `RealUpdateLedger(string mode, string data)`, returns nothing. The shipped Lua never calls it directly; `ui/helpers.lua` defines the global wrapper `UpdateLedger(type)` (section 3). `LedgerUpdateDisplay` (Reforged `ui/ui_buysell.lua`) is guarded by `if LedgerUpdateDisplay then`, and **nothing defines it**, so that call never runs.

Matching is exact, case-sensitive string equality. Let `full = (mode=="newplayer" or mode=="all")`. The function first reads the global table `Player` and `rank = (double)(float)Player.rank` (default 1.0). Then, in this order, each block runs if its condition holds:

| # | Condition | Effect |
|---|---|---|
| 1 | `full` or `time` | `Player.time` (default 0, weeks). Child `day` text := `Date(time)`. Then game state `GameWeeks` := time clamped to uint32 (`TGameState::SetState(str,uint)`; only the FirstPeek hooks would read it, so skippable). |
| 2 | `full` or `money` | `m = (double)(float)Player.money` (default 0). If `m != targetMoney`: `targetMoney = m` and `deltaMoney = (double)(float)((m - displayMoney) / 20.0)`. If `mode=="newplayer"` only: `UpdateMoney()` (re-renders the text at the **current** `displayMoney`; it does not jump to the target). |
| 3 | `full` or `rank` | `r = (uint)clamp(rank, 0, 4294967295)`. Child `rank` text := string-table lookup of `"rank_%lu"` (through the `BSGStringTable` singleton). Child `score` text is **shown if rank != 1, hidden if rank == 1** (`BSGShowWindow/BSGHideWindow`). Game state `Rank` := r. |
| 4 | `newplayer` or `quest` (**not `all`**) | child `questText` text := `data` verbatim (the second argument; may carry a `<font size='..'>` prefix). If the child is missing, nothing. |
| 5 | `full` or `factory` | `fac = Player.factories`. For each dropper in `droppers`, in order: look up `fac[dropper.name]` (the window's name is the factory name, e.g. `zur_factory`). If nil: hide the dropper window. Else show it and call `DookieDropper::Update()`. |
| 6 | `portview` | hide child `map_port`, show child `port_map`. |
| 6b | `mapview` | show `map_port`, hide `port_map`. (6 and 6b are exclusive; `full` does not touch them.) |
| 7 | `full` or `time` or `money` | score line, below. |
| 8 | always | `TWindowManager::InvalidateScreen()`. |

An unknown mode runs only step 8.

Score line (step 7): `money = (double)(float)Player.money`, `time = Player.time`. If `time > 0`: `s = floor(money / time)`, converted to uint32 with clamping to [0, 4294967295]; else `s = 0`. Text := `GetString("score_format", Dollars(s), "", "", "", "")` using the platform string table (`TStringTable`, 5 parameters, only %1% is used). In the shipped strings `score_format` is `%1%/week`. The child is `score`; this text is written even when the child is hidden (rank 1).

Money text (`UpdateMoney`, 0x27530): `t = Dollars(displayMoney)`. If `displayMoney >= 1e8` wrap as `<font size='25'>` + t + `</font>`; `>= 1e9` size 22; `>= 1e11` size 20; otherwise t unchanged. Applied to the `money` text window via its virtual SetText (vtable slot 0x74; the `#` literal prefix is only a Lua-side convention (inference): a native SetText takes the string as-is).

Quirks worth reproducing:

* All Player numbers go through `float`, so values above 2^24 lose integer precision in the ledger (a $123,456,789 purse displays $123,456,792). Visible only for huge money.
* `rank` and `time` are not cached; every call re-reads `Player`.
* The "all"/"newplayer" paths do not touch the factory *covers*; the Lua wrapper does (section 3).

### 2.1 Money counter animation (`Animate`, 0x28afe, every 50 ms)

```
if displayMoney != targetMoney:
    displayMoney += deltaMoney
    if (deltaMoney < 0 and displayMoney < targetMoney) or (deltaMoney > 0 and displayMoney > targetMoney)
       or deltaMoney == 0:  displayMoney = targetMoney      # clamp, never overshoot
    UpdateMoney(); dirty = true
```

`deltaMoney` was fixed at update time as (target - display)/20, so a full change takes about 20 ticks = **1 second**, linear. If a second change arrives mid-count, the step is recomputed from the current displayed value. After the money step, the quest-flash step (2.2) runs, then every dropper's `Animate()` runs, then `InvalidateScreen()` if the money moved. (The dropper timers advance even while the money is static.)

### 2.2 `SetLedgerQuestIndicator(mode)` (0x2887c) and the flash

Child `ledger_questgoals` is a `TImage` (150x100). If it is missing, return. Mode (exact match, checked in this order):

| mode | effect |
|---|---|
| `"on"` | texture `image/badge_button_indicator_complete`; `flashQuest=false; flashTimer=0` |
| `"flash"` | texture `image/badge_button_indicator_complete`; if `flashQuest` was false: set `flashQuest=true`, `flashTimer=1500`, fire `BSGSoundEvent("quest_requirements_met")` (once per transition, not on repeats) |
| anything else (Lua sends `"off"`) | texture `image/badge_button_indicator_incomplete`; `flashQuest=false; flashTimer=0` |

All end with `InvalidateScreen` (note the `flash` branch jumps to the invalidate directly).

While `flashQuest` is true, each `Animate` tick: `flashTimer = (flashTimer < 51) ? 0 : flashTimer - 50`; then if `flashTimer % 500 < 250` show the complete image else the incomplete image. So `flash` toggles every 250 ms for 1.5 s (1450, 1400... ), **then sticks on "complete" forever** (timer 0, 0%500 < 250) until another mode arrives: after the flash period it looks identical to `"on"`. Quirk: the constructor's initial `flashQuest=true, flashTimer=0` means the very first tick after creation shows "complete" unless an `off`/`on` update has already cleared it; `ledger.lua` ends with `QueueCommand(function() UpdateLedger("newplayer") end)`, whose wrapper always ends with `SetLedgerQuestIndicator`. So at most a brief wrong frame at start (inference: this is cosmetic).

### 2.3 `AnimateProduction()` (0x27874)

For each dropper: `DookieDropper::StartAnimation()` = set its animation clock to 0. Called by `TickSim` (`sim/sim.lua`) at the start of every game tick, before `Player:RunFactories()`. No arguments, no return.

## 3. The Lua wrapper `UpdateLedger(type)` (`ui/helpers.lua`)

Defined in Lua (original lines 21-64, Reforged 98-157), so the engine does not implement it, but its behaviour defines what `RealUpdateLedger` receives:

1. `type == "factory" or "newplayer"`: `UpdateLedgerFactoryCovers()` = `EnableWindow("<zur|cap|san|tok|wel|tor>_factory_cover", not factory:IsOwned())` for six covers. `EnableWindow` is the top-modal child lookup, which works because the ledger is a child of the top modal by then.
2. `type == "quest" or "newplayer"`: `data = _AllQuests[Player.questPrimary]:GetSummary()` ("" if no primary quest, or if the summary is `"#####"` or nil). Long summaries get a size tag prefix: original, `len > 120` -> `<font size='11'>`, `len > 110` -> `13`; Reforged, `> 160` -> 11, `> 130` -> 12, `> 110` -> 13. For every other `type`, `data` is `""`.
3. `RealUpdateLedger(type, data)` if defined.
4. `UpdateActiveQuestGoalsComplete()` (always): `q = Player:GetPrimaryQuest()`; mode `"off"` by default; if `q` and `q:AreGoalsMet()` returns `allgood` then mode `"on"` when `allhints` else `"flash"`; call `SetLedgerQuestIndicator(mode)` if defined.

Who calls it with which type: `sim/sim.lua` `TickSim` `"all"`; `sim/player.lua` `"newplayer"` (game start/load), `"factory"` (factory bought/changed), `"rank"` (`Player:SetRank`), `"quest"` (primary quest changed), `"money"` (`Player:SetMoney`); `sim/factory.lua` `"factory"`; `sim/ingredient.lua` `"all"`; `ui/ui_slotmachine.lua` `"money"`; the screens `"portview"` / `"mapview"`. `Player:SetMoney` calls it whenever the value changes, so the counter animation re-targets on each buy and sell.

## 4. What is on the ledger and who draws it

`ledger.lua` (original and Reforged are the same structure; differences listed after). Everything is ordinary Bitmap/Text/Button/DookieDropper windows; the native class adds no hit areas.

Inside `Bitmap ledger_background` (`image/badge_and_ledger`, at the ledger origin), coordinates relative to the background:

| Window | x,y,w,h | Notes |
|---|---|---|
| `ledger_questgoals` (Bitmap) | 181,29 | indicator image, swapped by `SetLedgerQuestIndicator` |
| Button (graphics `{}`, no art) | 267,78,442,34 | invisible hit area, opens `ui/ui_questlog.lua`; contains Text `questText` (kMax x kMax, centred, font uiFontName 17 black) |
| six `DookieDropper` | y=150; x = 229, 311, 393, 475, 557, 639 (both files); w=79,h=132; names `zur_factory, cap_factory, tok_factory, san_factory, tor_factory, wel_factory` | section 5 |
| six cover Bitmaps `<name>_factory_cover` | y=150; x=229, 311, 390 (Reforged 391.5), 472, 555, 637; images `image/ledger_cover_1..6` (78x106) | shown while the factory is unowned (`EnableWindow`) |
| Text `money` | 46,145,150x30 | font uiFontName 30, colour (208,208,208) |
| Text `day` | 51,175,140x15 | uiFontName 15; initial label `"#"..Date(Player.time)` |
| Text `rank` | orig 58,190,126x15; Reforged 51,190,140x15 | |
| Text `score` | 58,210,126x15 | hidden at rank 1 |
| Text `rawtime` | 58,245,126x15 (below the visible area) | never written by native or by the shipped Lua; black font; ignore |

Buttons that are siblings of the background (children of the ledger, drawn above it), labels from the string table, drawn by `LargeLedgerButton` / `SmallLedgerButton` / `MapPortButton` in `ui/style.lua` (4-layer buttons with the label letter drawn in each layer; masks `badge_button_*_mask`):

| name | position (relative to ledger origin) | command |
|---|---|---|
| `inventory` | +65,-19 | `DisplayDialog{"ui/inventory.lua"}` |
| `recipes` | -3,+3 | `ui/ui_recipes.lua` |
| `quest_log` | +134,+3 | `ui/ui_questlog.lua` |
| `map_port` and `port_map` | +45,+36, **overlapping** | both run `SwapMapPortScreens()` unless `gTravelActive`; `map_port` (label `ledger_port`) is visible on the map, `port_map` (label `ledger_map`) in the port, toggled by the `portview`/`mapview` modes |
| `mainmenu` | -3,+174 | `ui/ui_pause.lua`, `cancel=true` (Escape) |
| `medals` | +140,+174 | `ui/ui_medals.lua` |
| `catalogue` (Reforged only) | +70,+194 | `ui/ui_catalogue.lua` |

The first five commands wrap the dialog in `if gTravelActive then PauseTravel() end ... ResumeTravel()`. Button commands are wrapped by `gButtonsDisabled` in the style functions.

Reforged vs original `ledger.lua`: adds the `catalogue` button; `rank` text is wider/shifted; the port label of each dropper goes through `GetString`; cover 3 at x=391.5; otherwise identical structure.

So: **no tabs, no page turning, no book.** Inventory, recipes, quests and medals are modal dialogs; their content is Lua (`ui/inventory.lua`, `ui/ui_recipes.lua`, ...), outside this document.

## 5. `DookieDropper` (the six factory gauges; 0x20ac8..0x21ecc)

A `Rollover` subclass (so the `contents`, `command`, `fit` ... keys of Rollover apply; `contents = "<factory>:LedgerRolloverPopup()"` is a Lua expression evaluated when the hover popup opens; `command` triggers `FactoryPhone`). The DSL key `factory = <object>` in `ledger.lua` is **ignored** by the native; the factory identity is the **window name**.

DSL keys read by `PostChildrenInit` (0x20aee), with defaults: `itemx` 18, `itemy` 86 (product sprite position), `ingredienty` 58 (baseline of the ingredient icons/bars), `barHeight` 42, `countx` 57, `county` 86 (position of the timer dial). `ledger.lua` passes exactly these values. Children: Text `port` (label set from Lua, never touched natively) and Text `count`.

Constructor state: animation clock 2000 (finished), `playerRank` 1, product sprite (empty), timer sprite `image/timer_face_small` (38x38) with child `image/timer_hand_small` (38x38), idle texture `image/factory_idle_help` (32x32), bar colours `colorFull` = (50,205,30,255), `colorMedium` = (255,252,15,255), `colorTrouble` = (255,14,14,255).

### 5.1 `Update()` (0x211ee), called from `UpdateLedger` modes `factory`/`all`/`newplayer`

1. `info = Player.factories[self.name]`. `cur = info.current` (default `""`).
2. If `cur` differs from the cached product code: cache it; `prod = _AllProducts[cur]`; if `prod` exists: `LoadAppearance(prod)` -> layer image names and tints; rebuild the product sprite (identity matrix, translation preserved) with `BuildItemAppearance(sprite, names, tints, 0.25)`; then rebuild the ingredient list from `prod.counts`: for each entry, `ingredientName[n] = the table key` (inference: `counts = { ingredient = count }` in `sim/product.lua`, and the name is used to index `Player.supply`), `ingredientImage[n] = texture "items/" + name`, `n++`; `ingredientCount = n` (a byte; arrays hold 6; there is no bounds check). If `prod` is nil (empty product), the old sprite and ingredient list are left as they were.
3. If child `count` is a Text: `production = (uint)info.production` (default 0) and the text becomes `"%lu"` of it.
4. `stall = info.stall` (default false).
5. `playerRank = (uint)Player.rank` (default 1).

Order of ingredient columns follows Lua `next()` order of the `counts` table, which is not specified; the original gets whatever order Lua 5.0's hash gives. Our embedded Lua 5.0.2 should reproduce that if tables are built in the same order.

`LoadAppearance(product)` (0x1fcd2; `CustomItems.h`): if `product.appearance` exists, use it (via `LoadAppearanceInternal`); else if `product.code` is non-empty: one layer `items/<code>_big`, white; else if `product.name` non-empty: `items/<name>_big`, white; else treat the product table itself as the appearance list. `LoadAppearanceInternal` iterates the table in `next()` order: a string entry gives layer `custom/<string>` with white tint (1,1,1,1); a table entry `{ name, r, g, b }` gives `custom/<name>` with tint `(r,g,b,1)` from fields 2..4 (missing -> 0.0, not 1.0). `BuildItemAppearance(sprite, names, tints, scale)` (0x1fff4): remove all child sprites; scale the matrix if `scale != 1`; for each layer i, add a child sprite at z-index `z` with that texture and its tint, then, if a texture `<name>_highlight` exists, add it as another child (untinted) at z+1; z advances 1 per layer, 2 if a highlight was added.

### 5.2 `Draw()` (0x21ecc)

1. `TWindow::Draw` (children).
2. Draw the product sprite, then the timer sprite (the hand is its child), both with an identity draw spec (their own translations place them: product at (itemx, itemy), timer at (countx, county)).
3. Let `W` = window width, `cw = W / 6` (13.17 for w=79), `n = ingredientCount`, and start `x = (cw + (W - n*cw)) / 2`, the centre of the first of `n` columns centred in the window. `weeks = _weeksPerColumn[playerRank]`, a float table inside the binary (`__const`, address 0x192b40) indexed **directly by rank**: index 0..6 = **4, 6, 8, 8, 8, 10, 10**, so rank 1 -> 6, rank 2 -> 8, rank 3 -> 8, rank 4 -> 8, rank 5 -> 10 (rank 6 -> 10, rank 0 -> 4; ranks above 6 read unrelated data). The same 0..6 span matches `lowIngredientCutoff` in `sim/factory.lua`. For each ingredient i in order:
   * `ratio = Player.supply[name] (default 0) / weeks`. `Player.supply[name]` is `have / need` in game ticks (`Player:UpdateSupplies`).
   * `h = min(ratio * barHeight, barHeight)`.
   * colour: `ratio >= 0.75` -> full (50,205,30); `0.5 <= ratio < 0.75` -> medium (255,252,15); else trouble (255,14,14). All alpha 255.
   * bar rectangle as (x1, y1, x2, y2): `x1 = uint(max(0, x - cw/2 + 2.5))`, `y1 = uint(max(0, ingredienty - h - 3))`, `x2 = uint(max(0, x1 + cw - 4))`, `y2 = y1 + h`; filled with `TRenderer::FillRect(rect, colour, no texture)`. Bars are `cw - 4` wide and grow upward from y = ingredienty - 3 (55 by default), at most `barHeight` tall.
   * the ingredient icon (`items/<name>`, 32 px source) is drawn centred at `(x, ingredienty)`, alpha 1.0, scale `cw * 0.03125` (= cw/32, about 0.41).
   * `x += cw`.
4. If `stall` or `production == 0`, draw `image/factory_idle_help` at centre `(W/2, (ingredienty - barHeight) + texHeight/2)`, alpha 0.75, scale 1.0.

### 5.3 `StartAnimation()` / `Animate()` (0x20adc / 0x20d40, driven by the ledger's 50 ms task)

`StartAnimation` sets the clock `t = 0`. `Animate`: if `t < 2000`: `t = min(2000, t + 50)`. Then, only if not stalled and `production != 0`:
* timer hand rotation = `2*pi * t / 2000` (one revolution over the 2 s animation; the hand's matrix is replaced by a pure rotation about the sprite origin).
* product sprite scale: let `period = 2000` if `production < 10`, `1000` if `< 30`, `666` if `< 50`, else `500`. `s = ((t % period) + 1) * 0.25 / period`; if `t == 2000` then `s = 0.25`. The sprite's matrix is reset to identity, scaled by (s, s), translation kept. So the product icon grows from 0 to 0.25 once per period, more often at high production. At t=2000 it rests at 0.25, the size `BuildItemAppearance` used.

With the clock stuck at 2000 (constructor) nothing animates until the first `AnimateProduction()`.

## 6. `SetMood(name, happiness)` (0x7866, `Character::SetCharacterMood`)

Lua: `SetMood(string characterName, number happiness)`, no return. Callers: `sim/character.lua` `Character:SetHappiness` and `BumpHappiness`, both after clamping to 0..100 (and `Character.kNeutral` handling in Lua).

Native: walk the static list `Character::visibleCharacters`; for every `Character` (the `CharWindow` DSL type) whose name (string at +0x50) equals the argument, call `SetMood(happiness)`. Characters not on screen are not in the list, so the call is a no-op; their stored happiness lives in `Player.charHappiness`, and a newly created `CharWindow` takes its `happiness` key (default 50, `design-questions.md`).

`Character::SetMood(h)` (0x76fa), only if the window has a texture that is an animated texture (`characters/<name>.xml`) with N frames:
* N == 3: `k = (int)(h * 3 / 100)`; target frame = `1` if `k == 0`, `0` if `k == 1`, else `2`. So `h < 33.3` -> frame 1 (the unhappy/angry art), `33.3 <= h < 66.7` -> frame 0 (neutral), `h >= 66.7` -> frame 2 (happy).
* N >= 2, N != 3: `t = (int)((N-1) * h / 100) + 1`, capped at `N-1`; target = t.
* N < 2 (a still PNG): nothing.
* If the target differs from the current frame (+0x64): set the cross-fade clock (+0x6c) to 0 and `StartWindowAnimation(50 ms, ...)`.

`OnTaskAnimate` (0x71da): while the clock < 200, add 50 and invalidate; at >= 200 the clock is 200 and the current frame becomes the target; the task stops once the clock reaches 200 (about 200 ms, 4 steps). `Draw` (0x741c): always draws frame 0 first at full alpha; if there is more than one frame, draws the current frame over it at the window alpha, and, while the target differs, the target frame on top with alpha `alpha * clock / 200`. A still PNG is drawn once. The offset is half the scaled texture size; scale is the window `scale` key.

## 7. Game state, options, display and sound

### 7.1 `TGameState` and the `GetState` name collision (0x73e60 ctor; singleton created lazily by `TGameState::GetInstance`, which requires a platform)

The constructor registers five globals: `GetState` (member, `str GetState(str key)`), `_SetState` (static free function, `SetState(str,str)`), `QueryMuteGame`, `QueryRestartMode`, `QueryJumpToMenu` (members returning bool). The prelude's `SetState(key, value)` converts booleans to 1/0 and calls `_SetState(tostring(key), tostring(value))`. All commented-out `SetState` calls in the shipped Lua are marked FIRST PEEK (a demo/telemetry hook), so nothing in the game logic reads these values.

* State is a `map<str,str>`. `GetState(key)`: lookup; missing keys are **inserted with value ""** and "" is returned; then every registered `TGameStateHandler` (a vector, empty in this build as far as I can tell) may overwrite the result through vtable slot 0xc.
* `SetState(key, value)`: if `key` starts with `++`, the prefix is stripped and `value` becomes `to_int(GetState(key)) + 1` (integer text). The pair is stored; then handlers (vtable slot 8) are polled and the first to return true stops processing. Otherwise, **only if the key equals `TGameState::kMute`** (a static string whose text I did not read; (inference) `"mute"`): old = `to_int(previous)`, new = `to_int(value)`. new==0 and old!=0: restore the saved master volume (stored at map header +0x18) via `TSoundManager::SetVolume`, saved := 0. new!=0 and old==0: save the current master volume (`GetVolume(-1)`) and set it to 0.
* `QueryMuteGame()`: true if any handler says mute (vtable 0x18), else `to_int(GetState(kMute)) != 0`. `QueryRestartMode` / `QueryJumpToMenu` poll the handlers (0x10 / 0x14); with no handlers they return false.
* The engine itself calls `SetState` with: `GameWeeks` (ledger time), `Rank` (ledger rank), `ChocolateFactoryTime` ("restart" at game start, 0 on pause), `RecycleCount`, and `kMute` (from `ToggleSoundMute`).

**Name collision**: `THiscore::THiscore` (0x2339e) registers a second global named `GetState` (`int GetState()`, no key argument), and `~THiscore` sets `GetState`, `ScoreAvailable`, `UpdateText` and `SubmissionDone` to nil (note: `LaunchPrivacyPolicy` is registered but not cleared). The later registration wins. The shipped Lua calls only the no-argument form (in `ui/hiscore.lua`, after the `HiscoreWindow` exists). Engine: `GetState` is the Hiscore version (returns the state int, 0 when no hiscore window exists; bound by `HiscoreWindow`); the TGameState string map stays inside the engine. The key-argument form is unreachable from the shipped Lua because the only caller that would use it (`SetState` -> `_SetState`) is dead.

### 7.2 Options, volumes and mute

Prefs keys (per user, key `curuser` selects the user, -1 = global): `soundlevel`, `musiclevel`, `ambientlevel` (ints, level x 1000), `soundoff` (0/1), `fullscreen` (global, 0/1). **Defaults: soundlevel 750 (0x2ee), musiclevel 650 (0x28a), ambientlevel 500, soundoff 0, fullscreen 1.**

`SaveVolumes()` (0x2d678): registered by `TOptions` (the `OptionsWindow` DSL type) in its constructor (0x2dc3e) and not cleared when it is destroyed, so it exists after the first Options dialog is created and acts on that window's children. For each of the child sliders `sfxlevelslider`, `musiclevelslider`, `ambientlevelslider` that exists and is a `Slider`: `prefs[ "soundlevel"/"musiclevel"/"ambientlevel" ]` for the current user := `(int)(slider.GetValue() * 1000)` (float multiply then truncation toward zero), written with the flush flag. A missing slider is skipped. Arguments: none. Returns nothing. Called from the OK button (`ui/ui_options.lua`, `okFunction`) before the dialog closes.

`TOptions::PostChildrenInit` (0x2d1ce) initialises the dialog: button `fullscreen` toggle := `TSettings::IsFullScreen()`; `mutebox` toggle := (`soundoff` == 1) and, only when not muted, re-applies `SetSoundForCurrentUser`; each slider := `pref / 1000.0` (default values above), `SetValue(v, false)`.

`TOptions::OnMessage` (0x2d98c), for message type 9 (a slider value changed; (inference) the same message the Slider widget posts, see `window-system.md`): if the sender is one of the three sliders, and `soundoff != 1`, call the live volume setter with the slider value: `sfxlevelslider` -> `BSGSetSoundLevel(v)`, `musiclevelslider` -> `BSGSetMusicLevel(v)`, `ambientlevelslider` -> `BSGSetAmbientLevel(v)`; returns handled. The sliders update volume live and `SaveVolumes` persists on OK. The OK button in `ui_options.lua` is also `cancel=true`, so Escape saves too; there is no discard path.

The C++ functions (not Lua globals; `BSGSound.h`): `BSGSetSoundLevel(float)` (0x4f6a) sets the sound-manager type volume for types 1000 and 0 and sends CADI event `"Sfx Volume"`; `BSGSetMusicLevel` (0x4f22) type 1001 + `"Music Volume"`; `BSGSetAmbientLevel` (0x4fda) only CADI `"Environments Volume"`; `BSGSoundMute(bool)` (0x4e78): true -> master volume 0 and the three CADI volumes 0; false -> master 1.0 and `SetSoundForCurrentUser`. With audio silent, all of these can be no-ops; only the prefs bookkeeping matters.

`TSettings::SetSoundForCurrentUser` (0x35e20): read the three prefs for `curuser` (defaults above), call the three setters with `pref / 1000.0`, then if `soundoff == 1` call `BSGSoundMute(true)`. Called at game start (`InitGameToSettings`, 0x35fda, which also calls `TPlatform::SetDisplay(800, 600, fullscreen_pref == 1)` with default fullscreen = **1**) and when a user is selected.

`ToggleSoundMute()` (0x35a1e): no arguments, no return. `u = curuser`; `new = (soundoff(u) + 1) & 1` (flip, computed as a signed modulo 2 so any odd/even value works); `SetState(kMute, new)`; `prefs.soundoff(u) = new` (flushed); then `BSGSoundMute(QueryMuteGame())`.

`ToggleFullScreen()` (0x35928) returns a boolean: if currently fullscreen, `TPlatform::SetFullscreen(false)`, else `SetFullscreen(true)`; on success it writes `fullscreen = 0/1` (global prefs, flushed) and the call returns **true**; if the platform refuses, it returns **false** and changes no pref. Lua (`ui_options.lua`): a `false` result opens `ui/ui_generic.lua` with `text="screenswitchfailed"`. The window option button `fullscreen` is a toggle whose "on" state is set only at dialog creation, not by the call. (The engine plan also binds Alt+Enter/F11; those do not go through this native.)

`SoundEvent(key)`: see `api-globals.md`. Signature `SoundEvent(string)`, returns nothing, never errors on an unknown key. Keys the shipped Lua uses include `Stop_Music`, `main_menu`, `money_in_account`, `win_money`, `slots_win_3_in_row`, `slots_win_4_in_row`, `coffee_making_game`, `major_award_fanfare`, `award_fanfare`; native code also fires `quest_requirements_met`, `crate_click`, `crate_no_money`, `crate_with_money`, `randomize`, `Stop_machine_spin`, `Stop_win_money` through `BSGSoundEvent`. For a silent build all are no-ops; `QueryMuteGame()` and the prefs must still behave.

### 7.3 `QuitToMainMenu()` (0x3606c, `TSettings::QuitToMainMenu`)

No arguments, no return. Loop: let `top = GetTopModalWindow()`; stop if null; stop if `top`'s parent field (+4) is null; otherwise `PopModal(format("%8x", top->id))` and repeat. That unwinds every modal above the bottom-most one, leaving the root (the main-menu modal pushed by `DoMainWindow`). No shipped Lua calls it (the pause menu's `main_menu` button uses `SwapToModal("ui/mainmenu.lua")` instead). It is safe to implement exactly this way; it is also safe to leave it unregistered.

### 7.4 Movies (`StartMovie`, `IsMoviePlaying`, `StopMovie`; `TWindowScript` 0x9fdf0, 0x9feb6, 0x9fede)

Lua signatures `StartMovie(path, bool, bool) -> bool`, `IsMoviePlaying() -> bool`, `StopMovie()`. `StartMovie` returns false at once for an empty path; otherwise builds a `TFlashHost` (Flash player) on `path`, stores it in the script object (+0x5c), and returns the host's `Start` result (false and the host is deleted on failure). `IsMoviePlaying` is true only while a host exists and reports playing. `StopMovie` stops and deletes the host, clears the pointer; safe to call with none. The only caller is the prelude's `DisplaySplash` (`lua/prelude.lua`), which treats a **false** result from `StartMovie` as "show the still image instead". The plan says not to play the three `.swf` files, so: `StartMovie` -> false (log once), `IsMoviePlaying` -> false, `StopMovie` -> no-op. `DisplaySplash("splash/intro_movie.swf", "splash/playfirst_logo", 0)` (Options "replay intro") then shows `splash/playfirst_logo` and dismisses on timeout or key as per the prelude. (Which of the two bool arguments reaches `TFlashHost::Start` is not clear from the decompile; irrelevant when movies are skipped.)

## 8. Hiscore, score submission and the dead `Submit*` paths

Online play is out of scope; this section says what the Lua needs so the screens do not break.

### 8.1 Local score storage (`TPfHiscores`, owned by `TSettings`; 0x45440 `RecordScore`)

Native `LogScore(score, a, b)` (see `api-globals.md`; called only from `Player:LogScore`, which runs when the Hiscore screen opens, from the pause menu's score details, and from submission code). `Player:LogScore` (original `sim/player.lua`): `score = Floor(money / time)` (time defaults 1); only if `score > 0`: `a = string.format("%d-%09d-%012.0f", rank, time, money)`, `b` = concatenated `<medal name='PFMedalKeys[i]' per='game' />` for each held `medal_01..09`, then `LogScore(score, a, b)`.

`TPfHiscores::RecordScore(score, replaceOwn=true, a, b)` behaviour as read: tables are keyed by the current game-mode name; max **500 rows**; rows are kept sorted by score descending; a new row is inserted after every existing row whose score is greater than or equal to it (so a tie ranks **below** the older equal score in position, though it shares its rank number). With `replaceOwn` true (the `LogScore` path): if the current user already has a row in that table and `row.score >= new score`, **nothing changes**; if the new score is higher, the old row is removed first, then the new one inserted (one row per user). Ranks: tied scores share a rank and the next rank skips (1, 2, 2, 4). Each row stores: rank, score, user name (hiscore property 1), the `a` string, the `b` string, game-mode name (property 2), a "local" flag (true), a "does not qualify / submitted" flag (false), and a GUID string. After any change `SavePreferences` persists it (file format not decoded; our own format is fine, same as other prefs).

Reads: `GetScoreCount(local)`, `GetScore(local, i, &rank, name[64], ..., &isLocal, &score, a[64], ...)` return rank (row +0), name, `a` string, local flag and score. `ScoreAvailable` (below) uses `GetUserEligibleScore`.

### 8.2 `ScoreAvailable()` (0x22e38), `GetState()` (THiscore), `UpdateText()`, `SubmissionDone(bool)`, `LaunchPrivacyPolicy()`

All are registered when a `HiscoreWindow` (`THiscore`) is constructed, and (except `LaunchPrivacyPolicy`) cleared to nil when it is destroyed.

* `ScoreAvailable() -> bool`: `TPfHiscores::GetUserBestScore(0 /* eligible */, ...)` = `GetUserEligibleScore`: true iff the current user has a row in the current game-mode table whose "does not qualify" byte (+0x19) is 0. Since fresh rows have that flag 0, it is true as soon as `LogScore` stored a score for this user. Used by `hiscore.lua` `UpdateButtons` only to enable the Submit button, and only when `IsEnabled(kHiscoreLocalOnly)` is false.
* `GetState() -> int`: the window's state enum (`hiscore.h`): `0 eLocalView, 1 eRequestingCategories, 2 eRequestingScores, 3 eSubmitting, 4 eGlobalView, 5 eError`. The constructor sets 0. Lua mirrors these as `eLocalView=0 ... eError=5` (Reforged uses a local `eGlobalView = 4`). Offline, the state stays 0 unless the `view` button is pressed (below).
* `UpdateText()`: refreshes the score rows (`UpdateScoresTable`) and the right-hand panel texts. Not called by the shipped Lua (the window calls it itself).
* `SubmissionDone(bool qualified)`: `state := 1` (eRequestingCategories), request categories, `submitState := qualified ? eSuccess(1) : eDNQ(2)`, `UpdateText()`, then calls Lua `UpdateButtons();` (a global the screen must define).
* `LaunchPrivacyPolicy()`: opens `http://www.playfirst.com/privacypolicy.html`. Never called by Lua; no-op.

Native -> Lua hook: `THiscore::UpdateButtons` runs the Lua string `UpdateButtons();`, so `ui/hiscore.lua` must define a global `UpdateButtons` before the window's `PostChildrenInit` runs, which it does at construction (`PostChildrenInit` ends with `UpdateText(); UpdateButtons();`).

`THiscore::PostChildrenInit` (0x2566c): starts a 20 ms window animation, binds children `name1..10`, `score1..10`, `info1..10`, `1..10` (the rank number Text), `p1_1..10` (the P1 icon Bitmaps), then updates. `OnMessage` (type 3 = button command, by sender name): `categoryleft` / `categoryright` (step the category index +-1 with wrap, state := 2, request scores), `view` (state := 1, request categories, submitState := none), `viewlocal` (state := 0), `scrollup` / `scrolldown` (score offset -10/+10, clamped inside `UpdateScoresTable` to `0..count-5`, up shown if offset > 0, down shown if offset+10 < count). `OnTaskAnimate` polls `GetServerRequestStatus` while state is 1 or 2. For an offline engine, `view` should lead to the error state or simply be disabled; the Lua enables `view` only when not local-only.

Local view rows (`UpdateScoresTable`, 0x23e2e, state not 4): offset forced to 0 (no scrolling), up to 10 rows from `GetScore(local=true)`. Per row i: `name` text := stored user name, then the text's line height is reduced from 20 down by 1 until the text bounds fit the window (min 2); `score` text := `GetString("score_format", Dollars(score))` (`%1%/week`); the rank text := `"%d."` of the stored rank; `info` text := `ParseGameData(a)`; rows beyond the count keep their initial labels (the DSL shows `name`, `12345`, ...; (inference) the engine hides unused rows through `HideAllText` at the start of `UpdateText`, not decoded). The P1 icon is shown for non-local rows when neither local-only nor anonymous mode applies, i.e. never in local view. Header visibility uses the window flag 0x100 (the "visible/enabled" bit that `EnableWindow` toggles): in local view the `local` header is set visible when `kHiscoreLocalOnly` is **off**, and `topplayers` (if it exists; it is commented out in the shipped Lua) is set visible when local-only is **on**; the other headers are managed in `UpdateText` (not decoded).

`ParseGameData(a)` (0x22e96): `a` is `R-WWWWWWWWW-MMMMMMMMMMMM`. Rank name := string-table `rank_<first char of a>` (so `rank_1`..`rank_5`). Weeks := the integer between the first two dashes (if >= 1,000,000,000, treated as negative infinity); formatted with `Dollars` and then the leading money sign stripped (inference: `substr` from the end of the sign), i.e. a plain thousands-separated number. Money := the float after the second dash, clamped >= 0, `Dollars`. Result := `GetString("score_info_format", rank, money, weeks)` = `%1%: %2% in %3% weeks` (e.g. "CEO: $1,234,567 in 120 weeks"). Note the order: money is `%2%`, weeks `%3%`.

### 8.3 `SubmitWindow` / `TServerSubmit` / `SubmitToServer` / `GetLuaServerSubmitSetupVars`

`TServerSubmit::Register` (0x356b2), called at startup (in the type-registration list), does two things: `AddWindowType("SubmitWindow", TServerSubmitDynamicCreate)` and registers the free function `GetLuaServerSubmitSetupVars`. The window constructor (0x35084) registers `SubmitToServer` (member) and stores the settings' `TPfHiscores`.

* `GetLuaServerSubmitSetupVars(bool medalsMode) -> string` (0x35168): builds a Lua source string and returns it. Parts, in order: `gNameEdit='<pref _anonusername of the current user, default "">'; ` ; if the saved account info exists (`GetRememberedUserInfo`): `gAccountEdit='<account>'; gPassEdit='<pass>'; gRemember=true; ` else `gAccountEdit=''; gPassEdit=''; gRemember=false; ` ; then if `medalsMode`: `gMedalsMode=true;` else `gMedalsMode=false;gEligibleScore=%d` with the user's eligible local score (0 if none). Lua runs it with `loadstring(...)()` before opening the submit dialog (`ui/hiscore.lua` line 197 original; 492 Reforged, which also guards `type(vars) ~= "function"`). **The returned text must be valid Lua** (single quotes inside names are not escaped by the original; ours should escape). Offline: return `"gNameEdit=''; gAccountEdit=''; gPassEdit=''; gRemember=false; gMedalsMode=false;gEligibleScore=0"` (the original format has no trailing semicolon on the last statement; either is valid).
* `SubmitToServer(name, account, pass, remember, medalsOnly)`, no return (0x354ce). Original logic: if `pass` is empty, store `name` into pref `_anonusername` (current user, flushed) and submit anonymously; else submit using the account and pass. Then `SwitchModes(true)` (runs the Lua string `SwitchModes(true)` in the script; the Lua dialog defines `SwitchModes(submit)` to enable/disable controls, `submitconnect` etc.) and start a 20 ms window animation. The animation (`TServerSubmit::OnTaskAnimate`, 0x34dda) polls the request status: when finished with failure it shows the child `submiterror` text with the message and enables `submiterrorok` (the Lua `submiterrorok` button calls `SwitchModes(false)` again); when done successfully it runs the Lua string `gReturnValue='success'` (score not in the top table, "DNQ") or `gReturnValue='qualified'` and pops its parent modal by id. Lua then `DoModal` returns that string and calls `SubmissionDone(true/false)`. The `submittoserver` button enable state follows the `nameedit`/`accountedit` text lengths (message type 7, a text change: enabled if either is non-empty).
* Engine (offline): `SubmitToServer` sends nothing and runs `SwitchModes(false)` at once, so the dialog never stays in submit mode (`GameNatives.cpp`). The Reforged flow (`SubmitCommunityScore`) is behind the Community bridge and is not reached when Community is absent (it calls `DoModal("ui/hiscore_submit.lua")`, which uses `SubmitToServer(submitName, "", "", false, gMedalsMode)` only after obtaining an online ticket).
* `SubmitWindow` as a window type: the DSL constructor takes the standard `TWindow` keys; children are looked up by name (`nameedit`, `accountedit`, `submittoserver`, `submitconnect`, `submiterror`, `submiterrorok`). It has no keys of its own beyond the base window's (not decompiled beyond the above).

## 9. Natives with no live caller, or defined only by Reforged

| Name | Finding |
|---|---|
| `GetTopModalWindow` | Not a Lua global in the Mac binary (only a C++ method). Reforged `ui/helpers.lua` `CloseAllModals` calls it and then `topWindow:GetName()` / `:GetID()`, i.e. it expects a window userdata with methods. **`CloseAllModals` is never called anywhere**, so the missing global is harmless. Leave unregistered (a plain error would only occur if something calls it). |
| `SaveCreationToFile` | No string, no registration in the Mac binary. Only referenced by Reforged `ui/ui_kitchen.lua` `SaveCreation()` (arg: a table `{name, description, category, ingredients = {names}, appearance = { {image, r, g, b}, ... }}`), and **nothing calls `SaveCreation`**. Dead; leave unregistered or make it a logged no-op. |
| `RankNames` | A Lua global that nothing defines. `ui/community_scores_results.lua` line 53 guards it with `RankNames and RankNames[rank]`. No native. The real rank names come from string keys `rank_1`..`rank_5` ("Rookie", "CEO Candidate", "Interim CEO", "CEO", "Master Chocolatier"). |
| `LedgerUpdateDisplay` | Optional hook in Reforged `ui/ui_buysell.lua`; guarded, never defined. Do nothing. |
| `QuitToMainMenu` | See 7.3. Unused. |
| `UpdateText`, `LaunchPrivacyPolicy`, `SubmissionDone` (Reforged yes) | See 8.2. |
| `StartMovie` / `IsMoviePlaying` / `StopMovie` | See 7.4. |
| `bsgLoadStringFile`, `bsgStringExists`, `bsgGetString` | Registered, never used by the shipped Lua; documented in `api-globals.md`. |
| `QueryMuteGame`, `QueryRestartMode`, `QueryJumpToMenu`, `_SetState` | Registered, only the prelude's `SetState` reaches `_SetState`, and no live Lua calls `SetState`. Implement as stubs with the semantics in 7.1 if cheap. |

## 10. Other game-level natives that appeared in the stub list

### 10.1 Crates mini-game: `PrepareGame(bet)` and `SelectPrize(i, final)` (`TBonusGame`, 0x3cdc6 / 0x3cfea)

`PrepareGame(uint bet) -> uint` (name registered by `TBonusGame`'s constructor). For each of the **21** crates compute `v = (uint)(bet * f[k] + 0.5)` with the factor table `_PrizeFactor` read from the binary, then `v = (v / 100) * 100` (round down to hundreds), and if `v < 100` and `f[k] > 0` then `v = 100`. Factors, in order (crate slots 1..21 before shuffling): `0.435, 0.435, 0.435, 0.4, 0.35, 0.35, 0.35, 0.35, 0.25, 0.25, 0.25, 0.25, 0.24, 0.24, 0.24, 0.15, 0.1, 0.05, 0.0, 0.0, 0.0`. Sum of factors = **5.125**, so the expected total on the table is about 5.125x the bet before rounding; **three crates are empty ("whammy devil" bombs)** because three factors are 0. The values are then **shuffled** with a single pass: for each slot `k` from 0 to 20, swap `value[k]` with `value[rand() % 21]` (C `rand()`, so not a uniform Fisher-Yates and not seeded here; (inference) the game calls `srand` elsewhere, and our deterministic-test harness may use its own RNG). Returns the sum of the 21 rounded values (the Lua shows `n = total` rounded to the nearest 100/1000/10000 in its "first crate" hint).

`SelectPrize(index, final) -> uint`: `index` is **1-based** (the crate array starts at object +0x30 and `Select` addresses `this + index*0x30`; `ui/ui_crates.lua` calls `SelectPrize(i, false)` for `select_<i>` buttons). If that crate's state is `WAITING(0)`: play `crate_click`; state := `OPENING(1)`. If its value is 0: load texture `image/whammy_devil` as the crate graphic; if `final` then state := `BLANK(3)`; play `crate_no_money`. Else (value > 0): `Dollars(value)` is drawn as a text graphic in `fonts/fertigo.mvec` over the crate; opened normally it is black (0,0,0,1) at size 16 with a sparkle (`StartSparkle`: particle burst at the crate, sets Lua global `gSparkleTime = 2`) and sound `crate_with_money`; revealed with `final=true` it is size 14 in (0.8, 0.2, 0, 1) with no sparkle or sound. Returns the crate's value whether or not it was already open (the Lua disables the button, so it calls once per crate). Particle drawing belongs to Phase 4; for Phase 3 only the value bookkeeping and the state transitions matter. The window layout, per-state drawing, timings (`GROW_TIME` = 400 ms, 800 ms fade, 50 ms tick) and sparkle colours are in `game-windows-questions.md` 6. The `final=true` text is the colour (0.8, 0.2, 0, 1) at size 14; the rectangle the text is centred in is the button rectangle widened by 20 px left and right. Lua: `value > 0` adds to winnings; `0` ends the game.

### 10.2 Slot machine: `StartSlots(s1,s2,s3,s4,payout)` and `StopSlots()` (`SlotMachine`, 0x39f16 / 0x3a0ca)

`StartSlots(uint, uint, uint, uint, float)`: the four ints are the **1-based option indexes** (into the `options` ingredient list the DSL passed) that wheels 1..4 land on; the fifth argument is the payout multiplier (the stub list calls it "speed"; `ui_slotmachine.lua` passes `payout`). Sets the "all stopped" flag false and "sparkle done" false, resets the sparkle clock, calls `SlotWheel::Start(wheel, symbol)` on each of the four wheels, stores the payout float, and starts a **50 ms** window animation. `OnTaskAnimate` ticks all four wheels; when all have stopped it plays `Stop_machine_spin`, calls the Lua function from the DSL key `onstopped`, and sets the globals `gSparkleTime = payout*0.5 + 1` and `gSparkleDensity = payout*10`. If `payout <= 0` the sparkle phase is finished at once; otherwise the particle system updates by 50 ms per tick until `gSparkleTime <= 0`; then it plays `Stop_win_money` and calls the DSL key `onpayout` function. The animation ends when both phases are done. `StopSlots()` (`StopOne`) stops **one** wheel: the first wheel (1..4) that is still spinning (`stepsLeft != 0`) and has `stepsLeft > 3` gets `stepsLeft := 3` (a step count, not a state; the target symbol is inserted on the first of those steps and the wheel then stops normally; see `slot-machine.md` section 9); called by the Lua after the lever pull to stop wheels in sequence. DSL keys of `SlotMachine` (`PostChildrenInit` 0x3b1e0): `options` (table of ingredient names), `wheel1..wheel4` (tables with `x`,`y`), `onstopped`, `onpayout`. The wheel physics, symbol strips and win logic are in `SlotWheel` (not decoded here; Unknown).

### 10.3 `RecipeDesigner` natives (0x327a4..0x33872)

Used by `ui/ui_kitchen.lua`. Four layers; layer image lists are filled from the files in the `custom/` asset directory.
* `SetRecipeType(string category)` (0x32e32): clears the four layer lists, then enumerates the directory `custom/<category>` (`TFile::GetNextFile` returns paths including the directory, e.g. `custom/bar/layer1_01.png`), skipping names containing `_highlight` and names not ending in `.png`. For each, strips `.png`, loads the texture, adds it to the window's asset map, and files the stripped path by name substring into list 0 (`layer1`), 1 (`layer2`), 2 (`layer3`) or 3 (`layer4`); other names are ignored. At the end an empty-string entry (meaning "none") is appended: for `category == "beverage"` to lists 1 and 3; for every other category to lists 1, 2 and 3. List 0 never gets one. Returns nothing. (The shipped `custom/` has directories `bar`, `beverage`, `blend`, `exotic`, `infusion`, `truffle`.)
* `SetLayer(layer, delta) -> int` (0x33418): `layer` is 0..3; new index = `(delta + size) % size` where `delta` is the **index** the Lua computed (`design[field]+1` or `-1`; the `+ size` makes -1 wrap). It stores the index, updates the window children `option_layer<n>` / `option_highlight<n>` / `display_layer<n>` / `display_highlight<n>` (images shown/hidden depending on whether the texture and its `_highlight` variant exist; option images at alpha 0.5, display images at alpha 1.0; an empty-string entry hides all four). Returns the index applied. Note the index uses the size of the list at call time.
* `GetLayerImage(layer) -> string`: the current entry for the layer with the first 7 characters removed (the leading `custom/`), e.g. `bar/layer1_01`; for the empty entry, `""`. `LoadAppearance` later puts `custom/` back in front.
* `RandomDesign()` (0x327a4): resets the random-timer to 0, plays sound `randomize`, starts a **100 ms** window animation. `OnTaskAnimate` (0x33872): timer += 100; while timer < 2000 each tick re-rolls layers: layer 0 while timer <= 1000 with `RandRange(0, size-1)`; layer 1 while timer < 0x5dd (1501) with `RandRange(1, size-1)`; layer 2 while timer < 0x709 (1801) with `RandRange(2, size-1)`; layer 3 every tick with `RandRange(0, size-1)` (note the lower bounds 1 and 2 are the original's, and look like a bug that avoids index 0; keep them). Each rolled layer also builds the Lua text `SetRecipeTint(<layer 1..4>, <RandRange(1,100)>);` which is run as one Lua string per tick, so `ui_kitchen.lua` must define the global `SetRecipeTint(layer, 1..100)`. Ends (returns false) at timer >= 2000: 20 ticks = 2 s.

### 10.4 `SignEditor` setters (`SignEditor.h`, 0x36d4c..0x3795e)

`SetBackground(name)` and `SetLogo(name)` prepend `"logo/"` and load that texture; `SetBackground` assigns it to the sign sprite, `SetLogo` to the logo sprite. `SetFont(name)` prepends `"fonts/"` and sets the text sprite font; `SetText(str)` sets the text sprite text (this is not the generic window `SetText`; the stub list's `SetText` row is this one). All four invalidate the screen and return nothing. `UpdateTextColor` converts the editor's HSV (`textH`, `textS`, with `textV`, alpha `textA/100`) with `HSVtoRGB`. The editor's own message handling (sliders, drag, rotate) is in `OnMessage` (0x3710a), not decoded here (Unknown).

### 10.5 Small window natives

* `BTSetTint(windowName, {r,g,b,a})` (`TBitmapTint::SetBitmapTint`, 0x3b8b6): finds the child by name **in the top modal window** (recursive), requires it to be a `BitmapTint`; sets tint components from table fields `[1]..[4]`, each defaulting to 1.0. No invalidate call. `BTSetOffset(name, x, y)` sets float offset (+0x58, +0x5c); `BTSetFlip(name, bool)` sets a flip byte (+0x68). All are no-ops when the window is not found.
* `SetRectangleColor(name, {r,g,b,a})` (`TRectangle::SetRectangleColor`, 0x3c926): top-modal child lookup, requires a `Rectangle`; sets the four floats from `[1]..[4]` with default **0.0 for any missing field** (so a table without alpha makes the rectangle invisible); calls `InvalidateScreen`.
* `BTSetTexture` (`SetBitmapTexture`, 0x3b9da) exists in the binary (`(name, image, scale)`) but is not in the stub list.

## 11. Unknown

* `LedgerWindow::CleanUp` (declared in the header) has no function in the Mac binary; whether the ledger is ever destroyed at game exit or at "main menu" is not known. Nothing in the Lua depends on it.
* The second argument of the top modal's `AddChild(window, 1)` in `Grab`.
* The text of `TGameState::kMute`; whether any `TGameStateHandler` is ever registered (I saw none).
* Exact `TPfHiscores` persistence format (`SavePreferences`), tie-break details in `RecordScore`, and how `HideAllText` blanks unused rows.
* The exact TURect argument order in `DookieDropper::Draw` bars (read as x1,y1,x2,y2).
* What `_weeksPerColumn` holds beyond index 6.
* `SlotWheel` mechanics, `SignEditor::OnMessage`, `SlotMachine::Draw`, and the `Rollover` popup layout for droppers (see `design-questions.md` for Rollover).
* Which of `StartMovie`'s two bool arguments is passed to `TFlashHost::Start`.
* Whether the Windows/Reforged exe registers `GetTopModalWindow`, `SaveCreationToFile` or `LedgerUpdateDisplay` (the Mac binary does not; no Reforged Lua path needs them).
