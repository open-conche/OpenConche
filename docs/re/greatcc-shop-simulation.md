# Great Chocolate Chase: the shop simulation (ShopWindow, levels, customers, scoring, HUD)

Labels: **F** fact (read from the Ghidra decompile of `re/greatcc/GreatChocolateChase_i386`, its disassembly, or shipped data; cited as `Class::Method` or file), **I** inference, **U** unknown.
Data paths are relative to `Great Chocolate Chase.app/Contents/Resources/assets/` in the Mac bundle ("assets/"). The Windows build's `levels/*.xml` and `values.xml` (ZXML-decoded) are byte-identical to the Mac files (F, compared all 60 levels plus goals and values), so nothing below differs per platform.
Everything here is native C++ driven by `scripts/game.lua`, `hud.lua`, `prelevel.lua`, `endlevel.lua`, `pause.lua`, `loading.lua`; there is no Lua gameplay logic.

Terminology trap (F): the game has **no cash register station**. `VRegister` is only the odometer-style digit display of the score/cash (section 8). Customers pay by leaving money on their counter, which the player collects.

Classes outside this area that the simulation calls (interface only, other analysts): `VPlayer` (two hands, `TravelTo`), `VChocolate`/`VChocolateVendor` (order construction, price), machines/`VDelivery`/`VConveyor`, `VSpriteManager`/`VSprite`/`VSuspendable`, `VValueManager`/`VValue`, `VUpgradeManager`, `VTutorialManager`, `VPlotManager`, `VQuestManager`, `ZoneController`.

---

## 1. The big picture

- F: One `ShopWindow` instance (a `VSpriteWindow`) exists for the whole game session. `game.lua` builds it once, with a `VShopHud` and `VQuestWindow` as children, and with one Lua table per location (`location_iquitos`, ...: `background`, `stations`, `player`, `customers`, `objects`, `chocolates`, `plot`, `music_01`) plus the shared `tutorials` and `posse` tables. `ShopWindow::PostChildrenInit` stores those tables, sets the singleton `mInstance`, and sets `TSettings.gameLoaded` (offset 0x15) to true. `IsGameLoaded()` in Lua reads that flag.
- F: Each level (re)start is `ShopWindow::LevelSetup()` (Lua global `LevelSetup`, called from `loading.lua`). It tears down and rebuilds everything for the level: sprites, stations, customers, background, chocolate vendor, upgrades, tutorials, plot, music. The same window then runs the level through its animation task `ShopWindow::OnTaskAnimate`, ticked every 16 ms (F: `VSpriteManager::OBJECT_UPDATE_INTERVAL` = 16, read from the binary).
- F: Scripted customers come from `levels/levelNN.xml`; the endless mode generates them. Orders come from the same XML (story) or from `VCustomer::GenerateOrders` (endless).
- F: The player is a `VPlayer` character with two hands. Clicking a station or customer sends the player to a "player point" in the zone graph; on arrival the station triggers the machine or customer logic (section 6).

### ShopWindow field map (F, offsets from `this`; useful when reading the decompile)
| Off | Meaning |
|---|---|
| 0xec | `VSpriteManager` (inherited); its +0xee byte is the "soft pause" flag, +0xec the "lockdown" flag |
| 0xf8 | start countdown, ms (set to 1000 by `LevelSetup`) |
| 0xfc | "level not started yet" flag |
| 0x100 | `CSLevel` of the current level |
| 0x108 | map location name -> `VLuaStyle` (the six location tables) |
| 0x120 / 0x128 | tutorials table / posse table |
| 0x130 | (reserved style) |
| 0x138 | `VPlayer` |
| 0x140 | map station name -> `VStation` (`station01` .. `station17`, lowercase) |
| 0x158 | list of pending `VCustomer` (scripted, not yet arrived) |
| 0x160 | list of active `VCustomer` (on screen) |
| 0x168 | `VBackground` |
| 0x170 | `VCustomerVendor` |
| 0x178 | `VUpgradeManager` |
| 0x180 | `VChocolateVendor` |
| 0x188 | `ZoneController` |
| 0x190 | `VValueManager` |
| 0x198 / 0x1a0 | `VTutorialManager` / `VPlotManager` |
| 0x1a8 | `VShopHud` |
| 0x1ac | `RTimer` (the HUD's `game_timer`, i.e. `VShopHud+0x30`) |
| 0x1b0 | `RActiveTextLayer` (floating texts) |
| 0x1b4 | `VQuestWindow` |
| 0x1bc / 0x1c0 | combo chain: last combo type / chain counter |
| 0x1c4 | endless-mode flag |
| 0x1c8 | endless: elapsed ms at which the next customer spawns |
| 0x1cc | endless: elapsed ms of the last pseudo-level check |
| 0x1d0 | "level over" timestamp (timer ms), 0 while running |
| 0x1d4 | "waiting on zeppelin" flag (`ShopWindow::WaitOnZeppelin`) |
| 0x1d8 | goal-popup progress: 0 none, 1 goal shown, 2 mastery shown |
| 0x1dc | "inside LevelSetup" flag |
| 0x1dd | "plot dialog pending" flag |
| 0x1de | "prelevel pending" flag (set by `ReadyForPrelevel`) |

---

## 2. Level lifecycle

### 2.1 Lua-side flow (F: `scripts/*.lua`)
1. Main menu: "single" -> `SetCurrentGameMode(0)`, "endless" -> `SetCurrentGameMode(1)`; both go to `worldmap.lua`.
2. `worldmap.lua::CloseWorldMap`: in endless (`GetCurrentGameMode() ~= 0`) it shows `choosedifficulty.lua` and calls `SetCurrentDifficulty(diff)` (diff 0..2; negative = cancelled). Then, if `IsGameLoaded()` it fades and opens `loading.lua`; the first time it swaps to `game.lua` (which builds the `ShopWindow`) and then opens `loading.lua{isFirst=true}`. Story mode picks the level with `SetCurrentLevel(...)` (worldmap lines ~179-183: level 20 / 60 for the "first peek" demo end, otherwise `GetHighestComplete()+1`).
3. `loading.lua`: fades from black, then `CloseLoadingScreen`: if `IsGameLoaded()` -> `LevelSetup()` (blocking), fade to black, `CloseLoading()` = `CloseWindow(); InsideGame(true); fade from black running "ReadyForPrelevel();"`. If the game isn't loaded -> `QuitToMainMenu()`.
4. `ReadyForPrelevel()` only sets `ShopWindow+0x1de`. On the next `OnTaskAnimate` tick (story only) the window clears it and: if `CSLevel::NeedPrelevel` is false, sets the plot flag (0x1dd); otherwise runs Lua `DoModal('scripts/prelevel.lua')` and sets the level's prelevel flag to false so it shows once per `LevelSetup`.
5. `prelevel.lua` (upgrade shop, shows `VRegister context="prelevel"` = current cash). Its Play button runs `InsideGame(true); Close(); PauseSounds(false, kSFXSoundGroup); CloseWindow()`. `Close()` is `VPrelevelWindow::Close` (the window's constructor binds the Lua global): it ends the previews and calls `ShopWindow::UpgradeComplete` (F, *meta* 5.1), which sets the plot flag (0x1dd) when the vendor exists, so the diary follows the shop.
6. The HUD "Upgrade" button (extension tab) opens `prelevel.lua` mid-level via `DoModal` **in endless mode only**: `VShopHud::LevelInit` hides the whole `extension` window (button included) in story mode (section 8), where the upgrade shop is the pre-level screen. HUD "Menu" opens `pause.lua` in both modes.
7. When the level ends, native `DoEndLevel` runs Lua `DisplayDialog{'scripts/endlevel.lua', level_pass='endlevel_pass|endlevel_fail|endlevel_master', level_num=N, level_status=0|1|2}` (story) or `level_pass='endlevel_endless', time='m:ss', level_status=0` (endless). `endlevel.lua` calls `InsideGame(false)` and reads `GetGoalScore(level_num)`, `GetMasteryScore(level_num)`, managed ints `current_level_total`, `served_this_level`, `angry_this_level`, `current_worth`, and `GetEndlevelText(level_status)`. Its Continue button calls `GotoNextLevel()`: endless -> `loading.lua` again (same location); story -> endgame screen if (`firstpeek`=="1" and `GetCurrentLevel()>20` and `GetHighestComplete()>=20`) or (`GetCurrentLevel()>60` and `GetHighestComplete()>=60`); else `worldmap.lua` if managed `do_map_transition`==1; else `loading.lua`. A failed level does not advance `current_level`, so Continue (labelled "try again" when `level_pass=='endlevel_fail'`) reloads the same level.
8. `pause.lua`: Resume = `InsideGame(true); PauseSounds(false, kSFXSoundGroup); PopModal`. Map / Restart / Quit ask `yesno.lua` first, then call `DoMetrics(2,false)` (abandon marker) and go to the world map / `loading.lua` / `QuitToMainMenu()`. All native flags are `TSettings`: `InsideGame(b)` stores `TSettings+0x14` and, if the game mode is story, writes the game-state flag `StoryModePlayTime`; if `b` is true and the app is not the foreground application it immediately opens the pause modal (so losing window focus pauses). `QuitToMainMenu()` pops every modal that has a parent (top first), then `VSoundManager::PurgeSounds`: with the Screen as the one parentless modal that is every modal but the Screen, the one `DoMainWindow` pushed included (the screens swap into it: main menu, map and `game.lua` are successive contents of that modal), so `DoMainWindow` returns and `Main` pushes a fresh main menu (I).
9. Pausing is a modal stack effect: while `pause.lua`/`prelevel.lua` are on top, the shop's modal is not top, so its animation task and `RTimer` do not advance (I; `RTimer` constructor binds to the top modal's `TClock`, `ShopWindow` is a modal child). No explicit pause code exists in the shop for dialogs. The ESC key is **not** handled by `ShopWindow` (it only handles Ctrl+C = console); `RGame::OnKeyDown` (ESC -> pause) exists but `game.lua` never instantiates `RGame` or `RScorer` (F: grep of scripts; `RScorer` is only in a commented-out block of `hud.lua`). The pause on Escape is the application's main loop, which pauses whenever the "inside game" flag is set (`docs/re/greatcc-boot-and-natives.md` 1.2).

### 2.2 `LevelSetup` order of operations (F: `ShopWindow::LevelSetup`)
1. `endless = (TSettings.gameMode != 0)`; set "inside LevelSetup" (0x1dc); `VSoundManager::PurgeSounds`; `VSpriteWindow::Reset` (= `VSpriteManager::Reset`: it zeroes the update clock, the hover time and the soft-pause flag, puts the last mouse position at (-10000, -10000) and then calls vtable slot 0, `RecalcMouse` (the manager's vtable is `PTR_RecalcMouse_00283ec8`), so the next update clears the hover; it removes no sprite, and the lockdown byte and the exclusive set are not touched; ours also removes every object here, since the stations and background are rebuilt by the load); `VAssetManager::BeginLoad`.
2. First call only: `OneTimeInit` (see 2.4).
3. Load `assets.xml` into the asset manager; clear both customer lists; zero combo chain (0x1bc, 0x1c0), level-over stamp (0x1d0), goal popup state (0x1d8).
4. `VValueManager::Reload()`: **re-parses `values.xml` and clears every value first** (F: `LoadValues` clears the map). So all `*_this_level`, `current_level_total`, `current_worth`, modifiers etc. return to their defaults; only `saved="true"` values come back from the user's prefs. Values that are not declared in `values.xml` (`current_level_total`, `served_this_level`, `angry_this_level`, `mastery_this_level`, `menu_/service_/payment_multiplier_this_level`, `zeppelin_served_this_level`, `decayed_this_level`, `endless_level_counter`) are created lazily at 0.
5. Endless only: `endless_diff_modifier` := 0 (difficulty 0), `endless_medium_modifier` (-0.3) if difficulty 1, `endless_hard_modifier` (-0.5) if difficulty 2.
6. Choose the level number N. `VLevelManager::GetCurrentLevel()`; if 0 it is set to 1. Story: N = current level. Endless: N is normalised to the **first level of the current location**, `N = 10*floor((cur-1)/10) + 1` (20 -> 11, 15 -> 11, 21 -> 21, 60 -> 51), and `SetCurrentLevel(N)`. `VQuestManager::SetCurrentLevel(N)`; `VQuestWindow::LevelInit`.
7. Load `levels/levelNN.xml` (`%0.2i`) with `CSLevel::Create(N, path)`; hand it to `VLevelManager::SetLevelObject` (which also sets current level and the manager's "already completed" byte).
8. Story only: `current_worth` := `VLevelManager::GetCurrentScore()` (the per-location cash, section 7.4); if the level has `chocolatedecay` (ms, nonzero) set value `chocolate_decay_rate` := 100 / chocolatedecay; if `customerdecay` > 0 set value `level_customer_decay` := customerdecay + 1.0.
9. `VUpgradeManager::SetNoSave(endless)`; `VUpgradeManager::SetCurrentLevel(level, "locations/<loc>/upgrades.xml", -1, previousCurrentLevel, !endless, ...)`; reset tutorials; `VTutorialManager::SetCurrentLevel`.
10. Using the location style (`GetLocationStyle(level.location)`): `LoadChocolates` (key `chocolates` -> `VChocolateVendor`), `LoadStations` (see 6.1), `LoadCharacters` (keys `objects`, `customers`, `player`), `LoadBackground` (key `background`), `LoadPlotItems` (key `plot`). Then `VUpgradeManager::ResolveBoundObjects`, `VPlotManager::SetCurrentLevel` (story passes the level, endless passes null), `VChocolateVendor::UpdateCapability(true)`.
11. Story: `VChocolateVendor::SetMaxLayers(level.maxlayers)` (default 3). Endless: `0x1cc` := timer elapsed, `0x1c8` := **3000** (first customer at 3 s of elapsed time).
12. Global chocolate counter reset to 0. Story only: for each `VCustomerData` in the level (XML order) look up the prototype with `VCustomerVendor::GetCustomer(data.type)`; if found, `SetCustomerData(data)` on the fresh copy and append to the **pending** list (0x158). A type name that exists in neither the posse nor the location table silently produces no customer (F).
13. `VShopHud::LevelInit`, clear floating texts, start location music (`music_01`), `VSpriteWindow::UpdateObjects`.
14. Arm the start: countdown 0xf8 := **1000 ms**, flag 0xfc := true, `RTimer` running := false, `VSpriteManager::SetLockDown(true)`, `SoftPause(true)` (the flag gates only what `sprites` 2.6 lists: customer arrival and patience, chocolate decay, new delivery orders; walking and machines are not stopped), 0x1d4 := false, `InitMetrics` (analytics state, see 10), `StartWindowAnimation(16 ms)`, `VAssetManager::EndLoad`, clear 0x1dc.

### 2.3 Per-tick state machine (F: `ShopWindow::OnTaskAnimate`)
Order within one tick:
1. **Start gate**. If the 1000 ms countdown (0xf8) is still > 0: subtract `VSpriteWindow::GetElapsed()` and clamp at 0. Otherwise:
   - If 0xfc is clear (level running): in story, no tutorial in effect, `VTutorialManager::ShouldShowTutorials()` true and the timer exists -> `VSpriteManager::SetLockDown(level.InLockDown(percentElapsed))` (re-evaluated every tick; when a tutorial is in effect this branch is skipped and the tutorial owns the flag; when tutorials are disabled the flag is forced false every tick). The argument is `RTimer::PercentElapsed()` = `min(100, elapsedMs / durationMs * 100)` (0..100, not 0..1; 0 when the duration is not positive); `CSLevel::InLockDown(p)` = `min <= p <= max`, inclusive, comparing against the `<lockdown min max>` floats **unscaled** (default -1/-1, i.e. never). Only level 1 has one (`min=0 max=10`): the lockdown lasts while the running level timer is within the first **10 %** of its 30 s, i.e. the first 3 s of counted time (the timer does not count during the pre-level phase or while a tutorial is in effect), plus the whole pre-level phase (step 14 set it on). What lockdown blocks: `VSpriteManager::IsValidSprite` requires the sprite to be in the exclusive set whenever lockdown is on or the set is non-empty; `OnMouseDown` (hover target becomes pressed), `OnMouseUp` (the Up/Click delivery) and `DoHitTest` (candidates) all call it, so with an empty exclusive set no sprite of the level layer receives hover, down, up or click. Keyboard handling does not consult the flag (F); the HUD buttons are `TWindow` buttons, not sprites, so I that they stay usable.
   - If 0xfc is set and (endless, or neither 0x1de nor 0x1dd is pending): **start the level**: clear 0xfc; `RTimer` running := true with start stamp = now; `SoftPause(false)`; `SetLockDown(false)`; fire tutorial trigger `endless_start`.
   - Race note (I): in story, if `ReadyForPrelevel` has not arrived by the time the 1 s countdown ends, the gate above would start the level before prelevel. The loading screen's fade normally finishes first and the shop clock does not run underneath modals, so this is expected not to matter.
2. `UpdateCustomers()` (section 5.1).
3. `VShopHud::UpdateHud()` (section 8).
4. Story only: pending prelevel (0x1de) handling as in 2.1 step 4; pending plot flag (0x1dd): if the level has plot entries (`CSLevel::NeedPlot`) and a plot manager exists -> `VPlotManager::LaunchPlotDialog()`.
5. **End detection**. Story: while `0x1d0 == 0`, not waiting on zeppelin (0x1d4), and either (timer elapsed >= total AND active list empty) or (active list empty AND pending list empty): stamp `0x1d0 := timer elapsed`. Endless: `UpdateEndlessMode()` (2.5), then if `angry_this_level >= angry_customer_limit` (default arg 5, declared value 10 scaled by difficulty) stamp `0x1d0`.
6. If `0x1d0 != 0` and timer elapsed - 0x1d0 > **2000 ms**: `DoEndLevel()` and **return false** (task stops). Otherwise `VSpriteWindow::OnTaskAnimate` (which runs `VSpriteManager::UpdateObjects`: `Update(paused)` on every sprite, effects, etc.).
- `LevelIsComplete()` (read by delivery machines): `0x1d0 != 0`, or (not endless and elapsed >= total).
- F consequence: after the time is up no new scripted customer is spawned (spawning requires elapsed < total), but customers already in the shop keep their patience decaying and must be served, pay, or leave angry before the level ends. A level also ends early when every scripted customer has come and gone. Customers whose `start` percent is never reached (the 135 values in levels 29, 30, 49, 50; see section 9) simply never appear.
- Money lying on a counter blocks the end: a customer is removed only when its state is 7, which for a paying customer happens when the player collects the money (or in endless when the money decays).

### 2.4 `OneTimeInit` (F)
First `LevelSetup` only: create `ZoneController` from `zones/zones.xml`; cache `VUpgradeManager`, `VPlotManager`, `VValueManager`; give the tutorials table to `VTutorialManager` and the sprite manager via `SetSpriteManager`; create the `VCustomerVendor` and `LoadPosseFromTable(posse)`; story: `VQuestManager::LoadFromLuaFile("locations/quests.lua")` + `LoadCurrentUser()`, endless: `VQuestManager::Clear()`.

### 2.5 Game modes and difficulty (F)
- `TSettings.gameMode` (offset 0x10): 0 = story, 1/2/3 = endless easy/medium/hard. `SetCurrentGameMode(m)` (valid 0..3) stores it, **resets difficulty to 0**, sets the hiscore property "game mode name" and the pref `gamemode`, and sets state `StoryModeSessionTime` (1 for story, else 0). `SetCurrentDifficulty(d)` (0..2): if the current mode is endless (>0) it calls `SetCurrentGameMode(d+1)`; then stores the difficulty at `TSettings+0`. `GetCurrentDifficulty` returns it. In `LevelSetup`, "endless" = mode != 0 and the difficulty (not the mode number) selects the modifier. Net effect: modes 1..3 are the same code path as long as the stored difficulty matches.
- Difficulty only changes data values (via the `byval="endless_diff_modifier"` modifiers in `values.xml`). Resulting numbers with modifier m = 0 / -0.3 / -0.5 (I on exact float rounding):
  - `customer_delay` = 15000*(1+m), clamped to [3000, 15000] -> 15000 / ~10500 / ~7500 ms.
  - `customer_delay_variance` = 6000*(1+m) -> 6000 / ~4200 / ~3000.
  - `angry_customer_limit` = 10*(1+m) -> 10 / 7 / 5.
  - pseudo-level thresholds (minutes) and ingredient/topping/infusion/novelty/truffle unlock minutes are multiplied by (1+m).
  - `endless_multi_chance` uses `elapsedSeconds / (1+m)`, so harder modes reach double orders sooner.
- "Expert/mastery" is the second score threshold of a level (section 7.2). There is no separate difficulty setting in story mode.

### 2.6 Endless mode specifics (F unless marked)
- No scripted customers, no timer limit (`RTimer` counts up, text = elapsed `m:ss`), no goal/mastery gauge, no plot, no prelevel, no quests. Score = `current_worth` (reset to 0 by `Reload`), not persisted into the location cash.
- Spawn: when timer elapsed >= `0x1c8`: choose a free counter (any of `station01..06` with no customer), take `VCustomerVendor::GetRandomCustomer()` (uniform over the union of **all 12 posse characters and all 12 location locals**, `Rand32 % count`), add it to the window and active list, `SetActive(station, 100.0)`. If no counter is free nothing spawns, but the next spawn time is rescheduled anyway. `0x1c8 := elapsed + max(0, (rand % variance + delay - variance/2) * (1 - m))` where `m = clamp(0.0006 * (elapsed/1000 seconds, integer), max 0.90)` (`customer_delay_modifier`, caps after 1500 s = 25 min).
  Note `variance` and `delay` are `GetUint` (floored), so `rand % variance` is in [0, variance-1] and the offset range is [-variance/2, variance/2 - 1].
- Spawn details (F, `UpdateCustomers`): the arrival is only the random character, `AddObject`, the active list and `SetActive(station, 100)`: no arrival sound and no `++TotalCustomers` (those belong to the story branch). The spawn is skipped when the vendor or a free counter is missing, and the next time is set either way. `GetRandomCustomer` builds a list of the posse (key order) followed by the locals (key order); with one entry or none it draws nothing and takes index 0, else `Rand32 % size`; the chosen prototype is copied and initialised like `GetCustomer`'s. The first arrival is due at 3000 ms of timer.
- Money decay (F, `VCustomer::Update`): the stamp (+0x2a8) is the customer clock's time when state 6 is entered (also with no money object or no station) and 0 in every other case; each update outside endless, with no stamp, or in any state but 6 writes 0 to it. In endless with state 6, when `now - stamp > endless_money_decay` (unsigned compare) the stamp is cleared, the pile (+0x2c4) has its effects cancelled and gets a fade-out alpha effect of `transition_time` (200) ms, and the state becomes 7.
- `UpdateEndlessMode` (every > **250 ms** of timer, `CHOC_VENDOR_UPDATE_INTERVAL` = 250 read from the binary): let `L` = `VLevelManager::GetCurrentLevel()`, `k = L % 10`. If `k > 0` and `endless_threshold_k` (minutes, difficulty-scaled) <= `(elapsed_ms/1000)/60.0`: `SetCurrentLevel(L+1)`, `VUpgradeManager::SetCurrentLevel(L+1, "", -1, -1, ...)` (grants the story upgrades of that pseudo-level), `VChocolateVendor::UpdateMaxLayers(elapsed_ms/60000)` (integer minutes as a float; the max layers become 3 when `endless_three_layers` <= minutes, else 1 when minutes < `endless_two_layers`, else 2; both values ship as 0, so it is always 3) and `UpdateCapability(true)`, tutorial `tutorial_endless_2` (`CheckTutorial` by name: it starts when no tutorial is active and it has not been shown). Stations are not told to `UpdateAvailable`, and the upgrade shop's availability follows the pseudo-level: the Iquitos shop offers its first purchases at level 4 (`minlevel`), which Hard reaches 2.5 minutes in. Thresholds in minutes (before difficulty): k=1..9 -> 1,3,5,7,9,11,12,13,15. The level number is normalised back at the next `LevelSetup`.
- Random orders: `GenerateOrders` (section 5.4). Money on the counter vanishes after `endless_money_decay` = 15000 ms (customer `Update`, state 6 -> 7).
- End: `angry_this_level >= angry_customer_limit` (any customer leaving angry counts, section 5.3), then the 2 s delay.
- Result: `DoEndLevel` stores `endless_time` (seconds = timer elapsed / 1000, `SetValue` called with the save flag **false**, so it is only written to prefs by a later `SaveValues`; the endless branch itself never calls `SaveValues`), runs achievement checks `endless` then `adventure`, logs `LogScore(current_worth, seconds)` to the hiscore service, and shows `endlevel.lua` with `level_pass='endlevel_endless'`, `time=<RTimer text>` and no `level_num`, so the script's `GetGoalScore`/`GetMasteryScore` calls get nil (the labels they make are replaced or hidden by the script's endless branch, which shows `current_worth` as Total Score, `time` as Play Time, and hides the Master Score and Customers Lost lines).

### 2.7 `DoEndLevel` (story) in order (F)
1. `statusBefore = VLevelManager::GetCompleteStatus(N)`; `total = GetInt("current_level_total")`; `result = VLevelManager::LevelComplete(total, &delta)` (section 7.2).
2. `level_pass = "endlevel_fail"`. If `result != 0`:
   - `level_pass = "endlevel_pass"`; if `statusBefore == 0` add 1 to saved value `completed_<location>` (`completed_iquitos`, ...).
   - if `result == 2`: `level_pass = "endlevel_master"`, and if `statusBefore != 2` add 1 to saved `num_levels_mastered`.
   - `VQuestManager::PostLevelProcess()`.
   - `total_worth += delta` and `current_worth += delta` (both saved-flag adds), then `VLevelManager::UpdateCurrentScore(current_worth)` (writes the per-location cash, section 7.4).
   - `VLevelManager::IncrementLevel()`: since status for N is now != 0, `SetHighestComplete(N, false)` (only ever raises it) and `SetCurrentLevel(N+1)`. If the new current level `% 10 == 1` and `GetHighestComplete() < new level` (first time entering a new location) set managed `do_map_transition` = 1 (the world map plays the location transition on next entry).
3. `VValueManager::SaveValues()` (always, even on fail).
4. If the level number (value read **before** the increment) is > 59 and `result != 0`: `VAchievementManager::CheckAllAchievements("endgame")`. Always then `CheckAllAchievements("adventure")`.
5. If `result != 0`: `LogScore(total_worth, highestCompleteLevel)` -> `TPfHiscores::LogScore(score, true, "%i", medalXml)` (story passes the achievement medal XML; the call is skipped when no hiscore object is configured).
6. Run the Lua dialog (2.1 step 7) and `DoMetrics(result==0, result==2)`.
Nothing is awarded for a failed level: cash, `total_worth` and `current_level` are untouched, and `current_level_total` is discarded by the next `Reload`.

### 2.8 `GetEndlevelText(status)` (F)
Story: `VQuestManager::GetPostLevelText()` if non-empty; else string key `endlevel_blurb_1` when `status != 0`, otherwise `endlevel_blurb_2`. Endless: `endlevel_endless_blurb_1`. (Text comes from the string table; Lua prefixes it with `#` to make a literal label.)

---

## 3. Data

### 3.1 `levels/levelNN.xml` (F: `VLevel::LoadLevel`, `CSLevel::LoadCustomNode`, `VCustomerData::ParseNode`, `VOrderInfo::ParseNode`)
Root `<level>`; children are matched by tag name. Tag names in `<customer>` are lower-cased before matching (`VCustomerData`); the level-level tags are compared as written (lowercase in all files).

| Tag | Parsed by | Meaning, default |
|---|---|---|
| `name` | VLevel | string-table lookup, stored at +0x18 (display only). |
| `location` | VLevel | `iquitos`, `grytviken`, `namche`, `krakatoa`, `kamchatka`, `gaberoun`; selects the `location_<x>` table and the `locations/<x>/upgrades.xml` path. |
| `minutes`, `seconds` | VLevel | ints, default 0; level length = minutes*60000 + seconds*1000 ms (HUD `LevelInit`). |
| `goal`, `mastery` | VLevel | floats (stored +0x20/+0x24) but **no shipped level uses them**; the real thresholds come from `levels/goals.xml` (7.1). |
| `plot` | VLevel | repeatable, string ids pushed on a list (`story_<loc>_<n>`); `NeedPlot()` = list non-empty. |
| `upgrade` | VLevel | two attributes stored as a pair list; unused in shipped levels (I). |
| `maxlayers` | CSLevel | int, default 3; passed to `VChocolateVendor::SetMaxLayers`. Not used by any shipped level. |
| `lockdown min= max=` | CSLevel | percent of level time during which input is locked down while tutorials show; default -1/-1. Only level 1: 0..10. |
| `chocolatedecay` | CSLevel | int ms until a finished chocolate in the open goes stale; 0 = never. Shipped: 15000 or 20000 in levels 51-60. |
| `customerdecay` | CSLevel | float extra patience-loss factor; `level_customer_decay = 1 + value`. Shipped: 0.0 (level 51), 0.2, 0.25, 0.3 (levels 51-60). |
| `zeppelin` | CSLevel | list of `<order>` consumed in order by the delivery station (`VDelivery::GenerateOrder` pops the front; when empty it makes a fully random chocolate). 41 levels have one (levels 14-60, 5..15 orders). |
| `customer` | CSLevel -> `VCustomerData` | one scripted customer, see below. 1577 customers over 60 levels (8 in level 1 up to 54). |

`<customer>` children: `name` (optional; sets the sprite/tutorial name, e.g. `tut_cust_1`, default = the type's own name), `type` (key in the posse or location customer tables, e.g. `professor`, `iqu_male`), `start` (float, **percent of level time** 0..100; default FLT_MAX), `end` (float, default FLT_MAX; parsed but **never read by the shop** since `UpdateCustomers` only uses `InRange` = `start <= percent`; only level 3 has one), `station` (e.g. `station03`; empty = any free counter), `decay` and `tip` (floats, default -1 = keep the character's own; not used in shipped levels), one or more `order`.
`start` is compared against `RTimer::PercentElapsed()` = `min(100, elapsed/total*100)` (F), so `start` is **not seconds**, even though level 1 (30 s long) lists starts up to 85.

`<order type=... shape=... mix=... layers=...>` (`VOrderInfo`, F):
- `type`: case-insensitive `sauce`/`ksauce` = 1, `chocolate`/`kchocolate` = 0, `drink`/`kdrink` = 2, anything else = -1 (invalid). Only chocolate orders read further attributes.
- `shape`: `kBar`=0, `kTruffle`=1, `kNovelty`=2, `kInfusion`=3 (`VChocolate::GetTypeFromString`; unknown string, e.g. the typo `kTruffler` in one order, or missing attribute -> -1 which is stored as **-2 = "unspecified/random"**). Field default -2.
- `mix`: int 0/1/2 (dark/milk/white mixer buttons) if the attribute is present and non-empty; default -1 = random. (The attribute is read through `str::length`, so `mix=""` also means random.)
- `layers`: int clamped to [1,3] (values <=1 -> 1; 2 -> 2; >=3 -> 3); absent -> 1. Stored at +0x20. Used by `GetChocolate` as the **total layer budget**, see 5.5.
- Child `<layer type="kIngredientLayer|kInfusionLayer|kToppingLayer" option="N"/>`: layer type index must be > 0 (`kIngredientLayer`=1, `kChocolateLayer`=2, `kInfusionLayer`=3, `kToppingLayer`=4, from `defines.lua`). `option="N"` pins that option (0..2, the ingredient/infusion/topping button); no `option` attribute = **-1: "include this layer, random option"**; layer absent = -2 = "not requested". Commented-out `<!--<layer` lines appear in 38 places and are ignored.
Valid shape/layer combos seen in data: Bar/Truffle/Novelty with optional ingredient and topping layers, Infusion shapes with infusion layer (+ topping).

### 3.2 `levels/goals.xml` (F: `VLevelManager::LoadGoals`)
`<levelgoals><levelgoal level="N" goal="G" mastery="M"/>...`. Parsed once in the `VLevelManager` constructor into per-level `{goal, mastery}` ints (plus best score and status, 7.3). Examples: level 1 = 100/160, 2 = 130/170, 10 = 1100/1400, 11 = 200/450, 60 is the last row. Goals reset per location (each location's first level has a low goal). Native: `GetGoalScore(level)`, `GetMasteryScore(level)` (Lua globals) read these; `VLevel`/`CSLevel::GetGoalScore` (formatted strings) read the XML-level floats and are not used by the shipped Lua (I).

### 3.3 `values.xml` entries used by the shop (F; base value, modifiers)
Price and payment: `layer_price` 10, `layer_quality_modifier` 0.1, `sauce_price` 15, `drink_price` 15, `salemodifier` 1.0 (+0.05 each `kCustoms1`, `kCustoms2`), `tipmodifier` 1.0 (+0.05 each `kService1`, `kService2`).
Patience: `patience_decay_modifier` 1.0 (-0.1 with `kPhonograph1`), `order_patience_bonus` 20.0, `complimentary_patience_bonus` 25.0, `level_customer_decay` (set per level, default arg 1.0 when absent).
Combos: `menu_combobonus`, `service_combobonus`, `payout_combobonus`, all 3.0 (`sauce_combobonus` is declared but never read, F).
Orders: `multi_order_min_level` 5, `multi_order_chance` 10.
Customer visuals/timing: `customer_fade_time` 500, `transition_time` 200, `thought_offset` 5.0, `thought_tint` 0.3, `customer_idle_cooldown_base` 15000, `customer_idle_cooldown_var` 5000, `chocolate_fade_time` 4000.
Endless: `endless_diff_modifier`, `endless_medium_modifier` -0.3, `endless_hard_modifier` -0.5, `endless_money_decay` 15000, `customer_delay_modifier` 0.0006 (max 0.90), `customer_delay` 15000 (min 3000, max 15000, +byval diff), `customer_delay_variance` 6000, `angry_customer_limit` 10, `endless_multi_chance` 0.001 (max 1.0), `endless_threshold_1..9` (minutes: 1,3,5,7,9,11,12,13,15), unlock minutes `endless_mix_option2` 2, `endless_mix_option3` 5, `endless_ingred_option1..3` 2/7/10, `endless_topping_option1..3` 7/13/15, `endless_infusion_option1..3` 10/13/15, `endless_novelty` 13, `endless_truffle` 7 (the unlock values are consumed by `VChocolateVendor::UpdateCapability`, not by the shop).
**Declared but not read by any native code** (F: the names are absent from the binary's string table, and `values.xml` itself marks the penalty as "not used anymore"): `drink_patience_bonus` (30), `complimentary_pickup_prob` (30), `endless_patience_penalty` (10), `sauce_combobonus` (3.0). Drinks therefore give the same +20 patience as everything else. (`endless_two_layers`/`endless_three_layers` and `player_*` do appear in the string table; they belong to the vendor/player.)
Persistence (`saved="true"`): `current_level` (0..61), `highest_level` (0..60), `game_intro`, `total_worth`, achievement counters (`num_bars`, `num_sauce_sold`, `num_infusions`, `num_truffles`, `num_deliveries`, `num_disposed`, `num_served`, `num_angry`, `endless_time`, `completed_<loc>`, `num_levels_mastered`, `num_char_upgrades`).
`VValue` semantics used here (F): `GetValue` = base + sum over active modifiers of (base * modifierValue), then clamped to [min,max]. Modifier kinds: upgrade (active when the named upgrade/category is active), plot item active, or `byval` (modifier value read live from another value). `GetInt` = floor, `GetUint` = floor clamped at 0. `GetProduct(name, x, clamp)` = value * x (clamped to [min,max] when `clamp`). `SetValue` clamps to [min,max]. `AddToValue(name, amount, ?, save)` creates a missing value at the default then adds.

### 3.4 `locations/` tables
- `defines.lua` (F): enums used by the Lua tables and XML: station types `kEmpty`0 `kCounter`1 `kMixer`2 `kIngredient`3 `kBarPress`4 `kTopping`5 `kSauce`6 `kInfuse`7 `kDrink`8 `kDisposal`9 `kDelivery`10 `kDisplay`11 `kConveyor`12; layers `kEmptyLayer`-1 `kBucketLayer`0 `kIngredientLayer`1 `kChocolateLayer`2 `kInfusionLayer`3 `kToppingLayer`4; bar types `kNothing`-1 `kBar`0 `kTruffle`1 `kNovelty`2 `kInfusion`3; facings `kLeft`0 `kRight`1 `kFront`2 `kBack`3; tutorial triggers `kNoTrigger`0 `kClick`1 `kResume`2 `kArrival`3 `kIgnore`4; blend modes; arrow orientations; `DarkChocTint` FColor(0.6,0.6,1.0). `locations/values.lua` is a stray `valueTable = {a=15}` (unused, I).
- `locations/<loc>/customers.lua` (F: `VCustomerVendor::LoadObjectsFromTable/LoadLocalsFromTable`, `VCustomer::LoadFromTable`): table `<loc>Objects` has `thought_bubble` (states `happy/content/neutral/annoyed/angry`, frames 0..4 of `thoughtbubble`, plus `transparent`), `anim_menu` (`menu`, `dots`), `anim_money` (`dollar`, tint), `orderables` (`sauce`, `drink` icons). Table `<loc>Locals` has two characters per location. A character table's keys: `name` (vendor key), `decay` (**milliseconds a fresh customer takes to lose all 100 patience**; if the value is < 1 it is used directly as per-ms rate; default 100000), `tip` (default 100; a dollar amount scale), `orderDelay` (ms the customer reads the menu, default 5000; never set in shipped data), `static_*` tables (body/head sprites per mood: `default`,`angry`,`menu`,`happy`,`neutral`,`annoyed`), `idle_*` tables (`idle_happy`, `idle_angry`), `assets`, `children`. Shipped `decay` values: posse actress 90000, aviator 110000, aviatrix 45000, diplomat 130000, industrialist 65000, painter 115000, princess 45000, professor 120000, scholar 145000, spy 55000, sultan 95000, yachtsman 100000; locals iquitos 100000, grytviken 95000, namche 80000, krakatoa 75000, kamchatka 70000, gaberoun 65000. Shipped `tip`: 8 everywhere except industrialist 5, sultan 5, gry_fisher 5, aviator 10.
- `locations/chocolateposse.lua`: the 12 posse characters, same format; loaded once by `OneTimeInit` and looked up first by `GetCustomer` (`GetCustomer(name)`: posse map, then locals map; both keyed by the table's `name`).
- `locations/<loc>/stations.lua` (F: `ShopWindow::LoadStations`, `VStation::LoadFromTable`): every key starting with `station_` whose value is a table becomes a `VStation`, stored under its lower-cased `name` (`station01`..`station17`). Layout is the same in all six locations: 01-06 `kCounter`, 07 `kDelivery`, 08/09 `kEmpty` (host sauce / drink machines), 10 `kInfuse`, 11 `kTopping`, 12 `kBarPress`, 13 `kIngredient`, 14 `kMixer`, 15 `kDisposal`, 16 `kDisplay` (centre table), 17 `kConveyor`. Keys read by `VStation`: `stationID`, `stationType`, `machines`, `playerPoint` (a list of alternating zone-point names and facings, e.g. `{"station01", kLeft}`, the display case has four), `custPoint` (counters only: zone-point name of the customer spot, `customerNN`), `money = {x, y}` (counters: where the pile of dollars and the payout text go), plus sprite assets. After loading, `ZoneController::DisablePoint("avatar","middle_center", true)`.
  `ShopWindow::HasCenterTable()` = `station16` exists and its visible flag (+0x10) is set; the player then routes through the centre via `ZoneController::AddPath` (`VPlayer::TravelTo`). Station16 is a `VDisplay`, whose `Init` shows it only while the upgrade `kDisplay1` is active (free from level 2 in Iquitos), so level 1 has no centre table and the avatar walks straight.
- `locations/machines.lua`, `quests.lua`, `tutorials.lua`, `plot.lua`, `upgrades.xml`, `player.lua`: other analysts.

---

## 4. Orders and `VOrderInfo` resolution (interface used by customers and the zeppelin)

`VOrderInfo` layout (F): +0 type (0 chocolate, 1 sauce, 2 drink), +4 shape (-2 unset), +8..+0x18 five layer slots indexed by `eChocolateLayer` (only 1,3,4 are ever written; -2 unset, -1 random option, >=0 pinned), +0x1c mix (-1 random), +0x20 layer budget (1..3, or -1 for non-chocolate).
`VCustomer::AddOrder(info)` (F):
- chocolate: `VChocolateVendor::GetChocolate(info)`; the resulting `VChocolate` is appended to the customer's order list (+0x30c). If the vendor returns null, nothing is added.
- sauce / drink: create a `VCarriable` from the vendor's `orderables`, tag it `sauce` or `drink`, set asset `sauce`/`drink` (sauce icon scaled to 0.8), append.
`GetChocolate(info)` (F structure, **I for the semantics of hidden arguments** and detail; owned by the chocolate analyst): layer budget = `info.layers` if >= 1, else 1 when the vendor's max layers < 2, else random `1..maxLayers` (`Rand32 % maxLayers + 1`). Shape: unless it is kInfusion, `shape = GetDisplayInfluence(shape layer, specified shape)`; if the result is -2 pick a random available shape (or -1 if none). Mix: pinned if `info.mix >= 0` else random via `GetRandomLayer`. Remaining slots in order ingredient(1), infusion(3, only when shape is infusion), topping(4): pinned options are built through `GetDisplayInfluence(layer, pinned option)`, "random" ones through `GetRandomLayer`; extra optional layers are added while budget remains (ingredient first, topping coin-flipped when only one slot is left).
**`GetDisplayInfluence(layer, default)` (F, args confirmed in disassembly)**: if the display case currently holds any item for that layer, then with probability `(display_influence_X + 1)/101` (`Rand32 % 101 <= value`; X = `mix` 30, `ingredient` 20, `shape` 30, `infusion` 20, `topping` 20) the default is **replaced** by a uniformly random option present in the display case; otherwise the default is returned. Consequence (F): even scripted, pinned XML options can be replaced by display-case contents when the display case is stocked.
`VChocolate::Compare(other)` (F): true when all five layer slots (0..4) match pairwise (both null, or `VChocolateLayer::operator` equal). `GetCost()` (F): for each present layer slot 1..4: `+layer_price` (10) and, if the per-option quality upgrade is owned, `+ layer_quality_modifier * layer_price` (+1). Upgrade names: `kIngredientQ{opt+1}`, `kMixerQ{mix+1}`, `kInfusionQ{opt+1}`, `kToppingQ{opt+1}` (category `station`). The shape does not add cost.

---

## 5. Customers

### 5.1 `ShopWindow::UpdateCustomers` (F)
Skipped entirely while the sprite manager is soft-paused. Story (not endless):
- Runs the pending-list pass only if the timer exists, is running (+0x59), and `elapsed < total`.
- For every pending customer in list order: `station = GetAvailableCustomerStation(data.station)`; if null (station occupied) skip; if `!data.InRange(percent)` skip; otherwise add its sprite to the window, push on the active list, `customer->SetActive(station, 100.0)`, play `audio/sfx/customer_arrives.ogg`, increment game-state `TotalCustomers`, and erase from the pending list. A null entry in the pending list is erased.
- `GetAvailableCustomerStation(name)` (F): if `name` non-empty look up that station (by the exact name; the stations are stored under their lower-cased names and the level files write them in lower case; a missing key is just null). If that is null (empty name **or unknown station**), build a vector of counter stations (`station01..06`, `stationType == kCounter`, no customer attached) and pick `Rand32 % size`. The result is returned only if non-null **and currently has no customer**; a named but occupied station returns null (the customer waits; it is not re-routed). The draw is made **before the range test and on every tick**: each pending customer with an empty or unknown station name costs one `Rand32` per tick while a counter is free, whether or not its `start` has been reached.
Both modes then, for each active customer (list order): `customer->Update(softPaused)`; if `ShouldDelete()`: get its station, `station->DetachCustomer()`, remove the sprite from the window, erase from the list.
The endless spawn branch is in 2.6.

### 5.2 State machine (F: `VCustomer` fields: state +0x2ac, mood +0x2b0, patience +0x290 float, decay +0x294 float per ms, tip +0x298, pending price +0x2a4, orderDelay +0x2a0, money-spawn stamp +0x2a8, wanted item +0x314, remaining orders +0x30c)
| State | Meaning | Entered by | Left by |
|---|---|---|---|
| 0 | prototype / inactive | constructor | `SetActive` |
| 1 | arrived (fading in, menu bubble over head), waiting for the player | `SetActive` | player arrives at the station -> 2 |
| 2 | "reading the menu" (suspended) | `OnArrival(player)` from state 1 | after `orderDelay` ms `VSuspendable` calls `OnResume` -> 3 |
| 3 | order shown, waiting for items | `OnResume` | last order served -> 4; patience <= 0 -> 5 |
| 4 | served completely | `OnArrival` when no orders remain | `Payout` -> 6 or 7 |
| 5 | leaving angry (patience hit 0) | `UpdateMood` when patience <= 0 and state < 4 | `Payout` -> 6 (if partial money) or 7 |
| 6 | money pile on the counter, waiting to be collected | `Payout` | player arrives at the station -> 7 (story); in endless also after 15000 ms -> 7 |
| 7 | finished; ready to delete | collect / no money / timeout | `ShouldDelete` once fade effects are over |
- `SetActive(station, startPatience)` (F): `Init` (which ends in `UpdateMood`, so the mood is happy from the first frame), record `TClock` stamp (+0x29c), state 1, patience = `startPatience` (100.0 from the shop), `SetStation` (position := the station's `custPoint` zone point, **no walking**), `station->AttachCustomer(this)`, start a fade-in alpha effect of `customer_fade_time` 500 ms, and if both a thought bubble and the menu object exist attach the bubble (anchor `bubble_single` of the customer's frame) with a single order, the menu object (shown at frame 1, anchor `menu`), showing the current mood. Fires tutorial trigger `customer_arrival` with the customer's sprite name.
- `OnArrival(player)` (F, called from `VStation::OnArrival` when a customer is present):
  - state 1: -> 2, `SetMood(mood)`, `VThoughtBubble::NextBubble()`, play `audio/sfx/menu.ogg`, `Suspend(orderDelay)` (default 5000 ms), `ShopWindow::AddCombo(1)` (menu chain).
  - state 3: (F, disassembly: the hand ids passed are 2, then 1) slot A is asked first (`GetObject(2)`), and slot B (`GetObject(1)`) only when slot A is empty or its item was refused; the first item `ServeCustomer` accepts is detached from that slot and the visit ends there, so **one item is handed over per arrival** (a customer with two orders needs two visits). Afterwards, whatever happened, if `HasMoreOrders()` is false: state 4 and `Payout()` (a customer whose orders list came out empty is "served" by any visit).
  - state 6: collect: compute the pile position (station's money position, +(0,-50) for the text), `ShopWindow::OrderComplete(price = +0x2a4, position, customer)`, fade out the money object over `transition_time` 200 ms, play `audio/sfx/score_board.ogg`, state 7.
  - any other state: nothing.
- `OnResume` (F, state 2 only): `GenerateOrders()`, state 3, `AttachThought()`, `SetMood(mood)`, fire tutorial trigger `customer_order` with the sprite name. **Orders are therefore created when the menu delay elapses, not when the customer spawns.**
- `Update(paused)` (F, per call):
  - not paused: every call applies decay while `0 < state < 4` (states 1, 2 and 3 all drain patience): `patience -= elapsedMs * decay * patience_decay_modifier * level_customer_decay`, clamped at 0, then `UpdateMood()`. `elapsedMs` = `VSprite::GetElapsed()` = time since this sprite's last update, **0 if >= 3000 ms** (long stalls are ignored). Note `ShopWindow::UpdateCustomers` and `VSpriteManager::UpdateObjects` both update the customer each frame, the second call sees ~0 ms.
  - customer fade effect finished -> hide the sprite.
  - idle chatter (decorative): while state is 1 or 3 and no idle anim is playing, count down `idleCooldown` (+0x240) by elapsed; at 0 `PlayIdleAnim` (needs fewer than 4 idle anims running game-wide; `idle_angry` when mood > 3 else `idle_happy`), then re-arm with `customer_idle_cooldown_base` (15000) + a random offset in [-var/2, var/2] (`var` = 5000). Idle anim end -> `StopIdleAnim`.
  - endless only: state 6 and `now - moneyStamp > endless_money_decay` (15000 ms): fade the money (alpha over `transition_time`), state 7.
  - then `VSuspendable::Update` (runs `OnResume` after the suspend duration).
- `ShouldDelete()` (F): fade effect (+0x32c) and money effect (+0x334) not active, and state == 7.
- `HasMoreOrders()` (F): remaining list non-empty **or** the wanted item (+0x314) non-null.
- `ReadyToOrder(percent)` (F body: `start <= percent <= end` and `HasMoreOrders()`): **no caller exists** in the binary (I: dead code).

### 5.3 Mood and patience (F)
Patience starts at 100 and has **no upper clamp**: `ServeCustomer` adds `order_patience_bonus` (20) and `ComplimentaryService` adds `complimentary_patience_bonus` (25) without capping, so it can exceed 100 (this also inflates the tip, 6.2).
`UpdateMood` (thresholds on patience p; mood only re-applied when it changes):
| p | mood id (`eCustomerMood`) | head state / bubble |
|---|---|---|
| > 80 | 1 | happy / happy |
| > 60 | 2 | happy (head), content (bubble) |
| > 40 | 3 | neutral |
| > 20 | 4 | annoyed |
| > 0 | 5 | angry |
| <= 0 | state := 5 (only if state < 4), then `Payout()` | - |
`SetMood(m)` stores it, forwards to the thought bubble, and sets every `static_*` sprite's asset state to `happy`(1,2)/`neutral`(3)/`annoyed`(4)/`angry`(5), or `menu` while the customer is in state 2.
Time to angry-leave from full patience = `decay` ms (e.g. 100000 -> 100 s) divided by `patience_decay_modifier` and `level_customer_decay`; mood bands are 20% each, i.e. `decay/5` ms (20 s at decay 100000). `kPhonograph1` lowers the decay modifier to 0.9. Levels 52-60 multiply the drain by 1.2-1.3.
`ComplimentaryService(item)` (F): returns false unless the item is non-null and the customer's state is 1..3; otherwise `patience += complimentary_patience_bonus`, plays `audio/sfx/chomp.ogg`, returns true. Caller: `VConveyor` (F: the conveyor offers an item to the waiting customer with the **lowest patience** among states 1..3 across counters 1..N, strictly lower than the target's; the conveyor analyst owns the rest).

### 5.4 Serving (`ServeCustomer(item)`) (F)
Accepts only if `item` and the wanted item (+0x314) are non-null and their type strings (+0x194: `chocolate`/`sauce`/`drink`) are equal. Then:
- chocolate: both must cast to `VChocolate` and `Compare` true; else **return false and change nothing** (wrong items are silently refused, no penalty, no sound, and the item stays in the player's hand). Price `+= GetCost()` of the chocolate.
- sauce: `num_sauce_sold += 1` (saved), price `+= sauce_price` (15). drink: price `+= drink_price` (15).
- Then, common: `AddCombo(2)` (service chain), patience `+= order_patience_bonus`, wanted item := front of the remaining list (popped) or null, `NextBubble()`, play `audio/sfx/customer_leaves.ogg` (yes, on every serve), increment game-state `OrderCompleted`, return true.
Which hand is tried first: slot A, then slot B (5.2).

### 5.5 `Payout()` (F; called when the last order is served, or on patience 0)
- `served = (state == 4 && no more orders)`.
- If state is 4 or 5: if `price <= 0` -> state 7 immediately (an angry customer that received nothing leaves with no money). Otherwise show the money (F, disassembly), when the customer has a station and a money object: the money object loses its children and shows its `dollar` state (fallback `default`); `n = floor(price/8)` extra sprites if `price < 48` else 5 (so a price under 8 gives none) are added to it as asset leaves (`AddAssetLeaf("money", "dollar", 0)`) with the pile's tint; leaf `d` (1..n) goes to `(+-(2 + (Rand32 & 3)), -d)` (the sign from a second draw, negative when `Rand32 & 1` is 0), is flipped horizontally by `(Rand32 ^ 1) & 1` (a third draw), fades in (`VAlphaEffect` fade in, 300 ms, delay `100 * d`) and plays from `100 * d` ms; then the pile is added to the station, put at the station's `money` position and played. State := 6 (also when there was no money object or station) and stamp the time for the endless money decay.
- Fade the customer out (`customer_fade_time` 500) if not already fading, play `audio/sfx/customer_leaves.ogg`.
- Counters: if `served`: `num_served += 1` (saved; story only), `served_this_level += 1` (both modes). Else (angry exit): `angry_this_level += 1` (both modes); story additionally `num_angry += 1` (saved) and game-state `TotalCustomersLost`. (In endless `angry_this_level` is what ends the game.)
- The customer's `+0x2a4` price accumulates only items actually served, so an angry exit after a partial serve still leaves money for the served part.

### 5.6 `GenerateOrders()` (F, structure; story rarely reaches the random path because every shipped customer has orders)
1. Clear the list. If the customer data has scripted orders: `AddOrder` each and return.
2. Needs a chocolate vendor.
3. Number of orders `n`: story: `L = VLevelManager::GetCurrentLevel() % 10`; if `L >= multi_order_min_level` (5): chance% = `multi_order_chance * (L - 5 + 1)` = 10*(L-4), `n = 2` if `Rand32 % 100 < chance`, else 1. Endless: chance = `clamp(0.001 * (elapsedSeconds / (1 + endless_diff_modifier)), max 1)`; `n = 2` if `chance > Rand32/2^32`, else 1.
4. For each order slot: sauce is "available" if the station upgrade `kSauce1` is active and the orderables object has a `sauce` asset; drink likewise with `kDrink1` / `drink`. If at least one is available and `Rand32 % 6 == 0` (1 in 6): make a sauce/drink order (if both available a coin flip `Rand32 & 1`: odd = sauce, even = drink). Otherwise a fully random chocolate: `GetChocolate(VOrderInfo(chocolate))`, then `VChocolateVendor::CanCreate`. If any chocolate so far is not creatable the order count is forced to at least 2 (so an un-makeable pick comes with another order) and, if the very first order is the un-makeable one and there are >= 2 orders, the first order is rotated to the back.
There is no per-customer-type difference in order generation; types only change art, `decay`, `tip`.

### 5.7 Thought bubble (F interface)
`AttachThought` (from `OnResume`): requires `HasMoreOrders` and a bubble object (F: the first order becomes the wanted one only inside this branch, so a customer without a bubble object is never served); detach any old one, add `VThoughtBubble` to the customer sprite with asset `bubble_single`, add one entry per order currently in the list (chocolate -> `chocolate` bubble with enabled = `VChocolateVendor::CanCreate(chocolate)`; sauce -> `sauce` enabled = `kSauce1` active; drink -> shown with the `chocolate` bubble type, enabled = `kDrink1` active), then pop the first order into the wanted slot (+0x314) and apply the mood tint. `NextBubble()` increments the bubble index; earlier bubbles are faded out and removed in `VThoughtBubble::Update` (alpha effect `transition_time` 200). `thought_offset` 5.0 and `thought_tint` 0.3 are visual. Customers show **all** their orders at once as consecutive bubbles; the active one is the first not yet consumed.

`VThoughtBubble` in detail (F, decompile): `AddOrder(item, anchor, enabled)` with `n` bubbles already there adds a leaf of the bubble's `bubble_single` asset (`AddAssetLeaf("bubble_single", "default", 0)`, sprite layer `-n`) at `(-thought_offset * n, -thought_offset * n)`, tinted (1, 0.75, 0.75) when `enabled` is false, with a fade-in of `transition_time / 2 * n + transition_time` ms; for `n > 0` also a tint effect (intensity `thought_tint * sqrt(n)`, `transition_time` ms, colour red when disabled else black). The item becomes a leaf child of that bubble, attached to the anchor named `anchor` of the bubble art (`chocolate`, `sauce`, `menu`). `SetMood(m)` switches every bubble leaf to the state `content` (2), `neutral` (3), `annoyed` (4), `angry` (5), else `happy`. `NextBubble` increments the front index. Each update, for every bubble `i` with the front index `c`: `i < c` starts a fade-out of `transition_time` ms from the leaf's current alpha when it has none, and removes the leaf once that fade is over; while `c` is less than the bubble count the front bubble's tint effect is stopped (it then ramps down), the bubbles behind get the tint intensity `thought_tint * sqrt(i - c)` and every leaf steps toward `(-offset * (i - c), -offset * (i - c))` by at most `offset * elapsed / transition_time` per axis. `Clear` removes the children, stops the effects and zeroes the index.

---

## 6. Stations, the player and the counter

### 6.1 Station basics (F)
- `VStation` fields used here: type +0x22c, machine count +0x228 / 0x244, customer slot +0x250 (null = free), player-point set +0x284, facing map +0x268, "suspended/disabled" flag +0x218, `mouse` byte +0xd9 (hit-test enable; `sprites` 2.1).
- `AttachCustomer(c)`: only for counters (type 1) with no customer and a non-null customer; sets +0x250 and `mouse` (+0xd9) := true. `DetachCustomer()`: removes the customer's menu and money child sprites from the station, `customer->SetStation(null)`, clears +0x250, `mouse` (+0xd9) := `machineCount > 0`; if that is false and the hover flag is set it dispatches a mouse-leave.
- `VStation::LoadFromTable` (F): `stationID`, `stationType` (any depth); the child table `machines` (each table entry a `VMachine`, added as a child sprite and kept by lower-cased key); `playerPoint` read as a table (alternating zone point name at odd `i` and facing number at `i+1`, default facing 2) when it is one, else as a single name; counters only: `custPoint` (own table, default `customerNN` from `stationID`) and `money` (numbers `[1]`, `[2]`); then `VObject::LoadFromTable`. `VStation::Init`: `VObject::Init`, then `SwitchAsset("tabletop", state, "default")` with `state` = `upgrade2` / `upgrade1` / `default` by the aesthetic upgrades `kStation2` / `kStation1`, then the machine count is refreshed and `mouse` := machine count > 0 or a customer is attached. A station is created as `VStation(0, type)` (name `station00`), named by its table's `name`.
- Hit testing (F: `VSpriteManager::DoHitTest`): topmost-first through the sprite list; only visible sprites whose `mouse` flag (+0xd9) is set respond (the station/machine constructors also set `clickable` +0xda, so the Click fires on mouse-up). A counter is therefore hit-testable only while it has a customer; stations with machines always are. Hover (`OnMouseEnter/Leave`) is forwarded customer <-> station by posting event ids 8 (enter), 0x10 (leave), 4 (click) to the station. `VCustomer::OnClick/OnMouseEnter/OnMouseLeave` forward to the customer's station, so clicking a customer = clicking its counter.
- Lockdown (F set by `ShopWindow`, `VTutorialManager`): `VSpriteManager::SetLockDown(bool)` sets a byte at +0xec of the manager; it makes `IsValidSprite` demand membership of the exclusive set, which gates hover, mouse-down, mouse-up/click and hit-test candidates (2.3).

### 6.2 Click handling (F)
1. `ShopWindow::OnMouseDown`: game-state `++TotalClicks` then `VSpriteWindow::OnMouseDown` -> `VSpriteManager::OnMouseDown` (the hovered sprite becomes the pressed sprite and gets the Down event; the Click is fired on the mouse-up, `sprites` 2.5).
2. `VStation::OnClick`: if a player exists and the station has player points: choose the **player point closest (Euclidean) to the player's current position** (single point = that point), call the player's vtable slot +0x13c = **`VCharacter::QueueMovePoint(point)`** (position is read through slot +0x94 = `VSprite::GetPosition`), then game-state `++TotalValidClicks`. The click only appends to the player's move queue; a click while the player walks does not replace the trip, and the queue is never cleared (`sprites` 5.2). Machine sub-buttons (`VStation::OnMachineClick(name)`) append the button name to a pending list (+0x258) and post a click event.
3. `VPlayer::TravelTo` (F; reached when `VCharacter::Update`/`AtDestination` pops the queue via `GotoNextPoint`): stores the destination zone point; if the center table exists routes via `ZoneController::AddPath(from current point to destination)` and starts moving; else moves directly. If the player already stands exactly on that point, `AtDestination` (and so `VStation::OnArrival`) runs immediately inside the same call (`sprites` 5.1).
4. On arrival `VPlayer::AtDestination` -> `ShopWindow::GetStationByZonePoint(point)` (the station whose player-point set contains it) -> `VStation::OnArrival(player, point)`:
   - unless the station is suspended (+0x218; a machine suspends its station from `Activate` to its `Unlock`, so an arrival while a machine works does nothing, machines 4.5): if it has one machine (+0x248) call that machine's arrival handler; else if it has several, call the arrival handler of **each button name queued in the pending list** (only when the station has more than one machine); then if a customer is attached call `VCustomer::OnArrival(player)` (section 5.2); then notify tutorial listeners.
   - clear the pending-click list; if the player's move queue is empty (`IsPathEmpty`), set its facing from the point's facing entry (`kFront`=2 if no point; a point with no stored facing gives 0, left).
So taking an order, serving and collecting money are all the same action: walk to the counter. Which of the three happens depends only on the customer's state at that moment.

### 6.3 Counters and "the register"
There is no register object. The counter shows the customer's menu bubble, then order bubbles, and, after the customer leaves, the pile of `dollar` sprites. Payment is collected only when the player arrives (state 6 -> `OrderComplete`, 7.1). The money pile's pixel position per counter is the `money` key of `stationNN` (e.g. station01 {128,293}, station06 {553,222} in iquitos).

### 6.4 Delivery (zeppelin) station (F, brief; owned by the machines analyst)
`VDelivery` timer: `Reset` sets the next request in `delivery_interval` (30000, -0.5 with `kDelivery2`) + a random offset in `[-variance/2, variance/2)` (`delivery_variance` 5000). When it elapses (and `LevelIsComplete()` is false) it requests a chocolate (next `<zeppelin>` order from the level, or random), shows a bubble, patience 100 draining 1 per second with the same mood thresholds; at 0 the request is cancelled and the timer is reset. Delivering a matching chocolate: `zeppelin_served_this_level += 1`, `OrderComplete(chocolate.GetCost(), (622,246), null customer)` (no tip), then `AddCombo(4)` via `OrderComplete`. `VDelivery::Update` calls `ShopWindow::WaitOnZeppelin(true)` while a request or zeppelin is in flight, which blocks the level-end stamp (2.3).

---

## 7. Scoring

### 7.1 Payment (`ShopWindow::OrderComplete(price, pos, customer)`) (F)
```
sale = salemodifier * price                       (VValueManager::GetProduct)
tip  = customer ? tipmodifier * (customer.patience / 100 * customer.tip) : 0
amount = sale + tip                               (floats, not rounded here)
current_level_total += amount                     (VValueManager::AddToValue)
endless only: current_worth += amount; endless_level_counter += amount
```
`price` is the customer's accumulated `+0x2a4`: sum of `GetCost()` for chocolates (10 per layer + quality bonus), 15 per sauce, 15 per drink. Patience is frozen once the customer is in state >= 4 (no decay there), so the tip uses the patience at the moment the last item was served (it can exceed 100% because of the +20 bonuses; there is no cap). A floating `VPaymentText` shows `"$%0.2i"` of `floor(amount)` at the pile. After payment: goal popups (below), then `AddCombo(4)`.
Goal popups (story only, only if the active-text layer exists): if `0x1d8 < 2` and `current_level_total >= mastery` -> `VGoalText(2)`, play `audio/sfx/master.ogg`, `0x1d8 = 2`, set value `mastery_this_level` = 1; else if `0x1d8 < 1` and `total >= goal` -> `VGoalText(1)`, `audio/sfx/goal.ogg`, `0x1d8 = 1`. Mastery takes precedence, and the popups fire once each. The same check runs after each combo bonus (7.3).

### 7.2 Result of a level (`VLevelManager::LevelComplete(score, &delta)`) (F)
- `result = 2` if `score >= mastery`; else `1` if `score >= goal`; else `0` (strict `<` comparisons: equal to the threshold passes). `goal`/`mastery` are the `goals.xml` ints of the current level. If there is no level object returns 0 without touching `delta`.
- `delta` (money credited): if the level had status 0 before: `delta = score`; else `delta = max(0, score - bestScore)` where `bestScore` is the stored best for this level **before** this attempt. (So replaying a passed level pays only the improvement.)
- Stored status is raised only if the new result is greater than the stored one. Then `UpdateBestScore(level, score)`: best score updated if `score > best` (also for failed runs; a later first pass still pays the whole score because status was 0).
- Status values: 0 not passed, 1 goal reached, 2 mastered ("expert").
- `SetHighestComplete(n, force)`: unless `force` it only raises `highest_level`; it then loops levels 1..max(60): levels <= n with status 0 become 1, levels > n with a non-zero status are reset to 0 (so the cheat can lower progress). Status bytes persist as binary block `level_status` (60 bytes), best scores as `level_best_score` (60 ints), per-location cash as `level_score` (6 ints).

### 7.3 Combos / chains (`ShopWindow::AddCombo(type)`) (F)
Types: 1 = menu (customer reaches the menu state, from `VCustomer::OnArrival`), 2 = service (successful `ServeCustomer`), 4 = payout (`OrderComplete`, which includes zeppelin deliveries).
- State: `lastType` (0x1bc), `counter` (0x1c0), both 0 at `LevelSetup`.
- If `type != lastType`: `lastType = type; counter = 0;` **return (no bonus)**.
- Else `counter += 1`; `bonus = <type's combobonus value> * counter` with value names `menu_combobonus` (type 1), `service_combobonus` (2), `payout_combobonus` (4), each 3.0. So with three same-type events in a row the bonuses are 3, 6 (the first event of a run gives nothing).
- `current_level_total += bonus` (endless also `current_worth` and `endless_level_counter`). A `VComboText` is spawned (text `"<label> x<counter+1>\n+$<bonus>"`, labels `combo_menu`, `combo_service`, `combo_payment` from the string table) at the player's position (400,300 if no player), (x, y-75).
- Goal/mastery popups as in 7.1.
- Metrics: max chain per type kept in values `menu_multiplier_this_level`, `service_multiplier_this_level`, `payment_multiplier_this_level` (= counter+1 when greater); points earned are accumulated in game-state strings `MenuDeliveryChainPointsEarned`, `ServiceChainPointsEarned`, `CashPickupChainPointsEarned`.
- Because the chain is global and only broken by a different type, interleaving events breaks it: e.g. serve, serve (bonus on the second), then any customer arrival or any payout resets the counter. This is intended (I) as "serve several customers before collecting money".

### 7.4 Money and persistence (F)
- `current_level_total`: score of the level in progress (float). HUD, goal check, `DoEndLevel` all use it. Reset by `Reload`.
- `current_worth`: the player's **spendable cash for the current location** in story. At `LevelSetup` it is set from `VLevelManager::GetCurrentScore()` = `level_score[(currentLevel-1)/10]` (one slot per location). Upgrade purchases subtract it and call `UpdateCurrentScore`; quest rewards add to it (`VQuest`); `DoEndLevel` adds `delta` then writes it back. Cash does not carry between locations (a new location starts with its own slot). In endless it is the run's score instead.
- `total_worth`: lifetime earnings, saved, used for the high-score board and map display; incremented by the same `delta`.
- Achievement counters (saved): `num_served` (full serves, story), `num_sauce_sold`, `num_angry` (story), `completed_<loc>`, `num_levels_mastered`, `endless_time`; others (`num_bars`, `num_truffles`, `num_infusions`, `num_deliveries`, `num_disposed`) are incremented by machines.
- Cheats (`ShopWindow::DoCheat`, Lua `DoCheatGame(type, param)`; `scripts/cheats.lua`'s `DoCheat(type, param)` calls it when the dialog's `context` is `game` and `VWorldMap::DoCheat` (`DoCheatMap`) when it is `map`; the `game_cheats` buttons of `game.lua` and `worldmap.lua` are enabled by `IsEnabled(kCheatMode)`; both natives check nothing themselves) (F). `param` is read with `to_int`. 0 fail: story, `current_level_total` := min(total, goal - 1); endless, angry_this_level += 10 and served_this_level := 5 (neither saved); 1 pass: total := max(goal, min(total, mastery - 1)); 2 master: total := max(total, mastery); 0, 1 and 2 then call `DoEndLevel` and answer `""`. 3 add money: for `n > 0` `current_level_total += n` (saved), and in story also the location's cash (`UpdateCurrentScore(GetCurrentScore() + n)`), `current_worth += n`, `total_worth += n`; the answer is `"Added %i to current and lifetime total"` whether or not `n` was positive (an endless run's score, `current_worth`, is not touched). 4 go to level n (1..60): `SetCurrentLevel(n)`, runs `DoModal('scripts/loading.lua');` through the command queue, answers `""`; 5 set the highest level complete (1..60): `SetHighestComplete(n, true)`, answer `"Set highest level complete to %i"`; a level outside 1..60 answers `"Invalid Level"`, an unknown type `"Invalid Cheat"`. `VWorldMap::DoCheat`: 3 adds `n` (> 0, else `"No money added"`) to the first open pin's location cash and to `total_worth` (answer `"Added %i to location %i and lifetime total"`, the second `%i` is not passed to the formatter; no open pin answers `"No location selected"`), 4 sets the current level and runs `CloseWorldMap();`, 5 as above plus `ResetMap`, 0..2 answer `""` and do nothing. `ShopWindow::Debug` (`DoDebug`, button `game_debug`, enabled by config `game_debug` = 1) reloads `zones/zones.xml` into the zone controller. Console commands (`DoCommand`, Ctrl+C, `scripts/console.lua`): `choctint`, `setvalue name value`, `setpos`, `setlayer`, `setvisible`, `getupgrade`, `setdelay`, `setscale`, `addminutes n` (adds n*60000 ms to the timer's elapsed), `closeconsole`.

---

## 8. HUD (`VShopHud` + `hud.lua` + `VRegister` + `RTimer`) (F)

Elements of `ChocolateHud`: `Menu` button (opens `pause.lua`), `progress_gauge` (a 3-layer `BLayeredWindow`: layer 0 `progress_goal` + `goal_score`, layer 1 `progress_master` + `mastery_score`, layer 2 `progress_anger` + `anger_score` for endless), `dialPower` (`KDial` needle, rotation range -1.5..1.5, starting at 0), `timer_sweep` (`KSweepDial`), `level_name` text, `game_timer` (`RTimer`), the `VRegister` named `register_window` (7 digits) with a "$" label, and the `extension` bitmap with the `Upgrade` button (opens `prelevel.lua`).
`VShopHud::LevelInit` (called by `LevelSetup`): `RTimer::Reset`; timer incrementing flag := endless; story: timer limit = level minutes*60000 + seconds*1000 ms (also cached in the hud at +0x50); `RTimer::SetText`; sweep dial := 1.0; needle := 0; story: `goal_score` text := `"$%i"` of `GetCurrentGoal()` and `mastery_score` := `"$%i"` of `GetCurrentMasteryGoal()`, and `SetFlags(0)` on the `extension` window, which **hides it** (F: `TWindow::SetFlags` keeps only the `0xff00` bits of the argument, so 0 clears the 0x100 visible bit that `TWindow::DoDraw` and the child hit test require; descendants of a hidden window are neither drawn nor hit). `extension` carries the Upgrade button, so in story mode the HUD Upgrade button is invisible and unusable for the whole level; nothing in `VShopHud` ever shows the window again, and endless mode never hides it; endless: `anger_score` := `"0/<angry_customer_limit>"`; `level_name` := `<level_abbreviation string>  <current level>` (format `"%s  %i"`). In endless (`TSettings.gameMode != 0`) `PostChildrenInit` also selects gauge layer 2, calls `SetFlags(0)` on `level_name` (hidden, same mechanism: the "Lvl N" label is not shown in endless mode, although `LevelInit` still writes its text) and moves the timer up 10 px.
`VShopHud::UpdateHud` every shop tick (F):
- Timer run/stop: if no tutorial is in effect and the sprite manager is not soft-paused, set the timer running (and stamp the start time if it was stopped); otherwise stop it. (`RTimer` accumulates real clock deltas only while running.)
- Sweep dial (story): `1 - elapsed/total`, only written when different. Endless: 1.0.
- Story gauge: layer = 1 if `total >= goal` else 0. Needle: `total/goal` while below goal; after goal `(total-goal)/(mastery-goal)` (can exceed 1). The needle uses `current_level_total` as a float.
- Endless: needle = `angry_this_level / angry_customer_limit`; `anger_score` text `"%i/%i"`.
`RTimer::SetText` (F): story shows the remaining time `max(0, limit - elapsed)` as `"%d:%.2d"` (minutes : seconds, integer division by 60000 and 1000); endless shows elapsed with the same format. It refreshes every `OnTaskAnimate` tick of the RTimer while running.
`VRegister::OnTaskAnimate` (F; `str::compare` here is strcmp-like, 0 = equal, confirmed from `VCustomerVendor::LoadPosseFromTable` usage): the shown integer is computed from the managed values as follows, keyed on `TSettings+0x10` (game mode) and the register's `context` string:
- endless (mode != 0): `current_level_total` for every context (in endless that is the run score).
- story, `context="endlevel"`: `current_worth` only (cash after `DoEndLevel` already credited the level).
- story, `context="prelevel"`: `current_worth + current_level_total`.
- story, any other context (the HUD register has none): `current_level_total`.
Digits are `VDigit` sprites, one per decimal place (`numDigits` 7, laid out right to left from the register's width, `digit_1`, `digit_10`, ...), a digit is visible if its place is 0 or `value >= 10^place`, and each shows `(value % 10^(i+1) - value % 10^i) / 10^i`; recomputed only when the integer value changes (start value -1 forces a first draw).
Floating texts (`RActiveTextLayer`; F: `VPaymentText`, `VComboText`, `VGoalText` and their constructors): each is a text of font `"edwardian_medium.mvec"` (F: the literal has no `fonts/` folder, so the file is not found, `fonts/default.mvec` is not shipped, and `TFontData::GetMvec` gives the font embedded in the executable, a 468-glyph sans; the shipped `fonts/edwardian_medium.mvec` is not used here, and its `+` is a missing-glyph box) centred on its position in a box of its own size, wrapped `<outline color='000000' size='2'><b>...</b></outline>` (the goal's outline is `ffffff`). `VPaymentText`: `"$%0.2i"` of `floor(amount)` (two digits: `$09`), 25 pt, white, 150 x 30. `VComboText`: `"<label> x<chain>\n+$<bonus>"`, 18 pt, yellow (1, 1, 0), 150 x 36. `VGoalText(1 or 2)`: string `reached_goal` / `reached_master`, 46 pt, 800 x 50, at (400, 250), colour green (0.254, 0.699, 0.195) / gold (0.8125, 0.723, 0.109), alpha 0 to start. Each tick adds the layer's delta to an elapsed counter `t` (ms) and: payment: `t < 501` the text sits at its start and rises `75 t / 500` px (truncated) while its alpha is `t / 500`, 501 to 1000 nothing changes, from 1001 the alpha is `(1000 - t) / 500 + 1` (not below 0) and the text rises one more pixel per update, gone from 1501; combo: the same with the hold up to 1500, the fade `(1500 - t) / 500 + 1` and gone from 2001; goal: rise 100 px, alpha `t / 500`, hold up to 1700, alpha `(2000 - t) / 500` from 1701 (it steps from about 1 to 0.6 there), gone from 2001. A new text is drawn at its start place with alpha 1 (alpha 0 for the goal) until its first update.

---

## 9. Data quirks worth knowing
- F: `levels/level28.xml` is malformed XML in both builds: two `<order>`s are never closed, the yachtsman's second (customer index 27) and the last customer's second (index 29), so each following `<station>` ends up nested in the order. The SDK parser is hand-rolled (`TXmlNode::Parse`, `ParseTag`, `pfGetToken`) and `ParseTag` does not compare end-tag names: any `</x>` ends the innermost open node. So `</customer>` closes the yachtsman's open order, the customer node stays open, and the later customers nest under it; open nodes at the end of input are left. `VLevel::LoadLevel` walks the root's direct children only. I: the level therefore loads 28 customers, the yachtsman with both orders and no `station`, and customers 28 and 29 are lost (the `TGetChar` flags at +4, +0x10, +0x11 were not traced).
- F: levels 29, 30, 49, 50 contain customers with `start` 135 (> 100 percent). `PercentElapsed` never exceeds 100, so these customers never spawn (they stay in the pending list). The level still ends normally by time. Levels 26/27/46-48 have starts of exactly 100, which do spawn when the timer percent hits 100, i.e. at or after the time is up, but spawning requires `elapsed < total`, so **start = 100 never spawns either** (both conditions can't hold: percent reaches 100 only at elapsed >= total). (I derived from the two F conditions.)
- F: level 3 is the only `<end>` user; harmless.
- F: levels 1-3 never show the prelevel window (`CSLevel` constructor sets "needs prelevel" = `level > 3`); plot dialogs still run from the flag path (2.1 step 4).
- F: level durations (`minutes:seconds`): iquitos 0:30, 1:30, 2:00, 2:30, 3:00, 3:00, 3:30, 4:00 x3; grytviken 2:00 up to 5:00; namche 2:30-5:00; krakatoa 3:00-6:00; kamchatka 3:00-5:00; gaberoun 3:30-6:00 (levels 51-60 also have `chocolatedecay`/`customerdecay`). Customers per level: 8 (L1) up to 54 (L59-60); zeppelin orders in levels 14-60.
- F: `drink_patience_bonus` and `complimentary_pickup_prob` are inert (section 3.3).

---

## 10. Random numbers and determinism
- F: every gameplay random draw uses `TPlatform::Rand()` = `TRandom::Rand32()`, a standard **MT19937** (the same generator documented for Chocolatier 3, `docs/re/trade-and-save.md`) returning raw 32-bit words; callers reduce with `% n` (bias included) or multiply by 2^-32. Lua's `random(n)` is `Rand32 % n + 1`; `rem(a,b)` is floor-mod.
- F: seeding: `TPlatform` constructor builds a 4-word array from libc `rand()` (three calls per word, `a*b*8 + (c>>3)`) and calls `TRandom::SeedArray`; libc `rand()` is seeded by an `srand(...)` call elsewhere in start-up (the argument is not shown; I: time based). So native runs are not reproducible. For deterministic OpenConche tests inject the RNG (a seam with `next32()`), do not assume the original sequence.
- F draw sites (each is one `Rand32` call): customer-station pick (`% candidates`), `GetRandomCustomer` (`% (posse+locals)`), endless spawn jitter (`% variance`), `GenerateOrders` (multi-order roll `% 100` or `/2^32`; sauce/drink roll `% 6`, flavour `& 1`), chocolate generation (`GetChocolate`, `GetRandomLayer`, `GetDisplayInfluence`: `% 101` then `% optionCount`), money sprites (`& 3`, `& 1`, `& 1`), idle cooldown jitter, delivery interval jitter.

---

## 11. Lua natives touched by these scripts (F)
| Global | Native | Registered by | Notes |
|---|---|---|---|
| `LevelSetup()` | `ShopWindow::LevelSetup` | `ShopWindow` ctor | blocking rebuild, 2.2 |
| `ReadyForPrelevel()` | `ShopWindow::ReadyForPrelevel` | `ShopWindow` ctor | sets 0x1de |
| `GetEndlevelText(status)` -> str | `ShopWindow::GetEndlevelText` | ctor | 2.8 |
| `DoMetrics(int, bool)` | `ShopWindow::DoMetrics` | ctor | analytics only (`InitMetrics`/`DoMetrics` write `TGameState` keys: `LevelNumber`, `TotalCustomers`, `TotalCustomersLost`, `OrderCompleted`, `*ChainPointsEarned`, `TotalClicks`, `TotalValidClicks`, `PlayTime_<n>`, ...) |
| `DoCheatGame(type, str)` -> str, `DoCommand(str)`, `DoDebug()` | `ShopWindow` | ctor | dev only; `Debug` re-inits zones from `zones/zones.xml` |
| `NeedPrelevel()`, `NeedPlot()` -> bool | `VLevel` | **each `VLevel` constructor re-registers them bound to that level object** | story levels only |
| `GetGoalScore(level)`, `GetMasteryScore(level)` -> int | `VLevelManager` | ctor | from goals.xml |
| `GetCurrentResult()`, `GetHighestComplete()`, `GetCurrentLevel()`, `SetCurrentLevel(n)` | `VLevelManager` | ctor | `current_level`/`highest_level` values; `GetCurrentResult()` = stored status of the current level |
| `GetCurrentGameMode()`, `SetCurrentGameMode(n)`, `GetCurrentDifficulty()`, `SetCurrentDifficulty(n)`, `InsideGame(b)`, `AreInsideGame()`, `IsGameLoaded()`, `SetQuitting(b)`, `AreQuitting()`, `QuitToMainMenu()`, `random(n)`, `rem(a,b)` | `TSettings` | `TSettings` ctor | 2.1, 2.5, 10 |
| `PauseSounds(bool, group)` | `VSoundManager::PauseSounds` | ctor | pauses/resumes a sound group (`kSFXSoundGroup`) |
| `GetManagedInt/Real/Uint(name, default)`, `SetManagedValue(name, value, save)` | `VValueManager` | ctor | exposes the value table to Lua |
| `ShowNewAchievements(n)`, `HideNotification()` | `VAchievementManager` | ctor | not simulation |

---

## 12. Unknown / not determined
- U: the `TGetChar` flags at +4, +0x10 and +0x11 in `TXmlNode`'s tokenizer, which the 28-customer result for `level28.xml` (section 9) assumes do not change the tree.
- U: exact float rounding of difficulty-modified values (x87 vs SSE) beyond the rounded numbers given.
- U: whether `VCustomer::SetActive`'s initial-patience argument is ever not 100.0 (the two call sites in `ShopWindow` both pass 100.0, F; no other caller found).
- U: the start-of-level race (1000 ms countdown vs `ReadyForPrelevel`) if the loading fade were ever shorter than 1 s of shop-clock time (2.3).
- U: `DoMetrics` details (analytics only).
- U: `RScorer`/`RScore` internals: they are SDK-library leftovers not instantiated by the shipped scripts; only their symbol shapes were seen (`GetTotalScore`, `GetScore`, `UpdateScoreIncrement`).

---

## Choices where the code does not say (I)

* `mAnimationsRunning` (the idle animations playing in the shop, at most four) is a process-wide static in the original; here it is one too (a plain counter that a customer's destructor gives back to), reset by the tests.
* The customer prototypes are built with no vendor (as `LoadPosseFromTable` does) and each customer made from one is given its own bubble, menu, money and orderables; a customer's `name` key (own table) is the prototype's key in the posse or locals, so a table whose `name` is missing is keyed by the empty string.
* A customer's sprite name is the level data's `<name>` when it has one, else the generated `object_customer<N>`; the tutorials look customers up by it (`tut_cust_1` .. `tut_cust_4`).
* The `idle_angry` sheet of the yachtsman names the image `idle_angry.png`, which neither install ships (the file is `idle_angry.jpg`); the sprite does not load and logs one `[gfx] texture 'idle_angry.png' not found`; nothing else is affected. The posse table carries `leaf = true` at its top level, so a posse customer is a leaf (its clickable parts hand the click on to the customer).
* Level 1 with Tutorials off: customers arrive at 0, 1.2, 2.4 (blocked), 4.5, 7.5 s of level time and so on, and nothing is locked after the pre-level second (the lock is forced off every tick). With Tutorials on, `tut_cust_1` arrives on the first tick of the level and starts `tutorial_1` at once; the first three seconds of level time are locked only in the gaps between tutorials (none arises in level 1: `tutorial_2` and `tutorial_3` start at their customers' arrivals the moment the previous tutorial ends).
* A tutorial in effect stops the level's clock (`VShopHud::UpdateHud`), and the soft pause of a step stops the arrivals; a customer whose time comes while a step pauses arrives on the first tick after it. So the level-1 clock reads 0 through `tutorial_1`, 1200 ms through `tutorial_2` and 2400 ms through `tutorial_3`.
* The tutorial trigger names (`customer_arrival`, `customer_order`, `endless_start`) are passed with the customer's sprite name (or empty); a tutorial whose `trigger_object` is empty takes any object. `modes = {0}` at the top of `game_tutorials` is a table, so `VTutorialManager::LoadFromTable` makes a tutorial of it too (named `modes`, no steps, no trigger: nothing ever checks it).
* A step's panel text is laid out in the text area (`w - (m1+m3)` by `h - (m2+m4)`: 120 x 100 for a 200 x 185 panel in `tutorial_text_style`) but drawn with the sprite's full `h` as the clip (`VTextSprite::Draw`, docs/re/greatcc-sprites-and-zones.md 7.3). The text is vertically centred (flags 5), so a long text runs past the area's bottom into the margin and still draws: `tutorial_1_24` (tutorial 3, step 3) and `tutorial_1_22` (tutorial 3, step 14) show every line.
* The original's `std::map<int, shared_ptr<VTutorialStep>>` gains a null entry whenever `GetCurrentStep` is asked for a step that is not there (step 0 at the start, the step past the end), so `GetNumSteps` (the map's size) grows; only the skip branch of `AdvanceStep` reads it (`step = GetNumSteps() + 1`), and any number past the last step does the same, so the count here is the real one.
* `VTutorial::AdvanceStep` of a tutorial that is already complete is ignored (the original would count the step up again and write state 3 again, with no visible change). A tutorial that is added to a window while a window that is still fading it out holds it leaves that window first (the original leaves the old window's manager holding it).
* Before a tutorial step is in a sprite manager its clock is the top modal's (the sprite constructor takes it in the original); the step's begin and request times are read from it.
* Fix F4: with it on, tutorial `x` keeps its state under `tutorial.x`; with it off every tutorial reads and writes `tutorial.shared`, which the last writer's value decides (a tutorial that starts writes 1 over another's 3).
* `VTutorialManager::SetCurrentLevel` takes the level's number (the number is what `InRange` reads); the tutorials' `TutorialTime_<PLAY_COUNTER>` states use a counter that `InitMetrics` increments once per level load.
* The pre-level second and the customers: a customer that is created before its station exists in the scene (a level whose `objects` table is missing) has no bubble, no menu and no money; it can arrive and leave but not be served.

* `ShopWindow::PostChildrenInit` calls `TWindow::SetWindowDepth(textLayer, this)`: the floating-text layer, a sibling made before the shop window, is taken to move in front of it (the texts draw over the sprites and the HUD).
* `VQuestWindow::LevelInit` hides the quest panel while no quest is active (*meta* 3: the panel is shown only when the active set is non-empty); until the quest manager exists the shop hides it at each level load.
* `<upgrade>` in a level file stores two attributes as a pair; the attribute names are not visible in the decompile and no shipped level has one. They are read as `name` and `category`.
* The tag names of a level file are compared as written (lowercase everywhere in the shipped files); a customer's child tags are lower-cased first.
* A level file that does not parse leaves the constructor's defaults: no length, no customers, `needPrelevel` for levels above 3.
* `str::to_int` and `str::to_float` over element text are read as `atoi` and `atof`.
* `VAssetManager::Preload` calls (`VBackground::LoadFromTable`, `VStation::LoadFromTable`, the customer vendor) only warm a cache and are not reproduced.
* The first level load makes the zone graph from `zones/zones.xml`; `HasCenterTable` reads station16's visible flag. `VDisplay::Init` shows the case while `kDisplay1` is active (from level 2 in Iquitos), so level 1 walks straight and level 2 on routes round the table through the zone graph (e.g. `station01 -> station16_north -> station08`).
* The upgrade manager answers `IsActive` for the machines, the stations, the backdrop and the vendor (*machines* 7): the location's file is read at each level load, the player's purchases are loaded, the level's grants run, and the sprites the upgrades are bound to are looked up after the location's content is loaded. `VStation::Init` shows the tabletop's `upgrade2`/`upgrade1` state for `kStation2`/`kStation1`; `VBackground::Init` the backdrop's, and a static is shown while its upgrade is on.
* The pending-customer list holds the level's customers whose type the vendor's posse or locals know (their names are read from the tables); with no customer sprites yet nothing arrives, so a story level runs to its time limit.

## 13. Implementation notes for OpenConche
- Engine fit: GREATCC is a new game module like `src/games/choc1..3`. The shop is a `VSpriteWindow` subclass; the existing `src/sdk/Sprite.*`/`SpriteScript.*`/`Ticker.h` layer is the nearest base for `VSprite`/animation clocks. The Lua shell (`game.lua`, `hud.lua`, `endlevel.lua`, ...) runs unchanged if the natives in section 11 plus the window classes (`ShopWindow`, `VShopHud`, `VRegister`, `RTimer`, `BLayeredWindow`, `KDial`, `KSweepDial`, `RShrinkText`, `RActiveTextLayer`, `VPrelevelWindow`, `VUpgradeList`) are provided. `TSettings` (mode, difficulty, insideGame, gameLoaded, quitting) and `VValueManager` (managed values incl. modifiers and saved flag) are mandatory dependencies; `docs/re/trade-and-save.md` and `docs/re/api-globals.md` describe the Chocolatier equivalents.
- Suggested module split: `LevelManager` (goals, status, best score, location cash; pure data, fully unit-testable with the formulas in 7.2/7.4), `CustomerSim` (state machine in 5.2/5.3 with an injected clock and RNG), `ShopTick` (the ordered phases in 2.3), `ComboChain` (7.3), `OrderResolver` (4; depends on the chocolate vendor), `Hud` (section 8).
- Determinism seam: replace `TPlatform::Rand()` with an injectable `uint32 next32()`; use the exact reductions listed in section 10 so tests can script outcomes. Time: the game uses per-modal clocks in milliseconds; model `elapsed` as accumulated frame deltas (cap each customer's single delta to < 3000 ms, as `VSprite::GetElapsed` does).
- Test hooks that follow directly from F facts: percent-of-time spawn (`start` is percent of `minutes*60000+seconds*1000`, clamped at 100, spawn gate `elapsed < total`); station availability and fallback; patience decay `100/decay` per ms through the 3-state window; mood thresholds 80/60/40/20/0; the 2000 ms end delay; level result strict-compare and `delta` rule; combo chain first-event-no-bonus; `GetCost` = 10/layer (+1 per owned quality upgrade); payout floor for the text only (the total stays a float; `GetInt` floors when displayed or compared).
- Mac and Windows data agree; read XML from the plain Mac files (`levels/*.xml`, `values.xml`), the engine's ZXML reader for the Windows build.
