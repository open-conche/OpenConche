# Coffee factory, kitchen, recipe designer, sign editor, curve editor

Sources, in trust order: the decompiled Mac i386 binary (`re/mac/decomp/`, Ghidra addresses; binary at the Chocolatier 3 Mac app bundle's `Contents/MacOS/chocolatier-decadence-design`), the Chocolatier 3 Mac app bundle's `Contents/Frameworks/*.h`, then the Lua in the original assets' `` (original) and the Reforged mod's `assets/`. Machine-readable tables (natives, DSL keys, messages, Lua touch points) are in `docs/re/coffee-factory-and-recipes.tsv`.

Companion docs. Read first, not repeated here: `api-globals.md`, `api-windows.md`, `window-system.md`, `ledger-and-game-natives.md` (section 10.3 already has the `RecipeDesigner` natives and 10.5 `BTSetTint`/`SetRectangleColor`; section 10.4 the `SignEditor` setters). The chocolate side (`ChocolateFactory`, `Recycler`, `FlyingRing`, `Glitz`, particles) is another analyst's file `chocolate-factory.md`. Pieces shared with it are described here only where the coffee side differs: `IngredientQueue`/`IngredientQueueTask`, `FlyingIngredient`, `Ingredient`, `TScalingTask`.

Conventions: every number is a fact from decompile or data unless marked **(inference)**. Times are milliseconds. "Float constants" are IEEE single or double values decoded from the immediates in the decompile (Ghidra shows the high word of doubles, e.g. `0x40a77000` = 3000.0). Offsets such as `+0xf4` are object field offsets used to identify fields.

---

## 1. CoffeeFactory

### 1.1 Composition

`CoffeeFactory : TBSGWindow`, window type name `CoffeeFactory` (DSL constructor). It is a pure-C++ game. It owns a root `TSprite` (`pBaseSprite`, layer 0, no texture) and all the game objects hang off that root; the window itself draws that tree in `Draw`.

| Part | Class | Field | Notes |
|---|---|---|---|
| Ingredient conveyor | `IngredientQueue` + `IngredientQueueTask` | +0x74 / +0x7c | shared with chocolate, coffee uses colour count 1 |
| Gun | `CoffeeGunTask`, `TAnimatedSprite` `image/gun.xml`, static `image/gun_base` | +0x80, +0x84 | |
| Grid | `CoffeeIngredientGrid` (a `TSprite`) + `CoffeeGridTask` | +0x98 / +0x94 | |
| Hopper | `CoffeeHopper` (a `TSprite`) + `HopperCollectionTask` | +0xa0 / +0xa8 | |
| Case belt | `CoffeeProductQueueTask` holding `CoffeeProduct` sprites | +0xac | |
| Bonus popups | `CoffeeBonus` list | +0xbc | |
| Clock | sprite `image/timer_face` with child `image/timer_hand` | +0x108 / +0x110 | |
| Refill | `CoffeeRefillTask` | not built | header has it commented out; the Mac binary has only the symbol `BeginRefill`, no body |

State fields: `state` +0x64, `gamePaused` +0xe0, `warnOnMiss` +0xe1, `countdownTime` +0xe4, `gameTime` +0xe8, `warnTime` +0xec, `clock` +0xf0, `score` +0xf4, `bonusScore` +0xf8, `pTime` +0xfc, `pCountdown` +0x100, `pScore` +0x104, `missCount` +0xc4, `matchBonus` vector +0xb0, `speedUpFactor` +0x8c, `maxSpeed` +0x90.

States (enum `GameState`): `GAME_INIT`=0, `GAME_COUNTDOWN`=1, `GAME_PLAYING`=2, `GAME_ENDING`=3, `GAME_ABORTED`=4, `GAME_OVER`=5. The class keeps a static `pInstance` (set by the constructor, cleared by the destructor).

### 1.2 Constructor (0x11e3a)

Seeds nothing; creates the root sprite, a `TSoundHandler`, sets `state=0`, then registers four natives in the Lua global table through `lua_pushdirectclosure` (bound to this object): `StartGame()`, `PauseGame(str)`, `ResumeGame()`, `GetScore()`. The destructor does **not** nil them (unlike `RecipeDesigner` and `SignEditor`), so the globals stay bound to the last factory window made. Because the chocolate window registers the same names, the most recently constructed factory window wins.

### 1.3 DSL keys read by `Init` (0x123aa) and defaults

`Init` first calls `TWindow::Init`, seeds the random generator with `TPlatform::Timer()`, sets this window as the default focus of its parent modal, then reads keys. A number key is read as a double and (for integer fields) clamped to [0, 4294967295] and truncated.

| Key | Default | Meaning / use |
|---|---|---|
| `warnOnMiss` | false | tutorial hint: pause with text `coffee_first_missed` after the first miss |
| `matchBonus` | none | table; native vector = `{0, t[1], t[2], ...}` |
| `countdowntime` | 3000 | countdown length |
| `gametime` | 60000 | play length |
| `warntime` | 8000 | whistle when remaining time crosses it |
| `productName`, `productCode` | "" | stored only |
| `recipe` | none | array of ingredient names, duplicates allowed (a recipe `{coffee, coffee, cream, sugar}` has four entries) |
| `conveyorpath` | none | `TCurve` for the ingredient belt (array of `{x,y}`) |
| `conveyorcount` | 5 | ingredients on the belt |
| `conveyortime` | 1200 | ms for a belt step (see 1.8); **(inference)** the decompiler dropped the constructor argument, but the value is read immediately before `IngredientQueueTask(queue, float)` |
| `colorcount` | 1 | passed to `IngredientQueue`; 1 means no colour rings |
| `productpath` | none | `TCurve` for finished cases |
| `producttime` | 3000 | ms for a case to travel its path |
| `productspacing` | 1000 | ms; divided by `producttime` by the task constructor |
| `appearance` | none | table, see section 3 |
| `top_hood`, `bottom_hood` | (0,0) | `{x,y}` of the two mask sprites |
| `speedup` | 1.1 | grid speed multiplier per match |
| `maxspeed` | 8 | cap on the grid speed multiplier |
| `columns`, `rows` | 9, 9 | grid size (Lua passes 6 or 8, and 9) |
| `gridx`, `gridy` | 10, 10 | grid origin in window pixels (Lua 146 or 103, and 15) |
| `griddx`, `griddy` | 50, 50 | cell width/height (Lua 45, 45) |
| `gridspeed` | 3000 | ms per row scroll at scale 1 **(inference on flow, as conveyortime)** |
| `gunspeed` | 250 | gun flight time for 1000 px |
| `missed` | none | array of `{x,y}` park points; table length = miss limit |
| `autoFillGunPos` | (0,0) | read into a local and dropped. `autoFillRate`, `autoFillCount`, `categoryName` are never read (no such strings in the binary) |

Facts about what the original Lua actually passes (the original assets' `ui/coffee_factory.lua`, identical in Reforged apart from comments): `speedup` and `maxspeed` are **not passed**, so the category values `speedup = 1.2` (beverage) and `1.15` (blend) in `items/categories.lua` are dead; the effective values are 1.1 and 8. `gridspeed` = product override or category (beverage 5000, blend 6000). `columns` = product, category or 6 (blend has 8). `rows` = 9, `griddy` = `griddx` = 45. `gridx` = 146 (6 columns) or 103 (8). `matchBonus` = `{0,0,0,1,2,2,3,3,4,5}` for both coffee categories. `gunspeed` = 250, `countdowntime` 3000, `gametime` 60000, `warntime` 8000. Category `productspacing`: original 1500 (ratio 0.5 of `producttime`), Reforged 1000 (ratio 0.333, faster production).

### 1.4 What `Init` builds, in order (positions in window pixels)

1. Parse `recipe` into the ProductRecipe vector (`ParseRecipe`, 0x10e38).
2. `TCurve` from `conveyorpath` (if the table exists), `IngredientQueue(recipe, colorcount&0xff, curve, conveyorcount, root)` and `IngredientQueueTask`.
3. `TCurve` from `productpath`.
4. `LoadAppearance(appearance)` giving image names and tints (section 3).
5. Hopper `CoffeeHopper::Create((695,301), recipe, image)` with image `image/machine_2` if the recipe has exactly 3 entries, else `image/machine_5`. Added to root; `HopperCollectionTask`; `CoffeeProductQueueTask(root, hopper, productCurve, producttime, productspacing, appearanceNames, tints)`.
6. Sprite `image/box_open` (layer 1) at the productpath END point; sprite `image/box_open_mask` (layer 3) at the same point; a child sprite (layer 0) of the mask holding the case icon (`BuildItemAppearance` at scale 0.5, offset (0, 9)). Cases (layer 2) therefore slide between box and mask.
7. Masks: `image/conveyor_base_mask_lower` at `bottom_hood`, `image/conveyor_base_mask_upper` at `top_hood`, both layer 7. They hide grid overflow at the top and bottom.
8. Grid `CoffeeIngredientGrid::Create(recipe, gridx, gridy, columns, rows, griddx, griddy)`, added to root; `CoffeeGridTask(grid, gridspeed)`.
9. Gun: `image/gun_base` sprite (layer 0) at the conveyor path END point (stays (0,0) if no path), animated sprite `image/gun.xml` layer 10 at the same point, frame 0; `CoffeeGunTask(gunSprite, gunspeed, queue, grid)`.
10. `missed` points into the park vector (each element must be a table).
11. Clock sprites: `image/timer_face` (layer 0) child of root, `image/timer_hand` child of the face. (Positioned in `PostChildrenInit`.)
12. `PauseGame("game_paused")` (does nothing visible yet: the sibling windows do not exist during Init).

`PostChildrenInit` (0x15c10): `TBSGWindow::PostChildrenInit`, finds child TText windows named `time`, `countdown`, `score` (any may be null; the shipped coffee Lua has no `time`). Clock face position = centre of the `score` text rectangle in screen coordinates; fallback (720, 80) if no `score`. With the shipped layout (score text x=621,y=50,w=148,h=148) that is (695, 124). Sets window alpha 1.0 and calls `ResetGame`.

### 1.5 Drawing

`CoffeeFactory::Draw` = `TBSGWindow::Draw` (the DSL children: the `score` and `countdown` texts and the cosmetic bitmaps) followed, inside a 2D begin/end pair, by drawing the root sprite tree at an identity transform. So the whole sprite tree is drawn after the window's own children. **(inference: the score text, being a window child, is drawn before the sprites; the hopper and clock sprites therefore can cover it.)**

Sprite layers (higher = drawn later, as far as seen): gun base and root 0; box_open 1; case sprites 2; box_open_mask 3; ingredient on belt 4 (default of `Ingredient`); ingredient in a grid slot 5; ingredient removed from grid (flying to hopper) 7; masks 7; ingredient fired from the gun 8; hopper 9; gun sprite 10.

Fixed art from the Lua (not native): background `image/factory_background`, `image/factory_windows_<port>`, eight `missed_ingredient_divot` bitmaps, `conveyor_base` (78,0), `machine_stack_base` (591,47), buttons `go` and `pause` (both at 670,0; only one visible at a time), belt bitmap (`conveyor_belt_wide` at 97,58 for 8 columns, else `conveyor_belt_narrow` at 135,58), text labels. The 8 divot positions coincide with the 8 `missed` points offset by +13,+13.

### 1.6 Game flow and timing

`TWindow::StartWindowAnimation(this, 10, true, false, true)` is used for the main tick, so `OnTaskAnimate` (0x15434) is called on a 10 ms period, and **every call advances the game by exactly 10 ms**: each of the scaling tasks is driven by `TScalingTask::Tick(10)`, the clock by 10. **(inference)** the animation driver catches up when frames are late; this is the `TScalingTask` fix the header comment of 24 Jan 2009 describes.

`TScalingTask` (shared): fields `time` (scaled), `lastTime`, `scale`, `lastClock`, `taskClock`, `paused`. `Tick(ms)` does nothing if paused, otherwise `taskClock += ms` and calls virtual `Animate`. `Animate`: `time += (taskClock - lastClock) * scale`. `AnimateDelta(full)` returns the change in `frac(time/full)` since the previous call (handles wrap). `Reset(s)`: all zero, `scale = s`, paused = true. `Restart(delay)`: `lastClock = taskClock`, unpaused (the argument is ignored). `Pause()`: paused = true. Virtual table order: Reset, Restart, Animate, Pause. `CoffeeGridTask::Pause` additionally plays sound event `Stop_conveyor_belt_loop`; `CoffeeGridTask::Restart` plays `conveyor_belt_loop`.

`OnTaskAnimate` order:
1. If the app is not foreground (`TPlatform::IsForeground`) call `PauseGame("game_paused")` and return true (nothing else runs).
2. If state is `ABORTED`: `SetGameState(OVER)`. If `ENDING`: when the gun has no flying ingredients **and** the case belt has no cases, `SetGameState(OVER)`. (The hopper-collection flights are not checked.)
3. `COUNTDOWN`: if `clock > gameTime + 10`: `clock -= 10`; when the second digit `clock/1000` changes (old > new) play `clock_tick` and set the `countdown` text to `(clock - gameTime + 999)/1000`. Else: `clock -= 10`, play `start_bell`, `SetGameState(PLAYING)`, countdown text "" . The countdown therefore shows 3, 2, 1 for exactly 3000 ms. Initial `clock = gameTime + countdownTime` (63000).
4. `PLAYING`: if `clock <= 10`: reset the hand to identity, set `time` text "0:00", `SetGameState(ENDING)`. Otherwise `clock -= 10`; if `warnTime < oldClock` and `newClock <= warnTime` play `whistle`; `time` text = `m:ss` of `ceil(clock/1000)` (`"%lu:%02lu"`); hand rotation = `-(2 * clock/gameTime) * pi` (one full turn over the game).
5. Tick the five tasks in this order: conveyor (+0x7c), gun (+0x80), case belt (+0xac), hopper collection (+0xa8), grid (+0x94), each by 10 ms (paused tasks ignore it).
6. Tick bonus popups by 10 ms; remove ones that report finished.
7. `InvalidateScreen`, return true.

`SetGameState(n)` (0x1479e) effects, then `state = n`:

| n | Effect |
|---|---|
| 1 COUNTDOWN | pause conveyor, gun, case belt, hopper collection and the grid: the branch falls through to 0x14923, which calls `CoffeeGridTask::Pause` (vtable 0x21ba08 slot 0xc = 0x15e3e) and plays `Stop_conveyor_belt_loop` (verified in the Mac binary, `SetGameState` 0x1479e) |
| 2 PLAYING | sound `coffee_making_game`; Restart(15) on conveyor, gun, case belt, collection; grid task `Reset(1.0)` then `Restart(15)` (grid scale and time back to 1.0 / 0 at play start) |
| 3 ENDING | pause conveyor; `Reset(1.0)` conveyor (note `Reset` of this task also empties the ingredient queue); sounds `coffee_making_game_outro`, `game_over`; pause grid (plays `Stop_conveyor_belt_loop`) |
| 4 ABORTED | restore cursor `cursor/cursor` hotspot (1,1); sounds as ENDING; pause conveyor + reset; pause grid |
| 5 OVER | restore cursor; `TopModalOnly(false)`; pause+reset conveyor and hopper collection (resets also empties the hopper meters and removes flying ingredients); pause gun and case belt; pause grid; stop window animation; fill sibling `information` with `ui/chocolate_gameover.lua`, show it, hide `go` and `pause` buttons |

`ResetGame` (0x158ea, called from `PostChildrenInit` and `StartGame`): `clock = gameTime + countdownTime`; `score = bonusScore = missCount = 0`; `state = COUNTDOWN` set directly (not via `SetGameState`); `grid.Reset()` (empties all slots and removes their sprites); `Reset(1.0)` on conveyor, gun, case belt, collection, grid; `PauseGame("game_paused")`; removes the parked missed sprites from the root and clears that list; score text "0"; time text ""; sound `coffee_making_game_intro`; hide `go` and `pause`. Quirk: `CoffeeProductQueueTask::Reset` is only `TScalingTask::Reset`, so cases still on the belt are neither removed nor un-listed. After a game ended with cases in transit (the 8-miss abort) a "Try again" leaves those cases in the list; they resume moving and, on arrival, add to the new game's score (derived from the code; not run).

`StartGame` (0x15bb8): `ResetGame`, start window animation (10 ms), `SetGameState(COUNTDOWN)`, `ResumeGame`. `ResumeGame` (0x14c88): cursor `cursor/cursor` hotspot (16,16); if paused: unpause sound, hide `go`, show `pause`, hide `information`, Restart(15) on all five tasks, restart window animation, re-run `SetGameState(state)`, clear paused. Consequences derived from the code: in COUNTDOWN all five tasks are restarted and then re-paused by `SetGameState(COUNTDOWN)` (0x1479e -> 0x14923, grid `Pause` 0x15e3e), so the grid stays still and empty through the countdown (on the first game the first row arrives `gridspeed` ms into play; on later runs the stale `previousT` still gives one shift at play start); resuming in PLAYING re-runs the PLAYING entry actions, i.e. plays `coffee_making_game` again and does `Reset(1.0)` on the grid task, so **every resume drops the accumulated grid speed-up back to 1.0 and zeroes the scroll phase** (the next grid tick then sees `t < previousT` and shifts one row immediately, since `previousT` is not reset); resuming in ENDING repeats the ENDING actions (conveyor reset, outro sounds again). `PauseGame` (0x11018): see the TSV. Paused state keeps `state` unchanged, only the paused flag and task pauses; the clock stops because `StopWindowAnimation` stops `OnTaskAnimate`.

`OnKeyDown` (0x112a6): only the Escape key matters. It pauses (text `game_paused`) if not already paused. If the key-flag word has bit 0 set it also runs the Lua string `DoModal('dev/dev_menu.lua')` (the decompile reads the `kCheatMode` config value but does not test it before that call; **unknown** whether the real test was lost). `ToggleForeground` (static, 0x113e8) = pause with `game_paused` on `pInstance`.

### 1.7 Input

* `OnMouseDown(p)`: only if `state == PLAYING`, calls `CoffeeGunTask::Fire(p)`. Return value is the window's swallow-mouse flag (+0x4c), not the fire result. **Resolved**: nothing blocks it. `state` stays PLAYING during a pause, the paused flag is not tested, `CoffeeGunTask::Fire` does not test it, the `information` overlay is a sibling Bitmap, and `SetTopModalOnly` only gates drawing. A click on the playfield outside the pause popup while paused fires a shot that stays frozen until Resume (`factories-questions.md` section 3).
* `OnMouseMove(p)`: gun aim: `d = normalize(p - gunPosition)`; `angle = acos(d.y)`, negated if `d.x > 0`; the gun sprite matrix = rotation(`angle + pi`) at the gun position. Also calls `grid.SetTargetPosition(p)` when PLAYING, or `(-1,-1)` when not (hides the target marker).
* The grid target marker (`SetTargetPosition`, 0x164c8): slot under the cursor; if none and the marker was visible: cursor back to `cursor/cursor`, marker hidden. If a slot: when the marker was hidden switch to `cursor/blank_cursor` (hotspot (1,1)), show the marker, put it at the slot centre in grid-local coordinates (so it scrolls with the grid). The marker is a sprite with texture `image/conveyor_belt_gridtarget` (57 x 57 px), layer 0, created invisible and added to the grid after the slots and seams (`CoffeeIngredientGrid` ctor 0x166ea). Its child sprite (layer -1, so drawn behind it, alpha 0.5) shows the texture of the NEXT belt ingredient (`SetTargetImage`). Verified in `factories-questions.md` section 5.
* Keyboard other than Escape and the dialog buttons: none. Buttons (Lua): `go` -> `ResumeGame()`, `pause` -> `PauseGame()`.

### 1.8 Ingredient belt and gun (coffee specifics)

Belt (`IngredientQueue`, shared): `spacing = 1/conveyorcount`. Its task advances the head position `h` (0..1) by `AnimateDelta(conveyortime)` per tick; belt contents are placed on the path at `h - i*spacing`. `FillQueue(5)` (called when the reserve is empty): for `i` in `0 .. recipeSize*5-1` create `Ingredient(recipe[i % recipeSize])`, then shuffle by swapping each element with `RandRange(0, n-1)`. So the reserve holds each recipe entry 5 times and duplicate recipe entries (e.g. two `coffee`) appear proportionally more often. `PopConveyor` returns the head ingredient only if `h == 1.0`, then `h -= spacing`; so the maximum fire rate is one shot per `conveyortime * spacing` = 240 ms (scale 1), and the belt needs 1200 ms from empty (game start or reset) before the first shot.

`CoffeeGunTask::Fire(p)` (0x18100): pop ingredient, abort (return false) if none. Set the grid target image to the new head (`PeekConveyor`). Ingredient layer = 8. Gun position `g` = gun sprite position. Slot under `p` = `GetSlotIndex(p)`.
* If no slot or the slot is not empty ("a miss"): direction = `(p - g)` scaled to length 1000, `distance` = 1000.
* Else direction = `slotCentre - g`, `distance = |direction|`.
The `FlyingIngredient` (start = `g`, direction, distance) is added to the list, the gun plays animation `DoAnim`, and the message `kSoundEvent` with name `launch` is posted. `FlyingIngredient::Tick(dt)`: `t = min(1, t + dt)`; progress `u = min(1, t*1000/distance)` when `distance > 0` else `t`; position `= start + direction*u`; returns `u`. `dt` is `AnimateDelta(gunspeed)`, so a shot moves at 1000 px per `gunspeed` ms = 4 px/ms (1000/250). A flight to a slot at 300 px lasts 75 ms. **(inference)** A target further than 1000 px would never complete (u never reaches 1); not reachable in the 800x600 layout.

On arrival (`u == 1`, `CoffeeGunTask::Animate`, 0x18470): if `grid.CanAccept(ingredient)` (slot index from the ingredient's CURRENT position against the grid's CURRENT scrolled origin, slot must be empty): `grid.AddIngredient(ingredient, allowMatch = true)`; else `Ingredient::Die()`, which sends `MissedIngredientMessage` to the child named `FactoryGame` of the top modal. In both cases the entry leaves the flying list. A shot at an empty slot can still be refused if the belt scroll moved the ingredient into a different or occupied slot (scroll during flight is at most ~0.4 row at maximum speed, so this is rare).

### 1.9 The grid (`CoffeeIngredientGrid`, `CoffeeGridTask`)

Slots are `columns * rows`, row-major (`index = row*columns + col`). A slot has `available` (always true; nothing clears it), `contents` (ingredient name, empty string = empty) and a shared pointer to the `Ingredient` sprite. Geometry: slot `(c, r)` centre in grid-local coordinates `((c+0.5)*dx, (r+0.5)*dy)`; `GetSlotIndex(x, y)` maps window coordinates with the grid sprite's current position as origin and returns -1 outside `[gx, gx+cols*dx) x [gy, gy+rows*dy)`. Two scrolling belt-seam sprites (`image/conveyor_belt_wide_seam` for 8 columns else `..._narrow_seam`) are children, at x = half texture width, y = 0 and y = (rows/2)*dy.

The grid starts **empty**; rows are generated at the top by the scroll.

`CoffeeGridTask::Animate`: `t = frac(time / gridspeed)`; if `t < previousT` (wrapped) call `ShiftRows`; set the grid sprite y = `t*dy + rootY` (`rootY` captured in the constructor); `previousT = t` (initial value is never set in the constructor and `Reset` does not clear it, so the first run reads uninitialised heap and later runs carry the last tick's value; `factories-questions.md` 9.3). Speed scale: see 1.11.

`ShiftRows` (0x1729c): rotate all rows one step down (the bottom row wraps to row 0 by repeated swaps), snap every occupied sprite to its slot centre; then for each column of row 0 (the former bottom row): if occupied, clear the slot and remove the sprite from the grid (the ingredient is simply lost, **no miss is counted**, no score effect); then refill the column with `idx = RandRange(0, 2*N)` (inclusive, N = recipe entries): if `idx < N` create `Ingredient(recipe[idx])` in the slot, else leave empty. So each slot of a new row is filled with probability `N/(2N+1)` (3 entries: 3/7, 4 entries: 4/9), and an entry that occurs twice in the recipe is twice as likely. The two belt-seam sprites move down one row, wrapping at `rows*dy`. `fillCount` is not touched here.

`AddIngredient(ing, allowMatch)` (0x17d30): index from the ingredient's position; fails if index < 0 or slot not empty. Otherwise `fillCount++`, store name and pointer, layer 5, position = slot centre (grid-local), reparent from root to the grid. If `allowMatch` and the contiguous run of equal names through that slot is 3 or more horizontally (`CountHorizontal`) or vertically (`CountVertical`), call `ClearFrom(index)`.

`ClearFrom` (0x17bee): if the slot is non-empty, build a `CollectedIngredientMessage` and run `ClearGrid(col, row, type, list)`: a 4-neighbour flood fill over **all connected slots of the same name** (not only the 3-run): visit left, up, then remove self (layer 7, `fillCount--`, name cleared, appended to the list) then right, down. Duplicates are harmless because a removed slot no longer matches. The message is sent to the `FactoryGame` child of the top modal. `kCollectedIngredientEvent` handling is in 1.11.

`Reset` empties every slot, removes their sprites, `fillCount = 0`.

### 1.10 Hopper and case belt

`CoffeeHopper` (a sprite, layer 9, 0xcc bytes): one counter per recipe entry (up to 6) and one meter per entry. Each meter is a sprite `image/machine_meter` with a child icon `items/<name>` at (-1.5, -1) and a needle `image/machine_needle` (layer 2) at (25.5, 24). Meter positions (hopper-local): 3 entries: `(-6.5, -53 + 65*i)`; 4 or 5 entries: `((i%2)*106 - 56.5, (i/2)*81 - 32)` for i<4, and the 5th at (-3.5, 11); more entries are not positioned (**inference**: the game has at most 4 coffee ingredients; custom beverages have 3, blends 4). Needle y = `24 - 4.8*min(count, 10)` (so 0 -> 24, 10 or more -> -24).
* `Add(name)`: among entries whose name equals `name` pick the one with the smallest count (ties go to the later entry); increment it; update meters. Unknown name: ignored.
* `Release()`: if every counter is at least 1 then subtract 1 from all, update meters, return true; else return false and change nothing.
* `Reset()`: zero all counters.

`HopperCollectionTask` (collection time 250, a constant in the constructor): `Add(list)` wraps each ingredient in a `FlyingIngredient` with start = its current (grid-local) position and target = hopper position with x reduced by 75 (hopper y unchanged); `Tick` runs `AnimateDelta(250)` so each flight lasts 250 ms of scaled time (task scale stays 1). On arrival: `hopper.Add(ingredient name)`, remove the sprite, drop the entry. **(inference)** the ingredient is still a child of the grid when it flies, so the on-screen end point is the target plus the grid origin: with `gridx = 146` the net x offset is +71 from the hopper corner, which puts the icons near the hopper centre; the -75 compensates for the grid origin. If this were not intended, the ingredients would fly to x=620.

`CoffeeProductQueueTask::Animate` (0x19834), per tick:
1. `dt = AnimateDelta(producttime)`.
2. Walk cases oldest first. `CoffeeProduct::Update(dt)`: `t = min(1, t + dt)`, position = `productCurve.GetPoint(t)`, returns `t < 1`. Stop walking when the case just updated has `t < 0.1`. A case that reaches `t == 1`: remove the sprite, send `kCountProductEvent` (0x3ef) to the `FactoryGame` window (score +1), post the global message 1002 `chocolate_counted` (a sound), erase from the list, treat the previous-case `t` as 1.0 for the next step.
3. Spawn rule: let `last` be `t` of the last case examined (1.0 when the list is empty or the last one was removed this tick). If `last > productspacing/producttime` **and** `hopper.Release()` succeeds, create a new case sprite (layer 2, `BuildItemAppearance` at scale 0.5, start at the path start). Otherwise, if the list is empty, set the task scale to 1.
With the original category values (`producttime` 3000, `productspacing` 1500) a new case can start every 1500 ms at most (Reforged: 1000 ms). One case consumes one of every recipe entry from the hopper. The case belt never speeds up: its scale is not touched by the grid speed.

### 1.11 Scoring, speed, misses, end

* `kCollectedIngredientEvent` (message 1010; `OnMessage` 0x150ec): let `n` = number of ingredients in the list. Grid scale := `scale * speedUpFactor`, capped at `maxSpeed`. `bonus = matchBonus[n]` (or the last element if `n >= size`). If `bonus != 0`: `bonusScore += bonus`, `score += bonus` (update the `score` text), sound `ingredient_match_<min(bonus,5)>`, and a `CoffeeBonus` popup is created. If `bonus == 0` the sound is `ingredient_match_0`. In all cases the ingredients are passed to the hopper collection task. With the shipped table: `n` = 1,2,3 -> 0; 4 -> +1; 5, 6 -> +2; 7, 8 -> +3; 9 -> +4; 10 or more -> +5. `n` counts the whole connected group, not the line of three.
* `kCountProductEvent` (1007): `score++`, text updated. `kCountBonusProductEvent` (1006): `bonusScore++` and `score++`; nothing in the coffee code sends it. 1004 and 1005 (manufacture / produce) add a case straight to the belt; nothing in the coffee code sends them either.
* `score` = cases that reached the end of their belt + sum of match bonuses. Bonus cases are added immediately and need no hopper contents.
* `kMissedIngredientEvent` (1003) -> `MissedIngredient` (0x14ec8): keep the sprite in a list (removed at the next reset); if `missCount < parkPoints`, set its matrix to identity scaled 0.6 and position to `park[missCount]`; `missCount++`. If `missCount == parkPoints.size()` (8): `SetGameState(ABORTED)` and sound `ingredient_failure_factory shutdown`. Otherwise: grid scale := `scale * 0.5`, and if it is then below 1 set it to 1 (so a miss only undoes speed-up, never slows below base); sound `ingredient_failure_<missCount>`; if `warnOnMiss` and `missCount == 1` pause with text `coffee_first_missed`. Note the order: with 8 park points a miss does not slow the grid on the eighth.
* A miss is only (a) a shot that does not land in an empty slot. Ingredients falling off the bottom of the grid are not misses.
* `CoffeeBonus` popup (0x11576, 0x11442): a `TTextSprite` 120x120, alignment 5, font `fonts/fertigo.mvec`, size 80, white, text `+<bonus>`, child of the parent of the first collected ingredient (the grid), at the mean position of the collected ingredients. For 2000 ms it rises: position = start + (0, -150) * (age/2000), alpha = `1 - 0.8*age/2000`, with `age` taken before the tick's delta is added; it is removed when `age + dt >= 2000`. The factor is confirmed in the disassembly (`factories-questions.md` 9.2).
* End condition: time out (`clock <= 10` while PLAYING) -> ENDING; or 8 misses -> ABORTED (immediately game over next tick, in-transit cases and hopper contents are lost). In ENDING the gun, case belt and hopper collection keep running while the grid and conveyor are paused; the hopper keeps turning complete sets into cases until it cannot, and the game goes to OVER when no shot is in flight and no case is on the belt. The grid ingredients that were never matched are lost. Time-out therefore pays out whatever the hopper can still convert.
* Payout: the game never touches money. `ui/chocolate_gameover.lua` shows `GetString("factory_gameover", score)`; the OK button calls `CloseWindow(score)`, so `DisplayDialog` returns the score. `Product:RunMinigame` (sim/product.lua) returns `tonumber(score)`, or -1 if the dialog closed with nil (Cancel on the start dialog); `ui_factory.lua` then does `factory:SetProduction(product, count)` for `count >= 0`. That stored count is the cases-per-tick output used by the simulation (Lua, `sim/factory.lua`). The ingredient consumption for the plan is derived by Lua from the recipe (`Product:GetNeeds`), not by the native game.

### 1.12 Sound events used

`coffee_making_game` (Lua OK button and PLAYING), `coffee_making_game_intro` (ResetGame), `coffee_making_game_outro`, `game_over`, `clock_tick`, `start_bell`, `whistle`, `conveyor_belt_loop` / `Stop_conveyor_belt_loop`, `ingredient_match_0..5`, `ingredient_failure_1..7`, `ingredient_failure_factory shutdown`, and via message 1002: `launch`, `chocolate_counted`.

### 1.13 Lua interface summary

* Natives provided: `StartGame()`, `PauseGame([text])`, `ResumeGame()`, `GetScore()` (TSV).
* Lua read by native: DSL table keys above; globals `gPauseText` (written by native); files `ui/game_pause.lua` and `ui/chocolate_gameover.lua` (filled into the sibling window named `information`); sibling window names `go`, `pause`, `information`; child window names `FactoryGame` (found by `Ingredient::Die`, `ClearFrom`, product queue via the top modal), `score`, `countdown`, `time`.
* Strings: `coffee_first_missed`, `game_paused` (both shown by the pause dialog), `factory_gameover`, `coffee_start`.
* The Lua dialog table is expected to supply `product` (needs `recipe` as array, `code`, `appearance`, `GetName()`, `GetMachinery()`), `factory` (`port.name`, optional `windowX`, `windowY`). The native reads none of that; the Lua passes values in the DSL.

---

## 2. `TCurve` (paths for belts, from `Curve.h`)

Used for `conveyorpath` and `productpath`, `Transition` paths and port animations.

* `TCurve::Create(TLuaTable&)` (0x1d2f2): control points = every table entry that is itself a table, `{x, y}` (indices 1 and 2, default 0). Non-table entries skipped.
* Cubic Bezier chain: segment `k` uses control points `3k .. 3k+3`; the number of segments is `floor((n-1)/3)` (loop runs `i = 0,3,6,... while i < n-3`). Fewer than 4 points: no divisions; length 0; `GetPoint` for `0 < t < 1` is still computed from an empty division list (returns the zero vector for `t` in (0,1)); `GetStartPoint`/`GetEndPoint` return the first/last control point.
* Flattening (`Subdivide`, 0x1c578): recursive de Casteljau halving at 0.5. A piece is accepted when `(|p1-p0| + |p2-p1| + |p3-p2|) - |p3-p0| <= 0.2` (`ACCEPTABLE_ERROR` = 0.2, data at 0x1d80e0). Each accepted piece stores `tStart`, `tEnd`, `segmentLength` = the control polygon length (not the chord), `totalLength` (cumulative) and its start/end points.
* `GetLength()` builds the subdivisions on first use and returns the total.
* `GetPoint(t [, tangent])`: `t >= 1` -> end point; `t <= 0` -> start point; otherwise the arc-length position `t * length`, walking the pieces and linearly interpolating inside the piece **(inference: the interpolation factor operand was lost in the decompile)**. The tangent, when requested, is the normalised direction of the piece.
* `AddControlPoint`/`SetControlPoint` clear the cached pieces. `BuildExportString` prints `"{x,y},"` per point with `%d` (truncating floats). `Move`, `RenderCurve`, `RenderHull`, `RenderControlPoints` are editor/debug drawing.

---

## 3. `CustomItems` (`LoadAppearance`, `BuildItemAppearance`; 0x1fcd2, 0x1f9e4, 0x1fff4)

There is no native persistence file. A custom product's look is a Lua table kept in the player record (`Player.itemAppearance[code]`, saved with the game by Lua) and rebuilt on load (`BuildCustomProduct` in `sim/recipe.lua`).

`LoadAppearance(table, names, tints)`: look for a nested table `appearance`; if present parse it as a layer list. Otherwise: if the table has a non-empty string `code` use a single layer `items/<code>_big` tinted white; else if it has a non-empty `name`, `items/<name>_big` (a literal "items/" prefix then the name then `_big`); else treat the table itself as the layer list. Layer list entry forms: a string `s` -> image `custom/<s>`, white; or a table `{image, r, g, b}` -> `custom/<image>`, tint `(r, g, b, 1)` from indices 2..4 (default 0). In the coffee game the Lua passes `{code = product.code}` for stock products, so cases look like `items/<code>_big`; for custom products it passes the saved layer list (`product.appearance`).

`BuildItemAppearance(sprite, names, tints, scale)`: removes the sprite's children, applies `Scale(scale, scale)` to its matrix if scale != 1, then for each layer in order creates a child sprite at the next layer number with the texture and the tint (r,g,b,a copied to the sprite colour), followed, if a texture `<name>_highlight` exists, by an untinted child on the next layer number. So a layer occupies one or two consecutive layer numbers; the highlight is drawn above its layer. The `TLuaTable*` overload (`BuildItemAppearance(sprite, table, scale)`) is the same through `LoadAppearance`.

Lua-side forms (for reference, `sim/product.lua`): `GetAppearance`, `GetAppearanceBig`, `GetAppearanceHuge` build the same stack with `BitmapTint` + `Bitmap` windows (scales .25*s, .5*s, 1).

`SaveCreationToFile` (Reforged `ui/ui_kitchen.lua` `SaveCreation`) has no counterpart in the Mac binary; nothing calls `SaveCreation`; see `ledger-and-game-natives.md` section 9. Its table argument is `{name, description, category, ingredients = {names}, appearance = { {image, r, g, b}, ... }}`.

---

## 4. RecipeCreator and the kitchen

`RecipeCreator` (0x326cc) is an empty `TWindow` subclass (constructor only sets the vtable; `Init`, `PostChildrenInit`, `Draw` forward to `TWindow`; `OnMouseMove`/`OnMouseDown` return false). No Lua in either asset tree constructs a `RecipeCreator{}`. **Nothing native implements the kitchen**; the type is a registered stub and need only be accepted by the DSL.

The kitchen is `ui/ui_kitchen.lua` plus `sim/recipe.lua`, `sim/kitchen.lua`, `sim/recipe_feedback.lua`. What the engine must provide to it: `RecipeDesigner{}` (section 5), `BitmapTint` windows with `BTSetTint`, `Rectangle` with `SetRectangleColor`, `Rollover`, `CharWindow`, `TextEdit` with `clearinitial`, `widthlimited`, `ignore` keys, `EnableWindow`, `SetBitmap`, `SetButtonToggleState`, `GetLabel`/`SetLabel`, `Mod`, `RandRange`. Behaviour (Lua):

* Categories (`bar`, `beverage`, `infusion`, `truffle`, `blend`, `exotic`) give `usedSlotCount` 3, 3, 4, 6, 4, 5. Up to six bowls/slots; unused ones are disabled and cleared. Ingredient drawers: `cacao, coffee, dairy, sugar, fruit, nut, flavor` (truffle powder is added to the cacao drawer); only ingredients in `Player.labIngredients` are clickable; `AddSlotIngredient` fills the first empty slot with the clicked ingredient, shown as `items/<name>_big`.
* "Taste" (`TasteIt`) requires all used slots full and calls `EvaluatePlayerRecipe(category, ingredients, slotCount)`, which returns feedback text, low and high price and an allow flag. Scoring: base 140 points; variety penalties (1 distinct ingredient -> 30; ratio < .5 -> -60; < .6 -> -30); category rules per factory (coffee needs coffee beans: -80 without, fruit -80, nuts -40; >70% of one class of ingredient -60/-80; chocolate needs cacao and a sweetener or fruit, truffles need `powder`, else points := 30; pair bonuses from `CoffeeEvaluators`/`ChocolateEvaluators`); a recipe identical to an existing system product gets 50 points and "tastes like" feedback; points clamp to [10, 300]; price = ingredient price sum * `category.markup * points/100`, rounded, at least 1. A user recipe that duplicates a known custom one sets `allow=false`.
* "Design" is enabled only if `table.getn(_AllCategories.user.products) < Player.customSlots` and `allow`. Design mode calls `SetRecipeType(targetCategory)` and on the first entry `RandomDesign()`.
* Saving: `ConfirmRecipeCreation` rejects empty or default name/description (`invent_noname`), rejects names equal (case-insensitively) to any product name except another player's custom item (`invent_name_inuse`), asks `invent_confirm`; `DoRecipeCreation` builds the appearance list `{ {image, r, g, b}, ... }` for layers 0..3 from `GetLayerImage(i)` and `colorOptions[colors[i+1]]` and calls `CreateCustomRecipe(name, description, ingredients, appearance, category)`. That creates the product with code `category_ing1_ing2...` (ingredient codes sorted) in the `user` category and fills `Player.itemRecipes`, `itemAppearance`, `itemNames`, `itemDescriptions`, `itemPrices`, `itemMachinery`. Persistence is the Lua save; see `trade-and-save.md`.
* Illegal characters in names: `kIllegalProductChars = ><\"` (style.lua).

The recipe book screens (`ui/ui_recipes.lua`, `recipe_category.lua`, `recipe_recipe.lua`) are pure Lua (no natives besides the generic window ones); seven category tabs at x = 35, 142, 242, 342, 442, 542, 642 (y 9).

---

## 5. RecipeDesigner

Native members and `RandomDesign` timing are in `ledger-and-game-natives.md` 10.3. Additions:

* Class `RecipeDesigner : TWindow` (ctor 0x32b96) has no `Init`; it registers `RandomDesign`, `SetRecipeType`, `SetLayer`, `GetLayerImage` in the constructor and sets them to nil in the destructor. State: `layerAssets` (TAssetMap, keeps the textures alive), `RandomizeTimer` (+0x50), four `vector<str>` layer lists (+0x54, stride 12), four current indices (+0x84).
* Child window names it requires (looked up with `GetChildWindowCast` by name, per layer `n` = 0..3): `option_layer<n>` (BitmapTint), `option_highlight<n>` (Image), `display_layer<n>` (BitmapTint), `display_highlight<n>` (Image). The shipped Lua creates them at 64x64 with `scale=0.5` (option) and 128x128 with `scale=0.5` (display).
* `SetLayer(layer, idx)`: `idx` is an **absolute index**, not a delta: stored index = `(idx + size) % size`. The Lua passes `design[n]+1` / `design[n]-1` and keeps the returned index in `design[n]` (initial `{0,0,0,0}`). The option windows show the texture at scale 0.5 and the display at 1.0 (the third argument of `TImage::SetTexture` is a **scale**, confirmed: it multiplies the texture size for the window size and `TImage::Draw` applies it; alpha is a separate field; `factories-questions.md` section 6); the highlight variant (`<name>_highlight`) is shown in the paired highlight window if the texture exists, otherwise hidden.
* Directory scan order: `TFile::GetNextFile` walks a sorted (bytewise, lower-cased) resource map, so ascending name order; `SetRecipeType` drops `_highlight` and non-`.png` names, then appends one empty entry to layers 2 and 4 (and to layer 3 except for `beverage`). The Reforged Lua assumes index `NN-1` maps to `layerL_NN`; that holds in both trees (no gaps). Selectable image counts for `custom/bar` (layers 1..4): **original (vanilla) 4, 7, 4, 8; Reforged 4, 7, 5, 11**. (`layer1_05_highlight.png` has no base image and is not a layer; it is filtered out.) All categories: `factories-questions.md` section 7.
* Tint: the designer never tints by itself; the **Lua** global `SetRecipeTint(layer1to4, index)` (defined in `ui_kitchen.lua`) does `colors[layer] = index`, `SetRectangleColor("tint"..layer, color)`, `BTSetTint("option_layer"..(layer-1), color)`, `BTSetTint("display_layer"..(layer-1), color)`. The palette has 65 entries (grays 5, then browns 10, then ten groups of 5; the 5th magenta has a typo `876`). `SetRecipeTint` wraps indices over 65 with `Mod(index, 65) + 1` (index 66 -> 2, 100 -> 36; 65 itself is valid). `RandomDesign` sends `RandRange(1,100)` so 35% of random tints take the wrapped path. Highlight windows are not tinted.
* `RandomDesign` ticks: the first tick happens 100 ms after the call, `timer` takes 100..1900; layer 0 changes on ticks with `timer <= 1000`, layer 1 on `timer <= 1500`, layer 2 on `timer <= 1800`, layer 3 on every tick; each tick runs the concatenated `SetRecipeTint(...)` string only for the layers rolled in that tick plus always layer 4. The tick with `timer = 2000` returns false and stops the animation without rolling. The animation is a `StartWindowAnimation(100, true, true, false)`.
* Quirk: after `RandRange(1, size-1)` with `size == 1` the result is 1 and `SetLayer` wraps it back to 0.

Persistence and `SaveCreationToFile`: see section 3 (none native).

---

## 6. SignEditor

Dead feature in the shipped game: `ui/sign_basic.lua` is the only user and nothing opens it; it still contains the placeholder text "BASIC SIGN DESIGNER: NOT YET IMPLEMENTED". `Player.sign` is commented out in `sim/player.lua`, so the `sign=Player.sign` DSL value would be nil and the native code (which dereferences `pSign` without a check in `Draw`, `SetBackground`, `SetLogo`, `SetFont`, `SetText` and slider handling) would crash if run. The engine can register the window type and natives as no-ops.

* DSL keys: `sign` (table), `signx`, `signy` (position of the sign sprite inside the window, default 0). Parsed in `PostChildrenInit` (0x387cc) only if `sign` is a table; the sign sprite is created from it (`Sign::Create`, 0x37c98).
* `sign` table keys (defaults): `textH` 0, `textS` 0, `textV` 0, `textA` 100, `textX` 0, `textY` 0, `textSize` 30, `logoScale` 100, `logoA` 100, `logoR` 0, `logoX` 0, `logoY` 0, `background` "bg1", `logo` "logo1", `font` "fertigo.mvec", `text` "".
* `Sign` sprite: texture `logo/<background>`; child logo sprite (layer 1) with texture `logo/<logo>`, transform = position (`logoX`, `logoY`), scale arguments `logoA/100` and `logoScale/100` passed to `TDrawSpec` in that order (see Unknown 7), rotation `2*logoR*pi/360`; text sprite 350 x 200, alignment 5, font `fonts/<font>`, size `textSize` (clamped to >= 0), colour = HSV(`textH`, `textS`, `textV`) with alpha `textA/100`, last argument 2, at (`textX`, `textY`), text `text`.
* Slider handling (`OnMessage` 0x3710a): reacts only to message type 9 (`kSliderValChanged`), looks up the child slider named in the message, reads its value `v` (0..1 **(inference)**) and updates by name: `textH` = `v*360`; `textS`, `textV`, `textA` = `v*100`; `textX`, `textY` = `v` (raw, no slider in the Lua); `textSize` = `v*100 + 15` and sets the text sprite line height (clamped >= 0); `logoScale` = `v*100`; `logoA` = `v*100` and also the logo sprite alpha `v`; `logoR` = `v*100`; `logoX`, `logoY` = `v` raw. Then update the colour (H, S, V or A), text matrix (position `textX`,`textY`, scale arguments `textA/100` and 1.0, rotation `2*textR*pi/360` with `textR` having no slider; see Unknown 7) or logo matrix, and invalidate. It always returns false.
* Drawing: `Draw` draws the sign sprite tree first, then the window children.
* Lua Natives: `SetBackground(name)`, `SetLogo(name)`, `SetFont(name)` (prefix `fonts/`), `SetText(text)`; registered by the constructor, set to nil by the destructor.

---

## 7. CurveEditor (`TCurveEditor`, dev tool)

Not used by the shipped game except the dev menu (`dev/dev_animation.lua`). The engine can leave it unimplemented.

* DSL: `background` (image shown at the window centre), `port` (table; in `PostChildrenInit`: `buildings` array of `{name, layer, scale, x, y}` and `sprites` array of `{image | images[1], layer, behind = {layer}, scale, x, y}` become static sprites with texture `ports/<port name>/<name>`; sprites with a `path` are skipped); children `scaleslider`, `farslider`, `nearslider` (sliders, message type 9). Other keys in the dev Lua (`xOffset`, `yOffset`, `scale`, `portName`, `port` as key) are not read by this class (only `background` and `port` are).
* State (constructor 0x1de2e): `mode` MOVEPOINT 0 / ADDPOINT 1 / DELETEPOINT 2 (initially 0), `select` -1 (none), an empty curve, `scale` 0.5, `xOffset`/`yOffset` 0, `farY` 0, `nearY` 600.0, `scaleFar` and `scaleNear` 1.0, `pathMode` 2 (`ONEWAY=0, PAUSE=1, LOOP=2, BOUNCE=3`), `pathTime` 5000 ms, plus an animated preview sprite.
* Input: holding `A` sets ADDPOINT (released: back to MOVEPOINT), `D` sets DELETEPOINT. Mouse down in ADDPOINT appends a control point at `(p - offset)/scale` and selects it; in MOVEPOINT picks the first control point within 5 px (`CONTROL_RADIUS`) and drags it on move (`OnMouseMove` updates it, `OnMouseUp` clears the selection). DELETEPOINT does nothing (the handler returns without deleting). `C` copies a ready-made Lua statement to the clipboard: `CreateSprite(<port>, { image='<image>', time=<pathTime>, motion='loop', layer=<layer>, scaleFar=%01.3f, scaleNear=%01.3f, path={ {x,y}, ... }, })` (the sprite name field is the editor's first string).
* Animation (`OnTaskAnimate`, adds `_TICK_TIME` = 20 ms per tick): `t = (time mod pathTime)/pathTime` (loop) or ping-pong for mode 3; sprite placed at `curve.GetPoint(t)`; the animated sprite's frame is picked from the heading angle: `angle = acos(tangent.y)*(-180/pi) + 180` (360 - angle if the tangent x < 0), frame = `round(angle / (360/numFrames)) mod numFrames`, remainder rotation applied; scale = `scale * (scaleFar + ((y - farY)/(nearY - farY)) * (scaleNear - scaleFar))`.
* Sliders: `scaleslider` value is the display scale (0..1 as stored; `SetScale` recentres: offset = (window size - image size*scale)/2); `farslider`/`nearslider` map `v` to `0.01 + (5 - 0.01)*v` (`scaleMin` 0.01, `scaleMax` 5.0).
* Natives (Init 0x1ed18): `SetAnimScale`, `UpdateAnimTime`, `UpdateAnimLayer`, `SetAnimSprite`, never unregistered.

---

## 8. Reforged differences relevant to the engine

* `ui/coffee_factory.lua` is the same configuration as the original plus `categoryName` (already present) and comments; `label="#"..GetString("cases")` where the original used `label="cases"`. The `#` prefix means "literal text".
* `items/categories.lua` Reforged lowers `productspacing` to 1000 for beverage and blend.
* `ui/ui_kitchen.lua` Reforged adds community creations (`OpenCommunityCookbook`, `LoadCreation`, `SaveCreationToFile`, `HasPersistableLayer`), dynamic slot buttons (`+`/`-`), a `GetRandomString` helper; it still relies only on the same four `RecipeDesigner` natives. `SetCommunityLayerImage` tries `option-1, option, option+1` against `GetLayerImage` to cope with differing `SetLayer` bases, confirming that native `SetLayer` takes a 0-based absolute index into the (alphabetical) file list.

---

## 9. Unknown

1. `previousT` of `CoffeeGridTask` is uninitialised on the first run (heap-dependent) and carried over later; see `factories-questions.md` 9.3. Assume 0 for the first run.
2. `CoffeeBonus::Tick` rise factor is resolved (age/2000, linear). Still open: the operand lost in decompile for `TCurve::GetPoint` interpolation, inferred as linear in remaining/segmentLength.
3. Whether the `kCheatMode` condition guards the dev-menu launch in `CoffeeFactory::OnKeyDown`.
5. `TFile::GetNextFile` order is sorted on the Mac binary (`factories-questions.md` section 7); the Windows build was not checked.
6. (Resolved: scale) `TTextSprite::Create` last argument (999 for the bonus popup, 2 for the sign; likely shadow/outline).
7. The second argument to `TDrawSpec(position, a, b, rotation)` ordering for the sign logo/text matrices: the decompile passes `logoA/100` as the first scale factor and `logoScale/100` as the second for the logo, and `textA/100` and `1.0` for the text; whether these are (scaleX, scaleY) or (alpha, scale) was not determined.
8. How the message type 1002 handler maps names `launch` and `chocolate_counted` to sounds (read as sound events by inference only).
9. Hopper behaviour for recipes with more than 5 ingredients (positions unset for the sixth).
10. Why `RecipeCreator` exists (no user in any asset tree; likely an abandoned earlier kitchen).
11. The dev-menu behaviour of `TCurveEditor` slider ranges, and `pathMode` use (`Pause`/`Resume` declared in the header have no body in the binary).
12. The scale/layout of `sign` coordinates and `Sign::Draw` camera: untested since the feature is dead.
