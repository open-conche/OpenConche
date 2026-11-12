# Chocolatier 1 factory mini-game (`FactoryGame` / `TFactoryGame`)

Everything an engine coder needs to implement the Chocolatier 1 factory mini-game faithfully. Read with `docs/re/choc1-natives.md`
section 10 and `docs/re/choc1-fit.md`.

Tags: **fact** = read from the decompile, the disassembly, the data tables in the exe or the Lua bytecode; **inference** = reasoned
from those; **unknown** = not determined.

## 0. Sources and how to read the addresses

- Exe: the Chocolatier 1 Windows exe (`Chocolatier.exe`) (unpacked MSVC 8, RTTI, image base 0x400000). Decompile: `re/choc1/decomp/_unnamed.c`
  (functions named `FUN_<address>`). The decompile loses x87 arguments, so every number that matters below was re-read from the
  disassembly (`objdump -d -M intel`, Apple LLVM; `.text` VA = file address) and the constants from the PE's data (doubles and floats
  at the cited addresses).
- Lua: `re/choc1/pfp_extracted/sim/factory.lua` (disassembled with a workbench script; the cites `factory.lua:N` are the
  bytecode's source lines), `ui/factorystatus.lua`, `sim/sim.lua`, `sim/item.lua`, `data/items.lua`. There is no Lua source in this
  game.
- Strings: `strings.xml` from the Chocolatier 1 Windows install's `assets/` (ZXML = 12-byte header + zlib; the XML inside is SpreadsheetML).
- Art and sound under `re/choc1/pfp_extracted`: `image/fac_*`, `image/tray2..6`, `image/traycomplete2..6`, `image/gun_shoot.{png,xml}`,
  `image/powerup.{png,xml}`, `image/missed_ingredient.png`, `cursor/gun_aimer.png`, `item/<name>.png`, `audio/sfx/*`, `audio/music/minigame.ogg`.

Class layout (fact, the vtable at `0x4e740c`, read from the PE):

| slot (byte) | address | role |
|---|---|---|
| 0 (0x00) | `00409f60` | returns the class name string `"TFactoryGame"` |
| 1 (0x04) | `00410e50` | type test (is-a against the class tag) |
| 2 (0x08) | `0040d5d0` | scalar deleting destructor, calls `0040ce00` |
| 5 (0x14) | `0040b080` | **Init** (reads the Lua keys, builds the tray wheel) |
| 6 (0x18) | `00410830` | **PostInit** (finds children, shows the start dialog, then calls Reset `0040df90`) |
| 7 (0x1c) | `0040c4f0` | **Draw** |
| 13 (0x34) | `00410b30` | **the 10 ms tick** (inference for the role: it is the only method with a fixed `dt = 10`, it returns true, and `StartGame` registers a 10 ms repeating task) |
| 14 (0x38) | `0040df60` | **click** (inference: mouse down; it fires the gun) |
| 17 (0x44) | `0040ab00` | **mouse move** (aims the gun) |

The constructor is `0040e940` (it builds all art and sounds, and registers the natives, below).

## 1. The Lua side

### 1.1 Entering the mini-game (fact)

1. Port view building click -> the building's `UISelect` (`sim/factory.lua:382`, `LFactory:UISelect`): `c, quest = self:ChooseCharacter()`;
   if a quest is pending `c:UIHandleQuest(quest)`; if the factory is **not owned** `self:UIPurchase(c)` (`factory.lua:401`, a
   yes/no dialog `factory_purchase`, money check `factory_toomuch`, `gSim:AdjustMoney(-price)`, `Purchase`, `UpdateStandardUI`), and on success
   `SwapToModal("ui/portview.lua")` then `self:UISelect()` again; if owned, `self:UIStatus(c)` (`factory.lua:477`) and `LeaveBuilding`.
2. `UIStatus` shows a `DisplayDialog{"ui/dialog.lua", body=..., sound="charopen"}` containing the `factory_status` `DynamicWindow`
   (`ui/factorystatus.lua`, content: the `item/<product>_big` bitmap at (96,123), the product name, `"#"..rate` in `RedColor`
   at (341,105) 90x70 `factorystatus.lua:17`). It has three buttons: `leave` (sets `gCurrentFactory = nil`), `reconfigure` (the recipe picker),
   and `minigame` (`control/btn_minigame_up|down|over`, label `minigame`), plus help (`DisplayHelp(self.helpkey = 6)`).
3. The `minigame` button runs the local function at `factory.lua:422`: it counts recipe ingredients for which
   `gSim:GetInventory(ing) < qty` (qty = the recipe's per-batch quantity). If **none** are short it calls `self:DoMiniGame(c)`; otherwise it
   opens `ui/chartalk.lua` with text `factory_noingredients` and one `Bitmap{image="item/"..ing}` plus a `Rollover` per missing ingredient
   (icon scale `ingIconHeight`/`ingIconWidth`). So one batch of every ingredient is the entry requirement; the mini-game itself consumes nothing.
   (The mini-game does not consume or check inventory afterwards. The weekly `ProductionRun` does, section 1.4.)

### 1.2 `DoMiniGame(self, c)` (`factory.lua:134-293`, fact)

Order of operations:

1. `SetUpdateRate(0)` (freezes the sim loop), `StopBackgroundMusic()`, `SetAmbient(kFactorySetupAmbient)`.
2. `firstMiss = (gFirstMissWarning and self.product.name == "basebars")`, then `gFirstMissWarning = nil`.
   `gFirstMissWarning` is set to `true` for a new game (`sim/sim.lua:58`, new-game constructor), so the "wasted ingredient" warning
   happens **at most once per game, and only if the first mini-game played is for `basebars`**. After the call it is nil whatever the product was.
3. Build the body table and `DisplayDialog{ "ui/dialog.lua", sound="charopen", body = {name="minigame", Bitmap{...}} }`:
   - the named modal is `minigame` (the exe's `00403130` looks for the top modal with this name, section 5.6);
   - `Bitmap{x=0,y=0,w=800,h=600,image="image/factory"}` is the 800x600 backdrop;
   - inside it `FactoryGame{x=0,y=0,w=800,h=600, name="factory", character=c.name, firstMiss=<bool>, port=gSim.port.name,
     product=self.product, prodname=self.product.name, recipe=self.product.recipe, type=self.product.type, <children>}`.
4. The call blocks (modal) and returns the dialog result `rate`. **`rate > 0`**:
   `self:SetConfiguration(self.product.name, rate)` (`factory.lua:83`: `config[product] = rate`, `self.product = LItem:ByName(name)`, `self.rate = rate`);
   `UpdateDynamicWindow("factory_status")`; `BackgroundMusicNow()`; `PlayerMessage(GetVariableString("factory_ratechange",
   {port=self.port.name, product=self.product.name, rate=rate}))` ("Adjusted the <port> factory to make <rate> cases of <product> per week");
   `gSim:Tick()`; `UpdateStandardUI()`.
   **`rate <= 0`** (cancel returns -1, a run with 0 cases returns 0): none of that happens.
5. Always at the end: `SetUpdateRate(kDefaultUpdateRate)` and `SetAmbient(kPortAmbient)`. Note `BackgroundMusicNow()` is **only** in the `rate > 0`
   branch, so a cancelled or zero-case run leaves the background music stopped (fact from the bytecode; whether the original bug is
   audible is unknown).

### 1.3 The keys the native reads, and the HUD children (fact)

`FactoryGame` keys (read in Init `0040b080`, string keys fact, types by use):

| key | type | meaning / default |
|---|---|---|
| `x y w h name` | number / string | 0,0,800,600, `"factory"` (the native `00403130` finds it by this name) |
| `character` | string | the keeper character name (default `"sffactorykeep"`, stored at +0x39c); used for the first-miss command only |
| `firstMiss` | bool | +0x3a4; true only until the first wasted ingredient pauses the game |
| `port` | string | the port name (+0x3a0), used as a prefix of the `chartext` string key |
| `prodname` | string | name of the product; the native loads `item/<prodname>` (+0xc0). **`milkbar` is only the default value of `prodname`** (a string literal in Init), not a key |
| `product` | table | passed but **not read** by the native (inference: only the Lua side uses it) |
| `recipe` | table `{ingredientName = qty}` | iterated with the Lua `pairs` order (section 2.1) |
| `type` | table | the `ProductType` row (the item loader replaces `item.type` with it, `sim/item.lua:106-113`); keys read: `trayCount` (default 7), `ingredientRate` (default 350), `speedup` (1.1), `startspeed` (1.0), `maxspeed` (8.0) |

The `ProductType` rows in `data/items.lua` (fact): `bar` has **only** `price=1, markup=3` (so all defaults: 7 trays, 350 ms, x1.1, start 1.0, max 8.0);
`square` trayCount 8, startspeed 0.9, speedup 1.07, maxspeed 8; `infusion` 7, 0.8, 1.1, 8; `truffle` 6, 0.9, 1.09, 8. **No row sets `ingredientRate`**, so it is always 350.
Recipe totals (sum of quantities = ingredients per tray) are 2-4 for bars, 4 for squares, 5 for infusions, 6 for truffles (from `data/items.lua`; the tray art only exists for 2..6, section 2.2).

Note the three speed values are **globals** (`_DAT_005253c0` speedup, `_DAT_005253c8` startspeed, `_DAT_005253d0` maxspeed, initial 1.1 / 1.0 / 8.0) that
Init overwrites only when a `type` table exists; they are not reset otherwise (edge case, inference: every shipped product has a `type`).

Children (names the native finds; positions are `x,y,w,h` from the bytecode):

| name | what | notes |
|---|---|---|
| (unnamed) `Text` | product name, (616,380) 170x33, `kVAlignCenter+kHAlignCenter`, `stdFontFace` 16, `Color(255,121,1)` | static |
| (unnamed) `Text` | label `cases`, (681,3) 38x16, `kVAlignTop+kHAlignCenter`, 16, `Color(255,1,17)` | static |
| `counter` | `Text` (646,15) 108x38, label `#0`, font 35, `Color(255,41,77)` | native writes `"%lu"` of the count |
| (unnamed) | label `mini_time`, (669,65) 63x16 | static ("TIME") |
| `timer` | `Text` (669,77) 63x35, label `#1:00`, font 35, `Color(255,173,43)` | native writes `"m:ss"` |
| `countdown` | `Text` (150,150) 300x300, label `#4`, `stdFontFace` 200, `RedColor` | native writes `"3"`,`"2"`,`"1"` |
| `pause` | `Button` (679,416), graphics `control/fac_btn_pause_up|down|over`, `command = TogglePause()` (`factory.lua:204`) | |
| `pause_label` | `Text` (639,444) 124x24, label `pause`, 22, `WhiteColor` | native swaps label `pause`<->`resume` |
| `paused` | `Frame` (47,233) 500x134 `BrownColor`; `Text "paused"` (0,14) `gameAlertFont`; `Button` (242, kMax-74) `genericButtonGraphics` -> `TogglePause()` with `Text "resume"` at (302,...) 100x64; help button (178,...) -> `DisplayHelp(7,true)` between `SetUpdateRate(default)` and `SetUpdateRate(0)` | shown while paused |
| `keeper` | `Window` (10,30) 597x346 `fit`: `Rectangle{CharColor}` (52,71) 492x264; `chartext` `Text` (192,76) 345x197 `charDialogFont` label `factory_start`; four `image/dchar_left|right|top|bottom` bitmaps (0,45) (533,45) (64,45) (64,305); child windows `gamestart`, `gameend`; a help button (62,266) -> `DisplayHelp(7,true)`; plus `c:UIAppearance(23, 0)` | the character speech panel |
| `gamestart` (in `keeper`) | `Button` (124,266) `exitButtonGraphics` -> `CloseWindow(-1)` + `Text "cancel"`; `Button` (322,266) `yesButtonGraphics` -> `gSim.minigames += 1; SetAmbient({}); StartGame()` + `Text "start"` | start dialog |
| `gameend` (in `keeper`) | `Button` (124,266) `genericButtonGraphics` -> `gSim.minigames += 1; RestartGame()` + `Text "try_again"`; `Button` (322,266) `yesButtonGraphics` -> `CloseWindow(GetProductCount())` + `Text "ok"` | end dialog |

`gSim.minigames` (`sim/sim.lua:94`, saved at `sim.lua:246`/`362`) counts started runs (start and try-again). It is used by `sim/firstpeek.lua:28` and probably medals (inference).
Labels beginning `#` in the Lua are literal text (`#0`, `#1:00`, `#4`); the native writes plain `"0"`, `"1:00"`, `"3"` through the Text window's text slot (vtable +0x68);
whether that slot looks the text up in the string table is not known (unknown; the engine should treat native text as literal).

### 1.4 What happens to the result: production accounting (fact)

The mini-game's result is **a weekly production rate in cases**, not an inventory change. The mini-game returns the number of completed trays (`GetProductCount`).
`rate` is stored in `LFactory.config[productName]` and `self.rate`. The weekly accounting is in `Simulator:Tick` (`sim/sim.lua:537-556`): for every
owned factory `LFactory:ProductionRun()` (`factory.lua:333-377`):

- if `self.product` and `self.rate > 0`: `n = floor(min over recipe of inventory[ing] / qty)`, then `n = min(n, rate)`;
- if `n > 0`: `inventory[product] += n`; for each ingredient `inventory[ing] -= qty * n`; `usetime[ing] = gSim.time`; returns `n`;
- if `n == 0` and `gFirstPeek`: increments `gSim.shortages[ing]` for short ingredients;
- (bytecode quirk, fact: line 340 tests the global `count` (nil) `== 0`, which is never true, so the division is always done.)

`Tick` adds `n` to `gSim.totalMade`, and sets a flag when `n == 0` that posts `factory_stop`. `UIReconfigure`'s status line and `EstimateProduction`/`Running`
(`factory.lua:308-331`) use `rate` for the "weeks of supply" text. `LFactory.defaultProductionCount = 0` (`factory.lua:26`): a new recipe has rate 0 until a mini-game is won.

### 1.5 Which `chartext` string the end panel shows (fact, native, `0040a340`)

At the end the native fills the `keeper`'s `chartext` child with the string for the key `<port>_<verdict>` (port = the `port` key), or, if that string table
lookup returns the missing marker `"#####"`, the plain `<verdict>`. Keys that exist in `strings.xml`: `production_low|med|high|awesome|stopped` and the per-port
versions for `sanfrancisco newyork riodejaneiro london mahajanga hongkong`. `wasted_once` and `factory_start` are plain keys.
Verdict (ratio `r`, section 2.9): `stopped` when the run was aborted by 8 misses; otherwise
`r < 0.11` -> `production_low`; `0.11 <= r < 0.19` -> `production_med`; `r >= 0.19` -> `production_high`, **except** `production_awesome` when
`r >= 0.65` and the recipe has <= 3 ingredients per tray, or `r >= 0.55` and it has >= 4 (thresholds are doubles at `0x4e71d8`=0.11, `0x4e71c0`=0.19,
`0x4e71a8`=0.65, `0x4e7190`=0.55; the decompile's reading of the <=3 branch is wrong, the disassembly at `0040a4d8-0040a510` is the source).

## 2. The native game

### 2.1 Instance fields (fact unless marked)

| offset | meaning |
|---|---|
| +0x34 | state (section 2.2) |
| +0x38 | state saved while paused |
| +0x3c | product count (what `GetProductCount` returns) |
| +0x40 | elapsed ms (counts the countdown first, then the 60 s) |
| +0x44 | tick time base, advanced by 10 per tick (only used to compute `dt`) |
| +0x48 | unused leftover (set to leftover ms when the gun is loaded) |
| +0x50 (double) | wheel angle, radians, starts 0, **decreases** |
| +0x58 (3 floats) | normalised aim vector (mouse - (300,300)) |
| +0x60 (double) | aim angle for the gun sprite (initial 3pi/2) |
| +0x68 (double) | wheel speed (in revolutions-per-30-s units, section 2.4) |
| +0x70 (double) | speed decay per tick (0 except after the end) |
| +0x80, +0x88, +0xa0 | sprites: flywheel (123,15); the big wheel with its mask (300,300); the gun (300,300) |
| +0xc8 / +0xcc | arm animation active flag / its clock |
| +0x110 | trayCount (default 7, from `type.trayCount`) |
| +0x114 + i*0x3c | tray i: +0 sprite, +4 image ref, +8 powerup animation sprite, +0x10..+0x1c screen bbox (left, top, right, bottom ints), **+0x20..+0x34 six slots (99 = empty, else the ingredient index)**, +0x38 filled count |
| +0x2f4 | the "hub" (recipe template) sprite at (700,300) |
| +0x2fc / +0x308 | spawn clock (ms) / next spawn time (ms) |
| +0x300 | `total` = sum of recipe quantities (ingredients per tray) |
| +0x304 | ingredientRate (350) |
| +0x31c + k*8 | ingredient image k (`item/<name>`) and its ref; up to 6 |
| +0x34c + j*4 | slot template: ingredient index for slot j (index repeated `qty` times, in recipe iteration order), `j < total` |
| +0x364 | the ingredient queue (std::vector<uint>, begin +0x368, end +0x36c) |
| +0x374 | the ingredient currently loaded in the gun (pointer to a belt node, or 0) |
| +0x37c | belt list (ingredients travelling to the gun), count at +0x380 |
| +0x388 | shots in flight, count +0x38c |
| +0x394 | missed ingredients, **count +0x398** |
| +0x3ac | finished products on the output conveyor, count +0x3b0 |
| +0x3b4 / +0x3b8 | `counter` / `timer` Text windows |
| +0x39c / +0x3a0 / +0x3a4 | character name / port name / firstMiss |
| sounds (obj,ref pairs) | +0x3bc countdown_tick, +0x3c4 minigame_starting_bell, +0x3cc minigame_load_launch, +0x3d4..+0x3f4 ingredient_success_01..05, +0x404..+0x440 ingredient_failure_01..08, +0x444 template_complete, +0x44c factory_snatch, +0x454 template_success, +0x45c powerup, +0x464 warning_bell, +0x46c mini_game_production_over, +0x474 factory_shut_down |

### 2.2 State machine (fact)

| state | meaning |
|---|---|
| 0 | idle: the start dialog (`gamestart`) is up; nothing ticks (set by PostInit -> Reset `0040df90`) |
| 1 | countdown 3-2-1 (2400 ms), wheel already turning |
| 2 | running (60 000 ms of play) |
| 3 | paused (state saved in +0x38) |
| 4 | finishing: the clock ran out, the player can no longer fire, the machine drains |
| 5 | over (`gameend` shown; the wheel coasts to a stop) |

Transitions: 0 -> 1 `StartGame`; 1 -> 2 when elapsed >= 2400; 2 -> 4 when elapsed > 60 000; 4 -> 5 or 2/4 -> 5 by the end tests (section 2.7);
any of 1,2,4 <-> 3 by `TogglePause`; `RestartGame` goes to 0 (Reset) then immediately to 1. State 5 ignores `TogglePause`.

### 2.3 Natives (fact, ctor `0040e940` registers them, the destructor `0040ce00` registers `nil` over them)

- `StartGame()` (`0040a150`): sets the cursor `cursor/gun_aimer?simple` with hotspot (16,16) and calls the cursor-lock helper with 1 (inference: hides the OS cursor);
  hides `keeper` (clears its 0x100 visibility bit); `timer` text `"1:00"`, `counter` text `"0"`, `countdown` text `"3"` and shown;
  `speed = startspeed`, `decay = 0`, wheel angle 0, count 0, elapsed 0, spawn clock 0, next spawn 0, gun empty;
  starts the **10 ms repeating task** (`FUN_00421980(this, 10, 1, 0, 1)`); state = 1; plays music **`minigame.ogg`** (`FUN_00409ef0(musicManager, "minigame.ogg")`).
- `RestartGame()` (`0040e1c0`) = Reset (`0040df90`) then `StartGame()` body.
- `GetProductCount()` (`00409fd0`) returns +0x3c as a number.
- `TogglePause()` (`0040abb0`): ignored in state 5.
  - **Not paused** (state 0, 1, 2 or 4): save the state in +0x38, state = 3, **pause all sound** (`FUN_00443ea0(soundMgr, 1, -1)`), show `paused` (set its 0x100 bit),
    `pause_label` text = the string `resume`, cursor `cursor/cursor` hotspot (1,1), cursor-lock helper called with 0.
  - **Paused** (state 3): state = saved state, resume sound (`FUN_00443ea0(soundMgr, 0, -1)`), hide `paused`, `pause_label` text = the string `pause`,
    cursor `cursor/gun_aimer` hotspot (16,16), cursor-lock helper called with 1.
- Pause from the Lua button, the `paused` Frame buttons, and from the exe's focus/close handling (section 5.6).

### 2.4 Reset (`0040df90`, also from PostInit and `RestartGame`) (fact)

1. `FUN_0040c150`: for every tray: slots = 99 (all six), remove its child sprites, filled = 0, tray image = the empty tray image (+0x94); clear the belt, missed, shot and product lists.
2. state = 0. Clear the queue vector.
3. Build the ingredient queue: `U = trayCount * total`; `len = (((60000 / rate) + U) / U) * U`, all **integer** divisions, i.e. `len = (floor(floor(60000 / rate) / U) + 1) * U`;
   for rate 350, `60000 / 350 = 171`, so `len = (floor(171 / U) + 1) * U`. Entry `i` = `slotTemplate[i % total]` (the ingredient index).
4. **Shuffle in blocks of U**: for `i = 0 .. len-1`: `r = rand()`; `j = r % U + (i / U) * U` (integer division, so `j` is in the same block of U as `i`); swap `queue[i]` and `queue[j]`.
   Each block of U therefore holds exactly `trayCount * qty_k` of ingredient k: one block fills every tray exactly once. (The shuffle is the plain swap, not an unbiased Fisher-Yates.)
5. Hide `paused` and a child named `finished` (not present in the Lua; harmless).

Spawning pops from the **back** of the vector (so the last block is used first).
Examples: square (8 trays, total 4): U = 32, len = 192; bars (7 trays) with total 3: U = 21, len = 189; truffles (6 trays, total 6): U = 36, len = 180.
`len >= 172` always, which is just enough for the number of spawns a 60 s run can make (172 spawns at 350 ms).

RNG (fact): the factory draws from the process CRT `rand()` (the MSVC generator, `seed = seed * 214013 + 2531011; return (seed >> 16) % 32768`), the same stream as
`UtilMathRandom` (0041dbe0 reads it) and the other callers (port animations and the like). `UtilMathSeedRandom`
(0041dc20) is `srand(ms since start)`, and `ui/mainloop.lua` calls `bsutil.srand()` at boot before the menu, so the original's stream is **time-seeded**, not the default seed 1. The
engine keeps the unseeded stream (seed 1) so the factory, the dice and the story tests stay reproducible - a deliberate deviation; `UtilMathSeedRandom` stays a no-op. A headless test
injects the generator.

### 2.5 Geometry constants (fact, from the ctor/Init/static initialisers at `004e5000-004e5240` and the PE data)

- Wheel centre = gun pivot = **(300, 300)** (static `0x53304c`, built from the float at `0x4e7050` = 300.0 for both x and y). Gun hit/exit area: the playfield is 800x600.
- Belt end (gun load point) = (300, 300): `DAT_0053307c = (int)300`, `DAT_0053308c = (int)300`. Belt start: x = 0, y = **571** (0x23b).
- Tray ring radius **225**; tray `i` of `N = trayCount` sits at wheel-local `(225 cos(2 pi i / N), 225 sin(2 pi i / N))` (`FUN_004ac6f0` = cos, `FUN_004ac820` = sin; verified by the rotation helper `00409da0`).
- Slot ring radius **50**; slot `k` of `total` sits in the tray (and in the hub) at `(50 sin(2 pi k / total), -50 cos(2 pi k / total))` (clockwise from the top). The ingredient icon is `item/<recipeKey[template[k]]>`.
- The hub (`traycomplete%lu` image, `%lu = total`) is at **(700, 300)** and carries the recipe icons on the same 50 ring (the "recipe template on the right").
- Tray image `image/tray%lu` and `image/traycomplete%lu`, `%lu = total` (2..6, all 150x150) (Init `0040b080`).
- Hit radius squared **5881** (float at `0x5253e0`, radius about 76.7 px) around the tray's screen bbox centre.
- Collect line: a tray's bbox **top** crossing y = **252** (0xfc) upward while full.
- Gun sheet `image/gun_shoot.xml`: 5 frames 100x150, registration (50,75), one row. Powerup sheet `image/powerup.xml`: 11 frames 150x150 (the 11th is blank), registration (75,75), 6 per row.
- Missed-ingredient panel: 4 columns x 2 rows of `image/missed_ingredient` (36x43) cells at `(631 + (n & 3) * 46, 144 + (n >> 2) * 46)` for the n-th miss, with the ingredient icon drawn on it (Draw `0040c4f0`).
- Output conveyor: products travel from y = 300 to y = 516 at x = 700 (section 2.8).
- Four conveyor wheels `image/fac_smallwheel` at x = 16, 116, 216, 316, y = 608 (just below the screen; they turn with the spawn clock, section 2.6).
- Machine arms at (723,273) `fac_upperarm` and (723,323) `fac_lowerarm` (with registration 154,44 / 154,18), gears at (646,329) and (639,272) (section 2.8).
  These are art placements from the ctor; the names are inference from the file names.

### 2.6 The 10 ms tick (`00410b30`, fact; `dt = 10` always)

`dt` is a constant 10: the function computes `iVar5 = base + 10`, `dt = iVar5 - base` (= 10), stores `base = iVar5` at the end. It does not read a clock, so **game time is tick count x 10 ms**
whatever the real time (whether a late task catches up is unknown, section 7). Per tick, in this order:

1. If state != 3:
   1. `elapsed += 10`.
   2. `speed -= decay` (decay is nonzero only after the end); if `speed < 0` then `speed = 0, decay = 0`.
   3. **Warning bell**: if state != 5 and `(60000 - elapsed) / 1000 < 8` (integer) and `(60000 - (elapsed - 10)) / 1000 > 7`, play `warning_bell` (+0x464). With elapsed in steps of 10 this fires **once, at elapsed = 52 010**.
   4. If the arm animation is active (+0xc8): `0040a880(10)` (section 2.8).
   5. If misses == 8 (+0x398) and state != 5: end the game aborted: `0040a340(1)`.
   6. If elapsed > 60 000 and state == 2: play `mini_game_production_over` (+0x46c), **state = 4**, and `if speed < 5.0 then speed = 5.0`.
2. If state == 1 (countdown): `countdown` text: if `elapsed < 2400`: number `n = (2400 - elapsed) / 1000 + 1` (3 while elapsed < 400, 2 for 400-1399, 1 for 1400-2399);
   play `countdown_tick` (+0x3bc) on the tick where `n` differs from the previous tick's value (so at elapsed 410 and 1410).
   Else (elapsed >= 2400): play `minigame_starting_bell` (+0x3c4), hide `countdown`, `elapsed -= 2400`, **state = 2**, `nextSpawn = 0` (the spawn clock is already 0).
   **`dt` becomes 0 for the rest of this tick** (fact, `0x410cd9`: `add [esi+0x40], -0x960; mov ebp, [esi+0x40]`, and `ebp` is the `dt` pushed to the shots `0040da00`, belt/spawn `0040e260`, conveyor `0040c360` and rotation `00410940` calls; elapsed is exactly 2400 here, so it is 0 after the subtraction): on the bell tick the wheel does not turn, the belt budget is 0, the spawn clock stays 0 (the first ingredient spawns at 0 px) and the conveyor does not move.
3. If state in {1, 2, 4, 5}: **rotate the wheel** `0040e940`->`00410940(10)` (section 2.7a); then, if state != 5, the hit test `0040e480`.
4. If state in {2, 4}: shots `0040da00(10)`, belt and spawn `0040e260(10)`, then (if state != 5) the hit test `0040e480` again.
5. If state != 3: the output conveyor `0040c360(10)`.
6. If state == 4 and no shots in flight (+0x38c == 0) and no product on the conveyor (+0x3b0 == 0): count the trays with `filled == total`; if there are none (or the loop finds zero) end normally `0040a340(0)`;
   if some tray is still full and uncollected, wait (it will be collected when it crosses the line).
7. `base += 10`; returns true.

The timer text (Draw, `0040c4f0`, state 2 only): `s = elapsed < 60000 ? (60000 - elapsed) / 1000 : 0` then `"%lu:%02lu"` of `(s / 60, s % 60)`. The counter text is written by the conveyor (section 2.8).

#### 2.6a Wheel rotation `00410940(dt)` (fact)

`angle -= dt * 2 pi * speed / 30000` (constants `0x4e7210` = 2 pi, `0x4e79a8` = 30000.0; the angle is stored as a double at +0x50 and narrowed to a float for the matrix).
With `dt = 10`: `d angle = 2 pi * speed / 3000` per tick: **speed 1.0 = one revolution per 30 s, speed 8.0 = 3.75 s**.
The big wheel (+0x88, at (300,300)) and the flywheel (+0x80, at (123,15)) get the rotation matrix of `angle`; each tray sprite gets the matrix of `-angle` (it stays upright) and keeps its local position.
Then each tray's screen bbox is recomputed (the sprite's bounds through the wheel transform) and **if the old bbox top was >= 252, the new top < 252, the tray has `filled == total` and state != 5: collect it (section 2.8)**.
Direction on screen: the matrix is built as `[[cos, sin, 0], [-sin, cos, 0], 0 0 1]`; with y down and a decreasing angle, the art layout (the arms at x 639-723 and the collect line at the right, where a tray crosses y = 252 going up) implies the wheel turns **counter-clockwise on screen** (inference).

### 2.7 Gun, shots, hits

**Aim** (`0040ab00`, mouse move, also called by the click before firing): `v = (mouse.x - 300, mouse.y - 300)`, normalised in place (`FUN_0042b310`; the zero vector stays zero, so
a click exactly on (300,300) gives a shot that never moves and, since it stays inside the playfield, never misses: edge case, inference); the gun sprite angle is `acos(v.y)` negated when `v.x > 0`
(`FUN_004ac950` is inference `acos`; the sprite art points down at angle 0). The gun sprite sits at (300,300).

**Click** (`0040df60`, only if a gun is loaded and state == 2): update aim, then fire (`0040de90`): play `minigame_load_launch` (+0x3cc); remove the loaded node from the belt list;
store the normalised aim vector in it; append it to the shots list; clear the gun; play the gun sprite's `DoAnim` (the 5-frame `gun_shoot` sheet).
Nothing else limits fire rate; the next ingredient only arrives when the belt moves again.

**Shots** (`0040da00(dt)`): per shot per tick, `step = dt * 1200 / 1000 = 12` px (double); `x = (int) trunc(dir.x * step + x)`, `y = (int) trunc(dir.y * step + y)` (the shot position is an **integer**, truncated toward zero every
tick, so shots drift slightly: the vector loses its fraction each tick). A shot starts at its belt node's position (300,300). If the new `x > 800` or `y > 600` as **unsigned** (so any negative coordinate too) the shot is a **miss**:
play `ingredient_failure_%02d` with index = the number of misses so far (+0x398, 0-based: failure_01 for the first, 08 for the eighth); move the node to the missed list (+0x394);
`speed = (speed - startspeed) * 0.5 + startspeed`; if `firstMiss` is set remember to warn.
After the loop, if a warning is due (a miss happened and `firstMiss`): if state is not 3 and not 0 call TogglePause (pause!), clear `firstMiss`, and run (via `DoString`) the Lua
`table.insert(gCommandQueue, function() local c = LCharacter:ByName('<character>') c:UIModalSpecial(GetString('wasted_once')) end)` ("Uh-oh! Wasted ingredient!...").
The game is paused (state 3) until the player presses resume in the `paused` frame; the command-queue entry shows the character's modal on the next drain.
**8 misses end the run** (tick step 1.5, verdict `production_stopped`).

**Hit test** (`0040e480`): for each tray `t` in order, for each shot in list order: tray centre = `((left + right) / 2, (top + bottom) / 2)` of its screen bbox; if `(shot.x - cx)^2 + (shot.y - cy)^2 <= 5881`
and the tray is not full, find the **first** slot `k < total` (in template order) with `template[k] == shot.ingredient` and `slot[k] == 99`. If found:
- play the sound `+0x3d4 + 8 * filledBefore` (`ingredient_success_01` for the first fill, up to `_05`), **except** that Init overwrote the entry for the last slot (index `total - 1`) with **`template_complete`**;
- `filled += 1`; `slot[k] = ingredient`; add the ingredient icon as a child of the tray sprite at `(50 sin(2 pi k/total), -50 cos(2 pi k/total))`;
- if `filled == total`: set the tray image to the full image (`traycomplete%lu`) and **`speed = min(speed * speedup, maxspeed)`** (this is the only speed-up event);
- delete the shot and stop testing it. If no slot matches (that ingredient is already complete on this tray), the shot ignores this tray and keeps flying.

After all trays: if **every** tray is full at the same moment, play `powerup` (+0x45c) and collect all trays at once (section 2.8) (the "power-up": a whole wheel of trays in one go).
The hit test runs twice per tick in states 2 and 4 (after the rotation and after the shots move) and once in 1 and 5.

### 2.8 Belt, spawning, collection, the output conveyor (fact)

**Belt and spawn** (`0040e260(dt)`, only while the gun is empty (+0x374 == 0), states 2 and 4): `budget = dt * 600 / 1200 = 5` px per tick (0.5 px/ms, integer).
Each belt node moves along the path x: 0 -> 300 at y = 571, then y: 571 -> 300 at x = 300, carrying leftover distance round the corner (`00409f70` is the same step for a new node).
When a node reaches (300,300) it becomes the **loaded** ingredient (+0x374 = node), the belt halts (the function is not run while the gun is loaded), and the spawn clock stops.
The spawn clock `+0x2fc` advances by the time actually spent moving (a full 10 ms tick, less twice the overshoot past the end on an arrival tick);
`while (spawnClock >= nextSpawn)`: pop the **last** queue entry as a new node at (x = 0, y = 571) advanced by `(spawnClock - nextSpawn) * 600 / 1200` px, then `nextSpawn += 350`.
So ingredients appear every 350 ms of *unblocked* time; a node needs 571 px / 0.5 = **1142 ms** to reach the gun. The four conveyor wheels are rotated by the spawn clock (cosmetic).
Spawning continues in state 4 (inference from the call condition; the queue of >= 172 entries is not exhausted in practice).

**Collection** (`0040e1d0(tray)`, called when a full tray crosses y = 252, or for all trays on the power-up): start the arm animation if idle (`0040a810`: if +0xc8 is 0 play `factory_snatch` (+0x44c); set +0xc8 = 1, clock = 0);
reset the tray (`0040aff0(tray, 1)`: slots = 99, children removed, the `powerup.xml` sheet is re-added as a child and played once with `DoAnim`, filled = 0); the tray image back to empty; append a new product node with value 300.0 to the conveyor list (+0x3ac).

**Arm animation** (`0040a880(dt)`, cosmetic, fact): the clock `+0xcc += (uint)(dt * speed)`; `f = clock * 7 / 1000`; `f` is truncated and the animation ends only at **`f >= 8`** (+0xc8 = 0; clock >= 1143; fact, `0x40a8f3-0x40a915`), the position being clamped to 7 (the tables have 8 entries) for the last ticks. The clock reset to 0 in `0040a810` and the increment `trunc(dt * speed)` are literal. The arm sprites interpolate their x offsets through
two 8-entry tables at `0x4e706c` (-60, -110, -90, -75, -60, -50, -30, 0; applied to the sprite at (723,273)) and `0x4e70d8` (-45, -100, -80, -65, -50, -40, -20, 0; (723,323)); the two gears at (646,329) and (639,272) are rotated by the same matrix.
So the arm takes `1143 / speed` ms.

**Output conveyor** (`0040c360(dt)`, state != 3): each product value `v += dt * (516 - 300) / 2000 = 0.108 * dt` (216 px in **2000 ms**). When `v > 516`: delete it, **`count += 1`**, write `"%lu"` of the count to `counter`;
after the loop, if any arrived, play `template_success` (+0x454) once. The product icon (`item/<prodname>`, +0xc0) is drawn at (700, v) once `v >= 477`.

### 2.9 End of the run (`0040a340(aborted)`, fact)

1. state = 5; `decay = speed / 300` (the wheel decelerates linearly to 0 over 300 ticks = 3 s).
2. Sounds: if `+0x474` exists: when `aborted` stop the background music (`FUN_00409ef0(musicManager, "")`), then play **`factory_shut_down`** (+0x474) (also on a normal end).
3. `chartext` text: aborted -> `<port>_production_stopped`; else compute `r`:
   `maxTrays = (60000 / ingredientRate) / total` (integer divisions, 171 / total for rate 350), `r = count / maxTrays` (as doubles), and pick the key by section 1.5.
   Division by zero: `total >= 2` always for shipped recipes.
4. Hide `gamestart`, show `gameend`, show `keeper` (the 0x100 bit is visible), restore the arrow cursor `cursor/cursor` (hotspot 1,1) and cursor-lock off (0).

`r` is the share of the theoretical maximum number of trays: the 60 s run offers at most 171 ingredients, a tray eats `total` of them.
The Lua `ok` button returns `GetProductCount()` = `count` = **trays that completed AND reached the end of the output conveyor** (a tray collected in the last 2 s of the run still counts in state 4 because the end waits for the conveyor to drain).

### 2.10 Pause, resume, focus (fact)

`00403130` is called from the main loop's message cases 3 (close), 0x11 and 0xb (when the parameter equals `DAT_00525cdc`) (`re/choc1/decomp/_unnamed.c:2710-2757`). It looks up the top modal; if its name is `minigame` it
finds the child `factory`, checks it is a `TFactoryGame`, and calls `0040afc0`: if state is not 0 and not 3 it calls `TogglePause` (which does nothing in state 5). So **close, window deactivate (0x11) and the 0xb event pause a running game**;
a game in state 0 (start dialog) or already paused is untouched. **I found no Escape key handling in the factory code** (unknown whether a message 0xb/0x11 originates from Escape in the host; the task text mentions Escape).
Resume is only via the `pause` button or the `resume` button in the `paused` frame; focus does not resume.

## 3. Rendering (fact from Draw `0040c4f0`; names of roles marked inference)

All art is on the 800x600 backdrop `image/factory` (the Lua bitmap). Draw order inside the native, back to front:
1. the four conveyor wheels (`fac_smallwheel`, +0xf0..);
2. the belt nodes: for each node whose `y < 571` the plate `image/fac_plate` (57x21, +0x30c) at the node's position, then the ingredient icon `item/<name>` at (x, y);
3. the flywheel (+0x80), the big wheel (+0x88, with all trays and their ingredient icons as children) and the gun sprite (+0xa0);
4. the loaded ingredient (state 2, 3 or 5) at the gun pivot;
5. the shots in flight (icon at (x,y));
6. the crate and conveyor art around (700,470-550) (`fac_cratefront`, `fac_cratelid`, `fac_bellows`) at (693.5,504)/(695,537)/(700,469), the product icons on the output conveyor (x = 700, drawn when y >= 477) and a stack icon at (699,548) (positions fact; which of the three crate images is which is inference);
7. the missed panel cells and icons;
8. the arm sprites (+0xd0, +0xd8), the two gears (+0xe0, +0xe8), the machine overlay `fac_machineover` (+0x78) and the hub (+0x2f4) with the recipe icons;
9. then the Lua children (text, buttons, the `paused` frame, `keeper`).

What rotates: the big wheel (+0x88) and flywheel (+0x80) by `angle`; trays counter-rotate by `-angle` (upright); the gun by the aim angle; the four conveyor wheels by the spawn clock; the arm gears during the arm animation.
Unit of the cursor: `cursor/gun_aimer.png` is 32x32, hotspot (16,16) (centred).

## 4. Audio (fact)

| event | sound file (`audio/sfx/`) | when |
|---|---|---|
| countdown number change | `countdown_tick.ogg` | elapsed 410 and 1410 of the countdown |
| go | `minigame_starting_bell.ogg` | the tick the countdown ends |
| fire | `minigame_load_launch.ogg` | each click that fires |
| good hit | `ingredient_success_01..05.ogg` | index = slots already filled in that tray (0..4) |
| last slot of a tray | `template_complete.ogg` | replaces the success sound for slot `total - 1` |
| miss | `ingredient_failure_01..08.ogg` | index = misses so far |
| tray snatched | `factory_snatch.ogg` | arm idle at collection |
| product reaches the crate | `template_success.ogg` | once per tick in which at least one product arrived |
| all trays full at once | `powerup.ogg` | 0040e480 after the hit loop |
| 7.99 s left | `warning_bell.ogg` | elapsed 52 010 |
| time over (60 s) | `mini_game_production_over.ogg` | state 2 -> 4 |
| game over | `factory_shut_down.ogg` | every end, aborted or not |

Music: `audio/music/minigame.ogg` starts at `StartGame` (the Lua first calls `StopBackgroundMusic`, `SetAmbient(kFactorySetupAmbient)`, then `SetAmbient({})` on the start button). It stops (`""`) only on an aborted end; `BackgroundMusicNow()` (Lua) restores the
background tune only when `rate > 0`. Pause stops all sounds (`00443ea0(1,-1)`) and resume restarts them (`(0,-1)`); the semantics of the parameters are inference. The sound loader call is `FUN_00434360(path, 1, 1000)` for every effect
(the two numbers are not decoded). No ingredient-specific sounds exist.

## 5. What `GetProductCount` and the verdict mean (summary)

Count = completed trays shipped. The ratio's denominator is `floor(171 / total)` (42 for total 4, 28 for total 6, 85 for total 2); each tray needs `total` hits, so that is also the hard ceiling.
The weekly rate in cases is that count (`rate`), so the player's weekly output is directly set by the best run's tray count.

## 6. Reuse against the existing engine

- `games/choc2/FactoryGame` and `games/common/factory/*` (Chocolatier 3 `ChocolateFactory`): a path-based tray conveyor with a template; a different game (no wheel, no gun, no 60-second countdown/shut-down rule, the miss panel is a 4x2 grid image but the rules differ).
  Reusable at most: the sound-event plumbing, the 4x2 miss-grid concept, the `Text`/`Button` HUD plumbing. Expect to write `games/choc1/FactoryGame` from this document.
- Needs from the engine: a 10 ms task that is the single time source (`dt = 10`); sprites with rotation about their own origin and parent-child transforms (tray upright under a rotating wheel: child `-angle`); a bounds query of a rotated child in screen space;
  a `Frame`/`Window` visibility bit; a `cursor` hotspot setter; the sound manager's pause-all.

## 7. Unknown

- Whether a late 10 ms task is caught up with extra ticks (the native assumes 100 ticks/s of game time).
- The rotation sense on screen (CCW inferred from the layout; the matrix convention of the engine's `Matrix3` was not traced) and `FUN_004ac950` = `acos` (inference).
- The order of `pairs(recipe)` in the original Lua 5.0 table (fixes which icon sits at which ring position and the ingredient indexes); only visual, gameplay-neutral if the engine uses its own consistent order.
- Whether the `Text` slot at vtable +0x68 resolves its argument through the string table (the Lua uses `#` prefixes for literals); the real meaning of the two numbers passed to the sound loader; the gun sheet's frame period.
- Escape handling (none found), and whether message 0xb/0x11 are focus-loss messages exactly.
- The seeding of `rand()` (no `srand` in the exe).
- The art role names for `fac_cratefront`, `fac_cratelid` and `fac_bellows` and the exact draw calls in the crate area, the exact meaning of +0x48 (unused), and the cursor-lock helper `FUN_0042d3d0`.
- Whether spawning really continues in state 4 and what happens if the queue ran dry (the vector is read with a checked iterator, `FUN_004ac4a5`).

### 7.1 What the engine implementation (`games/choc1/FactorySim` and `FactoryGame`) chose for the unknowns

Every item is an **inference** (the literal reading of the text above unless it says otherwise) and is marked `INFERENCE` in the code.

- **Rand:** one CRT stream in `Choc1World::crtRand`, seed 1 (the engine keeps the stream unseeded although the original seeds it with ms since start at boot), shared by every factory game of a session and by `UtilMathRandom`; `FactorySim` takes any `rand()` function, so tests inject the stream. The sim's constructor is PostInit's Reset (one `rand()` per queue entry), `restart()` is Reset again.
- **Late ticks:** the window's 10 ms animation is a catch-up one (`startWindowAnimation(id, 10, true)`): a late frame runs several ticks, so game time is tick count x 10 ms.
- **Rotation sense:** `x' = cx + lx cos a - ly sin a`, `y' = cy + lx sin a + ly cos a` with y down; the angle decreases, so the wheel turns counter-clockwise and tray 0 (at (525,300) at angle 0) goes up through the collect line y = 252 (its box top is `cy - 75`, so the crossing is at `cy = 327`, level with the lower arm at y 323). Pictures rotate by the same angle in degrees (clockwise positive, as the renderer does).
- **Tray bounding box:** the float position minus 75, truncated to ints (so `cx() = (left + right) / 2` is the hit-test centre).
- **Belt:** nodes are whole-pixel arc lengths on the 571 px path, oldest first; when the lead node reaches the gun in a tick every node moves only as far as the lead did, and the spawn clock gains `10 - 2 x overshoot` ms. A spawned node starts `(spawnClock - nextSpawn) / 2` px along. An exhausted queue spawns nothing (and `nextSpawn` still advances).
- **Gun:** a click exactly on (300,300) gives a zero aim vector, a shot that never moves and never misses, and state 4 then never ends (the literal reading; one pixel of the screen). The gun sprite's first angle is 3 pi / 2 (fact: the constructor sets `0x4012d97c80000000`, StartGame does not reset it) until the first mouse move. Frame 0 of `gun_shoot` is the idle pose; the sheet plays once at **100 ms a frame** after a shot (period unknown).
- **Miss sounds (fact, literal):** the exe indexes `this + misses * 8 + 0x404` unbounded, so a ninth miss (two shots leaving in the same tick) plays the next sound object, `template_complete`, not `failure_08`; the engine emits `TemplateComplete` for any index past 8. Success sounds are indexed by slots already filled (`filled == total - 1` plays `template_complete`).
- **Arm:** a collection restarts the arm clock (`clock = 0`) even when it is running; the sound only when it was idle. The arm x offsets interpolate linearly between the table entries; the two gears turn one revolution per arm cycle (cosmetic).
- **Power-up sheet:** 11 frames at **50 ms**, the blank 11th not drawn; played over a collected tray.
- **Crate pictures:** `fac_bellows` (128x16) centred (700,469), `fac_cratefront` (114x70, the front board) centred (695,537), `fac_cratelid` (117x24, the top rim) centred (693.5,504); the product icon is drawn over the crate front at (699,548) always. The doc's order of the three crate positions was a naming guess; sizes and the backdrop decide it. Drawn: bellows, falling products, front, lid, label; the lid only while no product is in transit, so the crate is open for a falling one (follows `choc-factory-game.md`; not yet confirmed against the decompile: **inference**).
- **Hub:** `fac_machineover` (202x165) is centred on (700,300), under the `traycomplete` hub and its recipe icons. The recipe order on the ring is the engine's table iteration order.
- **Missed grid:** cell `missed_ingredient` and the ingredient icon (32x32) both centred on (631 + 46 (n&3), 144 + 46 (n>>2)) (the backdrop's eight black circles are there), the icon over the cell.
- **Wheel mask:** `fac_wheel_mask.jpg` keeps its alpha in the **red** channel (green and blue are 0), unlike the blue-channel masks the texture cache reads; `FactoryGame` joins the two images itself. `TextureCache` now picks the red channel for such masks (a mask whose blue maximum is < 128 and red maximum >= 128; grey masks are unchanged), `FactoryGame` loads the wheel with `load(image, mask)`, and a size mismatch is logged by the cache. The same holds for `factory_status_mask.jpg`, now honoured by `Bitmap` (the workbench decision log).
- **Pause:** `Audio::setGamePaused` (music and CADI voices; the one-shot sfx tails are short and play out). A game closed while paused releases it. Focus loss pauses a game that is not in state 0 or 3 (`onForeground(false)`); regaining does nothing; the exe's two messages (close and 0x11/0x0b) are taken as "focus lost". The Escape key (the main loop's key-27 check, section 8 of `choc1-natives.md`) runs the same helper at the LOOP level: `Choc1Module::onEscapeKey` looks the FactoryGame up in the modal chain (whatever has the focus) and `escapePause()` pauses it unless it is in its start dialog (0) or already paused; the key then continues into the normal window processing.
- **Cursor:** set for real through the app's cursor service (`platform::Window::setCursor`: an RGBA image and its hotspot, into an `SDL_Cursor`): `cursor/gun_aimer?simple` with hotspot (16,16) at StartGame, `cursor/gun_aimer` plain on resume, the arrow `cursor/cursor` (1,1) at pause and the end. Without the service (unit tests) or when the platform refuses it, the request is logged once per name as before. The dump line shows `cursor=` for the mode the game asked for. Chocolatier 2's own art tree keeps both cursors under `image/` - the `FactoryCommon` paths carry that difference. The whole game runs with the skinned arrow: the main loop itself sets `cursor/cursor` (1,1) at boot, and the cursor art's
transparency is the pure-magenta colour key (`docs/re/focus-and-display.md` 6).
- **Texts:** native text is literal (`setLabel`, no string-table lookup); only `pause`/`resume` (pause_label) and the verdict text (`<port>_<verdict>`, else `<verdict>`) go through the string table, as the original reads them by key.
- **Finishing:** step 6 runs in the same tick that step 1.6 sets state 4, so a run with nothing in flight at 60 010 ms ends in that tick (state 4 is only visible while a shot is flying or a product rides the conveyor).
- **Harness aim:** `FactorySim::bestAim()` (the harness's `autoshoot`): the tray with the most filled slots that still needs the loaded ingredient, led by the flight time (12 px a tick) at the current wheel speed; nothing when no tray needs it (the original game can deadlock the same way: a wheel that needs only the ingredient the belt cannot deliver while the gun holds the other).

## 8. Headless assertions (suggestion)

- Dump each tick: `state, elapsed, speed, angle, count, misses, loaded, belt (x,y), shots (x,y,idx), trays (filled, slot[]), queue length`.
- Deterministic scenario: inject the CRT `rand` (seed 1), build `xdarkbars` (square, 8 trays, total 4: `U = 32, len = 192`), assert `len`, and that each aligned block of 32 contains 8 sugar and 24 cacao; assert the countdown ticks at elapsed 410 and 1410, the bell at 2400 and `state == 2`; spawns at ticks 1, 36, 71, 106 (the bell tick counts as 1 and spawns at 0 px, its dt being 0; the spawn clock is 0 after it, then 10, 350, 700, 1050)
  (the spawn clock advancing only while the gun is empty); a node arrives 1142 ms after spawn; fire straight at a tray centre and assert the hit when the distance is <= sqrt(5881); assert `speed` after one completed tray = `startspeed * speedup`;
  eight misses end at state 5 with `production_stopped`; at elapsed 52 010 the warning bell; at 60 010 `state == 4` (or already 5 in that tick when nothing is in flight, 7.1) and `speed >= 5.0`; verdict thresholds from `count / (171 / total)`.
