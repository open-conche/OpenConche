# Chocolatier 1: the map screen and travel (`MapScreen`, `AnimateTravel`, the Lua around them)

Sources: the Windows exe decompile (`re/choc1/decomp/_unnamed.c`; addresses are exe virtual addresses; where the decompiler lost a
call's arguments the listing was read with a disassembler), vtables read from the Chocolatier 1 Windows `Chocolatier.exe`, the shipped Lua
bytecode (`re/choc1/pfp_extracted`, listed with a workbench script; Lua cites are source line numbers from the bytecode line
table) and the Chocolatier 1 Windows install's `assets/strings.xml` (a ZXML file: 8-byte header, then zlib; spreadsheet XML). Labels: **fact** (read in
decompile, listing or data), **inference** (reasoned from those), **unknown**. Companion docs: `choc1-port-view.md` section 2.3 (the
`LinearPath` class) and section 3 (the earlier scoping of the map), `choc1-natives.md` (sound, `UtilDisableWindow`),
`choc1-rollover-and-quirks.md` (the `Rollover` widget and the icon keys).

Summary of the model: **a trip is `route.legs`, each leg a polyline plus a duration in weeks. One week is exactly 2000 ms of trip
time. The native moves one vehicle sprite along the polyline of the current leg and, once per 2000 ms, asks the Lua to run
`gSim:TravelTick()` (a sim week, plus an optional character encounter). When the last leg ends it asks the Lua to run
`gSim:ArriveAtPort('<endName>')`, which charges the fare, switches the sim to the new port and queues `Simulator.EnterPort`.** Everything
about money, weeks, encounters and the port switch is Lua. The native is only the animation clock, the sprite, the dots, the vehicle
sound, the music fade and the two Lua command strings.

## 1. `ui/map.lua` and the map icons

### 1.1 The file (`ui/map.lua`, fact)

Statements in order (line numbers are the source lines):

| Line | Statement |
|---|---|
| 6 | `BackgroundMusic()` (queues a random gameplay track; native, see `choc1-natives.md` section 6) |
| 7 | `SetAmbient(kMapAmbient)` |
| 8 | `SetUpdateRate(kDefaultUpdateRate)` |
| 10 | `mapButtonGraphics = { "control/port_up", "control/port_down", "control/port_over" }` (a **global**, used by the `map` button the ledger builds on the other screens, `ui/ledger.lua:54`) |
| 12-19 | the root table `R = { x=0, y=0, w=800, h=600, image="image/map", <children...> }` (a `Bitmap`, see below) |
| 16 | child 1: `LPort:LayoutMap()`, a `Group{...}` of all port icons, labels and the current-port marker (section 1.2) |
| 17 | child 2: **`MapScreen{ x=0, y=0, w=800, h=600 }`**, the native window (no children, no other keys) |
| 18 | child 3: `Bitmap{ image="image/toolbar", x=0, y=369 }` |
| 21-23 | `if gSim.mode == "empire"`: two more children appended: `Bitmap{ image="image/trophy_shelf", x=0, y=0 }` and `gSim:LayoutMedals()` (appended through `table.insert`, so they come after the toolbar) |
| 26-31 | `MakeDialog{ Bitmap(R), StandardUI("map"), CheatBar() }` (the usual full-screen modal wrapper) |
| 33 | `if table.getn(gSim.messages) == 0 then PlayerMessage(GetString("title_welcome", gSim.player)) end` (English: `Welcome to Chocolatier, %1%.`) |
| 35 | `UtilTriggerAd()` (last statement; a host hook, a no-op without the launcher, see `choc1-natives.md` section 1.4) |

So the **draw order** inside the 800x600 background Bitmap is: the icon/label Group, then the `MapScreen` window (the vehicle and the dots are
drawn over the icons and labels), then the toolbar bitmap, then the trophy shelf and medals (empire mode); `StandardUI("map")` and the
cheat bar are siblings of the Bitmap inside the dialog, so they are above it. `MapScreen` has no children and no `name`.

`StandardUI("map")` (`ui/standard.lua`, builds through `ui/ledger.lua`) is where the three windows that travel disables come from (fact,
`ui/ledger.lua:54,56,58`): `Button{ name="map", graphics=mapButtonGraphics, x=133, y=122, command=<5th local, not read> }`,
`Button{ name="recipe", x=58, y=142, command=<closure> }` and `Button{ name="pause", x=0, y=122, command=<closure> }`; ledger display
Texts named `money`, `rank`, `time`, `score` (`ledger.lua:17-20`), the tab area, and `map_disable`, `recipe_disable`, `menu_disable`
backdrops (`ledger.lua:42-44`). On the map screen (`gCurrentLedgerScreen == "map"`) the `map` button gets a different command from the one
on the port screen (`ledger.lua:48-51`; the map button on the port screen runs `bsutil.AutoSave` then `SwapToModal("ui/map.lua")`,
`ledger.lua:32` region; not decoded further here). Any money/date display on the map is therefore the standard ledger, not map code.
Inference: nothing in `Simulator:Tick` calls `UpdateStandardUI` (the constants of `Simulator.Tick`, `sim.lua:534`, do not name it), so the
`time` text on the map screen does not advance during the trip; it refreshes when the port view is built after arrival. Unverified in the
running game.

### 1.2 `LPort:LayoutMap()` (`sim/port.lua:197-276`, fact)

Constants: `kPortIconSpacing = 15`, `kPortIconRadius = 17` (locals, line 200-201); `const.travelTagWidth = 210`, `const.travelTagHeight = 100`,
`const.screenWidth = 800` (`ui/styles.lua:24,51,52`). The port's `mapx`/`mapy` are the data values with **`mapy + LPort.mapYOffset` (17)
already added** by `DefinePort` (`port.lua:28-31`), and every route waypoint `y` gets the same +17 (`travel.lua:161`), so icons, labels
and paths share one coordinate space. Example: San Francisco data `mapx=110, mapy=115` becomes `(110, 132)`.

For each port in `LPort:AllPorts()` (insertion order of `DefinePort`, which is the load order of `data/ports.lua`: sanfrancisco merida quito
newyork trinidad riodejaneiro london accra istanbul mahajanga colombo hongkong sulawesi sydney):

1. `yOffset = -travelTagHeight / 2` (-50). `xOffset = 0`, or `-travelTagWidth` (-210) when `mapx + 210 >= 800` (the tag is placed to the left).
2. `x = mapx - 17`, `y = mapy - 17`.
3. If the port **is** `gSim.port`: `Bitmap{ x=x, y=y+7, image="control/port_current" }` is added (the current-port marker; it is a plain
   bitmap, not a Rollover, drawn first, so the icon Rollover is over it). That is the only place the current port is marked.
4. If `port.available`: one `Rollover` (always the same keys, the child differs):
   `{ x=x, y=y, xAbsolute=mapx, yAbsolute=mapy, xOffset=<above>, yOffset=-50, hotSize=60, target="LPort:ByName('<name>')", clicksound=kDefaultPopupClickSound, <child> }`
   with the child chosen by the first matching rule:
   * `port.factoryowned` (note: the **port's** flag) and `LFactory:ByPort(name):Running()`: `SimpleAnimation{ animx=16, animy=16, name=<port>, anim="control/factory.xml", fps=4 }`;
   * `factoryowned` and not running: `Bitmap{ x=0, y=0, image="control/factory_idle_single" }`;
   * `port.shopowned`: `Bitmap{ name=<port>, image="control/icon_shop" }`;
   * `not port.visited`: `SimpleAnimation{ animx=16, animy=16, name=<port>, anim="control/port_new.xml", fps=3 }`;
   * otherwise `Bitmap{ name=<port>, image="control/port_available" }`.
5. If **not** `port.available` and **not** `port.hidden`: the same Rollover keys **without `clicksound`**, child
   `Bitmap{ name=<port>, image="control/port_unavailable" }`. (The target is still set, so the popup shows "Port Not Open".)
6. If `port.hidden` (and not available): no Rollover, no label (the function skips to the next port). All shipped ports have `available = false`
   in their `DefinePort` data (the same `false` literal is read in the bytecode of every `data/<port>.lua` except San Francisco, which does not
   set it, i.e. nil/false initially; the Lua unlocks ports later, `Simulator:EnablePort`). `hidden` is read by the Lua; which data sets it was not searched.
7. Labels (every non-hidden port, three `Text` windows each, `w=140, h=20, label=<port name>`, `flags` below): two drop shadows at
   `(x+2, y+2)` and `(x+1, y+1)` with `font=portLabelDropFont`, then the label at `(x, y)` with `font=portLabelFont`, where `(x, y)` and
   `flags` come from `port.labelpos` and `mapx, mapy`:

   | `labelpos` | x | y | flags |
   |---|---|---|---|
   | `kHAlignLeft` | `mapx - 15 - 143` | `mapy - 10` | `kHAlignRight + kVAlignCenter` |
   | `kVAlignBottom` | `mapx - 75` | `mapy + 15` | `kHAlignCenter + kVAlignTop` |
   | `kVAlignTop` | `mapx - 75` | `mapy - 15 - 20` | `kHAlignCenter + kVAlignBottom` |
   | anything else (`kHAlignRight` in the data) | `mapx + 15` | `mapy - 10` | `kHAlignLeft + kVAlignCenter` |

   then `x` and `y` are clamped to a minimum of 0. Shipped data: San Francisco `kVAlignBottom`; accra colombo hongkong istanbul mahajanga
   newyork quito sulawesi trinidad `kHAlignRight`; london merida riodejaneiro sydney `kHAlignLeft`.
8. The function returns `Group(layout)`.

Port data (`mapx, mapy` as in the data files, before the +17): sanfrancisco 110,115; merida 165,171; quito 183,231; newyork 207,118;
trinidad 224,204; riodejaneiro 263,307; london 364,87; accra 378,213; istanbul 436,116; mahajanga 487,293; colombo 571,213; hongkong
634,161; sulawesi 670,231; sydney 740,343.

The Rollover keys are the engine's existing C1 Rollover (`src/games/choc1/Rollover.cpp`, `choc1-rollover-and-quirks.md` section 9).
Nothing else in the map screen needs a new window type.

### 1.3 The popup (`LPort:CreatePopup`, `sim/port.lua:295-347`, fact)

Called by the Rollover through `target` (`LPort:ByName('<name>')` returns the port; the popup contents come from the port's `CreatePopup`,
reached through the engine's `DispatchRolloverClick`/`_rollover` path already built for the port view). It builds a text and wraps it in a tag
bitmap:

* Lines (joined with `"\n"`), first line always `"#" .. GetString(port.name)` (the `#` is the literal first character of the label; whether the
  `Text` window treats it as a style marker is unread, **unknown**).
* **Port not available**: mode `free` -> `travel_freedisabled` ("This port may be opened in Story Mode"), otherwise `travel_visarequired`
  ("Port Not Open "). Nothing else.
* **Port available, not the current port**: `route = LRoute:ByRoute(gSim.port.name, port.name)`, `price = route:Price()`.
  * quest variable `LQuest._Variable.airship > 0`: `price = 0` for display; line `GetVariableString("travel_time", { time = floor(route.time / 2 + 0.5) })`
    ("A <time>-week journey."), **no** price line;
  * else: `GetVariableString("travel_time", route)` (reads `route.time`) and `GetVariableString("travel_price", { price = Dollars(price) })`
    ("This trip will cost <price>.");
  * then `price <= gSim.money` -> `travel_click` ("Click to go here.") else `travel_expensive` ("You can not afford this.").
* **Port available and the current port**: `travel_here` ("You are here."). (`travel_enter`, "Click to enter port.", is in the bytecode as
  the else branch but is unreachable: the branch is only entered when `gSim.port == port`; fact for the reading, a leftover in the source.)
* Tag: a `Bitmap{ fit=true, x=0, y=0, image="image/traveltag", w=210, h=100, [hflip=true], Text{ x=5 or 51, y=5, w=kMax-51 or kMax-5, h=kMax-5, label=<text>, font=travelTagFont, flags=kVAlignCenter+kHAlignLeft } }`.
  When `mapx + 210 >= 800` the tag is mirrored (`hflip`) and the text starts at x=5 with width `kMax - 51`; otherwise the text starts at
  x=51 with width `kMax - 5`. Returned as `{ Bitmap{...} }` for the Rollover popup.

`travel_stowaway` exists in `strings.xml` but nothing in the files read here uses it.

### 1.4 The click: `LPort:OnPopupClick` (`sim/port.lua:281-293`, fact)

```
if self.available then
  if gSim.port ~= self then
    price = LRoute:ByRoute(gSim.port.name, self.name):Price()
    if price <= gSim.money or LQuest._Variable.airship > 0 then gSim:InitiateTravel(self) end   -- otherwise nothing
  else
    SwapToModal("ui/portview.lua")          -- click on the current port: back to the port view
  end
end                                          -- unavailable: nothing
```

(Paraphrase of the bytecode.) Note: this checks the price with the jittered `Price()` (section 4.2), the same number the popup showed, so the
gate and the popup agree. The click sound is the Rollover's `clicksound`.

## 2. The `MapScreen` window class (`TMapScreen`)

Class `TMapScreen`, vtable **`004e867c`**, ctor `00418000`, dtor `004172f0`, deleting dtor `00417730`, class-name string `004e8640`
`"TMapScreen"` (returned by `004161e0`), factory `00418520` (`CreateObject`). The object is 0xb0 bytes (fields used below are offsets). Slots
(fact, read from the vtable): 5 Init `00421d20` (**the plain `TWindow` Init**, so the Lua keys are only the generic ones: `x y w h`, plus `name` as
usual), 6 PostInit `004161f0` (the `TWindow` post-init `00422300`, then it starts the window's own timer, below), 7 draw `004167d0`, 13 update
`00417800`; everything else is the base window. The mouse slots are the base `return 0` ones (the same set the other plain windows have), so
the map screen **does not consume mouse or key events**: the port icons are Rollovers in sibling windows.

Constructor (fact): resources loaded once per window, as shared pointers kept in the object:

| Field | What | Asset |
|---|---|---|
| `+0x68` | boat sprite sheet | `image/icon_boat.xml` (7 frames; sheet 421x54, per-frame registration points in the XML) |
| `+0x70` | train sheet | `image/icon_train.xml` (7 frames, 347x53) |
| `+0x78` | airship sheet | `image/icon_airship.xml` (7 frames, 422x61) |
| `+0x98` | the dot bitmap | `image/traveldot` (8x8 png) |

and it registers three Lua natives bound to this object (the old ones are replaced, `choc1-natives.md` section 1; the dtor registers `nil`, so the
three exist only while a `MapScreen` window exists; the engine's choice of "always bound, look the window up in the modal chain" is also fine):

* **`AnimateTravel(route)`** -> `00417da0` (section 3).
* **`PauseTravel()`** -> `00416210`, a `jmp` to `004213b0`: **detaches the window's `TWindowAnim`** (`window+0x30`): if there is one, its owner pointer
  (`+0x18`) is zeroed and the window's pointer cleared. (Fact for the code.)
* **`ResumeTravel()`** -> `00416220`: `lastTime = now` (`+0x60 := Timer`), then recreates the window animation with period **62 ms**
  (`00421980(0x3e, 1, 1, 1)`: creates the `TWindowAnim` if missing, sets period 62, `catchUp` flag 1, flag 1). (Fact for the code.)

How the update runs (fact): the base `TWindowAnim` (vtable `004e925c`, tick `0042ff80`) is a periodic timer: it advances `nextDue += period` and
calls its owner's virtual slot 13 (`vtable+0x34`) while `nextDue < now`, resetting `nextDue` to now if it is more than 1000 ms late; `TMapScreen`
starts it with period 62 ms in its PostInit (`004161f0`), so **`TMapScreen::update` (`00417800`) runs about every 62 ms** (about 16 Hz) for the life
of the window, whether or not a trip is active. Inference from this: after `PauseTravel` the owner pointer is null, so the 62 ms timer's calls
do nothing (the slot call returns 0 for a null owner, `00421400`) and the animation, **and the trip clock, stop**; `ResumeTravel` re-arms it
and also resets `lastTime`, so the paused interval is not counted into the trip's elapsed time. This is exactly what the Lua needs: `Simulator:TravelTick`
(`sim.lua:591`) wraps its encounter dialog as `PauseTravel(); building:UISelect(); ResumeTravel()` (`sim.lua:614-616`). The Lua is the only
caller of both (grep of all bytecode: `PauseTravel` and `ResumeTravel` appear only in `sim/sim.lua`).

`MapScreen` draws nothing but the trip: see section 3.4. It has no message handler of its own, no `OnKeyDown` and no children.

## 3. `AnimateTravel(route)` step by step

### 3.1 The argument (fact)

The `route` is the **Lua route object** (`LRoute`, from `LRoute:ByRoute(from.name, to.name)`, or the synthetic airship table), with the keys
the native reads:

* `endName` (string): the destination port name. Stored (`+0x58`) and used for the arrival command and the sting.
* `legs` (array, 1..n, no gaps; the loop stops at the first nil): each a leg table with
  * `path`: a path table in the format of `choc1-port-view.md` section 2.3: `{ skip=<bool>, {x1,y1}, {x2,y2}, ... }` (built by `LLeg:GetPath`, `travel.lua:53-70`:
    `{skip=true, {startPort.mapx, startPort.mapy}, <waypoints...>, {endPort.mapx, endPort.mapy}}`). The native builds a `LinearPath` from it with the
    port-view loader (`00417650` -> `00419e20`);
  * `time` (number, **weeks**, default 1.0 if missing): the leg's duration is `round(time * 2000)` ms; **this overwrites the path's own `time` field** (`path+4`);
  * `vehicle` (string, default `"boat"`): the vehicle for this leg (`"boat"`, `"train"`, `"airship"` occur).

The other keys (`startName`, `price`, `waypoints`) are not read by the native. Only if `route.legs` exists does the rest run (section 3.2 step 7);
a route without `legs` does nothing at all.

How the Lua builds it (section 4): `InitiateTravel` calls `route:GetLegs()` first (`travel.lua:280`) so `route.legs` is filled (via-routes are
expanded into their parts), or builds the one-leg airship route itself.

### 3.2 `AnimateTravel` (`00417da0`, fact)

1. `elapsed (+0x5c) = 0`, trail clock (`+0xac`) `= 0`, current leg index (`+0x34`) `= 0`, last-played vehicle (`+0x88`) `= ""`.
2. `endName` copied from `route.endName`.
3. If `route.legs` is nil: return (nothing else happens, the window stays inactive).
4. For `i = 1, 2, ...` while `route.legs[i]` is non-nil: if `leg.path` is non-nil, build a `LinearPath` (object 0x3c bytes) from it and push it on the
   path list (`+0x38`, begin at `+0x3c`); `duration = round(leg.time (default 1.0) * 2000)` stored in the path's `+4` (round to nearest, x87 default);
   push `leg.vehicle` (default `"boat"`) on the vehicle list (`+0x48`, begin at `+0x4c`). A leg without `path` adds no path **and no vehicle** (the two
   lists then go out of step; the shipped data never does this).
5. `lastTime (+0x60) = Timer` (the application clock in ms).
6. Register the window in the app's updatables list (`0042ebf0(this)`), `DAT_005330a0 += 1` (disables every Rollover popup, `choc1-rollover-and-quirks.md`), `active (+0x64) = 1`.
7. Returns nothing. (Nothing is queued here; the first tick comes at the next 62 ms update.)

It does not clear old legs or dots: calling it twice on the same window appends (the game builds a fresh map modal per trip, so this never happens).

### 3.3 The per-update algorithm (`TMapScreen::update`, `00417800`, fact unless marked)

Run about every 62 ms (section 2). Returns 1 always; sets the app's "needs redraw" byte (`+0x27`) on exit (inference: a dirty flag; irrelevant for an
engine that redraws every frame).

1. If `active == 0`: nothing (exit).
2. `now = Timer`; `elapsed += now - lastTime`; `lastTime = now`. (`elapsed` is **in ms inside the current leg**, see step 4; `uint`.)
   `weekTicks = elapsed/2000 - oldElapsed/2000` (integer division, before step 4 reduces it): the number of 2000 ms boundaries crossed by this update.
3. `leg = legs[idx]`. Run the dot sampler (`00417750(leg, elapsed)`): `t = min(elapsed, leg.duration)`; for every full 200 ms between the trail clock
   (`+0xac`) and `t`: `trailClock += 200`, evaluate the path at `trailClock` and **append that point to the dot list** (`+0xa0`, nodes with float x, y).
   So a dot every 200 ms of leg time (10 per 2000 ms week), at the vehicle's position at that time. Dots are never removed, also not between legs.
4. `done = leg.path.evaluate(elapsed, &pos(+0x8c), &heading(+0x94))` (LinearPath slot 2, `choc1-port-view.md` 2.3: position along the polyline by arc length,
   constant speed over the leg, `done` true when `elapsed >= duration`). While `done` and `idx < legCount - 1`: `elapsed -= leg.duration`, `idx += 1`,
   trail clock `= 0`, take the next leg, sample dots for it, evaluate again. (So the carry-over of overshoot into the next leg is exact.)
5. If `idx == last` and `duration - elapsed < 1000` (unsigned, so false once `elapsed > duration`): start the music fade (`00404830(DAT_00533044)`):
   if music is state 1 (playing) it goes to state 2 with a 1000 ms linear fade (`choc1-natives.md` section 6, `StartMusic`). Idempotent.
6. Vehicle sprite: default boat sheet; if `vehicles[idx] == "train"` use the train sheet, else if `== "airship"` the airship sheet (any other string, including
   `"boat"`, keeps the boat).
7. Vehicle sound: if `vehicles[idx]` differs (`strcmp`) from the last one played (`+0x88`): store it and **`PlaySound(<vehicle string>)`** (`004041d0`): `"boat"` ->
   `audio/sfx/ship.ogg`, `"train"` -> `train.ogg`, `"airship"` -> `airship.ogg` (`choc1-natives.md` section 6). This fires on the first update of the trip and
   again whenever the next leg has a different vehicle. (Inference on the compare result: `004259c0` is a `strcmp`-like returning non-zero for different
   strings, `00425a40` is its "equal" wrapper; the initial empty string guarantees the first leg plays.)
8. Dispatch, by `weekTicks` and `done`:
   * `weekTicks >= 1` and `not done`: run the Lua string `table.insert(gCommandQueue, function() gSim:TravelTick() end)` (via `DoLuaString`, `0043b220`; so the
     tick runs when the main loop drains `gCommandQueue`, not inside the update), then `lastTime = Timer` (so any time the queue spends, e.g. an encounter
     dialog, is not charged to the trip even if the pause calls did not stop the clock).
   * `weekTicks >= 1` and `done` (the final boundary): the same with the **sting argument**: `table.insert(gCommandQueue, function() gSim:TravelTick('<endName>') end)`, then
     `lastTime = Timer`. The trip stays active; the next update (62 ms later) sees `weekTicks == 0`.
   * `weekTicks == 0` and `done` (this is the final leg finished and no boundary crossed in this update): `active = 0`; run
     `table.insert(gCommandQueue, function() gSim:ArriveAtPort('<endName>') end)`; remove the window from the updatables list (`0042e780`); **`DAT_005330a0 -= 1`**.
   * `weekTicks == 0` and not done: nothing.

   Consequence (fact for the structure, inference that boundaries coincide): leg durations are whole multiples of 2000, and `elapsed` is reduced by leg durations
   only, so the 2000 ms boundaries line up with week ends across legs. A trip of W weeks therefore queues `TravelTick` W times (once per 2000 ms of trip time,
   **including the very last boundary**, which carries the sting `endName`) and then, on the following update, `ArriveAtPort`. Total trip time is
   `2000 * sum(leg.time)` ms plus up to two update periods (62 ms each). For an airship route (`time = floor(route.time/2 + 0.5)`) fewer weeks pass.
   A route whose last boundary is skipped because a single update spans it with `done` true (very long frame stalls): the `TravelTick` count is then W-1 and the
   arrival is queued in the same update only when `weekTicks == 0`; with a multi-boundary jump `weekTicks` counts all crossed boundaries but only **one** `TravelTick`
   is queued per update (a stall loses weeks; inference from the code, no catch-up loop).

### 3.4 The draw (`004167d0`, fact; the details of the sprite draw call are inference)

Only if `elapsed != 0` (so nothing is drawn before the first update accumulates time):
1. each dot in the list: draw `image/traveldot` at the dot's `(x, y)` with scale 1, no flip (drawn first, under the vehicle);
2. the vehicle: set the sheet frame to `(elapsed / 100) % frameCount` (`elapsed` is the **leg-relative** ms, so the animation restarts its phase at each leg change; 100 ms per frame, 7 frames);
3. draw the sheet at `pos (+0x8c)` with scale 1 and `flip = (heading < 0 or heading >= pi)`: the same rule as `choc1::SimpleAnimation` (`choc1-port-view.md` 2.4 step 5): a vehicle moving
   right (heading negative) is mirrored, moving left is not (the art faces left, like Chocolatier 2). The sheet's per-frame registration point (the `regx/regy` of the XML) places the frame on the point.

The whole thing runs between `Begin2d`/`End2d` calls (`0041fb30`/`0041fb80`) as every window draw does. The window rect is 800x600 so no clipping issue. Not drawn: any text, alpha or fade. There is no
ledger or date drawn by the window.

### 3.5 What the native does **not** do (fact)

No date or money change, no `UpdateDynamicWindow`, no message, no window enable/disable (the Lua disables `map recipe pause` before the call and nothing re-enables them
here; they are inside the map modal that `EnterPort` swaps away), no music start (only the fade of step 5; `ArriveAtPort` queues the next track with `BackgroundMusic()`).

## 4. The Lua around the trip

### 4.1 `InitiateTravel(to)` (`sim/travel.lua:252-294`, fact)

1. `UtilDisableWindow("map")`, `("recipe")`, `("pause")` (clears the enabled bit `0x100` of the first window of that name under the screen root, `choc1-natives.md`).
2. `gTravelCharacterEncounter = false`; `gTravelPrice = 0`; `from = self.port`; `route = LRoute:ByRoute(from.name, to.name)`; `assert(route)`.
3. If `LQuest._Variable.airship > 0`: `time = floor(route.time / 2 + 0.5)`; `route = { startName = from.name, endName = to.name, price = 0, time = time, legs = { { time = time, vehicle = "airship", path = { skip = false, {from.mapx, from.mapy}, {to.mapx, to.mapy} } } } }` (one straight leg, `gTravelPrice` stays 0).
4. Else: `route:GetLegs()` (side effect: fills `route.legs`, expanding `via` routes), `gTravelPrice = route:Price()`; then if `LQuest._Variable.FreeTrip` is set and `> 0`: `gTravelPrice = 0` and `FreeTrip = FreeTrip - 1`.
5. `AnimateTravel(route)` (returns immediately), then `gTravelCharacterEncounter = nil`.

### 4.2 Data shapes and the price (fact)

* `data/routes.lua`: 91 calls `DefineRoute{ "<start>", "<end>", [time=<weeks>], [price=<n>], [vehicle="train"], [waypoints={ {x,y}, ... }], [via="<port>"] }` (the start and end are array entries 1 and 2).
  Many routes are `via` routes (no waypoints of their own). Vehicles used: only `"train"` (sanfrancisco-merida, sanfrancisco-newyork, merida-quito, quito-trinidad, istanbul-colombo, istanbul-hongkong); all other legs default to `"boat"`. `"airship"` appears only in the synthetic route.
* `DefineRoute` (`travel.lua:139-173`): for a non-`via` route: `time = time or 1`; `price = price or time * Simulator.travelcost` (`travelcost = 150`, `sim.lua:9`); build one `LLeg{ startName, endName, waypoints (each y += LPort.mapYOffset), vehicle (default "boat"), time, price }`; the route gets `legs = { leg }` and loses `waypoints`/`vehicle`. A `via` route keeps `via` and gets its legs later in `GetLegs`.
* `LRoute:ByRoute(a, b)` (`travel.lua:178-187`): looks up `a..b`, else `b..a` and returns `CreateReverseRoute()` (same time and price, the legs reversed in order and each reversed with `CreateReverseLeg`: swapped names, same vehicle/time/price, path reversed). Missing: a dev message, nil.
* `LRoute:GetLegs()` (`travel.lua:192-218`): for a `via` route: `leg1 = LLeg:ByRoute(start, via)` and `legs2 = LRoute:ByRoute(via, end):GetLegs()`; `legs = { leg1, legs2... }`, `time` and `price` are the sums unless the route set its own; `via` is cleared. So a trip across several ports passes through the intermediate port **without entering it** (it only appears as a leg endpoint, and the path goes through that port's `mapx,mapy`); e.g. SF to Trinidad via Quito: leg SF-Quito 2 weeks (445), leg Quito-Trinidad 1 week by train. (`LLeg:ByRoute(a,b)` also tries the reversed leg.)
* `LRoute:Price()` (`travel.lua:118-134`): `p = gSim.travelPrices[self.endName]`; if nil: `base = self.price or Simulator.travelcost * self.time`; `p = floor(base * bsutil.random(80, 120) / 100)`, cached in `gSim.travelPrices[endName]` (the table is reset to `{}` in `Simulator:SetPort`, `sim.lua:826`, so the jitter is fixed for the stay in a port, and the popup and the click gate agree). Then: `airship > 0` -> 0; else `FreeTrip` truthy and `> 0` -> 0; else `p`. (Whether `bsutil.random(80,120)` is inclusive of 120 is the native's; not read here.)
* Weeks per leg: `leg.time` (default 1; 2 and 3 appear on long hops). `route.time` for a via route is the sum.
* `LLeg:GetPath` (`travel.lua:53-70`) is run once for every leg at start-up (`LRoute.PostProcessAll`, `travel.lua:239-246`), so `leg.path` exists before the first trip.

### 4.3 `TravelTick(sting)` (`sim/sim.lua:591-626`, fact)

Runs from `gCommandQueue` once per 2000 ms of trip time:
1. If `self.rank > 0` and not `gTravelCharacterEncounter`: `b = LBuilding:ByName("travel")` (the `travel` building of `data/travelers.lua`, `encounterOdds = 20`, `travelers.lua:81`); `odds = b.encounterOdds or 20`; `encounter = bsutil.random(100) <= odds`;
   if quests are enabled and the active `quest` has its `ender` among `b`'s characters and `quest.goals.endTime < self.time`: `encounter = true` (as read from the bytecode: an expired quest forces the meeting).
   If `encounter`: `gTravelCharacterEncounter = true` (so at most one encounter per trip); `PauseTravel()`; `b:UISelect()` (a modal character dialog); `ResumeTravel()`.
2. `self:Tick()` (`Simulator:Tick(weeks = 1)`, `sim.lua:534`): `time += 1` (**one sim week**), `PlayerMessage("newyear")` on a year boundary else `PlayerMessage("tick")`, factory production runs, ingredient spoilage, `ProjectProduction`.
3. If `sting`: `StartMusic(sting .. "_sting.ogg")` (a per-port sting file, `<endName>_sting.ogg`; the file existence is not checked here).

### 4.4 `ArriveAtPort(port)` (`travel.lua:301-316`, fact) and `EnterPort`

`port` may be a name string (the native passes one): `port = LPort:ByName(port)`. `gTravelPrice = gTravelPrice or 0`; **`if gTravelPrice <= gSim.money then AdjustMoney(-gTravelPrice) end`** (an unaffordable fare is simply not charged; only possible by the airship/FreeTrip paths or a money change mid-trip); `gTravelPrice = nil`; `BackgroundMusic()`;
`PlayerMessage(GetString("arrival", GetString(port.name)))` ("Arrived in %1%."); `self:SetPort(port)` (`sim.lua:805`: if the port changed: `port`, `portName`, `days = 0`, `haggle = true`, new `portPrices` (`PreparePricing`, `ApplyTipPricing`), `travelPrices = {}`);
then `table.insert(gCommandQueue, Simulator.EnterPort)`. `Simulator.EnterPort` (`travel.lua:297`) calls `gSim.port:EnterPort()` (`port.lua:182`): `SwapToModal("ui/portview.lua")`, and for Sydney `gSim:EvaluateMedals()` / `AwardMedal`. So the map modal is replaced by the port view; the windows disabled by `InitiateTravel` go with the map modal.

Money is charged **on arrival**, not at departure. The display of the money after arrival comes from the port view's ledger. The airship/`FreeTrip` cases skip the fare because `Price()` is 0.

## 5. Reuse and differences against the Chocolatier 2 map

What exists in the engine for Chocolatier 2 (`docs/re/choc2-map-binary.md`, `src/games/choc2/Map.cpp`, `MapTravel.cpp`, `games/common/MapWindow`, `Travel`) and how it maps:

| Piece | Chocolatier 2 | Chocolatier 1 | Reusable? |
|---|---|---|---|
| Window types | `Map` (icons, colour mask) plus `MapTravel` (vehicle) windows, two Lua-created layers | **One** native window `MapScreen`, empty, over the Lua icon Group; the icons are Lua `Rollover`s | New window class; no `colormask`, no `ports/map_mask_*`, no `visited` style table |
| Trip driver | Lua coroutine calls `InitiateTravel(leg)` per leg, `TravelOneWeek(type)`, `StillTravelling()`, `Pause(1900)` | The native owns the whole trip (all legs at once), calls back into Lua with `gCommandQueue` strings | Different protocol; the week boundary (2000 ms) is the same constant |
| Week length | `weeks * 2000` ms, tick-counted at 40 ms | `time * 2000` ms of **wall time** (Timer deltas), 62 ms update period; pause = detach the timer and reset `lastTime` | Same number; clock differs. `WeekMover`/`Travel` classes (30 ms tasks, fractions, 12 px dots) do not fit: C1's dots are every 200 ms of time |
| Path | `TCurve` (Bezier chains) | `LinearPath` polyline, arc-length constant speed per leg (`choc1-port-view.md` 2.3) | The existing `choc1::SimpleAnimation` LinearPath evaluator (`SimpleAnimation.h:33`, `004195a0`) is the right primitive (position and heading); `gfx::Curve` is not needed |
| Vehicle art | per-type sheets, 16-way heading frames, rotation | 3 sheets (`icon_boat/train/airship.xml`, 7 frames, 100 ms per frame), no rotation, **mirrored when moving right** | Mirror rule shared with `SimpleAnimation`; no 16-way logic |
| Dots | `image/traveldot`, every 160 ms of the tick clock | `image/traveldot`, every 200 ms of leg time, never removed | Same image, different cadence |
| Sound | ambient plus one-shot per vehicle type (`choc2-map-binary.md` 1.4), not in engine | one `PlaySound(vehicle)` when the vehicle changes (boat=`ship.ogg`, train, airship), music fade in the last second | New; `PlaySound` already exists for C1 (`Choc1AudioNatives`) |
| Rollover tag | Lua `LPort:MapRollover`, C2 Rollover | Lua `LPort:CreatePopup`, `choc1::RolloverWindow` (absolute mode, `hotSize=60`) | Already done |
| Rollover gate | none | `DAT_005330a0` (`Choc1World::mapTrips`), raised by `AnimateTravel`, lowered on arrival | Needs the counter hooked in |
| Arrival | Lua (`ArriveAtPort`-like) | queued string `gSim:ArriveAtPort('<endName>')` | Needs `gCommandQueue` (engine has it, `ScriptHost.cpp:272,306`) |
| Pause | `TMap` message 1001 opens the pause dialog while travelling | none: only Lua `PauseTravel`/`ResumeTravel` around an encounter | Not needed |

So almost nothing of the C2 map code is reusable; the shared constants (2000 ms/week, `traveldot`) and the `Rollover` plumbing are. Needed from the
engine side: a `ui::Window` subclass with an update every ~62 ms (or every frame with the same time arithmetic), the three natives, a `LinearPath`
evaluator that is callable from outside the `SimpleAnimation` class, a sheet draw with a flip, and the `gCommandQueue` string execution.

## 6. What an engine implementation needs (checklist)

1. Window type `MapScreen` (`x y w h`, no children): load the three vehicle sheets and `image/traveldot` at construction; bind `AnimateTravel`, `PauseTravel`, `ResumeTravel`.
2. `AnimateTravel(route)`: sections 3.1-3.2. Read `route.endName` and `route.legs[i].{path,time,vehicle}`; build per-leg polylines (the points in the table, `skip` as in 2.3), durations `round(time*2000)`, vehicles; set start clock; `mapTrips++`; mark active. Do nothing if `legs` is nil.
3. Per update (62 ms or per frame): section 3.3 exactly: wall-clock deltas, week boundary count, one `TravelTick` per update at most, last-boundary sting, arrival one update later, `mapTrips--`, no `ArriveAtPort` twice (clear `active`).
4. Draw: section 3.4.
5. `PauseTravel`: stop advancing the clock (stop the timer). `ResumeTravel`: set `lastTime = now`, restart. Both are no-ops without an active trip, but must exist.
6. Sound: `PlaySound(vehicle)` on change, music fade in the final second (the engine's `StartMusic` fade state machine).
7. Strings run through the existing `gCommandQueue` path: `gSim:TravelTick()`, `gSim:TravelTick('<port>')`, `gSim:ArriveAtPort('<port>')`.

## 7. What a headless test can assert

* After loading `ui/map.lua` (Story game, port San Francisco): the tree has the root `Bitmap` 800x600 `image/map` with children in the order Group (icons, labels), `MapScreen` (0,0,800,600), `Bitmap image/toolbar` at (0,369); `StandardUI` windows `map`, `recipe`, `pause`, `money`, `rank`, `time`, `score`; the current port has a `Bitmap control/port_current` at `(mapx-17, mapy-10)` (for San Francisco, data `mapx=110,mapy=115` -> `(93, 122)`); each available port a Rollover at `(mapx-17, mapy-17)`, 14 ports minus hidden ones, with three label Texts each (`w=140,h=20`), `UtilTriggerAd` called last.
* Click San Francisco -> Merida (train, 1 week, price 200 +-20%): `dump` shows `map`, `recipe`, `pause` disabled; `gTravelPrice` (global) equals `LRoute:ByRoute("sanfrancisco","merida"):Price()`; `mapTrips == 1`; no Rollover popup opens while the trip runs (hover a port icon: nothing).
* After about 2000 ms: `gSim.time` increased by 1 and `PlayerMessage "tick"`. At about 2000 ms + 62..124 ms: `gSim.port.name == "merida"`, money reduced by the fare, `ui/portview.lua` is the top modal (the Merida port view), `mapTrips == 0`, `gTravelPrice == nil`.
* A two-leg trip (San Francisco to Trinidad via Quito, 3 weeks): `TravelTick` ran 3 times, the last with argument `trinidad`; the `MapScreen` has 2 legs: dots every 200 ms of leg time (about 20 in leg 1 (2 weeks), about 10 in leg 2) and the sound `ship` then `train` played.
* Airship quest variable > 0: price 0, time `floor(route.time/2+0.5)` weeks, one leg, vehicle `airship`.
* The trip with an encounter (rank > 0, `random(100) <= 20` forced through the test RNG): `PauseTravel`, the `travel` building's modal opens, closing it calls `ResumeTravel`, and the elapsed trip time did not grow during the dialog.

## Implementation notes

`games/choc1/MapScreen.{h,cpp}`: `MapTrip` (plain logic of section 3.2/3.3 with step numbers in the comments) and the `MapScreen` window.
Settled or decided while implementing:

* **Clock (inference, decision).** "Timer" is the owning modal's clock (`WindowManager::modalNow`), the same clock the 62 ms window animation runs on, so a dialog over the map (the encounter dialog) stops the trip even without `PauseTravel`. `PauseTravel` stops the window animation, `ResumeTravel` resets `lastTime` and restarts it at 62 ms (`catchUp` false: a late frame runs one update, like the exe's reset-when-1000-ms-late). Times are whole ms (floor).
* **Gate.** `Choc1World::mapTrips` is raised once per window by `AnimateTravel`, lowered by the arrival step and by the window's destructor (the exe leaks it on a mid-trip teardown, unknown 7 below: engine choice).
* **Second `AnimateTravel` on one window** replaces the legs and dots instead of appending (the game never does it).
* **Music fade (inference).** Step 5 calls `Choc1Music::start("")` only while the track is `Playing`; the exe's `00404830` changes the same state, but whether it also drops the queue is unread. At arrival `BackgroundMusic()` queues the next track anyway.
* **Dots** are drawn centred on their point (the unknown about top-left vs centred stays an inference). Vehicle frames mirror about the registration point as `SimpleAnimation` (`mirrored(true, ...)`).
* **Verified headless** (`check_choc1_story.py` run 2): clicking the Merida icon's tag (SF to Merida, train, 1 week) gives `trip=active leg=1/1 vehicle=train`, `elapsed` about 1170 ms with `dots = elapsed / 200` at 1.2 s, no Rollover popup opens during the trip, the date moves one week (February 11 to 18, `A week passes.`), the fare (here $48) leaves the money, `Arrived in Merida.`, the run is on `ui/portview.lua` of Merida with `gTravelPrice == nil`; the train sound plays. The new user has rank 1, so a traveller encounter is possible (20 %): the test sets the `travel` building's `encounterOdds` to 0. The encounter path (PauseTravel / dialog / ResumeTravel) is only unit-tested (the pause freezes `elapsed`), not played through. Multi-leg trips, the airship and the sting file (`merida_sting.ogg`) are unit-tested through the command strings only, not run on the shipped Lua.

## Unknown

* Whether the engine-facing `bsutil.random(a, b)` is inclusive (affects `Price()` jitter and encounter odds by one step); the native is not part of the map's reading.
* The exact meaning of the leading `#` in the popup's first line (`"#" .. GetString(name)`).
* The sheet registration placement of the vehicles (regx/regy are in the XML; the exact draw offset applied by the draw call `00401ec0` is inference, as for `SimpleAnimation`), and whether dots are drawn centred or by their top-left corner (inference: centred, like stills).
* The `WindowAnim` mechanism's effect on `PauseTravel` is read from code (owner pointer cleared) and from the Lua usage; not observed in a running game.
* What the `command` of the ledger's `map` button does on the map screen itself (`ui/ledger.lua:48-51`), and whether any ledger text changes during a trip.
* Behaviour when the window is destroyed during a trip (e.g. quit to menu): the destructor (`004172f0`) does not touch `DAT_005330a0`, so a mid-trip teardown would leave the gate raised (inference); the Rollover gate is therefore best reset by the engine on game exit.
* The file existence and content of `<port>_sting.ogg` (not checked in the Lua).
* Whether `FUN_0042d3e0`'s byte at `+0x27` (set on every update) is a redraw flag (inference only).
