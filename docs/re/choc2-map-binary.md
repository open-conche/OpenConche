# Chocolatier 2 map, travel, `Frame`, credits, intro and options: what the Mac binary says

Source: the Chocolatier 2 Mac app bundle's `Contents/MacOS/Chocolatier2`, i386 slice, unstripped; Ghidra output in `re/choc2/decomp/`
(git-ignored): `TMap.c`, `TMapTravel.c`, `TCurve.c`, `TRollover.c`, `TFrame.c`, `TCredits*.c`, `TMainMenu*.c`, `TOptions.c`,
`TGLDisplayHandler.c`, `_anonymous_namespace_.c`. Same method and labels as `docs/re/choc2-port-binary.md` (which already covers `TMapTravel` in its
section 5; this doc covers the remaining classes). Constants Ghidra hid were read from the i386 slice with `otool -tvV` (`re/choc2/Chocolatier2_i386`).

**Chocolatier 3 findings (the question asked).** The Chocolatier 3 Mac binary has none of these classes. It has no `TFrame` (its frames are sprite
sheets, `BSGWindow.frame`), no `TMap`/`TMapTravel` (`TravelTask`, a different event-list design), no `TRollover`, and its `TCredits`/`TOptions` are
different code. So the Chocolatier 2 Mac decompile is the authority below, not Chocolatier 3. Chocolatier 1's `TFrame` (`docs/re/choc1-natives.md` 3) is a
different class again: six hard-coded images, no `corner/top/left/ornament/alpha/empty` keys.

Labels: **FACT** = read from the binary (address = hex VA in the i386 slice), **INFERENCE** = reading around a gap, **UNKNOWN**.
Engine comparisons were by reading code; nothing was run.

**Status: DONE in the engine** - map/travel A1 (colormask, inference: max alpha, alpha always rewritten), A2, A3 (Chocolatier 2 only, via
`World::setForeground`), A4, A5 (`WindowManager::setLegacyRollover`), A6, and `Frame` B1-B4 (ornament origin still INFERENCE). Decisions are listed in
the workbench decision log "Decisions for review". Not done here (other areas): intro, main menu art, credits, options defaults. A7 needed no change.

## 0. Ranked summary

| # | Area | Binary | Engine now | Visible as |
|---|---|---|---|---|
| 1 | Intro (c) | No scroll anywhere. A 5-state machine: all menu art fades out 1000 ms, `intro_text` fades in 1000 ms, holds 22000 ms **static**, fades out 2000 ms, then queues `MainMenuFinishIntro` | 30 s upward crawl of the text, only `fade_buttons`/`fade_text` fade | the intro looks and lasts differently (26 s, text still) |
| 2 | Main menu art (c) | `TMainMenu::Draw` animates 4 searchlight beams, a blimp (25 s pass) and two skyline pictures over the menu | none of it drawn | menu is missing its animated background |
| 3 | Credits (c) | Pages, not a scroll: 300 ms fade in, 4000 ms hold, 500 ms fade out, `---` separates pages, sizes 20/30/40 | scrolling, 18 px, 40 px/s | the whole credits dialog |
| 4 | `colormask` (a) | Texture = `image/map_color.jpg` pixels with alpha rewritten to the union of the visited ports' `map_mask_<port>` alphas | not drawn | visited regions never turn to the colour map |
| 5 | Map rollover defaults (a) | `Rollover` `xOffset` defaults to **20**, `rolloverSound` plays when the popup opens, optional `area` rect list | `xOffset` 0, no sound, no `area` | every pointer-anchored popup (inventory, recipes, lab) sits 20 px too far left |
| 6 | Travel sounds (a) | per-type ambient loop and a one-shot named after the type | none | silent trips |
| 7 | Map Escape (a) | Escape on the map runs `DoModal('ui/pausedialog.lua')` | no handler found | Escape does nothing on the map |
| 8 | `Frame` ornament (b) | ornament bottom-aligned to the window's top edge (occupies the 33 px above it) | centred on the 15 px top band (9 px lower) | the banner on every dialog |
| 9 | Options defaults (c) | the three volume sliders default to 750 | 750/650/500 | first-run slider positions |
| 10 | `Frame` fill (b) | fills exactly the window rect | fills the outer rect inset 5 (up to 14 px outside the rect) | faint |

## 1. Map and travel (a)

### 1.1 `TMap` (window type `Map`) - 0x22e98..0x23860

All FACT unless marked.

| Item | Binary |
|---|---|
| Ctor 0x23104 | registers a texture-refresh callback; sets the class-static `pCurrent = this`. Dtor clears it |
| `Init` 0x22f4e | reads the style table `ports` (the Lua `AppendStyle{ports=LPort._ByName,...}`); keeps the **name** of every port with `available` **and** `visited` true in a static list `visited`; makes itself the modal's default focus. The style keys `routes` and `current` are **not read** |
| `PostChildrenInit` 0x23752 | if the static `pColorMap` is empty, `UpdateColorMap()`; finds the child named `colormask` (an image window) and gives it `pColorMap`; if `bsgCheckConfig("dev")`, runs `dev/dev.lua`; starts a 40 ms window animation |
| `OnTaskAnimate` 0x22eac | only invalidates the screen (the map has nothing of its own to animate; the 40 ms task makes the port icons' animated sheets redraw) |
| `OnMouseDown`/`OnMouseMove` | return 0 |
| `OnKeyDown` 0x22ed6 | Escape runs `DoModal('ui/pausedialog.lua')`; every key returns 1 |
| `OnMessage` 0x23688 | message **2000**: `RefreshTextures` (`UpdateColorMap` again and re-attach). **1001**: stop the window animation and, if `TMapTravel::StillTravelling()`, open the pause dialog (`DoModal('ui/pausedialog.lua')`). **1000**: restart the 40 ms animation. 1000/1001 are `kUserMessageBase` + 0/1; INFERENCE: app activate/deactivate (a trip is paused when the window loses focus), 2000 = textures lost |
| `ResetColorMap` 0x230e4 | clears the `visited` list and drops `pColorMap` |

`UpdateColorMap` 0x23248 (runs only if `visited` is non-empty):
1. Loads the texture `image/map_color` with flags 2 (the render-target-capable kind) - its pixels are `map_color.jpg` (800x600).
2. `BeginRenderTarget(texture, mode 4)`. Mode 4 (`TGLDisplayHandler` 0x93884) sets a GL combine texture environment (RGB = mask alpha x vertex colour, alpha constant) and clears to (0,0,0,0).
3. Fills the whole texture rectangle with (0,0,0,0).
4. For each visited port name draws `ports/map_mask_<name>` (texture fetched with fallback `temp/error`) centred at (width/2, height/2) with scale 1.
5. `EndRenderTarget` in mode 4 copies the pixels back with `CopyBackAlpha` (0x92d3c): it reads the framebuffer (BGRA) and writes **only the alpha byte** of the texture from it, and **only if the texture `HasAlpha`**. RGB stays the JPEG's.
Result (FACT as the sequence, INFERENCE as the picture): `colormask` shows the shaded-relief `map_color.jpg` through an alpha equal to the union of the visited ports' masks (`ports/map_mask_*.png`: white RGB, alpha 255 only over that port's region). This confirms the mechanism guessed in `docs/re/choc2-port-and-ledger.md` 8.4.
Lua: `ui/map.lua` builds `Bitmap{name="colormask", w=kMax,h=kMax}` as a child of `Bitmap map`; `sim/travel.lua:162` (`SetPort`, first visit) and `ui/mainmenu.lua:233` call `ResetColorMap()`. Because `pColorMap` and `visited` are class statics, the picture is rebuilt only when the next `Map` is created after a reset (the colour does not change during a trip; the arrival port is added the next time the map opens).

UNKNOWN: how overlapping mask alphas combine (the GL blend function is not in `UpdateColorMap`; the framebuffer clear and the mode-4 env are all that was found), whether the JPEG texture really reports `HasAlpha` (if not, `CopyBackAlpha` does nothing and the whole thing is inert - then why the code exists is the argument that it does), and the vertical flip of the read-back.

Engine now: `src/game/choc2/Map.cpp:3-22` reads `ports`/`routes`/`current` into details only; `Map.h:21` says `colormask` draws nothing; `Choc2Natives.cpp:327` makes `ResetColorMap` a logged no-op.

### 1.2 `TMapTravel` (window type `MapTravel`) - re-check of `choc2-port-binary.md` 5

Section 5 of that doc holds for `TMapTravel`. Additions:

| Item | Binary (FACT) | Engine now |
|---|---|---|
| Week length | leg total = `weeks x 2000` ms (`InitiateTravel` 0x242fc, weeks read as a float, clamped to uint); week end = `weeksDone x 2000 + 2000` (0x24410) | `TravelWeek.h:17` `kWeekMs = 2000`; `MapTravel.h:27` still says "INFERRED", it is now FACT |
| Tick | 40 ms; the clock is **tick-counted** (`clock += 40` per call, not wall time) so 50 ticks make a week | real elapsed time on the modal clock, 40 ms tick (`MapTravel.h:30`); same on a steady machine |
| Position | `f = min(1, clock/total)` where the clock is **not reset between weeks** (only by `InitiateTravel`), `GetPoint(f)` | `WeekMover` from/to fractions; same |
| Dots | `clock % 160 == 0` after the position is set, so the dot sits where the vehicle is; first at 160 | `TravelWeek.cpp:31-38` same |
| Two-leg routes | the Lua calls `InitiateTravel(leg)` per leg: clock 0, weeks-done 0, new total, curve rebuilt and moved by (xOffset,yOffset). **The dot list is not cleared**, so leg 1's trail stays; the dot phase restarts at 0 for leg 2 | `initiateTravel` keeps the dot sprites (`MapTravel.cpp:56-66`), `WeekMover::begin` resets the counter. Same |
| Vehicle sprite | one `TAnimatedSprite`, created on the first `TravelOneWeek`, **reused across legs**; its clock is the parent modal's (`+0x50`); texture swapped to `image/icon_<type>` on every call; plays animation `DoAnim`; drawn only while `travelling` | recreated per leg, hidden between weeks. No visible difference |
| Week end | `clock >= weekEnd`: weeks-done++, `travelling = 0`, resume the Lua coroutine directly (`TScript::Resume(0)`), task ends (returns `travelling`) | `onAnimate` returns false; Lua's `Pause(1900)` then `while StillTravelling() do Yield() end` is the same dance |
| Start position | `TravelOneWeek` also sets the vehicle position and flip from `GetPoint(...)` before the first tick (argument lost by Ghidra; INFERENCE: the current fraction) | `place()` at the week's start fraction |
| Sounds | see 1.4 | none |

`Draw` 0x238c8: every dot as `image/traveldot` (8x8) drawn at its point with no offset (the curve was moved at `InitiateTravel`), then the vehicle only while `travelling`. The engine's root-sprite offset by `xOffset/yOffset` (`MapTravel.cpp:19-21`) is equivalent.

### 1.3 `TCurve` (the vehicle path)

`TCurve::GetPoint` (0x879e) walks the subdivision list: it multiplies `t` by the total length (`GetLength`, which runs `DoSubdivision` lazily when there are >= 4 control points), subtracts each piece's length until the remainder fits, then interpolates linearly between the piece's endpoints; the tangent is that piece's direction, normalised. (FACT.) This is arc-length uniform along the flattened polyline and is exactly `src/gfx/Curve.cpp:46-72`. It resolves the open item "arc-length uniformity of curves" in the workbench decision log (4). The subdivision threshold (0.2) and Bezier-chain layout (3n+1 points) are INFERENCE for Chocolatier 2: `Subdivide` is 922 bytes in both binaries (0x82f4 here, 0x1c578 in Chocolatier 3), which is the evidence they are the same code.

### 1.4 `TravelOneWeek(type)` sounds (FACT, 0x24410; not in the engine)

Ambient (`TChocoSound::SetAmbient`): `train` -> `sfx/saloon_loop.ogg`; `ship` and `raft` -> `sfx/seaside_loop.ogg`; `airplane` -> `""` (silence); `hiker` and `camel` -> `sfx/farm_loop.ogg`; `canoe` -> `sfx/remote_loop.ogg`; any other type (`boat`, the `DefaultRoute` default) leaves the ambience unchanged. One-shot (`PlaySound(type)`): only when the type string differs from the previous call's (`+0x38`), and cleared to empty for `raft`, `hiker`, `camel`, `canoe`, so the one-shot plays for `train`, `ship`, `airplane` (files `sfx/train.ogg`, `ship.ogg`, `airplane.ogg` exist; `airship.ogg` is not referenced here) and would try `boat`. The routes in `ports/portroutes.lua` use `ship` 54, `train` 13, `raft` 9, `camel` 3, `hiker` 2, `canoe` 1, plus `airplane` (airship quest). The ambience is restored by `LPort:SetAmbient` when the next port/map view builds (`choc2-audio.md` 4).
Engine: `grep seaside_loop src` is empty; nothing in `MapTravel.cpp` plays sound.

### 1.5 Port-icon rollover tags

The tag is Lua (`LPort:MapRollover`, `sim/port.lua:233-290`: a `MakeDialog{Bitmap{image="image/traveltag", hflip=...}}` with the label text, flipped when `mapx + 210 >= screenWidth`), shown by a generic `Rollover` widget (`TRollover`, 0x29ec8 Init, 0x2a4c0 `OnMouseMove`). The Lua is run by the engine as is. The widget itself (FACT):

| Key | Default | Use |
|---|---|---|
| `xOffset` | **20** | popup x = pointer screen x + xOffset |
| `yOffset` | 0 | popup y = pointer screen y + yOffset |
| `xAbsolute`, `yAbsolute` | -1 | used instead of the pointer value when >= 0 (the map's icons set both, so a negative `yAbsolute` falls back to the pointer) |
| `hotSize` | 20 | read and stored, **never used** by `OnMouseMove` |
| `rolloverSound` | "" | `LoadSound` at init; played when the popup opens |
| `area` | none | list of `{x,y,x2,y2}` rects (4 numbers each, tested as left <= px < third, top <= py < fourth); when present the popup opens only inside one; else anywhere in the window |
| `contents`, `command` | | as in Chocolatier 3 |

`PostChildrenInit` fits the window to its children. The class static `rolloversEnabled` starts at 1 and nothing writes it (the Lua never calls `EnableRollovers`). Then: new `TPopupTestRect` from the window rect, new `TPopupWindow`, adopted into the top modal, `SetContents(contents)`, `PositionPopup`, command handed to the popup, mouse listener, sound.

Engine: `ui/widgets/InputWidgets.cpp:551-554` reads `xOffset` default **0**, no `hotSize`, `rolloverSound` or `area`; `onMouseMove` (`:620-630`) otherwise matches. The Chocolatier 3 Rollover also defaults to 0 (`docs/re/design-questions.md` 172), so this must be gated on the game.

### 1.6 Engine changes: map and travel

| # | Change | Label |
|---|---|---|
| A1 | Draw `colormask`: build a texture from `image/map_color` RGB with alpha = the union (use max or "over"; blend is UNKNOWN) of `ports/map_mask_<port>` alphas for every port with `available and visited` in the `ports` style table; rebuild only when a `Map` is created after `ResetColorMap`; `ResetColorMap` clears the list. Needs a pixel-composing texture path (the existing note in `Map.h:21`) | FACT (sequence), INFERENCE (blend) |
| A2 | `Map::onKeyDown`: Escape runs `DoModal('ui/pausedialog.lua')`; return true for every key | FACT |
| A3 | App deactivate (message 1001) while `StillTravelling()` opens the pause dialog and stops the map's animation; activate (1000) restarts it | INFERENCE (message meaning), FACT (behaviour per id) |
| A4 | `TravelOneWeek`: set ambience and one-shot per 1.4 | FACT |
| A5 | `Rollover`: `xOffset` default 20 for Chocolatier 2 (a generation flag like the others in `App.cpp`), play `rolloverSound` on open, support `area` | FACT |
| A6 | Update `MapTravel.h:27` comment: week = 2000 ms is FACT, tick counted | FACT |
| A7 | `Map` should not read `routes`/`current` (it does, for logging only): no change needed | FACT |
| - | No change: dot cadence, trail persistence across legs, mirror rule, hidden between weeks, curve arc-length | FACT |

## 2. `Frame` (b)

`TFrame` (a `TAnimationWindow`; ctor 0x14e34, `Init` 0x14ed8, `Draw` 0x15594). Style keys, all from `styles.lua` `standardStyle` and per-window: `color` (4 numbers, default 1,1,1,1), `corner`, `top`, `left`, `ornament` (texture names, fallback `temp/error` when non-empty and missing; empty name = no image), `alpha` (default 1), `empty` (bool, default false). Default images: `control/frame_corner` 19x15, `frame_left` 19x64, `frame_top` 64x15, `frame_ornament` 128x33; rollover popups use `control/rollover_*` (4x4, 4x64, 64x4) with `ornament=""` and `empty=true`.

Let W,H be the client size, L = left image width, T = top image height, O = ornament height if there is an ornament else T, C = corner size.

**Draw order and geometry (FACT for the sequence and formulas; the coordinate reading is INFERENCE).**
1. Unless `empty`: `FillRect(client rect 0,0,W,H, color)` (the colour's own alpha; not scaled by `alpha`).
2. The viewport is grown: left and right by L, top by O, bottom by T. All following coordinates are in the grown viewport, so the window's rect is the rectangle (L, O, W, H) inside it: **the border is drawn outside the window**, the frame's outer size is (W+2L, H+O+T).
3. Top edge: clip to x in [0, W+L] (right end cut at window x = W). Tiles are centred images stepped by the top image's width, starting with the tile whose left edge is at x = L (window x 0), bottom aligned at y = O (window y 0), continuing while the tile's left edge <= W. Bottom edge: the same tiles flipped vertically at y = O+H (window y = H).
4. Left edge: clip y to [0, O+H], tiles stepped by the left image's height from y = O (window 0) while the top <= H, left edge at x = 0 (window -L). Right edge: flipped horizontally, right edge at W+2L.
5. Clip reset to the grown viewport. Corners (one image, centred draws): TL as is, left edge at window x -L and bottom edge at window y 0; TR flipped horizontally with its right edge at window x W+L; BR flipped both ways; BL flipped vertically with its top at window y = H.
6. Ornament, last: centred horizontally in the outer width, centred vertically on **O/2** in the grown viewport, i.e. its bottom edge touches the window's top edge and it occupies [-O, 0] in window coordinates. Every texture draw uses alpha = the `alpha` key.

Engine now (`game/choc2/FrameWindow.cpp`): border outside the rect, edges tiled from `corner` size (`:15-27`), corners and flips (`:28-31`), ornament drawn last. Differences:

| # | Engine | Binary | Label |
|---|---|---|---|
| B1 | ornament y = `(top.h - ornament.h)/2` in outer coordinates (`:33-34`), i.e. centred on the 15 px top band, 9 px lower than the binary for the default frame (window-relative -24..+9 instead of -33..0) | bottom of the ornament at the window's top edge | FACT (formula), INFERENCE (the origin of the grown viewport) |
| B2 | fill = outer rect inset 5 px (`frameFillRect`, `:12`; extends b-5 = 14 px/10 px past the window rect) | fills exactly the window rect | FACT |
| B3 | edge tiles overrun to the outer rect, cut only at the outer rect (`:99-103`) | cut at window x = W and y = H, so they never show through a corner's transparent pixels | FACT |
| B4 | `alpha` key ignored (window alpha only) | multiplies every border texture; the fill is unaffected | FACT; no shipped Frame sets it |
| B5 | tile phase: starts at `corner.x` of the outer rect = window 0 when corner.w = left.w (19 = 19) | starts at the left image's width (window 0) | same for the shipped art |

Changes: B1 ornament bottom-aligned at the window top (move to `-ornament.h`), B2 fill the window rect, B3 clip edge tiles to [0,W]x[0,H], B4 honour `alpha`. All FACT except the B1 origin (INFERENCE; a screenshot of one dialog from the original would settle it).

UNKNOWN: whether the fill is multiplied by the window's fade alpha (the draw uses the colour as is; fades are probably applied by the parent through `SetAlpha` on Text/Image only, as `TFader` does), and what `TAnimationWindow` contributes to a `Frame` (the shipped `Frame`s have no animation).

## 3. Intro, credits, options (c)

### 3.1 `TMainMenu` intro (0x2cff8..0x2e1d6) - FACT

State byte at +0x38 (0 idle, 1..5), a per-state clock at +0x34, a free-running clock at +0x30 (all in 40 ms ticks of a window animation started in `PostChildrenInit`). Lua: `ui/mainmenu.lua` `StoryMode` hides `buttons`, shows `fade_buttons`, calls the native `DoIntro()`; `MainMenu` holds `Button intro_button` (full screen, `default`, `command = MainMenuFinishIntro`) with `Text intro_text` (x=100, y=0, w=600, h=500, `intro_scroll`, kVAlignCenter+kHAlignLeft). `TMainMenu::TFader` sets alpha on every `Text` and `Image` below a window (recursively); it does not touch Rectangle/Frame.

| State | Entered by | Each tick (40 ms) | Leaves when |
|---|---|---|---|
| 1 fade out | `DoIntro` (ignored unless idle): clock 0, **hides the cursor** | fader value `1 - t/1000 ms` applied to **every child of the MainMenu's parent** (the whole `image/mainmenu` bitmap: menu art, `splash_title`, buttons, texts, the MainMenu itself); beams swing toward their `up` angle (4.1) | value reaches 0: clock 0, `bsgShowWindow(intro_button)`, `intro_text` alpha 0 |
| 2 fade in | state 1 | fader `min(1, t/1000)` on the MainMenu's children (so the intro text) | value 1 |
| 3 hold | state 2 | nothing moves; the text stays put | `clock + 40 >= 22000` (22 s) |
| 4 fade out | state 3 | fader `1 - t/2000` on the MainMenu's children | value 0: `StopIntro` |
| 5 done | `StopIntro` (0x2d07a): stops the animation, clears the clock, **queues `MainMenuFinishIntro`** | - | - |

Total about 1 + 1 + 22 + 2 = 26 s plus the tick rounding. **There is no code that moves the intro text**: `OnTaskAnimate` and `Draw` contain no scroll, and `intro_text` is a plain centred `Text`. The key name `intro_scroll` is only a string key (the passage is 78 words, which fits the 600x500 box). A click or Enter on the button runs `MainMenuFinishIntro` itself.

Engine now (`MainMenuWindow.h:12-30`): a 30 s upward crawl of the text from y=550 to -450 with a 1000 ms fade of `fade_buttons`/`fade_text` only. **Wrong in four ways**: no scroll, 22 s hold not 30 s, the fade-out covers everything (including `splash_title` and `disabled_buttons`) and the text fades in over 1000 ms and out over 2000 ms. UNKNOWN: whether the cursor is shown again (`StopIntro` does not; `SwapToModal` probably rebuilds it), and the exact message ids behind state timing (none needed).

### 3.2 `TMainMenu::Draw` - the animated menu (0x2d4ae) - FACT unless marked

After its children, every frame:
* **Searchlights** (texture `image/splash_searchlight`, 50x1000), 4 beams from static tables: pivot (790,560), (800,580), (0,445), (0,475); angle limits min/max (degrees) 290/355, 285/355, 5/85, 5/90; `up` angle 355, 350, 10, 5; period halves 5400, 6100, 5600, 6900 ms. Angle = `min + (max - min) * tri`, with `tri = (clock mod 2P) / P` folded to `2 - tri` above 1 (a triangle wave over the free clock); in state 1 the angle is interpolated linearly to `up` over the 1000 ms (`angle += (up - angle) * t/1000`); in states >= 2 it is exactly `up`; in state 0 as above. Drawn with a rotation matrix of that angle; the translation is INFERENCE: `pivot - R * (0, texH/2)`, i.e. the beam's centre lies half its length from the pivot along the beam.
* **Blimp** (`image/splash_blimp`, 86x30): centre x = `(clock mod 25000)/25000 * (W + 500) - 250`, centre y = `H - 1.5 * texH` (555 at 600 high).
* **Skylines**: `splash_skyline_left` (211x290) centred at (w/2, H - h/2), `splash_skyline_right` (233x172) at (W - w/2, H - h/2); static.
Engine: `grep splash_searchlight|skyline|blimp src` finds nothing; none of this is drawn. Change: draw them as above in a `MainMenu` draw hook (FACT for the constants; the beam translation sign is INFERENCE, compare with a screenshot).

### 3.3 `TCredits` (window type `Credits`) - FACT

| Item | Binary |
|---|---|
| Tick | 50 ms window animation (0x7908) |
| Style keys (default) | `fadeUp` 300 ms, `fadeDown` 500, `showTime` 4000, `file` (INFERENCE default `credits.txt`), `font`, `sizeName` **20**, `colorName` (0,0,0,1), `sizeHeader` **40**, `colorHeader` (0,0,0,1), `sizeTitle` **30**, `colorTitle` = `colorName`. The shipped `ui/credits.lua` passes only `x,y,w,h,font`, so all defaults apply |
| File parse | lines split on `\n`, empty lines kept; a line starting with `---` ends a page |
| Line kinds | `+text` header (size/colour header), `*text` title (size/colour title), otherwise a name; no `|` = one `Text` as wide as the window, height = size, flags 5 (kHAlignCenter + kVAlignCenter, INFERENCE: same bit values as the Lua constants), font size = size; an empty line is a blank 20 px row. A `|` splits the line into N cells of `width / N` (integer division) each with its own `*` prefix test (a `*` cell makes the row's height the title size) |
| Layout | rows stack from y 0; afterwards every child is shifted down by `(windowH - stackHeight) / 2` (vertical centre) |
| Timing | page clock += 50 per tick; alpha = clock/fadeUp while clock <= fadeUp; 1 up to fadeUp + showTime; then `1 - (clock - fadeUp - showTime)/fadeDown`; when clock >= fadeUp + showTime + fadeDown (4800) the next page is built (all `Text` alpha 0, clock 0). After the last page it wraps to the first (start line index modulo the line count) |

Engine now: `Credits::readParams` (`game/choc2/CreditsWindow.cpp:5-18`) uses the Chocolatier 3 scroller: 18 px, 40 px/s, 1.5 s intro pause, two columns with a 20 px gap, dark red headers; the header comment says the original "may page between the `---` separators" - it does. Changes: implement the pager above (a new Choc2 `Credits` window; sizes 20/30/40, black text, fades 300/4000/500, 50 ms tick, `|` columns, vertical centring), all FACT except the flag meaning and the `file` default. UNKNOWN: the colour the Lua theme intends (defaults are black on the dialog's parchment), the font size is taken as the pixel height as `Text` does.

### 3.4 `TOptions` (0x25c18..0x26436) - FACT

No timing. `PostChildrenInit`: `fullscreen` button from `TSettings::IsFullScreen`, `mute` button on when pref `soundoff == 1`, sliders `sfxslider` / `musicslider` / `ambientslider` set to `pref/1000` with defaults **0x2ee = 750 for all three** (`soundlevel`, `musiclevel`, `ambientlevel`). `OnMessage` (slider-changed, type 9) applies the volume live: sfx -> types 1000 **and** 0, music -> 1001, ambient -> 1002. `SaveVolumes` stores `(int)(value * 1000)` for the three. Engine: `ui/VolumeSliders.h:19-21` and `game/Users.cpp:107-109` default 750/650/500 (Chocolatier 3's; `docs/re/audio.md` 67). Change: Chocolatier 2 defaults 750/750/750 (FACT; applies where the user has no stored value).

## 4. Engine changes, in the order I would do them

| # | Change | Label |
|---|---|---|
| 1 | Intro state machine per 3.1 (no scroll; 1000/1000/22000/2000 ms; fade everything under the menu bitmap; cursor hidden) | FACT; **done (4715891)**: cursor hide is only logged, the engine has no cursor control |
| 2 | Main menu searchlights, blimp, skylines per 3.2 | FACT (constants), INFERENCE (beam translation); **done (4715891)**: clockwise from up, beam stands on its pivot, checked in a screenshot |
| 3 | Credits pager per 3.3 | FACT (flag value and `file` default INFERENCE); **done (4715891)** |
| 4 | `Rollover` for Chocolatier 2: `xOffset` 20, `rolloverSound`, `area` (1.5) | FACT |
| 5 | `Frame`: ornament bottom-aligned, fill = window rect, edge tiles cut at the window, `alpha` key (2) | FACT; ornament origin INFERENCE |
| 6 | `colormask`/`ResetColorMap` (1.1) | FACT sequence, INFERENCE blend |
| 7 | Map Escape and deactivate-while-travelling pause (1.1) | FACT / INFERENCE for the message meaning |
| 8 | Travel ambient and one-shot sounds (1.4) | FACT |
| 9 | Options volume defaults 750 x3 (3.4) | FACT; **done (4715891)** |
| 10 | Comment fixes: week = 2000 ms is FACT (`MapTravel.h:27`) | FACT |

## 5. Unknown

* The blend used when several `map_mask_*` alphas are drawn into one target, whether `map_color.jpg` has an alpha channel after loading, and the vertical orientation of the read-back (1.1).
* Whether the original shows the cursor again after the intro, and `TWindow::StartWindowAnimation(40, true, false, true)` argument meanings for the vehicle (assumed: repeating, modal clock).
* The initial vehicle position argument in `TravelOneWeek` (Ghidra dropped the float).
* The beam translation sign for the searchlights (3.2) and the credits `file` default, flag value 5 (3.3).
* The exact ids 1000/1001/2000 sent to `TMap` (INFERENCE: activate/deactivate/textures lost).
* The `Frame` fill versus window fade, and the exact grown-viewport origin (2); one reference screenshot of a dialog frame would settle B1.
