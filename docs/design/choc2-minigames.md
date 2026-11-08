# Design: Chocolatier 2's mini-games, `FactoryGame` (+ `FactoryStatus`), `DiceGame` and `BonusGame`

How Chocolatier 2's mini-game window classes are built. Facts come from `docs/re/choc-factory-game.md` (FG §n),
`docs/re/choc-dice-game.md` (DG §n), `docs/re/chocolate-factory.md` (CF §n) and `docs/design/factories.md` (FD §n), and are
not repeated. Chocolatier 2's `chocotwo.exe` is packed, so a native behaviour is one of: **fact (Lua)** (what the scripts pass and
expect), **fact (asset)**, **fact (binary)** (read from the Mac binary's `TFactoryGame` and `TFactoryStatus`,
`docs/re/choc2-factory-binary.md` and `choc2-ledger-binary.md` section 8; the Windows exe is the same game), or an
**inference**. The dice and crates games have no Mac-binary document, so everything native about them is inference.
Conventions are those of `docs/design/choc2-port-and-ledger.md`: per-game classes in `src/games/choc2/`, natives in
`Choc2*Natives.cpp`, `api-choc2.tsv` rows feeding `Stubs_choc2.inc`, Chocolatier 3 output byte-identical (every change to a
shared class is data with today's values as defaults).

**FG 12.1 `FactoryStatus` entries** (fact, bytecode of `ui/ledger_factories.lua`, Lua 5.0 `SETLIST` stores Bx+1 values): each `bars` entry
has **5** numbers `{left, top, right, bottom, fill}` and each `products` entry **6** values `{x, y, productName, rate, typeName,
recipeSize}`.

## 1. What the scripts depend on

**Factory** (`ui/factory_minigame.lua`, all fact (Lua)):
- `FactoryGame{x y w h warn colorWarn product type traypath trayEnd ingredientpath productpath traycount colorcount
  ingredientrate bonuscount maxspeed traycycle=30000 conveyortime=1200 flytime=500 producttime=2000 speedup=1.1
  starttime=60000 countdown=2400 bonusflytime=500}` with **children** `cases`, `counter` (`#0`), `timer` (`#1:00`), `countdown`
  (`#3`) and the `info_dialog` Frame (buttons `start help cancel resume done restart`, texts `infotext`, `infotext_alt`,
  `end_panel` with `performance`). The `pause` TextButton and `stack` (`image/machine_stack` at x=565) are children of the
  background Bitmap, drawn before the FactoryGame.
- Lua to native: `ResumeMinigame()` (Start, Resume, and after `ResetMinigame()` for Try Again), `PauseMinigame()` (the `pause`
  button), `ResetMinigame()`, `GetProduction()` (Done: `CloseWindow(GetProduction())`; `factory_status.lua` then
  `SetConfiguration(product, result)` when it is a number). Cancel closes with nil.
- Native to Lua, **globals the native must call**: `MinigamePause()` (text `factory_paused`), `MinigameWarn()`
  (`factory_waste`), `MinigameColor()` (`factory_color`, sets `gSim.firstcolor`) and `MinigameEnd(performance, fpData)`:
  `performance *= handicap`; `< 0` stopped, `< 11` low, `< 19` med, `< 55` high, else awesome; text `<port>_<key>` with
  `%1% = GetProduction()`. `fpData` is only passed to `fpWrite` when both are truthy.
- `warn` = `gSim.warnOnMiss and product.name == "b_01"`; `colorWarn` = `not gSim.firstcolor`. Done clears `gSim.warnOnMiss`.
  Music is all Lua (`SetMusicState`, `PauseMusic`, `ResumeMusic`; `docs/re/choc2-audio.md`).
- Data (fact (asset), `item/itemdefs.lua`): `traypath`, `ingredientpath`, `productpath` are Bezier control-point lists. b_01:
  trays `(-75,300) (166,8) (490,361) (658,300)`, ingredients `(0,570)..(300,570)`, products `(689,300)..(689,555)`, 5 trays,
  `trayEnd .95`, `colorcount 0`, recipe `{sugar=1, cacao=1}` (a name to count map). `ingredientrate`, `maxspeed` and
  `bonuscount` are set by no product; the type defaults give 250 and 8, and **`bonuscount` is nil everywhere**.

**`FactoryStatus{x y w h bars products colorLoads colorFull colorMedium colorTrouble}`** sits in the ledger's Factories tab over
`image/ledger_factory` (six 83 px columns, each a pocket with a ~20 px circle at the bottom). `fill` = weeks of stock /
`weeksPerColumn[rank+1]`; the Lua puts each ingredient's 13 px icon at the fill level and the rate text itself. The product
tuple's `(x, y)` is the circle's centre.

**Dice** (`ui/gamble_dice.lua`, DG 2): `DiceGame{x y w h fit}` with Bitmap children `die_0..die_3` (player, y=290) and
`die_4..die_7` (opponent, y=190), all `image/die_unknown`. `RollFirst()`, then `while gDiceRollCount < 2 do Pause(50) end`;
`RollSecond()`, then wait for `>= 8` (the Lua zeroes the global only before the first roll, so it counts stopped dice
cumulatively); `StopOne()` from the `stop` button; `PlayerTotal()`, `CharacterTotal()`. All money is Lua.

**Crates** (`ui/gamble_bonus.lua`, DG 3): `BonusGame{x y w h}` with buttons `select_1..select_25` (5x5, 50x47, pitch 60) and the
shared natives `PrepareGame(bet)`, `SelectPrize(i, final)`, `SetCommand`. All money is Lua (`gSim:AdjustMoney`,
`message_*crates`; the tie quirk is in the Lua and stays).

## 2. Decisions

### 2.1 `FactoryGame` reuses Chocolatier 3's `ChocolateFactory`, not Chocolatier 1's wheel

`choc2::FactoryGame : ChocolateFactory` (`choc2/FactoryGame.{h,cpp}`), fed by a pure key adapter `choc2::factorySpec(style)`.
The Chocolatier 2 data is Chocolatier 3's design: point-list paths for trays, conveyor and products, a delivery parameter near
the end of the tray path, colours with `tray<N>_ring`, a gun at the conveyor's end, a product belt ending at (689,555), and
`help7.lua`'s ring tint (235,14,14) is exactly Chocolatier 3's colour 1. Chocolatier 1's wheel shares only the timings, so
Chocolatier 1 has its own class (`choc1::FactoryGame` with `FactorySim`).

Kept from Chocolatier 3: the `FixedStep` 10 ms catch-up step on the modal clock, the state machine, clock rows, speed rule
(x1.1 per full tray, cap `maxspeed`), tray motion, colour rules, full-board bonus, product belt, ENDING, 8-miss abort, scoring
and `autoAim`. The Mac binary changed these for Chocolatier 2 (all fact (binary), `choc2-factory-binary.md` 4 to 6; each is a
`ChocolateSpec`/`FactoryCommon` field whose default is Chocolatier 3's behaviour, set by `factorySpec`):
- a shot covers the window's width per `flytime` (500 ms) and ends at the window edge; the hit test is a point within
  sqrt(5881) = 76.69 of a tray centre;
- the miss speed rule is `(s - 1) / 2 + 1`; the abort slows the trays (speed 0.5, then x0.99) and the end comes as soon as no
  shot and no product is in flight;
- a full tray is delivered when its phase wraps (`trayEnd` starts the machine's 600 ms turn); products on the belt are independent
  and start `i * 80` ms apart when every tray is full;
- the bag is `recipeSize * traycount`, colours `i % colorcount`, shuffled apart;
- a colour match always makes recipe-size rings (the product's `_big` picture along a Bezier to the box, one credit per tick);
  `bonuscount` is a credit amount, not a ring count, and is nil in every product;
- the colour hint fires when a tray holds exactly two matching ingredients (the `onSecondMatch` hook), not on completion;
- the countdown digits are blank, 2, 1;
- the machine `image/machine_<type>` at (690, 299) with the template tray at its centre, two `factory_finger`s, the crate
  (closed `large_crate` unless a product is on the belt), the plate, the pole and the ropes; the product picture is
  `item/<name>_big` (64 px); a colour match shows an `Nx` popup over the tray for 1.5 s.

Key map (each key falls back to Chocolatier 3's default when absent):

| Chocolatier 2 | Chocolatier 3 meaning | Note |
|---|---|---|
| `traypath`, `trayEnd`, `traycount`, `traycycle` | `traypath`, `trayend`, `traycount`, `traytime` | |
| `ingredientpath`, `conveyortime` | `conveyorpath`, `conveyortime` | gun at the path's end, (300,570) for b_01 |
| `ingredientrate` (250) | conveyor spacing = `ingredientrate / conveyortime` | inference: one ingredient per `ingredientrate` ms |
| `productpath`, `producttime` | same | |
| `flytime` (500) | ms for a shot to cover the window's width | fact (binary) |
| `bonusflytime` (500) | `ringspeed` | |
| `colorcount` | `colorcount`; 0 is no colours | fact (binary); `rainbow` false (no rainbow ring art) |
| `starttime`, `countdown` | `gametime`, `countdowntime` | `warntime` not passed: Chocolatier 3's 8000 ms |
| `speedup`, `maxspeed` | same | |
| `warn` | `warnOnMiss` | first-miss key `factory_waste` |
| `product.recipe` (map) | `recipe` list: each name `count` times, **names sorted** | inference: Lua 5.0 `pairs` order is undefined, so which ingredient is slot 0 is unknown (`choc2-factory-binary.md` 8); sorted is deterministic |
| (none) | `missed`: 8 points, `(missx + (n % 4) * missdx, missy + (n / 4) * missdx)` | fact (binary): defaults 631, 136, 39, relative to the stack at x=565 |
| `product.name` | belt picture `item/<name>` | |

Art and sounds are data in the spec (item prefix `item/`, gun sheet `image/gun_shoot.xml`, gun base `image/gun_base`, ingredient
ring `image/trayring`, `image/tray<N>` and `tray<N>_ring`, sparkle `image/powerup.xml`, miss marker `image/missed_ingredient`).
`gunRotationOffsetDeg = 180` is fact (binary): the sheet's nozzle points down at rest. Off for Chocolatier 2: conveyor and tray
pistons and the timer dial.

Sounds: `choc2::factorySound` (pure) maps the Chocolatier 3 event names to Chocolatier 2 files, played as `PlaySound` does:
`clock_tick`->`countdown_tick`, `start_bell`->`minigame_starting_bell`, `whistle`->`warning_bell`,
`launch`->`minigame_load_launch`, `snatch`->`factory_snatch`, `ingredient_success_N`->`ingredient_success_0N`,
`ingredient_color_match_N`->`color_match_N`, `ingredient_complete`->`template_complete`,
`ingredient_complete_colormatch`->`colormatch_complete`, `ingredient_complete_full_bonus`->`powerup`,
`chocolate_counted`->`template_success`, `ingredient_failure_N`->`ingredient_failure_0N` and the shutdown to
`factory_shut_down` (the miss sounds are the binary's `failure_(n+1)`, the eighth also `factory_snatch`). The intro, outro and
game-over events map to nothing (music is Lua). Inference: the mapping of event to file name. `factory_stall` and `timebonus`
stay unused.

### 2.2 Lifecycle, callbacks, `performance` and `GetProduction`

- **Start state.** After construction the game is reset and idle (`started_ = false`). `ResumeMinigame()`: not started starts
  the countdown, then sets `started_`; started and paused resumes; `Over` does nothing. `ResetMinigame()` resets to idle.
  `PauseMinigame()` pauses only while started and not `Over`.
- **Callbacks.** `pauseGame` calls a hook `notifyPaused(L, key)` (default nothing). Chocolatier 2 calls the global by key:
  `game_paused` to `MinigamePause`, `factory_waste` to `MinigameWarn`, `factory_color` to `MinigameColor`, never while idle
  (so `resetGame`'s internal pause calls nothing). Globals are looked up by name at each call and run with `lua_pcall` on
  `luaForStep()` (the caller's state inside a native, else the main state); errors are logged as `[lua-error]` and a missing
  callback is logged once. Nothing is stored. Escape pauses the same way (inference, as Chocolatier 1).
- **First colour match.** If `colorWarn`, the second-match hook clears it and pauses with `factory_color`; a mid-step pause like
  the first-miss pause.
- **Game over.** `showGameOver` is virtual; Chocolatier 2 calls `MinigameEnd(performance, nil)` and touches no siblings.
- **`GetProduction()` = the score** (products counted at the belt end plus rings), returned at any time and kept on an abort.
- **`performance`** (fact (binary) 5): `trunc(100 * production / ideal)`, `ideal = floor(floor(startMs / rateMs) / recipeSize)`
  plus `floor(startMs / rateMs)` when colours are on; 0 when `ideal` is 0; **-1 after the 8-miss abort**. With b_01's handicap 0.9
  the Lua's cut-offs read up to 15 cases as low, 16 to 26 as med, 27 as high.
- **Child names and texts** are data (`counter`, `timer`, `countdown`; Chocolatier 3 uses `score`, `time`, `countdown`). Reset sets
  `timer` to `m:ss` of `starttime` ("1:00") and `countdown` to `""`; `startGame` shows blank, then 2, 1 (2400 ms).
- **Siblings** `go`/`pause`: the Lua's Pause button has no name, so `parent()->find("pause")` finds nothing; it is neither
  hidden nor shown by the engine (harmless: `PauseMinigame` is a no-op while idle).

### 2.3 Draw order

The `info_dialog` is a **child** of the FactoryGame (Chocolatier 3's overlay was a sibling drawn later), so Chocolatier 2 draws
the sprite tree in `draw` (before the children) and nothing in `postDraw`: background Bitmap and stack, then trays, ingredients,
gun, rings and products by layer, then `cases`, `counter`, `timer`, `countdown`, `info_dialog`.

### 2.4 `FactoryStatus`

`choc2::FactoryStatus` (`choc2/FactoryStatus.{h,cpp}`) is a `ui::Window` that parses `bars` and `products` at `init` (fact
(binary) `choc2-ledger-binary.md` 8, `TFactoryStatus::Draw`). A bar fills from its bottom up to `min(fill, 1)` of its height in
one colour: `fill >= 1` Loads, `> 0.75` Full, `> 0.5` Medium, else Trouble. Each product draws `item/<name>` falling into
`image/machine_<type>_small`, the `image/tray<N>_mini` rotating by `t * 2 pi`, and for a factory with rate 0 a blinking
`image/missed_ingredient` at alpha 0.65 in the second half of each second. The fall and rotation run only while
`StartProductionAnim` is on (during a trip); the blink always, on the panel's own 50 ms clock. Inference (marked unknown in the
binary doc): the fourth texture argument of the falling item is its alpha. No natives of its own; it is rebuilt with its
`DynamicWindow` tab.

### 2.5 `DiceGame`

Chocolatier 1's mechanism (DG 1.5, 1.6) per roll, generalised to a list of dice (`choc2/DiceGame.{h,cpp}`). **All of this is
inference**: no native was read.
- `RollFirst()` rolls dice `{0, 1}`, `RollSecond()` dice `{2..7}`; each is ignored while a roll runs.
- At the roll's start the final faces are drawn **from the game stream `ctx.rng`, `range(1, 6)`, in list order** (2 draws, then
  6); nothing else draws from it.
- A 62 ms window animation with catch-up on the modal clock. All the roll's dice spin while `elapsed <= 2000`, then
  `remaining = n - (elapsed - 2000) / 750`; dice stop **in list order** (player's first, opponent's left to right; Chocolatier 1
  stopped the highest index first). The first roll ends at 3534 ms (stops at 2790, 3534), the second at 6510 ms.
- `StopOne()` is Chocolatier 1's `StopDie`: `elapsed = (n - (remaining - 1)) * 750 + 1938`, so the next die stops on the next
  tick; a no-op when nothing rolls.
- Each stopped die shows `image/die<v>` and sets `gDiceRollCount = (gDiceRollCount or 0) + 1`. Spinning dice show faces from a
  local xorshift seeded by the roll count. Deviation: Chocolatier 1 drew 4 `rand()` per tick from the shared stream, which made
  the game stream depend on Stop timing.
- `PlayerTotal()` / `CharacterTotal()` sum the stopped faces among dice 0-3 / 4-7. Unrolled dice keep `die_unknown`. No native
  sounds (the buttons and the Lua play `roll_dice`, `dice_win`, `dice_lose`).

### 2.6 `BonusGame`

A `CratesGame` built from `CratesSpec::bonus()` (`src/games/common/Crates.{h,cpp}`; the spec holds type name, count, factor
table, art names and sound route, and `CratesSpec::chocolatier3()` is today's constants and the default). Prepare, select, the
400/800 ms animation, the text and the shared natives are unchanged (`choc2/Choc2BonusNatives.cpp` registers only the type).
- Count 25. Art `image/large_crate`, `large_crate_open`, `large_crate_front` (100x94, drawn at scale 0.5 over the Lua's 50x47
  `select_N` buttons) and `whammy_devil`. Inference: no Lua names `large_crate*` or `whammy_devil`, so a native draws them, and the
  triple mirrors Chocolatier 3's closed/open/mask.
- Factors (**guess**): Chocolatier 3's 21 scaled by 25/21 per value class: `4x0.435, 0.4, 5x0.35, 5x0.25, 3x0.24, 0.15, 0.1,
  0.05, 4x0` (sum 6.16, 0.246 per crate against Chocolatier 3's 0.244; 4 whammies is 16% against 14.3%). Single-pass shuffle
  with `rng % 25`.
- Sounds `crate_click`, `crate_with_money` and `crate_no_money` are Chocolatier 2 files played as `PlaySound` does.

## 3. Types, ownership and lifetimes

```cpp
// src/games/common/factory/Chocolate.h: everything ChocolateFactory::init reads, plus the art (Chocolatier 3 defaults = today's).
struct ChocolateSpec { FactoryCommon common; recipe, colorCount, rainbow, curves, timings, look, machine, IngredientArt, gunSheet,
                       gunRotationOffsetDeg, pistons/fingers/box/timerDial/machineSprite flags, the binary-derived switches ... };
virtual ChocolateSpec ChocolateFactory::readSpec(const ui::Style&) const;     // Chocolatier 3's parse, moved verbatim

namespace choco::game::choc2 {
ChocolateSpec factorySpec(const ui::Style&);                                  // pure adapter, 2.1
std::string factorySound(std::string_view c3Event);                           // "" = silent
double factoryPerformance(unsigned production, bool aborted, double startMs, double rateMs, int recipeSize, bool colors);
class FactoryGame : public ChocolateFactory;   // readSpec, sound, notifyPaused, showGameOver, onSecondMatch, draw/postDraw
class FactoryStatus : public ui::Window;       // bars_, products_; static band(fill), fillHeight(rect, fill)
struct DiceRoll;                               // pure, tested with a fake rng: start, tick, stopOne, totals
class DiceGame : public ui::Window;            // static active(); 62 ms catch-up animation while rolling
}
```

- `FactoryGame` keeps Chocolatier 3's ownership (FD 2.2, 3.2): the sprite tree owns every sprite; each transient sprite has
  exactly one list reference; parked ingredients and their markers are removed at reset. The spec is plain data consumed by `init`.
- Natives reach windows only through `FactoryWindow::active()` (with `dynamic_cast<choc2::FactoryGame*>`), `DiceGame::active()`
  and `CratesGame::active()`, set in constructors and cleared in destructors. With no window they do nothing, and
  `GetProduction`, `PlayerTotal` and `CharacterTotal` return 0.
- Lua callbacks and `gDiceRollCount` are globals looked up per use; the windows hold no `LuaRef`. A callback may destroy windows
  (deferred) but runs after the step's own bookkeeping, as Chocolatier 3's mid-step pause does.
- `FactoryStatus` and `DiceGame` own their textures through `shared_ptr`; `DiceGame` sets its children's bitmaps by name each
  time (the same call `SetBitmap` makes), keeping no child pointers.

## 4. Order of work per frame

As FD 4. The factory step runs in `tickWindowAnimations` (N x 10 ms), its sprite scripts in `World::tick` (gun `DoAnim`,
sparkle). A Lua callback fired inside a step (`MinigameWarn`, `MinigameColor`, `MinigameEnd`) runs synchronously there, after the
step has paused or ended the game. The dice tick (62 ms) and the crates tick (50 ms) are plain window animations in the same
phase.

## 5. Where the code is

- Shared (Chocolatier 3 unchanged by default): `src/games/common/factory/{FactoryWindow,Chocolate,Parts}.*` (`ChocolateSpec`,
  `readSpec`, `FactoryCommon`, `IngredientQueue` taking a `double spacing` and `IngredientArt`, optional pistons, fingers,
  machine, box, dial, template position, gun rotation offset; `sound`, `showGameOver`, `notifyPaused` and `onSecondMatch` hooks)
  and `src/games/common/Crates.*`.
- Chocolatier 2: `choc2/FactoryGame`, `choc2/FactoryStatus`, `choc2/DiceGame`, `choc2/Choc2FactoryNatives.*` (`ResumeMinigame`,
  `PauseMinigame`, `ResetMinigame`, `GetProduction`, the window types `FactoryGame`, `FactoryStatus`),
  `choc2/Choc2DiceNatives.*` (`RollFirst`, `RollSecond`, `StopOne`, `PlayerTotal`, `CharacterTotal`, type `DiceGame`),
  `choc2/Choc2BonusNatives.*` (type `BonusGame`); `api-choc2.tsv` rows marked `@native` replace the stub rows.

## 6. Tests

Unit (doctest, a fake clock through the window manager's modal clock, `Rng` with a fixed seed or a recording fake):
- **Factory adapter** (`tests/test_choc2_factory.cpp`): b_01's table gives 5 trays, `trayEnd .95`, spacing `250/1200`, flight
  500, lap 30000, product 2000, countdown 2400, game 60000, no colours, recipe `cacao, sugar`, 8 park points, the Chocolatier 2
  art names, the machine at (690, 299); a coloured type and the Chocolatier 3 defaults for missing keys; counts repeat each name,
  names sorted. **Chocolatier 3 regression:** `tests/test_factory.cpp` pins `readSpec` on the Chocolatier 3 style field by field.
- **Sound map:** every row of 2.1, the numbered ones at their ends, unknown to "".
- **Performance:** the formula, the abort (-1), recipe sizes, ideal 0, and the cut-offs with handicap 0.9.
- **Lifecycle on a real window** (Lua defines the four callbacks as counters): `ResumeMinigame` from idle shows no digit, then 2
  and 1, and reaches Playing after 240 steps; `PauseMinigame` calls `MinigamePause` once and stops the clock; Resume continues;
  `PauseMinigame` idle or Over calls nothing; Escape pauses a started game; the first miss with `warn` calls `MinigameWarn`;
  the colour hint calls `MinigameColor` once on a two-ingredient match that does not complete the tray; `ResetMinigame` calls
  nothing, zeroes `GetProduction`, and the next Resume starts a countdown; 8 misses give `MinigameEnd(-1, nil)`; a normal end
  gives `MinigameEnd(100*P/ideal)` with `P == GetProduction()`; a missing or erroring callback is logged and the game goes on;
  a Chocolatier 3 factory never calls the callbacks. Natives without a window: no-op, 0.
- **FactoryStatus:** parse of 5- and 6-tuples; `band` and `fillHeight`; the production-animation flag.
- **Dice** (`tests/test_choc2_dice.cpp`): with a recording `Rng`, `RollFirst` draws 2 then `RollSecond` 6, in die order; stops at
  2790 and 3534 ms, then up to 6510; `StopOne` stops the next die on the next tick; `gDiceRollCount` goes 0, 1, 2 then up to 8
  (a nil global counts from 0); totals count stopped dice only; `RollFirst` during a roll and `StopOne` idle are no-ops;
  destroying the window mid-roll is clean.
- **Crates** (`tests/test_choc2_bonus.cpp`, `tests/test_crates.cpp`): the factors sum to 6.16 with four zeros; the 25-crate
  shuffle; `PrepareGame` and `SelectPrize` on 25 crates; Chocolatier 3's spec unchanged.

Headless scenarios (assets-gated; Story start, fixed `--seed`, screenshots looked at; allow-lists name only audio natives and
cursors): `check_choc2_factory.py` (give 1000 sugar and cacao, set `gSim.warnOnMiss`, click the San Francisco factory building,
through `ui/factory_status.lua` and `minigame` to `ui/factory_minigame.lua`; a deliberate miss pauses with `factory_waste`;
Escape shows `factory_paused` with the dialog still open; `autoshoot` to the end; the `performance` label equals
`GetProduction()`; Done sets the factory's `rate`; the Factories tab; Try Again restarts at 0; the same seed gives the same
production), `check_choc2_dice.py` (`PlayDice()` in New York's den, a bet, two rolls, one `stop` click, money moved by the
Lua's rule against `PlayerTotal()` and `CharacterTotal()`) and `check_choc2_bonus.py` (`PlayBonus`, crates opened until a whammy
or 5 opened, money by the Lua's rule). The `cancel` Escape walk is checked there: Escape pauses, then the modal presses
`cancel`, which the Lua has hidden by then, and the dialog must survive.

Chocolatier 3 unchanged: `unit`, `stubs_up_to_date`, the Reforged chocolate, coffee and crates scenarios byte-identical,
`choc2_*` green.

## 7. Open points

- **Inference remaining for the factory:** which ingredient is slot 0 (recipe iteration order), whether the fingers rotate about
  the image centre, the pulley height and animation speed, the exact sound on delivery beyond `factory_snatch` and the finger's
  timing against it, and the `Nx` text's style flags beyond size 80 (`choc2-factory-binary.md` 8). The aim is the click point where
  the original uses the last mouse-move direction (unchanged; a click normally follows a move).
- **Inference for dice and crates:** stop order and timing, the crate factors and art. A capture of the original (a roll filmed;
  one board's 25 values by repeating the bet on a seeded save; the crate art) would settle them.
- **Shared-class edits** (`FactoryWindow`, `ChocolateFactory`, `IngredientQueue`, `CratesGame`) are the main risk to
  Chocolatier 3; they are gated on byte-identical Reforged output.
- **Not implemented:** `factory_stall`/`timebonus` sounds, `fpWrite` telemetry, and the Chocolatier 1 quirks not copied (the
  doubled lateness conversion, the per-tick shared-stream dice draws).
