# World map and travel (`MapWindow`, `TravelTask`, `TCurve`)

Sources, in order of trust: the decompiled Mac binary (`re/mac/decomp/MapWindow.c`, `TravelTask.c`, `TCurve.c`, `TOldScalingTask.c`, `TAnimTask.c`, `TPathSprite.c`, `Rollover.c`), the Chocolatier 3 Mac app bundle's `Contents/Frameworks/MapWindow.h`, `Curve.h`, `PathSprite.h`, `ScalingTask.h`, then the Lua in the original install's `assets/` (original) and the Reforged mod's `assets/` (mod). Addresses are the Ghidra addresses used in `re/mac/index.tsv`. Items marked **(inference)** are not directly visible in the code. Native signatures are also in `docs/re/map-window.tsv`.

Already covered elsewhere and not repeated: the window base class, `Rollover`/`PopupWindow` mechanics, `Transition` with `path` (`docs/re/window-system.md`, `docs/re/design-questions.md` sections 1 and 10, `docs/re/api-globals.md`), `Date()` (`docs/re/api-globals.tsv`).

## 1. Where it lives

* `ui/mapview.lua` does `MakeDialog { name="map", ..., MapWindow { ... }, devMenu() }`, then `SoundEvent("Stop_Environments")`, `SoundEvent("map_screen")`, `GrabLedger()`, `UpdateLedger("mapview")`. It is entered by `SwapToModal("ui/mapview.lua")` from `StoryMode` (`ui/mainmenu.lua`, when the tutorial quest `tut_01` is complete or active), `Player:LoadGameFromFile`, and `SwapMapPortScreens` in `ui/portview.lua`. It is left through `SwapMapPortScreens` (`ReleaseLedger(); SwapToModal("ui/portview.lua")`) or `Port:EnterPort`.
* `MapWindow` is not defined in `ui/style.lua`. It is a native DSL type registered with `AddWindowType` at startup (`docs/re/api-globals.md`, step 6). Class name string `"MapWindow"`, `ClassId` = `MapWindowDynamicCreate` (0x2a37c). The window is a plain `TWindow` subclass (members in `MapWindow.h`).
* Singleton: a static `pInstance` is set in the constructor and cleared in the destructor. `MapWindow::ToggleForeground()` (0x2a622) forwards to the instance (see section 9).

## 2. DSL keys (`MapWindow::Init`, 0x2ab68)

Order: `TWindow::Init`, then `FindParentModal(this)->SetDefaultFocus(this)`, then the keys below. The shipped call is `x=kScreenCenterX, y=kScreenCenterY, w=kScreenWidth, h=kScreenHeight, background="image/worldmap", yFar=80, yNear=400, farScale=.4, nearScale=1, cloudY=370, cloudTime=60000`.

| key | type | default | meaning |
|---|---|---|---|
| `background` | string | `""` | texture name, loaded with `TTexture::Get`. Becomes the root sprite (layer 0) at (0,0). `image/worldmap.jpg` is 800x600. |
| `cloudY` | number | 360 | y of the cloud strip sprite |
| `cloudTime` | number (ms, converted to uint, clamped to 0..2^32-1) | 20000 | time for the clouds to drift one window width |
| `yFar` | number | 0 | y at which the vehicle is at "far" scale |
| `yNear` | number | 400 | y at which the vehicle is at "near" scale |
| `farScale` | number | 1 | vehicle scale at `yFar` |
| `nearScale` | number | 1 | vehicle scale at `yNear` |

The four scale values are stored (this+0x50, +0x54, +0x58, +0x5c: `farY`, `nearY`, `farScale`, `nearScale`) and copied into the travel task when travel starts. The defaults and the shipped values were verified from the double constants in the code (360, 20000, 400).

Init also builds the cloud strip: a sprite from `image/worldmap_clouds` (800x172) at layer 11, y = `cloudY`, flags unchanged, plus two child sprites of the same texture at x = +texture width and x = -texture width. The cloud root becomes a child of the background sprite. The starting phase is random: `cloudTime counter = RandRange(0, cloudTime)`, then `UpdateClouds` once.

`PostChildrenInit` (0x2d12c): `TWindow::PostChildrenInit`, `CollectPortData()`, then `StartWindowAnimation(30, true, true, false)`: the window gets an animation task with a 30 ms interval.

## 3. What is drawn

`MapWindow::Draw` (0x2a3e8): opens a `TBegin2d`, draws the root background sprite with a `TDrawSpec` at (0,0), scale (1,1), then calls `TWindow::Draw` (which draws the Rollover children, which draw nothing), then closes. Everything visible is therefore one sprite tree rooted at the background sprite (`pPortRoot`):

| sprite | layer | source |
|---|---|---|
| background | 0 | `background` key |
| port marker (one per non-hidden port) | 2 | `TAnimatedSprite`, texture by state, see section 4 |
| port label (two text sprites) | not decoded | `fonts/fertigo.mvec`, size 20 |
| "you are here" ring | 0 | `image/port_current` (32x32) at the current port, added to the root |
| vehicle | 10 | `TAnimatedSprite`, created by `InitiateTravel` |
| trail dots | 1 | `image/traveldot` (8x8), one per 12 px of path travelled |
| clouds | 11 | strip described in section 2 |

Layer is the first argument of `TSprite::Create`/`TAnimatedSprite::Create`. `TSprite` has `SetLayer` and `ResortSprites`, so children are ordered by layer **(inference: ascending layer, later ones on top; the sort itself was not read)**.

Cloud motion: the window animation tick `OnTaskAnimate` (0x2a512) adds a fixed 30 to the cloud time counter on every call (it does not measure elapsed time), calls `UpdateClouds`, invalidates the screen and returns true. `UpdateClouds` (0x2a490): `counter = counter mod cloudTime; cloud.x = counter / cloudTime * (window right - window left)`. With the shipped values, 800 px per 60 s, but only if the task really runs every 30 ms. The tile at -800 covers the left edge. The clouds keep moving while travel is paused; only a lost application focus stops them (section 9).

The map has no weather and no random-event rendering in native code. All "events" during a trip are Lua callbacks (section 7).

## 4. Port markers, labels and rollovers (`CollectPortData`, 0x2b77e)

Called once from `PostChildrenInit`. Reads the Lua globals `Player` (`Player.portsAvailable`, `Player.portName`) and `_AllPorts`, iterates `_AllPorts` with the table iterator (Lua `pairs` order, unspecified), and for each port table `p`:

1. `state = Player.portsAvailable[p.name]` (string, `""` if missing).
2. **If `state == "hidden"`: the port gets no marker, no label and no rollover** (nothing else is done for it besides the current-port step below).
3. Otherwise a `PortData{name, sprite}` is pushed on the window's list and a marker `TAnimatedSprite` (layer 2) is created at `(p.mapx, p.mapy, 0)` (both default 0). If `p.name == Player.portName` that sprite is remembered as `pCurrentPort` (this+0x38).
4. Marker texture by state:

| `portsAvailable` value | texture | animated |
|---|---|---|
| `open` (visited) | `image/port_visited` (32x32 png) | no |
| `new` (available, never visited) | `image/port_new.xml` (2 frames, 16 px and 12 px, `DoAnim`: 500 ms per frame, 1,2 loop) | yes: `Play("DoAnim")` |
| `factory` | `image/factory.xml` (2 frames 32x32, 250 ms per frame) | yes |
| `factory_stall` | `image/factory_idle.xml` | yes |
| `shop` | `image/icon_shop` | no |
| anything else (`locked`, anything unknown) | `image/port_locked.xml` (frame 1, 17x20) | **no** (no `Play`; shows the first frame) |

   So "locked" is the fallback branch. The Lua states are `new`, `open`, `locked`, `hidden`, `factory`, `factory_stall`, `shop` (`sim/player.lua` line 22). `Port:IsAvailable()` treats everything except `locked` and `hidden` as available.
5. Label: `align = p.align` (`""` default). If `p.mapx >= 750` or `align == "left"`, the label sits to the left of the marker at x = mapx - 10 with flags 6 (kHAlignRight + kVAlignCenter); otherwise to the right at x = mapx + 10 with flags 4 (left, vcenter). y = mapy. Two `TTextSprite`s are made with `TTextSprite::Create(200, 20, flags, "fonts/fertigo.mvec", 20, colour, layer)`: a **black** one (0,0,0,1) with **layer 3** and a **white** one (1,1,1,1) with **layer -1**. The last argument is the sprite *layer* (see `game-windows-questions.md` 2); there is no outline parameter. The text is `StringTable::GetString(p.name)` for both. The white sprite is a child of the black one, at position (X+2, Y+2, 0.0) with its **alpha (+0x5c) set to 0.75**; the black sprite goes onto the root. Because both positions have a third component of 0.0, a child's translation does not include the parent's translation, so the white copy lands at an absolute offset of (+2,+2) from the black one. Child layer -1 means the white copy is drawn **first, underneath** the black text: the visible result is black text with a 75 %-opaque white shadow to the lower right. The box anchor: flags 4 give origin (0,10) and flags 6 give origin (200,10) in the 200x20 box, so the text starts at mapx+10 (left-aligned) or ends at mapx-10 (right-aligned), vertically centred on mapy.
6. Rollover: rectangle `(mapx-17, mapy-17, mapx+17, mapy+17)` truncated to ints (a 34 px square). `new Rollover(rect, contents = name .. ":MapRolloverPopup()", clickString = name .. ":OnClick()")`. `SetPopupPosition(popupX, popupY)` with `popupX = p.popupX` default `int(mapx) + 10` and `popupY = p.popupY` default `int(mapy) - 48`; these become the Rollover's `xAbsolute`/`yAbsolute`, i.e. absolute screen coordinates for the popup's top-left (subject to `PopupWindow::Reposition`, a 4 px screen margin). The Rollover is added with the virtual AdoptChild (`vtable+0x24`, callInit false), so ports processed later are in front of earlier ones. **Locked ports also get a rollover and a click string**; the Lua decides what they do.
7. After every iteration (also for hidden ports, and so repeatedly) if `pCurrentPort` is already set, a `image/port_current` sprite (layer 0) is created at the current port's position and added to the root. **(Inference: because the check is inside the loop, ports iterated after the current one add duplicate rings at the same place. Harmless visually.)**

Per-port Lua data read: `name`, `mapx`, `mapy`, `align`, `popupX`, `popupY`. Examples: `zurich.mapx=400, mapy=50`; `wellington.align="left", popupX=526, popupY=310`; `bali.popupX=398`; `gobidesert.popupX=370`. Hidden/locked flags come from `port.hidden`, `port.locked` through `Player:Reset` (`sim/player.lua` 177-185): `hidden` -> `"hidden"`, else `locked` -> `"locked"`, else `"new"`. `Port:Unlock()` turns `locked` or `hidden` into `new`. `ui/portview.lua` sets `portsVisited[name]=true` and `portsAvailable[name]="open"` on entry (overwriting `factory`/`shop`; those are re-set by `sim/factory.lua`/`sim/shop.lua`/`Player` code when owned).

The marker states are fixed at window creation. Nothing in the native class refreshes them, so a state change takes effect when the map is rebuilt (`SwapToModal("ui/mapview.lua")`).

## 5. Mouse interaction on the map

The `MapWindow` itself has no mouse handlers (`OnKeyDown` returns false, 0x2a3bc). All interaction is through the per-port `Rollover` windows (`docs/re/design-questions.md` section 10):

1. Mouse moves into a marker's 34 px square: `Rollover::OnMouseMove` (acts only if the static `rolloversEnabled` is true) creates a `PopupWindow`, adopts it into the top modal, runs the `contents` string `"<port>:MapRolloverPopup()"` through `DoLuaString` with the popup as current window, then fits it to its children, fades it in over 125 ms and positions it at (`popupX`, `popupY`).
2. `Port:MapRolloverPopup()` (`sim/port.lua`) returns `MakeDialog { Bitmap { image="image/traveltag" (250x75, fit=true), Text{name}, Text{info} } }`. If `gTravelActive` it returns nil, so the popup is empty during a trip. Layout: name text `x=0,y=2,w=kMax,h=25, font=portNameRolloverFont (18 pt, grey 208), centered`; info text `x=24,y=25,w=201,h=40, font=rolloverInfoFont, top, centered`. Info text lines, joined with newline and prefixed `#`:
   * unavailable port: `trip_locked` ("This port is not yet available.").
   * current port: `trip_here` ("You are here.").
   * otherwise: `trip_time_single` ("This trip will take one week") when `route.time == 1`, else `trip_time` ("This trip will take %1% weeks") with the number; then `trip_free` ("on your private plane.") if the cost is 0, else `trip_cost` ("and cost %1%.", with `Dollars(cost)`) and, if `Player.money < cost`, `trip_expensive` ("You can't afford this."). The route used is `Player:GetPort():GetRoute(port)`; the cost is `Player.portsCost[name]` (0 with `questVariables.ownplane == 1`).
3. Click: `PopupWindow::OnMouseUp` (the popup is a mouse listener, so it sees the release wherever the pointer is while the popup lives) kills the popup and evaluates the click string `"<port>:OnClick()"` with `DoLuaString`. The popup dies when the pointer leaves the rollover rectangle (`TPopupTestRect::KeepVisible`). So the pointer must stay inside the 34 px square to keep the popup alive, and the click is "mouse up while the popup exists".
4. `Port:OnClick()` (original, `sim/port.lua` 147-335):
   * `gTravelActive` -> return.
   * `self.name == Player.portName` -> `self:EnterPort()` (no animation; `Player:SetPort`, `ReleaseLedger()`, `SwapToModal("ui/portview.lua")`, `SoundEvent(cadikey)`).
   * `IsAvailable()` and `Player.money >= cost`: travel starts (section 6). If money is short, nothing happens (the source has a TODO for a warning). There is **no confirmation dialog**: one click starts the trip and takes the money.
   * Locked or hidden port: nothing.
   There is no keyboard control on the map.

`gTravelActive` is a Lua global (set true in `OnClick`, false in the arrival event); nothing native reads it.

## 6. Routes, curves and starting a trip

### 6.1 Route definition (Lua)

`ports/portroutes.lua` exists only in the Reforged assets (the original file is not in the original install's `assets/`; `ui/mainloop.lua` does `dofile("ports/portroutes.lua")` after `LoadPorts`/`LoadQuests`). It is 190 lines of `src:DefineRoute { dest }` or `src:DefineRoute { dest, via=waypointPort }`. None of them carries `time`, `type` or `path`.

`Port:DefineRoute(t)` (`sim/port.lua` 388-409): `destination = t[1]` (name or port table), `route = { time = t.time or 1, type = t.type or "travel_air", via = t.via, path = t.path }`; a string `type` becomes `{ {0, type} }`; with neither `path` nor `via`, the default path is the four points `{src}, {src}, {dst}, {dst}` using `mapx, mapy` (a straight line expressed as a cubic Bezier with doubled end points). Stored in `self.routes[destination.name]`.

`Port:GetRoute(dest)` (lazy and cached):
1. `questVariables.ownplane == 1`: returns a one-week straight route (not cached).
2. Else `self.routes[dest.name]`. If missing: take the reverse route `dest:GetRoute(self)`; if one exists, copy `time`, reverse the point list, and reverse the type list with thresholds flipped (`1 - previousThreshold`, with the first reversed entry at 0). If nothing exists at all: a one-week straight route with `type = {0, "travel_air"}` (a flat list, so it carries no type entries and falls back to `travel_air`). The result is cached.
3. If the route has `via`: on first use, `MergeRoutes(t, self:GetRoute(via), via:GetRoute(dest))`: `time` = sum, `type` = the first leg's type, `path` = first leg's points followed by the second leg's points from index 2 on (so the shared waypoint appears once), `via` cleared, cached.

So a one-leg route has 4 control points (1 cubic segment), a `via` route has 7 (2 segments). All legs in the shipped data are `time = 1`, so a trip takes 1 week per leg: 1 or 2 weeks (a `via` of a `via` route would give more; the data nests at most the way `GetRoute` recurses, which I did not enumerate).

### 6.2 `TCurve` (Curve.h; `TCurve.c`)

A curve is a vector of `TVec2` control points forming a chain of cubic Beziers: point count 3n+1, segment k uses points 3k..3k+3. Construction from Lua (`TCurve::BuildCurve`, 0x1d204): iterate the table in `GetNext` order and take each element that is a table as a point `{x, y}` (index 1 and 2, default 0); non-table elements are skipped. Any change to a control point clears the cached subdivision.

Arc-length table (`DoSubdivision`, 0x1c912): for segment k (`i = 0, 3, 6 ...` while `i < size - 3`) run `Subdivide(0, 1, p0, p1, p2, p3)` (0x1c578):
* compute the chord length `|p3-p0|` and the control-polygon length `|p1-p0| + |p2-p1| + |p3-p2|`;
* if `polygon - chord <= 0.2` (`_ACCEPTABLE_ERROR`, a float at 0x1d80e0, value read from the binary = 0.2): emit one `Subdivision{tStart, tEnd, segmentLength = polygon length, totalLength (filled later), start = p0, end = p3}`;
* else split with de Casteljau at the midpoint and recurse on both halves, with the t range split in half.
Afterwards a running sum fills `totalLength` (cumulative) and `length` = the sum of all segment lengths. Note `segmentLength` is the control-polygon length, not the chord, of each piece.

`GetLength()` (0x1cd0e) subdivides lazily if there are at least 4 points and returns `length`; fewer than 4 points gives 0.

`GetPoint(normalLength, pTangent)` (0x1cd46): `s = normalLength`. If `s >= 1`: returns `GetEndPoint()` = control point at index `((size-1)/3)*3`, tangent not written. If `s <= 0` (and not NaN): returns point 0, tangent not written. Else `d = s * length`; walk the subdivisions: if `d <= segmentLength` the point is `start + (end - start) * f` and the tangent is the normalized `(end - start)` of that piece; otherwise `d -= segmentLength` and continue. The decompiler dropped the scalar `f` and the tangent subtraction order; the obvious reading is `f = d / segmentLength` (linear interpolation along the chord of the piece) and tangent = `end - start` **(inference)**. The speed along the curve is therefore uniform in arc length, independent of the Bezier parameter.

Straight default route `{A,A,B,B}`: polygon length equals the chord, so it is exactly one piece of length `|AB|`.

`TCurve::GetEndPoint`, `GetControlPoint(i)` (index taken modulo size), `FindControlPoint`, `SetControlPoint`, `AddControlPoint`, `BuildExportString` (`"{x,y},"` with ints), `RenderCurve`/`RenderHull`/`RenderControlPoints` exist for the editor (`TCurveEditor`) and are not used by the map.

### 6.3 `InitiateTravel(t)` (0x2b31c)

Registered global, called by `Port:OnClick` as `InitiateTravel { dest=dest, time=route.time, type=route.type, path=route.path, events=events }`. Native steps, in order:

1. If `pTravelTask` (this+0x6c) is non-null, return. **It is never reset to null**, neither on arrival nor in the destructor, so a second `InitiateTravel` on the same `MapWindow` object is ignored. This does not matter in play because arrival swaps to the port view, which destroys the map window.
2. Curve: if `t.path` is a table, `TCurve::Create(path)`. Otherwise use `t.dest` (a table with `mapx`, `mapy`) and build a curve `(cur, cur, dest, dest)` where `cur` is the current-port marker position. (The shipped Lua always supplies `path`; the `dest` branch is the fallback.)
3. Create a `TAnimatedSprite` (layer 10), add it as a child of the root sprite, and copy the current-port marker's position (x, y, z) into it. This dereferences `pCurrentPort`, so the map must have been built while the player was in a port (the normal case; `Player:SetPort(nil)` is called just before `InitiateTravel`, but the window was built earlier).
4. `new TravelTask(sprite, curve, t.type)` (100 bytes). Copy `farY, nearY, farScale, nearScale` from the window into the task (task +0x3c, +0x40, +0x44, +0x48).
5. `t.events`: for `i = 1, 2, ...` while `events[i]` is a table: `AddEvent(events[i].time (number, default 0), events[i].action (function))`. The native does **not** sort; the Lua sorts by time beforehand with `table.sort`. `events[i].time` is in milliseconds.
6. `TPlatform::AdoptTask(task)`, `task->SetDelay(30, true, true, false)` (interval 30 ms, repeating, from now), `travelPaused = false`.

The `time` and `dest` keys of the table are ignored when a `path` is given; the duration is derived solely from the events.

## 7. `TravelTask` (ctor 00171214, `Animate` 00171544)

Class: `TravelTask : TOldScalingTask : TAnimTask`. Fields: +0x28 vehicle sprite, +0x30 curve, +0x38 `duration` (float), +0x3c `yFar`, +0x40 `yNear`, +0x44 `farScale`, +0x48 `nearScale`, +0x4c list of `TravelType{float fraction; str mode}` (node: fraction at +8, name at +0xc), +0x54 cursor into that list, +0x58 list of events (`TravelEvent{float time; TLuaFunction* action}`), +0x60 event cursor. Base fields: +0x18 `time` (ms, float), +0x1c `lastTime`, +0x20 `scale` (1.0), +0x24 `lastClock`.

**Constructor.** Vehicle sprite hidden; duration 0; scale values 0,0,1,1 until the window overwrites them. `type` table (may be nil): for `i = 1 ..` while `i <= size`: take `type[i]` as a table, `fraction = type[i][1]` (default 0), `mode = type[i][2]` (string, default `"travel_air"`), append. Entries that are not tables (such as the flat `{0,"travel_air"}` default) are skipped. If the list is empty the mode is `"travel_air"`; otherwise the first entry is applied at once (regardless of its fraction) and the cursor moves to the second.

**SetMode(name)** (0x70fa4): if `name` has no `/`, prefix `image/`; if it has no `.`, append `.xml`; load that animated texture and set it on the vehicle sprite. `travel_air` becomes `image/travel_air.xml`: 16 frames (`travel_air.jpg` 494x220 with `travel_air.mask.png`, frames about 75-89 x 63-74, regs per frame in the xml). Only `travel_air` exists in the shipped assets.

**AddEvent(time, fn)** (0x70c24): if `time > duration` then `duration = time`; append to the event list; reset the event cursor to the head. The trip length is therefore the largest event time. The Lua arrival event is at `weeks * 3000`.

**Timing.** `TAnimTask` (base): +4 ready flag, +8 next-run time (-1 = paused), +0xc interval, +0x10 re-arm-from-now flag, +0x11 catch-up flag (`SetDelay(delay, repeat, fromNow, catchUp)`: `repeat` only decides whether the interval is `delay` or 0), +0x14 clock (null = platform timer). `SetDelay(30, true, true, false)` makes interval 30, repeat = true, catch-up = false. `DoTask` (0x813fe, re-read from the disassembly): if `now - next > 1000` it first sets next = clock now (drops the backlog); then, if the re-arm-from-now flag is set, next = now + interval, else next += interval; calls `Animate()`; with the catch-up flag it repeats while the stale `now` is later than the new next. With from-now (every shipped caller) there is therefore no catch-up. Return value false ends and removes the task. `Pause()` (0x81262) sets next = -1 and ready = 0.

`TOldScalingTask::Animate` (0x347da): `lastTime = time; time += (now - lastClock) * scale; lastClock = now`, with `scale = 1`, so `time` is wall-clock milliseconds since the baseline, advanced in ~30 ms steps. `Restart(delay)` (0x34830) sets `lastClock = now` and calls `SetDelay(delay, true, true, false)`. **`ResumeTravel` calls this virtual with 30**, so the time spent paused is discarded exactly. `GetT(full)` = fractional part of `time / full`.

**`TravelTask::Animate` (00171544), per tick, in order:**
1. `oldT = GetT(duration)` (before updating the clock).
2. `TOldScalingTask::Animate()` (advance `time`).
3. `t = (time < duration) ? GetT(duration) : 1.0`. This is the path fraction in [0,1].
4. Trail dots: `len = curve.GetLength()`. `oldCount = floor(oldT * len) / 12` (integer division, clamped at 0), `newCount = floor(t * len) / 12`. For each index from `oldCount` up to `newCount - 1`: a `image/traveldot` sprite (layer 1) is placed at `curve.GetPoint(index*12 / len)` (the argument is lost in the decompile; this is the natural reading, **inference**), z = 1.0, and added as a child of the vehicle sprite's parent (the root map sprite). **A dot is dropped every 12 map pixels of arc length.** Dots are never removed.
5. Vehicle mode switching: while the type cursor is not at the end and `cursor.fraction <= t`: `SetMode(cursor.mode)`, advance. (So `type = {{0,"travel_air"},{0.5,"travel_boat"}}` would swap the sprite at the half way point.)
6. Position and heading: `(pos, tangent) = curve.GetPoint(t)`. Heading angle: `a = 180 - degrees(acos(tangent.y))`; if `tangent.x < 0` then `a = 360 - a`. This gives 0 degrees = moving up the screen (y decreasing), 90 = right, 180 = down, 270 = left. `n = frame count`; `frame = floor(a / (360/n) + 0.5) mod n`; `SetCurrentFrame(frame)`. The vehicle sheet is therefore a rotation sheet with frame 0 = north and frames going clockwise **(inference for the clockwise ordering and which way frame 0 faces; the frame order in the png was not checked)**. For `travel_air.xml`, n = 16, 22.5 degrees per frame. The sprite matrix is set to a rotation by the left-over angle `a - frame*(360/n)`.
7. Perspective scale: if `farY != nearY` (i.e. if `yFar != yNear`): `s = (pos.y - farY) / (nearY - farY) * (nearScale - farScale) + farScale`, not clamped; the sprite matrix is reset to identity and then scaled by `s` (this **discards the residual rotation of step 6**, so with the shipped `yFar=80, yNear=400, farScale=.4, nearScale=1` the heading is quantized to the 16 frames). If `farY == nearY` the residual rotation stays and no scaling is applied.
8. Sprite position = (pos.x, pos.y, 1.0).
9. Events: while the cursor has not reached the end and (`event.time <= time` **or** `t == 1.0`): `Trigger()` the event and advance. Events fire in list order; the first whose time is still in the future stops the scan (so unsorted lists would delay later events). On the final tick every remaining event fires.
10. Make the vehicle visible (it is invisible until the first completed tick). Return `t < 1.0`; false ends the task.

Edge facts: when `t` is exactly 0 or 1 the curve's `GetPoint` does not write the tangent, which stays at (0,0) (default-constructed **(inference)**), so the frame computed on those ticks is the east-facing one (`acos(0)` = 90 degrees). On the arrival tick the window is being torn down anyway.

**Event dispatch** (`TravelEvent::Trigger`, 0x704f4): pushes the Lua function and calls virtual +0x14 on the script object. In `TScript`'s vtable, +0x10 is `Animate` and the next slot is `InjectFunction`, so this is **(inference)** `InjectFunction`: the action runs inside the `Main` coroutine as if it were a button command (the mechanism in `window-system.md`: resume with `(nil, closure)`). That explains why the Lua actions can open dialogs (`travelers:OnClick()`, `Player:AwardMedal`, `quest:Offer()`) and wait. If the action blocks in a modal, `Trigger` returns when the coroutine next yields, and `Animate` goes on with the rest of the tick. If the function is null (`TLuaFunction*` missing), nothing happens.

## 8. Pause, resume and arrival

* `PauseTravel` (0x2a3c4): if a task exists, `TAnimTask::Pause` and `travelPaused = true`. The vehicle freezes where it is. The window animation (clouds) keeps running.
* `ResumeTravel` (0x2a390): if a task exists, `Restart(30)` (virtual +0x18 on the task) and `travelPaused = false`.
* Both are no-ops without a task and are plain globals set in the constructor, bound to this window, and set to **nil** in the destructor (0x2a63c). Lua code outside the map must therefore guard (`ui/ui_pause.lua`: `if PauseTravel then PauseTravel() end`; ledger buttons guard with `gTravelActive`).
* Who calls them in the original Lua: `ui/ledger.lua` wraps each of the inventory, recipes, quest log, pause and medals buttons: `if gTravelActive then PauseTravel() end; DisplayDialog{...}; if gTravelActive then ResumeTravel() end`. The map/port toggle button does nothing while `gTravelActive`. `ui/ui_pause.lua` calls `Player:SaveGame()`, `PauseTravel()` on open, and `ResumeTravel()` in its resume handler; it disables the main-menu button while `gTravelActive`. `AskQuit` (`ui/mainloop.lua`) refuses while `gTravelActive`. Event actions wrap their dialogs in `PauseTravel()`/`ResumeTravel()` themselves.
* **Arrival**: there is no native arrival callback or message. The Lua appends an event at `time = duration = weeks*3000` whose action is `SoundEvent("Stop_travel_plane"); dest:EnterPort(true); gTravelActive = false`. `Port:EnterPort(sting)`: `Player:SetPort(self.name)` (also sets `destination=nil`, runs `PrepareIngredientPrices`, `PrepareProductPrices`, `PrepareTravelPrices`), `ReleaseLedger()`, `SwapToModal("ui/portview.lua")`, then `SoundEvent(cadikey.."_sting")` and `SoundEvent(cadikey)` (just `cadikey` without a sting). The port view marks the port visited (`portsVisited`, `portsAvailable = "open"`, `portVisitCount + 1`). The task returns false on the same tick (`t == 1.0`), so it ends and the map window is destroyed by the swap (clearing the three globals).
* The `TravelTask` is never freed by the window and `pTravelTask` is never nulled. **(Inference: the platform's task list frees finished tasks; nothing in `MapWindow` does.)** Calling `PauseTravel`/`ResumeTravel` after the task is gone but with the window alive would touch a dangling pointer; the Lua never does because arrival destroys the window.

## 9. Application focus (`WindowToggleForeground`, 0x2a53c)

Called through `MapWindow::ToggleForeground()` when the application changes foreground state (event type 0x12 in `window-system.md`).
* Lost foreground: `StopWindowAnimation()` (clouds stop). If the travel is not paused and this window is the top modal, run the Lua string `if not gUIPaused then DoModal('ui/ui_pause.lua') end` (which pauses travel and saves). The travel task itself is not paused directly.
* Regained foreground: `StartWindowAnimation(30, true, true, false)`; if not paused and a task exists, `Restart(30)` then `travelPaused = false`.

## 10. Travel time, the date and the simulation

* One game week of travel = **3000 ms** of unpaused travel time (`duration = weeks * 3000`). `weeks = route.time or 1`.
* `TickSim(1)` (`sim/sim.lua`): `Player.time = Player.time + 1` (weeks), `subticks = 0`, `AnimateProduction()`, `Player:RunFactories()`, `ExpireInventory()`, `UpdateSupplies()`, `UpdateLedger("all")`. The date shown is `Date(Player.time)`: `days = weeks*7 + 171`, year `1946 + days/365` (see `api-globals.tsv`).
* In `Port:OnClick`, one event per week `i = 1..weeks` at `time = (i-1)*3000 + 1` ms runs `PauseTravel(); TickSim(1); ResumeTravel()`. So the first week is advanced essentially at departure (1 ms), the second at 3001 ms, and so on; arrival does not tick. A one-week trip: tick at 1 ms, arrival at 3000 ms. The Reforged file keeps this and also calls `HighScoreModel:RecordTravel(Player, weeks)`.
* Cost: `Player:SubtractMoney(cost)` at click; `Player.destination = self.name`. Prices (`PrepareTravelPrices`, run on every `SetPort`): `base = min(10000, floor(450 * 1.1^floor(Player.time/13)))`; per port `price = floor(route.time*base * RandRange(900,1000)/1000 + 0.5)`, and for `route.time > 1` multiplied by `(1 - route.time*0.1)` and rounded. Free with `questVariables.ownplane == 1`.

### Events scheduled in the original `Port:OnClick`

All are Lua; the native side only runs them. Each of these (except the ticks and arrival) is wrapped as `SoundEvent("Stop_travel_plane"); PauseTravel(); <work>; ResumeTravel(); SoundEvent("travel_plane")`. `SoundEvent("travel_plane")` also plays at departure.
1. Encounter (only if `Player.rank > 1`): if `encounterTimer <= 1`, force one and set it to 0, else decrement and force with 12% (`RandRange(1,100) <= 12`; the comment says 13%). Forced as well when a traveler quest can be completed, when a priority-1 traveler quest is startable, or when an active quest has expired. On an encounter `encounterTimer = 10`, and the action is `travelers:HandleQuestExpiration()` or else `travelers:OnClick()`.
2. Else, if no encounter, `Player:CheckMedals()` result: award the medal.
3. Else a random delivery order: `minOrderWeeks = 20 - (rank-2)*4.5`, `maxOrderWeeks = minOrderWeeks + 10`; triggered if `time - lastOfferTime >= maxOrderWeeks` or `time - lastOrderTime >= 26`, or (above `minOrderWeeks`) with 15%; the offering shop is a random owned one of `bag_shop, bog_shop, dou_shop, hav_shop, kon_shop, lim_shop, tan_shop`.
4. Else any eligible "telegram" quest in `_NoStarterQuests`: offered.
Encounter, medal, order and telegram events are placed at `duration * (RandRange(1,50) + 25) / 100`, i.e. between 26% and 75% of the trip (not "the middle half" exactly). Reforged instead schedules per week at `(i-1)*3000 + RandRange(1000,2500)` and adds quest-hint and tutorial-tip events.

## 11. State: Lua versus native

| Lua | native |
|---|---|
| `gTravelActive` (bool, runtime only, never saved) | none |
| `Player.portName` (nil in flight; `SetPort(nil)` before `InitiateTravel`), `Player.destination` (name while in flight), `Player.portsAvailable`, `portsVisited`, `portsCost`, `encounterTimer`, `time`, `money` | none |
| route, event list and closures | copied into a `TravelTask` |
| nothing | vehicle sprite, trail dots, elapsed time in the task, event cursor, pause flag, cloud phase |

The native window holds the only copy of the journey progress; it is not exposed to Lua (no getter).

### Save and load mid-journey

* Nothing about the journey fraction, elapsed time, event schedule or vehicle is saved. `Player:BuildSaveGameString` (original `sim/player.lua` 298-314) serializes every field of `Player` with a generic serializer (strings, numbers, booleans, tables; nils omitted), so during a trip it contains `destination="<port>"` and no `portName`, plus the already-deducted `money` and already-advanced `time` (ticks happen at the start of each week).
* A save can occur mid-trip because `ui/ui_pause.lua` calls `Player:SaveGame()` when opened and the pause dialog is available during travel (and `AutoSave` every 4 minutes, if called).
* `Player:Reset(t)` (101-112): `self.portName = t.portName or t.destination` (a stored `"error"` becomes nil), then `self.destination = nil`. **A game saved in flight is therefore restored with the player already standing in the destination port**; the rest of the trip, remaining weekly ticks, and any pending event are lost. `portsCost` is restored from the save (computed from the origin port), so the destination's travel prices are stale until the next `SetPort`; `PrepareIngredientPrices`/`PrepareProductPrices` are also not re-run on this path **(inference from the code path: `Reset` only calls `SetPort("zurich")` when there is no `portName`)**. After a load the main menu enters `SwapToModal("ui/mapview.lua")` or `ui/portview.lua`; `gTravelActive` is a fresh global (nil/false), so the map is idle.
* The original `mapview.lua` guard `Player.portName ~= "enroute"` is dead code: nothing assigns `"enroute"` (only Reforged's `SetPort` mentions it). Reforged adds a `gTravelActive` guard in `SwapMapPortScreens` and carries the same `portName or destination` fallback (`sim/player.lua` 646, 727).

## 12. `TPathSprite` (PathSprite.h, 0x2ded2 / 0x2e0fe)

Not used by the world map. It is used by `PortWindow` for animated scenery (cars, boats, cable cars), driven by keys in the port Lua tables: `path` (points, same as above), `time` (duration in ms), `motion` (`"oneway"` -> 0, `"loop"` default -> 2, `"bounce"` -> 3; mode 1 `PAUSE` is not reachable from that string), `yFar`, `yNear`, `scaleFar`, `scaleNear`, `scale`, `speedFar`, `speedNear`. Recorded here only because `PathSprite.h` was named:
* `Update(dt)`: `time += speed * dt`, where `speed = speedFar` if `speedFar == speedNear`, else it is interpolated by the sprite's y between `yFar` and `yNear`. Fractions: ONEWAY and PAUSE stop at the end point (`time >= duration`); ONEWAY additionally calls the parent's removal virtual (+0x14 on the parent) **(inference: removes itself)**. Otherwise `frac = fmod(time/duration + offset, 1)` for LOOP, and `fmod(..., 2)` for BOUNCE (the post-processing that folds [1,2) back was lost in the decompile).
* Heading frame: same `acos` formula as `TravelTask` when the sprite has more than one frame; it also sets the frame on the optional mask sprite (+0xa0). Scale = `scaleFar + (y - yFar)/(yNear - yFar) * (scaleNear - scaleFar)` when scaling is set, the matrix is identity-then-scale (rotation discarded, same as the travel vehicle).

## Unknown

* The scalar and operand order inside `TCurve::GetPoint` (linear `d/segmentLength` and tangent `end - start` are inferences from what survived), and the argument of the `GetPoint` call that places trail dots (`index*12/len` inferred).
* Whether `TravelEvent::Trigger` really calls `InjectFunction` (virtual slot +0x14 identified by position in the vtable, not by reading the table).
* Who frees the finished `TravelTask`; whether `TPlatform::AdoptTask` takes ownership.
* The original `ports/portroutes.lua` (it is not in the original install's `assets/`; the Reforged copy may differ in times and `via` choices from the 2008 data, and the original might have had explicit `time`, `type`, `path`). The type names beyond `travel_air` (other vehicle sprites) are not present in the assets.
* The `BOUNCE` fold in `TPathSprite::Update` and the exact catch-up behaviour of `TAnimTask::DoTask` under heavy lag (derived from a partly garbled decompile).
* The `[clone .eh]` duplicate functions in `MapWindow.c` and `TravelTask.c` are unusable garbage (exception-table fragments); nothing was taken from them.
