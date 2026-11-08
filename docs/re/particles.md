# Particle effects: fireworks, crate sparkle, slot-machine payout

Sources: `re/mac/decomp/` (TFireworksWindow.c, TFireworksWindow__TFirework.c, TLuaParticleSystem.c, T2dParticleRenderer.c, TParticleCollection.c, TBonusGame.c, SlotMachine.c, F*.c), data read straight from the Chocolatier 3 Mac app bundle's `Contents/MacOS/chocolatier-decadence-design` (Mach-O i386; Ghidra address = VA), the original assets' `glitz/*.lua`, `ui/ui_medals.lua`, `ui/ui_slotmachine.lua`, and the installer archives (`docs/re/pfp.md`). Addresses are Ghidra/VA. Companion docs: `api-globals.md` section 4 (particle environment names), `embedded-lua.md` section 5 (particles.lua), `game-windows-questions.md` 6.7 (crates), `ledger-and-game-natives.md` 10.2 (slots), `chocolate-factory.md` section 11.

"Glitz" is only the asset folder `glitz/` plus `Glitz.h` (which defines `ParticleColor`). There is no Glitz class. Every effect below is one `TLuaParticleSystem` running a script from `glitz/`.

## 0. Summary of the three effects

| Effect | Owner | Script | Texture | Units drawn |
|---|---|---|---|---|
| Awards fireworks | `TFireworksWindow` (9 `TFirework`, 1 particle system each) | `glitz/firework.lua` | `firework_blue/red/yellow/green` (8x8 png) | up to 80 each |
| Crate sparkle | `TBonusGame` (`StartSparkle`) | `glitz/sparkle.lua` | `"money"` | **script and texture are absent from Chocolatier 3** (section 5) |
| Slot payout | `SlotMachine` | `glitz/slotpayout.lua` | `dollars_big` (64x64 png) | up to 80 |

Factories do not use particles (see `chocolate-factory.md` section 11; the tray sparkle is the sprite animation `image/powerup.xml`). The only creators of `TLuaParticleSystem` in the decompile are `TFirework::TFirework`, `TBonusGame::TBonusGame` and `SlotMachine::SlotMachine`. `coffee-factory-and-recipes.md` has nothing on particles. No particle XML or other data exists; particle definitions are only these Lua scripts.

## 1. Lua entry points

* `FireworksWindow { x=0,y=0,w=kMax,h=kMax }` is a window type with no keys. Its constructor (`0003e7c2`) registers two Lua globals bound to this instance (replacing earlier ones): `StartFireworks()` and `StopFireworks()`, no arguments, no return.
* Vanilla and Reforged call sites: only `ui/ui_medals.lua` line 68 (the window, last child of the top-level table, so drawn after and above the popup dialog) and line 72 `if Player.medals.medal_07 then StartFireworks() end` (Reforged: lines 92 and ~99, identical logic). `StopFireworks` is never called by either; nothing else calls `StartFireworks`. medal_07 is "Master Chocolatier" (Reforged comment). Reforged's `glitz/firework.lua` and `slotpayout.lua` differ from vanilla only by a header comment and removed commented-out lines; values are identical.
* Crates: `StartSparkle(i)` is a C++ method of `TBonusGame` called from `Select` (not exposed to Lua). Lua only sees `PrepareGame`/`SelectPrize`.
* Slots: `StartSlots(s1,s2,s3,s4,payout)` (see `ledger-and-game-natives.md` 10.2); the payout float drives the sparkle. Lua call: `ui_slotmachine.lua` line 104.

## 2. TLuaParticleSystem engine (shared)

Object (size 0xb8): +8 renderer (`T2dParticleRenderer` created by the ctor when none is passed, flag at +4), +0xc `TParticleCollection`, +0x10 own `TScript` (own Lua state, `math` opened), +0x20 script directory prefix, +0x24 particle float storage, +0x28 floats per particle (stride), +0x88 init instruction stream, +0x94 anim stream, +0xac system alpha (init 1.0), +0xb1 done flag, +0xb4 spawn accumulator.

**Init(path)** (`000961d8`): if the file exists (`TFile::Exists`), the directory prefix is taken from the path (for `glitz/x.lua` it is `glitz/`), the script is fetched via `TScriptCode::Get` and run with `DoLuaString`; returns 1. **If the file does not exist it returns 0 and does nothing** (no particles are ever allocated). Accumulator reset to 0.

**Script API** (native names from api-globals section 4; semantics below are from the F*::Process functions): `Allocate(n)` adds n floats of per-particle state; the prelude predefines `pPosition`(2), `pUp`(2), `pScale`(1), `pColor`(4), `pFrame`(1), in that order (prototype particle in `T2dParticleRenderer` ctor: floats 0..9, scale 1.0, colour (1,1,1,1), pos 0, pUp (0,-1), frame 0). Allocation order fixes the layout; extra fields start at 0. `SetNumParticles(n)` sizes the pool (n x stride floats, `0095966`); `SetTexture(name)`; `SetBlendMode(k)` (kBlendNormal=0, Opaque=1, AdditiveAlpha=2, Subtractive=3, Multiplicative=4; **all three shipped scripts use kBlendNormal, i.e. plain alpha blend; additive is never used**); `X:Init(expr)` appends to the per-spawn stream, `X:Anim(expr)` and bare `Anim(...)` append to the per-frame stream; multiple Init on one member run in order, last wins.

**SetTexture** (`000967f2`): name is tried as given in the asset map; if not found, retried as `<script dir prefix> + name` (so `firework_red` -> `glitz/firework_red`, `dollars_big` -> `glitz/dollars_big`, `sparkle.xml` -> `glitz/sparkle.xml`). Renderer size := texture width x height in pixels.

**CreateParticles(x)** (`00095bf2`): `acc += x`; then while `acc > 0.0` (strictly): `acc -= 1.0`, take a particle from the free list (stop the whole call if the pool is exhausted), zero it, copy the prototype, run the init stream, move it to the active list. So fractional carry can overshoot by up to one particle (a positive remainder spawns one and goes negative). Pool exhaustion is silent; leftover accumulator stays.

**Update(ms)** (`000986ca`): runs Lua `Update(<ms/1000.0>);` (format `"Update(%f);"`, divisor float 1000.0) in the system's state, then `ProcessParticles(ms)`. So **spawns happen before the same tick's simulation**: a new particle is aged and moved in the tick that creates it. The editor-config branch (unused in the game) is ignored.

**ProcessParticles** (`00095f7e`): every active particle runs the anim stream with dt = the integer ms; a particle for which `fExpire` fires is returned to the free list. Only active particles are processed; nothing runs if no ticks arrive (the system freezes).

**F-functions** (all values are float vectors): `fTimeScale(v)` = v * dt_ms / 1000 (per-second quantities); `fAge()` = dt in ms (so `pAge + fAge()` is age in ms); `fRange(a,b)` = uniform random per component between a and b (RNG `TRandom`, seeded from a GUID, so not reproducible); `fGreater(a,b)` = 1 when a>b else 0; `fExpire(x)` kills the particle when x != 0; `f2dRotation(a)` = (cos a, sin a) (radians); `fFade(t, c0, T, c1)` = c0 + (c1-c0) * max(t,0)/T for the single-segment form used here (multi-segment forms exist; clamping beyond the last segment not verified, unused because both scripts expire at T); plus `+ - * / unary-minus` on vectors. Anim statements run in script order each tick, so in the scripts position uses the velocity of the previous tick (explicit Euler: position first, then velocity).

**Draw** (`T2dParticleRenderer::Draw`, `000ce738`): sets the texture and blend mode, builds one quad per active particle in active-list order (spawn order), draws, resets blend mode to 0. Quad: centre = particle pos + system origin (+ a constant offset I did not resolve, see Unknown); half-extents are texture size * pScale * 0.5 on each axis (corner table +-0.5); rotated by `pUp` (u0,u1): x = cx - ex*u1*kx - ey*u0*ky, y = cy - ey*u1*ky + ex*u0*kx. UVs cover the whole image. Vertex colour = pColor (rgba floats -> 8 bit), alpha additionally multiplied by the system alpha (+0xac, 1.0, nothing sets it in these three effects). For animated textures (`.xml` sheets) the frame is `pFrame` with registration-point offsets (not used by the shipped scripts). `TLuaParticleSystem::Draw(origin)` does nothing when the active count is 0. Always called between `TBegin2d`/`Done`, i.e. in the window's 2D screen coordinates (y down, per the scripts' comments: gravity values are positive).

## 3. FireworksWindow (Awards)

### Window (`0003e350`, `0003e38e`, `0003e3ea`)
* `StartFireworks`: only if the list is empty, creates **9** `TFirework` (all with `glitz/firework.lua`), then `StartWindowAnimation(interval = 0x28 = 40 ms, true, true, false)` (flag meaning: `window-system.md`). Calling it again while the list exists only restarts the animation.
* `OnTaskAnimate` (every 40 ms): `TFirework::Update()` for each, `InvalidateScreen`, returns 1 (animation never ends by itself). `StopFireworks` = `StopWindowAnimation`; particles drawn so far stay visible, frozen. It never blocks the dialog: the OK button and the rest are independent; the effect runs until the window is destroyed (the destructor frees all).
* `Draw`: `TWindow::Draw()` (children) first, then inside `TBegin2d` each firework's `Draw`. Because the FireworksWindow is a later sibling of the popup in `ui_medals.lua`, sparks are drawn over the dialog. Window covers the whole screen at (0,0), so firework coordinates are screen coordinates.

### TFirework (object size 0x30; ctor `0003e1a0`, `Update` `0003df40`, `Draw` `0003e14a`)
Fields: +0 x, +4 y, +0xc timer ms, +0x10 particle system, +0x14 `ParticleColor` (rgba at +0x1c..+0x28), +0x2c state.
* Ctor: state 0, timer = `rand() % 1000`, position (200,100), colour (1,0,0,1). Creates the system, registers `ParticleColor` as data source `gSparkleColorStart`, sets Lua string `gSparkleTexture` = `particleOptions[rand() % 5]`, then `Init("glitz/firework.lua")`.
* `particleOptions` (data at 0x1d8240): `firework_blue`, `firework_red`, `firework_yellow`, `firework_green`, and **a NULL pointer** in slot 5. One time in five the texture string is built from a null `char*`. What `str(NULL)` and `SetTexture` do then is unknown (an empty string would fail the lookup, so those 80 particles probably draw nothing or crash-safe-skip). `firework_white.png` exists in `glitz/` but is not named in the binary.
* `Update()` per 40 ms tick:
  * State 0 (waiting): if timer <= 40: timer := 0, state := 1; else timer -= 40.
  * State 1 (fire): colour index `rand() % 6` copied from `_FireworkColors` into the ParticleColor (table, rgba: orange .784,.494,0; red .784,.196,.196; blue .196,.392,.863; yellow .941,.902,.118; green .196,.882,.118; purple .549,.086,.765; alpha 1; built by the static initializer at 001abaea); plays `BSGSoundEvent("firework%d", rand() % 5 + 1)` (sfx/firework1..5.ogg); timer := 0; state := 2; **x = rand() % 250 + 50, y = rand() % 200 + 100** (so bursts land in x 50..299, y 100..299 of the window); sets the system's Lua global `gSparkleTime` = 0.1 (re-arms emission).
  * State 2 (burst): if timer < 1100: timer += 40 and the particle system `Update(40)`; else state := 0 and timer := `rand() % 500 + 250`. So a burst is simulated for 28 ticks = 1120 ms and then not simulated or drawn at all; the 2000 ms particle life is never reached on screen. Cycle per firework: 1120 ms + (250..749 ms) + ~40 ms.
* `Draw`: only in state 2, `TLuaParticleSystem::Draw(Vec3(x, y, 0))`.
* The C++ colour is effectively unused: the script overwrites `gSparkleColorStart` with `Color(1,1,1,.8)` before compiling its fade (inference from script order; the texture itself carries the colour).

### glitz/firework.lua (vanilla values; Reforged identical)
* Allocations: pVelocity(2), pGravity(2), pAge(1), pSpin(1), pSpinSpeed(1). Blend kBlendNormal. `SetNumParticles(80)`.
* Spawn init: pPosition = fRange((-2,-2),(2,2)); pScale = fRange(.2,.75); pGravity = fRange((0,90),(0,110)); pVelocity = fRange((-90,-50),(90,100)); pAge 0; pSpin = fRange(0, 2*3.1415927); pSpinSpeed = fRange(60,80); pColor Init(1,1,1,1) then Init(1,1,1,0) (last wins, but overwritten by the fade on the first tick).
* Per tick (order): pPosition += vel*dt; pVelocity += gravity*dt; pColor = fFade(age, (1,1,1,.8), 2000, (1,1,1,0)); pScale -= 0.1*dt (per second); pSpin += spinSpeed*dt; pUp = rotation(pSpin); pAge += dt_ms; expire when pAge > 2000.
* Emission: script sets `gSparkleTime = .1`; `Update(seconds)`: if gSparkleTime > 0: gSparkleTime -= seconds and `CreateParticles(seconds * 800)`. With 40 ms ticks that is 32 per tick for 3 ticks (0.1 -> .06 -> .02 -> -.02), ~96 requested against a pool of 80; so the pool is full after the third tick. Particles left frozen from the previous burst of the same firework remain in the active list (they are not cleared when the state machine restarts), so a re-burst has fewer free slots and the old sparks resume at the new origin. (Inference from the code: no Reset call exists on this path.)
* Look: 8x8 sprites scaled .2..0.75 -> roughly 1.6..6 px quads, spinning fast (60..80 rad/s), alpha 0.8 fading linearly to 0 over 2 s (cut at 1.12 s, so about 0.35 at the end), falling with ~100 px/s^2 gravity; scale shrinks 0.1 per second, so at most 0.112 over the 1.12 s that is simulated (minimum scale .2, so it never reaches 0 on screen).

## 4. Slot machine payout sparkle

* `SlotMachine` ctor (`0003af00`): sets Lua string `gSparkleTexture = "dollars_big"` in the system's script, then `Init("glitz/slotpayout.lua")`. `gSparkleColorStart/End` are **not** registered, so the script's defaults apply.
* Origin: `Vec3(185, 244, 1)` in the SlotMachine window's coordinates (set in `PostChildrenInit`).
* Tick: window animation interval 0x32 = 50 ms. In `OnTaskAnimate` after all four wheels have stopped: `gSparkleTime = payout*0.5 + 1`, `gSparkleDensity = payout*10`. While payout > 0 and the sparkle phase is not done, each tick calls the system `Update(50)` and ends the phase when `gSparkleTime <= 0`. If payout <= 0 no particle update ever runs. Then `Stop_win_money` and the Lua `onpayout` function are called (details in `ledger-and-game-natives.md` 10.2). Payouts used by the Lua: 0, 1, 2.5, 8 -> sim time 0 (none), 1.5 s, 2.25 s, 5.0 s, density 10/25/80 per second.
* Draw order in `SlotMachine::Draw`: the four wheels, then the particles at the origin, then `TWindow::Draw` (children).
* glitz/slotpayout.lua: allocations pVelocity(2), pAge, pSpin, pSpinSpeed; texture `gSparkleTexture` (fallback `sparkle.xml`); kBlendNormal; `SetNumParticles(80)`. Init: pPosition = fRange((-5,0),(5,0)); pVelocity = fRange((-45,100),(45,100)); pAge 0; pSpin = fRange(0, 2*pi); pSpinSpeed = fRange(-1,1); pColor = start colour default (1,1,1,1), end default (1,1,1,0.5). pScale is not initialised, so it stays at the prototype scale 1.0 (the `fRange(.3,1)` line is commented out): each sprite is a full **64x64** dollar image. Per tick: position += vel*dt; vel += (0,300)*dt (gravity); pColor = fFade(age, start, 2000, end), i.e. alpha 1.0 -> 0.5 over 2 s, then removed abruptly at age > 2000 ms; spin += spinSpeed*dt (about +-1 rad/s); pUp = rotation(spin); age += dt; expire when age > 2000.
* Emission: `gSparkleTime` starts 0 in the script and is set by C++ each spin. `Update(seconds)`: `if gSparkleTime >= 1 then CreateParticles(seconds * gSparkleDensity)`; then `if gSparkleTime > 0 then gSparkleTime -= seconds`. So particles are spawned during the first `payout*0.5` s only (e.g. payout 8: 4 s at 80/s = up to 320 requested against a pool of 80, limited by spawn-only-when-free), and the sim ends 1 s later. At the end of the phase the system stops being ticked; younger particles stay in the active list and Draw keeps drawing them frozen (inference: nothing clears them; whether they remain visible depends on the window staying open and redrawing).
* The payout x gSparkleTime shows that the slot effect is quite small for payout 1: 10/s * 0.5 s = about 5 sprites.

## 5. Crate sparkle (`TBonusGame`)

* Ctor (`0003db00`): registers data sources `gSparkleColorStart` (+0x4e0) and `gSparkleColorEnd` (+0x4f8), sets `gSparkleTexture = "money"`, `Init("glitz/sparkle.lua")`. Emitter origin (+0x420/+0x424, ints) set in `StartSparkle` to the crate's x,y.
* `StartSparkle(i)` (`0003cf0c`): origin := crate (x,y) truncated to int; start colour (0.082, 0.559, 0.106, 1.0); end colour same rgb with alpha 0.2 (floats 0x3da8a8a9, 0x3f0f8f90, 0x3dd8d8d9, 0x3e4ccccd); `gSparkleTime = 2.0`. Called from `Select` when a crate with money is opened by the player (not on reveal; `game-windows-questions.md` 6).
* Ticking: the window animation is 0x32 = 50 ms but `OnTaskAnimate` calls `TLuaParticleSystem::Update(100)` (0.1 s of sim per 50 ms tick, i.e. 2x wall speed; the animation also advances open-crate timers by 0x32 each tick). Draw: `TWindow::Draw`, then inside `TBegin2d` the 21 crates, then the particle system at the last sparkle origin (above the crates).
* **`glitz/sparkle.lua` does not exist in Chocolatier 3**: it is absent from the Chocolatier 3 `assets.pfp` (1509 entries), from the original assets' `glitz/` and from Reforged's `glitz/`. The texture `glitz/money.png` and `glitz/sparkle.xml/.png` also do not exist there. They do exist in the Chocolatier 2 archive (`re/installers/choc2/assets/assets.pfp`: `glitz/sparkle.lua` 2306 bytes, `sparkle.xml`, `sparkle.png`, `money.png`; Chocolatier 1 has `image/sparkle.png/xml`). Consequence: `Init` returns 0, the pool is never allocated, `Draw` is a no-op, and each `Update(100)` runs `Update(0.1);` in an empty Lua state (a nil-function call error, presumably logged by `_ALERT`; Unknown whether it is visible). **Inference: the original Chocolatier 3 crate game shows no sparkle**. The Chocolatier 2 script (strings only; not decompiled) has the same structure as `slotpayout.lua` (sparkle texture, Normal blend, 80 particles, `fRange`, gravity, `fFade` of gSparkleColorStart/End over 2000 ms, `gSparkleTime` gate) and a constant 0.3 near its `pScale` init (a guess at the range, not decoded); exact numbers Unknown.

## 6. Engine notes: current stubs

`src/script/generated/Stubs.inc` lists `StartFireworks`/`StopFireworks` as stub natives and `FireworksWindow` as an empty window type; `src/game/Crates.cpp:142` stubs `StartSparkle`; `src/game/Stubs.cpp:96` says `StartSlots` has no sparkle.

## 7. What the engine must implement

Minimum for Awards, crates and slots:
1. A small CPU particle system (no Lua DSL needed): pool N=80 (free/active lists), per-particle pos, up/spin, scale, rgba, age, plus effect-specific vel/gravity/spin speed; `create(acc)` with the exact accumulator rule (acc += x; while acc > 0: spawn, acc -= 1; stop when the pool is empty); `update(ms)` order: emit (Lua `Update(seconds)` equivalent), then integrate (pos += vel*dt first, then vel += g*dt), fade, shrink, spin, age, expire at age > 2000 ms.
2. Quad draw: texture w*h*scale centred at origin+pos, rotated by spin, vertex colour rgba (8 bit), alpha blend (the shipped scripts only use kBlendNormal; no additive pass needed), in active-list order, in window coordinates; skip when no particles. Needs a triangle-list textured path (`gfx/Renderer.h` already has one).
3. `FireworksWindow` + `StartFireworks`/`StopFireworks`: 9 fireworks, 40 ms animation, the state machine in section 3 (timer init rand%1000; burst x 50..299, y 100..299; 28 ticks of 40 ms simulated per burst; wait 250..749 ms), random texture from the four names, sound events `firework1..5` (can stay silent), drawn after the children so it appears over the dialog. Use `rand()%5` as in the original but treat the NULL fifth option as: no draw for that burst (or substitute `firework_white`; either is a documented deviation).
4. Slot sparkle: origin (185,244), texture `glitz/dollars_big.png` at scale 1.0, gSparkleTime = payout*0.5+1, density payout*10, 50 ms ticks, velocity/gravity/fade per section 4, only if payout > 0; keep the existing 50 ms completion semantics (finish when gSparkleTime <= 0, then `onpayout`).
5. Crates: nothing is required to match the original (no script or texture ships in Chocolatier 3). Leave `StartSparkle` as a logged no-op. If a visual is wanted it would be an invention (colour and `gSparkleTime` = 2 are known, nothing else).

Can stay logged stubs: the Lua particle DSL (`particles.lua`, `Allocate`, `fRange` ... as a general language), `SetRendererOption1..4`, `fOpenCycle/fClosedCycle/fPick/fDistance/fIndex/fAlpha/f2dRadius/fDebug` (not used by the shipped scripts), animated-texture particle frames, the editor-config branches, `ParticleColor` data sources, the sounds, and `StopFireworks` (never called).

## 8. Unknown

* `str(NULL)` outcome for the fifth `particleOptions` entry; whether the original ever drew nothing for those bursts.
* The constant subtracted from the origin in `T2dParticleRenderer::Draw` (an unresolved PIC constant; probably 0 or 0.5 px) and whether the 8x8 sprite size uses the internal (power-of-two padded) texture size instead of the image size (`GetInternalSize` is used for UVs only).
* The exact `fFade` behaviour past the last segment and with more than two colours; never reached by these scripts.
* Whether the frozen leftovers after a slot spin or a fireworks burst are visible in the original (depends on redraw and window lifetime).
* Whether the nil `Update` call in the empty crate script logs or shows anything.
* The contents of Chocolatier 3's would-be `glitz/sparkle.lua` (does not exist); Chocolatier 2's was not decompiled here.
* `rand()` quality/seed: the system uses its own `TRandom` (GUID seed); firework state uses libc `rand()`; neither is deterministic.
* Whether `kBlendNormal` (0) in `TRenderer::SetBlendMode` is straight or premultiplied alpha (not decoded here).
