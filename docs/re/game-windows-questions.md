# Answers to the `game-windows` design's open questions (section 9, "what RE analysis should find")

Scope: the nine questions listed at the end of `docs/design/game-windows.md` section 9. Sources, in order of trust: the Mac binary (`re/mac/decomp/`, indexed by `re/mac/index.tsv`; raw disassembly with `objdump -d` where the decompile dropped operands), the Chocolatier 3 Mac app bundle's `Contents/Frameworks/`, then the Lua and assets. Addresses are Mac addresses. String and float constants were read straight from the binary's data (the decompile hides PIC-relative constants); they are given as values here.

Conventions used below:

* `TSprite` fields (object size at least 0x84): `+0x04/+0x08` shared_ptr self, `+0x0c` parent, `+0x10` visible byte, `+0x14` **layer** (signed int), `+0x18/+0x1c` texture shared_ptr, `+0x20` `TMat3` (9 floats, row-major, **row-vector convention**: `v' = v * M`, translation is the third row, floats 6,7,8 = `+0x38/+0x3c/+0x40`), `+0x44` origin `TVec2`, `+0x4c..+0x58` colour RGBA, `+0x5c` **alpha** (init 1.0), `+0x60..+0x6c` sub-rectangle / size (flag 4), `+0x70` flags, `+0x74` blend mode (-1 = inherit), `+0x78` child list head. `TDrawSpec` (what `Draw` passes down): `TMat3` at +0, origin at +0x24, colour +0x2c..+0x38, alpha +0x3c, sub-rect +0x40..+0x4c, flags +0x50, blend +0x54.
* Sprite/DrawSpec flag bits (verified in `TSprite::Draw` 0x7c7c0, `TSprite::GetRect` 0x7c05c, `TTexture::DrawSprite(TDrawSpec)` 0x73072): 1 = flip horizontally, 2 = flip vertically (a child's flags are XORed with the parent's `& 3`), 4 = use the sub-rectangle as the source rectangle, 8 = "the origin in the draw spec is explicit".
* The word "Unknown" below always means "not determined"; inferences are marked.

---

## 1. `TAnimatedTexture::Get(name, image, mask)` and the colour mask

### 1.1 What the arguments mean (`TAnimatedTexture::Get` 0x6e98e, `GetHandle` 0x6aba2, `InternalNew` 0x6e4f6, `ParseAnimation` 0x6dd32, `LoadTextures` 0x6b932)

* **Cache key** (`GetHandle`): the three strings joined with a colon, `name:image:mask`. `Get` looks the key up in the asset manager first; on a miss it creates a `TAnimatedTexture(name)`, registers it under the key, calls `ParseAnimation(image, mask)`, then `LoadTextures()`. If either fails the cache entry is cleared and null is returned. So `Get("a","","")` and `Get("a","a","m")` are different cached textures even though they share a sheet.
* **The sheet** is found from `name` only (`ParseAnimation`): if `name` exists as a file use it; else try `name + ".xml"`; else `name + ".anm"`; none: fail. The path is parsed with `ParseFromXML` if the resolved name contains `.xml`, else `ParseFromANM` if it contains `.anm`. The sheet's directory (everything up to and including the last `/` or `\`) is stored (record +0x14).
* **The two overrides.** The sheet's `<img name=".." mask="..">` attributes were stored by the parser in record +0x0c (image file) and +0x10 (mask file). After parsing, a non-empty `image` argument **replaces** the `img name`, and a non-empty `mask` argument **replaces** the `img mask`. (Empty arguments leave the sheet's own values.) The `<img>` attributes handled by `ParseFromXML` 0x6bec0 are, in code order: `regx` (if present, `regx`/`regy` go to record +0x2c/+0x30), `w`, `h` (+0x34/+0x38), `name` (+0x0c), `mask` (+0x10). `<sequence name=.. source=..>` go to +0x00/+0x04 (informational).
* **File resolution in `LoadTextures`**: with an empty mask it calls `TTextureData::LoadFile(dir + imageFile, null)`, and on failure `LoadFile(imageFile, null)` (the name as given, not prefixed with the sheet directory). With a non-empty mask it calls `LoadFile(dir + imageFile, dir + maskFile)`, and on failure `LoadFile(imageFile, maskFile)`. Both then do a config lookup whose result is ignored (inference: leftover). Before loading it sets the texture-data flag byte `|= 2` and `&= ~4`.
* Therefore a call like the port's mask overlay, `Get("ports/animations/car1", "ports/animations/car1", "ports/animations/car1_colormask")`, behaves as: parse `ports/animations/car1.xml` (frames, registration points, alpha, script all come from the sheet); the first attempt `dir + "ports/animations/car1"` (that is `ports/animations/ports/animations/car1`) fails; the second attempt loads image `ports/animations/car1` and mask `ports/animations/car1_colormask`, which `LoadFile` completes by extension probing (1.2). Both are paths relative to the asset root, not to the sheet. (Confirmed at `PortWindow.c` lines 1044-1045: arguments are `(image, image, mask)`.)

### 1.2 How the mask becomes alpha (`TTextureData::LoadFile` 0x6ffb2)

`LoadFile(imageName, maskName)`:

1. A `?` in the image name introduces options (substring searches for `slow` -> flags `|= 0x30`; one unresolved option string -> `|= 0x10`; `alpha` -> flags `= (flags & ~4) | 2`; `mipmap` -> `|= 8`); the `?...` part is stripped. Unused by the shipped data for these sheets.
2. **Extension probing**, image: if the file does not exist as named and the name contains none of `.png`, `.jpg`, `.jp2`, try `name.png`, then `name.jpg`, then `name.jp2`. Mask (when the mask file does not exist as named and has no `.png`/`.jpg`): try `maskName + ".jpg"` first, then `maskName + ".png"`. So `car1_colormask` resolves to `car1_colormask.jpg`.
3. Decoders by extension: PNG via `EzPngLoadImage`, JPEG via `EzJpegLoadImage`, JP2 via `EzJp2LoadImage`. All return **4 bytes per pixel, byte order R,G,B,A** (the code indexes the fourth byte as alpha and the third as blue). The JPEG decoder yields opaque pixels.
4. **Combining** (only if a mask name was given, or a `.mask.png` companion was found, and the mask loaded): if the **padded texture width of the mask equals that of the image** (`dc[0] == dc[4]`, the third output of each loader, i.e. the power-of-two width) then for every pixel `image.A = mask.B`, that is **the mask's blue channel becomes the image's alpha; the image's RGB is untouched; the mask's R and G are ignored**. The loop runs over `padW * padW` pixels (it assumes the padded buffer is square; inference: the loaders pad to square power-of-two buffers). If the widths differ nothing happens (silently opaque).
5. Companion mask without an explicit mask argument: a `.jpg` image is checked against a config flag (key string stored behind a global pointer, default `""`; not resolved); if that is non-zero and `<base>.mask.png` exists, that file is used as the mask exactly as above. This is the generic "JPG + `.mask.png`" path used by every sheet whose `<img>` has `mask="x.mask.png"` (e.g. `controls/rollover.xml`, `car1.xml`). The engine already has to implement this for the non-overlay case; the flag must be effectively true for the shipped assets to have transparent sprites (inference).
6. Texture upload: the pixel buffer is padded to power-of-two; if the renderer asks for square textures the height pad equals the width pad; larger than 0x400 is rejected unless option bit 0x20 is set. The half-pad offsets (`(padW - w)/2`, `(padH - h)/2`) are stored at texture-data +0x1c/+0x20 (inference: used to centre the image in the padded buffer).

### 1.3 So what is the car overlay?

For `lim_car1.images = {"ports/animations/car1", ...}` and `.masks = {"ports/animations/car1_colormask", ...}` the main sprite is `Get(image,"","")`: `car1.xml` frames over `car1.jpg` with alpha from the sheet's own `car1.mask.png` (blue channel). The mask sprite (`PathSprite::pMask`, `+0xa0`) is `Get(image, image, mask)`: the **same frames, the same registration points and per-frame alpha**, but the sheet image's alpha comes from `car1_colormask.jpg`'s blue channel instead of `car1.mask.png`. It is tinted by the sprite colour (per-sprite, see `port-window.md` 1.5 step 7) and drawn as a child of the main sprite at layer 0. The RGB it draws is `car1.jpg`'s RGB (not the colormask's RGB), multiplied by the tint. Both images are 229x149 in the shipped data, so the width test passes.

Answer to the design's question: **yes, the blue channel is the alpha**, for both the generic `.mask.png` companion and the explicit mask override.

### Unknown (1)

* The config key behind the "JPG companion mask" flag, and its default in the Mac build.
* The meaning of the `?option` string whose literal was not resolved (sets bit 0x10); the pad/offset use at texture-data +0x1c/+0x20.
* Whether a mask whose padded width differs is intended to be rejected (it is silently ignored).

---

## 2. How the map's text sprites draw (`MapWindow::CollectPortData` 0x2b77e, label code near 0x2c5cd; `TTextSprite::Create` 0x804e0, `Draw` 0x7f106)

### 2.1 The last `Create` argument is the layer

`TTextSprite::Create(width, height, flags, fontPath, lineHeight, colour, layer)` (0x804e0); the final argument is passed to `TSprite::TSprite(layer)` (sprite `+0x14`). The map passes `3` for the black label and `-1` for the white one. There is no outline parameter: the text is drawn once per sprite with `TTextGraphic::Draw`.

Constructed values (both sprites): width 200, height 20 (`0x14`), font `fonts/fertigo.mvec`, line height 20, flags `4` (port label on the right of its marker) or `6` (label on the left: `mapx >= 750` or `align == "left"`). Colours: first sprite (A) `TColor(0,0,0,1)` black, layer 3; second sprite (B) `TColor(1,1,1,1)` white, layer -1.

### 2.2 Tree, offsets, alpha

* A is the sprite added to the map's root sprite (`this+0x30`). B is added **as a child of A** (`AddChild(A, B)`).
* A position: `(X, Y, 0.0)` with `X = mapx - 10` (flags 6) or `mapx + 10` (flags 4), `Y = mapy`. B position: `(X + 2, Y + 2, 0.0)`. B's `+0x5c` is set to `0x3f400000` = **0.75, and `+0x5c` is the sprite's alpha** (default 1.0; multiplied with the parent spec's alpha in `Draw`). So the white copy is drawn at 75 % opacity.
* **The third component of the position is 0.0, not 1.0.** Positions are rows of a row-vector matrix (`v' = v * M`, verified from `operator*(TVec2, TMat3)` 0x794a8 and `operator*(TMat3, TMat3)` 0x7950c, where a child's matrix is `own * parentSpec`). With the third component 0, the child's translation is `x*row0 + y*row1 + 0*row2` of its parent's composed matrix, i.e. **the parent's translation is NOT added; only the parent's linear part (scale/rotation) applies**, and the third component stays 0 for everything below. This is why B's `(X+2, Y+2)` is an absolute screen offset from A, not "A's position plus (X+2, Y+2)". Engine consequence: for any sprite stored with third component 0 (map labels, port buildings, see `port-window.md` 1.4), the position is relative to the *window origin transformed by the ancestors' linear parts*, never to ancestor translations. For children of a root at (0,0) there is no difference; it matters only for a parent with a non-zero translation, and for the map label pair where it is essential. A reimplementation that adds the parent translation would draw B at `2X+2`.

### 2.3 Draw order (answer to "its layer")

`TSprite::Draw`-style traversal (`TTextSprite::Draw` copies it): first every child with `layer < 0`, then the sprite's own text, then the remaining children in list order. B (layer -1, child of A) is drawn **before** A's own text. Result: a white 75 % copy offset 2 px down and right, **underneath** black text. Visually: black text with a light drop shadow to the lower right, not "white text with a dark outline". Layers of the map's root children (from the `Create` calls in `MapWindow.c`): background root texture and the plain sprites such as the current-port marker `image/port_current`: 0 (the marker is created with layer 0, line ~1134; two more layer-0 sprites near line 428-440 are the other map overlays); port icon `TAnimatedSprite`s: 2; **labels (A): 3**; the travel vehicle `TAnimatedSprite`: 10; the cloud sprite `image/worldmap_clouds`: 11 (`0xb`). So labels sit above the port icons and the current-port marker and **under the clouds** and the vehicle (the numbers are exact; the clouds are mostly transparent).

### 2.4 The anchor under alignment flags (answer to "the anchor for flags 4 and 6")

`TTextSprite::Create` turns the flags into a pixel **origin** inside the 200x20 box (stored at sprite `+0x90/+0x94`, floats): flag bit 1 (centre) `origin.x = width * 0.5`; bit 2 (right) `origin.x = width`; neither: 0. Bit 4 (vertical centre) `origin.y = height * 0.5`; bit 8 (bottom) `origin.y = height`; neither: 0. (The constant 0.5 was read from the data.) The same flag value is also given to the `TTextGraphic` (`SetFlags`), which aligns the text inside its box with the usual `kHAlign*/kVAlign*` meaning (`fonts-and-text.md` 3.1).

`TTextSprite::Draw` (0x7f106) composes the matrix as for any sprite, then draws with **`topLeft = M.translation - origin`**, height = the box height, scale = length of the matrix's first row/column, alpha = the composed alpha, and rotation = `atan2(M[1], M[0])` in degrees about the (clamped non-negative) origin. Consequences for the map:

* Flags 4 (left, vcentre): origin `(0, 10)`: the 200x20 box spans `x .. x+200`, `y-10 .. y+10`; text is left-aligned in it, so the text **starts at x = mapx + 10** and is vertically centred on mapy.
* Flags 6 (right, vcentre): origin `(200, 10)`: the box spans `x-200 .. x`, text right-aligned, so the text **ends at x = mapx - 10**.
* Box height 20 and line height 20 give exactly one line; longer names are not wrapped further than the 200 px box (inference: the text layout wraps within the box width; names longer than 200 px at size 20 would wrap).

### Unknown (2)

* The `TTextGraphic::Draw` details for rotated drawing and whether a name wider than 200 px wraps or is clipped.
* The exact z-order between labels and the port-icon sprites (their layer values were not decoded here).

---

## 3. Which clock the sprite-script threads use, and do they freeze

### 3.1 Clock (answer: the platform timer, never the modal clock)

* `TAnimatedSpriteData` +0x0c holds an optional `TClock*`. Its constructor sets it to null (`TAnimatedSpriteData::TAnimatedSpriteData` 0x181bf8) and **nothing in the binary assigns it** (`TAnimatedSprite::SetClock` has no body; the only call sites read the field and apply it if non-null: `NewScript` 0x7634a and `Play` 0x771d6). So the sprite script `TScript` and its thread `TAnimTask` both have clock null, which `TAnimTask` interprets as `TPlatform::Timer()`.
* `TPlatformData::Timer` (0x8d3d0): `(gettimeofday() seconds+microseconds*1e-6 minus the start time) * 1000.0`, clamped to `[0, 4294967295]`, converted to an unsigned integer: **milliseconds since start-up, wall-clock, millisecond resolution**.
* Both tasks are handed to `TPlatform::AdoptTask` (0x59dca), which adds them to the **platform-wide idle task list** (`TPlatformData + 0x6a14`). That list is run with `TTaskList::DoAll(list, 0)` once per pass of the platform event loop (`TPlatformData::GetEvent` 0x929b8), regardless of which modal window is on top. Context `1` (`TWindowManager::DrawStack` 0x4d1a0 calls `DoAll(GetTaskList(), 1)` then the top modal's list) never runs them, because `TAnimTask::Ready(1)` returns the task's `+4` byte, which is 0 for all of these.

### 3.2 Task semantics (`TAnimTask`, 0x81276 ctor, `SetDelay` 0x81340, `DoTask` 0x813fe, `Ready` 0x812ea)

Fields: `+4` ready-in-context-1 byte, `+8` next-run time (-1 = paused), `+0xc` interval, `+0x10` **"re-arm from now"** flag, `+0x11` **catch-up** flag, `+0x14` clock.

* `SetDelay(delay, repeat, fromNow, catchUp)` (also the argument roles of `TWindow::StartWindowAnimation`, see 9.2): sets `+0x10 = fromNow`, `+0x11 = catchUp`. If `fromNow` is false and `next != -1`: `next += delay` (relative to the scheduled time). Otherwise `next = clockNow + delay`. In both cases `interval = repeat ? delay : 0`.
* `Ready(ctx)`: ctx 1 returns `+4`; otherwise ready when `next != -1` and `(int)(clockNow - next) >= 0`.
* `DoTask`: `now = clockNow`; if `now - next > 1000` then `next = clockNow` (resynchronise, forgetting the backlog); then repeat: `next = fromNow ? now + interval : next + interval`; call the task's run slot (vtable +0x10); if it returns false the task is finished and removed; if `catchUp` is set and `now > next` (unsigned) run again, else return true. (This **corrects** `map-window.md` section "Timing", which named +0x10 the repeat flag and described only the from-now path.)
* Sprite scripts: `WaitLuaNextFrame(ms)` inside the thread calls `SetDelay(ms, true, true, false)` and yields. The run slot resumes the coroutine; the yield's `SetDelay` overrides the pre-run `next = now + interval`, so the next resume is `clockNow_at_yield + ms`. **Re-arming is always from "now", never from the scheduled time**, and there is no catch-up, exactly as the design assumed. Lateness (including frame quantisation) accumulates. The original's loop rate is the OS event-loop rate (it polls with `ReceiveNextEvent`, backing off with `usleep` only when idle and inactive); no fixed frame period was found.

### 3.3 Do port sprite scripts freeze under dialogs? (answer: no)

* A dialog over the port is another modal pushed on `TWindowManager`'s modal stack. `TWindowManager::PushModal` (0x4e768) **pauses the previous top modal's `TClock` (`TModalWindow + 0x50`)**, and `PopModal` (0x4eb7e) restarts the new top's clock (`TClock::Start`). `TClock` accumulates the paused time, so a modal clock simply stops advancing while covered. Only tasks that use the modal clock are affected: window-animation tasks created by `TWindow::GetWindowAnim` (0x4aaa2) are bound to the nearest ancestor modal's clock, adopted by that modal's own task list (`TModalWindow::AdoptTask`), and that list is only run for the top modal (`TModalWindow::DoModalProcess` from `TWindowManager`). `StartWindowAnimation` users (`MapWindow` 30 ms, `TBonusGame` 50 ms, `CharWindow`, `SlotMachine`, `RecipeDesigner`) therefore **do** freeze while a dialog covers them.
* The **port scene does not use that mechanism**: `PortAnimTask` (the 50 ms scene update) is created with a null clock and adopted by the platform list (`PortWindow::PostChildrenInit` 0x32348), and every sprite script task is adopted by the platform list too. Neither freezes under a dialog.
* `PausePortAnimations(true)` (`InternalPausePortAnimations` 0x2eb14) calls `TAnimTask::Pause` on **only the `PortAnimTask`** (path sprites, the intro fade, music timing). It does **not** touch sprite script threads. So during the factory mini-game the wave and cloud sprite sheets keep animating, while path sprites (cars, boats) stand still.
* On losing OS foreground, `PortWindow::WindowToggleForeground` (0x2eb60) pauses the `PortAnimTask` only and, if the port is the top modal, runs `DoModal('ui/ui_pause.lua')`; sprite scripts are not paused by that code (whether the platform loop itself keeps calling `DoAll` while inactive was not examined).

Design consequence: the port's script threads and `PortAnimTask` should be driven by a plain wall-clock task list that is not stopped by dialogs; only `PausePortAnimations` and (inference) focus loss stop `PortAnimTask`. Window animations (not the port scene) use the modal clock and run only for the top modal.

### Unknown (3)

* Whether the platform loop stops calling the idle-list `DoAll` when the app is inactive.
* The per-pass cost: the original's real frame period when idle/active.

---

## 4. `Character::Draw`: the frame's registration point

### 4.1 The 6-argument `DrawSprite` (`TAnimatedTexture::DrawSprite(x, y, alpha, scale, rotation, flags)` 0x6b400)

It builds a `TDrawSpec`: identity matrix, `scale(param_4)` if not 1.0, rotation `param_5`, translation row `(x, y, 1.0)`, origin (0,0), colour white, alpha `param_3`, flags `param_6`, then calls the spec overload (0x6ad30). **Argument order is x, y, alpha, scale, rotation, flags** (this also clarifies the earlier doc text for `Character::Draw` and for the crate window). The spec overload (`TAnimatedTexture::DrawSprite(TDrawSpec)`):

1. Nothing is drawn if the current frame index is -1.
2. If the spec flags have bit 8 clear (always, for these callers): set it, and set the **origin to the current frame's registration point** (frame record `+0x14,+0x18` = `regx, regy`). With flip flags: horizontal flip -> `origin.x = frameWidth*0.5 - (frameWidth*(-0.5) + regx) = frameWidth - regx`; vertical likewise with `frameHeight`. (Float constants 0.5 / -0.5 read from the data.)
3. If frame record `+0x24` is not the sentinel `-10000000.0`, the frame's six-number `xform` becomes the 3x3 matrix `[[x0,x1,0],[x2,x3,0],[x4,x5,1]]` and is **pre-multiplied** (`xform * spec.matrix`). 
4. Source rect = the frame rectangle `(x, y, x+w, y+h)`; if the spec already had bit 4 the two rects are intersected. The spec alpha is multiplied by the frame's per-frame `alpha` value (record `+0x3c`, default 1.0).
5. `TTexture::DrawSprite(spec)`: the quad's top-left is `-origin` in sprite space (flag 8), then scaled/rotated/translated by the matrix. So with scale s, the registration point lands exactly at `(x, y)`, and scale/rotation are about it.

**Confirmed: the 6-argument `DrawSprite` puts the frame's registration point at `(x, y)`.** For a plain (non-animated) `TTexture` the same overload leaves flag 8 clear, so the quad is centred on `(x, y)` (offset `-size/2`).

### 4.2 `Character::Draw` (0x7441c)

Called from the window's Draw inside `TBegin2d`; coordinates are in the CharWindow's own space (`TWindow::DoDraw` pushes the window rect as viewport and clips to it, so anything outside the window rectangle is clipped).

* Static image (not a sheet): `texture->DrawSprite(w*scale*0.5, h*scale*0.5, alpha, scale, 0, 0)` with `w,h` the texture size: centred in the box `(w*scale) x (h*scale)` at the origin.
* Sheet: select frame 0, then `fx = frame0.w * scale * 0.5`, `fy = frame0.h * scale * 0.5` (the **width/height of frame 0**, via `GetWidth/GetHeight` which return the *current* frame's size); draw frame 0 with `DrawSprite(fx, fy, alpha, scale, 0, 0)`. If the sheet has more than one frame: select the happiness frame `+0x64` and draw it at the same `(fx, fy)` with the same alpha and scale; if the cross-fade target frame `+0x68` differs, draw it too with alpha `crossfadeTimer(+0x6c) * alpha / 200`. Each of these frames is placed by **its own** registration point at `(fx, fy)`: that is how the face frames line up with the body.
* Worked example (`characters/bag_marketkeep.xml`): frame 0 is 154x270 with registration (75,105); the centre `(fx,fy)` is (77,135) at scale 1, so the body's top-left lands at `(2, 30)`, not `(0,0)`; the face frames (56x71, registration (6,73)) have their registration point at the same (77,135).

### Unknown (4)

* Whether the character's window-local origin is the window's top-left client corner for every `CharWindow` placement in the Lua (taken from `DoDraw`).
* The per-frame `xform` default: no sentinel write was located in the parser; frames with an empty `xform` copy the previous frame's six numbers, the first frame with an empty one is left as constructed (the draw skips the multiply only if the first float equals `-10000000.0`).
* The `anchor` child attribute names in the sheet (`ParseFromXML` stores `{str, int, int}` per anchor; (inference) `name`, `x`, `y`).

---

## 5. Sprite flag 8, and the layer sort

### 5.1 Flag 8 (answer)

Meaning: **the sprite's origin (`+0x44`) is used as the pivot instead of the centre.** Verified in three places:

* `TSprite::GetRect` (0x7c05c): the untransformed rectangle is offset by `-size * 0.5` when flag 8 is clear (centred) and by `-origin` when set.
* `TTexture::DrawSprite(TDrawSpec)` (0x73072): the same switch on spec flag 8 (the sprite's flags are copied into the spec).
* `TAnimatedSprite::GetRect` (0x7605e) and the sheet draw above: if flag 8 is clear and the sprite has an animated texture, the registration point of the current frame is written into `+0x44/+0x48` (with the flip mirroring of 4.1) and flag 8 is set for the duration of the call (and cleared again afterwards in `GetRect`).

So a sprite with origin (0,0) and flag 8 set (the map background in `MapWindow::Init` 0x2ab68: `origin = (0,0); flags |= 8`, and the port root in `PortWindow::Init` 0x2f47e) is **anchored at its top-left corner**; the design's reading is correct. A plain sprite without it is centred on its position; an animated sprite is positioned by its frame's registration point.

Other flag bits of the same word (for completeness): 1 / 2 flip, 4 explicit sub-rectangle (fields `+0x60..+0x6c`; in `GetRect` the size comes from `+0x68/+0x6c`).

### 5.2 Layer sort (answer)

* Sort key: the signed int at sprite `+0x14` (`operator<(shared_ptr<TSprite>, shared_ptr<TSprite>)` 0x1724ec, a plain signed `<` on the layer).
* Sorting happens in `TSprite::AddChild` (0x7eaa0; after appending the new child at the **end**, it calls `std::list<shared_ptr<TSprite>>::sort()`) and `TSprite::SetLayer` (0x7c036: if the layer changed and there is a parent, calls the parent's virtual +0x24, which is `ResortSprites` 0x7ea92, again `list::sort()`). `std::list::sort` is a stable merge sort. **Equal layers keep insertion order**, and the earlier-added sprite is drawn first (behind).
* Draw order of one sprite (`TSprite::Draw` 0x7c7c0): compute own matrix/alpha/flags (matrix `own * parentSpec`, alpha `own * parent`, flags `own XOR (parent & 3)`, blend inherited when -1, **colour tint not inherited**); then walk the child list from the head: children with `layer < 0` are drawn first; at the first non-negative child stop; draw the sprite's own texture (via the texture's draw slot with the composed spec); then draw the remaining children (starting with that first non-negative one) in list order. Children get `depth - 1` (so depth -1 means unlimited). `visible == 0` skips the whole subtree.
* Therefore the effective order inside a parent is: negative layers ascending, the parent itself, then layers 0, 1, 2, ... ascending, insertion order within equal layers.

### Unknown (5)

* Whether anything other than `AddChild`/`SetLayer` re-sorts (none found).

---

## 6. The crates window (`TBonusGame`, Lua type `CratesGame`)

Registered in the type table as `CratesGame` (string at 0x158bd4, factory `TBonusGameDynamicCreate`). Class `TBonusGame : TWindow` (object size 0x530). Sources: constructor 0x3db00, `Init` 0x3d222, `PostChildrenInit` 0x3cc10, `OnTaskAnimate` 0x3cd7a, `PrepareGame` 0x3cdc6, `StartSparkle` 0x3cf0c, `Select` 0x3cfea, `Draw` 0x3da5a, `TCrate::Draw` 0x3d770. `PrepareGame`/`SelectPrize` bookkeeping is in `ledger-and-game-natives.md` 10.1 (re-verified, including the factor table 0.435 ... 0.0 summing to 5.125 and the single-pass `rand() % 21` shuffle).

### 6.1 DSL keys

`CratesGame` reads **no style keys of its own** (`Init` and `PostChildrenInit` only call the `TWindow` versions, then look at children). The Lua passes `name`, `x, y, w, h` and a `Group(selections)` of 21 `Button`s. The window requires children named exactly `select_1` ... `select_21` (`"select_%lu"`, 1-based); a missing one leaves that crate with a zero rectangle. `Init` loads three textures: `image/box_closed`, `image/box_open`, `image/box_open_mask` (each 133x135).

### 6.2 Layout

`PostChildrenInit` for each of the 21 slots: find the child `select_i`, take its window rectangle, convert it to the crates window's client coordinates, then **widen it by 20 px on both left and right** (button 50 wide -> rectangle 90 wide; top/bottom unchanged, 47 tall), and store the **centre** of the *original* button rectangle as the crate's `(x, y)` floats. State is set to `WAITING`. It then calls `StartWindowAnimation(50, true, true, false)` (a 50 ms window animation, so the animation runs on the modal clock and only while this modal is on top).

With the Lua geometry (`ui_crates.lua` lines 244-257): button i (1-based) at `x = ((i-1) % 7) * 60 + 226`, `y = floor((i-1)/7) * 55 + 95`, size 50x47, inside the `CratesGame` window at (0,0) of the dialog bitmap: 7 columns x 3 rows, centres `(251 + 60*col, 118.5 + 55*row)` in the window's coordinates. Crates are drawn centred on these points, not clipped by the button rectangles. The 90 px rectangle is used only for the value text.

### 6.3 Drawing (`TBonusGame::Draw`, then `TCrate::Draw` for slots 1..21 in order)

`TBonusGame::Draw`: `TWindow::Draw`, then inside `TBegin2d` the 21 crates in index order, then the particle system at `(selectX, selectY)` (the last sparkle origin). All crate sprites use the plain `DrawSprite(x, y, alpha, scale, 0, 0)` (centred on `(x,y)` for non-animated textures). Let `t` be the crate's `time` field (ms).

* **WAITING**: draw `box_closed` with alpha 1.0, scale **0.4** (so 53x54 px).
* **Otherwise (OPENING / BLANK):**
  1. `alpha = 1 - t/800`, `scale = (t/1600 + 1) * 0.4` (0.4 growing to 0.6 over 800 ms).
  2. If `alpha > 0` and state != BLANK: draw `box_open` at `(x,y)` with that alpha and scale.
  3. If the crate is a **bomb** (value 0, i.e. it has the `whammy_devil` texture, 128x128): `u = t mod 1000; if u > 500 then u = 1000 - u; scale = u/1000 + 0.5` (a triangle wave between 0.5 and 1.0, period 1000 ms); alpha 1.0. If state == BLANK the scale is fixed at **0.25**. Draw the whammy texture at `(x,y)` with `(alpha 1.0, scale)`.
  4. If the crate has **money** (value > 0): scale `s`: if `t >= 800` then 1.0; else if `t <= GROW_TIME` then `t/(2*GROW_TIME) + 1.0`, else `(t - GROW_TIME)/(-2*GROW_TIME) + 1.5`, where **`GROW_TIME = 400.0`** (set by a static initialiser at 0x1abaca). So the text scales 1.0 -> 1.5 over 0..400 ms and back to 1.0 over 400..800 ms. The text graphic is drawn with `TTextGraphic::Draw(rect, s, 0, 1.0, null)`, i.e. into the 90x47 crate rectangle with scale `s` (how the scale is anchored inside the rectangle is not decoded).
  5. If `alpha <= 0` or state == BLANK, stop; otherwise draw `box_open_mask` at `(x,y)` with the same alpha and scale as step 2 (it is the front of the crate, drawn over the text and the bomb).
* The closed crate is replaced by the opening animation immediately when selected: in WAITING only the closed box is drawn; on the next frame the open box fades out over 800 ms while the contents are shown at full opacity. The contents remain after the open box has gone (the text and the bomb keep being drawn every frame; `t` keeps growing, so the bomb keeps pulsing).

### 6.4 States and timing (`OnTaskAnimate`)

States: `WAITING = 0`, `OPENING = 1`, `OPEN = 2` (never assigned in the code; inference: unused), `BLANK = 3`. `Select(i, final)` on a WAITING crate: plays `crate_click`, sets OPENING; if the value is 0 it loads `image/whammy_devil`, sets BLANK when `final`, and plays `crate_no_money`; if the value is > 0 it builds the text and, when not `final`, also calls `StartSparkle` and plays `crate_with_money`. On any other state `Select` does nothing but return the value. The 50 ms tick adds **50 to `time` for every crate whose state is exactly OPENING (1)**; BLANK crates never advance (their scale is constant anyway), WAITING crates have `time` 0. The tick also advances the particle system (`TLuaParticleSystem::Update` with the argument **100**; units unknown) and invalidates the screen. It always returns true (never ends by itself).

### 6.5 Value text (`Select`)

`Dollars(value)` is the string. It is created with `TTextGraphic::Create(text, rect.width, rect.height, flags 5, "fonts/fertigo.mvec", size, colour)` where the rect is the 90x47 crate rectangle; flags `5` = horizontally centred (1) + vertically centred (4). Opened by the player: size **16**, colour **(0,0,0,1)** black. Revealed with `final == true`: size **14**, colour **(0.8, 0.2, 0, 1)** (reddish). Each crate owns its text graphic (destroyed with it).

### 6.6 The whammy texture and sounds

Bomb crate texture: `image/whammy_devil` (128x128). Sounds via `BSGSoundEvent`: `crate_click` (every first selection), `crate_no_money` (bomb opened, final or not), `crate_with_money` (money crate opened by the player; not on reveal).

### 6.7 Sparkle (Phase 4, recorded for completeness)

`StartSparkle(i)`: `selectX/selectY = (int)crate x, y`; start colour `(0.0824, 0.5588, 0.1059, 1.0)` (= 21,143,27 over 255); end colour `(0.0824, 0.5588, 0.1059, 0.2)`; sets the Lua global `gSparkleTime = 2.0` in the window's script. The constructor also sets the string global `gSparkleTexture = "money"`, registers the colour data sources `gSparkleColorStart/End`, registers the Lua globals `PrepareGame` and `SelectPrize` (cleared to nil in the destructor), and loads the particle script `glitz/sparkle.lua`. That file is **not present** in the original install's `assets/glitz/` or the Reforged `glitz/` folder (only `firework.lua`, `slotpayout.lua` and images); it presumably lived inside a packed archive (Unknown). Chocolatier 2 does ship `glitz/sparkle.lua`, `sparkle.png`, `sparkle.xml` and `money.png` (`re/choc2/pfp_extracted/glitz/`); the engine still stubs the sparkle for both games (a follow-up).

### Unknown (6)

* The anchoring of the scaled text inside the crate rectangle (`TTextGraphic::Draw(rect, scale, ...)`).
* The contents of `glitz/sparkle.lua`; the units of the `Update(100)` argument.
* Whether `OPEN (2)` is ever set (not in this class).
* The exact stacking relative to the `Button` children (the buttons have empty graphics so nothing is hidden).

---

## 7. `BSGWindow` `frame`: the 9-patch border (`TBSGWindow::DrawFrame` 0x6962, called from `TBSGWindow::Draw` 0x710e)

### 7.1 Key and image

`PostChildrenInit` (0x6516) reads the style string `frame` (default `""`). If non-empty: append `.xml` if it does not contain it, and `TAnimatedTexture::Get(name, "", "")`. So `frame = "controls/rollover"` loads the sprite sheet `controls/rollover.xml`. No animation: the frame is a static set of sheet frames.

Related keys read in the same function: `swallowmouse` (bool, default false), `color` (`GetColor`, defaults to the current `(1,1,1,0)`; the constructor leaves the colour at (1,1,1) and **alpha at 0.0**), and `fill` (bool, default true). If the colour's alpha is within [0,1], `SetAlpha(alpha, true)` is applied (propagating to children).

### 7.2 Sheet frames (header `BSGWindow.h` enum, same order in `controls/rollover.xml`)

`0 UL, 1 UR, 2 LL, 3 LR, 4 LEFT, 5 RIGHT, 6 TOP, 7 BOTTOM, 8 ORNAMENT` (the ornament is optional: drawn only if the sheet has more than 8 frames; `rollover.xml` has 8). In `rollover.xml` (jpg + `rollover.mask.png`, 8 frames): corners 4x4; LEFT/RIGHT 4x64; TOP/BOTTOM 64x4. Registration points: UL (4,4), UR (0,4), LL (4,0), LR (0,0), LEFT (4,0), RIGHT (0,0), TOP (0,4), BOTTOM (0,0).

### 7.3 Order of operations (`Draw` then `DrawFrame`)

`TBSGWindow::Draw`: if `fill` and the colour: `BSGFillRect(clientRect, color)` (colour includes its alpha; default alpha 0 means an invisible fill); then `TWindow::Draw`; then, if a frame exists, `DrawFrame`. (`TWindow::DoDraw` draws the window's children **after** its `Draw`, so frame and fill are under the children; for the 4 px outside ring this makes no visible difference.)

`DrawFrame` works in screen-rectangle space: `L, T, R, B` = this window's `GetWindowRect` (screen coordinates; `R, B` are the right/bottom edges). It saves the clip rectangle and viewport, **sets the viewport to the root window rectangle**, and draws every piece with `frame->DrawSprite(x, y, alpha, 1.0, 0, 0)` after `SetCurrentFrame(k)` -- i.e. the **registration point** of piece k is placed at `(x, y)`, scale 1, no rotation. `alpha` = the window colour's alpha (`this+0x48`).

1. **Top and bottom edges.** Clip: x in `[max(L,0), max(R,0)]`, y = full root rectangle. Piece 6 (TOP): for `x = L; x < R; x += pieceWidth(6)` draw at `(x, T)`. Piece 7 (BOTTOM): same loop at `(x, B)`. `pieceWidth` is the frame rectangle width (64 for rollover). The last tile is cut off by the clip at `R`.
2. **Left and right edges.** Clip: x = full root rectangle, y in `[max(T,0), max(B,0)]`. Piece 4 (LEFT): for `y = T; y < B; y += pieceHeight(4)` draw at `(L, y)`. Piece 5 (RIGHT): same at `(R, y)`. Tiling steps by 64, the last tile clipped at `B`.
3. **Corners.** Clip: the whole root rectangle. Draw piece 0 at `(L, T)`, 1 at `(R, T)`, 2 at `(L, B)`, 3 at `(R, B)`; then, if the sheet has more than 8 frames, piece 8 at `((L+R)*0.5, T)` (integer sum, halved).
4. Restore the saved viewport and clip rectangle.

**Effect with `controls/rollover.xml`:** the border is drawn **entirely outside** the window rectangle, 4 px thick. With the registration points above: UL occupies `[L-4, L] x [T-4, T]`, UR `[R, R+4] x [T-4, T]`, LL `[L-4, L] x [B, B+4]`, LR `[R, R+4] x [B, B+4]`; TOP tiles `[x, x+64] x [T-4, T]`, BOTTOM `[x, x+64] x [B, B+4]`, LEFT `[L-4, L] x [y, y+64]`, RIGHT `[R, R+4] x [y, y+64]`. Tiles are **not stretched**; they are repeated at their native size with the last one clipped. (An engine that places pieces by top-left instead of registration point would offset the corners by (4,4)/(0,4)/(4,0).)

### 7.4 Caveats

* The frame's alpha is the window colour alpha. All shipped users pass `color = rolloverColor` (`Color(255,215,171,255)`, alpha 1.0). `sim/factory.lua:356` uses `frame = "controls/rollover"` **without** a colour: alpha stays 0.0, so the frame (and the fill) are invisible there. That is the original behaviour, not a bug in the port.
* `fit = true` windows size to their children before `GetWindowRect` is used, so the ring wraps the text.
* Port building labels (`ui/portview.lua` line 73) therefore get: fill with `rolloverColor` over the window rectangle, then this 4 px ring outside it.

### Unknown (7)

* Whether `SetAlpha` fades from outside (e.g. `TWindowFader` on a parent) also changes the stored frame alpha: `SetAlpha(a, propagate)` writes `+0x48` (the colour alpha) for this window.

---

## 8. Does `ui/ledger.lua` look anything up by name while it is built?

**No.** Read in full (the Reforged mod's `assets/ui/ledger.lua`, 202 lines; the original is 167 lines with the same structure). At build time (top level of the file and the DSL calls evaluated while the `MakeDialog` table is constructed) it uses only: constants (`kLedgerPositionX/Y`, `kMax`, flag constants), `Color(...)`, `GetString`, `Player.money/time` with `Dollars()`/`Date()` for the initial labels, the style helpers (`SetStyle`, `AppendStyle`, `LargeLedgerButton`, `SmallLedgerButton`, `MapPortButton`, which only `GetTag`/`GetString`/`table.insert` and set `typename`, then `DoWindow`), the globals `zur_factory` ... `wel_factory` and `factory.name/port.name` (data, not windows), and the window types `Bitmap`, `Button`, `Text`, `DookieDropper`, `Group`. Every closure that touches windows or other screens (`InventoryButton`, `FactoryPhone`, `DisplayDialog`, `PauseTravel`, ...) runs only on a click.

The only deferred work is `QueueCommand(function() UpdateLedger("newplayer") end)` on the last line. It runs later from the command queue, after the ledger is built and after `PrepareInstance` (which also runs `UpdateLedger("all","")`), so it can see the named children.

Native side, also no global lookups while building: `DookieDropper::PostChildrenInit` looks up only its own child `count` and `port` (children are created before their parent's `PostChildrenInit`). `TButton::DoCommand` and `TText` link clicks use `FindParentModal` and by-name lookups, but only on user input. `TButton::OnNewParent` (0x9310e) just resets its state. The by-name lookups for the ledger (`money`, `day`, `rank`, `score`, `questText`, `ledger_questgoals`, factory covers) are all in `LedgerWindow` update code, after construction.

Conclusion: attaching the ledger to the top modal **before** running the file (the design's choice) is safe. The original order is run file -> `FitToChildren` -> `PrepareInstance` -> create the 50 ms task -> add to the top modal and move to the front (`ledger-and-game-natives.md` 1.2); nothing in the file can observe the difference. The one visible difference: `FitToChildren` must still run after the file, because the ledger's rectangle is the union of its children.

### Unknown (8)

* `DisplayDialog` etc. are untouched by the build, but the full list of globals the style helpers read (`controlStyle`, `C3ButtonStyle`, fonts) must exist in the engine's Lua state before the ledger is first grabbed (they do: `ui/style.lua`).

---

## 9. `rolloversEnabled` and the roles of the `StartWindowAnimation` arguments

### 9.1 `Rollover::rolloversEnabled` (answer: initially true, and never changed)

The static is a data symbol at 0x1d81c0 (`__ZN8Rollover16rolloversEnabledE`, section `__DATA`, initialised data). Its stored byte is **1** (read from the binary). The only instructions referring to it are the two reads in `Rollover::OnMouseMove` (0x345b4: `cmpb $0, 0x1d81c0` and the return value); there is no write anywhere in the program, and `Rollover::EnableRollovers(bool)` (declared in `Rollover.h`) has no body in the binary. So rollovers are **always enabled**. `Rollover::OnMouseMove` returns the flag, i.e. true on every mouse move that reaches it (see `design-questions.md` 10 for the rest of the flow).

### 9.2 `TWindow::StartWindowAnimation(delay, repeat, fromNow, catchUp)` (0x4ab98)

It is a thin wrapper: it gets (or creates) the window's animation task (`TWindow::GetWindowAnim` 0x4aaa2) and calls `TAnimTask::SetDelay(task, delay, repeat, fromNow, catchUp)`:

* `delay` (int, ms): the interval.
* `repeat` (bool): if true the interval is `delay` (the task keeps firing every `delay`); if false the interval is 0 (the stored interval is 0, so the next firing is immediately due again after each run until it pauses itself; used for one-shot waits).
* `fromNow` (bool): if true the next run is `clockNow + delay` and every run re-arms from the current time; if false the next run is `previous scheduled time + delay` (drift-free) when the task was already scheduled.
* `catchUp` (bool): if true, after a run `DoTask` runs again while the (stale) current time is later than the new next-run time.

So `StartWindowAnimation(50, true, true, false)` means: 50 ms interval, repeating, re-armed from the current time, no catch-up. The animation task lives on the **modal clock** of the nearest ancestor modal and in that modal's task list (3.3). The task is a `TWindowAnim` (stored at window `+0x2c`, back-pointer to the window at task `+0x18`); `TWindowAnim::Animate` (0x17847c) calls the window's virtual at vtable `+0x38`, `OnTaskAnimate`, and returns its result; returning false ends the task. `TWindow::OnTaskAnimate` (base) returns 0, so a window without an override runs once and the task ends. `StopWindowAnimation` clears the task's window pointer, after which `Animate` returns false.

### Unknown (9)

* How the engine's `repeat == false` interval of 0 interacts with `Pause` in the shipped users (all pass `true`).
