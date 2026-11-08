# Rollover popup placement and quest-log row layout

Sources: `re/mac/decomp/Rollover.c`, `PopupWindow.c`, `TPopupTestRect.c`, `TWindow.c`; Lua in the original install's `assets/ui/ledger.lua`,
the original install's `assets/sim/factory.lua`, the original install's `assets/ui/ui_questlog.lua`, the original install's `assets/ui/style.lua`.
Addresses are Mac image offsets as in `re/mac/index.tsv`.
Note: `re/choc3/pfp_extracted/` holds no Lua; the original install's `assets/` is the vanilla Lua used here.

## 1. Which native places the popup, and the rule

The `Rollover` window (`Rollover::OnMouseMove`, 000345b4) builds the popup; `PopupWindow::Reposition` (0002e5a4) places it.

Sequence in `Rollover::OnMouseMove`, run only if the global `rolloversEnabled` is nonzero (it also returns that flag):
1. Take the Rollover's own screen rect (`GetWindowRect`, parent-accumulated) and wrap it in a `TPopupTestRect` (the keep-visible test).
2. Create a `PopupWindow` holding that test, add it as a child of the top modal window (vtable slot 0x24, arg 0).
3. `SetContents(contents)` runs the Lua `contents` string with the popup as the window being built (`TWindowManager::DoLuaString`),
   then `FitToChildren`, alpha 0, and a `FadeInTransition` to 1.0 over 0x7d = 125 ms.
4. Anchor = the mouse point (in the Rollover's client space) converted by `ClientToScreen`, plus `xOffset`/`yOffset`.
   If `xAbsolute` (resp. `yAbsolute`) is >= 0 it replaces that axis outright (absolute is screen coordinates; default -1).
   Style keys read in `Rollover::Init` (00034338): `xOffset`, `yOffset` (default 0), `xAbsolute`, `yAbsolute` (default -1),
   `contents` (string), `command` (Lua function). `SetPopupPosition(x,y)` (000341f0) just sets the two absolutes.
5. `PopupWindow::Reposition(anchorX, anchorY)`, then the command is attached: a Lua function (if given) else the
   string at Rollover+0x44 (the click string used by the native-constructed Rollover) is stored in the popup.
6. `AddMouseListener(popup)`.

`PopupWindow::Reposition` rule, exact:
- Candidate rect = popup client rect (0,0,w,h) moved to (anchorX, anchorY).
- Limits come from the window manager's screen window (`TWindowManager+0xc`, its rect): maxX = width - 4, maxY = height - 4.
- If candidate right > maxX: shift left by the overshoot (right edge lands at maxX) AND add 32 (0x20) to y.
- Then (after that y change) if candidate bottom > maxY: shift up by the overshoot (bottom edge lands at maxY).
- Then `SetWindowPos`. There is no flip above the anchor and no left/top clamp. The +32 on the right-edge case pushes the
  popup below the pointer so it does not sit under it; the bottom case is a plain clamp, so a flip does not exist.
  Behaviour for a popup taller than the screen is therefore a negative y (not guarded).
- Placement is evaluated once, when the popup is created. It is never repositioned afterwards.

Re-evaluation and lifetime:
- A new popup is created on each `Rollover::OnMouseMove` call that reaches the Rollover; nothing in the Rollover guards against
  a second popup. Whether the existing popup stops the duplicates (it is a front child of the modal and a mouse listener) is
  Unknown from the decomp alone.
- `PopupWindow::OnMouseMove` (0002e7a0): converts the point to screen space and asks `TPopupTestRect::KeepVisible`
  (00173180): inside the Rollover rect when left <= x < right and top <= y < bottom (half-open). If outside, `Kill` and the
  move is not consumed; inside it returns true.
- `OnMouseDown` sets a pressed flag (+0x6c) and consumes. `OnMouseUp` (0002e6e0) always kills the popup first, then if pressed:
  runs the Lua command function if there is one, else `DoLuaString` on the click string (when non-empty). Flag cleared.
- `Kill` (0002e696): remove mouse listener, notify parent (vtable 0x28), `BSGHideWindow`, `SafeDestroyWindow`.

## 2. Popup size

Not fixed, no min or max. `PopupWindow::PostChildrenInit` and `SetContents` call `TWindow::FitToChildren` (0004aa2c):
the window is resized to the bounding box of its children (starting from the current size, which is 0 for a fresh popup).
The factory popup is built by `Factory:LedgerRolloverPopup` (`sim/factory.lua:132`): `TightText` lines of h=15 at y=0,15,30..., each
w=400 but tight-fitted to the measured text, plus an ingredient bitmap, so size = widest line by 15 x line count.
The vanilla `Rollover` wrapper itself is also `FitToChildren`ed in `PostChildrenInit` (000341ca), a separate thing.
`PopupWindow` ctor sets +0x38 = 0 (fill off, matching the engine's `nofill`).

## 3. Engine today (all agrees with the original)

- `src/ui/widgets/InputWidgets.cpp`, `PopupWindow::reposition` (line ~503): same rule (margin 4, right-edge shift + 32 on y,
  then bottom clamp). Matches 0002e5a4.
- `RolloverWindow::onMouseMove` (line ~575): same sequence, anchor = pointer + offsets, absolutes override, one-shot placement,
  125 ms fade (`kFadeMs`), keep-visible half-open rect test. Uses `wm.screen()->rect()` (800x600) for limits.
- The symptom is not a defect. The survey script `19_hover.script` moves the pointer to 270,540; the popup is 170x45,
  so its bottom is 585 and maxY is 596. Nothing overshoots, so the rule leaves it at (270,540). The screenshot
  `19_hover_factory.png` shows all three lines visible. The expected behaviour was never "flip above"; the original clamps,
  and only when bottom > 596 (pointer y > 551 for this popup).
- To see the clamp in the engine, hover lower, e.g. y=590: expected popup y = 596 - 45 = 551.
- Only unverified piece: that `TWindowManager+0xc` is the 800x600 screen window rather than something 603 high (the modal
  dumps as 800x603). A 3 px difference would not change the 540 case.

## 4. Quest-log row layout

Original data, not an engine misplacement. `ui/ui_questlog.lua:121-137` places `quest1..quest10` as `QuestSelectButton`
at x=0, y=59,89,...,329 (30 px pitch), all inside the `image/popup_back_quests` bitmap (x=0,y=13). Every row is the same
337x32 button at x=0 (the dump confirms rect=0,y,337,32 for all rows).
The difference is the art: radio graphics `button_quest_title_up/selected/over` are all 337x32, but the opaque pixels differ
(measured on the original install's `assets/image/`): up = columns 23..301, rows 4..27; over = 20..304; selected = 0..326, full height.
So unselected rows look inset (23 px of transparent padding) and the selected row's red tab begins at the button edge, x=0,
bleeding to the left bezel; the quest dialog window itself is at x=4, so the tab starts at screen x=4.
Text positions come from `QuestSelectButton` (`ui/style.lua:546`): unselected/over layers (0, 2) use text x=tx+27 with
`questUnselectedFont` (16 pt); selected layers (1, 3) use x=tx+3 with `questSelectedFont` (18 pt). The dump matches (3 vs 27).
No native positions the row; layer choice is the Button radio state (layer 1 = selected). `03_questlog.png` is consistent.

## Unknown

- Identity of `TWindowManager+0xc` (assumed the screen window; not confirmed in the decomp) and its exact height (600 vs 603).
- Whether repeated `Rollover::OnMouseMove` calls while a popup is already showing create duplicate popups (mouse dispatch
  order in `TWindowManager` not traced).
- `rolloversEnabled` writers (probably a user option) not traced.
- The Rollover `fit` Lua key (used in `ledger.lua` and others) is not read in `Rollover::Init`; presumably handled by
  `TWindow::Init`/`SizeAndPositionFromStyle`. Unverified.
