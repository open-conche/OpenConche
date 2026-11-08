# Chocolatier 2 factory mini-game: what the Mac binary says (`TFactoryGame`)

Source: the Chocolatier 2 Mac app bundle's `Contents/MacOS/Chocolatier2`, **i386 slice** (a PPC + i386 universal binary
with symbols). Decompiled with Ghidra (a workbench script) into `re/choc2/` (git-ignored, like `re/mac/` for
Chocolatier 3): `re/choc2/decomp/TFactoryGame.c`, `Tray.c`, `Recipe.c`, `Ingredient.c`, `BonusRing.c`, `index.tsv`. The i386 slice
was cut out with `lipo -thin i386` into `re/choc2/Chocolatier2_i386` first (the original install was not modified). Numbers that Ghidra
could not resolve (hidden float arguments, static initialisers) were read from `otool -tv` of the same slice. The Mac app also
ships the Lua as plain text under `Contents/Resources/assets/` (`ui/factory_minigame.lua`, `item/itemdefs.lua`,
`sim/producttype.lua`), which is how the Lua side below was checked.

Labels: **FACT** = read from the binary (address given), **INFERENCE** = reading around a gap, **UNKNOWN**.
Addresses are the file/virtual addresses of the i386 slice (`TFactoryGame::Draw` is 0xc48e, and so on). Field offsets are
offsets into `TFactoryGame`.

The binary is the same game the Windows `chocotwo.exe` runs (same Lua keys, same assets), so these facts apply to it. The mechanism
is Chocolatier 3's (a gun, a ring of trays, a conveyor), but the numbers and many rules are not.

## 1. Functions and who calls them

`TFactoryGame` (window type `FactoryGame`): ctor 0xec02 (registers the four Lua natives), `PostChildrenInit` 0xe9d0, `Init` 0xf612,
`Draw` 0xc48e, `OnTaskAnimate` 0xe0fe (the 10 ms step), `TickTrays` 0xdd8e, `TickConveyor` 0xa4c2, `TickFlying` 0xad68,
`TickProducts` 0xaa86, `TickBonuses` 0xa98a, `CheckCollisions` 0xd9ea, `CheckBonusClear` 0xd90a, `TickProduction` 0xa092,
`AddBonus` 0xc1d2, `CreateProduct` 0xa146, `RandomizeIngredientQueue` 0xa35a, `OnMouseMove` 0x9f8e, `OnMouseDown` 0xa242,
`OnKeyDown` 0xa93c, `OnMessage` 0xa968, `PauseGame` 0xa7c6, `ResumeGame` 0xa678, `ResetGame` 0xe8e0, `GameOver` 0xb0b4,
`GameAbort` 0xac00, `CleanUp` 0xe834, `GetProduction` 0x9f80. Helper classes: `Tray::AcceptIngredient` 0xd3d4, `Tray::Clear`
0xd772, `Recipe::GetSlotPosition` 0xa182, `Ingredient`, `BonusRing`.

**Lua natives (FACT, ctor):** `PauseMinigame` -> `PauseGame`, `ResumeMinigame` -> `ResumeGame`, `ResetMinigame` -> `ResetGame`,
`GetProduction` -> returns the uint at +0x5a4. `OnMessage` calls `PauseGame` on message 0x3e9; `OnKeyDown` calls it on Escape when not paused.
`PostChildrenInit` takes the window size (+0x528 = width, +0x52c = height), finds the Text children `counter` (+0x574), `timer`
(+0x578) and `countdown` (+0x57c) and calls `ResetGame`.

**Lua called back by the binary (FACT, via `DoLuaString` on the window manager's script):** `MinigamePause()` (PauseGame, only when not already paused and the game
is running: +0x5a0 != 0), `MinigameWarn()` (first miss when `warn`), `MinigameColor()` (see 5), and
`MinigameEnd(%d,%s)` (see 3). The second argument is the string `nil` unless the global `FirstPeekActive` is set (a developer
stats table), so `fpData` is nil in a shipped game.

## 2. Style keys and their defaults (Init)

Each is `TWindowStyle::GetNumber(name, default)`. Defaults decoded from the immediate doubles (FACT). The Lua
(`ui/factory_minigame.lua`) passes `traycycle=30000 conveyortime=1200 flytime=500 producttime=2000 speedup=1.1 starttime=60000
countdown=2400 bonusflytime=500` and `traycount colorcount ingredientrate bonuscount maxspeed` from the product or its type
(`sim/producttype.lua` supplies `ingredientrate=250`, `maxspeed=8`; `bonuscount` is nil, see 5).

| key | default | field | note |
|---|---|---|---|
| `xOffset`, `yOffset` | 0 | +0x30, +0x34 | applied to the three paths with `TCurve::Move` |
| `warn`, `colorWarn` | false | +0x570, +0x571 | |
| `trayEnd` | 0 | +0x88 (float) | |
| `traycount` | 7 | +0x538 (byte) | |
| `colorcount` | 2 | +0x539 (byte) | **0 means colours off; any non-zero (including 1) turns them on** |
| `traycycle` | 30000 | +0x53c | ms per lap at speed 1 |
| `conveyortime` | 1200 | +0x540 | ms for an ingredient to ride the whole path |
| `ingredientrate` | 350 | +0x544 | ms between spawns (Lua passes 250) |
| `flytime` | 500 | +0x548 | see 1 of the shot section below |
| `producttime` | 2000 | +0x54c | ms for a product to ride the product path |
| `speedup` | 1.1 | +0x550 | |
| `maxspeed` | 8 | +0x554 | |
| `starttime` | 60000 | +0x534 | |
| `countdown` | 2400 | +0x530 | |
| `missx`, `missy`, `missdx`, `missdy` | 631, 136, 39, 38 | +0x558..0x564 | the Lua passes none; **`missdy` is never read** |
| `bonusflytime` | 500 | +0x568 | the Lua passes 500 (type default 700 is not forwarded) |
| `bonuscount` | the recipe size | +0x56c | the **credit** for a colour match, not a number of rings (5) |

Also read: `traypath`, `ingredientpath`, `productpath` (Bezier tables, moved by xOffset/yOffset), `gun` (default `image/gun_shoot.xml`;
the Lua does not pass it), `product` (`name`; `recipe` = a table `{itemName = count}` iterated with `GetNext`, each item repeated `count` times; item
pictures `item/<name>`), `type` (`name`). Fixed constants from the static initialiser at 0x14660a (FACT, `otool`): `TRAY_RADIUS = 75`,
`SLOT_RADIUS = 50`, `ING_RADIUS = 32`, **`TEST_RADIUS = 5881` (an integer used as a squared distance, so a radius of 76.69)**, and
`_FactoryColors` (RGBA floats) = 0 red (0.9219, 0.0547, 0.0547), 1 blue (0.1765, 0.2188, 0.7734), 2 yellow (1.0, 0.8184, 0.0898).
Colour indices are 0-based (the engine's `factoryColor` is 1-based, so engine colour = binary colour + 1).

Slot `k` of an `n`-slot recipe sits at `(sin(2 pi k / n) * 50, -cos(2 pi k / n) * 50)` from the tray's centre (FACT, `GetSlotPosition`).

## 3. The 10 ms step and the state flags (`OnTaskAnimate`)

A word at +0x6ac holds the active parts. `ResetGame` sets it to 0x41. When the countdown ends it becomes **0xfeff** (everything). `GameOver` (time up) sets
**0xbd** (trays 0x01, flying 0x04, products 0x10, collisions 0x08, bonus-clear 0x20, deliver 0x80; no conveyor 0x02, no timer 0x40).
`GameAbort` sets **0x115** (trays, flying, products, 0x100 slow-down). Per tick, in this order (FACT): the slow-down (0x100), the countdown/timer (0x40), `TickTrays`
(0x01), `TickConveyor` (0x02), `TickFlying` (0x04), `TickProducts` (0x10), `TickBonuses` (0x04), `CheckCollisions` (0x08),
`CheckBonusClear` (0x20), `TickProduction` (always), then the end test.

- **Countdown (FACT):** +0x59c starts at `countdown` (2400) and loses 10 per tick. The Text child shows `ceil(new / 1000)` and is updated only when
  that value changes, with the tick sound `countdown_tick`. `ResetGame` sets the text to the empty string and nothing sets it again before the
  first change, so the **first 400 ms show nothing, then "2" for 1 s, then "1" for 1 s** (no "3"). When the value is <= 10 the countdown ends: text "",
  sound `minigame_starting_bell`, music state `factory`, flags 0xfeff.
- **Game timer (FACT):** +0x5a0 starts at `starttime`, loses 10 per tick; text `%lu:%02lu` of `(t/1000)/60`, `(t/1000)%60` (integer division, so 60000 shows
  `1:00` and 59990 shows `0:59`). `warning_bell` plays once when it crosses 7000. At <= 10 `GameOver` runs.
- **GameOver (FACT):** `SetTopModalOnly(false)`, cursor `image/cursor`, timer 0 ("0:00"), flags 0xbd, conveyor list cleared, `factory_shut_down` sound, and the tray speed
  is raised to **5.0 if it is lower** (so full trays are carried to the machine).
- **GameAbort (FACT):** same cursor and text, timer 0, flags 0x115, conveyor cleared, `factory_shut_down`, tray speed set to **0.5**. With 0x100 the speed is
  multiplied by 0.99 every tick, and when it falls below 0.3 it is set to 0 (and the pulley animations pause).
- **End (FACT):** MinigameEnd is called, once (+0x580), when the timer is 0, the flying list is empty, the product list is empty, **and either no tray is full or the flags
  are exactly 0x115 (aborted)**. Bonus rings in flight and the speed are **not** part of the test. It then sets 0x100 and calls
  `MinigameEnd(performance, nil)`.

### Tray speed (+0x5ac, "speed"), reset to 1.0
- Full tray: `speed = min(speed * speedup, maxspeed)` (any full tray, matched or not).
- Miss: `speed = (speed - 1) * 0.5 + 1` (FACT: it halves the excess over 1, so 8 -> 4.5, 2 -> 1.5, 1 -> 1). Chocolatier 3 halves the speed with a floor of 1.
- Trays: `acc += speed * 10` per tick, wrapped at `traycycle`; tray `i` of `N` is at phase `i / N + acc / traycycle` (wrapped) on `traypath`. Not tied to pause except by the flags.

## 4. Shots, hit test, misses (items 1 and 4)

- **Aim (FACT, `OnMouseMove`):** only while not paused. `dir = normalise(mouse - gunPos)`, rotation = `acos(dir.y)`, negated when `dir.x > 0`. At rest `dir = (0,-1)`,
  rotation pi. The gun sprite's nozzle therefore points down at rotation 0 (the engine's 180 degree offset is right, and positive is clockwise).
  **The direction is taken at the last mouse move, not at the click** (`OnMouseDown` does not read the click point).
- **Gun position (FACT):** the end point of `ingredientpath`. The product crate is at the end point of `productpath`.
- **Fire (FACT, `OnMouseDown`):** needs the front ingredient at the end of the belt (+0x484 ready), a non-empty belt, and not paused. It moves the front ingredient to the flying list with
  `t = 0` and the current aim, clears ready, counts a shot, plays `DoAnim` on the gun and the sound `minigame_load_launch`.
- **Flight (FACT, `TickFlying`):** each tick `t += 10` and `pos = gunPos + dir * (t * W / flytime)` where **W is the window's width** (+0x528), not 1000 px. With the window 800 wide
  and `flytime` 500 this is 1.6 px/ms (16 px per step), not the 2 px/ms the engine used. **There is no end at t = 1:** the shot ends when its
  position leaves `0 <= x < W, 0 <= y < H` (H = +0x52c), which is a miss.
- **Hit (FACT, `CheckCollisions`, after `TickFlying`):** for each flying ingredient in list order, for each tray in index order that is not full (`count < size`): a **point** test
  `|trayPos - shotPos|^2 <= 5881` (radius 76.69; the tray's position is its sprite position, no +16 and no segment sweep), then `Tray::AcceptIngredient`. The first tray that accepts wins; a tray that
  does not accept (no empty slot for that ingredient) is skipped and the shot flies on.
- **Accept (FACT, `Tray::AcceptIngredient`):** the tray takes the ingredient into the first *empty* slot whose recipe item equals the ingredient's type (slot order). Colours (only when `colorcount != 0`): the first ingredient sets
  the tray colour and a "colour match" flag; each later one keeps the flag only if its colour equals the tray colour (**there is no wild colour**); a mismatch clears the flag for the rest of the tray's life, hides the tray ring
  and removes the slot circles (`image/trayring`) from the tray. The slot circles of a matching tray are tinted with the tray colour, and so is the tray's ring (`tray<N>_ring`). A full tray always shows its ring (white if mismatched).
- **Miss (FACT, `TickFlying`):** the shot is put at the miss cell and kept in a list **only while that list has fewer than 8 entries**. Cell `n` (0-based) is `(missx + (n % 4) * missdx, missy + (n / 4) * missdx)`: **the row step is `missdx` (39), not `missdy`**, so with the defaults the cells are
  x = 631, 670, 709, 748 and y = **136 and 175**. Then `speed = (speed - 1) * 0.5 + 1`; then the sound `ingredient_failure_<size + 1>` where `size` is the list length after the push (**the first miss plays `ingredient_failure_02`, the seventh `_08`, and the eighth plays `factory_snatch`**); then if the size is 1 and `warn`, pause and call
  `MinigameWarn()` (and clear `warn`); else if the size is 8, `GameAbort`. The speed-up from a full tray is separate (3).
- **Missed marker drawing (FACT):** `image/missed_ingredient` is drawn at the cell, then the ingredient picture on top, **both at scale 1.0** (the engine used 0.6 for the ingredient). The items are 32x32 and the marker 36x43. These are drawn **first**, under everything else.
- **Conveyor (FACT, `TickConveyor`):** each ingredient has its own progress in ms. A tick moves every ingredient by `step = min(10, conveyortime - frontProgress)` (10 when the belt is empty); the front is ready when its progress equals `conveyortime`. A new ingredient is made when the spawn timer
  (+0x5b0, reset to `ingredientrate`) is <= `step`; it starts with the carried remainder and the timer is reloaded to `ingredientrate - remainder`. So ingredients are spaced `ingredientrate / conveyortime` of the path (250/1200 with the Lua's values; confirms the engine's inference) and only move while the front is not at the end. Position = `ingredientpath(progress / conveyortime)` (the divide by +0x540 is in the code). Not scaled by the tray speed.
- **Bag (FACT, `RandomizeIngredientQueue`):** when the bag is empty it is refilled with `recipeSize * traycount` entries: type `recipe[i % recipeSize]`, and (colours on) colour `i % colorcount`; then each of the two vectors is shuffled **independently** (for each `i` swap with `rand() % n`; the type vector first, then the colour vector, alternating).
  Entries are taken from the back. (Chocolatier 3 uses 5 per recipe item and random colours.)

## 5. Full tray, colour match, bonus rings, products, score

- `CheckCollisions` after an accepted shot: plays `ingredient_success_<count>` (colours off or the flag cleared) or `color_match_<count>` (flag set), where for `count == recipeSize` the slot is `template_complete` or
  `colormatch_complete`. If `count == recipeSize`: tray speed as in 3. **If the flag is set (a colour match)**: `AddBonus` (rings), `Tray::Clear` with the sparkle animation, the tray's bonus-text timer is set to 1500 ms with its colour, and one **product** is created (`CreateProduct(0)`). If the tray is full but mismatched it stays full and rides to the machine.
- **Colour hint (FACT):** `MinigameColor()` is called (with a pause) when a tray reaches **exactly 2 ingredients** with the match flag still set, `colorWarn` is true, colours are on, and the tray is not yet full (this is *not* at completion). The flag is then cleared (once per window).
- **Bonus rings (FACT, `AddBonus`, `TickBonuses`, `Draw`):** a colour match makes `recipeSize` rings. Each ring flies a cubic Bezier `P0 = slot position + tray position`, `P1 = (400, 150 - 100 k)`, `P2 = (productpath(0).x, productpath(0).y - 50 k)`, `P3 = productpath end`, with `k` the slot index, at `t = elapsed / bonusflytime` (10 ms per tick, no speed scaling). **Each ring is drawn as the product's big picture `item/<product>_big` at scale (0.8, 0.75)**, untinted. When rings reach `bonusflytime` they are removed, and **once per tick in which any arrived** the production (+0x5a4) gains `bonuscount` (the recipe size, since the Lua passes none) and `timebonus.ogg` plays. So the credit is `bonuscount`, not one per ring.
- **Products (FACT, `TickProducts`):** every product has its own time (+10 per tick, not scaled), position `productpath(time / producttime)`; there is no "wait for the one in front". At `time >= producttime` the product is removed, production +1, sound `template_success`.
- **All trays full (FACT, `CheckBonusClear`):** if every tray has `count == recipeSize` (colour is not checked): every tray is cleared (sparkle) and a product is made per tray with start time `i * 80` ms (so they are spread along the belt), plus the `powerup.ogg` sound.
- **Delivery (FACT, `TickTrays`):** a full tray (and flags & 0x80) is **cleared and a product (time 0) made when its phase wraps (passes 1.0 -> 0), sound `factory_snatch`**, not at `trayEnd`. `trayEnd` (>= 1 means the wrap) is only the phase at which the machine's template and the fingers start to turn
  (`+0x6b8 = 0`, and the finger counter +0x6bc = 0 if it is >= 60).
- **Machine animation (FACT, `TickProduction`):** counters +0x6b8 and +0x6bc count 0..60 (+1 per tick, stop at 60); the template tray on the machine is rotated by `2 pi * t / 60` (600 ms per turn, at rest at 60).
- **Score (FACT):** +0x5a4 = products counted + ring credits; the `counter` Text shows `%lu`. `GetProduction` returns it; kept after an abort.
- **Performance passed to MinigameEnd (FACT, `OnTaskAnimate`):** `ideal = floor(floor(starttime / ingredientrate) / recipeSize)`, **plus `floor(starttime / ingredientrate)` when `colorcount != 0`**. `performance = (int)(production * 100.0 / ideal)` (truncated toward zero, no cap), and **-1 when the game was aborted** (flags == 0x115). With the Lua's values and 3 ingredients: ideal 80 without colours,
  **320 with colours** (the engine used 80 and a fractional value). The Lua then multiplies by `handicap` and cuts at 11 / 19 / 55.

## 6. Layout and drawing (items 3 and 5)

All draws use a centre anchor `(x, y)` (FACT: the tiling loops below only make sense that way). Draw order of `TFactoryGame::Draw` (FACT, in this order):

1. `image/gun_base` at the gun position.
2. The missed list: `image/missed_ingredient` then the ingredient's `item/<name>` at each cell, scale 1.
3. The crate **back**: `image/large_crate_open` when any product is on the belt, else `image/large_crate`, at the sprite whose position is the **end of `productpath` with y - 23**.
4. Each product on the belt: `item/<product>_big` (64x64) at its position, scale 1.
5. The crate **front**: `image/large_crate_front` at the same place, with a child sprite of `item/<product>_big` at the front's centre (the label), scale 1.
6. For each ingredient on the conveyor: the **pole** `image/pole.png` (64x32), tiled down from `y + 16 + poleH/2` in steps of `poleH` while the centre is <= the window height; then the **plate** `image/plate.png` (64x32) at `(x, y + 16 + plateH/2)` = `y + 32`. (Drawn even when paused.)
7. For each tray: the **rope** `image/rope.png` (40x40) tiled **up** from the tray's position: first centre at `trayY - 20`, step -40 while the centre >= -20; then the **pulley** `image/rope_guide.xml` (animated, frames 40x30, "DoAnim") at `(trayX, frameHeight/2)` = `(trayX, 15)` (INFERENCE for the 15: Ghidra shows `height * 0.5`; the frame is 30 high).
8. The gun sprite (`image/gun_shoot.xml`, rotated by the aim).
9. The conveyor ingredients (only when not paused and flag 0x02): when colours are on a ring `image/trayring` tinted with the colour (drawn with a spec at the ingredient's position), then the ingredient `item/<name>` scale 1.
10. For each tray: its sprite (`image/tray<N>` with the `tray<N>_ring` child, slot circles and the accepted ingredients as children), then, **while the tray's text timer is non-zero, the text `"%d"x` (`Nx` with N = recipe size)** in the tray colour, font `fonts/fertigo.mvec` size 80, centred on the tray, alpha `min(1, timer / 500)`.
11. The two **fingers** `image/factory_finger.png` (256x256, an arm pointing up) at `(612, 223)` and `(612, 373)` (window coordinates, not offset). Rotation at rest: 3 pi/4 and pi/4 (they point at the machine). While the finger counter t < 60: `3 pi/4 - a` and `pi/4 + a` with `a = 2 pi t / 60` (opposite full turns in 600 ms).
12. The **machine** `image/machine_<type name>` (250x250, e.g. `machine_square`) at **(690, 299)**; its only child is the template tray `image/tray<N>` (with one `item/<name>` child per recipe slot at the slot position) at the machine's centre.
13. The flying ingredients (colour ring first when colours are on, then the picture).
14. The bonus rings (the product's big picture, scale 0.8 x 0.75, along their Bezier).

Sizes: ingredient `item/<name>.png` 32x32, tray 150x150 (`tray3.png`), `trayring.png` 50x50, `gun_base` 120x120, crate images 100x94. The product picture is the **big** item (`item/<name>_big.png`, 64x64) at scale 1.0 everywhere.

## 7. Current engine vs the binary

| item | engine (before this change) | binary | action |
|---|---|---|---|
| shot speed | 1000 px per `flytime` (2 px/ms) | window width per `flytime` (1.6 px/ms at 800) | fixed |
| shot end | at t = 1 (1000 px) | leaves the window rectangle | fixed |
| hit test | segment, radius `tray.radius + 16` = 91 | point, radius 76.69 | fixed |
| miss speed | `max(1, speed / 2)` | `(speed - 1) / 2 + 1` | fixed |
| miss sounds | `failure_n`, 8th: shutdown only | `failure_(n+1)`, 8th also `factory_snatch` | fixed |
| miss cell rows | y 135, 174 (measured from the art) | 136, 175 (`missy`, `missdx` step) | fixed |
| miss ingredient scale | 0.6 | 1.0 | fixed |
| performance ideal | `floor(floor(start/rate)/size)` | plus `floor(start/rate)` with colours; truncated to int | fixed |
| bag | 5 per item, random colours | `size * traycount`, colours `i % colorcount`, shuffled apart | fixed |
| delivery of a full tray | at `trayEnd` | at the phase wrap; `trayEnd` starts the machine turn | fixed |
| machine turn | 400 ms | 600 ms | fixed |
| product belt | one waits until the previous passed 0.1 | independent | fixed |
| all trays full | products all at t = 0 | start times `i * 80` ms | fixed |
| product picture | `item/<name>`, 32 px, scale 1 | `item/<name>_big`, 64 px, scale 1 | fixed |
| crate | at the path end, always the open back | y - 23; `large_crate` closed unless a product is on the belt | fixed |
| plate | centre `y + 16` | centre `y + 32` | fixed |
| pole | tiles while `centre < 632` | while the centre <= 600 | fixed |
| rope | 40 px pieces on a fixed grid from y = 30 | pieces anchored at the tray (centre `trayY - 20 - 40 k`) | fixed |
| bonus rings | `image/trayring` tinted, straight to (689, 555) | big product picture 0.8 x 0.75 on a Bezier, credit once per tick | fixed (a sprite has one scale: 0.775 is used) |
| colour hint | after a full colour match | when a tray has exactly 2 matching ingredients | fixed |
| countdown digits | "3" at once | blank, "2", "1" | fixed |
| machine sprite | none ("no art") | `machine_<type>` at (690, 299) | fixed |
| fingers | none ("no art") | `factory_finger` x 2 | fixed (one timer drives the template and both fingers; the binary has two counters and restarts a finger only when its turn is over) |
| `Nx` popup | none | text for 1.5 s over the tray | done: a `TextSprite` child of the tray (layer 100, so right after the tray's other children, as in the binary), `fonts/fertigo.mvec` 80, tray colour captured before `clearTray`, alpha `min(1, timer/500)`, 1.5 s on the factory's 10 ms step (pause freezes, Reset clears); only with `ChocolateSpec::matchPopup` (Chocolatier 2). The text style flags beyond size 80 (bold, outline) are unknown and not applied. |
| abort and end | trays decay x 0.95 until < 0.05, then the end | speed 0.5, x 0.99 to 0; the end as soon as no shot and no product | fixed |
| aim | the click point | the last mouse-move direction | not changed (a click normally follows a move) |
| mismatch tray look | all rings white | slot circles removed, ring hidden until full | not changed |

## 8. Unknown

- The order in which the recipe table's items are iterated (Lua 5.0 `GetNext`), so which ingredient is slot 0.
- Whether `Draw` uses exactly `item/<name of product>_big` for the first operand (the string concatenation's first operand is hidden by the decompiler; the Lua `item/<name>_big.png` files exist for every product).
- The font style flags of the `Nx` text (the literal 5 and 80 are read; the drawn size and outline are not).
- Whether the fingers rotate about the image centre (assumed; the pivot circle in the art is near the centre).
- Whether the pulley's vertical position is the frame height / 2 (the 30 px frame gives 15); the animation speed.
- The exact sound the machine/finger plays on delivery beyond `factory_snatch` (0x674), and the `factory_finger` timing relative to the snatch.
- `TWindowStyle::GetNumber` truncation: the unsigned casts clamp to [0, 2^32-1], so a negative or fractional `flytime` does what a C cast does.

## 9. Engine changes made from this

All gated by new `ChocolateSpec` / `FactoryCommon` fields whose defaults are Chocolatier 3's behaviour (`hitRadius`, `flyOutOfWindow`, `deliverAtWrap`,
`machineMs`, `independentProducts`, `fullBonusStaggerMs`, `ringsAreProducts`, `ringCredit`, `slowAbort`, `plainSuccessSounds`, `bagRounds`, `machinePos`,
`fingerImage`, `boxOffset`, `boxClosed`, `choc2Misses`, `missScale`) and set by `choc2::factorySpec`. Not gated: the plate and pole placement (only Chocolatier 2 sets
`IngredientArt::plate` and `pole`), the Chocolatier 2 clock text (`FactoryCommon::clockTexts`), and the colour hint moved from the full-tray match to a new hook
`ChocolateFactory::onSecondMatch` (Chocolatier 3 never overrode either hook). Tests: `tests/test_choc2_factory.cpp`, `tests/check_choc2_factory.py`.
