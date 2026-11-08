# Design: the chocolate and coffee factories

`ChocolateFactory`, `CoffeeFactory` and their parts, the `RecipeDesigner` natives, the shared appearance builder, and the
other windows registered beside them (`SlotMachine` and the placeholders). Facts come from `docs/re/chocolate-factory.md`
(CF §n), `docs/re/coffee-factory-and-recipes.md` (CO §n), `docs/re/ledger-and-game-natives.md` (LG §n) and
`docs/re/factories-questions.md` (the answers to the questions the original design left open), and they are not repeated
here. Conventions (`World`, `Ticker`, the `Sprite` tree, `SpriteScript`, `gfx::Curve`, window animations on the modal
clock, natives that do nothing while their window is absent, deferred destruction) are from
[game-windows.md](game-windows.md) (GW §n) and [window-system.md](window-system.md) (WS §n).

Where the code lives: the shared parts and the chocolate game are in `src/games/common/factory/` (`Parts.*`,
`FactoryWindow.*`, `Chocolate.*`), the coffee game and `RecipeDesigner` in `src/games/choc3/factory/`, the natives and
window registrations in `src/games/choc3/FactoryNatives.cpp`, `SlotMachine.*` beside it, and the appearance builder in
`src/games/common/Appearance.*`. Tests are `tests/test_factory.cpp`, `tests/test_recipe_designer.cpp` and the
`check_factory.py`, `check_recipe_designer.py` and `check_slotmachine.py` scenarios (§10).

## 1. Behaviour the scripts depend on

- **One integer out.** The only result is the score. `ui/chocolate_gameover.lua`'s `done` button runs
  `CloseWindow(score)`, so `DisplayDialog` returns it. `Product:RunMinigame` returns `tonumber(result)`, or -1 for
  `nil` (Cancel on the intro). `MakeChocolates` (`ui/ui_factory.lua`) then calls `factory:SetProduction(product, count)`
  and `TickSim(1)`, and `Player:RunFactories` adds the cases to `Player.products[code]` (§5).
- **Four globals**, `StartGame()`, `PauseGame([key])`, `ResumeGame()` and `GetScore()`, used by the buttons in
  `ui/chocolate_factory.lua`, `ui/coffee_factory.lua`, `ui/game_pause.lua` and `ui/chocolate_gameover.lua`. Nothing
  calls back into Lua during play. The native runs Lua only in two ways: it fills the **sibling** `information` Bitmap
  with `ui/game_pause.lua` (after setting the global `gPauseText`) or with `ui/chocolate_gameover.lua`. Both use
  `FillWindow` semantics: destroy the children, then run the file with that window as the creation top.
- **Names the native finds.** Its own children `score`, `countdown` and `time` (Text; `time` is absent in the shipped
  Lua). The siblings `go`, `pause` and `information`, found through the parent. The window is named `FactoryGame`.
- **The DSL keys and their defaults** are in CF 3.1 and CO 1.3. The Lua never passes `traycount`, `speedup`, `maxspeed`
  or `trayend`, so the defaults apply: 5 trays, ×1.1 per fill, a cap of 8, and the endpoint at 0.
- **Timing.** One logical step is exactly 10 ms of game time (CF 2.1). The countdown lasts 299 steps, then play lasts
  6000 steps. Catch-up is clamped: more than 1 s behind runs one step (factories-questions §2.1).
- **The kitchen** needs `RandomDesign`, `SetRecipeType`, `SetLayer` and `GetLayerImage` on a `RecipeDesigner` window
  (CO 5, LG 10.3), plus `BTSetTint` and `SetRectangleColor` (LG 10.5). The Lua defines the global `SetRecipeTint`,
  which `RandomDesign` calls through a Lua string.

## 2. Decisions

1. **Composition, not the original's task classes.** Each part (conveyor, gun, trays, belt, grid, ...) is a plain
   struct with a `ScalingTask` member and small pure rules. The factory window runs the step code that ties them
   together. The original sends window messages (0x3eb to 0x3f1, 1010) from the parts to `FactoryGame`. Ours makes
   direct calls on the window in the same places, which keeps the synchronous order and drops the message plumbing.
   Sound messages (0x3ea) become `sound(name)` calls, which post the game's audio event.
2. **Game objects are `Sprite`s in the window's tree, as in the original.** The tree owns them. A part's lists hold
   non-owning pointers into it. Every transient sprite (an ingredient, product, ring or case) is referenced by
   **exactly one** list at a time. The code that removes a sprite from the tree erases that reference in the same
   function. Reparenting (from conveyor to tray, or from root to grid) uses `Sprite::release` and keeps the pointer
   value stable. Unlike `PortWindow`, the tree is pruned (`removeChild`), because a run creates hundreds of
   ingredients and Try Again repeats it.
3. **The 10 ms step is a window animation with catch-up** (§4). It runs on the factory modal's clock: simulated in
   headless mode, real otherwise. So runs are deterministic, the step count does not depend on the frame rate, and the
   game freezes under a pushed modal (the Help dialog).
4. **One shared base, `FactoryWindow`, and two subclasses.** The clock, countdown, hand, whistle, misses, pause and
   resume, the `information` handling, the four natives and Escape are identical in CF 4 and CO 1.6. Each subclass
   composes its own step order from base helpers. There is no generic task list.
5. **Globals act on the newest live factory.** That is a static `active()` list maintained by the constructor and
   destructor, as in `CratesGame`. The original leaves the globals bound to a stale `this`; ours do nothing (they log
   once as unimplemented), and `GetScore` returns 0, when no factory is alive.
6. **No `RandSeed(Timer())` on open.** The game draws from the shared `ctx.rng`, which `--seed` sets, so runs repeat.
7. **Quirks we change:**
   - **Clicks while paused do nothing.** The original's `OnMouseDown` checks only `state == PLAYING`, so a click
     outside the overlay fires a shot that hangs in the air until resume (factories-questions §3, CO Unknown 4).
   - **Coffee `ResetGame` also clears the case belt.** Otherwise cases left over from an aborted run score in the
     next one after Try Again (CO 1.6). Without this, `CloseWindow(score)` would report goods the last run did not make.
   - **Guards for undefined behaviour.** If `matchBonus` is absent, the bonus is 0. An empty recipe spawns nothing,
     instead of looping on refill. A recipe size with no `image/tray<N>` logs once and draws an empty tray. A coffee
     hopper with more than 5 entries puts the extras at (0,0).
   - **The first step comes 10 ms after `StartGame`**; the original's call sequence gives about 20 ms
     (factories-questions §2.2).
   - **The big countdown digit is drawn again above the sprites** (the original draws it under them), so it can be read.
   - **Pausing on focus loss.** Losing or regaining the foreground both run `PauseGame("game_paused")` (`World` calls
     `FactoryWindow::focusChanged`); regaining never resumes, the player presses OK. A factory destroyed while paused
     unpauses the audio, which the original leaves paused (docs/re/audio.md 10.7).

   **Quirks we keep**, because players saw them and no script depends on the fix:
   - Resume re-runs the current state's entry actions, so the music event plays again.
   - A coffee resume in PLAYING resets the grid speed to 1 and its phase to 0, and shifts one row on the next step.
   - Coffee collection flights still in the air at OVER are lost, and ABORTED goes to OVER on the next step.
   - Chocolate goods still on the belt when ABORTED reaches OVER are not counted.
   - The `RandomDesign` lower bounds are 1 and 2.
8. **Left out:** the cheat-mode dev menu, the `TGameState` keys `ChocolateFactoryTime` and `RecycleCount` (nothing
   reads them), and the unreachable powerup ingredients (CF 9.3). Cursors are implemented through the app's cursor
   service (the gun aimer while playing, the arrow while paused); without that service (unit tests) the first use of each
   logs `[unimplemented] cursor <name>`, and the name shows in the window dump.

## 3. Types, ownership and lifetimes

### 3.1 Supporting pieces

- **`sdk::FixedStep`** (`src/sdk/Ticker.h`) is the original's `TAnimTask` with catch-up (CF 2.1). `start(now, ms)`
  sets `next = now + ms`. `due(now)` resyncs `next = now` when more than 1000 ms behind, then returns true and advances
  `next` by exactly `ms` while `now + 1e-6 >= next`. It is called in a loop until false.
- **`WindowManager::startWindowAnimation(id, ms, catchUp = false)`.** With `catchUp`, `tickWindowAnimations` calls
  `onAnimate` once per due step, re-checking after each call that the entry is still live (stop or restart) and the window
  is not destroyed, and stopping when `onAnimate` returns false.
- **`Sprite::release(child)`** detaches a direct child and keeps the pointer valid; **`removeChild(child)`** releases and
  destroys it with its subtree. Neither is called while the same tree runs `tickScripts` or `resortDirty`. Those run in
  `World::tick`, and the factory steps run in `tickWindowAnimations` (§4).
- **`Window::autoAim()` / `WindowQuery::autoAim()`**, for the harness only (§8): a window-local point to shoot at now,
  or none.
- **`Textures`** (`src/games/common/Textures.h`) is a thin caching view of the window manager's texture and sheet caches.
  It loads by name, logs a missing name once and answers null, so a sprite without its picture draws nothing.
- **`AssetFS::list(dir)`** is a sorted listing of the merged asset sources (used by `RecipeDesigner`).

### 3.2 Shared parts (`factory/Parts.h`)

- **`ScalingTask`** is `TScalingTask`, exact (CF 2.2), in doubles. `reset(scale)` zeroes the clocks and pauses;
  `softReset(scale)` (trays and recycler) keeps the position; `restart()` sets `lastClock = taskClock` and unpauses;
  `tick(ms)` returns false while paused, else adds to `taskClock` and returns true, and the owner then calls `animate()`:
  `lastTime = time; time += (taskClock - lastClock) * scale; lastClock = taskClock`. `phase(full)` is `GetT`, the fraction
  of `time / full`. `animateDelta(full)` animates and returns the phase change, wrap-aware.
- **`Ingredient`** is a layer-4 sprite `items/<name>` with a lazily made ring child (layer -1). `setColor` follows CF 5.1:
  the rainbow ring texture for colour 0, `item_ring` for a non-zero colour only when the previous colour was 0, tinted with
  `factoryColor(c)`. `IngredientArt` names the pictures, with Chocolatier 3's as defaults.
- **`IngredientQueue`** (CF 5.1, CO 1.8) is the train on the conveyor. `makeBag` is `FillQueue(5)`: five copies of each
  recipe entry, a colour for each when `colorcount > 1` drawn as `RandRange(rainbow ? 0 : 1, colorcount)`, then a swap
  shuffle, all colours first. The bag is the off-tree `reserve_`, consumed from the back; `belt_` holds the sprites in
  the tree under the base sprite, front at the gun. `update(dt)` moves the head, places the train and spawns at the tail.
  `pop()` answers only when the head is exactly 1.0 and then drops it by one spacing (0.2: ready after 120 steps, again
  240 ms later). `ready()` is the same test, for `autoAim`. `reset()` removes every belt sprite and empties the reserve.
  It also builds the arm-and-finger pistons that hold each belt item (factories-questions §8.2).
- **`Flight`** is `FlyingIngredient` (CF 5.2, CO 1.8): `t = min(1, t + dt)`, `u = distance > 0 ? min(1, t*1000/distance) : t`,
  and the ingredient is moved to `start + dir * u`. The chocolate gun flies 1000 px in 250 ms (40 px per step, `u == 1` at
  step 25); the coffee form ends at `distance/4` ms.
- **`gunRotationDeg(mouse, gun)`** is `acos(d.y)`, negated if `d.x > 0`, plus pi, as clockwise degrees: (1,0) gives 90,
  (0,1) gives 180, (0,-1) gives 0. **`segmentHitsCircle`** is the squared distance from the point to the segment against
  `r²` (CF 5.3).

**The ingredient's single reference moves, in order:**

1. `reserve_` (owning, off-tree).
2. The base sprite's child list (now the tree owns it), referenced by `belt_`.
3. On `pop`, the reference moves to the gun's `flights`, and the sprite goes to layer 8.
4. Then exactly one of these:
   - **Chocolate.** A tray adopts it with `tray.addChild(base.release(*ing))`, and `slots[i]` holds it until `clear`
     calls `removeChild`. A miss leaves it on the base, parked, referenced by `missed_` until `ResetGame`. The
     recycler calls `removeChild`.
   - **Coffee.** The grid adopts it into `slot.ing`. A clear moves the reference to the collection list, still a child
     of the grid, and arrival removes it. A miss parks it as in chocolate.

**The appearance builder (`Appearance.h`)** is the shared form of `LoadAppearance` and `BuildItemAppearance` (CF 3.4, CO 3):

- `loadAppearance(style)` reads an `appearance` sub-table as a list; else a `code` (or `name`) gives the single white layer
  `items/<code>_big`; else the table itself is the list. A string entry is the layer `custom/<s>` in white and a table
  entry `{name, r, g, b}` the layer `custom/<name>` tinted `(r, g, b, 1)`, missing numbers 0.
- `buildItemAppearance(sprite, layers, scale, textures)` removes the sprite's children and applies the scale. Then for
  each layer it adds one child in order: the tinted layer, then `<image>_highlight` untinted if that texture exists.
- Both factories (the product and the case picture) and the ledger's product builder (`DookieDropper`, in
  `src/games/common/Ledger.cpp`) use it. No Lua natives come with it: `SaveCreationToFile` is dead (LG 9) and stays
  unregistered.

### 3.3 `FactoryWindow : ui::BSGWindow` (`factory/FactoryWindow.*`)

- **State.** `enum State { Init, Countdown, Playing, Ending, Aborted, Over }` (values 0 to 5, as in the dump), the clock,
  `score_`, `bonus_`, `misses_`, `paused_`, `missed_` (parked ingredients) and `park_` (the `missed` points, whose number is
  the miss limit). `root_` is declared first, because the parts of a subclass point into it.
- **Per-factory pieces** a subclass supplies: `enterState` (the `SetGameState` table, CF 4.2 / CO 1.6), `resetParts`,
  `pauseParts`, `restartParts`, `fire`, `aim`, `speedTask` (trays or grid, halved on a miss), `firstMissKey`
  (`choco_first_missed` / `coffee_first_missed`), `gameOverFile`, `introSound` and `stepOnce`.
- **Helpers** the subclasses' `stepOnce` composes: `stepClock` (the COUNTDOWN and PLAYING rows: digits, bell, whistle,
  hand, m:ss, and the changes to PLAYING and ENDING), `setState`, `resetGame`, `missed` (CF 5.4 = CO 1.11, identical),
  `addScore` and `showGameOver`.
- **Construction.** `init` sets `modal.defaultFocus = id()` (CF 3.2 step 1). `postChildrenInit` finds `score`,
  `countdown` and `time` **by name each time they are used**, so the window keeps no child pointers. It centres the
  timer face on `score` (falling back to (720, 80)), runs `resetGame`, calls `world.setFactory(id())` and takes keyboard
  focus so Escape reaches `onKeyDown`.
- **Siblings.** `go`, `pause` and `information` are looked up as `parent()->find(name)` at each use, and a missing one
  is skipped. At `init` time `information` does not exist yet, so the first `PauseGame` only sets `paused_`, and the
  intro overlay keeps its Lua content (CF 3.2 step 12, then 4.1).
- **Filling `information`** works as `FillWindow` does: destroy its children (deferred), `lua_setglobal(L, "gPauseText")`,
  then `runFileInto` on `L`, then show it. `L` is the native's own state when Lua called the native, and the main state when
  a 10 ms step paused the game (a first miss, or game over). Neither file yields. The button whose command is running (Try
  Again, which calls `StartGame`, then `ResetGame`, then `PauseGame`) is destroyed only at the next `clearDestroyed`, so it
  is safe (WS 5).
- **Animation.** `StartGame` and `ResumeGame` call `startWindowAnimation(id(), 10, true)`. `PauseGame` and OVER call
  `stopWindowAnimation`. `onAnimate` is one `stepOnce` and returns true.
- **Natives** (`FactoryNatives.cpp`, registered at boot): `StartGame`, `PauseGame(key or "")`, `ResumeGame` and
  `GetScore` call `active()`. The same file registers the window types `ChocolateFactory`, `CoffeeFactory` and
  `RecipeDesigner`, and calls `registerStubNatives` (§3.7).
- **Audio.** Pausing pauses the CADI voices and music once the game has started, and resuming lifts it. A game whose
  script pauses its own music turns that off (`pauseAudio`).

### 3.4 `ChocolateFactory : FactoryWindow` (`factory/Chocolate.*`)

Everything the constructor reads or builds is plain data in `ChocolateSpec`: `readSpec(style)` fills it with Chocolatier 3's
keys and pictures, and a subclass for another game returns its own spec (§9). Parts, in declaration order after the base:

- `conveyor_` (an `IngredientQueue`).
- `Gun`: `{ScalingTask clock; double flightMs; Sprite* sprite; gfx::PointF pos; std::vector<Flight> flights;}`, with the
  sprite on `image/gun.xml`, layer 10.
- `TrayTask`: `{ScalingTask clock; double lapMs, speedup, maxSpeed; gfx::Curve path; std::vector<ChocolateTray*> list;}`.
- `std::optional<Recycler>`: the clock, a lap, a path and the bin sprite.
- `Rings`: the clock, a flight time and a list of `{sprite, curve, t}`.
- `Belt products_`: the clock, a trip time, a path, the product's appearance layers and a list of `{sprite, t}`.
- The machine, its two fingers, the tray template on the machine, and the box at the belt's end.

`ChocolateTray` is a layer-5 sprite `image/tray<N>` with debug name `tray:<i>` (CF 6). It holds the recipe, `slots`
(one per recipe entry), `fill`, `color`, `offset`, `endpoint`, `last` and `radius` 75, and the ring, sparkle and
telescoping piston children. `slotOffset(i, n)` is `(sin(2πi/n)*50, -cos(2πi/n)*50)`; `canAccept` ignores colour;
`crossed(u)` is the wrap-aware `[last, u]` test against `endpoint`; `setColor` recolours the slots and ring; `clear` removes
the slots and plays the sparkle `DoAnim`.

Pure rules, unit-tested alone: `trayColorAfter(tray, ing, &recolourIngredientTo)` (CF 6.3 step 2), `speedAfterFull`
(`scale * speedup`, capped at `maxspeed`), `speedAfterMiss` (halved, never below 1) and `chooseTray` (§8).

`hitTray(tray, ingredient)` does CF 6.3 in order:

1. Reparent the ingredient into the slot, then apply the colour rule.
2. If the tray is now full: speed-up, then the sound name. For a colour match also start the rings, `produce()` and
   `clear(true)` at once.
3. Otherwise post the step sound.

The tray never calls the window. The recycler and rings follow CF 8 and CF 7. A shot is tested against the trays in
**index order** with `segmentHitsCircle(old, new, tray centre, radius + 16)` (hit radius 91 at the default 75), and then
the recycler with a point test at 88. The original iterated a set ordered by pointer, which is arbitrary, so which of two
overlapping trays takes a shot may differ.

### 3.5 `CoffeeFactory : FactoryWindow` (`src/games/choc3/factory/Coffee.*`)

Parts, in declaration order after the base:

- `conveyor_` and `gun_`, as in chocolate.
- `CoffeeGrid grid_`: `cols`, `rows`, `dx`, `dy`, the scrolled top-left `pos`, `slots` (`{name, ing}` per cell, row-major),
  a `ScalingTask` whose scale is the scroll speed, and `previousT`. The scroll phase is not reset, because the original
  never resets it either (factories-questions §9.3). Seams and the target marker are sprites on the window.
- `Hopper hopper_`: one counter and needle per recipe entry.
- `Collection collect_`: the flights of cleared ingredients to the hopper.
- `CaseBelt cases_`: the clock, a trip time, a spacing, a path, the case appearance and a list of `{sprite, t}`.
- `popups_` (`Bonus`: a `TextSprite`, a start point and an age) and `matchBonus_`, with 0 prepended.

The pure rules are on `CoffeeGrid`, `Hopper` and free functions, and are unit-tested without sprites where possible:

- `slotIndex(p)` uses the grid's current position; right and bottom edges are exclusive.
- `countHorizontal` and `countVertical`.
- `floodClear(idx)` returns the removed slots in the original's order: left, up, self, right, down.
- `shiftRows(rng, recipe, spawn)` moves every row down, wraps the bottom row to the top, empties it (returning the fallen
  ingredients, which the window removes with no miss counted) and refills it: per column `RandRange(0, 2N)`, an entry of
  the recipe when below N, else a free cell.
- `Hopper::add(name)` gives the entry with the smallest count, ties to the later entry. `Hopper::release()` is all or
  nothing. `needleY(c)` is `24 - 4.8*min(c, 10)`.
- `matchBonusFor(table, n)` reads the table with 0 prepended, clamps `n` past the end and gives 0 for an absent table.
  `caseSpawnReady(lastT, spacingMs, tripMs)` and `chooseSlot` (§8).

`collected(list)` is CO 1.11 message 1010:

1. Multiply the grid scale by `speedup`, capped at `maxspeed`.
2. Look up the bonus; if it is not 0, add it as bonus score, play `ingredient_match_<min(b,5)>` and make a popup (it rises
   150 px over 2000 ms, factories-questions §9.2).
3. Move the list to the collection flights, which aim at the hopper position minus 75 in x, in grid-local coordinates.

The target marker (a 0.5-alpha preview of `peek()` over the slot under the cursor) follows the mouse while PLAYING only
(factories-questions §5).

### 3.6 `RecipeDesigner : ui::Window` (`src/games/choc3/factory/RecipeDesigner.*`)

- **`setRecipeType(category)`** lists `custom/<category>/` sorted (the original's `GetNextFile` order, factories-questions
  §7), skipping `_highlight` and non-`.png` files, and files each name by `layer1..4` (LG 10.3). Layers 2 and 4 get a
  trailing `""` (none), layer 3 too unless the category is `beverage`. The indices and child windows are not touched, so
  `GetLayerImage` still matches what the windows show.
- **`setLayer(layer, index)`** stores `(index + size) % size`, updates the four child windows and returns the index
  applied. An empty list or a bad layer changes nothing and gives 0. **`layerImage(layer)`** is the entry minus
  `custom/`, `""` for none.
- **`randomDesign()`** sets the timer to 0, plays `randomize` and starts a 100 ms window animation (no catch-up).
  `onAnimate` is the CO 5 / LG 10.3 tick: it adds 100 ms, ends at 2000 ms, and rolls layer 1 while the timer is at most
  1000, layer 2 up to 1500 and layer 3 up to 1800, always layer 4. Each roll is `setLayer(l, RandRange(lo, size-1))` with
  lower bounds 0, 1, 2, 0, plus one Lua string `SetRecipeTint(l, RandRange(1,100));...` run through
  `sink->runString`, which injects into `Main` while it waits, like `DoLuaString`. Nothing is touched after the call.
  With the shipped `custom/bar` that is 19 rolls.
- **Child lookup.** `setLayer` finds `option_layer<n>` (BitmapTint), `option_highlight<n>` (Bitmap), `display_layer<n>` and
  `display_highlight<n>` (BitmapTint in Reforged) with `find(name)` under the designer. It calls `setBitmap(image, 0.5)`
  for the option windows and `setBitmap(image, 1.0)` for the display windows; the second argument is a **scale**
  (factories-questions §6; LG 10.3 calls it alpha, wrongly). An empty entry hides all four windows. A missing highlight
  hides only the highlight windows. The designer keeps the loaded textures alive in `keep_` while it lives.
- **Natives.** All four do nothing with no designer, and log once: `SetLayer` returns 0 and `GetLayerImage` returns `""`.
  The original set them to nil, which would have been a Lua error.
- **`BTSetTint(name, t)` and `SetRectangleColor(name, t)`** do a recursive lookup under the top modal and need the right
  type (`BitmapTintWindow::setTint`, `RectangleWindow::setColor`). Missing components default to 1.0 for `BTSetTint` and to
  **0.0** for `SetRectangleColor`, so a colour without alpha is invisible.

### 3.7 Other registered windows

| Item | Behaviour |
|---|---|
| `SlotMachine` + `StartSlots/StopSlots` | Implemented (`SlotMachine.*`, docs/re/slot-machine.md). `StartSlots(n1..n4, payout)` draws, per wheel in order, 20 to 50 steps and a speed of 90 to 110 from `ctx.rng`, and starts a 50 ms catch-up animation. Each tick adds `speed` to the wheel's phase; at 200 the four symbol names shift down and a new top one is drawn, and the last two steps put the target in the middle. When the last wheel stops, `Stop_machine_spin` plays and `onstopped` runs (the Lua pays `Floor(bet * payout + 0.9)`). A payout above 0 then runs the dollar sparkle (docs/re/particles.md 4) until `gSparkleTime` is used up, and only then does `onpayout` run; no payout runs it in the same tick. Both are run with a protected call through the script sink, and `this` is not touched after `onstopped`, which may close the dialog. `StopSlots` cuts the first spinning wheel to 3 steps. Deviation: between steps the strip drifts by `phase * 60 / 200` in strip units (scaled by 0.425); the original adds window pixels and jumps back. The Lua draws the mask over the wheels, so the window does no clipping |
| `SignEditor`, `CurveEditor` | Placeholder windows that draw their children. `SignEditor` is unreachable in the shipped data and `CurveEditor` is a dev tool (CO 6, 7), so their natives (`SetBackground`, `SetAnimScale` and the others) stay the generated logged stubs, and third-party content gets a logged placeholder instead of a crash |
| `RecipeCreator` | Not registered: no Lua constructs it (CO 4), so it would be the generic placeholder |
| Particles and glitz | `registerParticleNatives` (docs/re/particles.md). The factories use none (CF 11) |
| `CratesGame` | A separate window (`src/games/common/Crates.*`, `CratesNatives.cpp`), with its own scenario and unit tests; it is not part of the factory code |

## 4. Order of work

**Per frame**, the GW 4 order with two additions:

```
1  input -> wm.handleInput                          // onMouseMove aims; onMouseDown fires (pops the conveyor now)
3  wm.tickWindowAnimations(now)                     // factory: N x onAnimate, N = due 10 ms steps (catch-up)
4  world.tick(now): ... factory: frame(now) = root_->tickScripts(now); resortDirty()   // gun, sparkle and recycler DoAnim
```

**Clock mapping.** `FixedStep` advances `next` by exactly 10 ms per step, not from `now`. With headless frames at
`k*1000/60` ms, frames run 1, 2, 2, 1, 2, 2 steps, and after T ms of modal clock the step count is `floor((T -
start)/10)` (with the 1e-6 epsilon). The count depends on the clock only, so runs are deterministic and match the
original at any frame rate. In real time, a stall longer than 1 s resyncs instead of running 100 steps. The modal clock
pauses under a pushed modal, so `HelpDialog` does not eat game time.

**One chocolate step** (`ChocolateFactory::stepOnce`; the order is the original's, factories-questions §1):

1. `endRow`: the ENDING or ABORTED row. ENDING waits for rings, flights, products and full trays; then the tray scale
   decays by 0.95 per step and the game goes to OVER (`showGameOver`) when it falls below 0.05.
2. `stepClock` (CF 4.2 COUNTDOWN and PLAYING rows).
3. Tick the parts, each by 10 and only if not paused: recycler, trays (then the all-full bonus), conveyor, gun (shots: tray
   hits, then the recycler, then dying at `t == 1`, which calls `missed`), rings, products.
4. The machine angle (CF 4.3) and the box.

**One coffee step** (CO 1.6):

1. `endRow`: ABORTED goes to OVER. ENDING goes to OVER when there are no flights and no cases.
2. `stepClock`.
3. Tick the parts: conveyor, gun, case belt, collection, grid.
4. The popups.

## 5. Scoring and production

- **Chocolate:** `score = P + R`.
  - `P` counts products reaching the belt end (`t >= 1` on `productpath`, 3000 ms).
  - `R` counts rings arriving at (689, 555), 500 ms; `bonus_ = R`.
  - A plain full tray makes 1 product when it crosses `trayend`, which is a manufacture plus a produce.
  - A colour-matched tray (colour neither 0 nor 4) makes 1 product at once, plus one ring per slot: `N + 1`.
  - All trays full at once: each tray is cleared and makes 1 product, with no rings.
  - Bars (`colorcount` 1) never colour-match, so b01 scores 1 per filled tray.
- **Coffee:** `score = C + Σ matchBonus[min(n, size-1)]`.
  - `C` counts cases reaching the end of `productpath`.
  - A case starts when the previous case has `t > productspacing/producttime` (Reforged 1000/3000) **and**
    `hopper.release()` takes one of every recipe entry.
  - `n` is the size of the connected group cleared. The table is `{0, 0,0,0,1,2,2,3,3,4,5}` (0 prepended), so groups of
    1 to 3 add 0, and 10 or more add 5. `bonus_` is the bonus sum.
- **After `CloseWindow(score)`** (Lua, unchanged):
  1. `count = score`, then `SetProduction(product, count)` sets `info.production = count`.
  2. `TickSim(1)` runs `RunFactories`: `produce = min(count, min over needs of floor(have/need))`. Each ingredient
     loses `need*produce`.
  3. `Player.products[code] += produce`, through `AdjustInventory`.
  4. Cancel gives -1, so nothing changes.

## 6. Input

- **`onMouseMove(p)`.** In any state, `gun.sprite.rotationDeg = gunRotationDeg(p, gun.pos)`. Coffee also moves the target
  marker (PLAYING only). Mouse events that the overlay or a button does not handle fall through to `FactoryGame` behind
  them (WS 5). It returns false.
- **`onMouseDown(p)`.** Only when PLAYING and not `paused_`: `fire(p)`. It returns the window's `swallow_` flag, which is
  off, so the press goes on to the Pause button behind the window.
  - **Chocolate** pops and flies 1000 px towards `p` in 250 ms.
  - **Coffee** gets the slot under `p`. An empty slot is aimed at its centre, with `distance = |centre - gun|`;
    otherwise the shot flies 1000 px. When `pop` fails, the click is lost and nothing else happens.
- **Escape.** `onKeyDown` pauses with `game_paused` unless already paused or the game is over, and returns false. The
  modal's `onChar` then presses `cancel` if one exists (factories-questions §4).
  - On the intro, that is Cancel, so `CloseWindow(nil)`.
  - On the game-over screen, `done` carries `cancel = true`, so Escape closes with `CloseWindow(score)`.
  - On the pause overlay there is no cancel button, so Escape does not resume; only OK does.

## 7. Drawing

- `draw` paints the timer dial (the face and hand), which sits under the Text children so the score shows on it. The Text
  children (`score`, the "Cases" label, `countdown`) draw next. Then `postDraw` draws `root_` at the window origin with the
  window's alpha, so the sprites fade with the dialog, and draws the countdown digit again above them (§2.7). All
  positions are window-local, at (0,0) and 800×600.
- **Static sprites** are built in `init`: machine, fingers, tray template with icons, boxes, gun base and gun, timer.
  Coffee adds the hopper, meters, masks and seams.
- **Animated sprites** use sheets with `play("DoAnim")` through a `SpriteContext`, as `PortWindow` does: `image/gun.xml`,
  `image/powerup.xml` (starting on its last frame) and `image/recycler.xml`.
- **Text** is the existing `Text` windows for score and countdown (`setLabel` with `%u`, `"3".."1"`, `""`). The `time`
  text is `m:ss` when that child exists. The coffee popup is a `TextSprite` (`fonts/fertigo.mvec`, 80, white, `+<n>`).
- **Debug names** for `expect-sprite` and the dump: `ingredient:<name>`, `tray:<i>`, `product`, `ring`, `gun`,
  `recycler`, `missed:<name>`, `grid`, `cell:<name>`, `case`, `hopper`, `meter:<name>`, `bonus`, `marker`, `timer`.
- **`detail()`** gives one line, for example: `state=2 paused=0 clock=41230 score=7 bonus=3 misses=1 cursor=gun_aimer`,
  followed by the subclass's part.
  - Chocolate adds the tray speed, shots, per-tray fill and colour, and the product and ring counts.
  - Coffee adds the grid speed, shots, cases, the hopper counters and the grid fill and row phase.

## 8. Harness additions (`app/ScriptDriver`)

- **`autoshoot <ms>`.** For the next `ms` of clock, on every frame where `WindowQuery::autoAim()` gives a point, inject
  `move`, `down` and `up` there, in screen coordinates. The query asks the shown windows of the top modal for
  `autoAim()` and adds `screenPos`. A plain `click` cannot work here: the targets move, and the script is static text.
  The policies are pure functions, unit-tested, and used only by `autoAim`. The window answers only when PLAYING, not
  paused, `conveyor.ready()` holds and no shot is in flight, so a shot cannot be wasted on a slot another shot is about
  to fill.
  - **Chocolate** (`chooseTray`): the current centre of the tray that accepts `peek()` with the highest `fill`, preferring a
    tray whose colour equals the ingredient's colour or is 0, with ties to the lowest index. At scale 8 a tray moves at
    most about 30 px during a 100 ms flight, which is well inside the 91 px hit radius.
  - **Coffee** (`chooseSlot`): the empty slot in rows 1 to rows-2 (never the bottom row, which a shift can push off the
    grid) with the longest run of `peek()`'s name through it, horizontal or vertical. Ties go to the lowest index. There is
    no answer when the grid phase is above 0.9.
- **`expect-dump <timeout-ms> <text>`.** It waits until `dumpWindows()` contains `text`; for example `expect-dump
  20000 state=5` for game over, which is filled into a sibling window rather than shown as a new modal.
- **Existing tools.** `lua` sets things up and captures results, `expect-lua` checks `Player`, `dump` reads `detail()`,
  and `click` hits the overlay buttons (their coordinates come from one `--dump-windows` run, hard-coded as
  `check_crates.py` does).

## 9. Games built on these parts

- **Chocolatier 2's `FactoryGame`** (`src/games/choc2/FactoryGame.*`) subclasses `ChocolateFactory` and returns its own
  `ChocolateSpec` from `readSpec`: the `ChocolateSpec` fields for other pictures, shot flight out of the window, hit
  radius, product and ring rules, and `FactoryCommon::choc2Misses` for its miss rules. Every default is Chocolatier 3's.
  See [choc2-minigames.md](choc2-minigames.md) 2.1 and 2.2 and `docs/re/choc2-factory-binary.md`.
- **Chocolatier 1's `FactoryGame`** (`src/games/choc1/FactoryGame.*`, `FactorySim.*`) is its own window, not built on
  these classes (`docs/re/choc1-factory.md`); it reuses only the global names.
- **`CoffeeFactory`, `RecipeDesigner`, `SlotMachine`** exist only in the Chocolatier 3 module.

## 10. Tests

**Unit tests** (`tests/test_factory.cpp`, `tests/test_recipe_designer.cpp`; doctest, no Lua unless noted):

- **`FixedStep`:**
  - Frames at `k*1000/60` give exactly 100 steps by t = 1000 ms.
  - A 1500 ms gap resyncs and does not run 150 steps.
  - `stop` stops it.
  - In `WindowManager`, an `onAnimate` that stops its own animation ends the catch-up loop.
- **`Sprite::release` / `removeChild`:** the pointer stays valid; the subtree goes with `removeChild`.
- **`ScalingTask`:** `animateDelta` across a wrap; scale changes mid-run; `reset` and `restart` leave no jump; a paused
  `tick` does nothing.
- **Queue:**
  - A bag of `5×size` names with 5 copies of each.
  - Colours lie in `[rainbow?0:1, colorcount]` and are not assigned when the count is 1.
  - The first `ready()` comes after 120 steps and the next after 24 more.
  - `pop` drops the head to 0.8.
- **Shots and collision:**
  - `Flight` moves 40 px per step and returns `u == 1` at step 25.
  - The coffee `u` reaches 1 at `distance/4` ms.
  - `segmentHitsCircle` for a tangent, a pass-through and a near miss at 91.
  - The recycler eats a shot with a point test at 88, counting neither a hit nor a miss.
- **Trays:**
  - The slot offsets for N = 2 and 3.
  - The `trayColorAfter` table (0 + c gives c; c + 0 recolours the ingredient; c + d gives 4; 4 + anything stays 4).
  - The crossing test across a wrap.
  - Full plain gives a product on crossing; full matched gives a product, N rings and an immediate clear.
  - The speed is 1.1^k capped at 8, halves on a miss and never drops below 1.
  - Full board: 5 products and no rings.
- **State machine and scoring:**
  - The countdown lasts 299 steps, then play lasts 6000 steps and ends in ENDING.
  - A scripted model run with an injected `Rng`: 2 plain trays and 1 matched N = 3 tray give `score == 2 + 4` after
    the belts drain.
  - ENDING waits for flights, rings, products and full trays, then decays 0.95 per step to OVER.
  - A first miss pauses with `choco_first_missed` when `warnOnMiss` is on; the 8th miss aborts.
  - The 240 ms gun cooldown, and parking of a missed shot.
  - A click while paused fires nothing.
  - Escape pauses while playing but not on the game-over screen.
  - PauseGame pauses the CADI audio once the game started, and a factory destroyed while paused unpauses it.
- **Coffee:**
  - `slotIndex` at the edges (right and bottom exclusive).
  - `floodClear` takes the whole connected group, not just the line of 3.
  - `shiftRows` with a fixed `Rng`: the fill odds come from `RandRange(0, 2N)`, and the fallen ingredients are removed
    with no miss counted.
  - The `matchBonus` lookup with 0 prepended, past the end and with the table absent.
  - `Hopper::add`'s tie rule, `release` being all or nothing, and the needle height.
  - The case spawn spacing.
  - Resume in PLAYING resets the grid scale (the kept quirk); Try Again leaves no cases (the fix).
  - The grid is paused through the countdown; focus change on the game-over screen still pauses.
- **Appearance:** a `{code="b01"}` table gives `items/b01_big`. `{ {"bar/layer1_01", .5, .2, 0}, "bar/layer2_03" }`
  gives tinted then white layers. A product table's `appearance` list wins over its `code`.
- **Policies:** `chooseTray` and `chooseSlot` on hand-built states, and `autoAim` answering only when a shot can be placed.
- **Lifetime (ASan):**
  - Destroy the factory mid-flight, with ingredients on trays, parked, and in the reserve.
  - Try Again from the game-over button: the button's own window is destroyed by `FillWindow` while its command runs.
  - Close the dialog during the countdown.
- **`RecipeDesigner`:** `AssetFS::list` is sorted; `setRecipeType("bar")` against the real `custom/bar` (layer1 has 4
  entries and no blank; layer2 has 7 + 1); `setLayer` wraps at -1 and at the size; `layerImage` gives `bar/layer1_01`;
  `setLayer` shows the pictures with the highlight only where the file exists; `randomDesign` makes 19 rolls and stops at
  2000 ms, with the Lua strings recorded by a fake sink; the natives do nothing without a designer; `BTSetTint` and
  `SetRectangleColor` find the named window under the top modal.

**Scripted real-data runs** (CTest `reforged_factory_chocolate`, `reforged_factory_coffee`, both `check_factory.py --game
chocolate|coffee`; `reforged_recipe_designer`, `check_recipe_designer.py`; `reforged_slotmachine`, `check_slotmachine.py`):

- **Setup**, after the `check_crates.py` intro:
  `lua f = zur_factory; f:MarkOwned(); p = _AllProducts.b01; Player.rank = 1; for k, n in pairs(p:GetNeeds()) do Player.ingredients[k] = 1000 end; gBefore = Player.products.b01 or 0`.
  Rank 1 turns the first-miss tutorial pause on. Coffee also runs `f:Equip("beverage")` and uses `p = _AllProducts.c01`.
- **The run:**
  1. `lua gRun = p:RunMinigame{factory = f}; if gRun >= 0 then f:SetProduction(p, gRun); TickSim(1) end`.
  2. `expect-window ui/chocolate_factory.lua` (or `ui/coffee_factory.lua`); click Go.
  3. `wait` to 4.3 s; one deliberate miss with `click`; `expect-dump` for `paused=1`; click OK.
  4. `autoshoot 62000`; screenshot mid-run.
  5. `expect-dump state=5`; `dump`; click Done.
- **Checks:**
  - `expect-lua` that `gRun` equals `GetScore()`, is at least 1, and `Player.products[p.code] == gBefore + gRun`.
  - `expect-lua` that each need's ingredient equals `1000 - need*gRun`.
  - The checker parses the final `detail()`. Its `score` equals the `RUN` line, and `misses=1`.
  - Chocolate: at least one `product` was counted (`score - bonus >= 1`).
  - Coffee: `cases >= 1`, so the hopper path ran, not only match bonuses.
  - The `--log-unimplemented` file names only sound, cursor and button-sound stubs.
- The same seed gives the same score on two runs; the checker runs one of them twice (`--twice`).
- **Slot machine** (`check_slotmachine.py`): `DisplayDialog{"ui/ui_slotmachine.lua", bet=100}`, four lever pulls on three
  seeds; the money changes by exactly `Floor(bet*payout+0.9) - bet`, `ok` appears only after `onpayout`, and the reel
  window differs between the idle and mid-spin screenshots.
- **`RecipeDesigner`** (`check_recipe_designer.py`): the kitchen's design mode driven headless against Reforged.

## 11. Choices and open points

- **`autoshoot` is test logic inside the engine.** It is small and pure, but it proves the pipeline, not human
  playability. The geometry unit tests and the screenshots cover aiming by hand.
- **Run time.** A run is about 4,000 headless frames, about 65 s of simulated clock. Do not shorten `gametime`, since the
  Lua fixes it.
- **Tray hit order** (index instead of pointer order) is harmless but not bit-exact (§3.4).
- **Doubles instead of the original's floats** in `ScalingTask` may move a crossing by one step at the 30 s lap.
- **Open in the RE notes:** the unknowns listed under "Unknown" in `docs/re/factories-questions.md`.
