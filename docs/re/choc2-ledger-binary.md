# Chocolatier 2 ledger: what the Mac binary says (`TLedger`, `TFactoryStatus`, the badge natives)

Source: the **i386 slice** of the Chocolatier 2 Mac app bundle's `Contents/MacOS/Chocolatier2` (unstripped), decompiled
into `re/choc2/decomp/` (`TLedger.c`, `TFactoryStatus.c`, `TDynamicWindow.c`, `TButton.c`, `TRedrawTask.c`, `TText.c`,
`TTextGraphic.c`, `_free/b.c` for `bsgDate`/`bsgDollarsUint`/`bsgScore`/`bsgGetMaxScroll`/`bsgSetStartLine`, `_free/r.c` for the
native registration, `_free/m.c` for the main loop). The Lua is the plain text under `Contents/Resources/assets/ui/ledger*.lua`
(`ledger.lua`, `ledger_badge.lua`, `ledger_messages.lua`, `ledger_factories.lua`, `ledger_inventory.lua`), `sim/*.lua`.
Format as `docs/re/choc2-factory-binary.md`: **FACT** (address given), **INFERENCE**, **UNKNOWN**. Addresses are i386-slice
addresses. The Windows `chocotwo.exe` is the same game, so the facts apply to it.

This settles the "ALL INFERRED" `Chocolatier 2 ledger natives` entry of the workbench decision log (engine: `src/game/choc2/LedgerWindow.*`,
`src/script/games/Choc2LedgerNatives.cpp`, `src/game/choc2/FactoryStatus.*`). Tab clicks, hit-testing and the click sound were
done in `docs/re/choc2-bottom-hud-clicks.md` and are not repeated; only what bears on the ledger state is added here.

## 0. Ranked discrepancies (what a player sees first)

| # | What | Binary | Engine now | Visible |
|---|---|---|---|---|
| 1 | **Badge date** and every other `bsgDate`/`bsgShortDate` string | `bsgDate(weeks, days)` returns "Month D, YYYY" (section 6); `bsgShortDate` "D Mon YYYY" | `bsgDate` is an unimplemented stub: the badge `date` Text is empty (headless dump shows `Text "date" label=""`), as are the weekly message `sim.lua:768`, the `q_minweek/q_maxweek/q_weeks` quest text and `travel.lua:377` | Always, on every ledger |
| 2 | **Factories tab drawing** (`FactoryStatus`) | Bars: 4 colour bands at fill >= 1, > 0.75, > 0.5, else; per factory the machine picture `image/machine_<type>_small`, the product `item/<name>` falling into it, the tray picture rotating, a blinking `image/missed_ingredient` on an idle factory; all animated while `StartProductionAnim` is on (during a trip) | Bars use bands >= 1, >= 0.5, >= 0.25, else (yellow where the original is red for 0.25..0.5); only a static `tray<N>_mini`; no machine, no product picture, no animation, no blink; `StartProductionAnim`/`StopProductionAnim` are logged no-ops | Whenever the Factories tab is shown (it is the default tab, see 5) |
| 3 | **Tab a fresh ledger opens on** | `ResetLedger` (called after every `LoadGame`) clears the remembered tab; a ledger built with no remembered tab shows **Factories** | Opens on Messages (or the Session tab) | First screen after loading/continuing a game |
| 4 | **Messages scroll** `bsgSetStartLine` | stores the Lua's number *unchanged* as the 0-based first line (the Lua passes `messageTop`, 1-based, so the first line is never shown at the top and the last page is one line short) | subtracts 1 | Messages tab, at top and bottom |
| 5 | **Money count-up** | 100 ms ticks, fixed step = max(1, gap/7), ~0.7 s | 50 ms ticks, fixed step = gap/20, ~1.0 s | Every sale/purchase |
| 6 | **Quest bar slide** | steps of 26 px (= (hide - show) / 2) every 100 ms: 53 -> 27 -> 1 -> 0 (3 ticks, 300 ms) | smooth 500 ms | Quest start/finish |
| 7 | **Badge rebuild** | `UpdateBadge` does not rebuild the badge (it only writes the four Texts); the badge is rebuilt only by `UpdateDynamicContent("badge")` (`medals.lua:88`) or a new ledger | `updateBadge` rebuilds the `badge` DynamicWindow every call | New-medal sparkle timing, flicker |
| 8 | Rest | see section 9 (SetLedgerMode scope, the 1e9 font tag, shared money, doubled tick, `view`/`mode` keys) | | Rare |

## 1. Class shape and shared state

**FACT** (`TLedger::TLedger` 0x1adc2, `~TLedger` 0x1afba): `TLedger` (window type `Ledger`, `ClassId` 0x19c2c) pushes itself onto a
static list `activeLedgers` on construction and removes itself on destruction. Its constructor registers five Lua natives, each a
static function that **loops over every ledger in `activeLedgers`**: `SetLedgerContents` (`DoSetContents` 0x1ad52), `UpdateLedgerContents`
(0x1a9c6), `SetLedgerMode` (0x1b974), `UpdateBadge` (0x1a992), `SetQuestHint` (0x1aa26). `ResetLedger` is registered elsewhere
(`_free/r.c`, with `UpdateDynamicContent`, `StartProductionAnim`, `StopProductionAnim`) and calls the static `TLedger::Reset`
(0x1aa86). So all natives act on all live ledgers, not on the topmost modal's.

The following are **static (class-level), not per window** (FACT, from the accesses in `TLedger.c`): `currentKey` (the tab),
`currentMode` ("port"/"map"), `currentHint` (the quest line), `money` (the displayed amount), `targetMoney`, `deltaMoney`, `questY`.
Per window: `showQuestY` (+0x30), `hideQuestY` (+0x34), the four unsigned numbers `rank` (+0x38), `weeks` (+0x3c), `days` (+0x40),
`score` (+0x44), all written by `UpdateBadge` from the table.

Consequences (FACT unless noted):
* A new ledger (every port/map swap builds one) does **not** reset the money: it shows the shared `money` and keeps counting to `targetMoney`. The remembered tab and mode and the quest bar's Y also carry over.
* `Init` (0x1a702) reads style keys `view` (default: the static tab), `mode` (default: the static mode), `data` (table; passed to `UpdateBadge` immediately, then released), `showQuestY`, `hideQuestY` (both default 0). `ledger.lua` passes only `data=gSim`, `showQuestY=0`, `hideQuestY=53`. The Lua never passes `view`/`mode`.
* Ticking is driven by `TRedrawTask` (section 3), not by a per-window animation: `TLedger` never starts one.

Children looked up by name (recursively, `GetChildWindow`): `contents` (a Window holding the three DynamicWindows `messages`, `factories`, `inventory`), `badge`, `money`, `date`, `rank`, `score` (Text), `quest`, `quest_text`, `btn_<tab>`, `to_map`, `to_map_label`, `to_port`, `to_port_label`. A missing child is simply skipped (`bsgHideWindow(NULL)` etc.); this is why `DialogLedger()` (`gDisableBadgeButtons = true`, no globe/port buttons) works.

## 2. `PostChildrenInit` (0x1b3c4): order of operations

1. `TWindow::PostChildrenInit`, then `FitToChildren` (the engine does the same).
2. Mode: if the static mode is not "port", hide `to_map` and `to_map_label`; if it is not "map", hide `to_port` and `to_port_label`. (INFERENCE on the sign of `str::compare`: the decompiler returns a char from an int compare, but `SetMode` and this function agree with "port shows to_map, map shows to_port", which is what the Lua wants; engine `Ledger::setMode` matches.)
3. `SetContents(currentKey)`; **if `currentKey` is empty, `SetContents("factories")`**.
4. If `questY == 800` (the sentinel `Reset` leaves), `questY = hideQuestY`. The `quest` child is moved to (its own x, `questY`) at once.
5. `currentHint` = the label `quest_text` was built with (so the Lua's initial hint, `"#"..SummaryText()` or nothing, becomes the hint).
6. `UpdateBadge(this, NULL)`: writes the Texts from the stored fields (section 4).

Quest bar at build is thus placed at the **static** `questY` (not from the hint). After a `ResetLedger` it is at the hide position and, if there is a quest hint, slides up (section 3) after the screen appears. A ledger rebuilt while the bar is already shown starts shown. The engine places the bar from the hint directly, with no slide at build.

## 3. Ticking: money, quest bar

**FACT** (`_free/m.c`, main): one `TRedrawTask` is created with `SetUpdateRate(10)` = a delay of `1000 / 10` = **100 ms** per tick (`TRedrawTask::SetUpdateRate` 0xf4a88). `TRedrawTask::Animate` (0xf4a60) calls the static `TLedger::DoTick`, which calls the virtual at vtable +0x38 of every active ledger (INFERENCE: that is `OnTaskAnimate` 0x1b058, the only override with the right shape), then `InvalidateScreen`. The task is paused when the app is deactivated (`m.c`) and restarted on activation. It has no window clock (`TAnimTask(NULL)`), so it keeps running under modal dialogs (INFERENCE: a null clock is the global one).

`OnTaskAnimate` (0x1b058), per ledger, per tick:
* Quest bar, first branch: if `currentHint` is empty and `questY != hideQuestY`: `questY += (hideQuestY - showQuestY) >> 1` (unsigned shift), capped at `hideQuestY`. Else if `currentHint` is non-empty and `questY != showQuestY`: `questY -= (hide - show) >> 1`, floored at `showQuestY`. With the Lua's 0 and 53: step 26; showing goes 53 -> 27 -> 1 -> 0; hiding 0 -> 26 -> 52 -> 53. After a step, for **every** active ledger the `quest` child is repositioned to (its screen x converted to the ledger's client x, `questY`) and its `quest_text` is re-set to `currentHint`.
* Money (the quest part falls through to this part in the same call). If `money == targetMoney` return. If `money < target`: when `target - money <= deltaMoney`, `money = target`, else `money += deltaMoney`. If `money > target`: same going down. Then `bsgDollarsUint(money)` text; if `money > 999,999,999` the string is built with the prefix `" <font size='25'>"` (shrinks the digits; INFERENCE on prefix vs suffix order, the tag is unclosed so a prefix is the only sane reading). The string is written to the `money` Text of **every** active ledger.
* Because the loop in `DoTick` calls `OnTaskAnimate` once per ledger and the state is static, **with N ledgers alive the quest bar and the money advance N steps per tick** (FACT from the structure; with a dialog over the port view N = 2, so the count-up is twice as fast then). Once the first ledger's step reaches the target the later ledgers find it equal and do nothing.

`UpdateBadge`'s money rule (0x19df6): the table's `money` is converted to an unsigned 32-bit (clamped to 0..4294967295; **negative money becomes 0**). If it differs from `targetMoney`: `targetMoney = new`, `deltaMoney = max(1, |new - money| / 7)` (integer division). So the whole count-up is 7 or 8 ticks (0.7-0.8 s at 100 ms) whatever the size, with a constant step, and the last step snaps. `ResetLedger(m)` (0x1aa86): `money = targetMoney = m`, `deltaMoney = 0`, `currentKey = ""`, `currentMode = "map"`, `DoSetQuestHint("")` (clears the hint and its Text in all ledgers), `questY = 800`.

Engine: `MoneyCounter` is 1/20 of the gap per 50 ms (1.0 s; exponent-free, so linear too, but different duration and tick), per ledger (with `snapToTarget` for covered ledgers as a workaround), and `Ledger::init` snaps to `data.money` at build; the quest bar is a 500 ms smooth `Swipe` (`kQuestSlideMs`, labelled a guess in the header). The binary's slide is 3 discrete jumps of 26/26/1 px at 100 ms. `ResetLedger` in the engine only snaps the counter (does not clear the tab, the mode or the hint).

`SetQuestHint(s)` (0x19d5c): `currentHint = s`, and the `quest_text` Text of this ledger is set to it **immediately** (so a cleared hint empties the text at once and the bar slides down empty; the engine matches that). The argument is passed to `Text` as it comes, with no `#` prefix added (the Lua's `quest.lua:411` passes `SummaryText()` raw; the initial text in `ledger.lua` is `"#"..SummaryText()`). UNKNOWN: whether a non-`#` label is looked up in the string table first (the engine writes it literally).

## 4. `UpdateBadge` (0x19df6) in detail

Reads from the table (unsigned clamp): `money`, `rank`, `weeks`, `days`, `score` (missing `money` keeps the current `targetMoney`; the others default to the previously stored value). Then **only if a child named `badge` exists** it writes, in this order:
1. `money` Text: `bsgDollarsUint(money)` of the **displayed** `money` (not the target), plus the 1e9 font prefix.
2. `date` Text: `bsgDate(weeks, days)` (the long form, written without a `#`).
3. `rank` Text: string-table key `rank_<rank>` (`Apprentice` ... `Chief Executive Officer`).
4. `score` Text: empty string when `rank == 0`, else `bsgScore(score)` = string-table `formatScore` (`"%1%/week"`) applied to `bsgDollars(score)` (INFERENCE on which stored field is passed: the stack argument is hidden by the decompiler; the field order and the Lua's `score` key make `+0x44` the only candidate).

It **does not** rebuild the `badge` DynamicWindow. The Lua builds the same four Texts and the medals (`ledger_badge.lua`); medals appear only after `UpdateDynamicContent("badge")` (`sim/medals.lua:88`, in `EvaluateMedals`) or the next ledger. `UpdateDynamicContent` (0x9a34) finds the **first** window of that name below the window manager's root and rebuilds it, so only one ledger's badge is rebuilt.

Match: money/score/rank text and the rank-0 score hiding match the engine (`scoreText`, `formatScore`, truncation via `bsgDollars`). The engine's `formatScore` inference (the workbench decision log item 1) is **confirmed**. Differences: the engine rebuilds the badge on every `UpdateBadge` (item 7 above): `sim/medals.lua:74` (`if gNewMedal then UpdateBadge(self) end`, after the award dialog) would rebuild and clear `gNewMedal` before `EvaluateMedals` rebuilds again at line 88, so the sparkle `Sprite` can be rebuilt away; the rebuild also makes a medal awarded by a quest show at once where the binary waits for the next ledger (INFERENCE: not run headless). The engine does not write `date`/`rank` natively; because it rebuilds, the Lua text shows, except for the date (item 1). The engine formats money with the `_MONEY_SIGN`/`_NUMERIC_SEPARATOR` globals; the binary uses the string-table `formatHundreds/Thousands/Millions/Billions` (`$%d`, `$%d,%03d`, ...). The output is the same for ordinary amounts.

## 5. Tabs

`SetContents(tab)` (0x1ab40): if a child `contents` exists, hide `messages`, `factories`, `inventory` (`bsgHideWindow`), then show the named one and call its `TDynamicWindow::UpdateContents` (destroy all children, run the `contents` file, `FitToChildren`: so the body is rebuilt **every time** the tab is shown, also when it is already shown). Then, whether or not `contents` exists, the child `btn_<tab>` is cast to a button and `SetOn(true)` (this clears the other radios of the group; `TButton::SetOn` 0x6f5d8 walks the parent's children twice for a radio). `currentKey = tab` is stored last. An unknown name hides all three bodies and stores the name (the engine logs and ignores it). Matches the engine (rebuild on show, radio on) except:
* **Default tab** (FACT, 0x1b3c4): an empty remembered tab opens **Factories**, not Messages. The remembered tab is cleared by `ResetLedger`, which the Lua calls after every successful `LoadGame` (`sim/sim.lua:412`; also `dev/devmisc.lua:40`). So the ledger on the first screen after Continue/Load shows Factories; `MainMenuFinishIntro` calls `SetLedgerContents("messages")` itself after building the port view (`mainmenu.lua:104-105`). The engine falls back to Messages and also keeps a `Session::ledgerTab` that survives a user change and a load.
* With **no active ledger** the binary's `SetLedgerContents` does nothing at all (the loop is empty); the engine records the tab in `Session` anyway.
* The engine empties hidden bodies; the binary leaves them as they were (their stale children stay, hidden). The engine's reason (`EnableWindow("scroll_up")` finds the first name match) is sound; in the binary `messages` comes first in the tree so its buttons win anyway (INFERENCE).
* `UpdateContents(tab)` (0x19cee): rebuilds only when `tab == currentKey` (the remembered key, not "which child is shown"); same effect in practice.
* `SetMode` (0x1b6e5) acts on every active ledger; the engine's `nativeSetLedgerMode` uses `Ledger::current` (top only). The binary's mode is static, so a ledger built later (a `DialogLedger` has no globe buttons, so it matters little) hides the opposite button at build.

## 6. `bsgDate`, `bsgShortDate`, `bsgScore`, `bsgDollarsUint`

**FACT** (`_free/b.c`; registration `_free/r.c`: Lua `bsgDate` = `bsgLongDate`, `bsgShortDate` = `bsgShortDate`, both `(weeks, days)`; `bsgDate` internally takes a 4th bool `short`):

* `w = weeks % 52`; `month = floor(w * 12 / 52)` (0..11); `day = (w - floor(month * 52 / 12)) * 7 + days`; `day = 1` if 0, capped at 28; `year = weeks / 52 + 1925` (0x785). The month name is the string-table key `month_<month+1>` (`month_12` = December).
* Long: string-table `formatDate` = `"%1% %%d, %%d"` with the month name substituted: "January 1, 1925" (day, then year). Short: the month name cut to its first 3 characters (`substr(0, 3)`, from the call arguments) and formatted `"%d %s %d"` (day, month, year): "1 Jan 1925".
* `bsgDollarsUint(n)` (0x20db4): billions = `n / 1e9`, then millions, thousands, ones groups; picks `formatBillions` (`$%d,%03d,%03d,%03d`), `formatMillions`, `formatThousands`, `formatHundreds` (`$%d`) from `strings.xml`, so comma and `$` are literal in the table. `bsgDollars(int)` (0x215c6): `"-"` + `bsgDollarsUint` for a negative argument (INFERENCE on the exact negation, the argument is hidden), else `bsgDollarsUint`. `bsgScore` (0x2168a): `formatScore` with `bsgDollars(arg)`.

Callers needing `bsgDate`/`bsgShortDate`: `ui/ledger_badge.lua:33`, `ui/help9.lua:41` (`bsgDate(100, 2)`), `sim/sim.lua:768` (weekly message), `sim/quest_functions.lua:67,76,250`, `sim/travel.lua:377`, `dev/devquest.lua:71`. Each expects a string. The engine returns an unimplemented-stub empty string.

## 7. Messages and Inventory bodies, scroll natives

Both bodies are pure Lua (`ledger_messages.lua`, `ledger_inventory.lua`); only the natives below are native.

* `bsgGetMaxScroll(name)` (0x21a38): finds the Text named `name` **inside the top modal window only** (`FindTextWindow`, `_free/f.c`), returns -1 if none. `TTextGraphic::GetMaxScroll` (0x35cee) walks the wrapped lines **from the last upwards**, adding each line's own height (`(lineHeight_i + padding) * scale`) until the sum exceeds the box height, and returns the index (0-based) of the first line that must be skipped to keep the last line visible: `lineCount - linesThatFit`, 0 when everything fits. The engine's `max(0, lines - floor(h / (size + padding)))` equals this for uniform lines (INFERENCE: wrapped lines of one font have the same height).
* `bsgSetStartLine(name, v)` (0x21b1c): walks **every window under the window manager's root** (`+0xc`), and for each Text whose name matches stores `v` as a float into the text graphic's start-line field (+0x58). `TTextGraphic::Draw` uses `(int)startLine` as the 0-based first drawn line (and the fraction as a sub-line offset). **There is no `- 1`.** The Lua is 1-based and the leading `#` line is an empty line 0 that the off-by-one hides at the top (section 12.1); the engine should not subtract 1.
* Inventory: `UpdateLedgerContents("inventory")` after scrolling (`ledger_inventory.lua:135,147`): rebuild only if the remembered tab is `inventory`. Nothing native.
* Messages are refreshed by `FlushMessages` -> `UpdateLedgerContents("messages")` (`sim.lua:502`) only when that tab is the remembered one.

## 8. `FactoryStatus` (`TFactoryStatus`, ctor 0x13702, `Init` 0x13c84, `Draw` 0x1323e, `OnTaskAnimate` 0x13200)

Window type `FactoryStatus`, a `TAnimationWindow` with a **50 ms** tick (constructor argument 0x32). `OnTaskAnimate` adds the tick delay to an elapsed-ms counter (+0xcc). Two static flags/functions are the Lua natives `StartProductionAnim` (`AnimateProduction` 0x13226: `animating = 1`) and `StopProductionAnim` (`StopProduction` 0x13232: `animating = 0`); the flag is one global for all FactoryStatus windows. The Lua sets it around each trip week (`sim/travel.lua:72-113`) and clears it in `portview.lua:121` and `mainmenu.lua:232`. (INFERENCE: the initial value of `animating` is 0, a static.)

Style keys: colours `colorLoads`, `colorFull`, `colorMedium`, `colorTrouble` (default opaque black `0,0,0,1`), tables `bars` and `products` (engine reads the same).

Constructor loads six textures, each defaulting to `temp/error`: `image/tray2_mini` ... `image/tray6_mini` (index = recipeSize, 2..6) and `image/missed_ingredient` (+0xc0).

`bars`: each entry `{left, top, right, bottom, fill}`; the rectangle is stored as unsigned ints with `top = bottom - (bottom - top) * min(fill, 1)` (a bar fills from its bottom upward), and the unclamped `fill` is kept for the colour. `Draw` fills each rectangle with **one colour by fill**: `fill >= 1.0` -> `colorLoads`; `> 0.75` -> `colorFull`; `> 0.5` -> `colorMedium`; `<= 0.5` (including 0) -> `colorTrouble`. With the Lua's colours (`GreenColor`, `YellowColor`, `YellowColor`, `RedColor`) that is green >= 1, yellow between 0.5 and 1, red <= 0.5. Engine `FactoryStatus::band` uses 1.0 / 0.5 / 0.25: a bar between 0.25 and 0.5 is yellow in the engine and red in the original, and a bar at exactly 0.5 is red in the original, yellow in the engine. The engine draws the fill height and alpha correctly otherwise. (UNKNOWN: whether `FillRect` coordinates are window-local; the engine draws them relative to the window, which is the only layout that fits the Lua.)

`products`: each entry `{x, y, productName, rate, typeName, recipeSize}` (the Lua passes `x, y` = the factory's circle centre). The window builds, per entry: texture `item/<productName>` (+8), texture `image/machine_<typeName>_small` (+0), the slot index `recipeSize`, and a **period** from `rate` (0 for rate 0, else 1000 ms for rate 1-20, 667 for 21-40, 500 for 41-60, 400 above 60). The engine's comment ("the Lua names the machine picture but never uses it") is therefore wrong: the native uses it.

`Draw` order (FACT, 0x1323e): window background (`TWindow::Draw`), all bars, then the **products**, in three passes per entry:
1. If the item texture exists and period != 0: the item is drawn at x, with `y` moving from the entry's `y` to `H - 16` (H = the window's height, 157 for the ledger) linearly over each period (`t = elapsed % period / period`, `y = y0 + (H - 16 - y0) * t`), with alpha `0.5 + 0.5 t` — when `animating` is on. When `animating` is off it sits at `y = H - 16` with alpha 1.
2. The machine picture is drawn at (x, y), scale 1, no rotation.
3. The tray picture for `recipeSize` is drawn at (x, y), scale 1, **rotated by `t * 2 pi`** (`t` as above; 0 when not animating or period 0).
4. If the period is 0 (rate 0 or the factory idle through missing stock) and `(elapsed % 1000) / 500 == 1` (the second half of each second), `image/missed_ingredient` is drawn at (x, y) with alpha 0.65 (0x3f266666): a blink. This also runs when `animating` is off.

(INFERENCE: the 4th texture argument is an alpha and the 5th a rotation in radians; the call is `Draw(x, y, scaleX, alphaOrScaleY, rotation, flag)` and the only consistent reading of `0x3f266666`, `0.5 + 0.5 t` and `2 pi t` is alpha, alpha, rotation. UNKNOWN: whether the 4th is alpha or vertical scale for the falling item.) What the engine draws instead: only `tray<N>_mini` centred, no rotation, no machine, no item, no blink.

Lua semantics worth keeping: `ledger_factories.lua` sets the entry's `rate` to 0 for an idle factory (missing stock) so the blink shows; `UpdateLedgerContents("factories")` rebuilds the tab after configuration changes (`factory_status.lua:95`, `travel.lua:335`, `sim.lua:534`, `quest_functions.lua:708`).

## 9. Remaining engine-vs-binary notes

* The engine's header says the ledger "keeps no `data` reference" and takes `data.money` at build: the binary calls `UpdateBadge(data)` in `Init` (which targets the money and stores rank/weeks/days/score) but takes no snap; the `badge` Texts are written in `PostChildrenInit`.
* Money Text at >= 1,000,000,000: font size 25 tag (section 3); the engine writes plain text. Negative money: the binary shows `$0` (clamped); the engine shows what `formatDollars` gives. Rare.
* `FitToChildren` in `PostChildrenInit` (FACT) confirms the engine's note (4) of the workbench decision log; the binary also runs it for the `DynamicWindow` after every build.
* `view`/`mode` Init keys exist but are unused by the shipped Lua.
* Sounds: **none** in `TLedger` or `TFactoryStatus`; the tab clicks use the generic button sound (`docs/re/choc2-bottom-hud-clicks.md`); `TButton::StateUpdate` (0x6ef66) plays the per-state sound on a state change and `OnMouseUp` plays the click sound; the ledger Lua buttons set neither.
* `TLedger::Draw` (0x19c40) is just `TWindow::Draw`: there is no native drawing in the ledger (confirms the engine's reading).

## 10. Unknown

* Whether `TRedrawTask` ticks are exactly 100 ms in practice (delay semantics of `TAnimTask::SetDelay(100, true, true, false)` are not decoded; the flags might mean something other than "periodic").
* The argument of `bsgScore`/`bsgDollars` in `UpdateBadge` and the sign handling of `bsgDollars(negative)`.
* The initial value of the static `animating`, and whether a `FactoryStatus` that is not shown still counts elapsed time.
* Whether `bsgDollars` on a score of 0 at rank >= 1 draws "$0/week" (the binary does; it hides only on rank 0).
* The exact alpha/scale meaning of the 4th texture-draw argument (section 8).

## 11. Verification done

Headless run (`--userdir` under `/tmp/ledgerre`, tutorial port, `dump`): the badge `date` Text has label `""` while `rank` is `Apprentice` and `money` counts `$0` -> `$1,500` (300 ms) -> `$3,000` (600 ms) toward 5000, i.e. the engine's 1/20-per-50-ms rate (the binary: step 714 per 100 ms, done after 7 ticks); the ledger opened on `inventory` at that point of the tutorial (set by the quest script). No engine code was changed.

## 12. Messages scroll, shared money, text labels

Sources: `re/choc2/decomp/TTextGraphic.c`, `TText.c`, `TWindowScript.c`, `TLedger.c`, and the plain Lua under
the Chocolatier 2 Mac app bundle's `Contents/Resources/assets/` (`ui/ledger_messages.lua`, `sim/sim.lua`). The Lua passes `bsgGetMaxScroll() + 1` and `bsgSetStartLine` adds no `- 1`.

### 12.1 Messages scroll: what the original really shows

**FACT**
* `TTextGraphic::GetMaxScroll` (0x35cee) returns a 0-based index `F` = the first line of the last page (0 if all lines fit; the loop adds `(padding + lineHeight) * scale` per line from the last line upwards and stops when the sum exceeds the box height `+0x1c`). Lines `F .. n-1` therefore fit.
* `TTextGraphic::Draw` (0x36a5c): `first = (int)startLine` (startLine `+0x58` clamped at 0); the sub-line fraction shifts the text up by `frac * lineAdvance (+0x44)`; lines are drawn **consecutively from line index `first`**, each advancing the pen by `padding (arg 3) + lineHeight`; a line is skipped once its baseline (`lineHeight*scale + pen`) is below the box height. So `startLine` = 0-based index of the first drawn line. `bsgSetStartLine` stores the Lua number unchanged (section 7).
* Lua (`ledger_messages.lua`, `sim.lua:482-497`): `messageTop` is initialised to 1; `scroll_up` is enabled when `messageTop > 1`, `scroll_down` when `messageTop < bsgGetMaxScroll("messageText") + 1`; scroll steps of 7 lines (`scrollUp` snaps to 1 when `messageTop <= 7`; `scrollDown` clamps to `max + 1`); the file ends with `scrollBottom()`, i.e. `messageTop = bsgGetMaxScroll + 1` and `bsgSetStartLine(.., messageTop)`. **The Lua is written for 1-based lines** ("1 = top", "max+1 = bottom") and never compensates (no `- 1` anywhere, the natives' conventions are as above).
* `gSim.messages` starts as `{"#", welcome}` and the table is capped as "30 messages, plus the leading `#`" (`sim.lua:116,478`); the text is `table.concat(messages, "\n")`, so it begins `"#\n<welcome>..."`. `TText::Init` (0x5cc82, the `label` key) strips a leading `#` with `substr(1)` and then uses the rest literally (no string-table lookup); every other label is looked up in the string table (unless it consists only of characters in a small set, probably digits and spaces: the set is in the data section and was not read). The Messages body is built through `Text{label=gSim.messageText}`, i.e. through `Init`, so the displayed text is `"\n<welcome>\n..."`: **line 0 is an empty line**.

**Consequence (FACT for the arithmetic, INFERENCE that a lone `\n` makes an empty counted line in `RenderText`, see Unknown):**
* At the top (`messageTop = 1`) the original hides line 0, the empty line produced by the stripped `#`, so the first real message is the first visible line. The Lua's "1 = top" is therefore correct *because of* the leading `#` line. The off-by-one is deliberate or at least harmless here.
* At the bottom (`messageTop = F + 1`) the original starts drawing at line `F + 1`: the last page shows `k - 1` lines (the first line of the true last page is hidden) with one line of blank space at the bottom of the box. This is visible only when the log is longer than one page and is the shipped behaviour (INFERENCE that it shipped, since no code compensates; the arithmetic itself is FACT).
* The engine passes `static_cast<int>(line)` unchanged with overrun allowed (`Choc2LedgerNatives.cpp`), so both ends match the original.

### 12.2 Money count-up with several ledgers alive

**Engine deviation (deliberate project decision):** the engine keeps one shared state and steps the money ONCE per 100 ms tick however many ledgers are alive; the original's N steps per tick (N times faster count-up with a dialog ledger open) is not reproduced. The recommendation below describing N steps is the original's behaviour, not the engine's.

**FACT** (`TLedger::DoTick` 0x1ab16, `OnTaskAnimate` 0x1b058, `UpdateBadge` 0x19df6; the section 1 and 3 statements re-checked):
* Shared (class-level statics): `money`, `targetMoney`, `deltaMoney`, `questY`, `currentHint`, `currentKey`, `currentMode`, and the list `activeLedgers`. Per ledger: `showQuestY`, `hideQuestY`, rank/weeks/days/score, and the child windows.
* `DoTick` walks `activeLedgers` and calls each ledger's `OnTaskAnimate`. Each call: (a) steps `questY` (if the hint state requires it) and repositions the `quest` child of **every** ledger; (b) if `money != targetMoney`, steps the shared `money` by `deltaMoney` (snapping to the target when the remaining gap is `<= deltaMoney`), formats it and writes the string to the `money` Text of **every** ledger.
* So with N live ledgers the shared money (and `questY`) advances **N steps per 100 ms tick**; with a dialog's Ledger over the port's, N = 2 and the whole count-up (nominally 7-8 ticks) takes about half as long (4 ticks, 0.4 s). Both ledgers always show the same number because both write the same string. A ledger built later shows the shared current `money` immediately (it is not reset) and joins the count.
* `UpdateBadge` (called for all ledgers by the Lua native, and once per ledger from `Init`) sets `targetMoney`/`deltaMoney` only if the new value differs from `targetMoney`; `deltaMoney = max(1, |new - money| / 7)` computed from the *current shared* `money` (so calling it again mid-count with the same value does nothing; a new value recomputes the step from where the count currently is).
* `ScreenToClient` in the quest part is called on the ticking ledger (`this`) for every ledger's `quest` child (an upstream quirk, harmless when the ledgers share the same origin; INFERENCE that it is harmless, because the dialog ledger may sit at a different screen position).

**Engine:** `money`/`target`/`delta` and the quest Y are one shared state (static, not per window), stepped once per tick by one 100 ms ledger task; every live ledger writes the same text, so a dialog ledger continues the shared count-up (`LedgerWindow.cpp`, `Choc2LedgerNatives.cpp`).

### 12.3 Text labels

* `TText::Init` label: `#` prefix stripped and used literally; other labels go through the string table (12.1).
* The Lua natives `SetLabel`/`UpdateBadge`/`SetQuestHint` write through the Text's virtual at vtable `+0x70` (`TWindowScript::SetLabel` 0x74a34 on a `TText`; `TLedger::OnTaskAnimate` for `money`/`quest_text`). INFERENCE: that slot is `TText::SetText` (0x5cb8c), which compares with the current text and calls `TTextGraphic::SetText` with **no `#` strip and no string-table lookup**. If right: a runtime `SetLabel("x", "#foo")` shows a literal `#foo`, and `SetQuestHint(s)`/badge strings are literal. Still UNKNOWN: confirm the slot by reading the `TText` vtable. Consequence for the messages tab: `ConcatenateMessages` calls `SetLabel("messageText", "#\n...")` on the old body before the rebuild; harmless because `UpdateLedgerContents` rebuilds it straight after.

### 12.4 Unknown

* Whether `RenderText` (0x381ea) creates a counted line for a leading `\n` (an empty first line) and what height it gets. The Lua only works at the top if it does.
* Whether `StartLine` beyond the line count draws nothing or is clamped somewhere above `Draw` (not seen).
