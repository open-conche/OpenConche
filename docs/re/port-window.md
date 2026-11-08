# Port screen, Character, Popup, PathSprite and the animation script

Scope: how the Mac binary (the Chocolatier 3 Mac app bundle's `Contents/MacOS/chocolatier-decadence-design`, decompiled in `re/mac/decomp/`) implements the port view (`PortWindow`), `CharWindow` (`Character`), `PopupWindow`, `TPathSprite`, and the per-sprite animation script (`animationscript.lua`). Addresses are Ghidra/Mac addresses (same as `objdump` addresses of the i386 Mach-O). The native list is in `docs/re/port-window.tsv`.

Read first, not repeated here: `window-system.md` (window tree, event dispatch, Lua DSL), `api-windows.md` / `design-questions.md` 7 and 10 (BSGWindow/CharWindow keys, `Rollover` -> `PopupWindow` flow), `startup-and-assets.md` section 2 and 3 (port file discovery, asset probing, sprite-sheet XML outline).

Conventions: "facts" come from the decompile or disassembly. "Inference" marks anything I derived but did not see directly. Times are milliseconds. `RandRange(a,b)` (0x317c) is inclusive: `a + floor((b-a+1) * u)`, u in [0,1).

---

## 0. Headline findings

1. `PortWindow` has **no** `AddBuilding`/`AddSprite`/`SetBuildingState` natives. It registers exactly one Lua function (`PausePortAnimations`). All scene data comes from the style key `port`, which is the **Lua `Port` table itself** (`port = _AllPorts[portName]` in `ui/portview.lua`). The window reads that table once, in `PostChildrenInit`, with `TLuaTable::GetString/GetNumber/GetTable`. Later changes to the table do nothing.
2. Click dispatch to Lua is a **string eval**: on mouse-up over a building the engine runs `gCurrentBuilding=nil;<buildingName>:OnClick()` on the main script. Hover has no Lua callback at all; it is purely C++ (a highlight sprite and an optional label window).
3. The port view does **not** use `PopupWindow`. Port hover uses a hidden "label" child window per building (built by `portview.lua`) plus a pulsing highlight sprite. `PopupWindow` is used by `Rollover` windows (map port popups, ledger, market) and is covered in section 4.
4. Port animation runs from its own 50 ms `TAnimTask` (`PortAnimTask`), not from the window's `OnTaskAnimate`, so it keeps running while a dialog is open over the port. Every tick advances the sprites by a **fixed 50 ms**, not by elapsed time.
5. Building hit testing is the building texture's bounding rectangle (see 1.7: the alpha test is effectively disabled by passing threshold 0), and when several buildings overlap the one with the **highest `layer`** wins.
6. `Character` mood frames: frame 0 is the body, frames 1..3 are face overlays (displeased, neutral, pleased). Mood changes cross-fade over 200 ms.
7. The sprite-script `frames{...}` numbers are **1-based** and the delay is in milliseconds (default 16 until `delay(n)` is called).

---

## 1. PortWindow

### 1.1 Class and registration
* `PortWindow : TWindow` (not `TBSGWindow`). Type name string `"PortWindow"` (0x2eb02). Registered as a Lua window type by `ChocoThreeMain` (`AddWindowType`) with factory `PortWindowDynamicCreate` (0x2f440). Lua constructor `PortWindow{...}` is the generic generated one (nothing in `ui/style.lua`).
* Object layout (offsets from `this`, from the decompile; header names in brackets):
  `+0x30` explicitPause, `+0x34` pAnimTask, `+0x38` startTime, `+0x3c` lastTime, `+0x40` rolloverTime, `+0x44/+0x48` pPortRoot (background sprite + refcount), `+0x4c` musicTime, `+0x50` music flag, `+0x54` `buildings` (std::list), `+0x5c` targetBuilding iterator, `+0x60` mouseDown iterator, `+0x64` `masks` list (really: all static scenery sprites), `+0x6c` `sprites` list (path sprites).
* `BuildingInfo` (list node payload, offsets from node+8): `name` (str), `pSprite` (building sprite), `pRollover` (highlight child sprite), `pLabel` (`TWindow*`, may be null), `rolloverScale` (float).
* Static `pInstance` set in the ctor (0x2f3be), cleared in the dtor. The ctor sets `music = true`, `explicitPause = false`.

### 1.2 DSL keys
Base `TWindow` keys apply (x, y, w, h, name, ...). Own keys:

| key | read in | type | default | meaning |
|---|---|---|---|---|
| `background` | `Init` 0x2f47e | string | `""` | texture path for the root sprite (`TTexture::Get(path, 0)`; asset probing adds `.png/.jpg`). `ui/portview.lua` passes `"ports/<port>/<port>"` (a `.jpg`). |
| `port` | `PostChildrenInit` 0x32348 | table | none | the `Port` Lua table (see 1.3). |
| `musicDelay` | `PostChildrenInit` | number | **25000** (double 0x40d86a00 high word) | ms after creation until the first music toggle. Not set anywhere in the shipped Lua. |

`Init` also: creates root sprite `TSprite::Create(layer 0, texture)`; sets the sprite's origin vec2 (+0x44) to (0,0) and sets flag 8 in its flags (+0x70), which means the sprite's origin (+0x44) is used as the pivot instead of the centre, i.e. it is anchored at its **top-left** (flag 8 = "origin is explicit"; `TSprite::GetRect`, `TTexture::DrawSprite(TDrawSpec)`, `game-windows-questions.md` 5.1); makes this window the default focus of its parent modal; and registers the Lua global `PausePortAnimations` (see 1.9).

Children declared in the DSL (the `Group(labels)` windows and the banner) are ordinary child windows. In `portview.lua`: one `BSGWindow` per building named `b.name`, frame `controls/rollover`, containing a `TightText` with the localized building name; and `Bitmap{ image="image/banner", x=275, y=0, Text{...port name...} }`. The **port name banner is plain Lua DSL, not engine code** (font `labelFontName` 22, colour (208,208,208), text at y=12 in the Reforged file, y=17 in the original).

### 1.3 The `port` table: keys the engine reads

Port level (`CollectSpriteData` 0x2fa68, `CollectBuildingData` 0x31936):

| key | default | use |
|---|---|---|
| `name` | `""` | builds building texture paths `ports/<name>/<buildingName>` |
| `buildings` | none | array of building tables (iterated with `TLuaTable::GetNext`, array order) |
| `sprites` | none | array of sprite tables |
| `yFar` | 0 | default for path sprites |
| `yNear` | **800** | default for path sprites |
| `scaleFar` | 1 | default |
| `scaleNear` | 1 | default |

Building table (the `Building` Lua object; metatable keeps other fields, only these are read): `name`, `x`, `y` (default 0), `layer` (default 0, cast to int), `labelx`, `labely`. Other keys on the shipped buildings (`inventory`, `windowX/Y`, `popupX/Y`, `type`, ...) are for Lua only.

Sprite table (what `CreateSprite(name, port)` appends, plus fields assigned later in e.g. `ports/zurich/zurich.lua`). `CreateSprite("zur_boat1", zurich)` makes `_G.zur_boat1 = { image = "ports/zurich/zur_boat1" }` and appends it. `OldCreateSprite(port, t)` (table form) appends a user table as is.

| key | default | meaning |
|---|---|---|
| `frequency` | 100 | percent chance this sprite exists this time the port opens: kept if `RandRange(0,99) < frequency` (cast to uint). 100 = always, 0 = never. Rolled again every time the port window is created. |
| `images` | none | array of image paths; if non-empty one is picked with `RandRange(1, n)` (1-based) |
| `masks` | none | array parallel to `images`; element with the same index is the colour-mask image (only if index <= #masks) |
| `image`, `mask` | `""` | used when `images` yields nothing |
| `layer` | 0 | sprite z-order within the root (cast to uint) |
| `behind` | none | sub-table; if present its `layer` n replaces `layer` with `n-1` (0 if n is 0). Not used by any shipped port. |
| `path` | none | table of `{x,y}` control points; **presence selects the path-sprite branch** (1.5) |
| `time` | 0 | path duration in ms (at speed 1) |
| `motion` | `"loop"` | `"oneway"` -> ONEWAY(0), `"bounce"` -> BOUNCE(3), **anything else (incl. `"loop"`) -> LOOP(2)**. PAUSE(1) can never be selected from Lua. |
| `x`, `y` | 0, 0 | static sprites only: position of the sprite centre (or registration point for animated sheets). Ignored when `path` exists (clouds in zurich.lua set `x,y` and `path`; only the path is used). |
| `scale` | static: 1; path: -1 | static: uniform matrix scale. Path: if > 0, `SetScaling(yFar,yNear,scale,scale)` (overrides scaleFar/Near). `bal_wave1.scale = -1.0` therefore means "no override". |
| `yFar`, `yNear`, `scaleFar`, `scaleNear` | port values | per-sprite override of the perspective scaling (path sprites) |
| `speedFar`, `speedNear` | 1, 1 | perspective speed (path sprites) |
| `random` | true | path sprites: randomise the starting phase (1.5) |
| `htile` | 0 | if non-zero, tile the texture (1.5) |
| `tint` | (1,1,1,1) via `GetColor` | colour multiplier of the mask overlay |
| `tints` | none | array of colours; if present one is picked with `RandRange(1,n)` and used instead of `tint` |

Colours are Lua `Color(r,g,b,a)` objects read by `TLuaTable::GetColor`; shipped data uses 0..255 components, the `Color` global does the conversion (see `api-globals.md`).

### 1.4 Construction order (`PostChildrenInit` 0x32348)
1. `TWindow::PostChildrenInit` (children, including labels, exist now).
2. `port = style.GetTable("port")`; `CollectBuildingData(port)`; `CollectSpriteData(port)`. **Buildings are collected first**, so building sprites are children of the root before any scenery sprites. Order only matters for ties because children are sorted by layer (1.6).
3. Create `PortAnimTask` (0x1c bytes, 0x1721f8), `TPlatform::AdoptTask`, `SetDelay(50, repeat=true, fromNow=true, catchUp=true)`.
4. `TintBackground((0.25, 0.25, 0.25, 1))`.
5. `startTime = lastTime = pAnimTask->GetTime()` (platform timer, ms).
6. `musicTime = now + musicDelay` (default 25000).

`CollectBuildingData` (0x31936), per building, in array order:
* texture = `TTexture::Get("ports/" + port.name + "/" + building.name, 0)`.
* If the texture is missing: use `ports/temp_building` and lay it out on a debug grid: running cursor starts at (50,100). If `cursorX + 100 > 500` then place at the cursor, then `y += 120` and `x = 50`; else place at the cursor and `x += 100`, `y += 20`. (Overrides the building's own x,y.)
* `sprite = TSprite::Create(layer, texture)`, position `(x, y, 0)`, added to the root. Building sprites are anchored at their **centre** (no flag 8), so `x,y` are centre coordinates (zurich uses half-pixels like 534.5). Note the third component 0: a position stored with `w = 0` ignores the ancestors' translation (only their linear part applies), see `game-windows-questions.md` 2.2; harmless here because the root sits at the origin.
* Highlight child: a second sprite from the same texture with layer **-1**, `SetVisible(false)`, added as a child of the building sprite.
* `rolloverScale = min(1.2, (w+50)/w, (h+50)/h) - 1` (w,h = texture width/height), i.e. grows by about 50 px but never more than +20 percent.
* Label window = `GetChildWindow(building.name)` among the PortWindow's children (may be null). If it exists: default position is `labelx = x - labelWidth/2`, `labely = y + textureHeight/4`, each overridable by the building's `labelx`/`labely`. Then `labelx = max(labelx, 0)`; if `labelx + labelWidth > windowWidth` then `labelx = windowWidth - labelWidth`; no vertical clamp. `SetWindowPos(label, (int)labelx, (int)labely)`.
* A `BuildingInfo` is pushed on the list. At the end `targetBuilding = mouseDown = end()`.

### 1.5 Scenery sprites (`CollectSpriteData` 0x2fa68)
For each entry of `port.sprites`, in order:
1. Roll `frequency`; skip the whole entry on failure.
2. Pick `image`/`mask` (via `images`/`masks` or `image`/`mask`).
3. Compute `layer` (with `behind`).
4. **No `path`: static sprite.**
   * Image name contains `".xml"` (e.g. `ports/animations/wave1.xml`): `TAnimatedSprite::Create(layer)`, texture = `TAnimatedTexture::Get(image, "", "")`; if the sheet has more than one frame call `Play("DoAnim")` (the sheet's script, section 6). Otherwise `TTexture::Get(image)` and a plain `TSprite`.
   * `TMat3::Scale(scale)`, position `(x, y, 1.0)`, `AddChild` to the root, push on the `masks` list (so it is tinted with the background, 1.8).
   * If `mask` is non-empty, a second plain sprite (layer 0) from `TTexture::Get(mask)` is attached as a child of the main sprite and coloured by the tint rule below.
5. **`path` present: path sprite** (`TPathSprite::Create(layer)`).
   * `TCurve::Create(pathTable)`; `time` (default 0); mode string -> enum as above.
   * Texture: `TAnimatedTexture::Get(image, "", "")` (this probes `<image>`, `<image>.xml`, `<image>.anm`); if that yields nothing, `TTexture::Get(image)`.
   * If `mask` non-empty: a `TAnimatedSprite` (layer 0) with `TAnimatedTexture::Get(image, image, mask)` becomes the path sprite's `pMask` (+0xa0). Confirmed (`game-windows-questions.md` 1): the second and third arguments are "sheet image override" and "mask override"; the mask's blue channel becomes the image's alpha. Original wording: the second and third arguments are "sheet image override" and "mask override" (see `TAnimatedTexture::ParseAnimation` 0x6dd32, `LoadTextures` 0x6b932), so the overlay is the same sheet drawn with the colour-mask image as its alpha, then coloured by the tint. Fallback to `TTexture::Get(mask)` if there is no sheet.
   * `SetCurve(curve, time, mode)`.
   * Perspective: `yFar = sprite.yFar or port.yFar`, `yNear` likewise, `scaleFar`, `scaleNear` likewise. If `yFar != yNear` -> `SetScaling(yFar, yNear, scaleFar, scaleNear)`. Then if `scale` > 0 -> `SetScaling(yFar, yNear, scale, scale)`. Then if `yFar != yNear` -> `SetSpeed(speedFar, speedNear)` (defaults 1, 1).
     Warning: if a sprite sets `yFar == yNear` and a positive `scale`, `SetScaling` divides by zero (inference from the formula; no shipped data does this).
   * Added to the root and to the `sprites` list.
   * Random phase: if `random` (default true) and `time > 1`: `n = RandRange(0, floor(time/50))` and `Update(50)` is called `n` times immediately. `random=false` starts at path position 0 (used by `bal_junk`, `bag_riverboat`, `bel_partingmist_*`).
6. `htile` (static or path): if non-zero and a main sprite exists, four more plain sprites using the **main sprite's texture** are added as children with x offsets `-htile`, `-2*htile`, `+htile`, `+2*htile` (children inherit the parent's scale and path motion). Clouds in zurich use `htile = 400` with path x from 0 to 800, so the strip wraps.
7. Mask tint: if there is a mask sprite and a main sprite: attach the mask as a child, then colour = `tints[RandRange(1,#tints)]` if `tints` is a non-empty table, else `GetColor("tint")` (white if absent); written to the mask sprite's colour (+0x4c..0x58). The tint is chosen once at creation.

### 1.6 Sprite tree, layers and drawing
* Everything hangs off the root background sprite. `TSprite::AddChild` re-sorts the child list with `std::list::sort` using `operator<(shared_ptr<TSprite>, shared_ptr<TSprite>)`, which compares the sprites' `layer` (0x1724ec). Ascending layer, stable.
* `TSprite::Draw` (0x7c7c0): children with **layer < 0 are drawn first** (behind), then the sprite's own texture, then the remaining children in ascending order, each one level down (`depth` argument is decremented; `PortWindow::Draw` passes -1 = unlimited). A child's drawspec is the parent's matrix times its own; alpha multiplies down; **colour tint does not multiply down** (each sprite uses its own tint).
* `PortWindow::Draw` (0x2ed4e): `TBegin2d`, draw root sprite with an identity drawspec `TDrawSpec((0,0), alpha 1, scale 1, flags 0)`, then `TWindow::Draw()`. The generic window machinery then draws child windows (labels, banner) **after** the sprites.
* Layer numbers in data: buildings 770..920, scenery 765..970 (zurich), highlight child -1, mask child 0.

### 1.7 Hit testing and mouse handling
* `FindBuilding(point)` (0x2f6e2): for each `BuildingInfo` calls the building sprite's `HitTest(point, identityDrawSpec, 0, 0)`. Of all hits it returns the one with the **highest layer** (the swap happens only when `best.layer < candidate.layer`, so equal layers keep the earlier list entry). Returns `end()` for none.
* `TSprite::HitTest` (0x7cd8e): fails if the sprite is invisible, or has no texture unless a config option is set; then the point must lie inside `GetRect`: left/top inclusive, right/bottom exclusive. `GetRect` (0x7c05c) is the transformed texture rectangle (centre anchored unless flag 8) **unioned with children only if depth > 0**; here depth is 0, so only the building's own texture rectangle counts (the hidden highlight child is excluded).
* After the rectangle test, a per-pixel alpha test runs only if the threshold argument is >= 0: it renders the sprite into a 4x4 target and compares the alpha of the pixel under the cursor with the threshold. `FindBuilding` passes **0**, so the comparison `0 <= alpha` is always true. Net effect: **rectangle hit test** (inference: the shipped game intended an alpha mask test but threshold 0 disables it; the code still depends on the offscreen target being creatable, and returns false if it cannot be). Reimplement as a rectangle test. The point is in window-local coordinates (the root sprite sits at 0,0).
* `OnMouseMove` (0x2f846): `hit = FindBuilding(point)`. If it differs from `targetBuilding`: (a) if there was a target, `CancelBuildingHighlight` (0x2edf6): hide its highlight sprite, `BSGHideWindow(label)` if any, `rolloverTime = 0`, invalidate screen; (b) `targetBuilding = hit`; (c) if `hit != end`: highlight sprite visible, label (if any) `BSGShowWindow` and `SetWindowAlpha(1.0)`, `rolloverTime = 0`. **Always returns false** (event not consumed). No sound, no Lua call.
* `OnMouseDown` (0x32510): if a child window named `curve_editor` exists and is visible (dev tooling only) the click goes to its handler and, if unhandled, to the first path sprite hit (`HitTest(point, identity, -1, -1)`, rectangle only) whose curve is then loaded into the editor; returns false in that branch. Otherwise `mouseDown = FindBuilding(point)` and the return value is `mouseDown != end` (the press is consumed when it landed on a building).
* `OnMouseUp` (0x2f906): proceeds only if `mouseDown != end` **and** `FindBuilding(point) == mouseDown` (press and release on the same building). Then: if there is a hover target, `CancelBuildingHighlight`; builds the string `"gCurrentBuilding=nil;" + building.name + ":OnClick()"` and runs `TScript::DoLuaString(mainScript, str, -1, 0)` (the same injection path as design-questions section 10); returns true. `mouseDown` is not reset afterwards (it is overwritten by the next press).
* `OnKeyDown` returns false.

What Lua receives: nothing is passed as an argument; the callee is the global named exactly like the building (`zur_market`, created by `Building:Create` as `_G[name]`), method `OnClick` (`Building:OnClick`, `sim/building.lua:922`, which then runs the priority chain of quests/dialogs). `gCurrentBuilding` is cleared first so `Building:OnClick`/`OpenBuilding` (`ui/helpers.lua:974`) see a clean slate. A reimplementation must therefore keep a global per building and tolerate a missing global (the real engine would log a Lua error).

### 1.8 Update loop (`PortAnimTask` and `DoAnimation` 0x2ee9a)
`PortAnimTask::Animate` (0x172228) calls `PortWindow::DoAnimation` (vtable slot +0x74) and returns true. The task is a `TAnimTask` with delay 50, repeat, `fromNow` and `catchUp` flags set. `TAnimTask::DoTask` (0x813fe) with `fromNow` set re-arms with `next = now + 50` before each call and never loops to catch up, so the rate is **at most one tick per 50 ms and slower if the main loop is slower**. If the task is more than 1000 ms late it resynchronises to now. Inference from `TAnimTask` code: `Ready()` is true when `now >= next`.

Per tick, in order:
1. `t = pAnimTask->GetTime()` (platform timer, ms).
2. **Intro fade** while `lastTime <= startTime + 2000` (evaluated before `lastTime` is updated, so one extra tick after 2000 ms runs):
   * `e = t - startTime`. Background brightness `v = 0.25 + 0.75 * e/2000` for `e < 2000`, else 1.0. `TintBackground((v,v,v,1))` writes the colour into the root sprite and every sprite in the `masks` list (static scenery). Buildings and path sprites keep their own white tint, so they are at full brightness from the start.
   * If `e > 1000`: label alpha `a = max(0, 1 - 2*(e/2000 - 0.5))`, i.e. 1.0 at 1000 ms falling linearly to 0 at 2000 ms. For every building other than the current hover target that has a label window: `SetWindowAlpha(label, a)` and `BSGShowWindow(label)`. (Between 0 and 1000 ms the labels are not touched, so they show with whatever the DSL gave them: visible at full alpha. This is the "show all building names briefly on arrival" effect.)
3. `lastTime = t`.
4. **Hover pulse** if `targetBuilding != end`: `rolloverTime = (rolloverTime + 50) % 3000`; `f = rolloverTime/1500`, folded to `2 - f` if `f > 1` (triangle 0 -> 1 -> 0, period 3000 ms); `alpha = (f > 0.4) ? 1 - 0.5*f : 0.8`; `scale = 1 + f * rolloverScale`. The highlight sprite's embedded drawspec is replaced by `TDrawSpec(position unchanged, alpha, scale, flags 0)`. Because the highlight is a layer -1 child at the building's origin it is drawn **behind** the building and scales about the building centre.
5. Every path sprite: `TPathSprite::Update(50)` (1.9a). Fixed 50 regardless of real elapsed time.
6. If `musicTime <= t`: `ChangeMusic()` (1.10).
7. Invalidate the screen; return true.

`PausePortAnimations(pause)` (0x2ec2a): stores `explicitPause = pause`; `InternalPause`: pause -> `TAnimTask::Pause` (next = -1, so nothing runs); unpause -> `SetDelay(50, true, true, true)` (next = now + 50). `startTime/lastTime/musicTime` are real timer values, so a long pause ends the intro fade immediately and may trigger a music toggle on resume. Used by `sim/product.lua:696-701` around the factory mini-game dialog.

Foreground handling (`ToggleForeground` 0x2ec10, static, called from the platform event type 0x12, see window-system.md): when the app is not in the foreground, pause the animation task; and if the top modal is this PortWindow, run `if not gUIPaused then DoModal('ui/ui_pause.lua') end` via `DoLuaString`. When it returns to the foreground, resume only if `explicitPause` is false.

### 1.9 TPathSprite (`PathSprite.h`; 0x2dd2c-0x2e4ee)
Class: `TPathSprite : TAnimatedSprite` (object size 0xcc). Fields: `+0x84` curve, `+0x8c` time (float ms), `+0x90` duration, `+0x94` offset (always 0 from PortWindow), `+0x98` scale, `+0x9c` mode, `+0xa0` pMask, `+0xa8` scaleType (0 none, 1 Y; only Y is ever set), `+0xac/0xb0` far (0, yFar), `+0xb4/0xb8` near (0, yNear), `+0xbc scaleFar`, `+0xc0 scaleNear`, `+0xc4 speedFar`, `+0xc8 speedNear`. Constructor: time 0, scale 1, mode 0, scaleType 0, speeds 1.

`SetScaling(yFar, yNear, scaleFar, scaleNear)` sets scaleType = Y and stores the values; also initialises `scale = ((spriteY - yFar)/(yNear - yFar)) * (scaleNear - scaleFar) + scaleFar` using the sprite's current y (0 before the first update). `SetSpeed` stores far/near speeds. `SetCurve` stores curve, duration and mode (note: resets nothing else).

`Update(delta)` (0x2e0fe), exact order:
1. **Time advance.** If `speedFar == speedNear`: `time += delta`. Else `speed = speedFar + ((y - yFar)/(yNear - yFar)) * (speedNear - speedFar)` with `y` = the sprite's **current** y position (from the previous update, not clamped), `time += speed * delta`.
2. **Curve parameter**:
   * mode ONEWAY(0) or PAUSE(1) and `time >= duration`: point = curve end (`GetPoint(1.0)`, no tangent). For ONEWAY the sprite is also removed from its parent (`parent->vtable[+0x14]`, RemoveChild; inference for the slot) so it disappears; `Update` keeps being called but only recomputes the end state.
   * otherwise `m = fmod(time/duration + offset, 1.0)` for LOOP and ONEWAY/PAUSE (before completion); for BOUNCE `m = fmod(time/duration + offset, 2.0)` and if `m >= 1.0` then `m = 2.0 - m`. (Constants 1.0 and 2.0 verified in the data section, 0x1ab6e4 / 0x1ab71c.) A negative `time` gives a negative `m`, which `GetPoint` treats as the start point.
   * `point, tangent = curve.GetPoint(m, &tangent)`; tangent is normalised and comes from the flattened segment under `m` (not reversed on the BOUNCE return leg; inference).
3. **Scale**: `scale = 1`; if scaleType is Y: `scale = scaleFar + ((point.y - yFar)/(yNear - yFar)) * (scaleNear - scaleFar)` using the **new** point y, unclamped (extrapolates beyond the far/near rows).
4. **Frame from heading** if the sprite has more than one frame: `angle = 180 - acos(tangent.y) * 180/pi`; if `tangent.x < 0` then `angle = 360 - angle`; `frame = (uint)(angle / (360/numFrames) + 0.5)`; `SetCurrentFrame(frame % numFrames)` on the sprite and on `pMask`. Angle 0 = moving up the screen, 90 = right, 180 = down, 270 = left; frame 0 faces up. At the exact start/end of the curve the tangent stays (0,0) (inference: default `TVec2`), giving 90 degrees.
5. **Transform**: matrix = identity then `Scale(scale, scale)`; position = `(point.x, point.y, 1.0)`.

So the sprite's anchor is its position (centre for plain textures, registration point for sheets). The perspective model: a pseudo-3D "street" where larger y means nearer; shipped data uses yFar 260..280 and yNear 600 (default 800 from the port), scaleFar 0.01..1, scaleNear 0.5..4 and speeds 0.05..1.5.

`TCurve` (`Curve.h`; not otherwise documented yet): `BuildCurve` reads each array element that is a table as a control point `{x, y}`. The control points form a chain of **cubic Beziers** using groups of four sharing endpoints: segment i uses points `3i..3i+3`, so a path wants `3n+1` points (4 for a straight run, 16 for the five-segment car routes). `DoSubdivision` flattens each cubic by recursive halving (de Casteljau) until `controlPolygonLength - chordLength <= __ACCEPTABLE_ERROR` (value not read; unknown), recording per piece the control polygon length and cumulative length. `GetPoint(t)` is **arc-length parameterised**: `t <= 0` -> start, `t >= 1` -> end, otherwise `d = t * totalLength`, walk the pieces, and linearly interpolate along the piece that contains `d` (inference for the lerp factor `d/pieceLength`); the optional tangent is the normalised vector end-start of that piece.

### 1.10 Music
`ChangeMusic` (0x2ec46): the flag starts true (arrival sting already playing from Lua `EnterPort`, which also sends `SoundEvent(cadikey)` for ambience).
* flag true -> flag false, `BSGSoundEvent("Stop_Music")`, next toggle in `RandRange(10000, 15000)`.
* flag false -> flag true, `BSGSoundEvent("port_music" + RandRange(1,3))` (`port_music1..3`), next toggle in `RandRange(40000, 60000)`.
* `musicTime = now + delay`. First call at `musicDelay` (25000 ms).

### 1.11 Transitions in and out
There is no engine-side "travel in/out" transition in `PortWindow`. Entry is Lua: `Port:EnterPort(sting)` calls `Player:SetPort`, `ReleaseLedger()`, `SwapToModal("ui/portview.lua")` (`sim/port.lua:197`), which builds the dialog described in 1.2. The visible "transition" is the intro fade of 1.8 (dark to lit over 2 s, building names visible for the first second then fading). Leaving is `SwapMapPortScreens()` -> `SwapToModal("ui/mapview.lua")`. Travel itself is `InitiateTravel{...}` on the `MapWindow` with 3000 ms per week of travel (documented in `api-globals.md`; the Lua schedules arrival with `dest:EnterPort(true)`).
`Building:TransitionIn(x,y)` (zoom into the building dialog) is Lua using the `Transition` global (types `path`, `zoomin`, `fadein`), unrelated to `PortWindow`.

---

## 2. Character / CharWindow (`Character.h`, `Character.c`)

### 2.1 Class
`Character : TBSGWindow` (so BSGWindow keys `frame`, `color`, `fill`, `swallowmouse` apply, see design-questions). Registered as Lua type `CharWindow` (factory `CharacterDynamicCreate` 0x73de). Fields: `+0x50` name, `+0x54/+0x58` pImage (texture, may be a `TAnimatedTexture`), `+0x5c` alpha (default 1), `+0x60` scale (default 1), `+0x64` happinessFrame (shown), `+0x68` targetHappinessFrame, `+0x6c` timer (ms). Every instance is appended to a static list `visibleCharacters` in the ctor (0x7350) and removed in the dtor.

### 2.2 DSL (`PostChildrenInit` 0x78ba)
After `TBSGWindow::PostChildrenInit`:
* `name` (string, `""`), `alpha` (1.0), `scale` (1.0), `happiness` (**50**).
* Image lookup: `TAnimatedTexture::Get("characters/" + name + ".xml")`; if null, `TTexture::Get("characters/" + name + ".png")`; if still null, `TTexture::Get("characters/temp_char")`. For an animated sheet `SetCurrentFrame(0)`. Names like `main_alex`, `zur_stationkeep`, and `<name>.silhouette` (catalogue; resolves to `characters/<name>.silhouette.png`) all go through this.
* Window size: `w = texW` and `h = texH` (current-frame size, so frame 0 for sheets); if `scale != 1` each is `(uint)(value * scale + 0.5)`; the window is then set to `max(current window size, that size)` per axis (`SetWindowSize`). Lua usually gives only `x,y`, so the window becomes the scaled body size.
* `SetMood(happiness)`, then `happinessFrame = targetHappinessFrame` immediately (no fade at creation).

### 2.3 Sprite-sheet XML (`TAnimatedTexture::ParseFromXML` 0x6bec0)
Used for characters, port animations and UI sheets (`image/factory.xml`, ...). Shape (see `characters/main_alex.xml`):
```
<sequence name="..." source=".\main_alex*.png">
  <img regx="83.0" regy="124.0" name="main_alex.png" [mask="x.mask.png"] w="0" h="0"/>
  <framelist>
    <frame regx="70" regy="105" name="main_alex.png" w="160" h="240" x="0" y="0"/>
    ...
  </framelist>
  [<script luascript="function DoAnim() ... end"/>]
</sequence>
```
* Node names in the binary: `sequence`, `img`, `framelist`, `frame`, `script`; attribute names: `regx`, `regy`, `xform`, `anchor` (child of `frame`, with three attributes: probably name, x, y; stored as {str, int, int}), `luascript`, `name`, `source`, `mask`, `x`, `y`, `w`, `h`. Errors logged: "does not contain a sequence/img/framelist node", "does not contain any frames!".
* Per frame record (0x40 bytes): `x,y,w,h` (int, rectangle inside the sheet image), `name` (string, informational), `regx,regy` (int registration point; if the attribute is empty copies the previous frame's, 0 for the first), `xform` (six floats separated by spaces; empty copies the previous frame; the draw skips the matrix only when the first float equals the sentinel -10000000.0, the place where that is written was not found), a float multiplier (+0x3c, default 1; the attribute name was not recovered, probably an alpha/scale factor), and the `anchor` list. Max frame width/height are tracked.
* The sheet image is `<xml directory> + img.name`, loaded as texture (PNG, or JPG plus a separate `.mask.png` for alpha: `LoadTextures` 0x6b932 passes the mask name to `TTextureData::LoadFile`). `Get(name, imageOverride, maskOverride)` caches by a handle string built from all three; non-empty overrides replace the sheet image or mask.
* Script: the `script` node's `luascript` attribute (or its text content if the attribute is empty) becomes the sheet's script. With no script node, `SetDefaultScript` (0x6b1da) generates `function DoAnim()\n    delay(60);\n    frames{1,2,...,n};\nend\n` (plays once, no loop).
* Attributes the shipped character files use: only `regx regy name w h x y`. All 61 shipped character sheets have exactly 4 frames and no script.

### 2.4 Drawing (`Draw` 0x741c)
* Plain texture: draw once at window-local `(texW*scale/2, texH*scale/2)` with alpha, scale (vtable +0x18 `DrawSprite(x, y, alpha, scale, rotation, flags)`; for plain textures x,y is the centre).
* Animated sheet: `fx = frame0.w * scale / 2`, `fy = frame0.h * scale / 2` (frame 0 selected to measure). Draw frame 0 (the body) at `(fx,fy)` with `alpha`. If the sheet has more than 1 frame: select `happinessFrame` and draw it at the same `(fx,fy)` with `alpha`; if `happinessFrame != targetHappinessFrame`, also draw the target frame at alpha `timer * alpha / 200`. For animated textures the point is where the frame's **registration point** lands (`TAnimatedTexture::DrawSprite` 0x6b400/0x6ad30 sets the draw origin to `regx,regy` of the current frame), which is why face frames (reg ~17,75) line up on the body (reg 70,105). Note the face overlay is drawn **additionally** over the body, not instead of it; both use the character's `alpha`. (Confirmed in `game-windows-questions.md` 4; originally an inference: the DrawSprite 6-argument form's `x,y` is the registration point position; the body frame's own reg then sits at the frame-0 centre, not at the window origin.)
* No frame/fill is drawn: `Character::Draw` replaces `TBSGWindow::Draw` entirely. Mouse events still follow `TBSGWindow` (`swallowmouse`).
* `SetAlpha(float)` (0x71cc) writes the character's own alpha field, which is the value the fade transitions drive.

### 2.5 Mood (facial animation)
* `SetMood(h)` (0x76fa), `h` in 0..100. Requires an animated texture. Let `n` = frame count.
  * `n == 3` (unused by shipped data): `i = (int)(h*3/100)`; target frame = 1 if `i == 0`; 0 if `i == 1`; 2 otherwise.
  * `n >= 2` otherwise: `last = n-1`; `i = (int)(last*h/100) + 1`; target = `min(i, last)`. For the shipped 4-frame sheets: `h < 33.3` -> frame 1 (displeased), `33.3 <= h < 66.7` -> frame 2 (neutral), `h >= 66.7` -> frame 3 (pleased); `h = 100` also 3. Frame 0 is always the body.
  * `n < 2`: nothing.
  * If the target differs from the shown frame: `timer = 0` and `StartWindowAnimation(50, ...)`.
* `OnTaskAnimate` (0x71da), every 50 ms: `timer += 50`; at `timer >= 200` clamp to 200 and set `happinessFrame = targetHappinessFrame`; invalidates the screen; returns "keep going" while `timer < 200`. Cross-fade = 4 ticks / 200 ms. (Re-targeting mid-fade snaps `timer` to 0 and restarts the fade from the *old shown* frame.)
* Lua: global `SetMood(name, h)` (registered in `ChocoThreeMain`, calls `Character::SetCharacterMood` 0x7866) applies to **every** live `Character` window whose name equals `name`. Called by `Character:SetHappiness/BumpHappiness` (`sim/character.lua:117,131`).

### 2.6 How dialogs draw a character (worked example `ui/ui_quest_offer.lua`)
```
Window { x=1000, y=35, w=601, h=366, name="quest_offer",
  Bitmap { x=0, y=49, image="image/popup_back_dialog",
     SetStyle(C3CharacterDialogStyle),
     Text { x=241, y=48, w=314, h=172, name="dialogue_text", ... },
     SetStyle(C3CharacterNameStyle),
     Text { x=41, y=201, w=187, h=20, label=name, font=characterNameFont, flags=kVAlignCenter+kHAlignCenter },
     Group(buttons) },
  CharWindow { x=49, y=0, name=name, happiness=happiness } }
```
The **name plate is a `Text` inside the dialog backdrop bitmap**; it is not part of `Character`. `CharWindow` is a later sibling of the bitmap, so it draws on top, with its top-left at (49,0). The dialog window starts off-screen (`x=1000`) and `OpenBuilding("quest_offer", building)` slides it in. Quest offers use happiness 50 (neutral). Scales used elsewhere: 0.5 (`quest_detail.lua`), 0.4 (`help_catalogue.lua`), `portrait_scale` (catalogue).

---

## 3. What replaces "port hover popups"

Inside a port: label window + highlight sprite (1.7, 1.8). The label is a Lua `BSGWindow` (`controls/rollover` frame, `rolloverColor`, `TightText` with `GetString(b.name)`), positioned once at construction and shown/hidden by the engine. `Building:PortRolloverContents()` (`sim/building.lua:161`) exists in Lua but nothing in the port view calls it; the engine does not reference `PortRolloverContents` anywhere (the `Rollover` windows that call `contents` strings are the map, ledger and market).

---

## 4. PopupWindow (`Popup.h`, `PopupWindow.c`) 

Not a Lua-constructible type (no `DynamicCreate`); created only by `Rollover::OnMouseMove` (flow already in design-questions section 10). Additional facts from the class:
* `PopupWindow : TBSGWindow`, object size 0x70. `+0x50` clickString, `+0x54` click `TLuaFunction` pointer, `+0x5c` `TPopupTest*` (close tester), `+0x64` transition-active shared flag, `+0x6c` clickActive.
* `PostChildrenInit` (0x2e65e): `TBSGWindow::PostChildrenInit`, then `FitToChildren`.
* `SetContents(luaString)` (0x2e7f2): run the string with the popup as current window (`DoLuaString`), `FitToChildren`, `SetAlpha(0, propagate)`, then a `FadeInTransition(popup, 1.0, 125 ms)`; the transition handle is stored in `+0x64`.
* `Reposition(x, y)` (0x2e5a4): rectangle = client rect translated to (x,y); limits are the screen window size minus **4**. If the right edge exceeds the right limit: shift left by the overflow **and** move down by **32** pixels (so the popup does not cover the cursor); if the bottom edge exceeds the bottom limit: shift up by the overflow (the 32-pixel push is applied before this, using the already shifted rectangle). Then `SetWindowPos`.
* `OnMouseMove` (0x2e7a0): convert the point to screen coordinates and ask `TPopupTest::KeepVisible`; if false, `Kill()`. Returns whether it stays. Testers: `TPopupTestRect` (point inside a stored screen rectangle), `TPopupTestSprite` (sprite `HitTest` in the parent's drawspec; unused by shipped Rollovers).
* `OnMouseDown` (0x2e684): sets `clickActive = true`, consumes.
* `OnMouseUp` (0x2e6e0): `Kill()` first; then, only if `clickActive`: if a click function exists, push it and run via the script's call slot (`vtable +0x14`); else if `clickString` is non-empty, `DoLuaString(clickString)`. Resets `clickActive`. Consumes.
* `Kill` (0x2e696): `RemoveMouseListener`, remove from its parent (parent vtable +0x28), `BSGHideWindow`, `SafeDestroyWindow`.
The popup fade (125 ms) and the 4 px / 32 px numbers are the only timing/geometry constants.

---

## 5. Animation script (`animationscript.lua`, per animated sprite)

### 5.1 Host side (`TAnimatedSprite`, `TAnimatedSprite.c`)
* Each `TAnimatedSprite` owns an `TAnimatedSpriteData` (0x30 bytes, `+0x80`): current frame (+0), texture (+4/+8), clock (+0xc), script `TScript` (+0x10), thread `TAnimTask` (+0x14), dead-script slot (+0x24).
* `NewScript` (0x7634a): creates a fresh `TScript` with its **own Lua state**, adopts it as a task, runs the embedded chunk `@pflib/src/core/animationscript.lua` (0xd93 bytes), registers 11 natives into that state (list in the TSV), uses the chunk name `ganimationscript` (string 0x15c008) and formats a Lua search-path string (`?;?.lua;scripts/?;scripts/?.lua;%s?;%s?.lua`, 0x15c07c) from the sheet's directory into a global C string, then runs the sheet's script text (`TAnimatedTexture::GetScript`) so `DoAnim` and any other functions exist.
* `Play(name)` (0x771d6): create the script if needed (needs a texture), reset the old thread, `NewThread`, adopt the thread task, push the global `name` (normally `"DoAnim"`) and `lua_resume` it with 0 args. Runs immediately up to the first yield; a Lua error is logged with `WriteDbg` and playback stops.
* `WaitLuaNextFrame(ms)` (0x75e14): with one argument: `thread.SetDelay(ms, repeat=true, fromNow=true, catchUp=false)`; with none: `thread.Pause()` (never wakes). Then `lua_yield` if called from the thread (not from the main chunk). So the frame time **is the `delay` value in ms**. The clock is always the **platform timer** (`TPlatform::Timer`, ms since start): `TAnimatedSpriteData` +0xc is never assigned, and both the script task and the thread are on the platform task list, so sprite scripts do **not** freeze under dialogs and are **not** stopped by `PausePortAnimations` (that only pauses `PortAnimTask`). See `game-windows-questions.md` 3. No cap, no fixed step.
* `SetCurrentFrame(i)` (0x75f8c): accepts `i > -2`; stores it, and if `i < numFrames` also sets the texture's current frame. `Draw` re-applies the stored frame before drawing, so sprites sharing a texture do not interfere.
* `Pause(pause, depth)` (0x77318): pause -> `thread.Pause()`; resume -> `SetDelay(1, ...)` (wakes in 1 ms); recurses into child sprites to the given depth (children that are `TAnimatedSprite`). `IsPlaying(depth)` (0x77608): true if the thread coroutine is active or (recursively) any child is. `Die` (0x78d14): reset thread, orphan the script task. `RemoveSprite`: detach from parent. `Random(min,max)` (0x75eac) = `min + rand() % (max - min)`: **exclusive of max and a divide-by-zero if equal** (unlike global `RandRange`).
* Animated sprite anchor: registration point of the current frame (section 2.4), so animated static scenery's `x,y` is the registration point.

### 5.2 Lua side (embedded chunk, disassembly in `re/mac/lua/animationscript.lst`)
State: `framedelay` (local upvalue, initially **16**), globals `alpha=1`, `alphadelta=0`, `alphacount=0`.

| function | semantics |
|---|---|
| `delay(ms)` | `framedelay = ms` (the delay applied after each frame by `frames`/`showFrame`) |
| `pause(ms)` | `WaitLuaNextFrame(ms)` |
| `showFrame()` | `WaitLuaNextFrame(framedelay)` |
| `fadeout(n)` | `alphadelta = -alpha/n; alphacount = n` (fade to 0 over the next n frames of `frames`) |
| `fadein(n)` | `alphadelta = (1-alpha)/n; alphacount = n` |
| `setAlpha(a)` | `alpha = a; SetAlpha(a)` |
| `frames(list)` | for each element `f` of the array: `frame = f`; if `alphacount ~= 0`: `alpha = alpha + alphadelta; SetAlpha(alpha); alphacount = alphacount - 1; if alphacount == 0 then alphadelta = 0`; then `SetCurrentFrame(f - 1)` (**list entries are 1-based**); then `WaitLuaNextFrame(framedelay)` |
| `loop(times, list)` | `for i = 1, times do frames(list) end` |
| `run{ xml, image=, mask=, anim= }` | `if Load(xml, image or "", mask or "") then` evaluate `GetScript() .. "; " .. (anim or "DoAnim") .. "()"` via `loadstring` and call it. Switches the sprite to another sheet and runs one of its functions inline. |
| `die()` | `Die()` |
| `halt()` | `Pause(true, -1)` |
| `stop(depth)` | `Pause(true, depth or -1)` |
| `isPlaying(depth)` | `IsPlaying(depth or -1)` |
| `endReferences()` | no-op |
| `random(a,b)` | `Random(a,b)` |

No easing functions exist: fades are linear per-frame steps, and all frame changes are instantaneous. Shipped sheets use only `delay(n)` followed by `while true do frames { ... } end` (delay 85..250 ms; repeated frame numbers hold a frame for multiple delays).

Timing example: `wave1.xml`: `delay(85)`, 69-entry list, so one cycle is 69 * 85 = 5865 ms.

### 5.3 What Glitz / particles / ScalingTask have to do with this
Nothing in `PortWindow`, `Character`, `PopupWindow` or `TPathSprite` references `Glitz`, `TLuaParticleSystem` (zero hits in those decompiled files). `Glitz.h` only defines `ParticleColor` (a particle machine function pushing a colour). `ScalingTask.h` (`TOldScalingTask`, `TScalingTask`) is used only by the mini-games (Chocolate/Coffee factory tasks, FlyingRing, Hopper...), not by the port screen.

---

## 6. Reimplementation checklist (what the Lua expects)

* `PortWindow{...}` must accept `port` (table), `background`, `musicDelay`; read the table once at `PostChildrenInit`; provide global `PausePortAnimations(bool)`.
* After building clicks run `gCurrentBuilding=nil;<name>:OnClick()` on the main script.
* Child windows named like buildings are positioned and shown/hidden by the port window; their initial state is visible.
* `CharWindow` with `name`, `alpha`, `scale`, `happiness`; global `SetMood(name, 0..100)`.
* Sprite-sheet loader with `<img>`, `<frame regx regy x y w h>`, `<script luascript>` and a per-sprite Lua state exposing the 11 natives plus the 15 embedded functions in 5.2.
* `TCurve` chain of cubic Beziers with arc-length lookup and tangent.
* Layer sort order, negative layers behind, per-sprite (non-inherited) tint.

---

## Unknown

* `__ACCEPTABLE_ERROR` (flatness tolerance in `TCurve::Subdivide`) and the exact lerp factor inside `GetPoint` (inferred as arc fraction within the piece).
* Whether `TSprite::HitTest`'s threshold `0` is a deliberate bypass; the config option consulted when a sprite has no texture; what happens if `BeginRenderTarget` fails (returns false in the code I read).
* The `anchor` child's attribute names (inference: `name`, `x`, `y`). Resolved: the per-frame float at +0x3c is the frame attribute **`alpha`** (default 1.0, multiplied into the draw alpha); the `<img>` node stores `regx/regy` (record +0x2c/+0x30, only when `regx` is present), `w`/`h` (+0x34/+0x38), `name` (+0x0c), `mask` (+0x10); the two last are the targets of the `Get(name, image, mask)` overrides (`game-windows-questions.md` 1).
* Exact semantics of `vtable +0x14` on a sprite parent (assumed `RemoveChild`) used when ONEWAY path sprites finish, (resolved: `StartWindowAnimation(delay, repeat, fromNow, catchUp)` forwards to `TAnimTask::SetDelay`; see `game-windows-questions.md` 9.2).
* Whether `TBSGWindow::SetAlpha(alpha, propagate)` reaches `Character::SetAlpha` through a virtual call (a `Character` has its own alpha field; Lua fades on a parent window may or may not fade the character).
* Order/stability guarantee of `TLuaTable::GetNext` for non-array tables (shipped data uses arrays).
* `IsDone`/`RandomizeTime` in `PathSprite.h` have no separate decompiled body (probably inlined or unused); PortWindow does its own randomisation loop.
* Behaviour when `port.buildings`/`sprites` or the `port` style key is nil (`CollectBuildingData` is called with a possibly null table).
* Behaviour of `TAnimTask` when resumed after the app was suspended for a long time beyond the 1000 ms resync.
