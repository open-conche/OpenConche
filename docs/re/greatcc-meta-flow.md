# Great Chocolate Chase (GREATCC): everything around the levels (tutorials, story, quests, world map, upgrade shop, rewards, menus)

Scope: tutorials, plot/story, quests, world map, prelevel upgrade-shop windows, rewards windows, help/upsell/xsell/hiscore/menu
shells. Audio is in `docs/re/greatcc-audio.md`. Sources: Ghidra decompile `re/greatcc/decomp/<Class>.c` (Mac i386), shipped Lua/XML under
the Mac bundle's `assets/`. Tags: **F** fact (decompile / file bytes), **I** inference, **U** unknown.
All addresses below are Mac VM addresses from `re/greatcc/index.tsv`. Mac and Windows data: all 107 ZXML files in
`re/greatcc/pfp_extracted` decompress byte-identical (modulo CR/LF) to the Mac XML (**F**, checked); the Windows Lua is Lua 5.0 bytecode and was
not diffed, so Lua parity is **I** (same game, same build year).

Dependencies on other analysts' classes (not described here): `VSpriteManager` (hit testing, effects), `VLevelManager` (progress and
scores), `VUpgradeManager`/`VUpgrade` (shop model), `VValueManager`, `VAchievementManager`/`VAchievement`, `ShopWindow`/`CSLevel`
(level flow), `VSoundManager`.

## 0. Big picture: how a level session flows through these pieces

1. World map (`scripts/worldmap.lua`, native `VWorldMap`) -> player clicks a globe / Play -> `SetCurrentLevel(n)` + `CloseWorldMap()` ->
   (non-story mode asks difficulty first) -> `scripts/game.lua` is swapped in with `scripts/loading.lua` on top (first time) or the loading
   dialog alone (game already loaded). **F** (worldmap.lua).
2. `ShopWindow` loads the level; per-location Lua table `location_<loc>` supplies `plot = <loc>Plot`; the manager singletons are fed:
   `VPlotManager::LoadFromTable(plot table)` then `LoadCurrentUser`; `VQuestManager::LoadFromLuaFile("locations/quests.lua")` then
   `LoadCurrentUser`; `VTutorialManager::SetSpriteManager`, `ResetTutorials`, `SetCurrentLevel`, `VQuestManager::SetCurrentLevel(n)`,
   `VPlotManager::SetCurrentLevel(level)` (ShopWindow.c ~457-465, 4231-4258, 4497, 4586-4592, 4652; **F**).
3. At level start, in `ShopWindow::OnTaskAnimate` (**I** for the exact flag meaning): when the pre-level flag on `CSLevel` (+0x28) is set it
   runs Lua `DoModal('scripts/prelevel.lua')` (the upgrade shop, `VPrelevelWindow`), then clears the flag; once the shop is dismissed and
   `CSLevel` reports it has plot items (vfunc +0x14), it calls `VPlotManager::LaunchPlotDialog` (diary page). Only after that does the level
   timer start. **F** order shop -> plot: the shop's `Close()` calls `ShopWindow::UpgradeComplete`, which sets the plot flag (+0x1dd) when the vendor exists
   (5.1); the "has plot" predicate is **I**.
4. During play: tutorials may lock input (section 1); quests are evaluated at level end (section 3).
5. Level end (`ShopWindow::DoEndLevel`, **F**): `VQuestManager::PostLevelProcess()` (quests -> rewards -> text), `VLevelManager::IncrementLevel()`;
   if the new current level `% 10 == 1` and `highest_complete < current` it sets managed value `do_map_transition = 1`; saves values;
   `CheckAllAchievements("adventure")` (and `"endgame"` when level > 59 and passed). `scripts/endlevel.lua` calls `ShowNewAchievements(0)`.
   `GotoNextLevel` (endlevel.lua): story mode: if `firstpeek=="1"` and current level > 20 and highest >= 20, or current level > 60 and highest
   >= 60 -> `DoModal("scripts/endgame.lua")` (final diary page + Credits); else if `do_map_transition==1` -> world map with the dot-trail
   animation; else straight into loading the next level. **F** (endlevel.lua:8-31).

Level numbering: 60 levels (`levels/level01..60.xml`) plus `levels/goals.xml`; locations of 10 levels each: iquitos 1-10, grytviken 11-20,
namche 21-30, krakatoa 31-40, kamchatka 41-50, gaberoun 51-60 (**F**: `<location>` and `<plot>` of levels 1/11/21/31/41/51/60).
`values.xml`: `current_level` min 0 max 61, `highest_level` min 0 max 60 (both saved) (**F**) - 61 is the "past the end" value.
Every one of the 60 level files has exactly one `<plot>` element named `story_<loc>_<k>` (k = 1..10 within the location) (**F**, grep counts 60).

## 1. Tutorials: `VTutorialManager`, `VTutorial`, `VTutorialStep`, `VTutorialSpider`, `VTutorialListener`, `VTutorialWindow`

### 1.1 Data: `locations/tutorials.lua` -> global `game_tutorials`, handed to `VTutorialManager::LoadFromTable` (via `tutorials = game_tutorials` in game.lua:139)

`VTutorialManager::LoadFromTable` (0x58b28): iterates every key of the table; a key whose value is a table creates a `VTutorial` named
`lowercase(key)` (so `tutorial_1`, `tutorial_upgrades`, `tutorial_endless_1`); non-table keys are skipped for creation. `modes = {0}` at the top of
`game_tutorials` is a table, so it makes a tutorial named `modes` too (no steps, no trigger: nothing checks it) and it is also what the others
read through the parent chain (**F**: `GetStyle("modes", -1)`; `tutorial_1` has no `modes` of its own yet InMode succeeds in story mode). After loading it calls
`LoadCurrentUser`. Manager passes persistence slot index **0 to every VTutorial** (disassembly: immediate 0 at the call to `VTutorial::Create`)
(**F**) - see 1.6.

Per-tutorial keys (`VTutorial::LoadFromTable` 0x57a9a, **F**):

| key | meaning / default |
|---|---|
| `minlevel` | int, default 0 (search depth -1: the parent's value is inherited) |
| `maxlevel` | int, default 0x7fffffff (this table only) |
| `window` | name of a child window of the current top modal used for click-through tests (default ""): only `tutorial_upgrades` sets it (`prelevel_container`) |
| `pause` | bool, default true (depth -1) |
| `repeatable` | bool, default false (this table only) |
| `tryonce` | bool, default false (this table only) |
| `modes` | list of ints (game modes where it may run): 0 = story, 1/2/3 = endless difficulties. Stored as `std::set<int>` |
| `trigger_type`, `trigger_object` | read as strings by the manager at check time (default ""); not in `VTutorial::LoadFromTable` |
| `steps` | array of step tables, 1-based, each becomes a `VTutorialStep` (children of the tutorial sprite) |
| `skip` | style of the skip button (a `VButton` created from it); `VTutorial` keeps it as the "skip" control |
| `assets`, `highlight_objects`, ... | `VObject::LoadFromTable` base keys (background art) |

Step keys (`VTutorialStep::LoadFromTable` 0x5a112, **F**), defaults from the constructor (0x5afc6): `duration` -1, `delay` -1 (ms; negative =
infinite / none), `pause` true, `dim` true, `skip` true, `clickAny` false:

| key | meaning |
|---|---|
| `duration` | int ms, default -1. Step "duration complete" when `now - begin >= duration`; always complete if no begin time yet or duration < 0 |
| `delay` | int ms, default -1. "Delay complete" analogous, measured from `Advance()` being requested |
| `pause` | bool default true: sprite-manager "soft pause" while the step is up (only if the tutorial's own `pause` is also true) |
| `dim` | bool default true: darken the whole scene except exempt sprites |
| `skip` | bool default true: show the "skip tutorials" button on this step |
| `clickAny` | bool: any click advances. Final value = `clickAny` OR (`duration < 0` AND no trigger names at all) - wait, see note |
| `highlight_objects` | array of sprite names: only exempt from dimming |
| `action_objects` | array of sprite names: exempt from dimming AND clickable (added to the exclusive set) AND looked up by name |
| `trigger_objects` | array of `{ name, kind, action=bool }`; each named sprite gets trigger kind `kind` (default 0 = kNoTrigger), is looked up, exempt from dimming, and (unless `action=false`) is clickable |
| `button` | style: a `VButton` (the "Continue" button, name `button_advance` in the Lua) added as a child |
| `x,y,w,h`, `label`, `style`, `flags`, `children` | `VTiledObject` layout of the speech panel: if the step's tile size is not both > 0 then the step has no panel (`[0x2e0]` true); otherwise a `VTextSprite` is created from the same table to show `label` |

Note on `clickAny` (**F**): final flag = `GetBoolean("clickAny", old)` OR (`duration < 0` AND the trigger-name set is empty)... precisely: it
is false only if `clickAny` is false AND (`duration >= 0` OR there is at least one trigger object). So a step with neither a duration nor a
trigger object advances on any click.

Trigger enum (`locations/defines.lua`, **F**): `kNoTrigger=0, kClick=1, kResume=2, kArrival=3, kIgnore=4`.

Meaning of triggers (listener + window code, **F**):
- `kClick`: a click on that sprite (`VTutorialListener::OnClick`) OR a UI button message with that name (type-3 message, `VTutorial::HandleMessage`) advances.
- `kArrival` (3): `VTutorialListener::OnArrival` on the named sprite advances. `VStation::OnArrival` calls it on every `VTutorialListener` in the station's active listener set, after the machines and the customer, and only when the station is not suspended (**F**).
- `kResume` (2): `OnResume(flag, sprite)` advances when the flag is false. `VSuspendable::Update` (when a wait ends) and `VSuspendable::Reset` (of a suspended one) call it on the sprite's tutorial listeners with the sprite's suspended flag as it is *after* `OnResume` returned, so a wait that sets another wait does not count (**F**). `{ "tut_cust_1", kResume, action=false }` is used to wait for a customer to be ready to continue ordering without making them clickable.
- `kNoTrigger` (0): the sprite is just exempt/clickable-via-others; it never advances. Used for `upgrade_list`.
- `kIgnore` (4): `VTutorial::HandleMouse` treats a window with this trigger as transparent (temporarily collapses its width to 0 while probing the point, so the hit test sees what is underneath). Used for `prelevel_background`. `TWindow::ChildWindowFromPoint(w, p, -1)` is the deepest shown window under the point (the container itself when none is).

Lookup depths of the step keys (**F**, the depth argument of each read): `duration`, `delay`, `pause`, `dim`, `skip` and the arrays `highlight_objects`, `action_objects`, `trigger_objects` 2 (the step, the `steps` array, the tutorial); `button` 1; `clickAny` -1; a trigger entry's `action` 0. The tutorial's own `skip` table (the skip button) is read at depth 1. A table found under a boolean's key (the tutorial's `skip = tutorial_skip_button`) is not a boolean: the step's `skip` stays true.

### 1.2 Manager state and entry points

`VTutorialManager` singleton fields (**F**): `current level shared_ptr` (+0x30), `active tutorial` (+0x38), `show tutorials` bool (+0x2c),
sprite manager (+0xc), map name -> tutorial (+0x18).
- `ShouldShowTutorials()` = flag; loaded in `LoadCurrentUser` from RState int block **`show_tutorials`** (== 1); `ShowTutorials(show, save)` sets
  it and, if `save`, writes the int block (Options "Tutorials" checkbox calls `ShowTutorials(!current, true)`; skip button calls
  `ShowTutorials(false, true)`).
- `TutorialInEffect()` = an active tutorial exists AND `VTutorial::vfunc +0x150` (IsComplete) is false.
- `CheckTutorial(name)` (0x586ca): only if no tutorial is active and the tutorial exists: `cur level = currentLevel->number` (+0xc of the level
  object; 0 if none); require `tutorial->InMode(TSettings.mode)` (TSettings +0x10), `tutorial->InRange(level)` (`min <= level <= max`) and
  `tutorial->ShouldShow()`. If tutorials are disabled (`show_tutorials != 1`): mark the tutorial state started (`SetState(1, save)`) and set
  GameState `TutorialEnd = 1`; otherwise `Activate(true)` and if that succeeds `PushTutorial`.
- `CheckTutorialsByTrigger(typeStr, objectStr)` (0x58832): for every tutorial until one is pushed: read `trigger_type` and `trigger_object`
  from its style (defaults ""); if both equal the arguments (and non-empty first) call `CheckTutorial`.
- `PushTutorial` (0x58522): requires a top modal window; creates a `VTutorialWindow` (0x114 bytes) as a child of it, `SetTutorial`, runs a
  window animation at `OBJECT_UPDATE_INTERVAL`, sets `VSpriteManager::SetLockDown(true)` on the level sprite manager, and sets GameState
  `TutorialTime_<PLAY_COUNTER> = 1`. If there is no top modal it pops immediately.
- `PopTutorial` (0x583fa): clears the active pointer, `SetLockDown(false)`, GameState `TutorialTime_<n> = 0`, and sets GameState `TutorialEnd`
  = `!show_tutorials` (analytics-style metric states; **I** that they are metrics).
- `ResetTutorials`: `Init()` each tutorial (re-arm) and clear the active pointer.

Who calls the trigger/check entry points (**F**): `VCustomer` (customer arrival: `CheckTutorialsByTrigger("customer_arrival", <customer name>)`
at VCustomer.c:2505; `"customer_order"` at :597), `ShopWindow` when the endless-mode countdown ends (`CheckTutorialsByTrigger("endless_start","")`),
`ShopWindow` when endless difficulty steps up (`CheckTutorial("tutorial_endless_2")`, ShopWindow.c:3247), `VPrelevelWindow::OnTaskAnimate` once
(`CheckTutorial("tutorial_upgrades")`), `VDelivery` (`CheckTutorial("tutorial_zepplin")`, but that tutorial is commented out in the Lua so it
does nothing). Other `TutorialInEffect()` readers (used to suspend things while a tutorial is up): `VCharacter`, `VDisplay`, `VDisposal`, `VShopHud`,
`ShopWindow`, `VPrelevelWindow`, `VTutorialWindow`.

### 1.3 `VTutorial` state machine

State int at +0x228, only ever increases: `SetState(s, save)` ignores `s <= current`; on increase and `save` it calls `UpdateCurrentUser`.
`ShouldShow()` = `state == 0 || (state == 1 && !tryonce) || repeatable` (**F**). States: 0 = never run, 1 = started/skipped-by-option, 3 = finished
(set by `AdvanceStep` end). State 2 is never written in the code I read (**U**).
Persistence: RState **binary block `tutorials`** (`GetBlock`, lazily fetched through `TSettings->RState`); `LoadCurrentUser` reads byte `[idx]`
into state; `UpdateCurrentUser` writes the byte and calls the block's save (+0x18). Because the manager passes idx 0 for every tutorial
(**F**), all tutorials read and write the same byte (**I**: original bug/quirk; in practice tutorials 1-6 are `repeatable` so it only affects
`tutorial_upgrades` (tryonce) and the endless ones). A reimplementation that wants save-compat with original saves should replicate "all share
byte 0"; a faithful reimplementation should do the same.

Per-run fields: current step number `+0x248` (0 before start, steps numbered 1..N), `done` flag `+0x24c`, `+0x24d`, "deferred advance"
flag `+0x260`, fade effect `+0x258`, spider `+0x264`.
- `Activate(true)`: `Init()`-like reset (vfunc +0x28), done flags cleared, `VTutorialSpider::Activate`, then `AdvanceStep()`, then `SetState(1, save)`.
  `Activate(false)` only recomputes the step number from `GetNumSteps()` (+0x138) and advances (resume).
- `AdvanceStep` (0x572b6): if a fade effect is still running it just sets `+0x260 = true` (retry later from `Update`). Otherwise:
  1. tell the current step it was advanced (`VTutorialStep::Advance()`: stamp "advance requested" time, once);
  2. require the step's `DurationComplete()` AND `DelayComplete()` (e.g. a step with `duration=2000` cannot be advanced by an early click; the
     request is remembered as `+0x260` and retried from `Update` every frame until both pass);
  3. if tutorials were turned off meanwhile: jump to the last step, end it, reset the spider, mark done (this is how Skip works);
  4. else `step++`; if the new step does not exist: `End()` the previous step with no effect, spider reset, `done=true`, `SetState(3, save)`.
     Otherwise `End()` the previous step with a **500 ms** `VAlphaEffect` fade-out (`Create(false, 1.0, 500, -1.0, 0)`), then start a
     **500 ms fade-in** alpha effect for the new step (`Create(true, 1.0, 500, -1.0, 0)`), `VTutorialStep::Begin(effect, pause)` and
     `VTutorialSpider::SetStep(step)`.
- `Update`: runs the sprite update and, if a deferred advance is pending and the step's duration/delay are complete, clears `+0x260` and calls `AdvanceStep`.
- `HandleMessage(msg)`: only type 3 (button/command messages): if the message name equals the skip button's name
  -> `ShowTutorials(false, true)` + `AdvanceStep` (which then collapses the tutorial); else if `GetTrigger(name) == kClick` -> `AdvanceStep`.
- `HandleMouse(window, pt)` (0x568dc): decides whether a click may reach the game. If `window` is null or the tutorial's `window` name is
  empty -> returns true (allow). Otherwise find child window `window`, convert the point, then repeatedly `ChildWindowFromPoint`; windows with
  trigger kind 4 (`kIgnore`) are temporarily collapsed so the probe sees through them; then walk the found window and its ancestors up to the
  container: allow if any has a trigger kind other than -1. `VTutorialWindow::HandleMouse` returns `!allow` (so "not allowed" swallows the click).
- `VTutorial::Update` skip handling and the skip button: `skip` button style is the tutorial's `skip` key. Clicking it posts message type 3 with that name.

### 1.4 `VTutorialStep` run behaviour

- `Begin(effect, tutorialPause)`: stamp begin time (once), if it has a panel add the fade-in effect and make visible (else just start the effect); get the
  manager's sprite manager and call its vfunc +0x30 (**F**: `VSpriteManager::SoftPause`, 0x4d91e) with `step.pause && tutorialPause`. While set, the avatar can still walk (so `kArrival` steps work), machines and conveyors keep running, seated customers stop losing patience, no customers arrive and the level timer is stopped (it is stopped during any tutorial regardless); the full list of what the flag gates is `sprites` 2.6.
- `End(effect)`: with a panel, run the given (fade-out) effect; always un-pause (+0x30 false). `Update` hides the step and clears the begin time when the fade-out finishes.
- `GetTrigger(name)` -> `map<name, kind>` or -1 when absent.

### 1.5 `VTutorialSpider` (what actually dims and locks)

Per step it keeps three per-step maps: `exceptions[step] = set<weak sprite>` (+0x34, exclusive listeners), `exemptNames[step] = set<name>` (+0x4c),
`effects[step][name] = tint` (+0x6c) and a global dim effect (+0x9c).
- `SetStep(n)` (0x59626): for sprites registered in the previous step, detach the tutorial listener; `ClearExclusiveSprites()`; take the step's name
  sets; set the step panel visible per its `skip` flag; for every name in the step's lookup set: `FindSpriteByName`; add the sprite to the
  step's listener set (the spider adds the tutorial listener to the sprite when its trigger kind is not 0: a named sprite with no trigger, an action object, gets it with kind -1; a `kNoTrigger` one does not) and, if the name is in the "action" set, `VSpriteManager::AddExclusiveSprite(sprite)` (only these
  can be hit while locked, because `VSpriteManager::IsValidSprite` rejects any sprite not in the exclusive set when the exclusive set is
  non-empty OR lockdown is on - **F**, VSpriteManager.c:289). If the step has `dim` false: cancel effects; else create (once) a global
  `VTintEffect(0.5, 300 ms, colour (0,0,0,0), 0.0)` -> a **0.5-alpha black dim fading in over 300 ms**, installed with
  `AddGlobalEffect` (the global effect is on every top-level sprite, and through its propagation on their children). Finally runs a `ForEachSprite(spider, false)` pass over the **top-level** sprites of the level only: a sprite that is exempt in some step so far (by its name) has the dim taken off or its own tint made; the others keep the global dim. Highlights, action objects and trigger objects that are children of a top-level sprite (`btn_mix_dark`, `machine_sauce`, `press_btn_1`, `machine_mixer`) are clickable but their stations stay dimmed unless the station is named too.
- `Process(sprite)` (spider callback per sprite): sprites whose name is in a step's exempt set are un-tinted if the set belongs to the
  current step (remove the dim effect, creating a 300 ms fade to alpha 0 when needed) or kept tinted if only in the previous step's set;
  sprites in no set are not touched (they keep the global dim). The rules (**F**): `e` is the tint the sprite had in the previous step's map. Exempt now: the global dim is removed from the sprite; with no `e` and not exempt a step ago a copy of the dim, starting at the dim's current intensity and stopped at once, fades it out; with `e` it is stopped (fades out); at step 1 and when exempt a step ago nothing more. Exempt a step ago but not now: the global dim is removed and an own tint (0.5 in 300 ms, from `e`'s intensity or 0) takes its place and stays; `e` is destroyed. The maps of step n-1 are dropped at the end of `SetStep(n)`. Visible result: the scene dims to 50 % black over 0.3 s except the exempt top-level sprites.
- `Activate()`: clear exceptions, new `VTutorialListener` (weak ref to the tutorial) attached to nothing yet; `Reset(cancelNow)`: cancel effects
  (immediately or fading), drop the listener, `ClearExclusiveSprites`, `SetLockDown(false)`... precisely `Reset` calls `VSpriteManager::vfunc(+0x30)(false)`
  (un-soft-pause, `SoftPause(false)`) and `ClearExclusiveSprites`, `ClearExceptions`.

### 1.6 `VTutorialWindow` and input lockdown summary

`VTutorialWindow` (0x114 bytes) is a `VSpriteWindow` pushed over the current top modal. `SetTutorial` hooks the tutorial sprite and runs a **500 ms**
alpha-in. `UpdateObjects` every frame: if the tutorial is done, run a 500 ms alpha-out then `Close()` and `PopTutorial`; if it is already finished
(`+0x24d`) close at once. `OnMouseDown`: the window's own sprite manager first (the panel, the skip and Continue buttons take the click); then if the current step has `clickAny` -> `AdvanceStep` and swallow; else `HandleMouse` (above). `OnMouseMove` and `OnMouseUp` are the sprite manager's, then `HandleMouse`. The window is 800 x 600 over the whole modal and holds the tutorial sprite in its **own** sprite manager (the dim and the lock act on the level's); a click it does not take falls through to the windows behind. `VSpriteManager::OnMouseMove` answers whether a hover target exists *now*, which is the one of the last hit test: the first move after a click on a panel is taken by this window (a script that moves once and clicks at once after clicking a panel has its second click missed; a real mouse sends many moves).
`OnMessage` forwards to the manager.

Effective rules for a reimplementation (**F** unless marked):
1. While a tutorial is active the level `VSpriteManager` has lockdown ON. With lockdown on, hit testing only returns sprites in the step's exclusive set
   (action objects + trigger objects without `action=false`). Everything else (including HUD buttons that are VSprites) is dead.
2. Window-level (non-sprite) widgets are filtered by `VTutorialWindow::OnMouseDown`/`HandleMouse` + `VTutorial::HandleMouse` (only used when the
   tutorial sets `window`, i.e. the upgrade tutorial: only the toggle buttons and `btn_buy_upgrade` get through; `upgrade_list` is `kNoTrigger` and `prelevel_background` is `kIgnore`).
3. `level01.xml` has `<lockdown min="0" max="10"/>` (the only level with it, **F**). `ShopWindow::OnTaskAnimate` (3559-3580): every frame, if
   there is no tutorial in effect and tutorials are enabled and the level timer exists, `VSpriteManager::SetLockDown( CSLevel::InLockDown(timer.PercentElapsed()) )`;
   `InLockDown(p) = (min <= p <= max)` on the raw float attributes. **`PercentElapsed` is 0..100** (`min(100, elapsed/duration*100)`, **F**: `RTimer::PercentElapsed` 0x17c0f4), so level 1 (30 s) is locked only for the first **10 % = 3 s** of counted level time (plus the pre-level phase, where `LevelSetup` sets it on); after that the flag is set false every tick. The lockdown there is the same mechanism as a tutorial step's: with an empty exclusive set no sprite of the level receives hover, down, up or click (`shop` 2.3). Between tutorial steps the tutorial owns the flag and nothing is clickable except what the current step exempts. When tutorials are
   disabled the lockdown is not applied (flag forced false).
4. `VCharacter`, `VDisplay`, `VDisposal`, `VShopHud` consult `TutorialInEffect()` to suppress their own interaction/auto behaviour (details are in those analysts' areas).

### 1.7 Shipped tutorial inventory (tutorials.lua, **F**)

| tutorial | trigger | levels | repeatable / tryonce | notes |
|---|---|---|---|---|
| `tutorial_upgrades` | `trigger_type="upgrade_dialog"` (but actually started by name from `VPrelevelWindow`), `window="prelevel_container"`, `dim=false` | 4..4 | tryonce, **not** repeatable | 7 steps: click `toggle_upgrades_aesthetic`, buy (`btn_buy_upgrade`), click `toggle_upgrades_gameplay`, buy, `toggle_upgrades_player`, buy, then `ok_button`. Steps carry arrows (`default_arrow` / `indicator_arrow`) |
| `tutorial_1` | `customer_arrival`, object `tut_cust_1` | 1..1 | repeatable | 5 steps, highlight `avatar`; triggers: `station01` arrival, `tut_cust_1` resume (action=false), `station08` arrival (sauce machine `machine_sauce`), `station01` x2 |
| `tutorial_2` | `customer_arrival` `tut_cust_2` | 1..1 | repeatable | 10 steps incl. `btn_mix_dark` click, `station14` mixer, `press_btn_1`, `station12` press, `station02` |
| `tutorial_3` | `customer_arrival` `tut_cust_3` | 1..1 | repeatable | 14 steps (second order pair `tut_cust_3`/`tut_cust_4`) |
| `tutorial_4` | `customer_arrival` `tut_cust_1` | 2..2 | repeatable | mixer/press/`station16`/`station15`; its 8th step is the Display Case panel (`tutorial_2_3`, box 200x280, text 14 lines at Edwardian 15) shown when the bar has been put on the case; the 9th is the Disposal panel (`tutorial_2_4`) |
| `tutorial_5` | `customer_arrival` `tut_cust_1` | 3..3 | repeatable | ingredient machine `station13`, `btn_ingred_1` |
| `tutorial_6` | `customer_arrival` `tut_cust_1` | **61..61** | repeatable | milk mix `btn_mix_milk`; level 61 never plays (levels end at 60) so this tutorial can never show (**F** data, **I** probably intended for another level) |
| `tutorial_7` | (inside a `--[[ ]]` comment) | 5 | - | dead |
| `tutorial_8` | `customer_arrival` `tut_cust_1` | 13..13 | repeatable | dark-chocolate/`btn_mix_dark` |
| `tutorial_9` | `customer_arrival` `tut_cust_1` | 11..11 | repeatable | similar |
| `tutorial_endless_1` | `trigger_type="endless_start"` (object "") | 1..100 | not repeatable (repeatable commented out), `modes={1,2,3}`, `pause=true` | two steps, each with a `button_advance` ("Continue") button (`advance_button_style`) and an arrow: the welcome text (box 300x230 at 380,150, button at 45,185, arrow at the HUD's Upgrades button) and the Customer Dissatisfaction Meter text (300x250, button at 45,205, arrow at the anger gauge) |
| `tutorial_endless_2` | none; started by name from `ShopWindow` on difficulty step-up | 1..100 | same, `modes={1,2,3}` | one text-only step (label `tutorial_endless_3`, box 300x230 at 50,50, button at 45,185) with Continue |
| `tutorial_zepplin` | commented out | 14 | - | dead |

Customers named `tut_cust_1..4` in `level01.xml` (and `tut_cust_1` in other tutorial levels) are the trigger objects: they are ordinary level customers
whose XML `<name>` matches the tutorial's `trigger_object`; `VCustomer` calls `CheckTutorialsByTrigger("customer_arrival", name)` at its arrival.
Because `tutorial_1..3` all share level 1 and have different `trigger_object`s, each is started by its own customer's arrival; a tutorial is only
started when no other tutorial is active (`CheckTutorial` guard).

Shared UI styles in the same file: `default_tutorial_assets` (`ui/tutorials/tutorial_back`), `default_arrow` (`tutorial_arrow`, continuous), `indicator_arrow`
(tint FColor(1.0,0.3,0.6), scale 0.7), `tutorial_skip_button` (`ui/buttons/hud_menu`, x=20 y=575, scale 0.7, label `app_skiptutorials`),
`advance_button_style` (label `btn_continue`), `tutorial_text_style` (edwardian_medium 15, margins {40,50,40,35}).

**Unknown** (1.x): which tutorial state values >1 other than 3 exist; exact `VTutorialSpider::Process` tint bookkeeping; whether `VTutorial::Update`
auto-advances steps whose only gate is `duration` (steps with `duration>=0` and `clickAny==false` and no triggers have no way to advance other
than `Update` timing; none ship); `trigger_type="upgrade_dialog"` is unused by code (the check is by name).

## 2. Story / plot: `VPlotManager`, `VPlotItem`, `VPlotStep`, `VPlotResponse`, `VPlotWindow`, `VPlotListener`

### 2.1 Data

`locations/<loc>/plot.lua` defines a global table (`iquitosPlot`, `grytvikenPlot`, ..., wired in game.lua `location_<loc>.plot`). Per-level, `ShopWindow`
passes the location's `plot` table to `VPlotManager::LoadFromTable` (every level load clears the item map first, `ClearPlotItems`, **F**).
`VPlotManager::LoadFromTable` (0x2fbc2): iterate every key; only table-valued keys whose first 6 chars (lower-cased) are `story_` are used
(`baseDir`, `default_button` etc. are style inheritance). Item kind = `type` string (lower-cased): `"quest"` -> `VQuestItem::Create(name, idx)`,
anything else -> `VPlotItem::Create(name, idx)`. `idx` is a running counter in iteration order = the item's persistence slot (**F**, so the slot
depends on Lua table iteration order: **I** stable per build).

Plot item (`VPlotItem::LoadFromTable`): `type` string (stored), `nextStep` table -> `VPlotStep::Create(item)` + `LoadFromTable` (the first step);
the rest via `VObject::LoadFromTable` (so `assets`, `children`, `text` of the item table itself act as the **backdrop**; shipped plots set `assets = CSStoryBackgroundAssets`).
Plot step (`VPlotStep::LoadFromTable` 0x30fb0): `speaker` string, `responses` table (each entry -> `VPlotResponse`: `text`, `label`, `speaker`,
`value` (int, default 0), its own `nextStep`; each response gets a `VButton`), `default_button` (a button style created when there are no responses;
the shipped plot tables define `default_button = { style = CSStoryButtonCorner, label = "app_ok" }` once at the top and every step inherits it),
`nextStep` (the next `VPlotStep`, recursively), and the usual `VObject` keys (`text` = diary text boxes, `children` = portraits with `autoState="evangeline"` etc.).
Shipped GREATCC plots use **no responses and exactly one step per item** (`nextStep` once, `default_button` OK): every plot item is a single diary page
with an OK button. (**F**; one exception: `story_iquitos_8` has `type="quest"`, see section 3.)

### 2.2 Persistence

`VPlotItem::LoadCurrentUser(location)`: RState binary blocks **`plot_<location>`** (byte `[idx]`: `1` = complete) and **`plot_value_<location>`** (int32 `[idx]`:
accumulated response value); blocks exist for the six locations (strings `plot_grytviken`, `plot_namche`, `plot_krakatoa`, `plot_kamchatka`, `plot_iquitos`,
`plot_gaberoun` and `plot_value_*`). `UpdateCurrentUser` writes both. Location name comes from the current level object (+0x1c). (**F**)

No page is ever stored as done: nothing in the plot, quest or shop code calls `VPlotItem::UpdateCurrentUser` (its only caller is `VQuestItem::UpdateCurrentUser`, itself uncalled), so the `plot_<location>` bytes are never written with 1. `ShopWindow::LoadPlotItems` (from level setup) calls `ClearPlotItems`, rebuilds the items from the level's `plot` table and calls `VPlotManager::LoadCurrentUser` each time, so the completed flag (+0x245, set in memory by `ChooseResponse`/`Advance`) does not survive into the next entry of the level. `VLevel::NeedPlot` is only "the plot list is non-empty" and `LaunchPlotDialog` tests only that the active list is non-empty. A replayed level therefore shows its diary again. (**F**)

### 2.3 Run behaviour

- `VPlotManager::SetCurrentLevel(level)`: stores it, clears the active list, and for every plot name in the level's `<plot>` list calls `ActivatePlotItem(name)`
  = push to active list (if absent) and call the item's vfunc +0x28 (`Init`: `VObject::Init`, init first step, clear value). So a level re-activates its
  item **every time the level is entered**.
- `LaunchPlotDialog` (0x2f730): if the active list is non-empty: new modal window; `PushModal`; `RunScript("scripts/plotwindow.lua")`; find child
  `plot_window` as `VPlotWindow` (if missing, pop modal `plot_modal`); `VPlotWindow::StartPlotSequence(manager)`.
  `scripts/plotwindow.lua` builds `MakeDialog{ name="plot_modal", RBounceWindow container (x = kCenter-800, targetpos 0,0, bouncelen 400, bouncefunc "sincx"), VPlotWindow{name="plot_window", fills it} }` (**F**).
  So the diary slides in from the left over 400 ms with a sinusoidal bounce.
- `VPlotWindow::StartPlotSequence`: GameState `DiaryTime_<PLAY_COUNTER> = 1`; create a `VPlotListener`; copy the active list; `StartNextItem()`; if none, `Close()`.
- `StartNextItem` (0x3195a): reset the previous item; pop the front name; `GetPlotItem`; listener `SetPlotItem(name)`; item vfunc +0x12c (attach, **I**);
  add the item as a sprite of the window; call item vfunc +0x130 = `AdvanceStep` (starts the first step); remember the item; return true. Returns false when the list is empty.
- `VPlotWindow::OnTaskAnimate` every frame: when the current item reports complete (vfunc +0x138 = `IsComplete`) start the next one; when none left `Close()`.
- Click handling: `VPlotListener::OnClick(sprite)` -> if the clicked sprite is a `VButton`: `VPlotManager::ChooseResponse(itemName, button.name)`
  (button's `+0x214` string = response id), else `VPlotManager::Advance(itemName)`.
  `VPlotItem::ChooseResponse(id)` (if not complete and a step is current): `value += step.ChooseResponse(id, &nextStep)`; `if step.Advance()` then `AdvanceStep()` and update complete flag.
  `VPlotItem::AdvanceStep` rotates previous/current/next step pointers, attaches the listener to the new step, and if the previous step is complete (or none) marks things.
  Plot step visuals: `VPlotStep::Begin` = **500 ms** alpha-in (`VAlphaEffect(true,1.0,500,-1.0,0)`); `IsComplete` = advanced flag set and the effect (if any) finished.
- `VPlotWindow::Close` (0x31776, idempotent): runs Lua `coroutine.resume(coroutine.create(function() DoTransition(1007); PopModal('plot_modal'); InsideGame(true); end))`
  (1007 = 0x3ef, the transition constant `kBounceBack`, **I**) and sets GameState `DiaryTime_<n> = 0`.
- `VPlotManager::GetPostLevelMessage()`: concatenation of each active item's `GetPostLevelMessage()`; for a plain `VPlotItem` this is `""` (0x2e5ce). No external caller found (**U** whether anything uses it).

Natives exposed to Lua by these classes: none registered by name (the plot window is driven by `VPlotWindow` placed in the Lua dialog). Lua depends on global names `plot_modal`, `plot_window`.

**Unknown** (2.x): how `VPlotItem::Update` (0x2ebfa) drives step fades (not read); `VPlotStep::End` effect parameters.

## 3. Quests: `VQuestManager`, `VQuest`, `VQuestItem`, `VQuestWindow`

### 3.1 Data (`locations/quests.lua` global `quests`)

Loaded by `VQuestManager::LoadFromLuaFile("locations/quests.lua")` (0x87742): a fresh `TScript` runs the file; global table `quests`; every table entry (`quest4` ... `quest59`)
becomes `VQuest::Create(slot, lowercase(name))` where `slot` = running counter (iteration order, again the persistence slot); `LoadFromStyle`; then loads the user's state.
`VQuest::LoadFromStyle` keys (0x856ca, **F**; defaults empty/0):

| key | meaning |
|---|---|
| `name` | string-table key of the quest title (`quest_title_N`) |
| `units` | string-table key shown after the progress number (`quest_served`, `quest_needed`, `quest_delivered`, `quest_chocolates`) |
| `minlevel`, `maxlevel` | inclusive level range where the quest is active (shipped: min == max == the level) |
| `threshold` | float right operand |
| `compare` | comparison string -> bit flags (below) |
| `compvalue` | name of a `VValueManager` real value read as the left operand |
| `reward` | upgrade name (or a money amount as a decimal string) |
| `rewardcategory` | upgrade category (lower-cased), e.g. `station` |
| `rewardtype` | lower-cased; default `"activate"`; `"reveal"`; `"money"` |
| `display` | int, default 0: `kProgressDisplay=0` (`"%i/%i <units>"` value/threshold), `kGoalDisplay=1`, `kBooleanDisplay=2` ("In Progress" / "Failed" / "Complete" strings) |
| `format` | `"currency"` changes the number format for goal display |

(A legacy XML loader `VQuest::LoadFromNode` accepts tags for the same fields; the shipped build uses the Lua path.)

Comparison parsing, `CSUtil::ParseComparisonString` (0xf618, **F**): lower-case; split on `+`, strip spaces; tokens `not`=1, `lessthan`=2, `greaterthan`=4, `equal`=8 OR'ed; then
literal characters `>` (4), `<` (2), `=` (8), `!` (1) anywhere in the string also set bits. `CSUtil::CompareValue(a, b, flags)` (0xf9ba): `less` and `greater` together without `equal` -> false; `result = (a<b if bit2) or (a>b if bit4)`; if bit8 `result |= a==b`; if bit1 `result = !result`.
So `"<"` is `a < threshold`; `">="` and `"greaterthan + equal"` are `a >= threshold`. (The `less`+`greater` both-set case returns false unless `equal` is set; irrelevant for shipped data.)

Shipped quests (all `minlevel==maxlevel`, **F**): level 4 `kMixerQ1` activate, `disposed_this_level < 1` (boolean); 5 `kMixerQ2` `service_multiplier_this_level >= 3`;
8 `kIngredientQ2` `served_this_level >= 16` (progress); 12 `kInfusionQ1` `payment_multiplier_this_level >= 5`; 15 `kToppingQ1` `current_level_total >= 750` (goal, currency);
18 `kMixerQ3` `angry_this_level < 1`; 23 `kIngredientQ1` `mastery_this_level >= 1`; 25 `kInfusionQ2` `menu_multiplier_this_level >= 5`; 28 `kMixerQ1` `zeppelin_served_this_level >= 3` (progress);
34 `kIngredientQ1` `chocolates_made_this_level >= 15`; 36 `kToppingQ2` `current_level_total >= 1000`; 38 `kInfusionQ2` `conveyor_max_this_level >= 3`; 44 `kToppingQ2` `service_multiplier_this_level >= 5`;
46 `kIngredientQ3` `zeppelin_served_this_level >= 4`; 48 `kInfusionQ1` `payment_multiplier_this_level >= 5`; levels 51-59 are **money** quests (`rewardtype="money"`, reward `"1000"`):
51 `decayed_this_level < 1`, 52 `angry_this_level < 1`, 53 `served >= 25`, 54 `current_level_total >= 1550`, 55 `decayed < 1`, 56 `zeppelin_served >= 6`, 57 `served >= 30`, 58 `angry < 1`, 59 `mastery_this_level >= 1`.
The value names are `VValueManager` keys written by the level code (other analysts).
Plus **one plot-embedded quest**: `story_iquitos_8` in `locations/iquitos/plot.lua` has `type="quest"`, `reward="kIngredientQ1"`, `rewardCategory="station"`, `compare="greaterthan + equal"`,
`compValue="served_this_level"`, `threshold=16` (read by `VQuestItem::LoadFromTable`, keys `threshold`, `compare`, `compValue`, `rewardCategory`, `reward`, `rewardType`, mixed-case). Level 8 therefore has both
`quest8` (reward kIngredientQ2, threshold 16) and this plot quest (reward kIngredientQ1). The plot-item quest is never evaluated: `VQuestItem::PostLevelProcess` is the only code that would check it, and `ShopWindow` calls only `VQuestManager::PostLevelProcess` (which walks the `quests` table quests); `VPlotManager` has no post-level pass (its `GetPostLevelMessage` is only the Lua global `GetPlotMessage`, which no shipped script calls). So `story_iquitos_8` behaves as a plain diary page and its `kIngredientQ1` reward is never granted; only `quest8`'s `kIngredientQ2` is. (**F** for the callers in the shop, plot and quest classes; no other caller of the virtual was found.)

### 3.2 State and persistence

Status int `VQuest +0xc`: 0 = in progress, 1 = complete, 2 = failed. `QuestComplete()` = status == 1. `IsReady(level)` = `minlevel <= level <= maxlevel` AND NOT complete
(so a failed quest is retried every time the level is replayed). `VQuestManager::GetBlock` creates RState-style binary block **`quest_block`** (100 bytes); `VQuest::LoadCurrentUser`
reads byte `[slot]` into status; `UpdateCurrentUser` writes it. `VQuestItem` persists in block **`quest_<location>`** (analogous to plot).

### 3.3 Run behaviour

- `VQuestManager::SetCurrentLevel(n)`: `currentLevel = n`; clear post-level text; build the active set = every quest with `IsReady(n)`.
- `GetActiveQuests()` returns the set copy (used by `VQuestWindow`).
- `PostLevelProcess()` (0x87a1e), called once from `ShopWindow::DoEndLevel` before the level counters reset: `CheckActiveQuests()` (calls `VQuest::CheckQuest` on each active quest),
  then builds the post-level text: for each active quest append (`"\n"`-separated) `quest->GetPostLevelMessage()` (vfunc +0x28) if non-empty, saves the block (vfunc +0x18 of the block), returns the text.
  `GetPostLevelText()` returns the cached string; `ShopWindow` (line 1486) shows it on the end-of-level screen when the level was passed/not.
- `VQuest::CheckQuest` (0x86f18): if not already complete: read the real value `compvalue`; `status = CompareValue(value, threshold, flags) ? 1 : 2`; on status 1 and non-empty `reward` apply `rewardtype`:
  `"reveal"` -> `VUpgradeManager::RevealUpgrade(reward, category)`; `"activate"` (default) -> `VUpgradeManager::EnableUpgrade(reward, category, true, ...)`; `"money"` -> add `to_float(reward)` (if > 0) to value `current_worth`
  (`AddToValue`); other types do nothing. A failed quest (status 2) can be re-evaluated next visit.
- `GetStatusInfo()` text: status 0 and `display==0` -> `"%i/%i <units>"` with value read by `GetInt(compvalue)` and `(int)threshold`; `display==1` -> `"%i <units>"`-style with the threshold (currency flavour when `format=="currency"`, with `$`); `display==2` -> string-table "in progress"; status 1/2 -> string-table "complete"/"failed" strings.
- `VQuest::GetPostLevelMessage()` (0x85cc6, 2312 bytes) assembles string-table entries `quest_complete`/`quest_failed` (+ `_info` variants) with `header`/`content` attributes; for money rewards the amount is printed as `$<reward>`. Text keys exist in `strings/en/strings.xml`. The reward upgrade's own `header`/`content` attribute strings are looked up through `VUpgradeManager::GetUpgrade(reward, category)->GetAttribute(...)` in the `VQuestItem` variant.
- `VQuestWindow` (HUD panel in `game.lua`, children `txt_quest_names`, `txt_quest_status`, `quest_header_1/2`): `PostChildrenInit` reads window style `colors` (a list of colours indexed by status; default white), `size`/`indent`-type ints and colour; `UpdateText()` (0x89acc, called from `LevelInit` and `OnTaskAnimate`) rebuilds the left text as one line per active quest `<font color='RRGGBB'>name</font>\n` with the colour chosen by `status` index, and the right text with each quest's `GetStatusInfo()`; the panel is shown only when there is at least one active quest (returns whether the set is non-empty).

`VQuestWindow::UpdateText` reads each active quest's live status (`VQuest` vfunc +0x18) every time it runs and picks the colour at that index of `colors` (white when the index is past the list), for both the name line and the status line, so the colours follow the status as it changes. (**F**)

**Unknown** (3.x): per-status colours (in game.lua style: see `questW/questH` block, colours not transcribed here); exact `GetStatusInfo` string keys.

## 4. World map: `VWorldMap`, `VPushPin`, `VPinListener/VPinSpider`, dots

(`BZeppelin` is **not** a map object: it is the in-level delivery zeppelin, see 4.6.)

### 4.1 Lua

`scripts/worldmap.lua` builds the dialog `worldmap`: background `ui/backgrounds/worldmap`; buttons `btn_play`, `btn_rewards`, `btn_rewards_endless`, `btn_options`, "main menu" (calls `QuitToMainMenu`), `game_cheats`;
and `VWorldMap{ name="world_map_window", pins = map_pins, paths = map_paths, dot = {assets = dot{green->dot_green, blue->dot_blue}} }`. It posts `kFadeFromBlack` with command `FadeComplete();` at the end.
`pin.lua` defines `map_pins` and `paths.lua` defines `map_paths` (**F**).
Play button: `SetCurrentLevel( firstpeek=="1" and highest>=20 -> 20; highest==60 -> 60; else highest+1 )` then `CloseWorldMap()`.
`btn_play` and `btn_rewards` are enabled only in story mode (`GetCurrentGameMode()==0`); `btn_rewards_endless` only in endless modes (header gem `gem_endless`, header label `map_header_endless`).
`FadeComplete()`: in story mode `CheckIntro()` (first ever: managed value `game_intro` 0 -> 1, then `DoModal("scripts/intro.lua")`); then if managed `do_map_transition == 1`: reset it to 0 and `PlayTransition(loc)` for
`level == 11 -> "grytviken"`, `21 -> "namche"`, `31 -> "krakatoa"`, `41 -> "kamchatka"`, `51 -> "gaberoun"`, else `ShowCurrentLocation()`; if no transition flagged just `ShowCurrentLocation()`.

### 4.2 Pins: `map_pins` (positions in the 800x600 map space)

| key | x,y | movex,movey | baseLevel |
|---|---|---|---|
| iquitos | 144,361 | -10,35 | 1 |
| grytviken | 213,500 | 40,0 | 11 |
| namche | 513,278 | 40,0 | 21 |
| krakatoa | 541,370 | -10,35 | 31 |
| kamchatka | 666,204 | -45,0 | 41 |
| gaberoun | 334,298 | 0,-50 | 51 |

All pins: `spriteLayer=1`, art `anim_pin`/`pin_enabled`/`pin_disabled` in `ui/map/`, a score `box` (`CSMapScoreBoxStyle` 130x110: level, best, goal, mastery rows), name/location texts (`map_name_<loc>`, `map_loc_<loc>`), and ten `globes`
`globe_01..globe_10` (`id` 0..9 at fixed offsets around the pin, e.g. globe_01 at (72,22), ... globe_10 at (41,30)) with art states `none` (grey), `current` (pink), `cleared` (green), `mastered` (gold), each with an `over` variant, click sound `audio/sfx/buttonclick.ogg`.

`VPushPin::LoadFromTable` (0x344f2, **F**): `baseLevel` int (0), `movex/movey` floats -> a vector (+0x258); iterates `globes` (a group level of its own, like `children`: its plain entries, e.g. `font`, reach the globes); for each table entry whose lower-cased key starts with `globe_` it creates a `VButton` (`VButton::Create(0, "")`), loads it from the entry (so its `id`, `style=` assets and `sound` come from there; the group's `font` reaches its label), adds `baseLevel` to its id (the level number), renames it **`level_btn_%0.2i`** of that number (so level 7 -> `level_btn_07`; the sprite name is the message name), subtracts the constant vector (67,125) from its position (the globe offsets are measured from the corner of the ring art, the pin's origin is its centre), sets its label text `%0.1i` of the level number, stores it in `levelButtons[level]` and adds it as a child. `box` (when the table has one, own table only) creates a `VTiledObject` score box and adds it as a child. Then `VObject::LoadFromTable` (assets, the `text` entries `name` and `loc`) and finally the loaded position of every **text sprite** goes into a name->vec2 map (the pin's closed layout; `SetOpen` closing returns to it).
`VPushPin::SetOpen(open)` (0x34c1c, **F**): does nothing unless the state changes. For every **text sprite** of the pin (`name` and `loc`; the walk is the VObject text map at +0x1dc in key order; the pin art, the score box and the globes are not touched): `CancelEffects` first, then one `VSpriteMove(target, durationMs, accel, delayMs)` (`sprites` 3):
  - **closing**: target = the sprite's stored position (the map filled at the end of `LoadFromTable`), **200 ms, accel 0.0 (linear), delay 0**;
  - **opening**: target = the sprite's *current* position + the pin's `(movex, movey)` vector, **200 ms, accel 0.0, delay 450 ms** (I: the delay lets the pin's own `DoAnim` run first). So the labels step aside as the ring opens.
  Closing also creates `VHoverGlow(0.15, false, true, true)` on the pin (stored at +0x250; the pin pulses while closed, i.e. a "click me" cue) and opening stops and clears it. The state byte (+0x22c) is then set (also when it did not change), and when opening a `VPinSpider` visits every top-level sprite of the manager (not recursively) and calls `SetOpen(false)` on every other `VPushPin` (so one pin is open at a time).
  `VPushPin::Update` (0x35ef2) is a 5-state machine on +0x278 (it contains **no hover or score-box logic**): 0 closed: if `open` -> state 1 and play the pin animation function `DoAnim`. 1 wait for the animation to finish: if `open` was cleared meanwhile -> `SetGlobeVisible(false)`, play `ReverseAnim`, state 4; else `SetGlobeEnabled(false)` (clears each globe's `mouse` byte), state 2, reveal index := `baseLevel`, timer 0. 2 globe cascade: timer += elapsed; each time it exceeds **24 ms** one globe appears: the button for level `index` is made visible with a **300 ms** `VAlphaEffect` fade-in and the button for level `index-2`, if it exists, is not in asset state `none` and is not the selected (current) globe, gets `VGlowEffect(1.0, 800 ms, 1.0, 1.0)`; then timer -= **25**, index++ (so about one globe per 25 ms, with carry-over). When `index > baseLevel + 11`: `SetGlobeEnabled(true)`, state 3, and the selected "current" globe gets `VGlowEffect(1.0, 600, -1.0, 0.0)`. 3 open and idle: if `open` was cleared -> `SetGlobeVisible(false)`, cancel the current globe's effects, play `ReverseAnim`, state 4. 4 wait for `ReverseAnim`: when done, if still closed -> state 0, else -> state 1 and `DoAnim` again. `SetGlobeEnabled` also forces a mouse re-evaluation (`VSpriteManager::RecalcMouse`, vtable slot 0, **I**: the manager's `Reset` calls the same slot after setting the mouse position to -10000, and `AddObject` ends with it). Replaying `ReverseAnim`/`DoAnim` is `VSprite::Play(name, loop=false, playChildren=false, delay=0)`; state 1 and 4 test `!IsPlaying || IsDone` of the pin's own script, so a pin closed while `DoAnim` is still playing finishes the animation first and then goes to state 4 without a cascade. `CancelEffects` on the selected globe when closing removes **all** its effects (its hover glow too; nothing re-adds it until `Init` runs again).
  The score box is driven by hover: each globe button carries a `VPinListener`; `OnMouseEnter`/`OnMouseLeave` call `VPushPin::SetScoreInfo(show, level)` (0x36788). Show: fill the box text sprites `level` (`"%s  %i"` of the `map_level` string and the level number), `best`, `goal`, `mastery` (each `"$%i"` from `GetBestScore`/`GetGoalScore`/`GetMasteryScore`), make the box visible and start a `VAlphaEffect` fade-in of **200 ms** from alpha 0 (or from the current alpha of a fade already running, which is stopped first). Hide: `VAlphaEffect` fade-out of **500 ms**, started after a **500 ms** delay when the box is fully opaque (0 delay if it is already partly faded), starting from the current alpha.
`VPushPin::OnClick`: story mode (`TSettings.mode == 0`): `if (state == 0) SetOpen(!open)`, i.e. only while the state machine is idle and closed (or before its first update has run); a pin that is opening, open or closing ignores the click. Endless mode: `DoEvent(Click)` on the globe button of level `baseLevel` (`levelButtons[baseLevel]`, which is the level-1-of-the-location globe; a locked one ignores it).
`VPushPin::Init` (0x3517e): first `SwitchAsset("base", "enabled", "default")` on the pin, `SetGlobeVisible(false)`, the selected globe cleared, state 0 (it does **not** call `VObject::Init`; the globes are initialised by the loop below). next = `highest_complete + 1`. The pin sprite is visible iff NOT ( `next < baseLevel` OR (`config firstpeek == "1"` AND `baseLevel >= 21`) OR (`current_level == baseLevel` AND managed `do_map_transition == 1`) ).
So locations appear as soon as their first level is reachable (level 11 pin appears when level 10 is completed), the demo ("firstpeek") shows only the first two locations, and the pin that is about to be animated in stays hidden until the dot trail finishes.
For every globe level `L = baseLevel + id`: `status = VLevelManager::GetCompleteStatus(L)`. Per globe, in this order: `SetDisabled(status==0 && L != next)`, `CancelEffects`, then: `status==0 && L != next` -> asset `none` and `RemoveListener(VPinListener)` (no score box, not clickable: `SetDisabled` cleared `clickable`); `status==0 && L == next` -> asset `current`, `AddListener(VPinListener)`, `VHoverGlow(0.15, true, true, true)`, remembered as the "selected" globe (+0x230); else asset `mastered` if `status == 2` else `cleared`, `AddListener`, the same hover glow. Last, when the pin has no hover glow yet and is closed, it gets its own `VHoverGlow(0.15, false, true, true)`. A locked globe is still a hover target once `SetGlobeEnabled(true)` has turned `mouse` on for every globe; only its click is dead (`VButton::OnClick` returns when disabled).
So the selectable levels are exactly: all completed levels (replay) and `highest+1`; nothing beyond. Hover over a globe shows `SetScoreInfo`: best (`GetBestScore`), goal (`GetGoalScore`), mastery (`GetMasteryScore`) for that level into the box text sprites `level`, `best`, `goal`, `mastery` (goal/mastery numbers come from `levels/goals.xml`).

### 4.3 Level selection message and map window

`VWorldMap::OnMessage` (0x620be): for command messages (type 3) whose name has more than 10 characters and starts with `level_btn_`: parse the rest as int `n`, `VLevelManager::SetCurrentLevel(n)`, run Lua `CloseWorldMap();` (queued on the command queue, as `RUtil::DoLuaString`). Every type-3 message is consumed (returns 1), whatever its name; other types return 0. A globe's `VButton::OnClick` posts that message with the sprite's name (`level_btn_NN`) to the window that owns the sprite manager (`VSpriteManager::GetParentWindow`: the child window of the top modal named like the manager's window name). `CloseWorldMap(loadGame)` (Lua): in endless modes first `DisplayDialog{"scripts/choosedifficulty.lua"}` -> `SetCurrentDifficulty(d)` (cancel if d < 0); then fade to black and either `CloseMapWindow` (loading dialog over the already-loaded game) or `SwapToModal('scripts/game.lua')` + loading dialog (first load). `SetState("MetaMapScreenTime", 0)` metric.
`VWorldMap` object: members `pins` map (+0xfc), `paths` map name -> vector of `VDotInfo{pos, state}` (+0x11c), `dot` template `VObject` (+0x110), transition name (+0x138), dot index (+0x13c), timer (+0x140), glow effect (+0x130).
`PostChildrenInit` (0x60c30): reads `pins`, creates a `VPushPin` for each table entry (stored under the lower-cased key), reads `paths` (each is an array of `{state, x, y}` -> dots in order), builds the dot template from `dot`, sets default focus on the parent modal, and in endless mode (TSettings +0x10 != 0) moves child `btn_options` to x = 341 (0x155).
Registered Lua globals while a `VWorldMap` exists: `ResetMap`, `DoCheat`, `PlayTransition`, `ShowCurrentLocation` (removed in the destructor). `ShowCurrentLocation()` opens the pin returned by `GetCurrentLocationPin()`.
`GetCurrentLocationPin` (0x614da): first pin (in map order) with `0 <= highest_complete + 1 - baseLevel < 10`, i.e. the location containing the next level to play (iquitos at start; none after level 60, so no pin opens).
`ResetMap()` = `ShowCurrentLocation()` + re-`Init()` every pin. `DoCheat(code, arg)` is used by `scripts/cheats.lua` (DoCheatMap; Lua `DoCheatMap(type, param)` returns the text the cheat window shows): code 3 adds `arg` (`to_int`; below 1 -> "No money added") to the cash of the location of the first (by key) open pin and to `total_worth` ("Added %i to location %i and lifetime total" - the original passes only one argument for two `%i`; the engine prints the base level for the second; no open pin or base level 0 -> "No location selected"); 4 sets current level (1..60) and runs `CloseWorldMap()`; 5 sets `highest_complete` (1..60) with save and `ResetMap` (**F**).

### 4.4 Arrival animation (dot trail, no zeppelin)

`PlayTransition(loc)` (0x6141c): `dotIndex=0`, transition name = `loc` if it exists in `paths` (else nothing happens), timer = 1 ms (so the first dot spawns on the next frame), and adds one null entry to the manager's exclusive set (`AddExclusiveSprite(shared_ptr<VSprite>())`: the set is then non-empty and holds no valid sprite, so the map's sprites ignore the mouse while it plays; the window's buttons are not sprites and still work). An unknown name only resets the index, the name and the timer.
`VWorldMap::OnTaskAnimate` (0x61960) every frame: while a transition is active, subtract the frame's elapsed time from the timer; when it reaches <= 0: if `dotIndex < path.size()`: create a dot `VObject` as a copy of the template (`VObject::CopyTo`: the new object shares the template's `VAsset`s; the template itself is never initialised), `SwitchAsset("dot", state, "default")` with the dot's state (`green`/`blue`), `SetPosition(dot position)`, `AddObject` into the window's manager, add it with a **200 ms alpha-in** and a **400 ms glow** (`VGlowEffect(1.0, 400, 1.0, 0.0)`), `dotIndex++`, and reload the timer from `VValueManager` int **`map_dot_interval` (values.xml = 100 ms)**. The dots are never removed (they keep `mouse` = true like every `VObject`).
When all dots are placed: find the current location pin, make it visible, give it a **700 ms glow** and a **400 ms alpha-in**, clear the transition name and exclusive set. When that glow effect finishes (checked each frame) the pin is opened (`SetOpen(true)`).
Path lengths (dots): grytviken 13, namche 32, krakatoa 8, kamchatka 16, gaberoun 29 (iquitos has no path) (**F**, paths.lua). Dot coordinates are absolute 800x600 pixel positions; colour = path segment colour (`green`/`blue`), the paths for different destinations overlap visually.
**There is no travel easing/zeppelin on the world map in GREATCC** (the brief's "zeppelin travel animation" does not exist here): the trail appears dot by dot at a fixed 100 ms cadence.

### 4.5 Unlock rules (summary)

- Location N becomes available when its first level is next (`highest_complete + 1 >= baseLevel`), i.e. complete level 10/20/30/40/50 to unlock the next location (each location = 10 levels). Replaying any completed level is allowed; levels beyond `highest+1` are unclickable.
- `firstpeek == "1"` (demo mode; `settings.xml` ships `<firstpeek>0</firstpeek>`): pins with `baseLevel >= 21` hidden; Play caps at level 20; after level 20 the end-of-game page is shown (`GotoNextLevel`). In this mode the main menu hides Endless/Scores buttons and shows `firstpeek_*` replacements (upsell). Full game: 60 levels then `endgame.lua`.
- Story vs endless: `GetCurrentGameMode()` 0 = story, 1..3 = endless difficulties (see tutorials `modes`); endless uses the same unlock progress, with `choosedifficulty.lua` shown on selection and `btn_rewards_endless` instead of Play.
- First time on the map (managed `game_intro` == 0): `intro.lua` modal ("game_intro" text + OK).

### 4.6 `BZeppelin` (in-level delivery zeppelin; belongs with delivery stations)

A `VObject` named `zeppelin` (type `zeppelin`, draw depth 3) that the `VDelivery` makes and `ShopWindow` adds to its scene; its only child is a plain sprite `zeppelin` (+0x218) that shows one of three sheets (`anim/machines/delivery/zepp_no_box`, `zepp_with_box`, `zepp_pickup`) and is what moves. The curve is a **cubic Bezier** over the points P0 (+0x238), P1 (+0x250), P2 (+0x258), P3 (+0x240); `t = (now - start) / duration`; position = P0*(1-t)^3 + 3t(1-t)^2*P1 + 3t^2(1-t)*P2 + t^3*P3 (`UpdatePosition` 0x62254), linear in t (no easing), set on the child. Fields: flying-in flag +0x260, leaving flag +0x261, box-ready flag +0x262 (set by `ReadyForPickup`, cleared only by the constructor), start time +0x264, duration in use +0x268, leave time +0x26c and approach time +0x270 (both **4500 ms**), state +0x284 (0 idle, 5 waiting, 6 grabbing), the sound +0x210, the machine (weak) +0x27c.
* `Init`: the child shows the no-box sheet, duration = approach time, P0 = (-117, rand 0..300), P3 = (590+43, 106+39) = (633, 145), P1 = (rand 50..450, rand 0..500), P2 = (rand 550..650, rand 0..200); hidden, idle, flags 0; a running engine sound is killed.
* `Pickup()` (from `VDelivery::Activate`): nothing while flying in; else `Init`, visible, flying in, P0 = (-116, rand 0..300), start = now, `audio/sfx/zepplin_loop.ogg` looped (the handle is kept).
* `Update`: `VSprite::Update`, then. Flying in and `t >= 1`: no longer flying in; box not ready -> state 5 and `WaitForChocolate`; ready -> `GrabBox`. State 5 and `t >= 1` (the start time is not renewed, so this holds every update): `WaitForChocolate` again or `GrabBox` when ready. `WaitForChocolate`: duration 1000, P0 = P3 = (590, 106), the child is put at (590, 106), P1 = (rand 651..671, rand 84..104): **the airship rests at (590, 106)** until the box is ready (P2 keeps what it was). `GrabBox`: the child to (0, 0) (the pickup frames carry their own positions, registration (0, 0)), the pickup sheet plays its default animation (22 frames x 30 ms), state 6, `audio/sfx/pickup_zepplin.ogg` once. State 6: while the pickup animation is past frame 7 the machine is told every update (vfunc +0x124, `Reset`); when it is not playing any more `Leave`. `Leave` (once): the child shows the with-box sheet, visible, leaving, P0 = (633, 145), P1 = (rand 550..650, rand 0..200), P2 = (rand 50..450, rand 0..500), P3 = (-116, rand 0..300), start = now, duration = leave time. Leaving and `t >= 1`: not leaving, state 0, `Init` (hidden).
* The destructor kills the engine sound.
Details of the delivery side: *machines* 5.7.

## 5. Upgrade shop windows: `VPrelevelWindow`, `VUpgradeList`, `VUpgradeTab`, `VUpgradeSelect`, `VUpgradeSpider`

### 5.1 `scripts/prelevel.lua` -> `VPrelevelWindow`

Dialog `prelevel_dialog`: `Window prelevel_container` (800x600) containing a `VTintWindow` (black alpha 160, fade) background `prelevel_background`, and `VPrelevelWindow name="prelevel_window"` (x=0,y=600 bouncing to y=0, `bouncelen=400`, `bouncefunc="sincx"`), with:
`VUpgradeList name="upgrade_list"` (x62 y152 w261 h277, `tick=43`, slider + `slider_start`/`slider_end` buttons), three radio toggle buttons `toggle_upgrades_aesthetic`, `toggle_upgrades_gameplay`, `toggle_upgrades_player` (`RDisableButton`s),
bottom-right `ok_button` ("Play", closes: `InsideGame(true); Close(); SetState("UpgradeScreenTime",0); kTintOff; DoTransition(kBounceBack); PauseSounds(false, kSFXSoundGroup); CloseWindow()`), bottom-left Quit (yes/no dialog -> `QuitToMainMenu`), a cash `VRegister` (7 digits, context `prelevel`) and the blurb text.
The script ends with `InsideGame(false)`, `SetState("UpgradeScreenTime",1)`, `PauseSounds(true, kSFXSoundGroup)`: **the shop pauses SFX while open** and resumes them on Play.

`VPrelevelWindow` (**F**; an `RBounceWindow`; the constructor binds the Lua global `Close` to `VPrelevelWindow::Close`; `PostChildrenInit` finds `upgrade_list`, runs `InitUpgrades`, starts a 10 ms window animation (`StartWindowAnimation(delay ms, repeat, flag, catch-up)`: here `(10, true, true, true)`, the list's is `(20, true, true, false)`; the last argument is the catch-up flag stored by `TAnimTask::SetDelay` at +0x11; started only when there is a parent modal) and makes the window the modal's default focus):
- `Draw` (0x31de2): `RWindow::Draw`, then the picture of the shop in the frame on the right: `ShopWindow::RenderPrelevel` hides every active customer, draws the shop's sprite manager with the translation row set to (342, 93) and the matrix scaled by 0.5 (the scene is drawn at half size with its corner at (342, 93); the translation is not scaled; the position is relative to this window, because `TWindow::DoDraw` pushes the window's rectangle as the renderer viewport before it calls `Draw`, so the picture rises with the bouncing window), and shows the customers again. The shop's own window is under the dialog and draws as usual; the picture is drawn before this window's children, so the frame art (`ui/prelevel/background` with its mask) covers its edges.
- `OnTaskAnimate` (0x3201e): `VSpriteWindow::UpdateObjects` of the `ShopWindow` instance (vfunc +0x90: the shop's sprites update every frame on the shop's own clock, which stands still behind the dialog); first frame only: `CheckTutorial("tutorial_upgrades")` (`InitUpgrades` asks too, at the end); then the category button that is on (the first of Machines, Decoration, Power-ups that is on and whose disabled state differs) is disabled while the list has no tab (list +0x120 flag = "the list is empty") and enabled again when it has, via `RDisableButton::ToggleState`.
- `OnMessage` (0x32144): first forwards to the tutorial manager if a tutorial is in effect (so tutorial triggers fire). With the "ready" flag (+0x13c, set after the first frame) and a list: message type 3 names:
  `toggle_upgrades_aesthetic` -> category **`aesthetic`**; `toggle_upgrades_gameplay` -> category **`station`**; `toggle_upgrades_player` -> category **`gameplay`** (note the UI-name vs category-name mismatch, **F**); each remembers the category (+0x134) and calls `VUpgradeList::ListUpgradeType(category)`;
  `btn_buy` -> `VUpgradeList::PurchaseUpgrades()`; `slider_start` / `slider_end` -> `RScrollable::ScrollBack/ScrollForward`. All return 0 (unhandled, so other listeners also see them).
- `InitUpgrades` (0x32462): for each of the three toggle buttons, disable it when `VUpgradeManager::GetVisibleOfType(category)` returns 0; selects the first non-empty category (priority per code order: station, aesthetic, gameplay -> the remembered category) and lists it.
- `Close` (0x3294e): `VUpgradeManager::DisableTempUpgrades("", "", no spider)` (drop any hover-preview upgrades), then `ShopWindow::UpgradeComplete` when the shop exists: it sets the plot flag (+0x1dd) and runs `VChocolateVendor::UpdateCapability(true)` when the vendor exists, gives the tutorial manager the shop's sprite manager again, calls vtable +0x134 (`UpdateAvailable`) on every station and `Init` (+0x28) on the backdrop (+0x168).
- `OnKeyDown` (0x31d98): Down scrolls the list forward, Up back (`RScrollable::ScrollForward/ScrollBack`, one `tick` = 43 px of the interior); both return true when the list exists; no other key is handled. Enter and Escape do nothing here.

### 5.2 `VUpgradeList` (0x5b114 ..)

Scrollable (`RScrollable`) holding `VUpgradeTab`s in `std::list` (+0x110). `ListUpgradeType(type)` (0x5b5d8): if `type` differs from the current one: `ClearList`; remember type; `VUpgradeManager::DisableTempUpgrades`; for each upgrade in `GetTypeMap(type)` that is **not active (not purchased) and not hidden**: create a `TImage` and run `scripts/upgrade.lua` into it (defines `VUpgradeTab name="upgrade_tab"`), find `upgrade_tab`, `SetUpgrade(upgrade)`, `SetSelectionSpider(...)`, push to the list; then set the slider to 1.0 then 0.0 (reset scroll) and `SortTabs()`. Returns whether the list is non-empty.
`UpgradeSort(a, b)`: by tab state ascending (`0 available, 1 over, 2 selected, 3 locked, 4 purchased`), ties by cost ascending (a stable list sort). `SortTabs`: sort, drop purchased tabs, stack them vertically (y accumulates each tab's height), refit the interior and restore slider position. A tab is made by running `scripts/upgrade.lua` into a window of its own (`TWindowManager::RunScript`), taking its `upgrade_tab` out of that window and into the interior; every non-hidden upgrade of the category that is not active is listed, whatever its range, so an upgrade that is not yet unlocked shows as a locked tab ("Unavailable"). The list's `PostChildrenInit` starts a 20 ms animation.
`OnTaskAnimate` (0x5b242): every frame: purchased tabs (state 4) are removed from the list; others `UpdateState()`; stacking positions slide upwards by at most **10 px per frame** (if a tab's y is more than 10 above... precisely: if the tab is lower than the running y by <= 10 it snaps, else moves up 10) producing the "list closes the gap" animation; `PurchaseUpgrades` buys every tab in order (used by `btn_buy`).

### 5.3 `VUpgradeTab` + `scripts/upgrade.lua`

`upgrade.lua` defines the tab layout: `VUpgradeTab` 235x90 with five layers (`SelectLayer(0..4)`: available, over, selected, locked, purchased) of `ui/prelevel/upgrade_back_*`, thumbnail `img_thumbnail`, `upgrade_contents` (`txt_header`, `txt_content`), price button `btn_buy_upgrade` (`RDisableButton` 72x28 with `txt_price`/`txt_price_shadow`), "Unavailable"/"Installed" labels in layers 3/4.
`VUpgradeTab::SetUpgrade` (0x5d512): window name `upgrade_tab_<upgradeName>`; `txt_header` <- string-table(upgrade attribute `header`); `txt_content` <- string-table(attribute `content`); `img_thumbnail` <- texture from attribute `image`; `txt_price` and shadow <- `"$%i"` of cost; caches the buy button; `UpdateState`; `FitToChildren`.
Cost = upgrade +0x38 float (0 if none) (`GetCost`).
`CanPurchase()` = upgrade is `VUpgrade::IsAvailable()` (state not 0 and not 3; the manager's requirement check is not repeated) AND the real value named at +0x4c >= cost; the constructor sets that name to `current_worth` in story mode and `current_level_total` when `TSettings` mode is not 0 (**F**: `VUpgradeTab::VUpgradeTab`); `PurchaseUpgrade` subtracts from the same value.
`UpdateState()` (0x5d47a): not available -> state 3 (locked) if not active else state 4 (purchased); available -> state 1 (over) if mouse is over and affordable, else 0; buy button toggled disabled = `!CanPurchase`. `SetState` (0x5d310): when the state changes it clears the enabled bit (0x100) of the buy button for the locked and purchased looks and sets it for the others, then `SetCurrentLayer(state)`. The button is adopted under "all layers", so the layer switch shows it again; it stays hidden only where `SetCurrentLayer` is a no-op, i.e. the first switch to layer 3, because the shipped `upgrade.lua` ends its build with `SelectLayer(3)` (the tab's current layer is then 3). In play a locked offer shows "Unavailable" with no price button, and a bought one is removed at once.
`OnMouseMove` (0x5dfcc): the point is taken to the list's coordinates (the tab's parent is the interior, whose parent is the list) and a point outside the list's own rectangle makes the tab call `OnMouseLeave` and return false; otherwise, the first time in (flag +0x48): registers as mouse listener; if affordable and not already selected (+0x49): `SetState(1)` and `SetActive(true)`; then `RUtil::MouseDispatch(point, 8)` hands the move to the window under the pointer inside the tab and returns true when there is one. `OnMouseLeave`: unregister, clear the flag, if not selected and affordable: `SetActive(false)`, `SetState(0)`. `OnMouseDown` returns false. `OnMessage`: a type 3 message named `btn_buy_upgrade` calls `PurchaseUpgrade` and returns 0. `SetActive(true)` does nothing unless the upgrade is available and the preview (+0x4a) is off; `SetActive(false)` does nothing unless it is on.
`SetActive(true)` = **preview**: marks the tab active and calls `VUpgradeManager::EnableTempUpgrade(name, category, ...)` so the level scene behind shows the upgrade while hovering; `SetActive(false)` -> `DisableTempUpgrades`.
`PurchaseUpgrade()` (0x5e130), message `btn_buy_upgrade` triggers it: if `CanPurchase`: clear "selected" flag, state 4, `SetActive(false)`, `VUpgradeManager::EnableUpgrade(name, category, true)` (permanent), `VValueManager::SubtractFromValue("current_worth", cost)`; in story mode (`TSettings.mode == 0`) also `VLevelManager::UpdateCurrentScore(currentScore - (int)max(cost,0))` (the spend lowers the displayed level score); if the upgrade's `category` string equals `"gameplay"` increments value **`num_char_upgrades`** by 1; in story mode records GameState entries (`UpgradeChosenStory` for the name, and begin/end status, `add-value`) for metrics; plays SFX **`audio/sfx/purchase_upgrade_button.ogg`**. Returns true.
`VUpgradeSelect` (a `VUpgradeSpider`, `Process` 0x18a9a8) is the glow of the preview (**F**): the list makes one at its construction, holding a `VGlowEffect(1.0, 500, -1.0, 0.0)` (peak 1, 500 ms a half wave, endless), and gives it to every tab. `Process(sprite)` sets the effect's propagate flag (+0x34) to the spider's `recursive` flag (`VUpgradeSpider::SetRecursive`, +0x14, set by `VUpgrade::Activate` from the bound object) and then `AddEffect` (vtable +0xd4) when the spider's upgrade is active or `RemoveEffect` (+0xd8) when it is not. A preview (state 4) is active, so it adds the glow; ending the preview restores the state and removes it. The purchase and `Close` pass no spider, so they add and remove nothing. The effect has its own `TClock` (*sprites* 3), so the glow pulses while the shop's clock stands still behind the dialog (**I**: a default `TClock` follows platform time).

**Unknown** (5.x): scroll-wheel behaviour.

## 6. Rewards / achievements windows

Model (`VAchievementManager`, `achievements.xml`) is another analyst's. Window side:

- `achievements.xml`: categories `adventure` (47 achievements), `endless` (4: `endless_time` thresholds 300/600/1200/1800), and a commented `endgame` category; each has `title`, `description` (string keys), `threshold`, `compare` (same comparison-string grammar as quests), `value` (a saved `VValueManager` name: `num_bars`, `num_sauce_sold`, `num_infusions`, `num_truffles`, `num_deliveries`, `num_disposed`, `num_served`, `num_angry`, `completed_<loc>` (threshold 10 each), `num_levels_mastered` (10/25/50/60), `total_worth` (500/1000/5000/20000/50000/100000), `endless_time`). Several thresholds do not match their names (`sauce1000` is 500, `disposed20` is 10, ...): use the XML values.
- Lua natives registered by `VAchievementManager` (**F**): `CheckAllAchievements(category:str, flag:bool, n:uint)`, `ShowNewAchievements(n)`, `HideNotification()`.
- `scripts/rewardnotify.lua` -> `VRewardNotify name="reward_notify"` (`duration=7000`, `transition=500`, `spacing=10`, x = 800-275, y = 0, 275x150): style keys read: `background`, `item` (with `icon` child states named after achievements, e.g. `delivery10`), `header`, `description`, `icon`, `duration`, `transition`, `spacing`. `AddAchievements(list)` (0x79dae) builds one item per newly-acquired achievement (header/description from the string table, icon state = achievement name) stacked with `spacing`. `OnTaskAnimate` (0x7aa3c) phases using the modal's clock: phase 0 slides in over `transition` (500 ms; each frame the box moves by `(now - phaseStart) * boxWidth / transition`, where `phaseStart` is the clock time when the phase began (not the previous frame), so it accelerates, and x is clamped at 0), phase 1 holds for `duration` (7000 ms) or until `SetCloseCondition(n, flag)` fires, phase 2 slides out; at the end `Close()` -> `VAchievementManager::NotifyComplete(window)` and destroy. Endlevel: `ShowNewAchievements(0)` (0 = all new), `HideNotification()` when leaving.
- `scripts/rewardswindow.lua` -> `VRewardsWindow` (`rewards = sweet_rewards`, `box_top`): `PostChildrenInit` creates a `VReward` per `reward_*` entry of `scripts/rewards.lua` (`name` = achievement name, `boxX/boxY`, `category`, `spriteLayer`, `assets.choc` animation `anim_<chocolate>`); `Ready()` (called from Lua `RewardWindowReady()` after the fade from black) lifts the `box_top` lid: `VSpriteMove::Create(target (600,-600), duration 400 ms, accel 1.9, delay 1000 ms)` (the third argument is the float whose bits are 0x3ff33333 = 1.9; the argument order is the one of `VSpriteMove::Create(TVec2, uint, float, uint)`) (**F**). Window `SetState("SweetRewardsScreenTime",1)` metric; OK button fades out and closes.
- `VReward` (0x38ca0..): keys `boxX`, `boxY`, `headerStyle`, `contentStyle`, `name`; builds a child `VTiledObject` tooltip box (hidden) containing two `VTextSprite`s (header, content from the achievement's string keys); `Init`: if the achievement is not acquired, tints the sprite to colour (0.2,0.2,0.2,1.0) (the greyed "not yet earned" look); `OnMouseEnter/Leave` show/hide the tooltip at `boxX/boxY`.
- `VChooseAchievements` (cheat window `scripts/cheats_achievements.lua`): buttons `btn_acquire`, `btn_release`, `btn_notify`, per-achievement toggles `btn_achieve_<name>`, `scroller`, `sprite_window`, `checkstyle` - debug only. **Can be inert/omitted** (the main-menu "debug" button `MainDebug()` = `VAchievementManager::AcquireAll`).

## 7. Menus and misc windows (this game vs Chocolatier 3)

C3's decompile does not contain `TMainMenu`, `TChoosePlayer`, `THiscoreSubmit` (those screens are Lua-only in C3); `TOptions`, `TCredits`, `THiscore`, `TXSellWindow` exist in both (GREATCC: TOptions 18.7 KB vs 36.3 KB, TCredits 17.8 vs 59.4, THiscore 86 vs 66, TXSellWindow 39 vs 48 KB of decompile text; not diffed function by function).

- **`TMainMenu`** (**F**): `PostChildrenInit`: plays music **`audio/music/theme.ogg`** (`VSoundManager::PlayMusic(name, loop=1, 1)`); hides `mainmenu_debug_button` (flags 0); `SetWelcomeText()`; sets default focus; if file `xsell/xsell.lua` does **not** exist, clears flag 0x100 (enabled) on child `moregames` (no `xsell/` dir ships => the "more games" button is permanently disabled). `SetWelcomeText` fills `txt_welcome(_shadow)` with string `mainmenu_welcome` (parameter `","` when a user exists else `"!"`) and `txt_user(_shadow)` with `"<name>!"` when `GetNumUsers() > 0`. `OnParentModalPopped`: GameState `MainMenuScreenTime = 0`. `MainDebug` = acquire all achievements. Lua natives: `MainDebug`, `SetWelcomeText`. `scripts/mainmenu.lua`: `CheckUsers()` first (prompts `entername.lua` when there are no users), buttons: new/continue story (`SetCurrentGameMode(0)`, `SwapToModal('scripts/worldmap.lua')`), endless (`SetCurrentGameMode(1)`), options, help, choose player, scores (`SwapToModal('scripts/hiscore.lua')`), awards (`rewardswindow.lua`), quit (yes/no), upsell/xsell buttons; `firstpeek_endless_button` / `firstpeek_scores_button` replace endless/scores when `firstpeek=="1"`.
- **`TChoosePlayer`** (natives `SetPlayerChoices`, `GetSelectedUserIndex`, `GetSelectedUserName`, `SetNameField`): user list picker used by `scripts/chooseplayer.lua` (Create/Delete/Rename via `CreateNewUser`, `DeleteUser`, `ChangeCurrentUserName`, `SetCurrentUser`). Required (players are per-user saves).
- **`TOptions`** (**F**, natives `SaveVolumes`): checkboxes `options_fullscreen_checkbox` (reflects `TSettings::IsFullScreen`), `options_muteall_checkbox` (pref `soundoff` per user, default 0), `options_tutorials_checkbox` (`VTutorialManager::ShouldShowTutorials`; click toggles with save); sliders `options_sfxlevel_slider` and `options_musiclevel_slider` (value 0..1) with child images `sound_fill` / `music_fill` resized to `width*value`. Prefs (per current user `curuser`, int in thousandths): **`soundlevel`** and **`musiclevel`**, default **750 (0x2ee)**, saved by `SaveVolumes` as `(int)(value*1000)`. Live effect: sfx slider -> `TSoundManager::SetTypeVolume(1000, v)` and `SetTypeVolume(0, v)`; music slider -> `SetTypeVolume(0x3e9 = 1001, v)`. (Sound type ids: 1000 = `kSFXSoundGroup`-style SFX type, 1001 = music; see the audio doc.) `ToggleSoundMute`, `ToggleFullScreen`, `PauseSounds` are shared SDK natives.
- **`TCredits`**: scrolling credits from `credits/credits.txt` (Mac only; absent from the Windows `.pfp` list, **U** where Windows reads it); style keys `time`, `intropause`, `columngap`, `columnwidth`, `font`, `fontsize`, `fontcolor`, `headercolor`; `credits_version` text = string `app_version` with the config value `version` (kGameVersion, empty in the shipped settings) as its argument, set in `PostChildrenInit`; the same function ends with `SetWindowDepth(0)`, which sends the scroller behind its siblings, so the frame art and the Close button draw over the scrolling text (F). Shown from Options and (context `endgame`) after `endgame.lua`. Cosmetic; can be a simple scroller.
- **`THiscore` / `THiscoreSubmit`** (**F**, `THiscore.c`; the Windows and Mac decompiles agree). The screen is `scripts/hiscore.lua`: a `HiscoreWindow` with Texts `header`, `gamemode`, Buttons `gamemodeleft` / `gamemoderight`, `leftpanel` (Texts `local`, `global`, `topplayers`, Buttons `categoryleft` / `categoryright`, ten rows each `N` (rank), `p1_N` (icon), `nameN` (an `RShrinkText`), `scoreN`), `rightpanel` / `rightpanelsmall` (online info, `moreinfo`, `submit`), `server`, `error`, `scrollup` / `scrolldown`, `back`, `view`, `viewlocal`. Config `hiscorelocal` (`kHiscoreLocalOnly`, shipped 0) and `hiscoreanon` (shipped 1) are read with `IsEnabled`. The constructor binds `GetState`, `ScoreAvailable` (the current user has a row in the current mode's table), `UpdateText` and `LaunchPrivacyPolicy` (opens `http://www.playfirst.com/privacypolicy.html`); `SubmitToServer`, `InitUserScore` belong to `THiscoreSubmit`. State (+0x30): 0 local, 1 requesting categories, 2 requesting scores, 3 submitting, 4 global, 5 error; `UpdateButtons` runs the Lua `UpdateButtons();` (it disables `view`, `viewlocal`, `submit`, `categoryleft/right`, then enables by state; with local-only `moreinfo`, `view` and `submit` stay disabled).
  - `PostChildrenInit`: starts a 20 ms window animation (it only polls the server request), binds the ten rows by name (`name%d`, `score%d`, `%d`, `p1_%d`), with local-only moves `leftpanel` 130 px (0x82) to the right, binds `gamemode`, runs `UpdateText` and `UpdateButtons`, and sets a field (+0x3c) of the `view_text` and `viewlocal_text` Texts to -6 (not traced).
  - `UpdateText`: `HideAllText` hides every row widget and the named Texts `eligible eligibleasterisk server error info category local global topplayers congratulations dnq yourrank yourrankglobalinfo rankvalue scrollup scrolldown`; `gamemode` gets `<outline color='000000' size='1'>` + string `scores_gamemode1..4` for game mode 0..3 + `</outline>` (empty for anything else); local-only hides both right panels (otherwise `rightpanel` in state 0 and `rightpanelsmall` in the others); in state 0 or 4 the table is drawn; in state 0 `info` (and `eligible`/`eligibleasterisk` when the user has a row) are shown, and local-only hides all three again at the end; state 1-3 shows `server`, state 5 shows `error` with the stored message.
  - `UpdateScoresTable`: local view (every state but 4): `local` shown unless local-only, `topplayers` shown only when local-only, the scroll buttons hidden, offset 0; global view clamps the offset to `count - 5` and shows the scroll buttons by `offset > 0` / `offset + 10 < count`. Per row (at most 10, from the table of the current game mode): `nameN` shown with the user name (then its line height drops from the box fit while the bounds do not fit), `scoreN` the plain `"%d"` of the score, `N` the `"%d."` rank (ties share a rank), `p1_N` only for a row that is not on the online table when neither local-only nor anonymous is set.
  - `OnMessage` (type 3, by button name): `gamemodeleft` / `gamemoderight` (only in state 0 or 4) step `TSettings::SetCurrentGameMode` by -1 / +1 with wrap over 0..3 (the mode is global: it is also the mode the next Story or Endless start uses), in state 4 re-request the scores, then `UpdateText`, `UpdateButtons`, handled; `categoryleft/right` wrap over the server's categories and request scores; `view` requests the category list (state 1); `viewlocal` returns to state 0; `scrollup` / `scrolldown` move the offset by 10; `submit` builds a `THiscoreSubmit` modal over `scripts/hiscoresubmit.lua`.
  - The four local tables are `TPfHiscores` tables named by `_PFGAMEMODENAMES` (`greatchocolatechase_storymode`, `..._endlesseasy`, `..._endlessmedium`, `..._endlesshard`), selected by the current game mode. `RecordScore(score, replaceOwn = true, a, b)` keeps rows sorted by score, a new row goes after every row whose score is at least as high, at most 500 rows, one row per user (a better score replaces the user's row, an equal or lower one changes nothing), ties share a rank, and the tables are saved after every change. Callers: `ShopWindow::LogScore(score, n)` writes `a = "%i"` of `n` and, in story only, `b = ` the achievement manager's medal xml; the story end-of-level calls it for a passed level only, after the `adventure` achievement check, with `total_worth` and `GetHighestComplete()`; the endless end calls it with `current_worth` and the play time in whole seconds (`b` empty); `TSettings::ChangeCurrentUserName(new)` first logs the old name's row of each of the four tables again under the new name (`a` of the old row, `b` the medal xml; the old rows stay), then stores the name.
  - The server is long dead (`playfirst.com`, `hiscoreserver.txt`, `hiscore.xml`, a local `user:hiscore.dat` with a checksum).
- **`TXSellWindow`** + `xsellstyle.lua`: cross-sell from `xsell/xsell.lua`, which is not shipped; `moregames` disabled (above) -> **inert**.
- **`VUpsellWindow`** (`scripts/upsell.lua`, `upsell.xml`): registers Lua globals `SetupUpsell()` (parses `upsell.xml`, collects the `link` attribute of every `<gameinfo>` into a list: Chocolatier 1, GREATCC, Chocolatier 2, Chocolatier 3 (placeholder "www.google.com")) and `LaunchGameLink(i)` (opens the browser on link `i` via `TPlatform::OpenBrowser`). `upsell.lua` buttons call `LaunchGameLink(0..2)`; zeppelin art `upsell_zeppelin`. **Can be inert** (shown from the main menu upsell button only; links are dead marketing URLs).
- **`VHelpWindow`** (`scripts/help.lua`): `TLayeredWindow` with N layers (one per help page) registers Lua `SetPage(n)`: clamps `n` to `[0, layers-1]`, shows layer `n`, sets header text `help_title_<n>`, page counter `"<n+1>/<N>"`, disables the "next" button on the last page and "previous" on page 0. `help.lua` calls `SetPage(0)` at the end. Needed only if the Help screen is wanted (pure UI).
- `VRegister` (cash/score number display), `VTintWindow`, `VObjectWindow` (endgame, options, upsell) are shared SDK-level UI classes (other analysts).

## 8. Persistence summary (this area)

| what | storage | format |
|---|---|---|
| show tutorials | RState int block `show_tutorials` | 1 = show |
| tutorial state | RState binary block `tutorials` | byte at idx (all tutorials use idx 0) |
| plot item done | binary block `plot_<location>` | byte idx: 1 = complete |
| plot value | binary block `plot_value_<location>` | int32 per idx |
| quest status | binary block `quest_block` (100 B) | byte per quest slot: 0/1/2 |
| quest item status | `quest_<location>` | analogous to plot |
| game intro seen | value `game_intro` (saved) | 0/1 |
| highest / current level | values `highest_level` (max 60), `current_level` (max 61) (saved) | ints |
| volumes / mute | per-user prefs `soundlevel`, `musiclevel` (x1000, default 750), `soundoff` | ints |
| tutorial/diary/upgrade timing metrics | `TGameState` keys `TutorialTime_<n>`, `DiaryTime_<n>`, `UpgradeScreenTime`, `MetaMapScreenTime`, `MainMenuScreenTime`, `SweetRewardsScreenTime`, `TutorialEnd` | analytics only; **safe to ignore** |

## Unknown / not determined

- Whether the tutorial state byte sharing (idx 0) is intentional; which code (if any) writes state 2.
- `VTutorialSpider::Process` full tint arithmetic.
- `VPlotItem::Update`/`VPlotStep::End` details; whether `VQuestItem::PostLevelProcess` is ever called; level 8's double quest.
- Quest HUD colours and `GetStatusInfo` exact string keys; `VUpgradeSelect` effect class; keyboard handling in `VPrelevelWindow::OnKeyDown`.
- Windows Lua bytecode was not diffed against the Mac Lua source (XML is identical).
- `THiscoreSubmit`/`TCredits`/`TChoosePlayer` internals not decoded beyond natives and strings; the -6 stored in `view_text`/`viewlocal_text`.

## Implementation notes for OpenConche

- No GREATCC game module exists in `src/games/` yet (only `choc1`, `choc2`, `choc3`, `common`). The pieces here sit on the shared `VSpriteWindow`/`VSpriteManager` layer that the level analysts are specifying; all of tutorials, plot, quests, world map and prelevel windows are `V*` classes of the game, so they must be written new (the C3 engine has none of them).
- Suggested module split: `TutorialManager` (data from `game_tutorials`, a `LockdownController` that owns the exclusive set + dim effect + step pointer), `PlotManager`, `QuestManager`, `WorldMapWindow` + `PushPin`, `PrelevelWindow` + `UpgradeList/Tab`, `RewardNotify/RewardsWindow`.
- Hit-testing contract needed from the sprite layer: `lockdown flag` + `exclusive sprite set` where a sprite is only hit if (no exclusive set and not lockdown) or it is in the set; `FindSpriteByName`; `AddGlobalEffect` (tint) and per-sprite effect add/remove.
- Window-layer contract: `VTutorialWindow` swallows `OnMouseDown`/message traffic; `HandleMouse` uses `ChildWindowFromPoint` and a temporary "collapse width to 0" trick for `kIgnore` windows. Window-level messages (type 3 + name) must be routed to the manager first (`VPrelevelWindow::OnMessage` forwards them while `TutorialInEffect`).
- Lua natives required by this area: `PlayTransition`, `ShowCurrentLocation`, `ResetMap`, `DoCheat` (cheat only), `SetupUpsell`, `LaunchGameLink`, `SetPage`, `ShowNewAchievements`, `HideNotification`, `CheckAllAchievements`, `MainDebug`/`SetWelcomeText`, `SaveVolumes`, `SetPlayerChoices`, `GetSelectedUserIndex/Name`, `SetNameField`, `GetState`/`ScoreAvailable`/`UpdateText`/`InitUserScore`/`SubmitToServer` (hiscore), `LevelSetup`, `GetEndlevelText`, `GetGoalScore`, `GetMasteryScore`, `GetManagedInt/SetManagedValue`, `SetCurrentLevel`, `GetHighestComplete`, `SetCurrentGameMode`, `SetCurrentDifficulty`, `PauseSounds`, `ToggleSoundMute`; most are shared with C3 (`docs/re/api-globals.md`). Window classes referenced by Lua: `VWorldMap`, `VPlotWindow`, `VQuestWindow`, `VPrelevelWindow`, `VUpgradeList`, `VUpgradeTab`, `VRewardNotify`, `VRewardsWindow`, `VChooseAchievements`, `VHelpWindow`, `VUpsellWindow`, `TMainMenu`, `TChoosePlayer`, `TOptions`, `TCredits`, `THiscore`, `THiscoreSubmit`, `VTintWindow`, `VRegister`, `VShopHud`, `ShopWindow`.
- Safe to stub: XSell, Upsell, hiscore server, `VChooseAchievements`, `MainDebug`, all `*ScreenTime`/`*Time_<n>` metrics, `THiscoreSubmit`.
- Priority for playing start to finish: (1) world-map selection + unlock rules (section 4), (2) prelevel shop (5) because quest rewards/upgrades gate difficulty, (3) tutorial lockdown on level 1 (1.6 rules 1-3), (4) plot diary pages (single-page, OK to close), (5) quests evaluation (3.3), (6) rewards windows.
- Audio-related facts of this area (music `theme.ogg` at main menu, `PauseSounds(true, kSFXSoundGroup)` in the shop, `purchase_upgrade_button.ogg`, zeppelin SFX, options volumes) are repeated in `docs/re/greatcc-audio.md`.
