# Chocolatier 2: bottom HUD tabs (Messages / Factory Status / Inventory), clicks and layering

Scope: the three ledger tab buttons next to the money badge in the port view. Sources, in order of trust: the unstripped Chocolatier 2 Mac decomp `re/choc2/decomp/` (`TButton.c`, `TText.c`, `TLedger.c`, `TPopupWindow.c`, `TPort.c`, `TSprite.c`, `TRollover.c`), the Chocolatier 2 Lua bytecode `re/choc2/pfp_extracted/ui/ledger.lua` (disassembled with a workbench script; line numbers are the bytecode's line table), then the engine and a headless run.

Labels: **fact** = read directly from the Lua bytecode or decomp; **inference** = derived from a shared code path or measurement; **unknown**.

Screen coordinates below are the 800x600 logical screen. `ui.xOffset`/`ui.yOffset` are 0 at 800x600 (the engine dump shows the ledger at `0,290,800,310`); at other sizes everything shifts together.

## 0. Mouse fall-through and the hidden quest bar

* The quest bar geometry (fact): `ui/ledger.lua:60-64`: the `quest` Bitmap (ledger-local (235,y), 587x129) holds a `Rollover` at (44,62,497,39) with `contents = "gSim:RolloverCurrentQuest()"`. `y` is `showQuestY` = 0 with a quest hint, `hideQuestY` = 53 without one. Hidden, the Rollover's screen rect is (279,405)-(776,444) (ledger origin (0,290)), which covers the lower rows (405..428) of all three tabs (tab rows 393..428) and the top of the body. Shown, it is (279,352)-(776,391) and misses the tabs, so the bug only exists while no quest hint is set.

### 0.1 The fall-through rule (fact from the C2 decomp and disassembly)

**Fact: Chocolatier 2's mouse fall-through does not translate the point into the sibling's coordinate space when it looks for the sibling's children.**

* The fall-through is `TWindow::HandleEvent` (C2 0x34028, `re/choc2/decomp/TWindow.c` lines 1345-1470): a window that does not handle a mouse event hands it to the next sibling behind it that contains the point, else to the parent. After choosing the sibling `S` (the `NextChildWindowFromPoint(parent, pt, ...)` at 0x3412b) the C2 code looks for a child of `S` with a second `NextChildWindowFromPoint(S, &pt, S.firstChild)` (call at 0x3414f) where `pt` is still in the **parent's** coordinates (the earlier `x += this->left` step only reaches the parent's space). `S`'s children have rects in `S`'s own space, so the test is shifted by `S`'s position. A found child is then hit-tested deeper with `pt - child.left/top` (0x34170-0x34194, again without `S`'s offset). The event handed over is converted correctly afterwards (0x341c8-0x3422b), so only the *choice of target* is skewed.
* Chocolatier 3's version (`re/mac/decomp/TWindow.c`, `TWindow::HandleEvent`) translates first: `pt - GetWindowRect(S)` before the child search. That is the code `docs/re/window-system.md` 6.5 and the engine follow. So Chocolatier 2 differs from Chocolatier 3 here (fact from the two decomps; the C2 value of `S` offset matters only when `S` is not at the parent's origin).
* The direct hit path (`TWindow::ChildWindowFromPoint`, C2 0x33d6c, used for the first target under the pointer) subtracts each level's offset correctly. So a Rollover is found normally when the pointer is directly over it, and only the fall-through is skewed.
* Effect on the quest bar (inference from the fact above plus the Lua geometry): the `quest` Bitmap `S` is at ledger-local (235, y). A move over ledger art that handles nothing (tab gaps, frame, body top) falls through ledger Bitmap -> `S`. The original tests the Rollover's `S`-local rect (44,62,497,39) against the point in **ledger** coordinates. Hidden (y=53) or shown, a point in the tab row has ledger y 103..138 (> 101), so it never matches: the Rollover is not reached and no popup is created. The skewed match region (ledger (44..541, 62..101), i.e. screen y 352..390) lies above the ledger Bitmap, where the pointer reaches the Rollover directly anyway. So in the original the hidden Rollover is effectively unreachable from the tab row; in the engine it is reached at its true position (279,405)-(776,444).
* Other candidates checked and ruled out (all fact from the decomp unless noted):
  1. Clipping: none. `NextChildWindowFromPoint` (0x32a9a) tests only the child's rect against the point and a "visible-bit 0x100 on itself and ancestors up to the modal" walk. A child sliding past the parent's bounds is still hittable.
  2. `TRollover::OnMouseMove` (0x2a4c0) checks only the static `rolloversEnabled` (initial value 1) and, if an area list exists, the point against it; no visibility, alpha, or content test. `TLedger::OnTaskAnimate` (0x1b058) only moves the bar (`SetWindowPos`); it never hides it or changes alpha. `hideQuestY` really is 53 (Lua constant, `ui/ledger.lua:57`) and the engine uses 53.
  3. Mouse routing (`TWindowManager::HandleEvent` 0x2ffc2, listener loop at 0x30229-0x30389, disassembled): when the listener list is non-empty the event goes to the listeners only and the function returns (no hit-test); a listener whose window rect does not contain the pointer first gets `OnMouseLeave` (vtable +0x4c) and is skipped only if that removed it (buttons do, `TPopupWindow` does not, so it still gets the event). The engine instead falls through to the hit-test when no listener handled the event (`Input.cpp` `routeMouse`); a secondary difference, not the cause here.
  4. A Rollover with empty contents still creates the popup: there is no content test in `TRollover::OnMouseMove`. `Simulator:RolloverCurrentQuest` (`sim/sim.lua:469`) returns nothing when `self.quest` is nil, so the popup is empty. `TPopupTestRect::KeepVisible` (0xfd17c) is a plain rect test and accepts a 0x0 popup.
  5. Ledger art is not mouse-opaque: `TImage` (`TImage.c`) has no mouse overrides, `TWindow::OnMouseMove/Down/Up` return 0, and `TImage` has no alpha hit test.
  6. The Rollover's rect is computed correctly in the engine (dump `rect=44,62,497,39` in the quest Bitmap at y=53 gives screen (279,405)); `hideQuestY` is not mis-set.

**Engine:** the coordinate rule is implemented as `WindowManager::setLegacyFallThroughPoint(bool)` (`src/ui/WindowManager.h`, used in the `routeMouse` fall-through in `src/ui/Input.cpp`). It is an engine-generation flag, set once in `App.cpp` from the detected game (`game == Choc2`), not a per-site game check; Chocolatier 3 keeps the translating default. Chocolatier 1: **unknown**, no `HandleEvent` decomp in `re/choc1/decomp`, so the flag stays off there until its `TWindow::HandleEvent` is read. Unit tests: `Fall-through:` cases in `tests/test_inputwidgets.cpp`.

## 1. Which elements are clickable

**Fact: each tab is ONE `Button` (type `kRadio`) whose art contains the icon. The label text is a separate `Text` window drawn on top of the button. Only the button handles clicks; the text and the icon are not hotspots of their own.**

Evidence, `ui/ledger.lua` lines 77-97 (all inside the `Bitmap{ image="image/ledger" }` at ledger-local (258,100), 540x208, so abs origin (258,390)):

| Tab | Button (name, parent-relative x,y; size = art size) | abs rect (x0,y0)-(x1,y1) exclusive | Art | Label `Text` (parent-relative x,y,w,h; flags) | label abs rect |
|---|---|---|---|---|---|
| Messages | `btn_messages` (1,3) 141x36, line 78 | (259,393)-(400,429) | icon at the left end | `lbl_messages` "Messages" (55,11,66,20); `kVAlignCenter+kHAlignCenter`; line 94 | (313,401)-(379,421) |
| Factory Status | `btn_factories` (170,3) 200x36, line 83 | (428,393)-(628,429) | icon (factory) in the centre | two texts: `lbl_factory` "Factory" (185,11,60,20), line 95; `lbl_status` "Status" (293,11,63,20), line 96; both centred | (443,401)-(503,421) and (551,401)-(614,421) |
| Inventory | `btn_inventory` (396,3) 142x36, line 88 | (654,393)-(796,429) | icon (sack) at the right end | `lbl_inventory` "Inventory" (407,11,75,20); `kVAlignCenter+kHAlignRight`; line 97 | (665,401)-(740,421) |

(Button sizes are the PNG sizes of `control/btn_<name>_up.png`: 141x36, 200x36, 142x36. The label strings are literal keys in the bytecode, resolved by `Text`; English text per `docs/re/choc2-port-and-ledger.md` 8.5.)

* **Fact:** none of the three buttons sets a `mask` key, and `standardStyle` (`ui/styles.lua`) has none either. `TButton::ReleaseCapture` (0x6e834) with no click mask returns "inside" for the whole client rectangle. So the hit area is the full art rectangle, including the transparent corners (the art is trapezoid tabs; alpha is under 16 in the rim corners). **Inference (measured on the PNGs):** the art is about 88-91% opaque, so the corners are small.
* **Fact:** the "Factory Status" tab is a single 200x36 hotspot with two labels either side of the icon. There is no separate "icon" hotspot and no separate "text" hotspot for any tab.
* **Fact (the text never takes the click):** `TText::OnMouseDown` (C2 0x5c6ac) returns 0 unless the text has a hyperlink under the pointer (`TTextGraphic::Pick` non-empty); `OnMouseMove`/`OnMouseUp` likewise only act on links. These labels are plain text, so the event is unhandled by the `Text` and the generic dispatch passes it to the next sibling behind (the button) and then the parent (`docs/re/window-system.md` 6.5; same pflib code as C3, **inference** that C2 `TWindowManager` is identical, not re-read in the C2 decomp).
* **Fact (siblings, z-order):** the Lua creates children in this order inside the ledger Bitmap: `contents` Window (21,40,498,157), then the three buttons, then the four `Text` labels. Later = on top (`window-system.md` 6.2). So labels are above buttons, buttons above the `contents` window; the buttons occupy rows 3..38 of the Bitmap and `contents` starts at row 40, so they do not overlap.
* **Fact (overlap with the money badge):** the Ledger's own children are, in creation order: `quest` Bitmap (235,0, 587x129), the `image/ledger` Bitmap (258,100), the `image/ledger_badge` Bitmap (0,72, 263x237; `ui/ledger.lua:101`). The badge holds the `badge` DynamicWindow (`ui/ledger_badge.lua`: Text `money` at (41,110), `rank` (41,138), `date` (41,156), `score` (41,176) in the badge's coordinates) and has **no buttons**. The badge Bitmap is on top of the ledger Bitmap and overlaps it in x 258..262 (5 columns), so the left 4 columns of `btn_messages` (x 259..262) lie under a plain Bitmap. A plain Bitmap does not handle mouse events, so the event falls through to the button. In the engine the hover test at x=259,260,261 shows the button hovering (headless run), so this overlap does not block.
* **Fact:** the "Travel/Port", "Recipes" and "Pause" buttons (`ledger.lua:26-46`) belong to the badge-side group (before the Ledger, drawn from x=16..226); they are not part of this question.
* **Fact:** the quest bar (`quest` Bitmap, `Rollover` at (44,62,497,39) with `Text quest_text`) is the Ledger's first child, i.e. lowest. Shown position y=0 (`showQuestY`), hidden y=53 (`hideQuestY`). The quest bar is behind the ledger Bitmap in z-order.

## 2. What a click does

* **Fact:** the commands (closures at `ledger.lua:81, 86, 91`) call `SetLedgerContents("messages")`, `SetLedgerContents("factories")`, `SetLedgerContents("inventory")`. Nothing else (no sound call, no dialog, no `DoModal`). So the tabs **switch the body in place**; they do not open dialogs.
* **Fact (`TLedger::DoSetContents` 0x1ad52, `TLedger::SetContents` 0x1ab40):** `DoSetContents` loops over **every** active ledger (`activeLedgers` list; a port with a dialog on top has two) and calls `SetContents` on each. `SetContents`: if a child `contents` exists, hide the three bodies `messages`, `factories`, `inventory` (`bsgHideWindow`), `GetContentWindow(tab)` (the named child of `contents`), `bsgShowWindow` it and call `TDynamicWindow::UpdateContents` on it (a rebuild of the body, every time, also when the tab was already showing); then find the child named `"btn_" + tab`, and if it is a `TButton` call `SetOn(true)`; finally store the tab name in the static `currentKey`.
* **Fact (`TLedger::UpdateContents` 0x19cee):** rebuilds only when the given name equals `currentKey`.
* **Fact (state/feedback, `TButton`, same code in C2 and C3):**
  * Down (`OnMouseDown` 0x6e982): if the button's mouse-state nibble is 0 it becomes a mouse listener; state 1 (down); `ReleaseCapture` runs the hit test (and `OnMouseLeave` if outside); returns 1 (always swallows).
  * `OnMouseMove`: outside -> returns 0 (event continues behind); inside and idle -> state 2 (over) and becomes a listener.
  * Up (`OnMouseUp` 0x7052e): `Press()` only if the state is down and the pointer is still inside. `Press` (C3 0x93cd0, C2 equivalent): state 3, plays the button's click sound if its style has one, runs the command; for a radio, `SetOn(true)` **after** the command (which clears the other radios in the group).
  * Radio graphics (4-image `graphics`, order off/on/over-off/over-on): the Lua passes `up, down, over, down`, so the **selected tab shows the `down` art, also while hovered**; an unselected tab shows `up`, and `over` when hovered (`docs/re/window-system.md` 6.6).
* **Fact (sound):** `standardStyle` (`ui/styles.lua`, set as the default style) has `sound = "sfx/ui_click.ogg"` and no `rolloversound`. So a completed click plays `sfx/ui_click.ogg`; hovering plays nothing. (The only `rolloversound` users in the C2 Lua are `ui/mainmenu.lua` and `ui/lab_game.lua`.) The click sound is played on every release inside, also on an already-selected tab.
* **Fact (what is not there):** no tooltip or `Rollover` on the tabs (`ledger.lua` has none), no hotkeys (`RegisterHotkey` appears in neither `ledger.lua` nor `portview.lua`), no `EnableWindow` on the tab buttons anywhere in `ui/` or `sim/` (the only mentions of `btn_messages/btn_factories/btn_inventory` in the shipped Lua are `ledger.lua` and the help pages `help6.lua`/`help9.lua`), no disabled state (e.g. none when the player has no factory), no flashing for new messages. `gDisableBadgeButtons` (set by `DialogLedger`, `uihelp.lua`) hides only the Travel/Recipes/Pause group; **the three tabs exist in dialog ledgers too**.
* **Fact:** other callers of `SetLedgerContents`: `mainmenu.lua:105` (`"messages"`), `sim/travel.lua:105` and `sim/factory.lua:229` (`"factories"`), `sim/quest_functions.lua:317`. Initial tab: `TLedger::PostChildrenInit` (0x1b3c4) calls `SetContents(currentKey)`, or **`"factories"` when `currentKey` is empty**.

## 3. Same for all three?

**Fact: yes.** Same constructor (`Button{ type=kRadio, graphics={up,down,over,down}, command=SetLedgerContents(..) }`), same y=3, same height 36, same labels-on-top structure. The only differences are width (141 / 200 / 142), position and the factory tab having two labels around its icon, and the inventory label being right-aligned. There is no bare-sprite tab.

## 4. A mechanism in the original that can eat clicks on the tabs (important)

**Fact:** a building hover popup is a mouse listener and swallows presses.

* `TPort::OnMouseMove` (0x2782e): when `gActiveBuilding` is nil, walks the sprite list from the last element backwards; for the first sprite with a non-empty `contents` string (sprite info +0x48) where `TSprite::HitTest(sprite, point, identity, 0xc0, -1)` succeeds, it creates a `TPopupWindow` with a `TPortPopupTest` keep-visible test, puts it in the Port, `AddMouseListener(popup)`, and returns 1.
* `TSprite::HitTest` (0x62924): the point must be inside the sprite rectangle, **and** if the threshold (param 3) is >= 0 the sprite is rendered into a small texture and the pixel alpha must be **>= 0xc0 (192)** at that point. The Port passes 0xc0. (**Inference** on the render-into-texture reading: the tail compares the alpha byte of the sampled texel with the threshold.)
* The popup's `OnMouseMove` (0x268e0) asks its tester (`TPort::TPortPopupTest::KeepVisible` 0x26f9a -> `CheckSpriteRollover` 0x26ef2, the same alpha hit test); true -> returns 1 (consumed), false -> `KillPopup`, return 0. `OnMouseDown` (0x26848) returns 1 unconditionally. `OnMouseUp` (0x2689c) kills the popup and runs the sprite's Lua function (the building `command`, which enters the building).
* The window manager routes mouse events to listeners first (`window-system.md` 6.5; **inference** for C2, shared code). `TPort` itself has no `OnMouseDown`/`OnMouseUp`, so the *only* way to click a building in C2 is through this popup.

Consequence, **inference**: while the pointer travels from a building (popup open) down into the HUD, and stays over **opaque pixels (alpha >= 192) of that building's sprite that continue under the ledger art**, the popup keeps consuming moves and presses, so the tab does not hover, and a click on the tab is eaten: down is swallowed, up kills the popup and runs the building's command (entering the shop, etc.). Which part of the tab area is affected depends on each building's pixels. Measured geometry (sprite rect = image centred at the layout `x,y`, scale 1, taken from `LBuilding._ByName` via the engine and the PNG sizes in `ports/`; tab rectangles as in section 1):

| Port | Building | Tab | Overlap of sprite RECT with the tab (px) | of which alpha >= 192 (original's test) |
|---|---|---|---|---|
| buenosaires | `ba_shop` | Inventory | 2112 | 1954 |
| casablanca | `cas_market` | Factory | 504 | 0 |
| casablanca | `cas_market` | Inventory | 994 | 70 |
| moscow | `mos_zuev` | Factory | 18 | 3 |
| moscow | `mos_zuev` | Inventory | 142 | 142 |
| newyork | `ny_shop` | Factory | 255 | 10 |
| newyork | `ny_shop` | Messages | 279 | 0 |
| sanfrancisco | `sf_bchq` | Messages | 202 | 185 |
| sanfrancisco | `sf_bchq` | Factory | 214 | 212 |
| sydney | `syd_other1` | Inventory | 342 | 294 |
| sydney | `syd_shop` | Messages | 195 | 0 |

(No other building overlaps a tab rect. Caveat: sprite positions assume centre anchoring and no layer `scale`; verified only for the five San Francisco buildings against the engine's sprite dump.) Because the overlap is a few rows at the top of a tab in most ports, it feels random: the click works on the text row but not near the top edge, or only on the icon side of the tab.

**Unknown:** whether the shipped game ever mitigated this (nothing in the decomp suggests so), and whether players could hit it in practice.

## 5. Engine comparison

What was checked: `src/ui/Input.cpp` (`routeMouse`, `childAt`, `deepestAt`), `src/ui/widgets/Widgets.cpp` (`ButtonWindow`), `src/game/choc2/LedgerWindow.cpp`, `src/script/games/Choc2LedgerNatives.cpp`, `src/game/PortWindow.cpp`, `src/ui/widgets/InputWidgets.cpp` (`PopupWindow`, `RolloverWindow`), `src/game/sprite/Sprite.cpp`, and a headless run (`build/choco --headless`, scratch `--userdir`, new Story game to the San Francisco port; scripts in `/tmp/hud*.script`).

**Matches the original (fact, by dump and click tests):**
* Layout and z-order: dump shows the same rects (buttons `1,3,141,36` / `170,3,200,36` / `396,3,142,36`; labels `55,11,66,20`, `185,11,60,20`, `293,11,63,20`, `407,11,75,20`) and the same front-to-back order (labels, then buttons, then `contents`; ledger children badge, ledger Bitmap, quest).
* Clicks at the label text, the icon, the corners and the 4-column badge overlap all switch the tab (12 clicks tried; all correct). Hover shows the `over` layer from any point of the rectangle, including over the text and under the badge overlap. Rect rows 393..428 and columns 259..795 hover (edges 1 px exact).
* `ButtonWindow` follows `TButton`: capture on down, press on up inside, radio turns on after the command, text falls through (`TextWindow` has no mouse handler).
* `SetLedgerContents` reaches every ledger in the modal chain (`Ledger::all`), like `DoSetContents`.

**Discrepancies:**
1. **(Secondary; see section 0 for the main cause.) Building hit region is the sprite RECTANGLE; the original uses an alpha >= 192 pixel test.** `Sprite::hit` (`game/sprite/Sprite.cpp:89`) is a pure rectangle test, and `PortWindow::findBuilding`/`keepsPopup` (`game/PortWindow.cpp:259, 327`) use it for both opening the popup and keeping it alive. Result: transparent bounding-box corners of buildings that extend under the ledger swallow tab hover and clicks (table in section 4: Casablanca `cas_market` over Factory 504 px vs 0 opaque; New York `ny_shop` over Messages 279 vs 0; Sydney `syd_shop` over Messages 195 vs 0; and so on). Reproduced headless: hover the San Francisco HQ at (400,300), then click the Messages tab at (346,394): the HQ opens (`Modal "sf_tutor"`, `ui/chartalk.lua`), the tab stays on "inventory". At (346,396) the click works. `docs/re/port-window.md` 1.7 documents the C3 rectangle test (`HitTest(point, identity, 0, 0)`), which is where the engine's choice came from; the C2 Port passes 0xc0.
2. **Even with the original's alpha test the swallow exists where buildings are opaque under the HUD** (Buenos Aires `ba_shop` over Inventory ~1954 px, Sydney `syd_other1`, Moscow `mos_zuev`, San Francisco `sf_bchq` top 2 rows). The engine reproduces the mechanism (popup is a listener, `onMouseDown` swallows, `onMouseUp` -> `popupClicked` -> `activate`), so this part is faithful, not a bug in the port; it is the original's own behaviour. If the aim is a better feel than the original, the layer test could exclude points covered by the ledger; that would be a deliberate deviation.
3. **Initial tab when none was chosen:** the original opens on `"factories"` when `currentKey` is empty (`TLedger::PostChildrenInit`); the engine opens on `"messages"` (`LedgerWindow.cpp:70`). Also the original sets `currentKey` only inside `SetContents`, i.e. only when a ledger exists; the engine's `nativeSetLedgerContents` stores the tab in `Session` even with no ledger (`Choc2LedgerNatives.cpp:26`, written as an inference there). Effect on a new game is **unknown** because it depends on whether a ledger exists at `mainmenu.lua:105`.
4. **`ButtonWindow::onMouseDown` returns false** when the button is a hovering listener and the pointer is outside it (`Widgets.cpp:263-269`), so the click continues to the window under the pointer. The original `TButton::OnMouseDown` always returns 1. Only matters when a press arrives with no earlier move event (touch, script); mouse use has a move first. Documented in the code as a deliberate choice.
5. **Quest Rollover under the tabs when the quest bar is hidden (REPRODUCED, this is the main cause, section 0).** The bar rests at y=53 when `SetQuestHint("")` is in force (`wait 3000` is needed for the swipe); the Rollover then spans abs (279,405)-(776,444). Original mechanism is the same by decomp (inference, section 0), so this is not a proven divergence; it is an engine defect in effect.

## 6. Unknown

* Whether `RolloverCurrentQuest` returns content when no quest exists (decides if the hidden-bar popup of item 5 is ever created).
* The exact sprite anchoring and layout `scale` for buildings in the 16 ports (section 4 table assumes centre anchor, scale 1).
* Whether C2's `TWindowManager::HandleEvent` listener-first ordering is identical to C3's (only C3 was read; TText/TButton/TPopupWindow in C2 were read and match).
* The click sound's volume/bus and whether `TButton::Press` plays it for an already-selected radio in C2 (C3 reading says yes).
* Whether the engine's `Sprite` hit rect equals the original's `GetRect` transform for scaled or flipped building sprites (not needed for the tab overlap, all scale 1 here).
