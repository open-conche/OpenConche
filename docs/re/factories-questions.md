# Factories: answers to the open questions of the original factory design

Sources, in trust order: the decompiled Mac i386 binary (`re/mac/decomp/`, Ghidra addresses, binary
the Chocolatier 3 Mac app bundle's `Contents/MacOS/chocolatier-decadence-design`), the Chocolatier 3 Mac app bundle's `Contents/Frameworks/*.h`, then the Lua and image files in the original install's `assets/` and
the Reforged mod's `assets/`. Where the decompiler lost an operand I read the i386 disassembly (`otool -tV`)
and the literal pools of the same binary; those spots are marked "(disassembly)". "Inference" marks anything not read
directly from code. Addresses are Ghidra/vm addresses (file offset = address - 0x1000 in `__text`).

Related notes: `chocolate-factory.md` (CF), `coffee-factory-and-recipes.md` (CO), `window-system.md` (WS).

---

## 1. `ChocolateFactory::OnTaskAnimate` (0xc180): exact step order

The design's assumed order (clock, then end states, then machine, then tasks) is **wrong in three places**. The real
order inside one 10 ms step is:

1. **Foreground check.** If `TPlatform::IsForeground()` is false: `PauseGame("game_paused")` and return 1 at once. No
   clock, no task ticks, no invalidate. (The window animation is stopped by `PauseGame`, so this runs once.)
2. **End-state handling, before the clock.** Reads `state` (+0x64):
   - `state == 4` (ABORTED): decay step (below).
   - `state == 3` (ENDING): only if all four conditions hold: the ring task's set is empty (field +0x34 of the ring
     task, the `rings` set count, inference from `FlyingRing.h`), the gun's flying-ingredient list is empty, the
     product belt's product list is empty, and `ChocolateTrayTask::AnyFull()` is false. Then the decay step.
   - Decay step: `s = trayTask.scale * 0.95`; if `s >= 0.05` (or NaN) `trayTask.SetScale(s)`; otherwise
     `SetGameState(5)`. Re-read `state` afterwards.
3. **Clock, by (re-read) state.** Remaining time `rem` is field +0xec; `gametime` is +0xe4, `countdowntime` +0xe0,
   `warntime` +0xe8. `ResetGame` sets `rem = gametime + countdowntime`.
   - state 1 (COUNTDOWN): if `gametime + 10 < rem`: `rem -= 10`; if `floor(old/1000) > floor(new/1000)` play
     `clock_tick` and set the `countdown` text to `"%d"` of `ceil((rem - gametime) / 1000)`. Otherwise (`rem <=
     gametime + 10`): `rem -= 10`, play `start_bell`, `SetGameState(2)`, set the countdown text to `""`. With
     `countdowntime = 3000`, step 300 of the countdown starts PLAYING (steps 1..299 stay in COUNTDOWN).
   - state 2 (PLAYING): if `rem < 11`: reset the timer hand matrix to identity, time text `"0:00"`, `SetGameState(3)`.
     Else `rem -= 10`; play `whistle` when `warntime < old && new <= warntime`; time text `"%lu:%02lu"` of
     `ceil(rem/1000)` as minutes:seconds (skipped when no `time` Text exists); hand rotation
     `-(2*rem/gametime)*pi` (skipped when no hand sprite).
   - other states: nothing.
4. **Task ticks, always, in every state, 10 each, in this order:** recycler task (+0xb8, only if present), tray (+0xb0),
   conveyor (+0x7c), gun (+0x80), ring (+0xbc), product belt (+0xb4). Paused tasks ignore the tick.
5. **Machine animation.** If `machineTime` (+0x8c) is non-zero: `t = t + 10`, and `t = 0` if that reached 400 or more;
   `angle = 2*pi*t/400` from the **new** value. Tray-template sprite (+0x90) rotation = `+angle`, upper finger (+0x98)
   = `-angle`, lower finger (+0xa0) = `+angle`; each keeps its translation. When `t` wraps to 0 the angle is 0 and the
   animation stops (rest pose). One revolution is 40 steps.
6. `TWindowManager::InvalidateScreen()`, return 1.

Consequences the design should adopt:

- The ENDING/ABORTED decay and the transition to OVER (state 5) happen **before** that step's clock and task ticks; the
  step that sets state 5 still runs steps 3 to 5 with state 5 (no clock, and the tasks that `SetGameState(5)` paused
  ignore the tick).
- A state change made in step 3 takes effect before the ticks of the same step. At the COUNTDOWN to PLAYING change,
  `SetGameState(2)` calls `Restart(0xf)` on tray, conveyor, gun, ring, product and recycler tasks, then step 4 ticks them
  once. So the first PLAYING step already advances the tasks by 10 ms. At PLAYING to ENDING, `SetGameState(3)` pauses
  and resets the conveyor, so the conveyor tick in the same step does nothing.
- The ENDING drain test (step 2) looks at the state left by the **previous** step; the step that enters ENDING does not
  test it.
- The machine angle is computed **after** the task ticks, from the new `t`.

Coffee (`CoffeeFactory::OnTaskAnimate`, 0x15434) differs: after the foreground check, state 4 goes **straight to
`SetGameState(5)`** (no decay); state 3 goes to state 5 when the gun's flights and the case belt's cases (+0xac list)
are both empty (no tray, no ring, no decay); the clock block is identical (fields +0xe4 countdown, +0xe8 game, +0xec
warn, +0xf0 remaining); the ticks are +0x7c, +0x80, +0xac, +0xa8, +0x94 (grid) in that order; then every `CoffeeBonus`
in the list (+0xbc) is ticked by 10 and erased when its `Tick` returns false. There is no machine animation.

---

## 2. `TAnimTask::DoTask` catch-up and the first step after `StartGame`

### 2.1 Resync: exactly one step runs

`TAnimTask` (0x81276 ..) fields: `due` (+8, `-1` means stopped), `period` (+0xc), `fromNow` (+0x10), `catchUp`
(+0x11), `clock` (+0x14). Time comes from the window's modal clock (below). `Ready` is true when `due != -1` and
`now - due >= 0`. `DoTask` (0x813fe), with `d = now - due`, `now` sampled once on entry:

- `d >= 1001` (more than 1000 ms late): `due = now`; then (with `fromNow == false`) `due += period` and the callback
  runs **once**; the catch-up test `now <= due` holds, so it returns. Result: one step, next due `now + 10`. A 1.5 s
  stall costs 10 ms of game time, not 150 steps.
- `0 <= d <= 1000`: each pass does `due += period` and then runs the callback. It loops while `catchUp` and `now > due`
  (using the entry `now`; the clock is not re-read). Number of steps = `max(1, ceil(d / 10))`. The final `due` lies in
  `[now, now + 10]`. If the callback ever returns 0, `DoTask` returns 0 at once (the factory callbacks always return 1).
- With `fromNow == true` each pass would instead set `due = now + period`; the factories pass `false`.

Clock caveat: the task's clock is the **parent modal window's `TClock`** (`GetWindowAnim`, 0x4aaa2). `PushModal`
(0x4e768) pauses the clock of the modal under the new top and `PopModal` restarts it with `TClock::Start`, which
rebases so paused time is excluded. So a help dialog opened over the factory does not create a catch-up burst on return.

### 2.2 First fire after `StartGame`

`StopWindowAnimation` (0x49eca) does not pause the task. It nulls the task's owner pointer and the window's pointer
(+0x2c), abandoning the old task. `GetWindowAnim` then makes a **new** task on the next `StartWindowAnimation`; its
constructor sets `due = clock.now` (the rebase in `SetClock` keeps the offset 0).
`StartWindowAnimation(10, true, false, true)` calls `SetDelay(10, repeat, fromNow=false, catchUp=true)`; with
`fromNow == false` and `due != -1` this does `due += 10` and `period = 10`.

Sequence in `StartGame` (0xd1d6): `ResetGame` -> its `PauseGame("game_paused")` runs (the paused flag +0xdc was 0) and
stops any task; `StartWindowAnimation(...)` creates the task, `due = T0 + 10`; `SetGameState(1)`; `ResumeGame`. Because
`ResetGame` left the paused flag set, `ResumeGame` runs its whole block and calls `StartWindowAnimation` **again**. The
task now exists, so `SetDelay` adds another 10: `due = T0 + 20`. First `OnTaskAnimate` therefore fires at the first
frame with modal clock `>= T0 + 20` (20 ms after `StartGame`), running `max(1, ceil(d/10))` steps for its lateness.

After a pause: `PauseGame` abandons the task, `ResumeGame` (called once) creates a fresh one: first step at `T0' + 10`.
A fixed-step port can use: first step after 20 ms on StartGame, after 10 ms on Resume. (The 20 ms is read from the
call sequence; I did not run it.)

---

## 3. `SetTopModalOnly(bool)`

**What it gates: drawing only.** `TWindowManager::SetTopModalOnly` (0x4cd6c) stores the flag at manager +0x26. Its
only reader is `TWindowManager::DrawLayer` (0x4cf72): when the flag is true (or the draw mode is 2) only the top modal
window is drawn (`DoDraw(top)`); when false it walks the modal stack from the top to the first modal that has window
flag 0x08 and the same size as the manager's base window (+0xc), and draws from there up to the top in order. So
`true` hides the stack under the factory (cheap), `false` keeps what is under it visible. (Draw-stack reading is
inference where noted in `window-system.md`.) The flag never touches event routing; `HandleEvent` and
`DispatchWindowMessage` do not read +0x26.

Callers: `Init` and `ResumeGame` set `true`; `PauseGame` (0x8384) and `SetGameState(5)` set `false`; coffee identically.
The constructor sets +0x26 to 0.

**Clicks while paused: nothing in the code stops them.**
- Mouse events go to the top modal and then to the deepest visible child under the point (WS 6.5). The pause overlay is
  the `information` Bitmap, a sibling of `FactoryGame` and not a modal. It only covers its own rectangle
  (`popup_back_generic_1`, centred), and the `go`/`pause` buttons sit at (670, 0).
- `ChocolateFactory::OnMouseDown` (0x8000) and `CoffeeFactory::OnMouseDown` (0x10c54) test only `state == 2`
  (PLAYING). A pause does not change `state` (it sets the paused flag +0xdc, pauses the tasks, and stops the window
  animation), and the paused flag is not tested. They return the window's flag byte +0x4c.
- `ChocolateGunTask::Fire` (0xd9f0) does not test pause either. It pops the head of the conveyor (needs `head == 1.0`),
  creates a flying ingredient, plays the gun animation and posts the `launch` sound.

So a click on the playfield outside the pause popup while paused, in PLAYING, **does fire a shot** in both factories:
the ingredient leaves the belt and the flight is created, but the gun/flying tasks are paused, so it hangs in the air
until Resume. Escape is not affected (see 4). I derived this from the code; I did not run the game. The design's
"ignore while paused" is therefore a deliberate fix, not original behaviour; both choices are defensible. If you keep
the original quirk, note that the frozen shot resolves normally after Resume. (A click inside the popup is consumed by
the popup or its button.)

---

## 4. Key dispatch order: modal Escape handling versus the factory's `OnKeyDown`

**A key press produces a character-class event first and a key-down event second, so the modal's `OnChar` (Enter and
Escape handling) runs before the factory's `OnKeyDown`.**

- Evidence, platform layer: `PFUnicodeAppEventEventHandlerProc` (0x903a4) queues, for one text-input event, either (a)
  type 0xd (char) then type 0xb (key down, `toupper(char)`) when the static unicode flag at 0x1d9281 is 0, or (b) type
  0xe (unicode text) then, for code points below 0x80, type 0xb. The flag is 0 in the data section; I did not find the
  writer. Either way the character-class event precedes the key down in the queue.
- `TWindow::HandleEvent` (0x4b578) maps type 11 to the virtual `OnKeyDown(char, flags)` (slot 0x5c), type 13 to
  `OnChar(char)` (0x54), type 14 to `OnUTF8Char(str)` (0x58, which falls back to `OnChar` for one character). Key
  events go to the manager focus window (`TWindowManager::HandleEvent`, focus at +0x38). The factory is the default
  focus of its modal (`SetDefaultFocus` in `Init`, ChocolateFactory.c line 858). Unhandled events bubble to the parent;
  the modal is the last stop.
- `ChocolateFactory::OnKeyDown` (0x866c) and `CoffeeFactory::OnKeyDown` (0x112a6) handle only Escape, and **always
  return false** (not consumed). `TModalWindow::OnChar` (0x74d42) handles Enter and Escape.
- `TModalWindow::OnChar` finds the default (Enter) and cancel (Escape) buttons with `FindDefault::Process`
  (0x181170). It accepts a button only if the button and **every ancestor up to the modal** have the visible bit
  (0x100). Hidden buttons never count. If the matching button exists and `DoCommand` succeeds, it is consumed.
  Otherwise a type-2 message is sent to the focus window (or the modal) and the function still returns true.

Resulting behaviour per screen (derived):

| Screen | Escape | Enter |
|---|---|---|
| Intro overlay (`information`, `ok` default, `cancel` cancel) | char event: `cancel` runs `CloseWindow(nil)`; the dialog closes. `PopModal` is synchronous (0x4eb7e). Then the queued key-down goes to the new top modal's focus. The map's `OnKeyDown` returns false, so it is harmless. | `ok` runs `StartGame()`. |
| PLAYING, no overlay | no visible default/cancel; the type-2 message reaches `OnMessage`, which does not handle it; then the key-down pauses via `OnKeyDown` (`game_paused`). | nothing |
| Paused (`game_pause.lua`: only `ok`, no `default`/`cancel` flag) | nothing at char; `OnKeyDown` sees the paused flag already set and does not re-pause. Escape does **not** resume. | nothing; only the `ok` button resumes |
| Game over (`chocolate_gameover.lua`: `try_again`, `done` has `cancel = true`) | closes with `CloseWindow(score)` | nothing (no default button) |
| Coffee equivalents | same (`coffee_factory.lua` has the same `ok`/`cancel` pair; game-over and pause Lua are the same shape) | same |

Not verified: the order of char versus key-down relies on the queue order above and on the main loop draining it in
order; I did not trace `TPlatformData::GetEvent` (0x929b8).

---

## 5. Coffee grid target marker

From `CoffeeIngredientGrid::CoffeeIngredientGrid` (0x166ea):

- Marker sprite (grid +0x90): texture `image/conveyor_belt_gridtarget` (57 x 57 px, same file in the original install's `assets/` and
  Reforged), created with `TSprite::Create(layer = 0, texture)`. It is **initially invisible**. It is added as a child
  of the grid sprite after the slot sprites and the two seam sprites. Default sprite anchor, so it is centred on its
  position (default anchor is the texture centre unless sprite flag 8 is set, see 8).
- Its child (grid +0x98) is created with `layer = -1` (a negative layer draws **behind** its parent, `TSprite::Draw`
  0x7c7c0), no texture at first, alpha 0.5. `SetTargetImage(ingredient)` (0x163e8) sets that child's texture to the
  texture of the given ingredient (the belt head); a null ingredient gives a null texture.
- `SetTargetPosition(x, y)` (0x164c8): `idx = GetSlotIndex(x, y)`. If `idx < 0` and the marker was visible: cursor back
  to `cursor/cursor` (hotspot 1,1), marker hidden. If `idx >= 0`: if the marker was hidden, cursor becomes
  `cursor/blank_cursor` (hotspot 1,1); marker shown; its position is set to `GetRelativeSlotPosition(idx)` (slot
  centre, `((col + 0.5) * dx, (row + 0.5) * dy)`) with z 1.0, in grid-local space.
- Size: the grid cells are 45 x 45 (`griddx = griddy = 45` in `coffee_factory.lua`), the marker is 57 x 57, so it
  overhangs a cell by 6 px each side.
- Layering among siblings (inference): the marker is layer 0, the same as the seams; ingredient sprites added to the
  grid carry their own layer, so the marker is drawn with or below them, not above. I did not read the
  ingredient layer inside the grid.

---

## 6. `TImage::SetTexture` third argument: it is a scale

`TImage::SetTexture(tex, float s)` (0x864d4) stores `s` at +0x38. If the texture is non-null it resizes the window to
`(int)(textureWidth * s + 0.5)` by `(int)(textureHeight * s + 0.5)` (width and height swapped when the image's rotate
flag +0x44 is set; the +0.5 is a literal at 0x1ab6ec). `TImage::Draw` (0x85d30) applies `TMat3::Scale(+0x38)`. Alpha is
a different field and method: `TImage::SetAlpha` (0x85f68) writes +0x3c. `LedgerWindow` and `LPCD` pass `1.0`.

`RecipeDesigner::SetLayer` (0x33418), per layer slot `n`:

| window | scale |
|---|---|
| `option_layer<n>` (BitmapTint) | 0.5 |
| `option_highlight<n>` (Image) | 0.5 |
| `display_layer<n>` (BitmapTint) | 1.0 |
| `display_highlight<n>` (Image) | 1.0 |

So CO 5 is right (scale), LG 10.3 (alpha) is wrong. Option thumbnails are drawn at half size; displays at full size.
Both show only when the texture loads; the highlight pair is shown only when `<name>_highlight` exists. An empty name
hides all four.

---

## 7. `TFile::GetNextFile` order for `custom/<category>/`

`TFile::GetNextFile(prefix, cursor, recurse)` (0x525e8) iterates a global `std::map<str, ...>` of resource names,
**ascending by key**, via `upper_bound(cursor)` (the prefix on the first call). Keys are lower-cased. With
`recurse == false` it skips entries whose remainder after the prefix contains a `/`, and stops when the next key no
longer starts with the prefix. The ordering is `str::operator<` (0x5053e) = `str::compare` (0x504ca) < 0, which is
`strcmp` truncated to a signed char: plain byte order, case-sensitive (irrelevant after lower-casing, and ASCII
differences fit a signed byte). So the listing is **sorted, bytewise**.

How `RecipeDesigner::SetRecipeType` (0x32e32) uses it: for each key it skips names containing `_highlight`, skips names
without `.png`, strips the 4-char extension, and keeps it only if `TTexture::Get` loads. It then files the name by
substring `layer1`..`layer4` (in that order of testing) into the four lists. After the scan it appends one empty
string `""` to layers 2 and 4 always, and to layer 3 unless the category is `beverage`; layer 1 never gets an empty
entry (index +0x54, +0x60, +0x6c, +0x78 respectively). `SetLayer` wraps indices modulo the list size. Other files (for
example Reforged `beverage/mug.png`, `rim_inner.png`, `rim_outer.png`) are loaded into the asset map but match no
layer and are not listed.

Sorted order equals numeric order here because indices are zero padded (`layer4_09 < layer4_10`), and I checked every
`custom/<cat>/` in both trees: base files are consecutive from `_01` with no gaps. Counts of selectable base images per
layer (L1, L2, L3, L4), highlight files excluded, empty option not counted:

| category | the original install's `assets/` | Reforged |
|---|---|---|
| bar | 4, 7, 4, 8 | 4, 7, 5, 11 |
| beverage | 4, 5, 7, 7 | 4, 5, 6, 8 |
| blend | 2, 2, 5, 8 | 2, 3, 5, 8 |
| exotic | 2, 2, 6, 10 | 2, 2, 6, 10 |
| infusion | 2, 5, 6, 8 | 2, 5, 6, 10 |
| truffle | 2, 6, 7, 7 | 2, 6, 7, 10 |

The 5, 7, 4, 8 in CO was a miscount of the original install's `assets/` bar: `layer1_05_highlight.png` exists without a `layer1_05.png`,
and highlight files are filtered out, so layer 1 has 4. The design's 4, 7, 5, 11 is the Reforged bar. (Several
Reforged layers have a `_highlight` file for only some images; the pair-lookup is per name, so that is fine.)

Caveat: this is the Mac binary's resource map. The Windows engine that ships Reforged could enumerate differently; with
this naming any sane order matches.

---

## 8. Piston, arm and finger geometry in the chocolate factory

Conventions read from `TSprite` (0x7e226, 0x7c05c, 0x7c7c0): a sprite's position (matrix translation +0x38/+0x3c) is
where its anchor lands; **default anchor is the texture centre**; if sprite flag +0x70 bit 8 is set, the anchor is the
vector at +0x44 in texture pixels from the top-left. Children with a negative layer draw before (behind) the parent,
others after. Image sizes below are from the original install's `assets/image/*.png`.

### 8.1 Tray pistons (`ChocolateTray::BuildPistons` 0xea2c, `BuildPiston` 0xe8ca, `Update` 0xf5ac)

- Four sprites, textures `tray_arm_4`, `_3`, `_2`, `_1` (widths 19, 24, 30, 38 px, all 124 tall), each **layer -100**,
  flag 8, anchor `(width/2 (integer division), height - 2)` = bottom centre, 2 px up from the bottom edge. They are
  added as children of the base sprite (the parameter of `BuildPistons`) in that order.
- Every `Update` (the tray task's `Animate` calls `Update` per tray each step, after the tray position is set from the
  curve): with the tray position `(x, y, z)`, piston `i` (i = 0..3 for `arm_4`, `arm_3`, `arm_2`, `arm_1`) is placed at
  `(x, (4 - i) * (y - 50) / 4, z)`. So `arm_4` (narrowest) bottom is at `y - 50`, `arm_3` at `0.75*(y - 50)`, `arm_2` at
  `0.5*(y - 50)`, `arm_1` (widest) at `0.25*(y - 50)`. The telescope stretches from the top of the screen down to 50 px
  above the tray, with the widest segment on top. (If `y < 50` the values go negative; no guard.)
- Timing: continuous, follows the tray every tick; nothing animates apart from that.

### 8.2 Conveyor pistons (`IngredientPiston` ctor 0x2623c, `SetPosition` 0x25cb2, `IngredientQueue::Update` 0x26f1a)

One piston per belt slot, made lazily in `Update` until the piston count equals the number of belt ingredients; **they
are never removed**, not even by `Reset` (stale ones keep their last position until reassigned). Each is a root sprite
with children, all added as children of the queue's base sprite:

| part | texture | size | layer | anchor | position |
|---|---|---|---|---|---|
| root | `ingredient_arm_2` | 15 x 124 | -100 | `(7.5, -16)`, flag 8 | `SetPosition(p)`: the piston's ingredient's translation |
| arm (child of root) | `ingredient_arm_1` | 19 x 124 | 0 | `(9.5, 0)`, flag 8 | `(0, (600 - p.y)/2 + 16)` relative to the root (initial `(0, 120)`), rewritten on every `SetPosition` |
| finger 1 (child of root) | `ingredient_finger_1` | 27 x 64 | 2 | default (centre) | `(-6, 0)` |
| finger 2 (child of root) | `ingredient_finger_2` | 27 x 64 | 2 | default (centre) | `(+6, 0)` |

- Piston `k` is attached to the `k`-th ingredient of the belt list (front first). Update calls
  `SetPosition(ingredient.translation)` for all pistons that have an ingredient.
- **Timing quirk:** the whole `Update` body, including piston repositioning, runs only while the belt head is below 1.0
  (`if head < 1.0`). Once the belt is full and parked (head = 1.0, waiting for a shot), pistons are not touched and
  stay where they were. After a pop the head drops by one spacing and everything is repositioned during the refill.
- Geometry check (inference): the shipped chocolate conveyor path runs along the bottom (`y = 570`) and then up
  `x = 295..300` to `y = 300`. With the anchors above, the narrow arm hangs from `y + 16` to `y + 140` below the
  ingredient (off-screen for belt points near `y = 570`) and the wider arm's top is at
  `y + (600 - y)/2 + 16`, midway to the bottom edge plus 16. By the formula there is a gap (for example `y = 300`: arm_2
  occupies 316..440, arm_1 starts at 466). I could not tell from the code whether the art hides that gap or the arms
  are meant to read as a telescope; compare against a reference screenshot before polishing.

### 8.3 Machine arms and fingers (centre machine, `Init` 0x9414, `OnTaskAnimate`)

| sprite | texture | size | layer | anchor | position (relative to machine) |
|---|---|---|---|---|---|
| machine | `image/machine_<n>` from Lua | | 6 | centre | window `(695, 301)` |
| upper finger | `machine_stack_finger_upper` | 56 x 132 | -1 | `(41, 14)`, flag 8 | `(-74, -51)` |
| lower finger | `machine_stack_finger_lower` | 56 x 135 | -1 | `(41, 119)`, flag 8 | `(-74, +50)` |
| tray template | `image/tray<N>` (N = recipe size) | | 0 | centre | `(0, -2)` |

At rest the pivots are at window `(621, 250)` (upper) and `(621, 351)` (lower). The recipe icons `items/<name>` (layer 0)
are children of the tray template at `ChocolateTray::GetSlotPosition(i, N)`. Message 0x3ec starts the animation if
`machineTime == 0` (see 9): then 40 steps of 10 ms, angle `2*pi*t/400`, template `+angle`, upper `-angle`, lower
`+angle`, applied to each sprite's rotation matrix with the translation kept (rotation is about the sprite's anchor;
the screen direction of a positive angle I did not decode). `box_open` (layer 1) and `box_open_mask` (layer 3) are at the
end of the product curve; product picture at scale 0.5, offset `(0, 9)` between them.

---

## 9. Remaining items

### 9.1 0x3ec to 0x3ed fall-through: real

`OnMessage` (0xcce8), disassembly: the jump table at 0x1928c0 sends 0x3ec to 0xcd0f and 0x3ed to 0xcd46. The 0x3ec block
sets `machineTime = 1` only if it was 0 (re-triggers while running are ignored), plays sound `snatch`, then continues
**without a jump** into 0xcd46, which is the whole 0x3ed block: `ProductQueueTask::AddProduct(sprite +0x68)`. So 0x3ec
(`kManufactureProductEvent`, "using the animated machine") = machine animation + sound + product; 0x3ed
(`kProduceProductEvent`, "immediately") = product only. 0x3ee falls into 0x3ef in the same way (0x3ee: `+0xf4 += 1`
then the shared `+0xf0 += 1` and `score` text `"%lu"`); the jump table sends 0x3ee to 0xcd83 and 0x3ef to 0xcd8a.
`ChocolateTray::Update` sends 0x3ec to the `FactoryGame` child of the top modal when a full matched tray crosses its end
point.

### 9.2 `CoffeeBonus` rise factor: elapsed / 2000, linear

`CoffeeBonus::Tick(dt)` (0x11442), disassembly: `f = age / 2000.0` (literal 0x1ab704 = 2000.0, computed from the age
**before** this tick is added). Position = `start + (0, -150, 0) * f` (constant `0xc3160000` = -150). Alpha (+0x5c) =
`f * -0.8 + 1.0` (doubles at 0x1ab4e0 = -0.8 and 0x1ab470 = 1.0). Then `age += dt`; if `age <= 1999` return true;
otherwise remove the sprite from its parent and return false. So first tick draws at age 0 (start position, alpha 1);
it rises 150 px linearly over 2000 ms and fades to alpha 0.2 at the end; at 10 ms steps it lives 200 ticks and the
200th tick shows `f = 0.995`. The constructor (0x11576) uses mean position of the collected ingredients (z = 1.0),
text `+%lu`, a 120 x 120 text box, alignment 5, font `fonts/fertigo.mvec`, size 0x50, white, last argument 999, and
adds the sprite to the first ingredient's parent. This confirms the CO inference.

### 9.3 `CoffeeGridTask` initial `previousT`: uninitialised, and never reset

Layout: `pGrid` (+0x1c, shared_ptr, 8 bytes), `gridTime` (+0x24), `previousT` (+0x28), `rootPosition` (+0x2c..+0x37),
object size 0x38 (`operator_new(0x38)` in `Init`, line 1203). The constructor (0x16374) initialises the base, `pGrid`,
`gridTime` and `rootPosition` only, **not** `previousT`. `Reset(scale)` is just `TScalingTask::Reset`, which does not
touch it either. So:

- On the very first run the value is whatever the allocator left in the block. A fresh page would give 0; a recycled
  block can give anything. The shipped game's behaviour on that first tick is therefore not defined by the code.
- On every later run, and after every resume in PLAYING (`SetGameState(2)` calls `Reset(1.0)` then `Restart`), the
  stale `previousT` of the last tick is carried over while `time` restarts at 0. The first tick sees `t < previousT`
  (almost surely) and calls `ShiftRows` once. CO already states this for resume; it also applies to Try Again.
- Engine: `previousT` starts at 0 (no shift at the first tick of the first game) and is never reset, so the carry-over
  on later runs matches the original (`Coffee.h`).
- The ShiftRows call precedes the position update in the same `Animate`: `if (t < previousT) ShiftRows();
  grid.y = t * dy + rootY; previousT = t`.

---

## Unknown

1. Whether the Mac static unicode-input flag at 0x1d9281 is ever set (selects char-then-keydown versus
   text-then-keydown; the order versus keydown is the same either way). Also not traced: `TPlatformData::GetEvent`
   (0x929b8), so strict in-order delivery of the two queued events is assumed.
2. Whether pause-time clicks have a runtime effect that I missed, such as the `information` Bitmap swallowing mouse
   events outside its drawn art. I read hit-testing as rectangle based (WS 6.5); I did not run the game.
3. The draw-stack rules of `DrawLayer` with flag 8 and the manager's +0xc window were read from `DrawLayer` only, not
   from `DrawStack`; `true` = top modal only is solid, the `false` walk is an inference.
4. Rotation direction of `TMat3::GetRotation` on screen (machine fingers, timer hand, gun).
5. The visual intent of the conveyor-piston gap (8.2) and the exact on-screen look of the tray-piston stack; needs a
   reference screenshot.
6. Value of `previousT` on the first coffee run (heap-dependent), and whether the unset flag byte `+0xdc` before the
   first `ResetGame` is 0 (assumed from the constructor not appearing to set it; the first-StartGame 20 ms follows
   only if `ResetGame`'s `PauseGame` sets the flag).
7. Layer ordering of marker versus grid ingredient sprites (5).
8. Whether the Windows build that runs Reforged enumerates `custom/<cat>/` in sorted order (7).
9. `SlotWheel` mechanics (not asked in detail; still open).
