# The ingredient-shooting factory mini-game (`FactoryGame`, Chocolatier 1 and 2)

Scope: the native `FactoryGame{...}` window of Chocolatier 1 (class `TFactoryGame`), what its Lua callers supply and read back, and what the Lua and assets of Chocolatier 2 show about its successor. The companion table is `docs/re/choc-factory-game.tsv` (constants, field map, sounds, natives, assets). The dice game and the crates game are in `docs/re/choc-dice-game.md`.

Sources, in trust order: the Ghidra decompile of the unpacked Chocolatier 1 `Chocolatier.exe` from the original Windows install (`re/choc1/decomp/_unnamed.c`, function names `FUN_<address>`; addresses below are virtual addresses in that exe), the exe's own data bytes (constants that Ghidra shows as `DAT_`/`UNK_` were read from the file with a PE reader and `llvm-objdump`), the Chocolatier 1 Lua (bytecode under `re/choc1/pfp_extracted/`, staged from the install's `assets/assets.pfp`, disassembled with a workbench script), the Chocolatier 2 Lua under `re/choc2/pfp_extracted/` and the sprite sheets in both `image/` folders. Chocolatier 2's exe is packed, so **nothing about the Chocolatier 2 native is decoded**; section 12 is the Lua contract and the assets only.

Marking: "fact" means read from code or data. "Inference" means derived and said so. Anything else is in "Unknown" (section 14).

## 0. Summary (what surprised me)

* The Chocolatier 1 game is **not** the conveyor-and-path game of `docs/re/chocolate-factory.md`. It is a **rotating wheel of trays** (493 px picture, centred at (300,300)) with the **gun in the wheel's centre**. Ingredients arrive on an L-shaped belt along the bottom of the screen and up to the gun, one at a time; the player aims at the mouse and fires; a shot flies straight at 12 px per 10 ms tick until it hits a tray that still needs that ingredient or leaves the 800x600 screen (a miss).
* Time is a **fixed 10 ms logical tick**. The run is a 2.4 s countdown, then exactly 60 s, then a wind-down until nothing is in flight.
* The **only output is one integer**: the number of finished trays that reached the crate (`GetProductCount`). The Lua uses it directly as the factory's new cases-per-week rate (`rate`).
* Eight misses stop the game ("production stopped"); the count is kept.
* Each completed tray multiplies the wheel speed by `speedup`; each miss halves the speed's excess over `startspeed`.
* The ingredient supply is a **pre-shuffled deck, balanced per block**, so every tray can always be finished; the belt only advances while the gun is empty, which makes the 350 ms feed interval the real fire-rate limit.
* `powerup.xml` and its sheet `powerup.png` (900x300, 11 frames of 150x150) are in the Chocolatier 1 archive, but the decompile shows no colours or recycler; the only powerup trace in the native is the `powerup` sound when every tray is complete (7.4). Whether the sheet is also drawn there is not decoded (unknown).

## 1. Where it lives

| Thing | Address | Notes |
|---|---|---|
| class `TFactoryGame`, vtable | `004e740c` | Lua type name `FactoryGame` |
| constructor | `0040e940` (7.9 KB) | zero-inits ~0x480 bytes, registers 4 natives, loads images and 23 sounds |
| destructor | `0040ce00` (scalar-deleting `0040d5d0`) | |
| `Init` (vtable slot 5) | `0040b080` | reads the Lua keys, builds the deck images, the tray wheel and the template |
| `PostInit` (slot 6) | `00410830` | binds child windows, calls ResetGame |
| `Draw` (slot 7) | `0040c4f0` | draws every sprite, updates the timer text |
| tick task (slot 13) | `00410b30` | the 10 ms game step, returns true to repeat |
| mouse click (slot 14) | `0040df60` | fires if loaded and state 2 |
| mouse move (slot 17) | `0040ab00` | aim |
| `StartGame` native | `0040a150` | |
| `RestartGame` native | `0040e1c0` | `ResetGame` (`0040df90`) then `StartGame` |
| `TogglePause` native | `0040abb0` | name string at `004e73d4` |
| `GetProductCount` native | `00409fd0` | returns the product counter as a double |
| end of game | `0040a340` (arg: 0 normal, 1 shut down) | |
| projectile step | `0040da00` | |
| belt step / spawner | `0040e260`, `0040dd50`, `00409f70` | |
| fire | `0040de90` | |
| tray hit test and fill | `0040e480` | |
| wheel rotation and tray collection | `00410940`, `0040e1d0` | |
| crate transit | `0040c360` | |
| collection machine animation | `0040a810` (start), `0040a880` (step) | |
| pause on Escape/deactivate | `00403130` (calls `0040afc0`) | the Escape and deactivation message note is in `choc1-natives.md` |

Natives (registered on the shared Lua state by the constructor): `StartGame()`, `RestartGame()`, `GetProductCount()`, `TogglePause()`. There are no `PauseMinigame`/`ResumeMinigame`/`ResetMinigame`/`GetProduction` in Chocolatier 1 (those are Chocolatier 2, section 12). The task description's "ingredient/recipe inputs" are Lua constructor keys (below), not natives.

## 2. How the Chocolatier 1 Lua builds and uses it

`LFactory:DoMiniGame(character)` (listing `sim/factory.lua`, function `main.10`, defined at Lua line 134) is the only caller.

0. The factory status dialog's `minigame` button runs `UIStatus`/`main.18` (Lua 422-478): if any recipe ingredient is out of stock (`gSim:GetInventory`), a character dialog with `factory_noingredients` and the list of missing items is shown instead and `DoMiniGame` is not called.
1. `SetUpdateRate(0)` (stops the money-counter task), `StopBackgroundMusic()`, `SetAmbient(kFactorySetupAmbient)`.
2. `gFirstMissWarning` is read once: `warnOnMiss = (gFirstMissWarning and product.name == "basebars")`; then `gFirstMissWarning = nil`. So the "wasted ingredient" pause message appears only for the first-ever game, and only with the starter product `basebars`.
3. A `Bitmap{image="image/factory", x=0,y=0,w=800,h=600}` (the 800x600 background) containing:
   `FactoryGame{x=0,y=0,w=800,h=600, name="factory", character=<char.name>, firstMiss=<bool>, port=gSim.port.name, product=<product>, prodname=<product.name>, recipe=<product.recipe>, type=<product.type>}`
   with children, all declared in Lua: `Text` "cases" (x=681,y=3,w=38,h=16; 16 pt colour (255,1,17)); `Text{name="counter"}` (x=646,y=15,w=108,h=38, label "#0", 35 pt colour (255,41,77)); `Text` "mini_time" (x=669,y=65,w=63,h=16); `Text{name="timer"}` (x=669,y=77,w=63,h=35, label "#1:00"); `Text{name="countdown"}` (x=150,y=150,w=300,h=300, label "#4", red 200 pt); `Button{name="pause"}` (x=679,y=416, graphics `control/fac_btn_pause_up|down|over`, command `TogglePause`) and `Text{name="pause_label"}` (x=639,y=444,w=124,h=24); a `Frame{name="paused"}` (x=47,y=233,w=500,h=134) with its text, a "resume" button (`TogglePause`) and a help button (`SetUpdateRate(kDefaultUpdateRate); DisplayHelp(7,true); SetUpdateRate(0)`); a `Window{name="keeper"}` (x=10,y=30,w=597,h=301+45, fit) holding a `Rectangle`, a `Text{name="chartext"}` (label `factory_start`), the four `image/dchar_*` frame pieces, an intro window `gamestart` (buttons **cancel** `CloseWindow(-1)` and **start**: `gSim.minigames += 1; SetAmbient({}); StartGame()`) and an end window `gameend` (buttons **try_again**: `gSim.minigames += 1; RestartGame()` and **ok**: `CloseWindow(GetProductCount())`), plus a help button.
4. The whole thing is shown with `DisplayDialog{"ui/dialog.lua", sound="charopen", body=...}`, which returns whatever `CloseWindow` was given.
5. After it returns: `if rate > 0 then self:SetConfiguration(product.name, rate); UpdateDynamicWindow("factory_status"); BackgroundMusicNow(); PlayerMessage(GetVariableString("factory_ratechange", {port=, product=, rate=rate})); gSim:Tick(); UpdateStandardUI() end`; in every case `SetUpdateRate(kDefaultUpdateRate)` and `SetAmbient(kPortAmbient)`.

So: **cancel before the game returns -1, which is not > 0, so nothing changes; finishing with zero products also changes nothing** (`0 < rate` fails). `SetConfiguration(product, rate)` stores `config[product] = rate` and `self.rate = rate` (listing `main.8`, Lua lines 83-93, default `LFactory.defaultProductionCount = 0`); `ProductionRun` then makes up to `rate` cases a week from the stocked ingredients (`main.14`, Lua 333-380). The rate is therefore *the final score unmodified*; there is no conversion table.

`Escape` and window deactivation pause the game (`choc1-natives.md`; `00403130` finds the top modal named `minigame`, its `factory` child, and calls `0040afc0` which pauses unless already paused or in state 0).

## 3. Constructor keys (Lua -> `Init`, `0040b080`)

| Key | Where read | Default | Use |
|---|---|---|---|
| `x y w h` | window base | | 0,0,800,600 |
| `prodname` | string, default `"milkbar"` | | picture `item/<prodname>` stored at `+0xc0`; drawn on the crate and flying to it |
| `character` | string, default `"sffactorykeep"` | | used for the "wasted ingredient" command only |
| `firstMiss` | bool | false | `+0x3a4` |
| `port` | string, default `""` | | prefix for the end-text key (`<port>_production_low`, ...) |
| `recipe` | table `ingredientName -> count` | | see 4.1 |
| `type` | table (the product type) | | `trayCount`, `ingredientRate`, `speedup`, `startspeed`, `maxspeed` read from it |
| `product` | table | | not read by name here (the C code only uses `prodname`, `recipe`, `type`) |

Type table keys (all numbers; read with defaults): `trayCount` default **7** (`+0x110`), `ingredientRate` default **350** (`+0x304`, string at `004e72e8`), `speedup` default **1.1**, `startspeed` default **1.0** (string at `004e72c8`), `maxspeed` default **8.0**. The values are kept in the globals `005253c0` (speedup), `005253c8` (startspeed) and `005253d0` (maxspeed). The shipped Lua (`data/items.lua`) sets: bar: nothing (all defaults); square: `trayCount 8, startspeed 0.9, speedup 1.07`; infusion: `7, 0.8, 1.1`; truffle: `6, 0.9, 1.09`; `maxspeed 8` throughout. `ingredientRate` is never set, so 350 always applies. Children looked up by name in `PostInit`: `counter` (Text, `+0x3b4`), `timer` (Text, `+0x3b8`), and inside `keeper` the windows `gamestart` (made visible) and `gameend` (hidden). Also by name at runtime: `countdown`, `keeper`, `chartext`, `paused`, `pause`, `pause_label`.

### 3.1 Recipe to slots
`Init` iterates the `recipe` table with `pairs`-style traversal (order of the Lua table's hash, **not defined by the data**). For the i-th distinct ingredient it loads `item/<name>` into `images[i]` (8-byte entries at `+0x31c`, six at most) and appends `i` to the slot list at `+0x34c` as many times as the count says. `+0x300` = total slot count `S` (the sum of the counts; the arrays hold up to 6, and the shipped images `tray2..tray6` / `traycomplete2..6` cover S = 2..6). Example: `basebars` = {sugar 1, cacao 1} gives S=2; `darkbars` = {sugar 1, cacao 2} gives S=3. The slot order (and so which ingredient sits at which angle on the template) depends on table traversal; the gameplay does not.

## 4. State and tick

### 4.1 Field map (offsets from the object, ints unless noted)

| Offset | Meaning |
|---|---|
| `+0x34` | state: 0 init, 1 countdown, 2 running, 3 paused, 4 wind-down (time is up), 5 over |
| `+0x38` | state saved by pause |
| `+0x3c` | **product counter** (finished trays that reached the crate) |
| `+0x40` | elapsed game ms (see 4.3) |
| `+0x44` | the game's own clock in ms; advances by exactly 10 per tick (initialised from the window clock at `StartGame`, inference for which clock) |
| `+0x48` | leftover belt time stored when the gun gets loaded |
| `+0x50` (double) | wheel angle |
| `+0x58,+0x5c` (floats) | aim direction (unit vector) |
| `+0x60` (double) | gun sprite angle |
| `+0x68` (double) | wheel speed |
| `+0x70` (double) | wheel deceleration per tick (0 until the end) |
| `+0x80,+0x84` | flywheel sprite (`fac_flywheel`) |
| `+0x88,+0x8c` | wheel sprite (`fac_wheel` with `fac_wheel_mask`), parent of the trays |
| `+0x90,+0x98` | images `tray<S>`, `traycomplete<S>` |
| `+0xa0` | gun sprite (animated `gun_shoot.xml`) |
| `+0xa8,+0xb0,+0xb8` | `fac_cratefront`, `fac_cratelid`, `fac_bellows` |
| `+0xc0` | product picture |
| `+0xc8` (byte), `+0xcc` | collection machine animating flag and its clock |
| `+0xd0,+0xd8,+0xe0,+0xe8` | `fac_upperarm`, `fac_lowerarm`, `fac_gear`, `fac_gear` |
| `+0xf0..+0x10c` | four `fac_smallwheel` sprites |
| `+0x110` | tray count `T` |
| `+0x114 + 0x3c*i` | tray record `i` (60 bytes): sprite, bounds rect (+0x10), six slot cells (+0x20, value 99 = empty), filled count (+0x38) |
| `+0x2f4` | recipe template sprite (`traycomplete<S>` plus ingredient icons) |
| `+0x2fc` | belt-time accumulator (ms) |
| `+0x300` | slot count `S` |
| `+0x304` | ingredient rate (350) |
| `+0x308` | next spawn time (ms of the accumulator) |
| `+0x30c`, `+0x314` | `fac_plate`, `missed_ingredient` pictures |
| `+0x31c` | six (image, refcount) pairs: ingredient pictures |
| `+0x34c` | slot list: ingredient index per slot (S entries) |
| `+0x364..0x370` | the deck (a vector of ints) |
| `+0x374` | the ingredient loaded in the gun (pointer, 0 = empty) |
| `+0x378/0x37c/0x380` | belt list (std::list) |
| `+0x384/0x388/0x38c` | flying list; `+0x38c` = its size |
| `+0x390/0x394/0x398` | miss list; `+0x398` = number of misses |
| `+0x3a8/0x3ac/0x3b0` | products in transit to the crate; `+0x3b0` = count |
| `+0x39c`, `+0x3a0` | `character` and `port` strings |
| `+0x3a4` | first-miss warning flag |
| `+0x3bc..` | sound slots, see section 11 |

### 4.2 Start (`StartGame`, `0040a150`)
Shows the cursor (`FUN_0042d3d0(..., 1)`) and sets it to `cursor/gun_aimer?simple` with hotspot **(16,16)**; hides `keeper`; sets texts `timer` = "1:00" and `counter` = "0"; shows `countdown` with "3"; sets `+0x68 = startspeed` (global `005253c8`), `+0x70 = 0`, `+0x50 = 0`, `+0x3c = 0`, `+0x2fc = 0`, `+0x308 = 0`, `+0x40 = 0`, `+0x48 = 0`; starts a **repeating 10 ms task** (`FUN_00421980(this, 10, repeat, no-fromnow, catch-up)`, the same signature as the Chocolatier 3 `StartWindowAnimation(10, true, false, true)`, so the same catch-up rules as `chocolate-factory.md` section 2.1 apply by analogy); `+0x44 = current clock`; **state = 1**; starts music `minigame.ogg` via `00409ef0`.

`RestartGame` first runs `ResetGame` (`0040df90`): `0040c150` (every tray: slots to 99, children removed, picture back to `tray<S>`; belt, flying, miss and transit lists emptied; counts to 0), state 0, **builds a fresh shuffled deck** (4.4), hides `paused`, `finished`; then `StartGame`. `PostInit` also calls `ResetGame`, so the first game's deck is built when the window is created.

### 4.3 The tick (`00410b30`), in the order it runs
All of this is skipped while the state is 3 except the last assignment; `dt` is a constant **10** (the task always runs the callback once per 10 ms of logical time).

1. If state != 3: `elapsed += 10`; `speed -= decel` (if it goes below 0, speed and decel are both set to 0).
2. **Warning bell** (`warning_bell`, once): state != 5, `(60000 - elapsed)/1000 < 8` now but `>= 8` one tick ago. It fires on the tick where `elapsed` first exceeds 52000 (52010).
3. If the machine-animation flag is set: `0040a880(dt)`.
4. **Too many misses**: `misses == 8` and state != 5: end the game (`0040a340(1)`), then continue with the rest of the tick.
5. **Time up**: `elapsed > 60000` and state == 2: play `mini_game_production_over`, **state = 4**; if the speed is under 5.0 set it to **5.0** (so the remaining full trays leave quickly).
6. **Countdown** (state 1): while `elapsed < 2400` the `countdown` text is `(2400 - elapsed)/1000 + 1` (integer division): "3" for elapsed up to 400, "2" up to 1400, "1" up to 2400. `countdown_tick` plays when this number differs from the previous tick's number (so at 410 and 1410 ms; not at the start). At `elapsed >= 2400`: play `minigame_starting_bell`, clear the visible bit of `countdown`, `elapsed -= 2400`, **state = 2**, `+0x308 = 0` (so the first ingredient spawns at once).
7. State in {1,2,4,5}: `00410940(dt)` (rotate the wheel, detect tray collection, section 7.4); if state != 5 then `0040e480` (hit test, 7.2).
8. State in {2,4}: `0040da00(dt)` (move shots, find misses, 6.3), `0040e260(dt)` (the belt and the spawner, 5), then `0040e480` again if state != 5.
9. State != 3: `0040c360(dt)` (crate transit, 7.5).
10. **End of wind-down**: state == 4, no shots in flight, no products in transit, and **no tray that is full but not yet collected** -> `0040a340(0)` (normal end).
11. `+0x44 += 10`; the task keeps repeating (it returns true even in state 5; the tick keeps decelerating the wheel and moving products after the game is over).

The playing phase is exactly 6000 ticks (60000 ms) after the 240 countdown ticks. The timer text (drawn in `Draw`) is `m:ss` of `(60000 - elapsed)/1000` (integer, 0 once elapsed >= 60000) and is only updated in state 2.

### 4.4 The ingredient deck (`ResetGame`, `0040df90`)
Let `B = T * S` and `N = 60000 / ingredientRate` (integer, 171 for 350). The deck length is `L = floor((N + B) / B) * B`, i.e. the smallest multiple of `B` that is greater than `N`. It is filled with `slots[k mod S]` for `k = 0..L-1` (the slot list cycles). Then each block of `B` entries is shuffled in place by a single pass: for `j = 0..L-1`, `r = rand() % B + floor(j / B) * B`, swap `deck[j]` and `deck[r]` (CRT `rand()`, not uniform). Every block therefore contains exactly `T` copies of the recipe, enough to finish every tray. Items are taken from the **back** of the vector. Example: basebars, T=7, S=2: B=14, L=13*14=182.

`rand()` is the CRT generator; the Lua seeds it with `UtilMathSeedRandom` (`srand(Timer())`, `choc1-natives.md`).

## 5. The belt and the gun's supply

Everything in this section only runs in states 2 and 4, and **only while the gun is empty (`+0x374 == 0`)**.

* **Spawn.** `+0x2fc` (accumulator) gains the tick's belt time; while it is `>= +0x308` an ingredient is created (`0040dd50`) and `+0x308 += 350`. A new item takes its index from the back of the deck and is pre-advanced by `(late * 600 / 1200) * 600 / 1200` units where `late = accumulator - nextSpawn` (the lateness is converted to belt units **twice**, so the item is advanced by `late/4` px rather than `late/2`; fact from the code, probably an oversight).
* **Belt time unit.** One tick moves each item by `dt * 600 / 1200 = 5` pixels, i.e. **0.5 px/ms (500 px/s)**.
* **Path (an L).** A new item starts at (x=0, y=571) (571 = `0x23b`). It first moves along x until x reaches 300 (`DAT_0053307c` = ftol(300.0)), which takes 600 ms; the overshoot carries into the vertical leg. It then moves up (y decreasing) until y reaches 300 (`DAT_0053308c`). Total path 300 + 271 = 571 px, about 1142 ms. The position is two ints at `+4` (x) and `+8` (y) of the item record.
* **Loading the gun.** The first item whose y equals 300 becomes `+0x374`; `+0x48` keeps the unused part of the tick, and the accumulator gains only the time actually used (`(5 - overshoot) * 2` ms in that tick).
* **Freeze.** While the gun is loaded none of this runs: no belt movement, no spawning. After a shot the belt resumes. Because items spawn 350 ms of belt time apart (175 px apart), **the next ingredient reaches the gun 350 ms of belt time after the previous one did** if it was already close, so the shortest interval between loaded ingredients is about 350 ms (about 2.9 shots/s; at most ~171 shots in a run) (derived).
* Drawing: the plate `fac_plate` is drawn under an item once its y < 571, offset +16 in y; the four small wheels at (16,608), (116,608), (216,608), (316,608) turn by `2*pi*accumulator/1200` radians (one turn per 1200 ms of belt time). They sit half off-screen under the horizontal leg (inference: purely decoration).

## 6. The gun

### 6.1 Aim
Pivot: **(300,300)** (the globals `0053304c` and `00533050` are both 300.0 from the static initialisers at `004e5000..004e5150`). `OnMouseMove` (`0040ab00`, called for every mouse move in any state; also by the click handler) sets `dir = mouse - pivot` (floats), normalises it to a unit vector at `+0x58`, and sets the gun sprite rotation from `acos`-style maths (`FUN_004ac950`; the angle is negated when `dir.x > 0`; exact sign convention not checked, it is cosmetic). The cursor is `gun_aimer` (32x32) with hotspot (16,16).

### 6.2 Firing
Mouse click (`0040df60`): ignored unless a loaded ingredient exists (`+0x374 != 0`) **and the state is 2**. So no shots in the countdown, when paused, in the wind-down or after the end. It re-applies the aim for the click point and calls `Fire` (`0040de90`): play `minigame_load_launch`; remove the loaded item from the belt list; copy the unit aim vector into the item record (`+0xc`, `+0x10`); append it to the flying list; `+0x374 = 0`; run the gun sprite's `DoAnim` (see 12.4 for the animation).

There is **no cooldown of its own**: the limit is the supply described in section 5 (about 350 ms).

### 6.3 Flight (`0040da00`)
For each shot every tick: `step = dt * 1200 / 1000 = 12.0` (a double); `x = ftol(vx * step + x)`, `y = ftol(vy * step + y)`, where `ftol` truncates toward zero and the position is stored as ints each tick (so there is truncation drift, which a port must copy to reproduce shots exactly). Speed is therefore **1.2 px/ms (1200 px/s)** along the aim vector. The shot is kept if the new `x <= 800` and `y <= 600` (compared as unsigned, so negative coordinates fail); otherwise it is a **miss** and is removed from the flying list:

1. Play `ingredient_failure_NN` where NN is the number of misses so far + 1 (the array holds eight; `+0x398` is the list size *before* this miss is appended).
2. Append the record to the miss list; the HUD shows it.
3. `speed = (speed - startspeed) * 0.5 + startspeed`. Applied once per missed shot.
4. After the loop, if any miss occurred and `firstMiss` was set: clear the flag, pause the game (if not already paused/state 0) and queue `table.insert(gCommandQueue, function() local c = LCharacter:ByName('<character>') c:UIModalSpecial(GetString('wasted_once')) end)` (the string table has `wasted_once`).

With `8` misses the tick ends the game (4.3 step 4). Shots that fly through trays that cannot take them keep flying and eventually count as misses.

## 7. The wheel, the trays and the score

### 7.1 Geometry
* Wheel sprite (493x493 JPEG `fac_wheel.jpg` with mask `fac_wheel_mask.jpg`) at **(300,300)**, rotated; trays are its children.
* `T` trays; tray `i` starts at wheel-local `(cos(a) * 225, sin(a) * 225)` with `a = i * 2*pi / T` (radius **225**), picture `tray<S>` (150x150).
* Rotation: `angle -= dt * 2*pi * speed / 30000` per tick, i.e. **one turn per 30 s at speed 1** (shown rotating counter-clockwise on screen, inference from where the collection arms are). The same rotation is applied to the flywheel sprite at (123,15).
* The recipe template (`traycomplete<S>` plus the ingredient icons) is drawn at **(700,300)**; icon `u` sits at `(sin(2*pi*u/S) * 50, -cos(2*pi*u/S) * 50)` from its centre, in slot order, starting at 12 o'clock (this is the "full tray you see on the right" of the intro text).

### 7.2 Hit test and fill (`0040e480`)
Runs twice per tick in states 2 and 4 and once in state 1 (never in 3 or 5). For each tray (record `+0x114 + 0x3c*i`) the centre is the middle of the tray's bounding rectangle (updated each tick in `00410940` from the sprite's world bounds). For each shot in the flying list:

* if `(x - cx)^2 + (y - cy)^2 <= 5881` (`_DAT_005253e0`, radius about **76.7 px**; the shot's position at the end of its step, no segment test) and the recipe has slots:
* find the first slot `u` (0..S-1) with `slotType[u] == shot's ingredient` and tray cell `u == 99` (empty). If there is none the shot is untouched (no hit).
* Hit: play the fill sound at array index `filled` (the count before incrementing): fills 1 to S-1 play `ingredient_success_01..` in order; the *completing* fill plays `template_complete`, because `Init` overwrites array element `S-1` (address `+0x3cc + 8*S`) with the `template_complete` sound. `filled += 1`. The cell stores the ingredient index. A child icon sprite is added to the tray at `(sin(2*pi*u/S) * 50, -cos(2*pi*u/S) * 50)`. The shot is removed (not a miss).
* If `filled == S` the tray is **complete**: its picture becomes `traycomplete<S>` and **`speed = min(speed * speedup, maxspeed)`** (done on completion, not on collection).

Then, if **every** tray is complete: play `powerup`, and collect all trays at once (7.4). This is a rare bonus and has no score of its own.

### 7.3 Order of slot search and multiple trays
Trays are scanned in index order; for a given tray all shots are scanned in list order; a shot removed by a tray cannot hit another in the same pass. A shot inside the radius of two trays that both need it fills the first one scanned.

### 7.4 Collection (`00410940` and `0040e1d0`)
Each tick, after rotating, every tray's bounding rectangle is recomputed. A tray is **collected** when it is complete (`filled == S`), the game state is not 5, and its rectangle top edge `y1` goes from `>= 252` on the previous tick to `< 252` on this tick (it crosses the line y = 252 going up). The tray is reset to empty (`tray<S>`, cells 99, icons removed), a product record with progress `300.0` is appended to the transit list, and the collection machine animation starts: `0040a810` plays `factory_snatch` only if the machine is not already animating, sets the flag and zeroes the clock. The machine animation (`0040a880`) advances its clock by `round(dt * speed)` per tick, uses `phase = clock * 7 / 1000` with 7 piecewise-linear keyframes for the upper arm x (offsets -60, -110, -90, -75, -60, -50, -30, 0 from 723) and lower arm x (-45, -100, -80, -65, -50, -40, -20, 0 from 723), and ends (flag cleared) when `phase > 7`, so it lasts 1000/speed ms of real time. The gears at (646,329) and (639,272) follow the same phase (positions fixed; only their rotation matrix is set).

### 7.5 Crate transit and the score (`0040c360`)
Each product in transit moves `dt * (516 - 300) / 2000` units per tick (**216 units over 2000 ms**). It is drawn at x = 700 once its value is >= 477. When the value exceeds **516** it is removed, the **product counter `+0x3c` increases by 1** and the `counter` text is set to `%lu`; if at least one arrived this tick `template_success` plays. So **score = number of full trays that were collected and then arrived at the crate**. Products still in transit when the game ends are still counted (the wind-down waits for them), and the counter is not reduced by misses.

## 8. End of game (`0040a340`, argument `shutdown`)

* State = 5; `decel = speed / 300.0` (so the wheel stops in 300 ticks = 3 s).
* `factory_shut_down` plays at **every** end (normal or not); only when `shutdown == 1` (8 misses) is the music also switched to the empty name (`00409ef0` with `""`, which I take to stop it; inference). The normal end had already played `mini_game_production_over` at 60 s (tick step 5).
* `keeper` -> `chartext` gets the text `GetString(port .. "_" .. key)`; if that returns the missing-string marker `"#####"` it falls back to `GetString(key)`. `key` is `production_stopped` when `shutdown == 1`, otherwise from the **ratio** `count / ideal` where `ideal = (60000 / ingredientRate) / S` (integer divisions: 85 for S=2, 57 for S=3, 42 for S=4):
  * `ratio < 0.11`: `production_low`
  * `0.11 <= ratio < 0.19`: `production_med`
  * `ratio >= 0.19`: if `S < 4`: `ratio < 0.65` -> `production_high`, else `production_awesome`; if `S >= 4`: `ratio < 0.55` -> `production_high`, else `production_awesome`.
  Only the speech depends on the ratio; the score does not.
* `gamestart` is hidden, `gameend` and `keeper` made visible (the buttons "try again" and "ok"), the cursor returns to `cursor/cursor` hotspot (1,1) and the cursor shown flag is reset.
* The Lua reads the result with `GetProductCount()` when **ok** is pressed (`CloseWindow(GetProductCount())`). `GetProductCount` returns the counter as a number (a double). It does not depend on the state, so pressing "ok" works any time the button is visible (only after the end).

## 9. Pause (`TogglePause`, `0040abb0`)

* If state == 5: nothing.
* Not paused (any other state, including 0 and 1): remember the state in `+0x38`, state = 3, pause the sound manager (`FUN_00443ea0(soundmgr, 1, -1)`; inference that this is pause-all), show the `paused` frame, set the pause button's graphics/label to "resume" (via `pause_label` text "resume"), cursor to `cursor/cursor` hotspot (1,1) and show the cursor.
* Paused: state = saved state, resume sound, hide `paused`, label back to "pause", cursor `cursor/gun_aimer` (16,16).
* While paused the tick advances only its own clock: no elapsed time, belt, shots, wheel or transit. The 10 ms task keeps being scheduled. Mouse moves still aim but clicks do not fire (state != 2).
* Pausing during the countdown is allowed and resumes the countdown where it left off.
* `0040afc0` (Escape, deactivation): `TogglePause` unless state is 3 or 0.

## 10. Draw order (`0040c4f0`)

All sprites are drawn inside one 2D begin/end bracket, after the Lua-declared children's bitmaps. Order, back to front (positions are the float arguments passed, x then y; inference where marked):

1. four `fac_smallwheel` (49x48) at (16+100k, 608), k=0..3.
2. the belt list: for each item `fac_plate` (57x21) at (x, y+16) when y < 571, then the ingredient picture at (x, y).
3. the flywheel `fac_flywheel` (141x141) at (123,15), the tray wheel (with trays and their icons) at (300,300), the gun sprite at (300,300).
4. the loaded ingredient at (300,300) when state is 2, 3 or 5.
5. flying ingredients at their (x, y).
6. crate: with nothing in transit, `fac_cratelid` (117x24) at (693.5,504); otherwise products in transit (the product picture at (700, v) for v >= 477), `fac_cratefront` (114x70) at (695,537), `fac_bellows` (128x16) at (700,469); then the product picture at (699,548) (inference: the label on the crate).
7. the miss grid: for the n-th miss `missed_ingredient` and the ingredient picture at (631 + 46*(n mod 4), 144 + 46*(n / 4)); a 4x2 grid, so eight is the full board.
8. `fac_upperarm` (170x64), `fac_lowerarm` (171x64), two `fac_gear` (97x97), `fac_machineover` (202x165).
9. the recipe template at (700,300).
10. the timer text update (state 2 only).

Where the Lua children (counter, timer, buttons, frames) fall relative to this is not determined (they are windows; the FactoryGame is a child of the 800x600 `image/factory` bitmap).

Pixel sizes (PNG/JPG, Chocolatier 1): `fac_bellows` 128x16, `fac_cratefront` 114x70, `fac_cratelid` 117x24, `fac_flywheel` 141x141, `fac_gear` 97x97, `fac_lowerarm` 171x64, `fac_machineover` 202x165, `fac_plate` 57x21, `fac_smallwheel` 49x48, `fac_upperarm` 170x64, `fac_wheel(.jpg)` and `fac_wheel_mask.jpg` 493x493, `tray2..6` and `traycomplete2..6` 150x150, `gun_shoot.png` 500x150 (5 frames of 100x150, anchor (50,75)), cursors 32x32. `image/missed_ingredient.png` is 36x43 and `powerup.png` 900x300; both are in the archive. Background: `image/factory.jpg`.

## 11. Sounds (all loaded in the constructor; the slot numbers are the object offsets)

| Sound file | Slot | Played when |
|---|---|---|
| `countdown_tick.ogg` | `+0x3bc` | countdown number changes (step 6) |
| `minigame_starting_bell.ogg` | `+0x3c4` | countdown ends |
| `minigame_load_launch.ogg` | `+0x3cc` | each shot |
| `ingredient_success_01..05.ogg` | `+0x3d4 + 8k` | nth fill of a tray (see 7.2) |
| `ingredient_failure_01..08.ogg` | `+0x404 + 8k` | kth miss |
| `template_complete.ogg` | `+0x444` | played for the completing fill (Init copies it into success index S-1) |
| `factory_snatch.ogg` | `+0x44c` | collection machine starts |
| `template_success.ogg` | `+0x454` | a product arrives at the crate |
| `powerup.ogg` | `+0x45c` | all trays complete at once |
| `warning_bell.ogg` | `+0x464` | 8 s left |
| `mini_game_production_over.ogg` | `+0x46c` | 60 s elapsed |
| `factory_shut_down.ogg` | `+0x474` | the end of every game (normal or eight misses) |
| music `minigame.ogg` | | started by `StartGame`; stopped with an empty name when shut down |

The Lua also plays `charopen` for the dialog and uses `kFactorySetupAmbient`.

## 12. Chocolatier 2 (Lua and assets only; exe packed)

### 12.1 What is visible
`ui/factory_minigame.lua` (chunk dated 2007-12-06) creates `FactoryGame{...}` from the factory/product/type tables and wires natives. Everything below is from that Lua, `sim/producttype.lua`, `item/itemdefs.lua`, `ui/factory_status.lua` and the string table.

**Constructor keys** (bytecode lines 120-165): `x=ui.xOffset, y=ui.yOffset, w=ui.screenWidth, h=ui.screenHeight`, `warn` (= `gSim.warnOnMiss` is true and `product.name == "b_01"`; so the warning is for the starter bar and a global gate), `colorWarn` (= not `gSim.firstcolor`), `product`, `type`, and each of `traypath`, `trayEnd`, `ingredientpath`, `productpath`, `traycount`, `ingredientrate`, `bonuscount`, `maxspeed` taken from the product, falling back to the type; `colorcount` = product's or type's (a value > 1 chooses the colour intro text `factory_start_colors`). Hard-coded in the Lua: `traycycle=30000, conveyortime=1200, flytime=500, producttime=2000, speedup=1.1, starttime=60000, countdown=2400, bonusflytime=500`.

**Defaults** in `sim/producttype.lua` (`LProductType`): `traycount 5, colorcount 1, traycycle 30000, conveyortime 1200, ingredientrate 250, flytime 500, producttime 2000, speedup 1.1, maxspeed 8, starttime 60000, countdown 2400, bonusflytime 700`. (The Lua override of `bonusflytime` to 500 makes the 700 unused.)

**Per product type** (`item/itemdefs.lua`; `traypath`, `ingredientpath`, `productpath` are point lists, 8 to 15 points each): square `trayEnd 0.98, traycount 7, colorcount 2, handicap 0.9`; infusion `0.99, 7, 2, 0.8` (price 49,000); sauce `0.98, 7, 3, 1.15` (380,000); praline `0.97, 8, 2, 1.22` (625,000); truffle `0.99, 7, 2, 1.25` (850,000); exotic `0.98, 7, 3, 1.28` (1,350,000). The starter product `b_01` has its own table: `trayEnd 0.95, traycount 5, colorcount 0`.

**Natives used by the Lua**: `ResumeMinigame()` (start button, resume button, restart), `PauseMinigame()` (the on-screen `pause` button), `ResetMinigame()` (try again; then `SetMusicState("")`, `ResumeMinigame()`), `GetProduction()` (number; `done` -> `CloseWindow(GetProduction())` after `gSim.warnOnMiss = false`), `PauseMusic()`, `ResumeMusic()`, `SetMusicState("factory_wait" | "" | "normal")`, `SetAmbient("")`, `EnableWindow`, `SetLabel`, `HelpDialog(7)`. The Lua exposes **callbacks the native calls**: `MinigamePause()` (paused text `factory_paused`), `MinigameWarn()` (`factory_waste`), `MinigameColor()` (sets `gSim.firstcolor = true`, text `factory_color`) and **`MinigameEnd(performance, fpData)`**. The first three show the `info_dialog` with the `resume` button and hide `restart`, `done` and the end panel (they call `PauseMusic()` first); `MinigameEnd` restores the music state to `factory_wait`.

`MinigameEnd` (lines 85-115): `performance *= (product.handicap or type.handicap or 1)`; the key is `production_awesome` by default, `production_stopped` if `< 0`, `production_low` if `< 11`, `production_med` if `< 19`, `production_high` if `< 55`; text key `<port.name>_<key>`; the label `performance` gets `tostring(GetProduction())` and the `end_panel` text the string with `%1% = GetProduction()`; it then hides `resume` and shows `restart`, `done`, `info_dialog` and `end_panel`; `if fpWrite then fpWrite(fpData)` (telemetry, may be left undefined). So `performance` is a **percentage-like number** with the same cut-offs as Chocolatier 1's ratio x 100 (0.11, 0.19) but a single 0.55-equivalent top cut-off and a per-type `handicap` that replaces the Chocolatier 1 distinction by recipe size. What the native passes as `performance` is not visible (Unknown).

**Dialog layout** (Chocolatier 2, 800x600 UI): background `ports/<factory>_bg` is the stage; a `Bitmap "stack"` (`image/machine_stack`, 250x600) at x = 565; `TextButton "pause"` at (82,50); Text `cases`(661,18), `counter`(645,29, 92x35), `timer`(634,80, 113x35), `countdown`(150,150, 300x300); an `info_dialog` Frame at (20,100) holding `infotext` (345x190), `end_panel` (current_output/performance/cases_per texts) and buttons `start` (label `game_go`), `help`, `cancel`, `resume`, `done`, `restart` ("try_again"). Intro text `factory_start` (colour variant `factory_start_colors`): "Aim and click to fire ingredients. Match the full tray you see on the right. I'll speed the machines up as you succeed. Waste ingredients, and I'll slow things down." (colours: "Fill each tray with ingredients of the same color for a production bonus. Waste 8 ingredients, and I'll be forced to halt the machines.").

`ui/factory_status.lua`: after a `DisplayDialog("ui/factory_minigame.lua", {factory=self, char=char})`, `type(result) == "number"` -> `self:SetConfiguration(self.product.name, result)`, `ChangeConfigurationMessage()`, `gSim:Tick()`, close the status dialog; a nil result (cancel) leaves it alone. Before launching, it lists missing recipe ingredients (`LItem:ByName(name).inventory < count`) and, if any, shows `ui/factory_out.lua` (text `factory_noingredients` plus a rollover list of the missing items, laid out in one column up to 3 items, else two columns) and returns without starting the game; that dialog's own `ok` is `close=true, default=true`. The status dialog shows the product (`item/<name>_big`), `current_output` (`rate`), `cases_per`, the machines, and the `reconfigure` / `minigame` TextButtons.

**`FactoryStatus` window** (`ui/ledger_factories.lua`, the ledger's Factories tab): `FactoryStatus{x=0,y=0,w=ui.ledgerWidth,h=ui.ledgerHeight, bars=<list>, products=<list>, colorLoads=GreenColor, colorFull=YellowColor, colorMedium=YellowColor, colorTrouble=RedColor}`. `bars` is a list of 5-number arrays `{left, top, right, bottom, fill}` (bytecode: Lua 5.0 `SETLIST` stores Bx+1 values) built per owned, producing factory and per recipe ingredient: the stock fraction `inventory / globalNeeds[ing]` divided by `weeksPerColumn[rank+1]` sets the bar's top; `products` is a list of 6-value tuples `{x, y, productName, rate, typeName, recipeSize}`. The window draws the bars and presumably the icons; its draw code is in the packed exe (Unknown).

### 12.2 Differences from Chocolatier 1 that the Lua and assets prove

| | Chocolatier 1 | Chocolatier 2 |
|---|---|---|
| layout | tray wheel, gun at the centre, L belt | `traypath`/`ingredientpath`/`productpath` point lists, `rope.png` and `rope_guide.xml` (10-frame pulley animation 40x30, 100 ms, loops), `machine_stack` 250x600; a path-driven tray conveyor like Chocolatier 3 |
| result | counter = rate | `GetProduction()`, plus a `performance` number to `MinigameEnd` |
| lifecycle natives | `StartGame`, `RestartGame`, `TogglePause`, `GetProductCount` | `ResumeMinigame`, `PauseMinigame`, `ResetMinigame`, `GetProduction` and Lua callbacks |
| colours / bonus | none | `colorcount`, `bonuscount`, `bonusflytime`, `image/trayN_ring`, `trayring.png` 50x50, `powerup.xml` + `powerup.png` (11 frames 150x150, sheet 900x300) |
| countdown / duration | 2400 / 60000 | 2400 / 60000 (same) |
| hard-coded times | | `traycycle 30000`, `conveyortime 1200`, `producttime 2000`, `flytime 500` |
| tray speed | wheel speed x `speedup` per tray, max `maxspeed`; halves excess on miss | `speedup 1.1`, `maxspeed 8` keys exist; the intro text says wasted ingredients slow the machines, the exact rule is not visible |
| misses to end | 8 | 8 (the intro text says so) |
| gun anim | 5 frames, no script in the XML | 6 frames 113x126 (sheet 455x253), `DoAnim`: 60 ms per frame, frames 1,2,3,4,5,6,1 |
| ingredient feed | 350 ms | `ingredientrate` default 250 (type), overrideable |
| first-miss warning | global `gFirstMissWarning` and product `basebars` | `gSim.warnOnMiss` and product `b_01` |

### 12.3 Chocolatier 2 sheets and frames
* `image/gun_shoot.xml`: 6 frames of 113x126 at (0,0), (114,0), (228,0), (342,0), (0,127), (114,127) of a 455x253 sheet, anchors (56, 51/74/69/63/58/52); image anchor (75,87). Script: `delay(60); frames{1,2,3,4,5,6,1}`.
* `image/powerup.xml`: 11 frames of 150x150 on a 900x300 sheet (the 11th is `blank.png`), anchor (75,75), no script (the native, or the sheet user, sets the timing; the Chocolatier 3 notes say 100 ms per frame).
* `image/rope_guide.xml`: 10 frames of 40x30 on a 122x123 sheet (3 columns, 1 px gutter), anchors (20,15), script `delay(100); loop frames{1..10}`.
* Chocolatier 1 `gun_shoot.xml` has **no script**; whether the sprite then plays all five frames, stays on frame 1, or errors is not known (Unknown). The Chocolatier 1 `powerup.xml` has the same 150x150 frame layout as Chocolatier 2's and its PNG is in the archive.

### 12.4 Gun animation in Chocolatier 1
`Fire` calls the sprite's `DoAnim` (`0044e460`). With no script in the XML the call has nothing to run. The static picture is frame 1 (`FUN_0044d9c0(...,0)`). Treat the gun as non-animated unless a test of the original shows otherwise.

## 13. Comparison with `docs/re/chocolate-factory.md` (Chocolatier 3) and `docs/design/factories.md`

| | Chocolatier 1 (`TFactoryGame`) | Chocolatier 3 (`ChocolateFactory`) |
|---|---|---|
| tick | fixed 10 ms, same task call shape | fixed 10 ms with catch-up, `TScalingTask` per object |
| duration | 60000 ms after 2400 ms countdown | `gametime` 60000, `countdowntime` 3000 default |
| warning | `warning_bell` at 52 s | `warntime` (Lua 10000) |
| speed | `startspeed`, `speedup`, `maxspeed` (type), halve excess on miss | tray scale x `speedup` (1.1) to `maxspeed` (8); miss rule differs |
| shot | 1.2 px/ms, ends at screen edge, point test, radius 76.7 | 1000 px in 250 ms (4 px/ms), segment test, radius 91 |
| supply | deck of `L` slots, 350 ms feed, gun-loaded freeze | 5-slot conveyor along a Bezier path with a head parameter |
| miss | leaves the 800x600 rectangle; 8 end the game | after the full flight; 8 end the game |
| score | trays that reached the crate | cases counted by the product belt |
| end text | by ratio against the ideal count | by score thresholds |
| colours / powerups | none | colours, rings, powerups, recycler |
| pause | state 3 stored; Escape/deactivate | `PauseGame` with an overlay |

Differences to remember when porting the later game: Chocolatier 1 is a different mechanism, so the Chocolatier 3 implementation cannot simply be parameterised into it; the hit test, projectile motion and score are new code.

## 14. Unknown

* The sign conventions of the rotation matrices (`00409da0`, `0042b520` argument order) and so the on-screen direction of the wheel and the gun art orientation. The collection line (y1 = 252) side is an inference from the arm positions.
* The exact meaning of `FUN_004ac950` in the aim (probably `acos`); cosmetic only.
* Whether the 800x600 children (counter, timer, pause button) draw before or after the sprites in `Draw`; the base `Draw` call position was not read.
* The `DoAnim` behaviour for a script-less `gun_shoot.xml`.
* The bytes of two small float tables used by the arm animation beyond the ones listed (`004e7094..` area), and the gear rotation formulas.
* Where the engine seeds `rand()` for the FactoryGame beyond `UtilMathSeedRandom` being available to Lua (the Lua calls it from `sim/utility.lua` at game start; the call site was not checked).
* The recipe-table iteration order, which fixes slot order (and so the icon order on the template).
* Everything native in Chocolatier 2: the hit test, the shot speed, the miss penalty, the colour bonus, what `performance` is, how `GetProduction` is derived, `bonuscount`, `trayEnd`, the ring and rope behaviour, draw order and sound events. The exe is packed.
* The `FactoryStatus` drawing (beyond the Lua constructors).
