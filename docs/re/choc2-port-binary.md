# Chocolatier 2 port scene: what the Mac binary says (`TPort`, `TCharacter`, `TPopupWindow`, `TMapTravel`)

Source: the Chocolatier 2 Mac app bundle's `Contents/MacOS/Chocolatier2`, i386 slice, unstripped, decompiled with Ghidra into
`re/choc2/decomp/` (git-ignored): `TPort.c`, `TPort__TSpriteInfo.c`, `TPort__TPortPopupTest.c`, `TPort__TFirework.c`, `TSprite.c`, `TDrawSpec.c`,
`TPopupWindow.c`, `TPopupWindow__TFader.c`, `TCharacter.c`, `TMapTravel.c`, `TLedger.c`, `TFactoryStatus.c`, `TMap.c`. Same method and labels as
`docs/re/choc2-factory-binary.md`. Float constants Ghidra hid were read from the i386 slice (`otool -tvV`, `re/choc2/Chocolatier2_i386`).
The Mac app ships the Lua as plain text under `Contents/Resources/assets/` (`ui/portview.lua`, `sim/port.lua`, `sim/building.lua`,
`sim/character.lua`, `sim/travel.lua`, `ports/*.lua`), which is how the Lua side below was checked. This replaces the "ALL INFERRED" port-scene
decisions in the workbench decision log and the unknowns in `docs/re/choc2-port-and-ledger.md` sections 1 and 8.

Labels: **FACT** = read from the binary (address given, hex VA in the i386 slice), **INFERENCE** = reading around a gap, **UNKNOWN**.
Engine comparisons were made by reading `src/game/choc2/{Port,PortScene,Character,MapTravel,TravelWeek,LedgerWindow}.cpp`,
`game/PortWindow.cpp`, `game/sprite/{Sprite,PathSprite}.cpp`, `ui/widgets/InputWidgets.cpp` and `script/games/Choc2Natives.cpp`; no
headless run was needed (every claim is about code paths, not rendering).

## 0. Top discrepancies, ranked by how visible they are to a player

| # | Where | Binary | Engine now | Visible as |
|---|---|---|---|---|
| 1 | Character mood faces (3.2) | Pleased when `mood >= kDefault` (50), displeased when `mood <= kAngry` (25), neutral between (26..49). **A character at its default mood 50 shows the pleased face.** `kHappy` (70) is never read. No cross-fade (instant swap). | pleased at >= 70, neutral 26..69, 200 ms cross-fade | every shopkeeper/quest-giver portrait in every dialog |
| 2 | Character `appearance` (3.1) | Body and faces are loaded from `characters/<appearance>` (falls back to the character name only when no `appearance`) | always `characters/<name>` | the six New York gamblers (`ny_gambler01b/01/02/03/04/05`) get no body and no face |
| 3 | Hover/click hit test (1.3) | sprite rectangle, then the rendered alpha at the pointer must be >= 192 | rectangle only | popups/clicks fire on the transparent corners of a building and through neighbouring buildings' empty space |
| 4 | Arrival labels and pulse (1.5, 1.6) | `buildinglabels` shown until 3900 ms then hidden at once (no fade); every clickable building pulses (1300 ms triangle, +20 px) in sync until then; any hover popup or `StopPulsing()` ends it for all but the hovered building | labels shown 1 s then faded over 1 s; C3 3000 ms hover pulse only | what the port looks like for its first 4 seconds, and the hover glow |
| 5 | Path sprite speed (1.4) | no `time` = **10000 ms**; `bounce` is a *round trip in `time`* (the engine takes `2 x time`) | no `time` = 15000 ms; bounce takes `2 x time` | all 30 timeless vehicles 1.5x slower; all 53 bouncing sprites (ships, cars, trains) 2x slower |
| 6 | Map travel vehicle icon (5) | mirrored when moving right (same tangent rule as port vehicles), shown only while a week is running, trail dot every 160 ms of leg time | never mirrored; dots by arc length (12 px) | ship/train/camel icons face the wrong way going east |
| 7 | `StartProductionAnim`/`StopProductionAnim` (6) | set a global flag that makes the ledger factory-status pictures bob and rotate | no-op | factories tab is static during travel |
| 8 | Fireworks, San Francisco after `bcsign > 5` (7) | 5 fireworks, drawn between two sprites of the layout | real: `FireworkSprite` on the shared TFirework machine | yes (the call-last edge case does not carry over) |
| 9 | `hflip` key (1.2) | read; mirrors the sprite | logged as "not read" | San Jose waterfall (`waterfall.xml`, `hflip=true`) is not mirrored |
| 10 | San Francisco airship (1.4) | `scale=.3` is only the default for `nearscale`/`farscale`; the explicit ones win, so the airship grows .2 to 1 with y | the later `scale` call overrides the perspective: constant .3 | one sprite |
| 11 | Quest bar (8) | moves 26 px per ledger tick, hidden at y 53, shown at y 0 | 500 ms swipe | slide speed/feel only |
| 12 | Popup details (2) | background rectangle fades to 0.8 (not the Lua `alpha`); command runs on any release | fades to 1; command needs the press on the popup | faint |

## 1. `TPort` (window type `Port`)

Layout: ctor 0x274b8, `Init` 0x269ec, `PostChildrenInit` 0x26abe, `OnTaskAnimate` 0x26fce, `OnMouseMove` 0x2782e, `Draw` 0x288b6, `AddSprite` 0x27f34,
`UpdatePulseTexture` 0x27b24, `CheckSpriteRollover` 0x26ef2, `StopPulsing` 0x26e8a, `StartFireworks` 0x271e2, `OnKeyDown` 0x26b6e, `OnMessage` 0x27ee6.
There is **no** `OnMouseDown`/`OnMouseUp` in `TPort`: a click on a building only ever reaches the building through its hover popup (2).

### 1.1 Natives registered by the ctor (FACT, 0x274b8)

`bsgAddSprite(table)` -> `AddSprite`; `StopPulsing()` -> `StopPulsing`; `StartFireworks()` -> `StartFireworks`. They are closures bound to the **Port
being constructed**, so every layout item that runs while the `Port{...}` children are built talks to the new Port. (Engine: same idea,
`Port::underConstruction`.) The ctor also registers a texture-refresh hook; `OnMessage(2000)` rebuilds every pulse texture (device reset).

State: `+0x30` sprite list (`TSpriteInfo` nodes, in call order), `+0x38/+0x3c` xOffset/yOffset (from `Init`, default 0), `+0x40` scene clock (ms),
`+0x44` hover clock (ms), `+0x48` hovered `TSpriteInfo*`, `+0x4c` "intro pulsing" flag (ctor sets 1), `+0x58` firework list, `+0x60` firework insert
index (ctor -1).

`PostChildrenInit` (FACT): `FitToChildren`; if `bsgCheckConfig("dev")` runs `dev/dev.lua`; `StartWindowAnimation(0x28 = 40 ms, ...)`; scene clock = 0.
`OnKeyDown`: Escape runs `DoModal('ui/pausedialog.lua')`, nothing else.

### 1.2 Keys `bsgAddSprite` reads (FACT, `AddSprite` 0x27f34)

| key | default | use |
|---|---|---|
| `image` | "" | `".LOGO"`: build the player-logo sprite from table `logo` (`TLogoDef`, `BuildLogoSprite`); no `logo` table = a sprite with no texture. Otherwise `bsgTTextureGet(image, "temp/error")` (a missing picture draws the error texture). `.xml` names are loaded the same way (animated texture) and `DoAnim` is played. |
| `x`, `y` | 0 | sprite position = `x + xOffset`, `y + yOffset` (sprite `+0x38`) |
| `path` | none | array of points -> `TCurve::Create`, then `TCurve::Move(xOffset, yOffset)` |
| `scale` | 1 | base scale (path or not) |
| `nearscale`, `farscale` | = `scale` | perspective end scales |
| `near`, `far` | 400, 0 | y of the near/far end of the perspective (stored as int) |
| `time` | **10000** | ms for one run of the path |
| `hold` | 0 | ms the sprite waits at the **start** of each cycle (1.4) |
| `bounce` | false | out-and-back (1.4) |
| `alpha` | 1 | draw alpha (goes into `TDrawSpec` param 2) |
| `htile` | false (GetBoolean) | tile horizontally (1.4); any non-nil, non-false value is true |
| `hflip` | false | forces the mirror flag on (bit 0 of the draw flags) |
| `labelx`, `labely` | -1000 | popup anchor (int); at or below -501 means "use the pointer" |
| `name` | `<none>` | read and thrown away (not stored in the sprite record) |
| `contents` | "" | Lua string run to build the hover popup; empty = not hoverable |
| `command` | none | function run on click; also decides whether a pulse texture is made |

Keys the Lua passes that the native **does not read**: `loop` (37 uses; clouds loop anyway because a non-bounce path wraps), `wait` (1 use, `bombay.lua:37`),
`random`, `imagelist`, `startweek`, `endweek` (all consumed by the Lua `SpriteLayer`/`portview.lua`: `random` is a percent chance, 1..100, that the
entry exists at all, `imagelist` picks the image with `bsgRandom`; both once per port build). There is no `layer` key.
Engine: reads the same set except `hflip`, `alpha`, and wrong defaults for `time`/`near` (see 1.4); notes `wait`/`hflip` as unread.

Record layout (`TSpriteInfo`, FACT from AddSprite locals / Draw offsets): `+0` sprite, `+8` alpha, `+0xc` curve, `+0x14` random phase, `+0x18` time,
`+0x1c` hold, `+0x20` bounce, `+0x24` near, `+0x28` nearscale, `+0x2c` far, `+0x30` farscale, `+0x34` htile, `+0x35` hflip, `+0x38/+0x3c` labelx/labely,
`+0x40` command, `+0x48` contents, `+0x4c` pulse sprite. Random phase = `rand() % (time + hold)`, drawn once per sprite (so a plane may already be mid-flight
at arrival). A `command` makes `UpdatePulseTexture` run at add time (1.6).

### 1.3 Anchoring, draw order, hit rectangle and the alpha test

* **Anchor (FACT, `TSprite::GetRect` 0x62140):** a sprite is positioned by its **centre** unless flag 8 is set (then top-left). `AddSprite` never sets 8, so every
  building and scenery sprite is centred on `(x + xOffset, y + yOffset)`. Mirroring (flag bit 0) is about that centre, so the position does not move.
  Engine: same (centre anchor, `topLeft` only for the background).
* **Draw order (FACT, `Draw` 0x288b6):** one list, drawn front to back in the order the Lua called `bsgAddSprite`: later = on top. Buildings, scenery and path
  sprites are all in that one list; there is no layer or z. Engine: same (`order_` shared by buildings and sprites).
* **Hover search order (FACT, `OnMouseMove` 0x2782e):** the list is walked from the **end** (topmost first). An entry is a candidate only if its `contents` is
  non-empty; the first one whose `TSprite::HitTest(point, identityDraw, 0xc0, -1)` succeeds wins. Sprites without `contents` never block a hover. Engine: equivalent
  order (highest layer among buildings).
* **`TSprite::HitTest` (FACT, 0x62924):** (1) false if the sprite has no texture or is invisible. (2) The sprite's bounding rectangle, **including all child
  sprites** (`GetRect` unions children to unlimited depth when the depth argument is -1), must contain the point: `left <= x < right`, `top <= y < bottom`
  (integers). (3) If the threshold argument is negative the answer is true. Otherwise the sprite and its children are drawn into a **4x4 render target**
  (cleared to transparent black), translated so the test point lands on a fixed pixel, and the pixel's alpha is read back: **hit iff alpha >= the threshold**.
  The port passes **0xc0 = 192** (of 255). The draw uses the sprite's own current draw spec (position, scale, alpha, flip as set by `Draw` this frame), so
  scale and mirroring are honoured, and a sprite with `alpha < 0.75` can never be hit.
  Engine: `Sprite::hit` is the image rectangle only (no children, no alpha). **Discrepancy 3.** To match, sample the texture's alpha (or a CPU alpha mask) at
  the pointer's source pixel with a 192/255 cut-off; for sheets use the current frame.
* `CheckSpriteRollover` (0x26ef2, used by the popup to decide whether to stay open) is the same test with the pointer converted from screen to Port-client
  coordinates and again requires non-empty `contents` and a sprite.

### 1.4 Path sprites: the exact formula (FACT, `Draw` 0x288b6)

The scene clock `T` (`+0x40`) grows by **40** every `OnTaskAnimate`, a fixed step per tick, not real elapsed time. For each sprite with a curve:

1. `t = (phase + T) mod (time + hold)` (unsigned integers). `phase` is the sprite's `rand() % (time+hold)`.
2. `t = (t < hold) ? 0 : t - hold`. **`hold` is the wait at the start of the cycle**; the sprite sits at the path's first point for `hold` ms, every cycle.
3. Not bouncing: `f = t / time`. Bouncing: if `t >= time/2` (integer halve) then `t = time - t` and the leg is "returning", else it is not; `f = 2t / time`.
   So a **bounce is out and back inside one `time`**, the outbound half taking `time/2`. There is no wait at the far end. (`time = 0` would divide by zero; no shipped sprite has it.)
4. Position and tangent come from `TCurve::GetPoint(f, &tangent)`.
5. **Mirror flag (the "art faces left" rule):** `angle = acos(tangent.y)` (0..pi); if `tangent.x > 0` then `angle = -angle`; if the leg is returning then `angle = -angle`;
   the draw flag is **1 (mirrored) iff `angle < 0 or angle >= pi`**, else 0. In words: the picture is mirrored exactly when the sprite is moving to the right (taking a
   bounce's return leg into account); art is authored facing left. A vertical tangent gives 0 going down and mirrored going up. If the sprite has no path, its own flags are kept. Then `hflip` ORs in 1.
   Engine: `PathSprite::update` sets `hflip = artFacesLeft && dx > 0` (with `reversing_`), parked/degenerate keeps the last facing; equivalent except the vertical case. **No rotation and no heading frames exist in `TPort`**; a sheet on a path sprite is just an animated texture playing `DoAnim`. Engine: same (`headingFrames` only for non-`.xml`).
6. **Perspective scale (applies to every sprite, path or not):** if `nearscale != farscale`:
   `s = max(0, farscale + (nearscale - farscale) * ((y - far - yOffset) / (near - far)))` with `y` the curve/sprite y; else `s = nearscale`. Defaults: `scale` 1, `nearscale = farscale = scale`, `near` 400, `far` 0. Clamped at 0 below, **not** above.
   Engine: linear and unclamped, with a Chocolatier 3 default `yNear = 800` when `near` is absent; and the second `setScaling(.., sd.scale, sd.scale)` call overrides the perspective whenever `scale` is also given (only `sanfrancisco` airship does both). All 24 shipped sprites with `nearscale`/`farscale` also give `near`, so only the airship differs.
7. Draw spec: `TDrawSpec(position, alpha, s, flags)`; the spec is stored in the sprite and then the sprite is drawn.
8. **htile:** instead of a single draw: `x` is reduced by the texture width `w` while `x > w/2`, then the sprite is drawn at `x, x+w, x+2w, ...` while `x < portWidth + w/2` (all at the path's y, same scale/flip). The period is the raw texture width, **not multiplied by scale**. All 27 `htile=true` entries are 800-wide clouds at scale 1.
   Engine: four child copies at +-1 and +-2 times the image width; equivalent for scale 1.

Engine vs binary on path timing: **default `time` is 10000 ms, not 15000** (`GetNumber("time", 10000.0)` at 0x27f34; the engine's `kDefaultPathMs = 15000` is wrong; the 30 timeless path sprites, all vehicles, run 1.5x too slowly). `bounce` period is `time`, not `2*time`. `hold` is a start wait, not an end wait; the engine's park-at-end/both-ends model has the same on-screen duty cycle only after the random start phase (the paths start and end off screen). The random initial phase is a uniform draw over `time + hold`; the engine draws one over `time + hold` in 50 ms updates, equivalent.

### 1.5 Intro pulse and the `buildinglabels` window (FACT)

* `Port` ctor sets `+0x4c = 1` ("pulsing"). The Lua puts a child `Window{name="buildinglabels"}` holding one name Frame per named layout item with `x,y` (`ui/portview.lua:40-57`, popup font, at `x-50, y-fontHeight/2`). It is created visible.
* `OnTaskAnimate` (40 ms): `+0x40 += 40`; **the first tick on which the clock crosses 3900 ms (0xf3c) calls `StopPulsing`** (i.e. at 3920 ms); fireworks are updated; `+0x44 += 40`; screen invalidated.
* `StopPulsing` (0x26e8a): clears `+0x4c`, **hides** `buildinglabels` with `bsgHideWindow` (clears the visible flag, no fade), and clears the hovered pointer (`+0x48 = 0`).
* Lua calls that reach it: `LBuilding:NameRollover` (`sim/building.lua:120`, runs whenever a building hover popup is built), the building `command` closure (`sim/building.lua:186`), `PlayerLogoRollover`/`DoPlayerLogo` (`sim/port.lua:51,63`), `sim/quest_functions.lua:520`, `ui/mainmenu.lua:106`.
* Consequence: on arrival (every time `ui/portview.lua` is loaded, e.g. via the ledger "city" button) the names of all buildings are on screen and all clickable buildings pulse for 3.9 s. The first hover popup stops it for everyone else (the hovered building keeps pulsing, 1.6).
* Engine: `Port::postChildrenInit` holds the labels 1 s and fades them over the next second (Chocolatier 3's arrival), `StopPulsing` is a logged no-op (`Choc2Natives.cpp:292`). **Discrepancy 4.** Change: hold 3900 ms (first 40 ms tick at or after), hide without a fade, `StopPulsing` hides the labels and ends the intro pulse immediately.

### 1.6 The pulse (FACT structure, INFERENCE for the look)

* A pulse sprite exists only for entries with a `command` (`UpdatePulseTexture` 0x27b24, called from `AddSprite`). It creates a texture of the building's size
  (`TTexture::Create(w, h, true)`), fills it with colour (1,1,1,1), then renders the building's own texture centred into it (`BeginRenderTarget`/`EndRenderTarget`).
  `TGLTextureData::FillRect` with a texture fills that texture, so the pulse picture is the building drawn over an opaque white rectangle. **INFERENCE:** the glow
  is a white box of the building's size with the building on top; per section 3.2 the render target in mode 4 writes only the building's alpha back, so the picture is a white silhouette, not a box.
* Draw (before the building itself, so behind it): shown when the building is the hovered one (`+0x48`) **or** the intro flag is set, and it has a `command` and a pulse sprite.
  Phase `p = 1 - |(clock mod 1300) / 650 - 1|` (a triangle 0..1..0, **period 1300 ms**; constants 1300, 650 and 20.0 read from the data section), where `clock` is the **scene clock `+0x40`** for the intro and the **hover clock `+0x44`** for the hovered building (reset to 0 each time the hover changes to a building with a `command`).
  Drawn with alpha **0.75** at the building's position and scale, with x scale multiplied by `1 + p * ((w*s + 20)/(w*s) - 1)` and y scale likewise with `h`, i.e. it grows to 20 px wider and taller (10 px a side) at the peak.
* Engine: a C3 style highlight child (alpha/scale triangle of 3000 ms, growth `min(1.2, (w+50)/w, (h+50)/h)`), only on hover. Change: period 1300 ms, +20 px, alpha 0.75, behind the building, on every building with a `command` for the intro (until `StopPulsing`) and on the hovered one.

### 1.7 Hover popup (FACT, `OnMouseMove` 0x2782e and 2)

* Skipped entirely if the Lua global `gActiveBuilding` is non-nil (a building dialog is open); the hovered pointer is cleared and the call returns 0.
* On a hit: a `TPortPopupTest(port, sprite)` and a `TPopupWindow(test)` are created and added as a child of the Port; `SetContents(contents)` runs the Lua string
  inside the popup (this is where `NameRollover()` runs, builds the rollover and calls `StopPulsing`). If the sprite has a `command` it is copied into the popup and the hover pointer/clock are set (clock reset only when the hovered sprite changed).
* Position: if the sprite's `labelx` and `labely` are both > -501 (0x1f5) the popup goes at **(labelx, labely) exactly** (treated as screen coordinates, **not** offset by `xOffset`/`yOffset`); otherwise at the pointer converted to screen coordinates. Then `PositionPopup` (2). The popup is added as a mouse listener and the call returns 1 (consumed).
* Engine: opens at the label anchor plus `xOffset_`/`yOffset_` (`Port::addSprite` adds the offset to `labelX/labelY`); identical at 800x600 where both are 0. UNKNOWN whether the original really misplaces popups on other resolutions.

## 2. `TPopupWindow` (the building hover popup; shared with other rollovers)

All FACT (0x26562 to 0x269a2):
* `SetContents` runs the Lua string (`DoLuaString`), `FitToChildren`, sets every child's fade alpha to 0, resets the popup clock, `StartWindowAnimation(50 ms)`.
* `OnTaskAnimate`: clock += 50; fade `a = min(1, clock/125)`; applied through `TFader`. **Fade-in 125 ms in 50 ms steps (0.4, 0.8, 1.0).** The fader sets: Text alpha, Image alpha, **Rectangle alpha = a * 0.8** (so a Rectangle's own Lua `alpha` is overridden: the popup background never exceeds 0.8), Frame alpha = a. Engine: fade 125 ms (`RolloverWindow::kFadeMs`); the 0.8 cap is not visible in `ui/Draw.cpp`/`setSubtreeAlpha` The cap applies to every popup (TPort hover and every `Rollover`), see section 3.3.
* `PositionPopup(x, y)`: popup rect placed at (x, y); `maxX = screenW - 4`, `maxY = screenH - 4`; if the right edge exceeds `maxX` the popup is shifted left by the overshoot **and moved down 32 px**; then if the bottom exceeds `maxY` it is shifted up by the overshoot. Engine's `PopupWindow::reposition` is identical (margin 4, +32).
* `OnMouseMove(p)`: if a keep-visible test exists, convert to screen and ask it; if false the popup is killed and the event passes on (returns 0); otherwise returns 1 (consumed). The test for the port is `CheckSpriteRollover` (alpha test of 1.3), so moving off the building's opaque pixels closes the popup.
* `OnMouseDown` returns 1. `OnMouseUp` kills the popup and, if it has a command, runs it (any release, no check that the press began on the popup). Engine requires the press on the popup (`pressed_`). Low impact.
* `KillPopup`: remove mouse listener, tell the owner, hide, `SafeDestroyWindow`.

Because a mouse listener sees every mouse event, a click anywhere while the popup is up counts as a click on the building (the last move was necessarily over it).

## 3. `TCharacter` (window type `Character`)

### 3.1 Loading (FACT, `PostChildrenInit` 0x5406)

Reads the `char` table: `name` (default "error", stored at `+0x30`), `appearance` (default = `name`) -> `+0x34 = "characters/" + appearance`, body `bsgTTextureGet(+0x34, "temp/temp_char")` at `+0x38`.
Then **`kDefault`** (default `+0x48`, constructor sets 50) -> `+0x48`; **`kAngry`** (default `+0x4c`, constructor sets 50) -> `+0x4c`; **`mood`** (default 50) -> `SetMood(mood)`. The window is enlarged to at least the body texture size (`SetWindowSize(max)`).
The values come through `lua_gettable`, so they follow the metatable: `LCharacter:new` does `setmetatable(t, LCharacter); LCharacter.__index = LCharacter`, so `char.kDefault` is `LCharacter.kDefault = 50` and `char.kAngry` is `25` (`sim/character.lua:17-21`). **`kHappy` (70) is not read.** (INFERENCE that `GetTable` returns the character's own table with its metatable; the Lua passes `char=self`, `sim/character.lua:86`.)
Engine: reads only `name` and `mood`, always loads `characters/<name>`; no `appearance` (**Discrepancy 2**: 6 definitions use it, `ports/newyork.lua:14-19`: `gambler` x2, `lender`, `traveler03`, `villain02`, `villain03`).

### 3.2 Mood faces (FACT, `SetMood` 0x500c)

`name = "characters/<appearance>"` plus: `mood <= kAngry (25)` -> `_displeased`; else `mood < kDefault (50)` -> `_neutral`; else `_pleased`. The face texture is `bsgTTextureGet(<that name>, "temp/error")` (always has a value; a missing face file yields the error texture, not nothing). **No fade, no timer:** `SetMood` replaces the face pointer and invalidates the screen. `SetCharacterMood(name, mood)` (the Lua native, `sim/character.lua:190`) calls `SetMood` on every live Character window whose `+0x30` name equals the first argument, so open dialogs update at once. Initial face = the table's current `mood`.
So the mapping with the shipped constants is: **<= 25 displeased, 26..49 neutral, >= 50 pleased**; every character starts at 50 (`defmood`) and so starts pleased.
Engine: `kHappy = 70`, 200 ms cross-fade (`MoodFade`), faces only used when all three PNGs exist. **Discrepancy 1.** Change: threshold 50, immediate swap, and see UNKNOWN for characters without face files.

### 3.3 Drawing (FACT, `Draw` 0x5194)

Body: texture centred in the window's own (0,0) origin box (`draw at (w/2, h/2)` of the *body* texture). Face: drawn centred at `(faceW/2, faceH/2)`, i.e. **top-left of the face at top-left of the body** (the exact placement 8.1 inferred). It then calls the normal `TWindow::Draw`. The draw ignores the window's fader alpha (a Character is not faded by an enclosing fade); engine applies `ctx.alpha`.

## 4. Sprites in the scene that are not buildings: summary for implementers

* A sprite's `name`, `labelx/labely`, `contents`, `command` only matter for buildings. A table with `contents` but no `command` is hoverable (popup shows) but not clickable and does not pulse.
* The player-logo sign and the Baumeister sign are ordinary entries (`.LOGO`, `scale`, `command`); their `labelx = x-120, labely = y-10` come from the Lua (`sim/port.lua:64-112`).
* `sf_darksky` and the sky for the bridge are Lua-side siblings (`ui/portview.lua:77-85`).

## 5. Map travel vehicle (`TMapTravel`) (FACT)

(For the "vehicle icon rotation and art facing" item.) `Init` 0x23b3c loads `image/traveldot`, reads `xOffset/yOffset`. `InitiateTravel(leg)` 0x242fc: leg clock = 0, weeks done = 0, **total leg duration = `weeks * 2000` ms**, curve from the leg table moved by the offsets.
`TravelOneWeek(type)` 0x24410: creates the sprite once; loads `image/icon_<type>`; plays `DoAnim`; sets the week end to `weeksDone * 2000 + 2000`; sets `travelling`. Ambient by type: `train` -> `sfx/saloon_loop.ogg`, `ship` and `raft` -> `sfx/seaside_loop.ogg`, `airplane` -> silence (empty), `hiker` and `camel` -> `sfx/farm_loop.ogg`, `canoe` -> `sfx/remote_loop.ogg`; a one-shot sound named after the type (`train`, `ship`, `airplane`, other types such as `boat`) plays when the type changes from the previous call; for raft/hiker/camel/canoe the one-shot is cleared.
`OnTaskAnimate` 0x239ae (40 ms): `clock += 40`; `f = min(1, clock / total)`; position and tangent from `GetPoint(f)`; the **same mirror rule as 1.4(5)**: flag 1 (mirrored) iff `acos(tangent.y)` with the sign set by `tangent.x > 0` is `< 0` or `>= pi`; **no rotation, no frames**. A trail dot is appended when `clock` is a multiple of **160 ms** (first dot at 160). The week ends when `clock >= weekEnd`: weeks done++, `travelling = 0`, the Lua coroutine is resumed directly (`TScript::Resume`).
`Draw` 0x238c8: all dots (`image/traveldot` centred), then the vehicle **only while `travelling`**.
Engine (`MapTravel.cpp`, `TravelWeek.cpp`): 30 ms tick, dots by arc length every 12 px starting at 0, vehicle never mirrored (`INFERRED ... do not rotate`), vehicle stays visible between weeks. Per-type ambient sounds: not found in the engine file (UNKNOWN whether handled elsewhere). The images are static PNGs (`icon_ship/train/airplane/raft/hiker/camel/canoe.png`), art direction facing left by the same convention as port vehicles (INFERENCE from the flag rule).

## 6. `StartProductionAnim` / `StopProductionAnim` (FACT)

They are **not port natives**. `TFactoryStatus::AnimateProduction` (0x13226) sets a static byte `animating = 1`; `StopProduction` (0x13232) sets 0. `TFactoryStatus` is the Ledger's factory panel (window type for `ui/ledger_factories.lua`), and `Draw` (0x1323e) reads the flag:
the panel keeps a clock (`+0xcc`) that advances every tick whether or not animating; per factory entry, when `animating` is set and the entry's period (entry `+0x20`, for the picture) is non-zero, with `f = (clock mod period) / period`: the product picture's y interpolates from its own base y towards the panel's `+0xc8` y by `f` and its alpha is `0.5 f + 0.5`; the machine/gear picture (the second loop, period entry `+0x20` of that record) rotates by `2 pi f`. When not animating the product picture is drawn static at alpha 1 and the gear at rotation 0. An entry with period 0 gets a blinking overlay picture (`+0xc0`, y scale 0.65) when `(clock mod 1000) / 500 == 1`. Field meanings are only partly decoded (see Unknown).
Lua callers: `sim/travel.lua:72,82,84,87,89,109,113` (start when a trip begins and after each week, stop around disaster/meeting dialogs and at the end of the trip) and `ui/portview.lua:121`, `ui/mainmenu.lua:232` ("just in case" stop). So the factory pictures animate **only while the player is travelling**.
Engine: both are no-ops that log once (`Choc2Natives.cpp:293`). Discrepancy 7. The per-entry fields are only partly decoded (see UNKNOWN); implementing the flag needs the `TFactoryStatus::Init` layout (0x13c84, not read here).

## 7. Fireworks (`StartFireworks`) (FACT)

`StartFireworks` (0x271e2): records `+0x60 = number of sprites added so far` and creates **5** `TFirework`s (`glitz/firework.lua`). `Draw` draws them **between sprite `index-1` and sprite `index`** (when the loop reaches the index equal to the stored count). If `Fireworks()` is the last layout item the index is never reached and they never draw. The Lua only calls it when `LQuest:Variable("bcsign") > 5` (`sim/port.lua:Fireworks`), in `sanfrancisco` (`ports/sanfrancisco.lua:42`, between the planes and the ships).
Per firework (40 ms `Update`, 0x26c18): state 0 (waiting) counts a timer down by 40 (initial timer `rand() % 1000`); at <= 40 it moves to state 1; state 1 picks a colour from 6 (`rand() % 6`), plays sound `firework<rand()%6+1>`, resets the timer, state 2 with position `x = rand() % 600 + 100`, `y = rand() % 90 + 10`, `gSparkleTime = 0.1`; state 2 advances the particle system by 40 ms per tick while the timer < 1100 ms (0x44c), then returns to state 0 with a timer of `rand() % 500 + 250`. Drawn only in state 2 at (x, y). The texture is one of 5 (`rand() % 5` of `firework_blue/green/red/white/yellow`).
Engine: implemented. `games/choc2/FireworkSprite.{h,cpp}` drives the shared `game::Firework` (the Chocolatier 3 TFirework, whose timing matches) with `kC2FireworkStyle` (the sound count 6, x in [100,699], y in [10,99]); the five sprites join the port's layer-sorted tree at the recorded layout slot, so they draw exactly between the sprites around the StartFireworks call (the omission when the call is the last layout item does not carry over: the engine draws them wherever they sit). The 40 ms task the port already runs updates them; a fire tick plays sfx/firework<N>.ogg through the port's sound hook (the sixth has no shipped file: "[audio] missing file" once, then silent - as the original's failed load). Chocolatier 2 rebinds `StartFireworks` (`script::rebindRaw`, the boot-trace native list unchanged). Resolved discrepancy 8.

## 8. Quest bar (ledger) and the banner (FACT / one INFERENCE)

* There is **no interplay** between the banner and the quest bar. The banner is `Bitmap{image="ports/banner", x=297+xOffset, y=yOffset}` at the top of the screen with the port name (`ui/portview.lua:111-114`); the `quest` Bitmap is a child of the ledger window, which sits at the **bottom** (`y = yOffset + screenHeight - 310`, `ui/ledger.lua:53`), x=235; they never overlap and no code links them. (The banner is drawn before the ledger, so the ledger is above it in z.)
* `TLedger::OnTaskAnimate` (0x1b058): a static `questY` (initial 800 = "unset"; `PostChildrenInit` sets it to `hideQuestY`; `Reset` sets 800). With the hint string empty it moves towards `hideQuestY` by `(hideQuestY - showQuestY) >> 1` = **26 px per tick** (clamped at hide); with a non-empty hint it moves up by 26 towards `showQuestY` (clamped). Every move sets the `quest` window y on **all** live ledgers and re-sets `quest_text` to the current hint. `SetQuestHint(text)` stores the text and writes it into `quest_text` of every ledger at once.
* Engine: a 500 ms swipe transition (`kQuestSlideMs`). The ledger's tick interval is not visible in `TLedger` (UNKNOWN), so the real slide time is 3 ticks of that interval (0 -> 53 takes ceil(53/26) = 3 ticks).

## 9. `ResetColorMap` (map, noted for completeness)

`TMap::ResetColorMap` (0x230e4) clears the static list of visited port names and releases the colour-map texture (`pColorMap`); `UpdateColorMap` (0x23248) rebuilds it lazily. Not decoded further here.

## 10. Current engine vs binary, per item the task asked about

| Item | Binary | Engine | Match |
|---|---|---|---|
| Sprite hit rectangle | bounding rect incl. children, [l,r) x [t,b) | image rect, no children | partial |
| Alpha >= 192 hit test | yes (4x4 render target) | none | **no** |
| Draw order | one list, call order | one list, call order (`layer = order_`) | yes |
| Hit order | reverse call order, only entries with `contents` | highest layer among buildings | yes |
| Building anchoring | centre of `(x+xOff, y+yOff)`, scale 1 (logo: `scale`) | same | yes |
| Hover popup position | `(labelx,labely)` raw screen coords, else pointer; flip rule; +32 y | `(labelx,labely)+offset`; same flip | yes at 800x600 |
| Popup fade/keep/click | 125 ms, alpha 0.8 on Rectangles, release runs command | 125 ms, 1.0, press on popup needed | mostly |
| Pulse/rollover | intro (all, 3.9 s, 1300 ms, +20 px, 0.75) then hover | hover only, C3 3000 ms | **no** |
| Building labels | shown to 3.9 s, hidden hard | 1 s + 1 s fade | **no** |
| Path default `time` | 10000 | 15000 | **no** |
| `hold` | start wait | end/both-end wait | visual equivalent after random phase |
| `bounce` | round trip in `time` | `2 x time` | **no** |
| `htile` | tile at texture width over the window | same (4 copies) | yes (scale 1) |
| `random` start phase | `rand() % (time+hold)` | uniform in `time+hold` | yes |
| Perspective | `near=400 far=0`, linear, clamp >= 0 | `yNear=800` default, unclamped, `scale` overrides | yes (paths, statics and buildings, clamped; this row predates the fixes - see 3.1) |
| Art facing / flip | mirror when moving right (acos rule) | same (`artFacesLeft`) | yes |
| `hflip` | mirrors | ignored | **no** |
| Character faces | 25 / 50, instant | 25 / 70, 200 ms fade | **no** |
| Character `appearance` | used | ignored | **no** |
| Start/StopProductionAnim | flag for ledger factory panel | no-op | **no** |
| StopPulsing | hides labels, ends intro pulse | no-op | **no** |
| StartFireworks | 5, positioned in layout | stub | **no** |
| Quest bar | 26 px per tick | 500 ms swipe | feel |
| Banner vs quest bar | independent | independent | yes |

## Unknown

* Characters with no face files (`assets/characters` has 245 files: 45 characters have all three faces, which leaves about 65 body-only characters): `bsgTTextureGet(.., "temp/error")` is used for the face with no check, so these draw the **error texture** over the body unless the texture loader maps a missing file to a blank. `temp/error` is not a file under `assets/`; where it comes from (built-in placeholder?) was not found. The engine draws no face for them. UNKNOWN which is right in play.
* Texture filtering: `Port::Draw` calls `SetFilteringMode(1)` once; the meaning of the value (bilinear?) was not decoded.
* Whether `TCurve::GetPoint` parameterises by arc length (assumed, as in the engine's `Curve::at`); `TCurve::Subdivision` exists in the binary.
* The ledger tick interval (quest bar slide time) and where `TLedger`'s animation is started.
* `TFactoryStatus` field meanings for the production animation (6): only the structure of the draw was read.
* How `bsgTTextureGet` treats an `.xml` name in `AddSprite` (animated texture assumed) and the anchor of an animated frame (centre assumed).
* Whether the original misplaces hover popups when `ui.xOffset/yOffset` are non-zero (labelx/labely are used raw).
* Which of `rand()`'s seeding `bsgRandomSeed` performs; the scene's phases are only reproducible if that is a fixed seed.

## 3. Port pulse, label rule and other details

Sources: `re/choc2/decomp/TPort.c` (`AddSprite` 0x27f34, `Draw` 0x288b6, `UpdatePulseTexture` 0x27b24), `TGLDisplayHandler.c`
(`BeginRenderTarget` 0x93884, `EndRenderTarget` 0x94bda), `_anonymous_namespace_.c` (`CopyBackAlpha` 0x92d3c), `TGLTextureData.c`
(`FillRect` 0xb80a4), `TPopupWindow.c` / `TPopupWindow__TFader.c`, `TRollover.c`, `TRectangle.c`, plus the Mac Lua under
the Mac app bundle's `Contents/Resources/assets/`.

### 3.1 Perspective on static (non-path) sprites

**FACT.** Perspective is applied in `TPort::Draw`, not in `AddSprite`, and the code is outside the `if (path)` block, so it runs for every sprite with a
texture, path or not. `AddSprite` only stores the numbers: `scale` (default 1) is copied to both `nearscale` and `farscale` defaults (`GetNumber("nearscale", scale)`, `GetNumber("farscale", scale)`), `near` (default 400), `far` (default 0), both converted to int. The sprite position is `(x + xOffset, y + yOffset)` (`this+0x38/0x3c`) and is written to the sprite at +0x38.
In `Draw`, for each sprite: position = the sprite's stored position (static) or the curve point (path); `s = nearscale` initially; **if `nearscale != farscale`**:
`s = max(0, farscale + (nearscale - farscale) * ((y - far - yOffset) / (near - far)))`, with `y` the position y of this frame (static: the stored y). The `- yOffset` removes the offset that `AddSprite` added to `y` (and `TCurve::Move` added to a path), so the formula is in layout coordinates. Linear, clamped at 0 below only. If the two scales are equal, `s = nearscale` (= `scale`, since `nearscale` defaults to it; an explicit `scale` together with explicit near/far scales is not combined, the near/far ones win in the stored fields).
The result `s` goes into `TDrawSpec(pos, alpha, s, flags)` as the uniform scale for the sprite; the htile branch draws all copies with the same spec scale (period = raw texture width, unscaled).
Engine: implemented for paths, static sprites AND BUILDINGS alike (`games/choc2/PortScene.cpp` reads the keys for every
kind and applies the formula at adapted-build time): `s = max(0, farscale + (nearscale - farscale) * (y - far)/(near - far))` when
`nearscale != farscale`, else `nearscale` (which `scale` feeds as the default); default `near` 400 (not 800), `far` 0; a later `scale` no
longer overrides explicit ends (the `sanfrancisco` airship case), and a building's explicit `scale` is honoured (the old engine hard-coded
1 for non-logo buildings). The buildings' y is the layout y, computed before the Port's offset - as AddSprite does and Draw un-does. No
shipped building carries the keys (a scan of every port layout: buildings only ever set name/character/ambient/x/y/labelx/labely), so this
is faithfulness for third-party content, not a shipped-data change.

### 3.2 The pulse picture: exactly what it draws

**FACT** (GL state decoded from the float-typed constants in `BeginRenderTarget`, mode 4; the decompiler passes the GL enums through float registers: 8448.0 = 0x2100, 5890.0 = 0x1702, 770.0 = 0x302, 34167.0 = 0x8577, 768.0 = 0x300, 7681.0 = 0x1e01, 34166.0 = 0x8576).
`UpdatePulseTexture` (once per sprite with a `command`):
1. Create an RGBA texture the size of the building with the has-alpha flag set.
2. `FillRect(whole, colour (1,1,1,1))` on it: the texture is **opaque white** (this CPU-side fill keeps alpha 1 when the has-alpha flag is set).
3. `BeginRenderTarget(texture, mode 4)`. Mode 4 is special: it clears the framebuffer to (0,0,0,0) and sets the fixed-function texture environment to **COMBINE**: RGB = MODULATE(texture **alpha**, primary colour), alpha = REPLACE(constant, env colour alpha 1). I.e. whatever is drawn comes out as `rgb = building alpha x white`, `a = 1`.
4. Draw the building texture at the centre of that target, scale 1, no rotation. The framebuffer is now **a greyscale alpha mask**: white where the building is opaque, grey where partly transparent, black elsewhere, alpha 1 everywhere.
5. `EndRenderTarget`: for mode 4 it does **not** copy the colour into the texture; it calls `CopyBackAlpha`, which reads the framebuffer back (BGRA) and writes **only the alpha channel** of the texture from the framebuffer (the first byte of each read pixel, the mask value). It then restores the texture environment to MODULATE.
**Result: a pure white texture whose alpha channel equals the building's alpha** — a flat white silhouette of the building, the same size as the building (not a white box: the box would only appear if alpha were not copied back; it is, so the outside of the building is transparent). The original white fill is exactly what survives in the RGB channels.
Draw (unchanged from 1.6): this silhouette, alpha 0.75, scaled up so it is 20 px wider and taller at the peak of the 1300 ms triangle, drawn behind the building. So the look is **a white glowing outline/halo around the building that breathes**, with the pure-white silhouette showing mostly as a rim outside the building (the building covers its centre).
A plain building scaled at alpha 0.75 has the same shape; the only difference is colour: the original silhouette is flat white (RGB forced to 1,1,1, texture alpha kept), the plain building keeps its own colours. INFERENCE for the final look on screen (blend state not read: standard alpha blending is assumed), but the construction above is FACT.
**Engine:** the pulse image is the building's texture with RGB set to white and the original alpha (a white silhouette), drawn at alpha 0.75 behind the building with the 1.6 scale and period (`PortWindow.cpp`). It is not a white fill with the building drawn over it: the picture is a silhouette, not a box.

### 3.3 The 0.8 Rectangle alpha cap: who it applies to

**FACT.** The cap is in `TPopupWindow::TFader::Process` (0x264b4): for every descendant of a popup window, `TRectangle` gets `alpha = fade * 0.8`, `TText`/`TImage` get `alpha = fade`, `TFrame` gets `fade` (at `+0x64`), recursively. It is a private nested class of `TPopupWindow`, run from `SetContents` (fade 0) and `OnTaskAnimate` (fade `min(1, clock/125)`, clock += 50 per tick; returns true while fade < 1).
`TPopupWindow` is constructed in exactly two places (search of `re/choc2/decomp`): `TPort::OnMouseMove` (building hover, 1.7) and `TRollover::OnMouseMove` (0x2a4c0: every Lua `Rollover { ... }` widget; the contents string runs in the popup with the same `SetContents`). `TMap` and `TLedger` create none themselves; the map port icons (`ui/map.lua:66`) and the ledger quest bar (`ui/ledger.lua:61`) are `Rollover` widgets, so their popups are `TPopupWindow`s and are capped the same way. `TMainMenu::TFader` (0x2d440) is a different fader: it fades Text and Image only and does not touch Rectangle/Frame.
**So the cap applies to every Rollover/popup in the game**, not just TPort's: all `MakeRollover` users (`ui/uihelp.lua:171`: `Frame{ Rectangle(t) }`) — building names (`sim/building.lua:123` asks for `alpha=1`, the fader makes it 0.8), player logo, items (`sim/item.lua`, Lua alpha .8), factories (.8), quest (.8), medals, map port targets, lab game (`ui/lab_game.lua`, Lua alpha .75, becomes 0.8 at full fade). The Lua alpha on the Rectangle is ignored for popups; the end value is always 0.8 (so the Lua values 0.75/0.8 are cosmetic and the building name's 1 is capped).
Non-popup Rectangles are unaffected: `TRectangle::Draw` (0x29ad4) draws with its own `alpha` field (+0xc0, from the style key `alpha`, default the colour's alpha): alpha exactly 1.0 uses an opaque `FillRect` and then `TWindow::Draw`; 0 < alpha < 1 draws a blended quad of the colour with that alpha; alpha <= 0 draws nothing (e.g. `haggle.lua` white .25 rectangles).
**Engine:** the popup fade is applied in one place for all popups (`InputWidgets.cpp`): over 125 ms in 50 ms steps (0.4, 0.8, 1.0), Rectangle alpha `fade * 0.8` whatever its Lua alpha, Frame, Text and Image alpha `fade`. Rectangles outside popups keep their own alpha.

### 3.4 Other items

* `TText` label rule (also in the ledger doc 12.3): `#` stripped at `Init`, non-`#` labels looked up in the string table (0x5cc82); runtime setters are literal (inferred).
* `Port::Draw` order of operations for a sprite is: position and perspective scale/flip -> store draw spec -> pulse (if hovered or intro) behind -> the sprite (htile: x reduced by the width while > w/2 and repeated). Static sprites follow the same code, so a `hflip` on a static sprite gives flag 1.
* Static sprite hit-testing and `htile` periods: unchanged from 1.3/1.4.

### 3.5 Unknown

* The exact blend function while `CopyBackAlpha`'s source is drawn (assumed standard alpha blending, harmless because the constant alpha is 1).
* The name/bit of the byte read by `CopyBackAlpha` (the first byte of a BGRA 8888-rev read is read as blue, which equals the mask in all three colour channels, so the choice does not matter).
* `str::strspn` character set in the `TText::Init` label rule (which strings skip the string table).
