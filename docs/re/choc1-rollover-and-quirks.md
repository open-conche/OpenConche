# Chocolatier 1: `Rollover` semantics and `WindowQuirks`

Answers open question 3 of `choc1-fit.md`. Sources: the Windows exe decompile (`re/choc1/decomp/_unnamed.c`, addresses below are exe
virtual addresses), vtables read from the Windows install's `Chocolatier.exe`, and the shipped Lua bytecode
(`re/choc1/pfp_extracted`, disassembled with a workbench script; Lua cites are `file:line` of the original source, as in the
bytecode line table). Labels: **fact** (read in the decompile or bytecode), **inference** (reasoned from facts), **unknown**.

No class names exist in the index, so classes were located through vtables: `TRollover` vtable `004e8afc` (name function `0041a880`),
`TRolloverPopup` `004e8a4c` (`0041a8a0`), `TWindow` `004e927c`, `TModalWindow` `004fd97c`, `TButton` `004ea32c`. Slot numbers below count
4-byte entries from the vtable start.

## 1. Window event routing (needed to read the Rollover)

**Fact.** `TWindow::HandleEvent` is `004225d0(event, flag)`. It calls a virtual by event type: 5 mouse down = slot 14 (`+0x38`),
6 = slot 15, 7 mouse up = slot 16 (`+0x40`), 8 mouse move = slot 17 (`+0x44`), 10 = slot 23 (`+0x5c`), 0xb = slot 21, 0xc = slot 22,
0xd char = slot 20 (`+0x50`). `TWindow` defaults for slots 14/16/17 return 0 (`00402e30`, `00402e40`, `00402e50`). On mouse down (5) it also
calls slot 24 (`+0x60`, focus) on the first ancestor with flag bit `0x2`. If the handler returns 0 and the window does not have flag
bit 0 of `+0x28` (script-handled), the event falls through (section 6.1).

**Fact.** `TWindowManager::HandleEvent` is `0042e3e0`. Event 2 is ignored; event 1 (idle/paint) runs the task list of the top modal only
(`00455e60` -> task manager `00430510`); events 5-8 and 10 go to the **mouse listener list** (`wm+0x30`, add `0042ebf0`, remove `0042ecd0`) if it is
non-empty, otherwise to the deepest child under the pointer of the top modal (`00421b90`). With listeners: each listener whose screen rect
(`00421060`) contains the pointer gets the event with the point converted to its own coordinates (`00421130`); a listener that does not
contain it gets slot 18 (`+0x48`, a "pointer left" call) and is skipped only if that call removed it. The event never reaches the hit-test while
the list is non-empty. This is the same structure the Chocolatier 2 notes describe (`choc2-bottom-hud-clicks.md` 0.1 item 3).
Inference: a `TRolloverPopup` does not remove itself on slot 18 (its slot 18 is the empty default `00401260`), so it keeps receiving events
even when the pointer is outside it. The hot rectangle below only makes sense if that is so.

## 2. `TRollover::Init` (`0041b960`): keys and defaults (fact)

Read in this order. `hotWidth` defaults to `hotSize` and `hotHeight` to `hotWidth` (the value read for `hotWidth`).

| Key | Default | Stored at | Use |
|---|---|---|---|
| `hotSize` | 40 | local | default for the next two |
| `hotWidth` | `hotSize` | `+0x6c` | hot rectangle width |
| `hotHeight` | `hotWidth` | `+0x70` | hot rectangle height |
| `xOffset` | **20** | `+0x5c` | popup offset from the anchor |
| `yOffset` | 0 | `+0x60` | |
| `xAbsolute` | -1 | `+0x64` | |
| `yAbsolute` | -1 | `+0x68` | |
| `script` | `"ui/rollover.lua"` | `+0x58` | file run to build the popup |
| `target` | `""` | `+0x54` | a Lua expression string, see 5 |
| `image` | `""` | `+0x40` | name of a `BitmapTint` to pulse while the popup is open |
| `clicksound` | `""` | `+0x4c/+0x50` | loaded with the sound loader only if non-empty |
| `popupsound` | `""` | `+0x44/+0x48` | same |
| `x`, `y` | 0, 0 | window position (`004211d0`) | |
| `w`, `h` | 0, 0 | window size (`00421640`) only if both are > 0 | |
| `area` | none | list at `+0x34` | table of tables, 1-based, each `{x1, y1, x2, y2}` |

The constructor (`0041b590`) allocates a one-byte "popup is open" flag, shared (ref-counted) with the popup, at `+0x74/+0x78`, initially 0.
Slot 6 (`0041b160`, post-children init) sizes the Rollover to the union of its children's rectangles starting from (0,0,0,0), so with no `w`/`h`
its size is (max child right, max child bottom). Every shipped Rollover has children (a `Bitmap`, `Text`, `BitmapTint` or `SimpleAnimation`
in the array part) and none sets `w`/`h` (see 7). Sound load category is 1000 (SFX), as `choc1-natives.md` says.

## 3. `TRollover` mouse move (slot 17, `0041b240`) (fact unless marked)

Input: the pointer in the Rollover's own coordinates (the manager already converted it).

1. If the `area` list is non-empty: the pointer must be inside one rect, tested `x1 <= px < x2` and `y1 <= py < y2`. If it is in none, the
   function returns **false** (event not handled, so it falls through to the sibling behind, section 6.1). With no `area` the result is **true**
   (handled) from here on, whatever happens next.
2. If the shared open flag is 0 **and** the global counter `DAT_005330a0` is 0, a popup is created; otherwise nothing happens (still returns true).
   `DAT_005330a0` is incremented in the map-travel window's Init (`00417da0`) and decremented when it ends (`00417800`, the place that queues
   `gSim:ArriveAtPort`). So rollovers are **disabled while a map trip is running** (inference on the meaning of that window; the counter use is fact).
3. Creation: set the open flag, build a `TRolloverPopup` (ctor `0041aee0`, below), then set the Lua global `_rollover` to `target` (the
   call `0043a180` takes `target` and the literal `"_rollover"`; inference for the argument roles, supported by `ui/rollover.lua` reading the global
   `_rollover`), then run the `script` file with the popup as the window being built (`0042d2d0` -> `00465300` + `0043b3b0`; the same path the Lua
   `DoModal`/`DisplayDialog` loaders use, inference), then post-children init, then placement:
   * Pointer mode: popup at `(px + xOffset, py + yOffset)`, `px, py` converted to the parent's absolute space (`004210f0`).
   * Absolute mode, used only when **both `xAbsolute >= 1` and `yAbsolute >= 1`** (the test is `< 1` for either meaning pointer mode):
     popup at `(xAbsolute + xOffset, yAbsolute + yOffset)`. The offsets are **added** to the absolute point.
   * Clamp to a hard-coded 800x600 screen (constants 800 and 600 in the code): if the popup's right edge is beyond 800, move it left by the
     excess **and down 32** (that move is applied to the rect, the later bottom test sees it); then if the bottom is beyond 600, move up by the excess.
     No margin (the engine uses 4).
   * Hot rectangle (`popup+0x48..0x54`, left/top/right/bottom, in the popup's own coordinates): centred on where the pointer was at
     creation, `hotWidth` x `hotHeight`. In pointer mode the pointer sits at popup-local `(-xOffset, -yOffset)` (adjusted by the clamp shifts), so the
     rect is `(-xOffset - hotW/2, -yOffset - hotH/2)` to `+hotW, +hotH`. In absolute mode the code adds `(xAbsolute - px)` to that
     (`px` the converted pointer), which is the opposite sign of the pointer's true offset from the anchor `(px - xAbsolute)`.
     **RESOLVED (re-read at 0041b2df..0041b396): the sign is real, not a decompiler artefact** - the popup is positioned at
     `(xOffset + xAbsolute)`, and its popup-local rect gains `(xAbsolute - px)`, so in screen space the rectangle's centre is
     `2 * xAbsolute - px` (the pointer mirrored about the anchor); the engine's literal reading is correct. The clamp shifts
     still apply to the rect in both modes; the engine does not shift its hot rectangle with the clamp (a known small deviation).
   * The popup is registered in the manager's window list as the current popup (`wm+0x3c`) and added to the mouse listener list (`0042ebf0`).
   * Last, `popupsound` is played if set (`00432d30`): **the popup-open sound**.
4. Return true.

`TRolloverPopup` constructor (`0041aee0`, fact): only one popup exists at a time, a static count `DAT_005330a4` and the last popup `DAT_005330a8`;
creating a new one first closes the previous one (`0041ac60`). If `image` is non-empty it schedules an image pulser (the `TImagePulser` class at vtable
`004e8a34`, ctor `0041a8c0`, repeating every **100 ms**, count 0): each tick sets the `BitmapTint` named `image` to a grey from the cycle
`1.0 0.9 0.8 0.7 0.6 0.6 0.7 0.8 0.9 1.0` (table at `00525580`, one entry per tick), and the task ends when the popup's open flag goes to 0. On close
the tint is reset to `(1,1,1,1)`.

## 4. The popup's own events and close conditions (fact)

* Mouse move (slot 17, `0041ade0`): if the pointer, in popup coordinates, is **outside** the hot rectangle (`x < left`, `x >= right`, `y < top`,
  `y >= bottom`) the popup closes (`0041ac60`). Always returns true. This is the keep-visible test; it is a rectangle around the pointer's position
  at open time, not the Rollover's rectangle.
* Mouse down (slot 14, `0041a8b0`): returns true (swallowed). No pressed state is kept.
* Mouse up (slot 16, `0041ad60`): closes the popup, then runs the Lua text `table.insert(gCommandQueue,DispatchRolloverClick)` (string at `004e8ab4`),
  then plays `clicksound` if set. Returns true. Any mouse up that reaches the popup does this, whether the press started there or not (inference: the
  listener list delivers it, section 1).
* Engine deviation (pulser phase): the exe's popup pulser and a `UtilPulseImage` pulser on the same image are independent 100 ms tasks, so
  their writes interleave. The engine starts the popup's pulser on the running `UtilPulseImage` pulser's phase (`index`, next tick) so a hovered
  building pulses at the normal speed during the port intro.
* Close (`0041ac60`): stops the pulser and resets the `BitmapTint`; removes the popup from the manager's lists and from its parent
  (parent slot 10, `+0x28`); clears the shared open flag in the destructor (`0041ab50`). There is no fade in or out.
* Not closed by: leaving the Rollover's rectangle (only the hot rect counts), a key press, the Rollover being destroyed (inference; not read).

`DispatchRolloverClick` is Lua (`ui/standard.lua:154`): it reads the global `_rollover`, sets it to nil, and if it is a table with an
`OnPopupClick` method calls `t:OnPopupClick()`. The global holds the object `ui/rollover.lua` evaluated from `target` (next section), so a click on a
hovered item runs that object's `OnPopupClick`.

## 5. `script` and `target` (fact)

`ui/rollover.lua` (default `script`) reads the global `_rollover` (asserted to be a string), evaluates `loadstring("return " .. _rollover)()` and stores the
result back in `_rollover`. It then builds the popup from the result `r`:

1. If `r` is a function, call it and use its result. If it is a table with a `CreatePopup` field, call `r:CreatePopup()` and use that result.
2. Table: `MakeDialog(table)`. Function: `MakeDialog{ result }`. String: `MakeDialog{ Rectangle{x=0,y=0,w=800,h=600,shrink=true,
   Text{x=5,y=0,w=kMax,h=infoFont[2],label="#"..string,font=infoFont}} }` (a plain text tip). Other (nil): nothing is built, the popup stays empty.

So `target` is a Lua expression such as `LItem:ByName('x')`, `LPort:ByName('p')`, `LBuilding:ByName('b')`, `LFactory:StatusPopup('p')` or
`GetMedalRollover(n)`, and the objects supply `CreatePopup` (`sim/item.lua:374`, `sim/port.lua:295`, `sim/building.lua:239`, `sim/medals.lua:141`,
`ui/buysell.lua:44`) and `OnPopupClick` (`sim/item.lua:380`, `sim/port.lua:281`, `sim/building.lua:233`, `sim/medals.lua:142`, `ui/buysell.lua:45`).
`target` is the Chocolatier 2 `contents` key under another name and with a different mechanism (a global plus a script file, not a command run in the popup).

**Added for the port view (fact, `0041a5f0`):** the string popup's `Rectangle{..., shrink=true}` is not a plain rectangle: `shrink` resizes its Text children to their measured width + `pad` (5) and the
rectangle (and its parent) to the union of the children plus `pad` at the bottom; the default fill is white. The engine's `Rectangle` now implements it (`choc1-fit.md` section 6, "Implemented for the port view"). Also fact from the run: the building
tooltip is that string popup (`LBuilding:CreatePopup` returns the `Rectangle` table, `sim/building.lua:239`), and `OnPopupClick` opens the building's dialog.

## 6. Quirks

### 6.1 `fallThroughPoint`: **true** for Chocolatier 1 (fact)

The C1 fall-through is `004225d0`, the same code shape as the C2 one in `choc2-bottom-hud-clicks.md` 0.1. For mouse events (5-8, 10) the window adds its own
`left/top` to the point (`00421170`, so it is in the parent's space), finds the next sibling behind it whose rect contains the point (`00421aa0`, rects in
the parent's space, visible-bit test `0x100` up to the modal), then calls `00421b90(S, &pt, -1)`, which looks for a child of `S` with
`00421b70(S, pt)` **without subtracting `S`'s position first** (the subtraction `00421190` happens only for the levels below). So the first
level is tested with the point still in the parent's space, exactly the C2 skew (Chocolatier 3 translates first). The event itself is converted correctly
afterwards (`004210f0` then `00421130`). The direct hit path (`00421b90` from the manager) subtracts correctly.

### 6.2 `popupFader`: **false**, plus a missing "no fade" mode (fact + inference)

C1 has no `TPopupWindow` and no `TFader` class (neither string exists in the exe; the only popup class is `TRolloverPopup`, whose constructor and close read in
full above contain no animation except the `image` pulser). Fact: the popup appears and disappears at once. Inference/gap: the engine's `popupFader = false` path is the
Chocolatier 3 smooth fade, so a Chocolatier 1 popup would fade in where the original does not. The engine should add an instant mode (or
make the C1 Rollover not use `openContentsPopup`'s transition); `popupFader = true` would be wrong too (stepped fade, Rectangle alpha 0.8).

### 6.3 `cancelDefault`: **false** (fact)

`TButton` flags (`FUN_00444790` init, string table `004ea39c..`): `beginGroup` 1, `sendToParent` 2, `close` 4, `default` 8. `TModalWindow::OnChar` (slot 20,
`00456140`) handles only the char `0x0d` (value at `00525cd8`): it runs a `FindDefault` visitor over the modal and presses the `default` button (`00444440`);
with none it posts a message of type 2 to the focus window (meaning unknown). Escape is not handled there. C1 `ui/buysell.lua` has **no `default` and no
`cancel`** anywhere; its two dismissing buttons are the sell-all button (`buysell.lua:330`, `close=true`, `sound=audio/sfx/sell.ogg`) and the yes/OK button
(`:355`, `close=true`). The Lua that does use `default` is `entername.lua:25`, `hiscoresubmit.lua:292`, `logobuilder.lua:78`, `dev/devfactories.lua:56`, all affirmative
buttons with no `label`. The C2 quirk (Cancel carries `default`, so Enter cancels) has nothing to act on, so the flag stays false.

### 6.4 `spriteHitAlpha`: **0** (fact + inference)

C1's port scene has no `TPort`; the exe has `TSprite`/`TAnimatedSprite` but no building hover test. The port buildings are Lua `Rollover`s with an `area` rect list
(`sim/port.lua:373`), so the hover region is those rectangles, not a pixel alpha. The alpha-192 hit test does not exist in this game (inference from the absence of
a port class and of any `HitTest` string; the `TSprite` methods were not read).

### 6.5 `rollover`: **true**, but see the gap table (fact)

`xOffset` default 20 and `area` are in `TRollover::Init`, as in C2. The sound key differs (`popupsound`, not `rolloverSound`) and C1 adds `clicksound`,
`hotSize/Width/Height`, `image`, `script`, `target`.

### 6.6 The two C2 behaviours

* **Fall-through point:** same as C2 (6.1).
* **Windows under a modal do not run:** fact for the mechanism: `TWindowManager::HandleEvent` event 1 runs the task list of the top modal only
  (`0042e3e0` -> `00455e60(top, 0)` -> `00430510`), and each `TModalWindow` owns its task manager at `+0x34`. So animations and tasks owned by a covered modal
  do not run. Unknown: whether a covered modal's clock is also paused so its tasks resume without catching up (the C2 `TClock` pause/restart was not looked for);
  `choc1-fit.md` 6 already relies on "modal clock" for `SimpleAnimation`.

## 7. How the shipped Lua uses `Rollover`

Complete list, by bytecode scan of every `.lua` under `re/choc1/pfp_extracted`. `ui/portview.lua`, `ui/ledger.lua`, `ui/map.lua`, `ui/ledgertabs.lua` contain **no** `Rollover`;
the rollovers on those screens come from the `sim` files that build their contents.

| Where | Keys set | Notes |
|---|---|---|
| `sim/port.lua:222,225,230,233,237,241` (`LPort.LayoutMap`, the map's port icons) | `x y xAbsolute yAbsolute xOffset yOffset hotSize=60 target="LPort:ByName('<p>')" clicksound=kDefaultPopupClickSound` (the last icon type, `:241`, has no `clicksound`); child `SimpleAnimation` or `Bitmap` | `xAbsolute/yAbsolute` = the port's `mapx`, `mapy`; `x,y` = that minus 17; `xOffset` = 0, or minus the tag width when the tag would pass the screen's right edge (`mapx + travelTagWidth >= screenWidth`); `yOffset` = minus half the tag height. This is the only place that uses absolute mode. |
| `sim/port.lua:373` (`LPort.UIPortView`, buildings on the port screen) | `x y hotSize=10 area name target="LBuilding:ByName('<b>')" clicksound image="<b>_img"`; child `BitmapTint name="<b>_img" image="ports/<img>" scale` | `area` is the building's hit rect list from its `layout` table; `image` is the tint the popup pulses. Followed by `UtilPulseImage("<b>_img", 30)` (`:376`). |
| `sim/item.lua:283,318` (ledger inventory icons: ingredient sack, product crate) | `x y target="LItem:ByName('<i>')"`; child `Bitmap` and `Text` | defaults for all else |
| `sim/factory.lua:469, 616`, `ui/factorystatus.lua:12` | `x y target="LItem:ByName(..)"` / `"LFactory:StatusPopup('<p>')"` | the status rollover at `:603` has `x=0 y=0`, owned factories only |
| `sim/medals.lua:179` | `x y target="GetMedalRollover(n)"` | |
| `ui/buysell.lua:75,102,127,149,252,304` | `x y target="LItem:ByName(..)"` | dialog item icons |
| `ui/receipt.lua:53,76`, `ui/farmreceipt.lua:21,30`, `ui/recipelist.lua:61,77`, `ui/recipeselect.lua:75,78` | `x y target="LItem:ByName(..)"` | |

Keys never set by any shipped Lua: `popupsound`, `hotWidth`, `hotHeight`, `script`, `w`, `h`. `kDefaultPopupClickSound` is `audio/sfx/click_2.ogg` (`ui/styles.lua:149`).
For the port view and ledger: `x y target`, `hotSize`, `area`, `name`, `image`, `clicksound`, plus the `xOffset` default of 20 (every plain Rollover uses it) and the
`_rollover` / `ui/rollover.lua` / `DispatchRolloverClick` mechanism are what matter. Absolute placement matters for the map.

## 8. The engine's `Rollover` against C1

Engine: `RolloverWindow` in `src/ui/widgets/InputWidgets.cpp` (`init` `:547`, `onMouseMove` `:620`, `openContentsPopup` `:582`) and `PopupWindow` (`:513`).
Today it opens a popup by running the `contents` string (a Chocolatier 3/2 key) inside the popup, keeps it while the pointer is inside the Rollover's own screen
rectangle (and the owner's `keepsPopup`), and a release that began on the popup closes it and runs `command`.

### Table: Rollover key

| Rollover key | C1 semantics | engine today | gap |
|---|---|---|---|
| `x`, `y`, `w`, `h` | window rect; `w`/`h` only if both > 0; then fit to children (union from origin) | honoured by `Window::init` and `fitToChildren` | none (C1 Lua sets neither `w` nor `h`) |
| `target` | Lua expression text; sets global `_rollover`, then `script` evaluates it and builds the popup (section 5) | not read; engine reads `contents` | **missing**: C1 needs the `_rollover` global + script path, or map `target` to a `contents` string that does the same |
| `script` | file run to build the popup, default `ui/rollover.lua` | not read | missing (only needed if `ui/rollover.lua` itself is run; always default in shipped Lua) |
| `clicksound` | played on the popup's mouse up, after the close and the `DispatchRolloverClick` post | not read | **missing** |
| `popupsound` | played when the popup opens | not read (`rolloverSound`, the C2 key, only with the quirk) | missing for C1 (never set by shipped Lua, so harmless) |
| `hotSize` (40), `hotWidth`, `hotHeight` | keep-visible rect: that size centred on the pointer's position at open, in popup coordinates; the popup closes on the first move outside it | not read; keep rect = the Rollover's screen rect | **differs**: with the C1 default (40) and `xOffset 20` the popup closes as soon as the pointer moves 20 px right; the port buildings (`hotSize 10`) close after 5 px |
| `xOffset` (20), `yOffset` (0) | added to the pointer, or to the absolute anchor | read; default 20 only with the C2 quirk; ignored in absolute mode | default fine with `rollover = true`; **absolute mode drops the offsets** |
| `xAbsolute`, `yAbsolute` (-1) | absolute mode only if **both >= 1**; popup at `(abs + offset)`; else pointer mode for both axes | each axis independently replaces the pointer coordinate when >= 0, no offset | **differs** (both-axes rule, `>= 1`, offsets added) |
| `area` | list of `{x1,y1,x2,y2}`, half-open, in Rollover coordinates; outside all -> event not handled (falls through) | read only with the C2 quirk; same test and return value | none with `rollover = true` |
| `image` | name of a `BitmapTint` pulsed every 100 ms through the 10-entry cycle while the popup is open, reset on close | not read | **missing** (cosmetic: building brightness pulse) |
| `name` | window name (base key) | base | none |
| (not a key) | one popup at a time; none while a map trip runs (`DAT_005330a0`); open flag per Rollover | one popup per Rollover instance only | missing global rules |
| (not a key) | placement clamp to 800x600, no margin, +32 y when pushed left | `PopupWindow::reposition` uses the real screen size and a 4 px margin | small difference (same +32 rule) |
| (not a key) | popup appears and disappears instantly | fades in (Chocolatier 3 style, or C2 stepped with the quirk) | **missing** no-fade mode |
| (not a key) | any mouse up reaching the popup closes it and clicks | needs the press to have started on the popup | harmless difference (a listener sees both) |

## 9. Quirk table

| Quirk | C1 value | evidence |
|---|---|---|
| `fallThroughPoint` | **true** | fact: `004225d0` tests the sibling's children with the point still in the parent's space (`00421b90`/`00421b70`, no `00421190` before the first level), as C2 |
| `popupFader` | **false**, and the engine needs an instant mode | fact: no `TPopupWindow`/`TFader`; `TRolloverPopup` ctor/close have no fade (`0041aee0`, `0041ac60`) |
| `cancelDefault` | **false** | fact: `buysell.lua` has no `default`/`cancel`; `TModalWindow::OnChar` (`00456140`) handles Enter only; C1's `default` buttons are the affirmative ones |
| `spriteHitAlpha` | **0** (not used) | inference: no `TPort`; port hover is `Rollover` + `area` (`sim/port.lua:373`) |
| `rollover` | **true** | fact: `xOffset` default 20 and `area` in `TRollover::Init` (`0041b960`); the other C1 keys are extra (section 8) |

## Unknown

* The sign of the absolute-mode adjustment of the hot rectangle (section 3 step 3): read literally it is mirrored about the anchor. Needs the disassembly of
  `0041b240` around `0041b4xx` to settle; effect is limited to the map icons.
* What the message of type 2 sent by `TModalWindow::OnChar` when no `default` button exists does (and so what Enter does in the buy dialog).
* Whether `TModalWindow` handles Escape elsewhere (slot 21/22 or a Lua key hook); `0x0d` is the only char slot 20 tests.
* Whether a covered modal's clock is paused (section 6.6), and what the popup does on a mouse up that is outside its window and the hot rectangle (inference only).
* The meaning of the second argument of `0043a180` and of `00465300`/`0043b3b0` (read as "set global" and "run file in a window context" from their callers and from `ui/rollover.lua`).
* Which window class the map-trip counter `DAT_005330a0` belongs to beyond "has `endName`, `vehicle`, and queues `ArriveAtPort`".
