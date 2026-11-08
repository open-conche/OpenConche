# Chocolatier 1: the port view (building placement, `SimpleAnimation` movement, map travel scoping)

Sources: the Windows exe decompile (`re/choc1/decomp/_unnamed.c`; addresses are exe virtual addresses), vtables read from
the Chocolatier 1 Windows `Chocolatier.exe` through the RTTI, and the shipped Lua bytecode (`re/choc1/pfp_extracted`, listed with
a workbench script; Lua cites are the source line numbers of the bytecode line table). Labels: **fact** (read in decompile
or bytecode), **inference**, **unknown**. Companion notes: `choc1-natives.md` 5, `choc1-rollover-and-quirks.md`, `choc1-fit.md` 6.

Vtables found for this note (slot numbers count 4-byte entries): `TSimpleAnimation` `004e6484` (slot 5 Init `004016d0`, slot 7 per-frame
draw `00401c10`), `TBitmapTint` `004e6634` (slot 5 Init `00402650`, slot 7 draw `00402510`), `LinearPath` `004e86e8` (slot 1 load `00419e20`,
slot 2 evaluate `004195a0`), `CircularPath` `004e8944` (slot 1 load `00418ff0`, slot 2 evaluate `004190a0`).

## 1. Building placement

### 1.1 How the port view is built (fact)

`sim/port.lua:356-394` (the port window builder, a `LPort` method; its `self` is the port) builds, in this order:

1. A background table `{x=0, y=0, image="ports/"..self.name}` (becomes a `Bitmap`, the 800x600 port picture). Everything below is
   inserted into this table's array part, so **every layout window is a child of the background Bitmap** and their coordinates are relative
   to it (0,0 = the picture's corner).
2. For each entry of `self.layout` (the `layout = {...}` list in `data/<port>.lua`, in array order = z-order, first is behind):
   * a **function** entry (`Animation{}` and `BitmapLayer{}` return closures, see 2.1): call it, and if the result is truthy insert it
     (`port.lua:366-370`);
   * a **table with `image`** (`Factory{}`, `Shop{}`, `Market{}`, `Building{}` return their own definition table: `sim/building.lua:24` just
     stores it; the constructor does not touch `x y scale area image`): `port.lua:371-376` inserts
     `Rollover{ x=e.x, y=e.y, hotSize=10, area=e.area, name=e.name, target="LBuilding:ByName('"..e.name.."')", clicksound=kDefaultPopupClickSound,
     image=e.name.."_img", BitmapTint{ name=e.name.."_img", scale=e.scale, image="ports/"..e.image } }` and then calls
     `UtilPulseImage(e.name.."_img", 30)`. Note `area` is passed through (nil for most buildings), `scale` is nil when the data omits it.
   * a table without `image` (not seen in the shipped data): a debug `Button{x=5, y=<running y>, label=e.name, command=e:SelectClosure()}`.
3. `Bitmap{image="ports/banner", x=297, y=0, Text{x=30,y=17,w=146,h=20, label=port name, font=portNameFont}}`, then
   `Bitmap{image="image/toolbar", x=0, y=369}`.

(The owned-factory overlay in `sim/factory.lua:603-663` is separate: a `Rollover{x,y,w,h,target="LFactory:StatusPopup(..)"}` per owned factory
with a `BitmapTint` of `ports/factory` and a `SimpleAnimation` of `control/factory.xml`; not needed for placement of the building art.)

### 1.2 The Rollover places itself with the raw `x`, `y` (fact), not the generic layout

`TRollover::Init` (`0041b960`) first calls the common window init `00421d20` (reads `name`, then the generic layout `004216c0` for
`x y w h align`), then reads `x` and `y` again (default 0) and sets the position with `004211d0`, which assigns left/top **directly**
(right/bottom shift by the same delta). No wrapping, no centre or far-edge bands: **a Rollover with `x=-3` has left = -3.** `w`/`h`
are only applied if both are > 0 (`00421640`); no shipped Rollover sets them. After the children exist, slot 6 (`0041b160`) runs the
common post-children step `00422300` (the `fit` key, then the generic layout `004216c0` again) and then sets the size to the union of the
children's rects together with the origin: width = max child right, height = max child bottom (`00421640`, which sets right = left + width).

The generic layout rules `004216c0` / `004212a0` / `00421200` (fact; the engine's `ui/Layout.cpp` already implements them):

* `kCenter` = 80000 and `kMax` = 160000 (lua constants used by the shipped Lua; no `kLeft` / `kRight` exists in the position math; `kHAlign*`
  / `kVAlign*` are text flags, unrelated). A position `p` with a parent whose dimension is `d`, size `s`:
  `p > 106666` -> `p += d - 160000` (far edge); else `p > 40000` -> `p += d/2 - s/2 - 80000` (centre); else `p < 0` -> **`p += d`** (negative
  = from the far edge, so `x=-3` on an 800 parent is 797). A non-negative `p <= 40000` is literal. Sizes above 80000 mean "fill" (`kMax`).
* The whole block is skipped (nothing applied, not even `y`) if `x` is outside 0..40000 **and `w` is 0** (`w` defaults to the window's
  current width), or the same for `y`/`h`. Compared as unsigned, so a negative `x` counts as out of range. A parent of width 0 skips it for any
  out-of-range value too.
* The new `x`/`y` are only taken if the window's current left/top is 0 (`004216c0` keeps a non-zero current position).
* The C1 code has **no "align given disables the negative wrap" rule**: `00421200` wraps any negative position, and `align` (default 0, read
  with the same helper as `x`, so absent and 0 are the same) only shifts the position by half/the whole size beforehand.
  (Inference for the engine: `Layout.cpp`'s `noWrap = align given` is a Chocolatier 2/3 notion; I did not find it in this exe. No shipped
  C1 window used a negative position together with `align` in the files I checked; unverified.)

For the sffactory entry: generic layout (x=-3, w=0 -> skipped), then raw position (-3, 53). The post-children generic layout is again skipped (x=-3
out of range, w=0 at that moment since the union is set afterwards). **Expected Rollover rect: left -3, top 53 (not 797).**

### 1.3 `scale` is read by `TBitmapTint` (fact); the Lua does not compute w/h

`data/sanfrancisco.lua:43`: `Factory{name="sffactory", price=10000, character="sffactorykeep", scale=0.5, x=-3, y=53, image="factory"}`.
`TBitmapTint::Init` (`00402650`) reads `image`, `mask`, `scale` (default 1.0), `alpha`, `mipmap`, `tint` (4 numbers, default 1), `dx`/`dy`
(default -9999 = "half the picture"), `hflip`, and `w`/`h` (default -1; applied only when both > 0, `00421640`). It then calls the shared
bitmap setter `0042f860(image, mask, scale)`, which sets the window size to **round(imageW * scale + 0.5) x round(imageH * scale + 0.5)** (the
`+0.5` then truncation; swapped when `hflip` is set). Drawing (`00402510`) renders the picture scaled by `scale`.
`ports/factory.png` is 395x286, so the BitmapTint is 198x143. **The engine's 198x143 is therefore already correct; the expected value is not
99x72** (that would be a second halving). The engine's only error for the factory is the Rollover's x.

Expected rects (children of the 800x600 background), all with the Rollover sized to its BitmapTint child (the only child):

| Entry (`data/sanfrancisco.lua`) | Lua keys given | Rollover rect | BitmapTint |
|---|---|---|---|
| `sffactory` (:43) | x=-3 y=53 scale=0.5 image=factory | **-3,53,198,143** | `ports/factory` 395x286 * 0.5 = 198x143 |
| `sfshop` (:46) | x=480 y=205 image=sf_shop | 480,205,(image size) | `ports/sf_shop`, scale 1 |
| `sftutorplace` (:47) | x=567 y=75 image=sf_cliffhouse | 567,75,(image size) | scale 1 |
| `sfsaloon` (:50) | x=359 y=150 image=sf_saloon | 359,150,131,133 | `ports/sf_saloon` 131x133 |
| `sfmarket` (:56) | x=0 y=42 image=sf_market `area={{206,0,260,256},{0,253,475,320}}` | 0,42,475,322 | `ports/sf_market` 475x322 |

The keys that matter for a building: `x`, `y` (raw, may be negative), `scale` (BitmapTint size and drawing), `image`, `area` (Rollover hover test:
each entry is `{x1,y1,x2,y2}` in the Rollover's own coordinates, `x1 <= px < x2`, see `choc1-rollover-and-quirks.md` 3), `name`; `price`,
`character` are game data. The market's `area` is two rectangles (upper right and the lower strip), not a single box.
A Rollover whose position is negative is partly off-screen; hit testing is in the Rollover's own coordinates and works the same.

### 1.4 Rule for the engine coder (building placement)

1. In `RolloverWindow::init`, after the generic init, set the window's left/top **to the raw `x`/`y` numbers** (defaults 0), no wrapping,
   no bands; keep the rect size at 0 until post-children.
2. In post-children, run the generic layout as now (it is a no-op for negative `x` with `w` 0), then set the size to the union of the children
   starting from the origin, and make sure nothing re-applies the generic layout after that union is set (the engine reports 797 for a Rollover whose
   `x=-3`; I could not run it, so the cause is unverified. Inference: `sizeAndPositionFromStyle` runs once more or with a non-zero `w`, which
   passes the range test and then wraps -3 to 800-3).
3. Do not touch the BitmapTint's size; `scale` handling there is already right.

## 2. `SimpleAnimation` movement

### 2.1 Where an `Animation{}` entry becomes a window (fact)

`sim/utility.lua:188-190`: `Animation = function(t) return function() return SimpleAnimation(t) end end` (a closure that calls the native
with the **unmodified** table, so every key in the data reaches `TSimpleAnimation::Init`, including `name`, `path`, `random`, `flip`).
`BitmapLayer` is the same wrapper for `Bitmap` (`utility.lua:193`). The port builder calls these closures (section 1.1) at port-window
build time, so a fresh animation (fresh start time and random draw) exists every time the port view is built.

### 2.2 Init (`004016d0`) in full (fact)

Common window init first (`name`, `x`, `y`, `w`, `h` through 1.2; the shipped animations set none, so the window is at (0,0) of the port
background). Then: `scale` (default 1.0, `+0x5c`), `anim` / `image` (sprite sheet XML or still picture; `anim` wins), `animx` / `animy`
(default 0; stored as integers `+0x38/+0x3c`), `xtile` (default 8000, `+0x40`), `ytile` (6000, `+0x44`), `flip` (bool, **default true**, `+0x48`; fact: the bool getter `00428ee0` is called with `CONCAT44(1, ...)`, default 1, as `random` is with 0), `path` (see 2.3, `+0x58`), `fps` (default 10: if > 0 the frame period `+0x4c` = **1000 / fps, integer division in ms**:
24 fps -> 41 ms, 14 -> 71, 12 -> 83, 8 -> 125, 30 -> 33), then the start time `+0x34` = the global millisecond clock `Timer` (`00426d90` /
`00426c90`) at creation, then `random`.

* **Window size**: the first frame's size (or the picture's size) if the Lua gave no `w`/`h`, **unscaled** (the scale is not applied to the
  window). The window sits at (0,0) unless `x`/`y` are given; that is correct for every San Francisco animation: **the pile-up at (0,0) is
  not a placement bug, it is the missing path offset in drawing** (the original also puts the windows at (0,0) and moves the picture inside).
* **`random`** (fact): if true, `offset = rand() % T` where `T` is the path's `time` (`path + 4`) or **20000** when there is no path (CRT
  `rand`, no visible seeding in this function). If `offset < start` then `start -= offset`. So `random` is a **random start phase**: the
  animation is that far into its cycle at creation (frame index and path position both). It does not delay the start and does not choose a
  random frame by itself. Since `Timer` is the application clock in ms, the guard only matters in the first seconds after launch (inference).

### 2.3 The `path` table (fact, `00419f00` factory, `00419e20` load, `00419d00` add point, `004195a0` evaluate)

The Lua table is `{ time=<ms>, loop=<bool>, [skip=<bool>,] {x1,y1}, {x2,y2}, ... }`. Which class is built: `radius > 0` in the table ->
`CircularPath`, else **`LinearPath`**. Every path of the shipped data is linear (no `radius`, `center`, `startAngle` anywhere in `data/*.lua`).
Common fields: `time` (`+4`, default **1000** ms, integer) and `loop` (`+8`, default false).

**LinearPath**:

* Load: points are the array entries 1..n (each `{x,y}`, missing numbers 0). The first point only sets the current point. Each further
  point appends a segment (previous point, new point, length = Euclidean distance) and adds the length to the total (`+0x24`), then becomes the
  current point. `skip` (default false; the C1 data never sets it true except the generated airship route in `sim/travel.lua:275` with false):
  when true, a segment of length >= 750 px is not added (a jump: the path teleports).
* Evaluate at elapsed `t` (ms, unsigned): if `loop`, `t = t % time`; then `t` is clamped to `time` (at or beyond `time` it returns "finished"
  and the position is the last point). Position = distance `t / time * totalLength` walked along the polyline from the first point: segments are
  consumed in order, subtracting their lengths, and the position is linear interpolation inside the segment where the remainder falls.
  **Constant speed along the whole polyline; the point list has no per-point times, a long segment takes proportionally longer.**
  A loop wraps by jumping from the last point back to the first (no segment between them): `schooner_far` jumps from (1000,232) to (600,232).
  `loop` = false stops at the last point for good.
* Second output (heading, used only for `flip`): for the segment under `t`, the normalised direction `(nx, ny)`; angle = `acos(nx)`, negated
  when `nx > 0`. (Inference from the decompile: right-moving gives a negative angle, left-moving a positive one in (pi/2, pi].)

**CircularPath** (unused by shipped data): `center {x,y}` default (0,0) (`+0xc/+0x10`), `radius` default 1 (`+0x18`), `startAngle` default 0
(`+0x14`), `time` as above. At `t` (always modulo `time`): angle = `(t % time) / time * 2*pi + startAngle`; position = `(cx + cos(angle) * radius,
cy - sin(angle) * radius)` (the sign is read from the code: the `sin` term is subtracted, so counter-clockwise on screen; `cos`/`sin` identity of
the two CRT calls is inference).

### 2.4 Per-frame draw (`00401c10`) (fact unless marked)

1. `elapsed = Timer() - start` (unsigned, includes the `random` offset).
2. `pos = (animx, animy)`; if there is a path, `pos += path(elapsed)` (the path output is an offset added to `animx/animy`).
3. The frame: `frame = (elapsed / period) % frameCount` (integer division by the period of 2.2; unsigned). Set on the sprite sheet; the
   check `-2 < frame < frameCount` is in the setter.
4. Tiling: `y = pos.y` and `x = pos.x`, drawing the sprite at `(x, y)` with `scale`, then `x += xtile` while `x < frameWidth/2 + 800`, then
   `y += ytile` and repeat while `y < frameHeight/2 + 600` (frame size in integer pixels, `>> 1`). Tiles only go right and down from the
   position. With the defaults (8000/6000) there is exactly one copy. For the clouds (`xtile = 800`, picture 800 wide) the path runs
   x from -400 to 400 and a second copy at x+800 fills the right edge. (800 and 600 are the hard-coded screen size.)
5. `flip` (default TRUE; the clouds set it false). Fact from `004195a0`: the heading is the normalised direction `(nx, ny)` of the segment under `t`
   (the last segment once stopped at the end), `acos(nx)`, negated when `nx > 0`, so right-moving is negative and left-moving is in
   (pi/2, pi]; no path gives 0 (initial value). Draw (`00401c10`): mirrored when `flip` and the angle is outside [0, pi) (the x87 argument
   passing is mangled; inference), i.e. a sprite moving RIGHT is mirrored (art faces left, as in Chocolatier 2), moving left is not, no path never.
   Quirk not reproduced: exactly horizontal rightward gives -0.0, which a literal `0 <= -0.0` would not mirror, and exactly left gives the float pi
   (`< pi` false, would mirror); the engine uses the intended rule (sign bit, so -0.0 mirrors; double pi, so left does not).
6. The sprite draw call (vtable slot 0x18) receives `(x, y, 1.0, scale, flip)`; for a still (`image`) the point is its CENTRE (inference: the same call that
   takes the centre for the Frame pieces, and the tiling bound `frameW/2 + 800` only fits a centre; sf_clouds 800x128 at animy 64 spans y 0..128). A sheet's own registration point (the sheet's per-frame origin) is applied
   inside it (inference, matches the engine's existing `reg * scale` placement). There is no visible clip to the window rect (inference:
   the paths run to x = -400..1200 while the window is only the first frame's size).

Not drawn in the original and not needed: any alpha; `xtile`/`ytile` have no other effect.

### 2.4a Mouse events (fact, vtable read from the exe)

`TSimpleAnimation` (vtable `004e6484`) has slots 14/16/17 (mouse down/up/move) = `004012f0`/`00401300`/`004012e0`, each `return 0`; the same holds for
`TBitmapTint` (`004e6634`) and the other plain window classes (`TWindow` uses `00402e30/40/50`, also `return 0`). So animations and bitmaps (the masks
`ports/sf_mask_cliff`, `ports/sf_pier_mask`, the clouds) never handle a mouse event: it falls through to the sibling behind (`choc1-rollover-and-quirks.md` 6.1)
and reaches the building Rollovers whatever the sprite rects cover. The only thing that does take a hover over the factory is the `PlayerLogo`
`Button` (44,72,106x60) in front of it. The engine matches (default `onMouse*` return false).

### 2.5 The San Francisco list (`data/sanfrancisco.lua:37-70`, fact), array order = draw order

| Line | name | image / anim | scale | fps | animx,animy | path (time ms, loop; points) | other |
|---|---|---|---|---|---|---|---|
| 37 | (clouds) | `ports/clouds_far` (800x100) | 1 | 10 | 0,0 | 210000 loop; (-400,120) -> (400,120) | random, xtile 800, flip false |
| 38 | schooner_far | `ports/boat_schooner.xml` | 0.15 | 24 | 0,3 | 40000 loop; (600,232) -> (1000,232) | random |
| 39 | airship_far | `ports/airship_fly.xml` | 0.3 | 24 | 0,3 | 90000 loop; (200,75) -> (-100,75) -> (1200,75) -> (200,75) | random |
| 40 | (lighthouse beam) | `ports/lighthouse_beam.xml` | 1 | 8 | 775,200 | none | random |
| 45 | horsebuggy_far | `ports/horsebuggywalk.xml` | 0.3 | 24 | 0,2 (the second `animy` wins) | 60000 loop; (30,194) (120,194) (230,208) (120,194) (30,194) (-100,194) (30,194) | random |
| 49 | (sf clouds) | `ports/sf_clouds` (800x128) | 1 | 10 | 0,64 | 75000 loop; (-400,0) -> (400,0) | random, xtile 800 |
| 51 | horsebuggy_close | `ports/horsebuggywalk.xml` | 0.7 | 30 | -125,-13 | 50000 loop; (30,280) (600,286) (660,300) (600,286) (30,280) | random |
| 53 | schoonerboat1 | `ports/boat_schooner.xml` | 1 | 24 | 0,0 | 5000 loop; (621,295) (620.9,291) (620.8,295) (a 4 px bob) | random |
| 57-65 | (waves, 9 entries) | `ports/wave.xml` | 0.5/0.7/0.3/0.5/0.3/0.7/0.4/0.3/0.2 | 14,12,16,12,14,10,16,18,14 | (43,394) (186,396) (255,388) (320,394) (390,392) (496,397) (670,392) (720,347) (747,342) | none | random |
| 70 | steamship_close | `ports/steamship.xml` | 1.3 | 24 | 0,20 | 25000 loop; (1200,323) -> (-400,323) | no random |

(The nine wave entries are lines 57-65; scales/fps/animx/animy are listed in line order.) The data also lists
`steamship_far`, `flag`, `test_jitter*` in `editable` (a dev list), but no entries exist for them. Other ports use `flip=false`, `xtile=800`
the same way; no shipped file sets `flip=true`, `ytile`, `radius`, `center`, `startAngle` or `skip`.

### 2.6 Rule for the engine coder (`SimpleAnimation`)

* Build a path from the `path` table (`time` default 1000 ms, `loop` default false; points = array entries; `radius`>0 would be circular but is
  unused). Keep the window at (0,0) / first-frame size; this is correct.
* Each frame: `elapsed = now - start`, where `start = creation time - (random ? rand() % T : 0)`, `T` = path time or 20000 without a path
  (skip the subtraction only if it would go below the clock's zero). Offset = polyline position at `elapsed` (loop: `elapsed % time`; no loop: clamp to the
  end), constant speed by arc length. Draw position = `animx,animy` + offset.
* Frame = `(elapsed / floor(1000/fps)) % frameCount` (the engine's `floor(elapsed*fps/1000)` differs slightly, e.g. 24 fps: 41 ms vs 41.67 ms).
* Tile with `xtile`/`ytile` as in 2.4 step 4 (with the defaults, one copy); do not clip to the window rect.
* `flip` defaults to true: mirror as in 2.4 step 5 (about the registration point); stills are centred on the point.
* Keep the registration point and the `scale` about it as the engine does now.

## 3. Map travel (brief)

Fact chain for a click on a port icon on `ui/map.lua`:

1. `ui/map.lua` (loaded as a modal): `BackgroundMusic()`, `SetAmbient(kMapAmbient)`, `SetUpdateRate`, a background `Bitmap` `image/map`, the list
   from `LPort:LayoutMap()` (`sim/port.lua:197-279`, the icon `Rollover`s, see `choc1-rollover-and-quirks.md` section on map icons), a **`MapScreen{x=0,y=0,w=800,h=600}`**
   window (native, `TMapScreen`, ctor `00418000`, vtable string `004e8640`; it registers the natives **`AnimateTravel`, `PauseTravel`,
   `ResumeTravel`**), the toolbar, medals in `empire` mode, `StandardUI("map")`, `CheatBar()`, `PlayerMessage` welcome, `UtilTriggerAd()`.
2. Hovering an icon builds the tag popup (`LPort:CreatePopup`, `port.lua:295`); the click is `DispatchRolloverClick` -> `LPort:OnPopupClick`
   (`port.lua:281-293`): if the port is `available`: when it is the current port -> `SwapToModal("ui/portview.lua")`; otherwise
   `route = LRoute:ByRoute(gSim.port.name, self.name)`, and if `route:Price() <= gSim.money` or the quest variable `LQuest._Variable.airship > 0`
   -> **`gSim:InitiateTravel(self)`**.
3. `Simulator.InitiateTravel(to)` (`sim/travel.lua:252-294`): disables the windows named `map`, `recipe`, `pause` (`UtilDisableWindow`); sets
   `gTravelCharacterEncounter=false`, `gTravelPrice=0`; with the airship quest variable builds a synthetic one-leg route (vehicle `airship`, path
   from the two ports' `mapx/mapy`, `time = floor(route.time/2 + 0.5)`, price 0); else `route:GetLegs()`, `gTravelPrice = route:Price()` (0 and
   `FreeTrip` decremented when a free trip is owed); then calls the native **`AnimateTravel(route)`** and clears `gTravelCharacterEncounter`.
4. The native `AnimateTravel` runs the travel animation window (init `00417da0`, end `00417800`, 1183 bytes) along the route's leg paths
   (`LinearPath`-based, same path class as 2.3; it increments the global `DAT_005330a0` that disables Rollovers while it runs). On arrival it posts
   `gSim:ArriveAtPort` through `gCommandQueue`; `Simulator.ArriveAtPort(port)` (`travel.lua:301-316`) deducts `gTravelPrice`, plays `BackgroundMusic`,
   a `PlayerMessage` ("arrival"), `SetPort`, then queues `Simulator.EnterPort`.

**Map travel needs**: the `MapScreen` window class (`TMapScreen`) with `AnimateTravel` / `PauseTravel` / `ResumeTravel` and the travel animation
(vehicle sprites over the route legs, the `LinearPath` of 2.3, random encounters via `gTravelCharacterEncounter`, which the Lua also reads),
the `DAT_005330a0` rollover-disable counter, `UtilDisableWindow`/`UtilEnableWindow` on `map`/`recipe`/`pause` (already in the engine per `choc1-fit.md`),
and `Simulator.EnterPort`. Not decoded here: the internals of `TMapScreen`, vehicle choice, speed per leg, encounter triggers, the sound used.
The engine's `MapScreen` supplies these natives (`choc1-map.md`).

## Unknown

* ~~Why the engine reports 797 for the Rollover (1.4 item 2)~~ **Answered.** The generic init skipped the block (x=-3, w=0), so left stayed 0; `Window::postChildrenInit` then ran the generic layout again at a moment when the window already had a non-zero size (traced: the pass sees x=-3 in the style with a non-zero current width), the range test passed, the "left is 0" rule let the new x through and the negative wrapped to 800 - 3 = 797. Which call gave the window its size before that pass was not pinned down. Fix (inference, the exe's order is not fully read): raw x/y after the generic init (as the exe) **and again after the union size** (so a late generic pass cannot undo it); left/top are then non-zero-safe for x=-3 and the two orders give the same -3,53,198,143.
* Which draw argument carries the mirror (the x87 sequences are mangled) and the exact-horizontal/-0.0 edge cases of 2.4 step 5.
* The `Timer` origin and whether it stops under modals (the engine already uses a per-modal clock); whether the sprite draw clips to the window
  (inferred no).
* Whether the sprite draw call places by registration point or top-left in the exe (the engine already uses the registration point; visually plausible: boats
  are positioned by their waterline `animy`).
* The initial value of the frame period `+0x4c` when `fps <= 0` (no shipped use).
* `TMapScreen` internals and the travel animation (3).
