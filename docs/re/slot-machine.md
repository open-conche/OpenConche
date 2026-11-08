# Slot machine wheels: SlotMachine / SlotWheel animation

Sources: `re/mac/decomp/SlotMachine.c`, `re/mac/decomp/SlotWheel.c` (addresses are Ghidra VAs of the Mac binary), the Reforged mod's `assets/ui/ui_slotmachine.lua` (the vanilla `assets/ui/ui_slotmachine.lua` in the original install has the same flow; Reforged only changed the odds), the images under `assets/image/slot_machine_*.png` and `assets/items/*_big.png`, `docs/re/particles.md` section 4, `docs/re/ledger-and-game-natives.md` 10.2, `docs/re/audio-events.tsv`. This note replaces the "SlotWheel not decoded" gaps in 10.2 and corrects its description of `StopSlots`.

Everything below marked (inference) is not read straight from code.

## 1. Summary

* The wheels are a **constant-speed scroll with no easing at all**. Each wheel scrolls a number of whole symbols chosen at random (20..50) at a random speed (90..110 units per 50 ms tick, 200 units = one symbol step) and then simply stops. There is **no per-wheel stagger and no fixed stop order**: the four wheels get independent random lengths, so the stop order is random.
* The Lua decides the outcome (tier, symbols, payout) **before** the animation: it rolls `r`, picks the symbol indices `n[1..4]` and `payout`, then calls `StartSlots(n1,n2,n3,n4,payout)`. The native never chooses symbols; it only lands each wheel on the 1-based option index it is given.
* When the last wheel stops, in that same 50 ms tick: `Stop_machine_spin` sound, `onstopped`, then the sparkle clock is armed. `onpayout` follows after the sparkle (`payout*0.5+1` s) with `Stop_win_money`.

## 2. Classes and layout

`SlotMachine` (a `TWindow`, window type name "SlotMachine", `SlotMachineDynamicCreate` 0x3b1a4) and `SlotWheel` (plain object, 0x54 bytes, held in `boost::shared_ptr`).

Field meanings (from offsets; names are mine):

SlotMachine: +0x30 allStopped flag; +0x34 Vec3 particle origin; +0x40 `TLuaParticleSystem`; +0xf8 int zeroed in `StartMachine` (otherwise unused here); +0xfc payoutDone flag; +0x100 float payout; +0x104 `vector<str>` options; +0x110/0x118/0x120/0x128 the four `shared_ptr<SlotWheel>`; +0x130 `onstopped` `TLuaFunction`; +0x138 `onpayout` `TLuaFunction`.

SlotWheel: +4 `stepsLeft` (0 = idle/stopped); +8 `speed`; +0xc `phase`; +0x10 base y (the wheel's y as given by the DSL); +0x14 pointer to the SlotMachine's options vector (shared, not copied); +0x18 target option index (0-based); +0x1c strip sprite; +0x24,+0x28,+0x2c,+0x30 the four symbol names currently on the strip (index 0 = top, 3 = bottom); +0x34.. four child sprites, 8 bytes apart.

### Constructors and DSL (`SlotMachine::SlotMachine` 0x3af00, `PostChildrenInit` 0x3b1e0)

* The constructor registers Lua globals `StartSlots` (-> `StartMachine`, 5 args uint,uint,uint,uint,float) and `StopSlots` (-> `StopOne`, no args) directly on the window script's state; the destructor (0x3a114 / 0x3a2b4) sets both back to nil. Sets particle global `gSparkleTexture = "dollars_big"`, `Init("glitz/slotpayout.lua")`.
* `PostChildrenInit`: reads the DSL table `options` (each entry via `AsString`, pushed into the options vector in order), then for k = 1..4 reads the table `wheel<k>` (format `"wheel%d"`) and, if present, creates `SlotWheel(Vec2(x, y), &options)` (`x`,`y` default 0). A missing wheel table leaves a null wheel pointer, which later code dereferences (the shipped Lua always supplies all four). Particle origin is `Vec3(185, 244, 1)`. `onstopped` and `onpayout` are read with `GetFunction` and kept; either may be absent.
* The Lua passes `options = ings` where `ings = {"dollars", <each name in Player.needs>}`, so option index 1 is always `dollars` and the rest are the `Player.needs` keys in `pairs` order (unspecified order in Lua; the engine's own table order is fine). **Symbol names are looked up as `items/<name>_big`** (inference on string concatenation order; the strings "items/" and "_big" are the only literals, and the file `assets/items/dollars_big.png` exists next to 179 other `*_big.png`).

## 3. The strip (`SlotWheel::SlotWheel` 0x3aa34)

* One strip sprite with a null texture, uniform scale **0.425** (`TMat3::Scale` on the sprite's matrix at +0x20), position `(x, y, 1)` from the DSL, base y remembered at +0x10.
* Four child symbol sprites, each given the texture `items/<name>_big` for a **random** option name (`RandRange(0, size-1)`, drawn in order for wheel 1..4, symbol 0..3, so 4 RNG draws per wheel at window creation). Children sit at local `(0, -120)`, `(0, -60)`, `(0, 0)`, `(0, 60)`. So the symbol pitch is **60 strip units = 25.5 px** on screen (60 * 0.425). Each `_big` image is **64x64** px (all items and `glitz/dollars_big.png` checked), so a drawn symbol is 27.2 px; neighbours overlap by 1.7 px. Sprites use the default (centred) anchor (inference), so the resting symbol is centred on `(x, y)` and is **child index 2**.
* `SlotWheel::Draw` (0x39fd4) draws the strip sprite with an identity `TDrawSpec` (scale 1, alpha 1, mask `0xffffffff`); the children follow the strip transform. The class never clips; the clipping window comes from `image/slot_machine_mask.png` (see section 7).
* Fields +4, +8, +0xc are **not initialised** in the constructor (the decompile shows only +0x14 and +0x18 set). Unknown whether the allocator zero-fills; in practice a wheel must be idle until `Start`, so use 0.

## 4. Starting a spin

`StartSlots(s1,s2,s3,s4,payout)` = `SlotMachine::StartMachine` (0x39f16):

1. allStopped := false, payoutDone := false, +0xf8 := 0.
2. For wheels 1..4 in order: `SlotWheel::Start(sN)` (0x39ecc): `target = sN - 1`; `stepsLeft = RandRange(20, 50)` (inclusive, 0x14..0x32); `speed = RandRange(90, 110)` (0x5a..0x6e). The two draws happen in that order (steps first), wheel by wheel, so 8 RNG draws per spin. `phase` and the strip names are **not** reset.
3. payout float stored; `TWindow::StartWindowAnimation(0x32 = 50 ms, true, true, false)`. The three boolean arguments are not decoded here (the engine's existing `startWindowAnimation(id, 50, true)` is consistent with the 50 ms tick).
4. There is no initial velocity ramp and no stagger: every wheel starts moving on the first tick.

A second `StartSlots` during a spin simply overwrites steps, speed and target of every wheel and restarts the animation timer (the Lua prevents this with its `ready` flag).

## 5. Per-tick wheel motion (`SlotWheel::Tick` 0x3a44c, called once per 50 ms from `OnTaskAnimate`)

Nothing happens when `stepsLeft == 0`. Otherwise, in order:

1. `phase += speed`.
2. If `phase >= 200` (a **step**; speed < 200 so at most one step per tick):
   a. If `stepsLeft == 1`: sound event `spin_stop` (via `BSGSoundEvent`; a one-shot, per wheel).
   b. Draw `i = RandRange(0, optionCount - 1)` (consumed on every step, including the last two).
   c. Shift the names down: `name[3]=name[2]; name[2]=name[1]; name[1]=name[0]; name[0]=options[i]`.
   d. `stepsLeft -= 1`. If the **new** value is exactly 2, overwrite `name[0]` with `options[target]`.
   e. Reload all four child textures from `items/<name[k]>_big` (children index k).
3. `phase %= 200` (the remainder carries over; after a step this is `phase - 200`).
4. Strip y: if `stepsLeft == 0` then y := base y (exact snap); else y := `phase * 60 / 200 + baseY` (float; the strip drifts **down** by up to one symbol pitch between steps, new symbols enter from the top).

Consequences (derived):
* The target symbol is inserted at the top when `stepsLeft` becomes 2, moves to index 1 on the next step and index 2 (the centre, resting) on the step that makes `stepsLeft` 0. So the wheel always lands on the chosen symbol with two random symbols above it and one below (the symbols that were above/below get overwritten by random draws as they scroll in).
* The last step is also the tick the strip snaps to rest (the sub-step offset is discarded). The `spin_stop` sound fires on that same tick. No visible deceleration at all; speed is constant until the final snap.
* Travel time of a wheel = about `stepsLeft*200/speed` ticks: from 20 steps at speed 110 (~37 ticks, about 1.8 s) to 50 steps at speed 90 (~112 ticks, about 5.6 s). The spin lasts until the slowest of the four; first tick of motion is the first `OnTaskAnimate` after `StartSlots`.
* Scroll rate: 0.45 .. 0.55 symbols per tick = about 9 .. 11 symbols (about 230..280 px) per second.

## 6. Machine tick (`SlotMachine::OnTaskAnimate` 0x3a74c), every 50 ms

Return value 1 = keep animating, 0 = finished.

1. If allStopped is false: set allStopped := true, then for wheels 1..4 in order call `Tick`; after each, if that wheel's `stepsLeft != 0`, set allStopped := false again. Then `InvalidateScreen`.
2. If allStopped became true on this call (this branch runs exactly once per spin):
   a. If the `onstopped` function exists: sound event **`Stop_machine_spin`**, then call `onstopped()` (pushed and invoked through the script's protected call; no arguments, results ignored). If `onstopped` is absent, the sound is not sent either.
   b. `gSparkleTime = payout * 0.5 + 1` and `gSparkleDensity = payout * 10` in the particle script (both always set, even when payout is 0; the payout float is the one passed to `StartSlots`).
3. If allStopped and payoutDone is false (this can be the **same call** as step 2):
   * if `payout <= 0`: payoutDone := true;
   * else: `TLuaParticleSystem::Update(50)` once, read `gSparkleTime` from the script; if `<= 0` payoutDone := true; `InvalidateScreen`. (The script itself decrements `gSparkleTime` by the update seconds, 0.05 per tick, and emits particles; see particles.md section 4.) So the first particle update happens in the tick where the last wheel stops, immediately after `onstopped`.
   * if payoutDone is now true and the `onpayout` function exists: sound event **`Stop_win_money`**, then call `onpayout()`.
4. Return 1 unless both allStopped and payoutDone, then 0.

Therefore:
* `onstopped` fires on the tick the last wheel makes its final step (same tick as that wheel's `spin_stop` sound; the order inside the tick is wheel `spin_stop` sounds (wheel order), `Stop_machine_spin`, `onstopped`).
* For payout > 0, `onpayout` fires on the tick where the accumulated sparkle time reaches `<= 0`: arming gives `payout*0.5+1` s and every tick subtracts 0.05, with the stopping tick already counting one update (consistent with the engine test "`onpayout` after exactly payout*0.5+1 seconds of 50 ms ticks"; the exact tick count depends on float rounding of the repeated subtraction, which the engine's Particles code already does).
* For payout <= 0 (a loss), `onpayout` fires in the same tick as `onstopped`, straight after it, with `Stop_win_money` before it (the sound stop is harmless when `win_money` never played).
* The "no animation after the dialog is gone" case: if `onstopped` destroys the window, step 3 would run on a dead object in the original. The engine already guards this; the shipped Lua never does it.

## 7. What Lua draws vs what the native draws

Native (`SlotMachine::Draw` 0x3a03e): inside a `TBegin2d`, the four wheel strips in order 1..4, then the particle system at origin (185,244), then `TWindow::Draw` (children). Lua draws everything else, in this order (all inside `image/slot_machine_base`, 366x403, centred horizontally at y=5 in the `popup_back_generic_tall` dialog):

1. the base bitmap `image/slot_machine_base` (366x403);
2. the `SlotMachine` window (x 0, y 0, w kMax, h 550) with its wheels and sparkle;
3. `image/slot_machine_mask` (366x403 PNG with alpha), drawn **on top** of the wheels. This is the clip: it is opaque except for transparent slots at y = 116..194 (79 px tall, centre 155) in each wheel column; bounding transparent columns around the wheel x values were measured at x = 94, 155, 216, 277. So the visible reel window is about 79 px high = about 3.1 symbol pitches (measured with PIL on the Reforged PNG; vanilla not compared). The mask also covers the area where the sparkle origin sits, so the sparkle particles near (185,244) draw **under** the mask (inference from the draw order; the mask has other transparent rows at y 262..309 in columns 155 and 216 only, which probably is the payout tray, unverified);
4. the lever `Button` ("pull", x 326, y 49, graphics handle_up / handle_down / handle_up, mask `slot_machine_handle_mask`, kPush, command `PullLever`).

Wheel centres from the Lua: `x = 94, 155, 216, 277` and `y = 156` (the file writes `80+14`, `141+14`, `202+14`, `263+14` and `116+40`): column pitch 61 px, centred on 185.5, which agrees with the particle origin x = 185. The wheels and the origin are in the same (SlotMachine-window-local = base-image) frame; the SlotMachine window's own offset is 0,0 in the Lua, so no extra translation (the origin and wheel coordinates agree, which supports this).

## 8. Lua flow and sounds

`PullLever` (command of the handle button), in order: return if not `ready`; `ready=false`; `EnableWindow("ok", false)`; `SetLabel("ok", GetString("ok"))`; `Player:SubtractMoney(bet,...)`; **`SoundEvent("pull_lever")`**; **`SoundEvent("machine_spin")`** (a looping sfx, killed by `Stop_machine_spin`); roll `r = RandRange(1,100)` and choose tier (Reforged: simple/basic/jackpot thresholds by difficulty with a looseness factor; vanilla file has the original odds, not recomputed here), fill `n[1..4]` (option indices 1..#ings, 1 = dollars), `payout` in {0, 1, 2.5, 8}; a four-pass swap shuffle of `n[]` (`s = RandRange(1,4)` per i); `StartSlots(n1,n2,n3,n4,payout)`.

`AllStopped` (= `onstopped`): `win = Floor(bet*payout + 0.9)`; if `win > 0`: `SoundEvent("win_money")` (a looping sfx, killed by `Stop_win_money` when `onpayout` fires), `Player:AddMoney`, `UpdateLedger("money")`, then `slots_win_3_in_row` (payout == 1 or payout <= 4) or `slots_win_4_in_row` (payout > 4), and the instruction label (`slots_won_even`, `slots_won_jackpot`, `slots_won`); else the label `slots_lost`. `PaidOut` (= `onpayout`): `EnableWindow("ok", true)`; `ready = true`.

Sound summary (all go through `SoundEvent` / `BSGSoundEvent`, see `audio-events.tsv`): `pull_lever` (oneshot) and `machine_spin` (loop) at the pull; `spin_stop` (oneshot, native) once per wheel on its final step; `Stop_machine_spin` (native) when the last wheel has stopped, just before `onstopped`; `win_money` (loop), `slots_win_3_in_row` / `slots_win_4_in_row` from `onstopped`; `Stop_win_money` (native) just before `onpayout`.

The tier-to-symbol relation is entirely Lua: wins have 3 (payout 1) or 4 equal symbols, with the rest decided by the Lua; the native never sees a "win" except through the payout float. `StopSlots()` is registered but **never called by any shipped Lua** (searched the Reforged assets and the original install's `assets/`), so the "stop on click" path is dead.

## 9. Click / Escape during the spin

* `SlotMachine` defines no mouse or key handler (only `Draw`, `OnTaskAnimate`, `PostChildrenInit`, `StartMachine`, `StopOne` and the class plumbing). A click on the machine does nothing.
* `StopOne` (`StopSlots`, 0x3a0ca): finds the first wheel in order 1..4 with `stepsLeft != 0` (if all are stopped it looks at wheel 4 and its test then fails); if that wheel's `stepsLeft > 3` it is set to **3**; one wheel per call. With 3 left it still needs three more steps (about 1/3 s), the target is inserted at the top on the first of them, and the wheel then stops normally (same `spin_stop`). The 3 is a step count, not a state, and there is no slow-down. Unused by shipped Lua, so implement it only as a native that does this.
* The pull button is a one-shot via `ready`; "ok" (cancel = true, close = true) is **disabled** by `EnableWindow("ok", false)` while spinning. Whether Escape reaches a disabled cancel button is a `TButton`/window-manager question (see window-system.md), not the slot machine's. If the dialog did close mid-spin, the destructor nils `StartSlots`/`StopSlots`, the window animation task disappears with the window, `onstopped`/`onpayout` never run, the bet is lost and `machine_spin` keeps looping (nothing sends `Stop_machine_spin`; not verified what the sound system does at dialog teardown).

## 10. For the engine (`src/game/Stubs.cpp`, `SlotMachine`)

* Currently the engine stops everything on the first tick; the test `reforged_slotmachine` / `test_stubs.cpp` cases (onpayout after exactly `payout*0.5+1` s of 50 ms ticks) were written for that. With real wheels they hold only counted from the tick the last wheel stops; the tests should then use the minimum possible spin (wheels with fixed counts) or measure from `onstopped`.
* Needed: four wheel objects with `stepsLeft, speed, phase, target, names[4], baseY`, options from the DSL, `RandRange` from the shared RNG in the order given (ctor 16 draws, `Start` 2 per wheel, 1 per step per wheel), the 50 ms tick, `spin_stop` per wheel, and the `Stop_machine_spin` plus sparkle timing of section 6 (including `onpayout` in the same tick as `onstopped` for payout <= 0).
* Draw: strip scale 0.425, children at y -120/-60/0/60, 64x64 `items/<name>_big` images, y offset `phase*60/200` down, snap at rest, in the Lua mask frame.

## 11. Unknown

* Whether `new SlotWheel` memory is zeroed before `+4/+8/+0xc` are used (the constructor does not set them).
* The exact meaning of the three booleans in `StartWindowAnimation(0x32, true, true, false)` and whether the first tick comes at 50 ms or immediately.
* The textual form of the symbol path (`items/` + name + `_big`) is inferred from the string literals; the concatenation order is hidden by the decompile.
* Whether sprites are centre-anchored (the default) so that the visual centre equals the DSL `(x, y)`; the mask slots (centre y 155, wheel y 156) strongly suggest yes.
* Draw order of sparkle against the mask and whether the mask's lower openings (y 262..309) are visible parts of the machine.
* Whether the sound system stops `machine_spin` when a dialog is torn down mid-spin.
* The vanilla odds (not reread; irrelevant to the animation).
