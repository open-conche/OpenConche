# Chocolatier 1's finale (`Finale` / `TFinale`), the reward screen's chocolate rain

Everything an engine coder needs to implement Chocolatier 1's ending window faithfully. Read with `docs/re/choc1-natives.md` and
`docs/re/choc1-fit.md` (section "What the engine covers, by topic").

Tags: **fact** = read from the decompile, the disassembly, the exe's data tables or the Lua bytecode; **inference** = reasoned
from those; **unknown** = not determined.

## 0. Sources

- Exe: the Chocolatier 1 Windows `Chocolatier.exe` (image base 0x400000). Decompile: `re/choc1/decomp/_unnamed.c`.
- Lua: `re/choc1/pfp_extracted/ui/reward_finale.lua` and `ui/reward.lua`, `sim/medals.lua`, `sim/sim.lua` (disassembled with
  a workbench script; the cites `file.lua:N` are the bytecode's source lines).
- Art: `re/choc1/pfp_extracted/item/<name>.png` for the 48 names in section 3 (`assets.pfp` holds the same files).

## 1. When the screen shows (fact, Lua)

`sim/medals.lua` builds `Simulator.MedalData` as eleven rows `{ check-function, key }` in order `novice bars entrep shop squares
choco factory infusions ports truffles master` (medals.lua:61-74; each check reads `gSim.rank` or counts, the last is the
"master" row). `Simulator.AwardMedal(gSim, i)` (medals.lua:121-133): sets `medals[i] = true`, `StopBackgroundMusic()`, and for
`i < table.getn(Simulator.MedalData)` shows `DisplayDialog{"ui/reward.lua", medal=i}` (the ordinary medal screen, the tutorial's
award); only the LAST row (`"master"`) instead snapshots the contest numbers and shows the finale:
`gSim.mcWeeks` (or `gSim.time`), `gSim.mcMoney` (or `gSim.money`) are stored on `gSim`, then
`DisplayDialog{"ui/reward_finale.lua"}`, then `BackgroundMusic()`.

`ui/reward_finale.lua` (bytecode lines 7-46, all fact): `i = table.getn(Simulator.MedalData)`, `data = Simulator.MedalData[i]`,
`medalkey = data[2]` ("master"); `score, weeks, money = gSim:GetMCScore()` (a `sim/sim.lua` method, lines 457-470: `mcWeeks` or
`time` clamped to [1, 999999999], `mcMoney` or `money` clamped to [0, 999999999], `score = truncate(money/weeks + 0.5)` capped at
999999999 - pure Lua, the engine runs it unchanged); `score` and `money` are `Dollars()`-formatted. The dialog, through
`MakeDialog(AppendStyle({font = rewardTextFont}), ...)` with `rewardTitleFont`/`rewardTextFont`/`rewardButtonFont` =
`{stdFontFace, 30|24|30, WhiteColor}`:

- a `Bitmap{x=0,y=0,w=800,h=600,image="image/mainmenu"}`,
- a **`Finale{x=0,y=0,w=800,h=600}`** - the native window this document is about,
- a `Bitmap{x=kCenter,y=kCenter-20,image="image/medal_"..medalkey}` (the master medal picture, centred),
- `Text{x=kCenter,y=30,w=740,h=24,flags=kVAlignCenter+kHAlignCenter}` labelled
  `"medal_master#"` .. `bsutil.GetVariableString("medal_master_finale", {weeks=, money=, score=})`,
- `Text{x=kCenter,y=44,w=800,h=80,flags=kVAlignCenter+kHAlignCenter,font=rewardTitleFont,label="medal_master"}` (string-table
  key),
- `Text{x=kCenter,y=450,w=600,h=120,flags=kVAlignBottom+kHAlignCenter}` labelled
  `"#" .. bsutil.GetVariableString("award_master")`,
- `Button{x=800-30-64,y=600-30-64,close=true,graphics=yesButtonGraphics}`,
- then `PlaySound("mcfanfare")` (the Chocolatier 1 alias of `win_game_fanfare` per `Choc1AudioNatives`) and
  `BackgroundMusic(nil, 10)`.

`GetMedalFlags(gSim)` (medals.lua:78-88, fact) packs the eleven `medals[]` booleans LSB-first (skip while `mode ~= "empire"` or
`gEnableMedals ~= 1`); `EvaluateMedals` (94-106) iterates `MedalData` and returns the first index whose check passes and whose
medal is not yet won, setting `gNewMedal`. The engine needs no new native for any of this: it is all shipped Lua.

## 2. The class (fact, the vtable at `0x4e7f24` read from the PE)

| slot (byte) | address | role |
|---|---|---|
| 0 (0x00) | `00410e80` | returns the class name string `"TFinale"` |
| 1 (0x04) | `004114a0` | type test |
| 2 (0x08) | `00411400` | scalar deleting destructor, calls `00411390` |
| 6 (0x18) | `00411090` | **Init**: `TWindow` init (`00422300`, which is `InitWindow` with a `"fit"` spec), then `FUN_00421980(this, 10, 1, 0, 1)` |
| 7 (0x1c) | `004110e0` | **Draw** |
| 13 (0x34) | `004110b0` | the **10 ms tick task proc** (see below) |

The constructor is `00411200`; the destructor `00411400`; the factory getter `00411490` and the class registered under the Lua
name `"Finale"` (`004e6824`; `main`'s registry `FUN_0042d340`). The exe never passes style keys beyond the generic rectangle
(`api-choc1.tsv`: "x y w h"): the Lua's table above has only `x y w h`.

`FUN_00421980(this, 10, 1, 0, 1)` registers a repeating `10` ms task through `FUN_0042fef0` - the same call the factory
mini-game's `StartGame` uses with `0x3e` (62) and the factory's slot-13 (0x34) method serves as its 10 ms tick
(choc1-factory.md section 0), and here the delay is literally **10**. The slot-13 method is `004110b0`: it steps all 60
particles once (`00410f90` each) and returns 1 - the particle update runs at a **10 ms repeating tick** (fact for the
registration; the slot's role is inference by the exact factory-game parallel).

## 3. The state (fact)

The object holds, after `TWindow`'s members:

- At `+0x34`: an array of **48 picture pointers** loaded by the constructor, one per name, in this order
  (the pointer table `0x4e7e08`..`0x4e7ec4`, each `"item/%s"` of these strings):

  `xdark_sq cashew_dark_sq almond_dark_sq hazelnut_milk_sq cherry_squares cinnamon_squares caramel_squares hazelnut_squares
  coffee_squares mint_squares almond_squares cherry_van_squares cinnamon_van_squares milk_van_squares hazelnut_van_squares
  almond_van_squares milk_choc_infusion vanilla_almond_infusion coconut_infusion cherry_infusion lemon_infusion coffee_infusion
  cinnamon_infusion raspberry_infusion mint_infusion maha_cherry_infusion quito_coffee_infusion colo_raspberry_infusion
  colo_coffee_almond_infusion maha_mint_raspberry_infusion sula_macadamia_infusion cacao_blend_infusion milk_truffles
  mint_choco_truffles dark_truffles almond_choco_truffles caramel_truffles coconut_lemon_truffles coffee_cinnamon_truffles
  hazelnut_milk_truffles maha_cinnamon_cherry_truffles sula_lemon_macadamia_truffles colo_coffee_caramel_truffles
  trin_almond_vanilla_truffles blend_mint_coffee_truffles blend_raspberry_coconut_truffles milk_blend_truffles
  exdark_blend_truffles`

  (four `_sq` singles, then the squares, infusions and truffles - the game's own item art; all 48 ship in `assets.pfp`).
- At `+0x1b4`: **60 particles**, 7 floats each, spawned by the constructor (`00411200` calls the respawn once per particle).

A particle (7 floats, offsets 0..6):

| field | role (fact) |
|---|---|
| [0] | `vx`: the per-tick x step |
| [1] | `vy`: the per-tick y step (gravity adds to it) |
| [2] | `x`: position, starts `400.0` |
| [3] | `y`: position, starts `250.0` |
| [4] | `spin`: radians added to [5] per tick; flips sign on the floor bounce |
| [5] | `angle`: `0.0` at spawn, `+= spin` per tick |
| [6] | the picture index: `rand() % 48` |

### 3.1 Respawn (`00410e90`, fact) - six `rand()` draws per particle

The constructor respawns all 60, so the boot of one Finale window draws 360 `rand()` values (and every later respawn 6 more)
from the shared CRT stream (`rand()`, `docs/re/choc1-factory.md` 2.4; the engine keeps seed 1 - the documented deviation from
the time-seeded original, `UtilMathSeedRandom`). In this exact order:

1. `x = 400.0; y = 250.0` (the screen centre - no `rand()`).
2. `vx = (float)(int)(800 / ((rand() % 2000 + 1500) / 10))` - integer division, so `vx` in [800/350..800/150] = [2.29, 5.33].
3. `vy = (float)(int)(600 / ((rand() % 1500 + 1000) / 10))` - `vy` in [600/250..600/100] = [2.40, 6.0].
4. `if (rand() & 1) vx = -vx` (the decompile's `& 0x80000001` with the `abs` fixup is MSVC's `% 2`; [4]'s flip with it is a
   no-op: [4] is 0 at this point).
5. `if (rand() % 3 > 0) vy = -vy` (two of three particles start rising).
6. `spin = (float)(rand() * PI / 32767.0) * 0.125` - a quarter of a quarter-turn at most, always >= 0;
   `angle = 0.0`.
7. `picture index = rand() % 48`.

### 3.2 The tick (`00410f90`, fact) - the 10 ms task's body, per particle

1. `angle += spin`.
2. `x += vx; y += vy` (the 2-D vector helpers `0042b1b0`/`0042b120` are plain float adds).
3. `vy += 0.08` (gravity, added after the move; the x87 keeps the add in double precision before the store).
4. If `y > 584.0` (the floor, 16 px above a 600-high window's bottom): `vy = -0.8 * vy` (of the just-incremented value),
   `spin = -spin`, and `y = 584.0 - (y - 584.0)` (mirrored back above the floor).
5. If `(vx < 0 and x < -64) or (vx > 0 and x > 864)`: respawn (3.1). A particle moving left is recycled once it passes 64 px
   past the left edge, one moving right 64 px past `800 + 64`. There is no ceiling test: a rising particle simply decelerates
   under gravity.

### 3.3 The draw (`004110e0`, fact; the placement is inference where marked)

`Begin2d` (`0041fb30`) before the loop and `End2d` (`0041fb80`) after it - both fact (the batch-guard helpers the other window
draws use). For each of the 60 particles, in order:

- A transform built from the picture: `00401ec0` (a placement transform with both scale factors 1.0, origin (0,0)) composed
  with the picture's own rotation/scale matrix (`00409da0` reads the picture's natural size - the item art is drawn at
  **natural size, unrotated**: no particle field reaches the transform. **Inference**: the `angle`/`spin` fields [4]/[5] are
  advanced every tick and flipped by the bounce, but no code path reads them - the rain does not tumble; dead state from the
  class it was copied from.)
- The picture's Draw, vtable slot `0x14`, called with the particle's `(x, y)` and `1.0` (the stack locals `004110e0` reads from
  the particle record: position [2]/[3] and the index as `record[4]`, i.e. the slot-0x14 draw takes a position and a scale
  factor). **Inference**: the position is the sprite CENTRE, as `TFactoryGame`'s falling product pictures use the same
  slot-0x14 draw around a float position and the factory doc reads them as centred (choc1-factory.md 8, "the crate and
  product picture (`_big`)" centred on the float position).

The window rectangle is passed by the Lua as `0,0,800,600`, so the rain covers the whole screen; nothing clips the particles
in TFinale's Draw (it draws raw positions), and the recycle bounds keep positions inside [-64, 864] x (.., 584]. The floor
constant 584 is hard-coded (0x5dc appears in the respawn's `+ 0x5dc`, and the comparisons at `00410f90`), as are 400/250,
-64, 864, 0.08, 0.8, 48 and 60 (`0x3c`).

## 4. The engine's implementation notes (decisions, deviations)

- `games/choc1/Finale.{h,cpp}`: `FinaleSim` (the plain logic: particles, respawn, tick - no window, no clock, `std::function<int()>`
  for `rand()`, exactly as `DiceSim`/`FactorySim` do) and `FinaleWindow` (the window).
- The CRT stream: `Choc1World::crtRand` (seed 1), shared with the factory and dice games as the exe's single `rand()` stream.
  The 360 boot draws happen at window construction, so a later factory/dice game on the same session continues from a
  deterministic point (deviation: the original is time-seeded; recorded with the UtilMathRandom deviation).
- The clock: one catch-up pass per draw, one tick per whole 10 ms of the modal clock (`manager().modalNow`), so a stopped
  modal clock (a dialog over it) freezes the rain - the same rule as SimpleAnimation.
- The draw: all 48 textures loaded at init; each particle drawn centred at `(x, y)` at natural size through the window's
  normal clipped draw path (the ±64 recycling overshoot is the only part a clip could ever cut, and the screen edge cuts it
  anyway).
