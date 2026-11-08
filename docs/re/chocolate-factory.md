# Chocolate factory mini-game (`ChocolateFactory`)

Scope: how the native `ChocolateFactory` window and its helper classes work, what the Lua around it supplies and consumes, and what a port must reproduce so that "a scripted factory run produces goods, and they arrive in inventory" (Phase 4 of the original workbench plan). The companion table is `docs/re/chocolate-factory.tsv` (natives, messages, DSL keys, game-state keys, sound events).

Sources, in trust order: the decompiled Mac binary (`re/mac/decomp/ChocolateFactory.c`, `ChocolateGunTask.c`, `ChocolateTray.c`, `ChocolateTrayTask.c`, `IngredientQueue.c`, `IngredientQueueTask.c`, `Ingredient.c`, `ChocolateProduct*.c`, `Recycler.c`, `RecyclerTask.c`, `FlyingRing*.c`, `FlyingIngredient.c`, `TScalingTask.c`, `TCurve.c`, `TAnimTask.c`, `TSoundHandler.c`, `_free/l.c` and `_free/b.c` for `LoadAppearance` / `BuildItemAppearance`), then the Chocolatier 3 Mac app bundle's `Contents/Frameworks/*.h`, then the Lua in the original install's `assets/` (the Reforged copy differs only cosmetically, see 12). All addresses are Mac-binary virtual addresses, hex, as in `re/mac/index.tsv`. Float constants are decoded from the IEEE hex in the decompile.

Read first: `api-globals.md` (one-line mentions), `window-system.md` (message routing, `TAnimTask`), `ledger-and-game-natives.md` (`TGameState`, weekly production display). Not repeated here.

Findings that surprise:

* The mini-game's **only output is one integer**: the score (cases per week). Lua does `CloseWindow(score)`; `Product:RunMinigame` turns it into the factory's weekly production. There is no formula from "performance" to quantity beyond "score = number of cases counted" (section 8).
* **No Lua callbacks.** `onstopped` / `onpayout` belong to the slot machine (`ui/ui_slotmachine.lua`), not the factory. The factory native calls Lua only by loading two UI scripts into the `information` child window (`ui/game_pause.lua`, `ui/chocolate_gameover.lua`) and, with a cheat config flag, `DoModal('dev/dev_menu.lua')`.
* **The particle system is not used** by the chocolate factory (section 11). The sparkle on a cleared tray is a sprite animation (`image/powerup.xml`).
* `traycount` is **never passed by the Lua**, although `categories.lua` defines `traycount = 7`. The native default of 5 applies: five trays. Likewise `speedup` and `maxspeed` are never passed, so the defaults 1.1 and 8 apply.
* The powerup ingredient classes (`ChainIngredient`, `EnlargeIngredient`, `SuperColorIngredient`) exist in the binary but nothing creates them (section 9.3).
* A shot has **no cooldown of its own**; the rate limit is the conveyor refilling (section 5.3).

## 1. Cast and object model

`ChocolateFactory : TBSGWindow` (window type name `ChocolateFactory`, `AddWindowType` entry in the startup list from `api-globals.md`; `ChocolateFactoryDynamicCreate` 0x93d8). Single static `pInstance`. Helper objects, all created in `Init`:

| Object | Type | Role | Member offset in window |
|---|---|---|---|
| base sprite | `TSprite` | root of every game sprite, drawn in `Draw` after the window's own children | +0x68 |
| `pConveyor` + `pConveyorTask` | `IngredientQueue` + `IngredientQueueTask` | ingredient train, feeds the gun | +0x74, +0x7c |
| `pGunTask` + `pGun` | `ChocolateGunTask` + `TAnimatedSprite` (layer 10, `image/gun.xml`) | fires ingredients, tracks flying ones | +0x80, +0x84 |
| `pTrayTask` | `ChocolateTrayTask` (+ 5 `ChocolateTray`) | the moving trays; its scale is the game speed | +0xb0 |
| `pProductTask` | `ChocolateProductQueueTask` | finished goods riding a belt to be counted | +0xb4 |
| `pRecyclerTask` | `RecyclerTask` (+ 1 `Recycler`), only if `recycler = true` | bin that eats shots harmlessly | +0xb8 |
| `pRingTask` | `FlyingRingTask` | colour-match bonus rings | +0xbc |
| `pSoundHandler` | `TSoundHandler` | turns message type 0x3ea into a sound event | +0x70 |
| timer face / hand | `TSprite` x2 | analogue clock | +0x104, +0x10c |

All tasks are `TScalingTask` subclasses. States: `GAME_INIT 0, GAME_COUNTDOWN 1, GAME_PLAYING 2, GAME_ENDING 3, GAME_ABORTED 4, GAME_OVER 5` (header order, confirmed by the switch in `SetGameState`).

Sprite layers (from `chocothree.h`): product 2, recycler 3, ingredient on conveyor 4, tray 5, machinery 6, flying ingredient 8, gun 10, flying ring 15, pistons -100.

## 2. Clock and tasks

### 2.1 Fixed 10 ms logical step
`StartGame`/`ResumeGame` call `StartWindowAnimation(10, true, false, true)`: delay 10, repeating, not "from now", **catch-up on**. `TAnimTask::DoTask` (0x813fe) runs the callback `OnTaskAnimate` repeatedly until its next-due time passes the real clock, advancing the due time by 10 each call; if more than 1000 ms behind (difference >= 1001) it resyncs to "now" and runs **exactly one** step (next due now+10) instead of catching up; otherwise it runs `max(1, ceil(lateness/10))` steps against the clock value sampled on entry. The clock is the parent modal's `TClock`, which is paused while another modal is on top. First step: `StopWindowAnimation` abandons the task and `StartWindowAnimation` makes a new one due 10 ms after creation; `StartGame` calls it twice (once itself, once via `ResumeGame`, because `ResetGame` left the paused flag set), so the first step is due 20 ms after `StartGame` and 10 ms after a `ResumeGame`. See `factories-questions.md` section 2. **Consequence: game time is counted in 10 ms logical ticks, independent of frame rate.** One `OnTaskAnimate` call = exactly 10 game milliseconds. A port should run a fixed 10 ms step with an accumulator and a ~1 s clamp.

### 2.2 `TScalingTask` (0x347a2..0x34950)
State: `time` (scaled ms, float), `lastTime`, `scale`, `taskClock` (unscaled ms, integer), `lastClock`, `paused` (starts true).
* `Tick(ms)`: if not paused, `taskClock += ms`, then virtual `Animate`.
* `Animate`: `lastTime = time; time += (taskClock - lastClock) * scale; lastClock = taskClock`.
* `GetT(full) = time/full - floor(time/full)` (phase in [0,1)).
* `AnimateDelta(full)`: phase before, run `Animate`, phase after; returns `after - before`, or `(1 - before) + after` if it wrapped. This is "fraction of one `full`-length period that elapsed this tick".
* `Reset(s)`: `taskClock=0, paused=true, time=lastTime=0, scale=s, lastClock=0`. `Restart(x)` (argument ignored): `lastClock = taskClock; paused = false`. `Pause()`: `paused = true`.
* Overrides: `ChocolateTrayTask::Reset` and `RecyclerTask::Reset` do **not** call the base: they only set `scale` and `lastClock = taskClock`, clearing trays (no animation). `ChocolateProductQueueTask::Reset`, `FlyingRingTask::Reset` call the base. `IngredientQueueTask::Reset` empties the queue then calls the base. `ChocolateGunTask::Reset` removes flying sprites then calls the base.

Each `OnTaskAnimate` ticks, in this order, every task by 10: recycler (if any), tray, conveyor, gun, ring, product. Tasks that are paused do nothing.

### 2.2.1 Order of one `OnTaskAnimate` step (verified, 0xc180)
1. Not foreground: `PauseGame("game_paused")`, return (nothing else runs).
2. End states **first**: state 4, or state 3 once ring set, flights, product list are empty and no tray is full: `tray.scale *= 0.95`, and `SetGameState(5)` if that would fall below 0.05.
3. Clock by state (COUNTDOWN / PLAYING blocks in section 4); transitions (`SetGameState`) happen here, before the ticks.
4. Task ticks (10 each) in the order above.
5. Machine angle (`machineTime`, 4.3), computed after the ticks from the new value.
6. `InvalidateScreen`, return 1.
The design's "clock, end states, machine, tasks" order is wrong. Details and the coffee variant: `factories-questions.md` section 1.

### 2.3 Curves (`TCurve`)
Every path in the DSL (`conveyorpath`, `traypath`, `productpath`, `recyclerpath`) is a list of `{x,y}` points read by `GetPoint` (0x3966: elements 1 and 2). They are **cubic Bezier control points in groups of 3n+1** (segment k uses points 3k..3k+3). `GetEndPoint` = control point index `((n-1)/3)*3`. The curve is flattened by recursive subdivision until `hull length - chord length <= 0.2` (the static `_ACCEPTABLE_ERROR` is 0.2f, read from the data section), and `GetPoint(t)` is **arc-length parameterised**: `t<=0` start, `t>=1` end, otherwise `t*totalLength` is walked along the flattened pieces with linear interpolation. Rings use a 4-point curve `(a, a, b, b)` (start, start, end, end). Path point counts in the shipped data: bar tray 16, exotic tray 13, conveyor 7, product 4, recycler 10; all are 3n+1.

## 3. The `ChocolateFactory` window

### 3.1 DSL keys (read in `Init`, 0x9414, via `TWindowStyle`)
Numbers are doubles converted to unsigned with clamping; defaults are used when the key is missing.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `productName`, `productCode` | string | "" | stored, not used by the game logic (Lua passes them) |
| `recipe` | table of strings | empty | ingredient names, one entry per ingredient instance (duplicates allowed), `ParseRecipe` (0x81a4) |
| `appearance` | table | none | product picture: see 3.4 |
| `machine` | string | "" | texture for the centre machinery sprite (layer 6) at (695,301) |
| `countdowntime` | ms | 3000 | pre-game countdown |
| `gametime` | ms | 60000 | playing time |
| `warntime` | ms | 8000 | whistle when the clock crosses this; Lua passes 10000 |
| `warnOnMiss` | bool | false | pause with `choco_first_missed` on the first miss |
| `conveyortime` | ms | 1200 | time for the train head to travel the whole conveyor curve at scale 1 |
| `conveyorcount` | uint | 5 | slots on the conveyor; spacing is `1/count` of the curve |
| `conveyorpath` | table | (none: no curve, nothing moves) | |
| `colorcount` | uint8 | 1 | number of ring colours in play (1..3 used) |
| `rainbow` | bool | false | allow wild (colour 0) ingredients |
| `producttime`, `productpath` | ms, table | 3000, none | belt for finished goods |
| `speedup` | number | 1.1 | tray speed multiplier per completed tray (Lua never passes it) |
| `maxspeed` | number | 8 | cap on tray speed scale (never passed) |
| `traytime` | ms | 30000 | one lap of the tray path at scale 1 |
| `trayend` | number | 0 | curve parameter of the "machine" point where full trays deliver (never passed) |
| `traycount` | uint | 5 | number of trays (never passed by Lua, so 5) |
| `traypath` | table | none | |
| `gunspeed` | ms | 250 | flight time of a shot over 1000 px |
| `ringspeed` | ms | 500 | flight time of a bonus ring |
| `missed` | table of `{x,y}` | none | one point per "wasted" slot; its length is the number of misses that end the game (8 in the shipped Lua) |
| `recycler` | bool | false | build the recycler |
| `recyclerpath`, `recyclertime` | table, ms | none, 20000 | bin path (needs the table) and lap time |

Child windows looked up in `PostChildrenInit` (0xd2e0) by name: `time` (Text, absent in the shipped Lua so the clock text is never shown), `countdown` (Text), `score` (Text). The `go` and `pause` buttons and the `information` overlay are **siblings** found through the parent (`this+4`), not children. `PostChildrenInit` also centres the timer face on the `score` text rect (falls back to (720,80)) and calls `ResetGame` and `SetAlpha(1.0, true)`.

### 3.2 `Init` order (what it builds)
1. `TWindow::Init`; `SetTopModalOnly(true)`; **`RandSeed(TPlatform::Timer())`** (global PRNG reseeded from the ms timer at every window open); the window becomes the default focus of its modal (so unhandled messages sent to the modal reach `OnMessage`).
2. Read `warnOnMiss`, `countdowntime`, `gametime`, `warntime`; `ParseRecipe`.
3. Conveyor: `IngredientQueue(recipe, colorcount, curve(conveyorpath), conveyorcount, base)`, task with `conveyortime`; `rainbow` written to the queue's `allowRainbow`.
4. Product belt: `LoadAppearance` fills the layer-name and tint vectors; `ChocolateProductQueueTask(curve(productpath), producttime, names, tints)`.
5. `speedup`, `maxspeed`, `traytime`, `trayend`, `traycount`; one `ChocolateTrayTask`; for `i` in `0..traycount-1` a tray is created with phase offset `i / traycount` and end parameter `trayend`, added to the base sprite, given pistons, `Update(0)` called, inserted in the task's set.
6. Machinery sprites (all on the base sprite): `machine` texture at (695,301) layer 6; child `image/machine_stack_finger_upper` (anchor (41,14), offset (-74,-51)); child `image/machine_stack_finger_lower` (anchor (41,119), offset (-74,50)); a "tray template" sprite `image/tray<recipeSize>` at (0,-2) and, on it, the recipe ingredient icons `items/<name>` at the slot positions of 5.1 (this is the "full tray you see on the right" of the tutorial text); `image/box_open` (layer 1) and `image/box_open_mask` (layer 3) at the end point of the product curve, with the product picture (scale 0.5, offset (0,9)) between them.
7. Gun: position = end point of the conveyor curve. `image/gun_base` sprite plus `TAnimatedSprite` from `image/gun.xml` (layer 10), frame 0; `ChocolateGunTask(gun, gunspeed, conveyor, trayTask)`.
8. `FlyingRingTask(ringspeed)`.
9. `missed` points copied to a vector.
10. If `recycler`: needs `recyclerpath`; `RecyclerTask(recyclertime)`, one `Recycler` (offset 0) on the base sprite, and the gun's recycler pointer set.
11. Timer face `image/timer_face` with child `image/timer_hand` (hand rotated every tick, see 4.3).
12. Ends with `PauseGame("game_paused")`, which hides the Pause button and shows Go while the intro overlay (the `information` Bitmap, created later) is on screen.

### 3.3 What is drawn (`Draw` 0x8030)
Window children first (`TBSGWindow::Draw`), then the base sprite tree at the identity transform inside `TBegin2d`, so Lua-declared bitmaps (background, divots, stack base, score text) lie under the sprites, and the Text children are drawn with the window. Lua draws: `image/factory_background`, `image/factory_windows_<port>` at `(factory.windowX or 66, factory.windowY or 71)`, eight `image/missed_ingredient_divot` bitmaps (positions in 3.5), `image/machine_stack_base` at (591,47), the Go/Pause buttons, a "Cases:" label (20 pt, colour (255,1,17)), the `score` Text (50 pt, (255,41,77), box x=621 y=50 w=148 h=148), the `countdown` Text (200 pt red, box 150,150,300,300, initial label "3").

Machine picture chosen by Lua from the category: default `image/machine_1`; infusion `machine_3`; truffle `machine_4`; exotic `machine_6`.

### 3.4 Appearance
`LoadAppearance(table)`: if the table has a sub-table `appearance` use `LoadAppearanceInternal` on it; else if it has `code` (or `name`) use the single layer `items/<code>_big` tinted white; else `LoadAppearanceInternal` on the table itself. `LoadAppearanceInternal` walks entries: a string entry `s` becomes layer `custom/s` tinted white; a table entry `{name, r, g, b}` becomes layer `custom/name` with colour (r,g,b,1) (components 2..4 taken as-is). `BuildItemAppearance(sprite, names, tints, scale)`: removes children, scales the sprite matrix by `scale` if not 1, then for each layer adds a sprite (layer index counting up) tinted by its colour, followed by `<layer>_highlight` if that texture exists. Used with scale 0.5 for the box picture and for every belt product.

### 3.5 Divot / miss slots in the shipped Lua
Bitmaps at `(593,111) (599,75) (617,48) (645,29) (718,29) (747,48) (765,75) (771,111)`; `missed` points are those +13,+13 (bitmap centres). The ninth divot (682,22) is commented out. So **8 misses end the game**.

## 4. Game loop

### 4.1 Lifecycle
* Lua opens `ui/chocolate_factory.lua` via `DisplayDialog` (from `Product:RunMinigame`). `go`/`pause` are disabled by `EnableWindow` until the overlay is dismissed.
* Overlay buttons: **ok** `StartGame()`, **cancel** `CloseWindow(nil)`, help `HelpDialog("help_chocolate")`.
* `StartGame` (0xd1d6): `TGameState "ChocolateFactoryTime" = "restart"`, `"RecycleCount" = 0`, `ResetGame`, `StartWindowAnimation(10,...)`, `SetGameState(1)`, `ResumeGame`.
* `ResetGame` (0xcf02): `clock = gameTime + countdownTime; score = bonusScore = missCount = 0; state = 1`; every task `Reset(1.0)` (so the tray scale returns to 1, conveyor emptied, flying ingredients and rings cleared, trays cleared); `PauseGame("game_paused")`; removes the missed-ingredient sprites from the base sprite and empties the list; score text "0", time text ""; plays `chocolate_making_game_intro`; hides both Go and Pause buttons.
* `ResumeGame` (0xc82c) always sets the cursor to `cursor/gun_aimer` (hotspot 16,16). If currently paused: unpause sound, `ChocolateFactoryTime = 1`, hide Go, show Pause, hide `information`, `Restart` all tasks, restart the 10 ms animation, `SetGameState(current state)` (re-applies that state's entry actions, including its sound), `gamePaused = false`. Finally `SetTopModalOnly(true)`.
* `PauseGame(textKey)` (0x8384): cursor back to `cursor/cursor` (hotspot 1,1). If not already paused: pause sound if state != 0, `ChocolateFactoryTime = 0`, show Go / hide Pause, `gamePaused = true`, pause every task, `StopWindowAnimation`; if the `information` window exists, set global string `gPauseText = textKey`, load `ui/game_pause.lua` into it and show it. Always ends with `SetTopModalOnly(false)`. That flag only affects drawing (`TWindowManager::DrawLayer`: true = draw only the top modal, false = also draw the modals beneath); it does not gate input (`factories-questions.md` section 3). `PauseGame()` from Lua passes an empty string; `game_pause.lua` then falls back to `game_paused`. `ToggleForeground` and a non-foreground check at the top of every `OnTaskAnimate` pause with `game_paused`.

### 4.2 State machine
| State | Entry (`SetGameState`, 0xbc48) | Per tick |
|---|---|---|
| 1 COUNTDOWN | restart tray task and recycler; pause conveyor, gun, ring, product | `clock` falls by 10. While `clock > gameTime+10`: when the thousands digit of `clock` changes, `clock_tick` sound and countdown text `((clock - gameTime) + 999) / 1000`. Else: `start_bell`, `SetGameState(2)`, countdown text "". |
| 2 PLAYING | sound `chocolate_making_game`; restart all tasks (tray, conveyor, gun, ring, product, recycler) | if `clock <= 10`: reset hand to identity, time text "0:00", `SetGameState(3)`. Else `clock -= 10`; `whistle` the moment `clock` crosses `warntime` (`warntime < old && new <= warntime`); time text `ceil(clock/1000)` as `m:ss` (only if a `time` Text exists); hand rotation `-(2 * clock / gameTime) * pi`. |
| 3 ENDING | sounds `game_over`, `chocolate_making_game_outro`; pause the conveyor and `Reset(1.0)` it (queue vanishes); if any tray is full, tray scale = 5.0 | wait for: no ring flying, no shot flying, no product on the belt, and no full tray. When all hold, tray scale `*= 0.95` each tick; when it would drop below 0.05 -> state 5. Otherwise just keep ticking. |
| 4 ABORTED | cursor back; sounds `game_over`, outro; conveyor paused and reset; gun paused | tray scale `*= 0.95` per tick; below 0.05 -> state 5. |
| 5 OVER | cursor back, `SetTopModalOnly(false)`; pause trays, conveyor (reset), gun, rings, products, recycler; stop window animation; load `ui/chocolate_gameover.lua` into `information` and show it; hide Go and Pause | none |

Notes: the playing phase is exactly `gametime / 10 = 6000` ticks. In ENDING the gun keeps running, so shots in the air still resolve and can still fill trays; no new shots can be fired. In ENDING the ring, product and tray tasks keep running so queued goods and rings still count; in ABORTED the products and rings still run only until the trays have slowed to 0.05 (about 59 ticks from scale 1, `ln(0.05/s)/ln(0.95)` ticks in general, 590 ms at scale 1), then state 5 pauses everything: goods still on the belt at that moment are **not counted**. Deceleration time from scale 8 is about 99 ticks.

### 4.3 Machine animation (cosmetic)
Message 0x3ec sets `machineTime = 1`. While non-zero it advances 10 per tick and wraps to 0 when it reaches 400 (`if (t+10 < 400) t+10 else 0`); angle = `2*pi*machineTime/400`. The tray-template sprite is rotated by +angle, the upper arm by -angle, the lower arm by +angle (each keeping its translation). One revolution per 400 ms. A new 0x3ec while running is ignored (only the product is added). Sprite geometry (finger anchors, layers) and the piston art: `factories-questions.md` section 8.

## 5. Ingredient flow

### 5.1 Ingredient queue (`IngredientQueue`, 0x26160..0x272a8)
Members: `recipe` (names), `allowRainbow`, `colorCount`, `curve`, `queue` (vector, consumed from the back), `conveyor` (list, front = nearest the gun), `pistons`, `headPosition` (0..1), `spacing = 1/conveyorcount`.
* **`FillQueue(5)`**: for `i` in `0 .. size(recipe)*5-1` create an `Ingredient` named `recipe[i % size]` (texture `items/<name>`, sprite layer 4). If `colorCount > 1` set its colour to `RandRange(allowRainbow ? 0 : 1, colorCount)` (inclusive both ends; `RandRange(a,b) = a + floor((b-a+1) * u)`, `u` a uniform double from the global `TRandom`). Then shuffle by swapping each element `i` with a uniformly random index in `[0, n-1]` (naive swap shuffle, not Fisher-Yates). So the stream is a shuffled "bag" with five copies of each recipe entry; when the vector is empty the next bag is built.
* **`Update(dt)`** (`dt` = `AnimateDelta(conveyortime)` = fraction of a conveyor trip): if `head < 1`, `head = min(1, head + dt)`. Walk the list front to back placing ingredient `k` at curve parameter `head - k*spacing`. Then, while the remaining parameter is `> 0`, take the next ingredient from the back of `queue` (refilling when empty), append it to the list, add it to the base sprite, place it at the remaining parameter and subtract `spacing`. Finally make sure one `IngredientPiston` (two arm sprites `image/ingredient_arm_1/2` and fingers `image/ingredient_finger_1/2`, visual) exists per list entry and position it; the arm's y is `(600 - y)/2 + 16`.
* **`PopConveyor`** returns the front ingredient only if `head == 1.0` exactly, else null. On success it pops the list front and does `head -= spacing` (head is exactly 1.0 at that moment, so it becomes 0.8 for 5 slots). **`PeekConveyor`** returns the front without conditions.
* At start `head = 0`, list empty: the first `Update` spawns ingredients as the head advances, one per `spacing`; the front reaches the gun after one full `conveyortime` (1200 ms). After a pop the train needs `spacing * conveyortime` = 240 ms (shipped data) to be ready again. **That is the effective fire cooldown** (about 4 shots/s, ~250 shots in a 60 s run at most).
* Colour 0 is "none/wild", 1 red, 2 blue, 3 yellow, 4 white (mismatch). Ring tints (`kFactoryColors`, static init at 0x1ab9fe): 0 (1,1,1,1) with texture `image/item_ring_rainbow`; 1 (0.9216, 0.0549, 0.0549) approx (235,14,14); 2 (0.1765, 0.2196, 0.7725) approx (45,56,197); 3 (1.0, 0.8196, 0.0902) approx (255,209,23); 4 (1,1,1,1). `Ingredient::SetColor(c)` (0x26930) lazily creates the child ring sprite `image/item_ring` (layer -1). Texture rule: `c == 0` selects `image/item_ring_rainbow`; `c != 0` selects `image/item_ring` only when the previous colour was 0 (changing between two non-zero colours keeps the texture). Then the colour is stored and the ring tint set from the table. With `colorcount = 1` no colour is assigned, no ring appears (colour stays 0, the ring sprite is never created).

### 5.2 Aiming and firing
* `OnMouseMove(p)` (0x80d2), in every state: `d = normalize(p - gunPosition)`; `angle = acos(d.y)`, negated if `d.x > 0`; the gun sprite's rotation matrix is set to `angle + pi` (translation kept). So rotation 0 = pointing up on screen, increasing clockwise-ish; the art is drawn pointing up. Mouse coordinates are used directly (the window covers 800x600 at the origin).
* `OnMouseDown(p)` (0x8000): only in state 2 (PLAYING, and note it does not check `gamePaused`; the pause overlay is **not** modal, it is a sibling Bitmap covering only its own rectangle, so a click on the playfield outside it while paused still calls `Fire` and pops a shot that stays frozen until Resume; see `factories-questions.md` section 3). Calls `ChocolateGunTask::Fire(p)`. Returns the window's "handled" flag byte.
* **`Fire`** (0xd9f0): `PopConveyor`; if null, nothing happens (the click is lost). Else the ingredient moves to sprite layer 8; direction = `normalize(p - gunPos) * 1000` (a vector of length 1000); a `FlyingIngredient` is created with start = the ingredient's current position (the conveyor end = gun position), direction as above, `distance = 0`, `t = 0`, appended to the flying list; the gun plays `DoAnim`; sound event `launch` is posted (message 0x3ea).
* **Shot flight** (`FlyingIngredient::Tick`, 0x2268e): `t += dt` clamped to 1, where `dt = AnimateDelta(gunTime=250)`; position = `start + direction * t`. With `distance == 0` the return value is `t`. So **a shot flies 1000 px in 250 ms (4 px/ms, 40 px per 10 ms tick), then dies**. If the point is ever off the aimed line there is no gravity or bounce.
* The gun animation `DoAnim` in `gun.xml`: 60 ms per frame, frames 1,2,3,4,5,6,1 (about 420 ms), cosmetic only.

### 5.3 Collision, hit, miss (`ChocolateGunTask::Animate`, 0xd628)
For each flying shot, each tick (iteration order is the list order; set iteration over trays/recyclers is by shared_ptr address so the **order in which overlapping trays are tested is arbitrary**):
1. Remember old position; `r = flying.Tick(dt)`.
2. If `r != 1.0` (still in flight):
   * For each tray: squared distance from the tray centre to the **segment** old->new; hit if `<= (tray.radius + 16)^2` (radius starts at 75, so 91 px) **and** `tray.CanAccept(ingredient)` (some recipe slot with the same ingredient name is still empty; colour is ignored here). On a hit call the ingredient's `HitTray(tray)` (virtual); if it returns true the shot is removed and the test stops.
   * Else, if a recycler task exists: for each recycler, squared distance from the **point** new position to the recycler centre `<= (72 + 16)^2`; `CanAccept` is always true; `HitRecycler` calls `Recycler::AddIngredient`, returns true, shot removed.
3. If `r == 1.0` (end of flight, collision tests skipped on that tick): call `Die()`; if true, remove it. Plain `Ingredient::Die` (0x2730a) looks up the top modal window's `FactoryGame` child and sends it a **`MissedIngredientMessage`** (type 0x3eb, carrying the ingredient). It returns true.

So a *hit* is a shot passing within 91 px of a tray that still needs that ingredient; a *miss* is a shot that flew the full 1000 px without such a hit (including shots that crossed trays which could not accept them, and shots fired while the matching tray was full). Passing through a recycler costs nothing and counts nowhere except the `RecycleCount` game-state counter.

`Ingredient::HitTray` (0x27450) = `tray.AddIngredient(self)`, returns true. `HitRecycler` = `recycler.AddIngredient(self)`, returns true.

### 5.4 Miss handling (`MissedIngredient`, 0xcac4; message 0x3eb)
1. The ingredient sprite is kept in a list (removed at `ResetGame`).
2. If `missCount < size(missed)`: scale it to 0.6 (matrix reset then scaled) and put it at `missed[missCount]`.
3. `missCount += 1`.
4. If `missCount == size(missed)` (8): `SetGameState(4)` and sound `ingredient_failure_factory shutdown` (the string has a space, as in the binary).
5. Else: tray task `scale *= 0.5`, and if that is below 1.0 it becomes 1.0 (so a miss at speed 1 changes nothing; speed 8 goes to 4, then 2, then 1). Sound `ingredient_failure_<missCount>`. If `warnOnMiss` and `missCount == 1`: `PauseGame("choco_first_missed")` (tutorial text from `strings.xml`).

## 6. Trays

### 6.1 Geometry and state (`ChocolateTray`, 0xed14)
A tray is a `TSprite` (layer 5) with texture `image/tray<N>` where `N` = recipe size (textures exist for N = 2..6), a hidden ring child `image/tray<N>_ring`, an animated child `image/powerup.xml` (starts on its last frame), and four piston arms (`image/tray_arm_1..4`, layer -100, visual: lined up between the tray and a point 50 px above it). Members: `lastUpdate`, `offset`, `endpoint`, `radius = 75`, `slots[N]` (ingredient or empty), `fillCount`, `color`.
* Slot `i` of `N` sits at `(sin(2*pi*i/N) * 50, -cos(2*pi*i/N) * 50)` relative to the tray centre (slot 0 at the top, clockwise). The same positions place the icons on the tray template.
* Slots are bound to recipe positions: slot `i` accepts only ingredients named `recipe[i]`.

### 6.2 Motion (`ChocolateTrayTask::Animate`, 0xf836)
`TScalingTask::Animate`, `T = GetT(traytime)`, then every tray `Update(T)`:
`u = frac(T + offset)`; position = `traycurve.GetPoint(u)`; pistons follow. So trays circulate with period `traytime / scale` (30 s at scale 1, 3.75 s at 8), evenly spread by offset `i/5`. The end condition is checked in `Update` (6.4).

### 6.3 Adding an ingredient (`AddIngredient`, 0xfa44)
Find the first slot `i` (ascending) with name match and empty slot. (If none: return false; the CanAccept pre-check normally prevents it.) Then:
1. The ingredient is reparented from the conveyor/flying layer onto the tray, positioned at the slot offset; `slots[i] = it`; `fillCount += 1`.
2. **Colour logic**: let `tc` = tray colour, `ic` = ingredient colour. If `tc != ic`: if `tc != 0`: if `ic == 0` (wild) the ingredient is recoloured to `tc` (`SetColor(tc)`), else (two different real colours, or tray already white) the tray colour becomes **4 (white)**; if `tc == 0` the tray adopts `ic` (`SetColor(ic)`). `ChocolateTray::SetColor(c)` (0xe48c) recolours every ingredient already in the slots to `c`, sets the ring sprite tint to `kFactoryColors[c]` and shows the ring unless `c == 0`.
3. If the tray is now **full** (`fillCount == N`):
   * send message **0x3f0** (speed-up) to the top modal (reaches `ChocolateFactory::OnMessage` because the factory is the modal's default focus);
   * name the completion sound `ingredient_complete` if the tray colour is 0 or 4, else `ingredient_complete_colormatch`;
   * if the colour is neither 0 nor 4 (a colour match): send a `FlyingRingStartMessage` (type 0x3f1) and then message **0x3ed** to the `FactoryGame` window, and `Clear(true)` the tray at once (product produced immediately, section 7);
   * otherwise leave the full tray on its way to the end point (6.4).
   Not full: the sound is `ingredient_success_<fillCount>` when the tray colour is 4, else `ingredient_color_match_<fillCount>` (so for colourless trays, colour 0, the *color_match* names are used).
4. The chosen sound name is posted as message 0x3ea (name field) to the window manager queue.

### 6.4 Delivery of a full plain tray (`Update`)
`Update` compares the previous phase `lastUpdate` with the new phase `u` and tests whether the tray **crossed its `endpoint` parameter** this tick (handles wrap-around: when `u < last` the interval is `[last,1)` plus `[0,u]`; otherwise `[last,u]`). If it crossed and `fillCount == N`: send message **0x3ec** to `FactoryGame` (manufacture: arm animation, `snatch` sound, and a product, section 7), then `Clear(true)`. `lastUpdate = u` is stored at the end. `endpoint` is `trayend` = 0 in the shipped data, i.e. the first point of the tray path (515,300 for bars).

### 6.5 Clear (`Clear(animate)`, 0xf3a6)
Removes the ingredient sprites, `fillCount = 0`, `color = 0`, hides the ring, plays the `powerup` child's `DoAnim` (11 frames at 100 ms) if `animate`, restores `radius = 75` and the identity rotation/scale (keeping position).

### 6.6 Full-board bonus (`ChocolateTrayTask::Animate`)
After updating, if **all** trays are full at once: post sound `ingredient_complete_full_bonus`, `Clear(true)` every tray and send message 0x3ed to `FactoryGame` once per tray, so each tray yields one product (5 with the default tray count). This needs every tray full simultaneously, i.e. none has yet reached the end point, which is unlikely but possible at high speed.

### 6.7 Speed rule
Every tray that becomes full sends 0x3f0: `scale = scale * speedup (1.1)`; if `scale > maxspeed (8)` it is set to 8. Every miss halves it (floor 1). It starts at 1 at each (re)start. Reaching the cap takes 22 completed trays.

## 7. Products, rings and scoring

* **0x3ec (manufacture)**: if `machineTime == 0` set it to 1 (animation); sound `snatch`; **then falls through into 0x3ed** (the decompile has no break; I read this as intended since 0x3ee also falls through the same way).
* **0x3ed (produce)**: `ChocolateProductQueueTask::AddProduct`: a `ChocolateProduct` (layer 2) at the start of the product curve, picture built with `BuildItemAppearance(..., 0.5)`, parented to the base sprite.
* **Product belt** (`Animate` 0xdfa4): `dt = AnimateDelta(producttime=3000)`; each product `t += dt` (clamped to 1) and sits at `productcurve.GetPoint(t)`. When a product reaches `t >= 1` it is removed and (a) message **0x3ef (count product)** is sent to `FactoryGame`, (b) sound `chocolate_counted` is posted. The loop stops early once it meets a product whose `t < 0.1`.
* **0x3ef (count)**: `score += 1`, update the `score` Text with `%lu`. **0x3ee (count bonus, from a ring)**: `bonusScore += 1` then falls through to `score += 1`. (`bonusScore` is only incremented; nothing reads it.)
* **Colour-match rings**: message 0x3f1 calls `FlyingRingStartMessage::Initiate(ringTask, (689,555))` (0x22c28): one `FlyingRing` (layer 15, texture `image/item_ring`, tint = the tray colour) per recipe slot, starting at the slot's global position and flying along a 4-point curve (start, start, end, end) to (689,555), the bottom end of the product belt in the default path. A ring's `t += AnimateDelta(ringspeed=500)`; on arrival it is removed and message 0x3ee (+1 score) is sent. So a colour-matched tray is worth **1 product + N rings = N+1 cases** (N = recipe size), the rings arriving about 0.5 s after the match and the product 3 s after.
* A plain full tray is worth 1 case (after reaching the end point plus the 3 s belt).
* **Final score** = number of 0x3ef plus 0x3ee events before the run is cut off. Nothing else adds to it.

### 7.1 Production quantity
`ui/chocolate_gameover.lua` shows `GetString("factory_gameover", tostring(GetScore()))` ("Production rate: %1% cases per week..."). Its `ok` button runs `CloseWindow(score)`, `try_again` runs `StartGame()` again in the same dialog (only the last run's score is returned). `Product:RunMinigame` returns `tonumber(result)` or -1 when the dialog was cancelled; `ui_factory.lua` does `if count >= 0 then factory:SetProduction(product, count); UpdateDisplay(); TickSim(1); CloseDialog() end`. `SetProduction` stores `info.output[code] = count; info.production = count` and recomputes needs. Each weekly tick, `Player:RunFactories` produces `min(production, floor(have/need) over each ingredient)` cases, consumes `count * produce` of each ingredient, calls `product:AdjustInventory(produce)`, `RecordMade`, and marks a stall if `produce < production`. So the score is literally the weekly case count, with **no scaling, rounding or cap in the mini-game**.

## 8. Recycler (`Recycler`, 0x33d8c)
Optional bin (only when the factory has the `recycler` powerup for that product's machinery category). `TAnimatedSprite` layer 3 with `image/recycler.xml` (`DoAnim`: 100 ms per frame, frames 2..9 then 1), radius 72 (hit radius 88), position on `recyclerpath` at phase `frac(T)` of `recyclertime` (offset 0; one bin). `CanAccept` is always true. `AddIngredient`: plays `DoAnim` if not already playing, removes the ingredient sprite, posts sound `recycler`, and increments the game-state counter `RecycleCount` (`SetState("++RecycleCount", 0)`). A recycled shot is neither a hit nor a miss. In the shipped data every category has `recyclertime = 5000` and a tiny path in the top-left (points between (50,50) and (75,100)); `Category` defaults are 20000 and a larger loop. Lua enables it through quests only: `AwardFactoryPowerup("zur_factory","bar","recycler")`, `("san_factory","truffle","recycler")`, `("wel_factory","exotic","recycler")`. The `rainbow` powerup has no award in the shipped Lua (`grep` finds only the read in `chocolate_factory.lua`), but the native supports it.

## 9. Ingredient classes
1. `Ingredient` (0x25dc0): `TSprite` layer 4, `name`, `color`, ring child. Sprite size 50x50 (`items/<name>.png`).
2. Virtuals used by the gun: `SetColor` (+0x28), `HitTray` (+0x2c), `HitRecycler` (+0x30), `Die` (+0x34).
3. **Unreachable powerups** (no `Create` function was emitted for them and nothing in the decompile constructs them): `ChainIngredient` (`HitTray` marks itself chained, creates a second plain ingredient of the same name and hits the same tray with it, returns false so the shot continues; `Die` is silent once chained; ignores colours), `EnlargeIngredient` (`HitTray` calls `tray.Scale(1.1)`, multiplying the tray hit radius and sprite scale, then behaves normally), `SuperColorIngredient` (adopts the ring texture `image/item_supercolor` for a non-zero colour; `HitTray` forces the tray colour to its own and restores the ring texture). Treat them as dead code; the port need not implement them.

## 10. Input
* Mouse move: aim (5.2). Mouse down: fire, only when state is PLAYING. No button state, no drag, no right click.
* Keyboard: `OnKeyDown(key, flags)`: **Escape** pauses with `game_paused` if not already paused; if the config key `kCheatMode` is set and flag bit 0 is on it also runs `DoModal('dev/dev_menu.lua')`. No other key does anything (no fire key, no aim keys). Returns false (not consumed).
* Key order: the char event (modal `OnChar`, Enter/Escape to default/cancel buttons) is queued before the key-down, so on the game-over and intro screens Escape closes the dialog via the cancel button and `OnKeyDown` never re-pauses; during play no visible cancel exists, so the key-down pauses. See `factories-questions.md` section 4.
* Application deactivation (message 0x3e9 `kAppDeactivate`, `ToggleForeground`, or `TPlatform::IsForeground` false at a tick) pauses with `game_paused`.
* Overlay buttons use Lua commands `StartGame`, `ResumeGame`, `PauseGame`, `CloseWindow`. The pause overlay's `ok` is `ResumeGame`; the Go button is `ResumeGame`; the Pause button is `PauseGame()`.

## 11. Particles and glitz
The factory classes make **no use** of `TLuaParticleSystem`, `CreateParticles`, `SetNumParticles`, `SetTexture`, `SetBlendMode`, `Vec2` or the `f*`/`p*` DSL: no reference to them appears in `ChocolateFactory.c`, the gun/tray/queue/ring/recycler/product/ingredient files or `Content.c`. `Glitz.h` (a `ParticleColor` function type) and `Fireworks.h` (`TFireworksWindow`) belong to the end-of-game fireworks and the slot payout (`glitz/firework.lua`, `glitz/slotpayout.lua`). All visual effects of the factory are sprite based: the tray clear sparkle (`image/powerup.xml`), the gun and recycler animations, the ring sprites, the machine rotation. Phase 4 can therefore port the factory with no particle work. (Particle details remain in `api-globals.md` / `embedded-lua.md` for the fireworks window.)

## 12. Lua side: data the native reads and writes

### 12.1 Chosen by `ui/chocolate_factory.lua` (original; Reforged identical but uses `GetString("go")`, `GetString("pause")`, `GetString("cases")` for labels instead of literal "Go", "Pause", "cases")
* Inputs: `gDialogTable.factory` (the `Factory` building; `.port.name`, optional `.windowX/.windowY`), `gDialogTable.product` (falls back to `_AllProducts["t01"]` for testing).
* `product.recipe`: list of ingredient names with repeats (built in `Product:Create` from the XML counts, in `pairs` order, so the order of slots is the Lua table iteration order of the count table, **not** stable by design), `product.code`, `product.appearance`, `product:GetName()`, `product:GetMachinery()` (category, overridable per product by `Player.itemMachinery[code]`).
* Per-category tuning, with product-level override `product.X or category.X`: `traytime, traypath, colorcount, conveyorcount, conveyortime, conveyorpath, gunspeed, producttime, productpath, ringspeed, recyclertime, recyclerpath`. Shipped values (identical in `Category` defaults, `categories.lua` and the Reforged copy): `traytime 30000`, `conveyorcount 5`, `conveyortime 1200`, `gunspeed 250`, `producttime 3000`, `ringspeed 500`, `recyclertime 5000`, product path `{696,300},{696,320},{696,500},{696,544}` (the `Category` default is x=689 and ends at 555, which matches the hard-coded ring target (689,555); the shipped categories use x=696 and end at y=544, so rings land about 7 px left and 11 px below the belt end).
* `colorcount`: bar 1, infusion 2, truffle 2, exotic 3 (beverage and blend are coffee, not used here).
* Tray paths: bar 16 points (a rectangle loop), infusion and truffle share a 16-point path, exotic 13 points. Conveyor paths 7 points. Full numbers are in the original install's `assets/items/categories.lua`.
* Powerups: `factory:HasPowerup(category, "recycler")` and `"rainbow"` read `Player.powerups[factory.name .. category.name .. powerup]`.
* `warnOnMiss = (Player.rank <= 1 and product.code == "b01")`.
* Fixed values written in the Lua: `countdowntime 3000`, `gametime 60000`, `warntime 10000`, the `missed` list, the text boxes of 3.3.
* `Player` fields used by the surrounding code, not by the native: `Player.rank`, `Player.powerups`, `Player.factories[name]` (`current, production, supply, stall, needs, output`), `Player.itemMachinery`, `Player.ingredients` (checked by `RunMinigame` before opening: all recipe ingredients must be in stock at least once; the mini-game does not consume any), `Player.money` (machinery purchase prompt).

### 12.2 Lua -> native calls
`StartGame()`, `PauseGame([key])`, `ResumeGame()`, `GetScore()` (globals registered by the `ChocolateFactory` constructor at 0x8ea8 into the Lua globals table; the later-constructed factory type, chocolate or coffee, owns the names; the destructor does **not** unregister them). `CloseWindow(value)`, `EnableWindow`, `DisplayDialog` come from the window system.

### 12.3 Native -> Lua
Only: `ui/game_pause.lua` (reads global `gPauseText`, then sets it nil; label default `game_paused`; `ok` button runs `ResumeGame`), `ui/chocolate_gameover.lua` (calls `GetScore()`), and the cheat-mode `DoModal`. No named callback functions, no events delivered to Lua during play. `TGameState` string keys written: `ChocolateFactoryTime` ("restart" at start, 1 when running, 0 when paused), `RecycleCount` (reset to 0, `++` per recycled shot); nothing in the shipped Lua reads them (inference from a search of the original install's `assets/`; they are for the sound or achievement layers).

## 13. Sounds (names only; message 0x3ea or direct `BSGSoundEvent`)
`chocolate_making_game_intro` (reset), `chocolate_making_game` (state 2 entry, again on every resume), `clock_tick`, `start_bell`, `whistle`, `launch`, `snatch`, `ingredient_success_1..N`, `ingredient_color_match_1..N`, `ingredient_complete`, `ingredient_complete_colormatch`, `ingredient_complete_full_bonus`, `chocolate_counted`, `recycler`, `ingredient_failure_1..7`, `ingredient_failure_factory shutdown`, `game_over`, `chocolate_making_game_outro`. The `TSoundHandler` registered by the window forwards any message of type 0x3ea to `BSGSoundEvent(message.name)`. The audio itself is out of scope for the engine.

## 14. Textures, animations, text
* Bitmaps via Lua: `image/factory_background` (jpg), `image/factory_windows_<port>`, `image/missed_ingredient_divot`, `image/machine_stack_base`, `image/machine_button_{go,stop}_{up,down,over}`, `image/popup_back_generic_1`, `image/machine_1|3|4|6`.
* Via native: `image/machine_stack_finger_upper|lower`, `image/tray2..6` and `_ring`, `image/tray_arm_1..4`, `image/ingredient_arm_1|2`, `image/ingredient_finger_1|2`, `image/box_open`, `image/box_open_mask`, `image/gun_base`, `image/item_ring`, `image/item_ring_rainbow`, `image/item_supercolor` (unused powerup), `image/timer_face`, `image/timer_hand`, `items/<ingredient>`, `items/<code>_big` or `custom/<layer>` (+ `_highlight`), cursors `cursor/cursor` and `cursor/gun_aimer`.
* Animated sprites (`TAnimatedTexture`, script `DoAnim`): `image/gun.xml` (6 frames, 60 ms), `image/powerup.xml` (11 frames, 100 ms), `image/recycler.xml` (frames 2..9,1 at 100 ms).
* Strings: `chocolate_start`, `choco_first_missed`, `factory_gameover` (%1% = score), `game_paused`, `help_chocolate`, `cases`, `go`, `pause`, `try_again`.

## 15. Port checklist (Phase 4)
1. Fixed 10 ms step with accumulator and a 1 s clamp; all task phases via the `TScalingTask` formulas.
2. Bezier path with 0.2 flattening and arc-length lookup (shared with other windows).
3. Implement states 0..5, `Fire`, conveyor pop/refill, shot flight, segment-vs-circle test, tray colour logic, tray end-point crossing, product belt, ring flight, scoring, speed up/down.
4. Open via `DisplayDialog{ "ui/chocolate_factory.lua", product=..., factory=... }`; the window must expose `StartGame/PauseGame/ResumeGame/GetScore`, support child lookup by name (`score`, `countdown`, sibling `information`, `go`, `pause`) and `FillWindowPtr` of the two UI scripts.
5. For the scripted-run test: deterministic RNG injection for `FillQueue`, then drive `Fire(point)` at tray positions and assert score and `Player.products[code]` after `TickSim(1)`.

## Unknown
* Exact `TRandom` algorithm (0x184da4 and `Rand32`) and its seeding: only the `RandRange` contract is known. A bit-exact replay of a native run is not possible without it; tests should inject the RNG.
* How `TWindowStyle::GetNumber/GetBool/GetTable` handle a key given with the wrong type (defaults are the 4th/3rd parameters as listed; coercion not decoded).
* Iteration order of `recipe` (comes from Lua `pairs` over the count table in `Product:Create`) decides which recipe slot is slot 0 on the tray; irrelevant to scoring but affects the picture.
* Draw order of `Text` children vs the base sprite (children first, then sprites on top, per `Draw`); whether score text is hidden behind the timer face (the face is centred on the score text rect) is not verified visually.
* The speed of rotation direction and sign conventions of `TMat3::GetRotation` (clockwise or counter-clockwise on screen) were not decoded; formulas above give the angle argument only.
* `kCheatMode` config key value and `TPlatform::GetConfig` lookup (used only for the dev menu).
* `bonusScore` has no reader; `TGameState` keys have no reader in the shipped Lua.
* Behaviour when `ingredient` has an empty recipe or a recipe larger than 6 (no `image/tray7`): texture load fails; not decoded.
* Arm/finger geometry: the visual intent of the conveyor-piston gap and the on-screen rotation direction (the geometry itself is in `factories-questions.md` section 8).
