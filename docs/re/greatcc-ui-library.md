# Great Chocolate Chase: the developer's UI library (`R*`, `K*`, `B*`, `VTintWindow`)

Source: `re/greatcc/decomp/<Class>.c` (Ghidra, Mac i386 binary), `re/greatcc/index.tsv`, `re/greatcc/strings.tsv`, `otool -tV` where noted, and the shipped Lua in
the Mac bundle's `assets/scripts/`. Addresses are Ghidra VM addresses. Tags: **F** fact (read from decompile/bytes),
**I** inference, **U** unknown. Pflib bases (`TWindow`, `TImage`, `TText`, `TButton`, `TSlider`, `TLayeredWindow`, `TTextEdit`) are described in
`docs/re/window-system.md`, `api-windows.md` and `fonts-and-text.md`; this file only covers what the game adds.

## 0. Summary tables

### 0.1 RMatch3 / RMatch3Piece: dead library code (F)
* Nothing constructs `RMatch3` (no constructor exists in the binary; it is abstract, with `OnMatch`/`OnAtRest`/`OnGridInitialization` etc. as unimplemented hooks), no subclass exists, `RMatch3Piece` is not in the Lua window-type list (section 0.3), and no Lua or XML mentions "match3".
* The Windows `.exe` contains no `Match3` string at all (not even RTTI), so the linker dropped it; the Mac build only keeps the class because of the symbols (`RMatch3Piece::RMatch3Piece` is referenced only by its own `RMatch3PieceDynamicCreate`).
* Skip it. (It would have been `RBounceWindow` pieces on a grid.)

### 0.2 Class status
"Lua" = the shipped (non-commented) scripts create it. "C++" = game C++ creates/uses it. "Dead" = registered or compiled but nothing reaches it.

| Class | Base | Reachable | Engine mapping (see section 14) |
|---|---|---|---|
| `RWindow` | `TImage` (Bitmap) | Lua: mainmenu, credits, endgame, console, entername, help, prelevel, upgrade, rewardswindow | thin subclass of `BitmapWindow` |
| `RBounceWindow` | `RWindow` | Lua: 13 dialogs | new: per-window animation + modal clock |
| `RShrinkText` | `TText` | Lua: ok, yesno, hud, console, chooseplayer, mainmenu, hiscore | thin subclass of `TextWindow` |
| `RDisableButton` | `TButton` | Lua: entername, help, prelevel, upgrade; C++: VHelpWindow, RTextEditDisable | thin subclass of `ButtonWindow` |
| `RTextEditDisable` | `TTextEdit` | Lua: entername | small subclass of `TextEditWindow` |
| `RScrollable` | `RWindow` | Lua: cheats_achievements | new but small |
| `RActiveTextLayer`, `RActiveText` | `RWindow` / plain object | Lua: game.lua (layer); C++: ShopWindow, VComboText | new, small |
| `RTimer` | `TText` | Lua: hud.lua; C++: VShopHud, ShopWindow | small `TextWindow` subclass |
| `KSplit9Image` (+`KSplit9` mixin) | `TWindow` | Lua: 15 dialogs | new widget (9-slice) |
| `KDial`, `KSweepDial` (+`KGauge`) | `TWindow` | Lua: hud.lua; C++: VShopHud | new widgets |
| `BLayeredWindow` | `TLayeredWindow(3)` | Lua: hud.lua; C++: VShopHud | trivial |
| `VTintWindow` | `TWindow` | Lua: ok, yesno, options, prelevel, ... | new, small |
| `RTransitionManager`, `ROverlay`, `RColorFade`, `RBounceOff`, `RTintOff`, `RCrossFade` | none / `TWindow` | Lua `DoTransition`, `kFade*`, `kBounce*`, `kTintOff` messages | new subsystem (not the choc3 `Transition{}` list) |
| `RScreenInvalidator` | `TAnimTask` | by ROverlay, VTintWindow, VPrelevelWindow, VSpriteWindow | trivial |
| `RAdapt` + `RAdaptAnim/Image/Sound/Text/Object` + tasks, `RSpriteLayer` | `TWindow` | Dead in the shipped game: only `scripts/adapt.lua`, which no script loads | skip unless wanted |
| `RState` | list of `RBlock` | C++ only (save/prefs); not UI | belongs to the save/state analysis |
| `RScorer` | `TText` | Dead: its only Lua use is inside a `--[[ ]]--` comment in hud.lua | skip |
| `RCursor`, `RButtonedSlider`, `RJustified`, `RAlert`, `RSplit9Text`, `RSplit9Expander`, `KBar`, `RParentButton`, `RAlphaSpider` | various | registered as Lua types (all but `RAlphaSpider`) but no shipped script/XML uses them: Dead | optional |
| `KGauge` | `TWindow` | abstract base of KDial/KBar/RCursor | empty |
| `BZeppelin`, `VButton`, `VTintEffect` | sprite classes | sprite side (`VDelivery`, `VSprite*`) | out of scope, see section 13 |

### 0.3 The Lua-creatable window types of this library (F, `_free/m.c` `Main`)
`Main` calls `AddWindowType` (name -> `ClassId`) for, in order: `TMainMenu TOptions TSlider TChoosePlayer TCredits RGame RScorer RTimer RDisableButton RSpriteLayer RAdapt RWindow RParentButton RScrollable RBounceWindow RButtonedSlider RJustified RAlert RTextEditDisable RActiveTextLayer RShrinkText KSplit9Image RSplit9Text RSplit9Expander KBar KSweepDial RCursor KDial ShopWindow VPrelevelWindow VUpgradeList VUpgradeTab VSpriteWindow VPlotWindow VWorldMap VRegister VShopHud VRewardsWindow VRewardNotify VChooseAchievements VTintWindow VObjectWindow VHelpWindow BLayeredWindow VQuestWindow VUpsellWindow HiscoreWindow`. It also calls `RTransitionManager::CreateTransitionManager()` and `VSoundManager::CreateSoundManager()` and sets cursor `cursor/chocolate_cursor_2` first. Not registered (so Lua cannot make them): `KSplit9` (it is a mixin, not a window), `KGauge`, `RMatch3*`, `RCrossFade`/overlays, `VButton`, `BZeppelin`, `RActiveText`.
Several class lists/keys here were also checked on the Windows exe: `Lua_DoTransition`, `sincpix`, `incrementsteps`, `numsegments`, `needle_regx`, `RJustified`, `RCursor`, `RAdapt` are all present, so the classes are the same on both platforms (F, `strings -a`). I did not diff the Windows Lua bytecode against the Mac text (U).

### 0.4 Common idioms
* `StartWindowAnimation(0x10, true, true, false)` (F, many `PostChildrenInit`s) = `TWindow::StartWindowAnimation(delay=16 ms, repeat, from-now, no catch-up)`: the window's `OnTaskAnimate` runs about every 16 ms. See `chocolate-factory.md` 2.1 for the task semantics (the clock is the owning modal's `TClock`, which pauses while another modal is on top).
* `SetFlags(0)` hides a window; `SetFlags(0x100)` shows it (`TWindow::SetFlags` keeps only `flags & 0xff00`, `0x100` = enabled/visible). (F, TWindow.c)
* "top modal clock": several classes cache `TWindowManager::GetTopModalWindow() + 0x50` (the `TClock` inside the `TModalWindow`) **at construction** and read time from it later (`RWindow`, `RTimer`; `VTintWindow` uses `FindParentModal()+0x50` each draw). (F)
* `RUtil::DoLuaString(s)` (0x7da00) = run the Lua text `table.insert( gCommandQueue, function() <s> end );` through the window script. So the string runs later, from the script's command queue, not synchronously. (F)

---

## 1. RWindow (0x6ec74...)
Base `TImage` constructed with `TImage(true)` (F). It is the "Bitmap with extras" used for backgrounds and as a container.

Object fields (F, ctor): `+0x48` last clock time (0), `+0x4c` last delta ms, `+0x50` accumulated ms, `+0x54` clock pointer = top modal `+0x50` at construction, `+0x58..0x64` bg colour (4 floats, all `-1`), `+0x68` has-bg flag, `+0x6c..0xeb` four `TTransformedLitVert` (rhw `0.5`).

Style keys read in `PostChildrenInit` after `TWindow::PostChildrenInit` (so `TImage`'s own keys `image`, `scale`, `mask`... and the `x/y/w/h` fit logic have already run) (F):

| key | type | default | effect |
|---|---|---|---|
| `imagealpha` | number | -1 | if `>= 0`: `TImage::SetAlpha(v)` (image opacity). help.lua uses 0.2, entername 0.1 |
| `disable` | bool | false | if true: `SetFlags(0)` (hidden) |
| `bgcolor` | colour | current (-1,-1,-1,-1) | if different from the stored colour: `SetBgColor(c)` (below) |
| `effect` | int | 1000 | if `!= 1000`: post a `TMessage(type=effect, name=luastring)` to the window manager (the transition manager consumes ids 1001..1013, section 3.4) |
| `luastring` | string | `""` | the message name used with `effect` |

* `SetBgColor` (0x6efa2) stores the colour, sets the flag, and builds a **window-local quad** `(0,0) (w,0) (0,h) (w,h)` at the window's size **at that moment** (not updated on resize), each vertex the colour as 8-bit. (F)
* `Draw` (0x6eddc): `TImage::Draw()` first, **then** if the bg flag is set a 4-vertex triangle-strip (type 5) quad. So the bg colour is drawn **over** the image, alpha-blended. credits.lua passes `bgcolor = Color(132,37,54,255)` with no image. (F)
* Reparent rule (end of `PostChildrenInit`) (F): if `FindParentModal()` is null (the window has no modal ancestor yet), then, if there is a top modal: remove the window from its current parent (virtual `+0x28`), add it to the top modal (`+0x24`, flag 1), start the 16 ms animation, call `topModal->+0x28(this)`, and re-add to the old parent (`+0x24`). If it does have a modal ancestor: only `StartWindowAnimation(0x10,true,true,false)`. **I**: the net effect is that a parentless RWindow ends up owned by the top modal and the old parent link is restored; I could not resolve the exact ownership after the sequence.
* `OnTaskAnimate` (0x6ee76): on the first call `last = clock.now`; then `delta = now - last; last = now; accumulated += delta`; returns true (keep running). Subclasses (`RBounceWindow`, `RScrollable`, `RTimer`-like classes, `RAlert`, `RActiveTextLayer`) call it first and use `+0x48/+0x4c/+0x50`.
* Lua uses: `RWindow{ name, image, x,y,w,h, scale, imagealpha, bgcolor, alpha=false (rewardswindow; a `TImage` key, not RWindow's) }` and as an overlay on button layer 3 (`RWindow{ image="ui/buttons/oval_disabled", scale=0.93 }` inside `SelectLayer(3)` of an `RDisableButton`, help.lua, prelevel.lua, upgrade.lua). mainmenu.lua wraps `Bitmap`/`Text`/`RShrinkText`/buttons in `RWindow{ name="mainmenu_window", image="ui/backgrounds/cs_background" }`.

## 2. RBounceWindow and RBounceOff / RBounceSearch

### 2.1 RBounceWindow (base `RWindow`; 0x6d370)
Fields (F): `+0xec,+0xf0` start (x,y), `+0xf4,+0xf8` target (x,y), `+0xfc` duration ms (default **500**), `+0x100` start time (0 = not yet taken), `+0x104` funcmin (default 0.0), `+0x108` funcmax (default **pi** 3.1415927), `+0x10c` progress t (0), `+0x110` function id (0), `+0x114` moving flag. Static `BOUNCEWINDOW_INVALID_POS` (0x287e60) is only the constructor's placeholder target; `PostChildrenInit` overwrites it. (F)

Style keys, in `PostChildrenInit` after `RWindow::PostChildrenInit` (F):

| key | type | default |
|---|---|---|
| `targetposx` | int | the window's own current x (after `TWindow` layout) |
| `targetposy` | int | the window's own current y |
| `bouncelen` | int (ms) | 500 |
| `funcmin` | number | 0 |
| `funcmax` | number | pi |
| `bouncefunc` | string | `"sincx"` (lower-cased; `"sincx"` -> function 0, `"sincpix"` -> 1, anything else -> 0) |

Order: start = current (x,y) (the style x/y, e.g. `y = -900` off-screen); read the keys; `Initialize(target)` = `SetTargetPos(target)` then `StartWindowAnimation(0x10,true,true,false)`. **Quirk (F):** `SetTargetPos` only restarts when the target differs from the stored one, and `PostChildrenInit` has just stored it, so `Initialize` does not call `OnSetTarget` (moving flag stays 0) and does not reset anything; the animation starts because `+0x100 = 0` and `+0x10c = 0` from the constructor. A window with no `targetpos*` keys therefore does not move.

`OnTaskAnimate` (0x6d75a), every 16 ms (F):
1. `RWindow::OnTaskAnimate()` (updates `+0x48` = modal-clock time).
2. If `t < 1.0`: if `+0x100 == 0` and `+0x48 != 0`, `+0x100 = +0x48` (start time = first tick). `t = (+0x48 - +0x100) / duration` (float).
3. If `t >= 1` or `t <= 0`: `SetWindowPos(t == 0 ? start : target)`; if `t >= 1` call `OnReachedTarget` (clears moving). Afterwards `t < 1` is false so nothing further happens.
4. Else `v = BounceFunctionValue()`, `x = targetX - (int)((targetX - startX) * v)`, `y = targetY - (int)((targetY - startY) * v)` (float math, truncation toward zero), invalidate the screen if the position changed, `SetWindowPos(x, y)`.

`BounceFunctionValue` (0x6d256) (F): `a = (funcmin - funcmax) * t` (default `-pi * t`). Function 0 (`sincx`): `v = sin(a)/a`. Function 1 (`sincpix`): `v = sin(a*pi)/(a*pi)`. So with the defaults and `sincx`, `v = sin(pi t)/(pi t)` falls monotonically from 1 to ~0 and the position fraction `p = 1 - v` is:

| t | 0 | .1 | .2 | .3 | .4 | .5 | .6 | .7 | .8 | .9 | 1 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| p | 0 | .016 | .065 | .142 | .243 | .363 | .495 | .632 | .766 | .891 | 1 |

(computed from the formula; **I** for the numbers, F for the formula.) It starts with zero velocity and arrives at full speed with **no overshoot**: it is an ease-in, not a literal bounce. `sincpix` with the defaults would sweep `a` over `[0, pi^2]` (three sinc lobes) and overshoot; nothing shipped uses it (all Lua says `"sincx"`).
The same function runs for the exit animation (section 2.2): start is the current position, target is the new off-screen point, so the window accelerates away.

Lua uses (F): `bouncefunc="sincx"` always; `bouncelen` 200 (ok, yesno, intro, hiscoreinfo), 300 (options, hiscoresubmit, help), 400 (prelevel, plotwindow); `targetposx/y` e.g. ok.lua `x=200, y=-900 -> (200,150)`; yesno computes the target from `gDialogTable.width/height`; options slides in from `x = kCenter + 800`. `rewardswindow.lua` has the whole RBounceWindow commented out in favour of plain `RWindow`.

### 2.2 RBounceSearch and RBounceOff (the exit "transition")
* `RBounceSearch` (a `TWindowSpider`) collects every `RBounceWindow` in a subtree into a list (it recurses by itself *and* the caller passes recursive=true, so nested windows are listed more than once; the duplicate `SetTargetPos` calls are no-ops). (F)
* `RBounceOff(id, luastring, mode)` (0x7239c) is an `ROverlay` (section 3.2). In its **constructor** (so at construction time, not when it becomes the current transition) it takes the top modal, collects its RBounceWindows, sets the overlay duration to the **maximum `bouncelen`** among them (starts at 0), and calls `SetTargetPos` on each. The new target depends on the side the window originally came from: `(X,Y)` = its start `(+0xec,+0xf0)`:
  * side (F, from the compares; `RBounceOff.c:62-81`), four tests in this order, a later one overriding an earlier one, with `X`/`Y` signed wherever no `|.|` is written: (1) **3 left** if `Y < |X|` and `X < 0`, else **0 top** (the default); (2) **2 bottom** if `|X| < Y`; (3) **0 top** if `X < |Y|` and `Y < 0`; (4) **1 right** if `|Y| < X`. So with both coordinates negative the result is top, whichever is larger. **I** for the side names; the integer values and tests are F.
  * mode 0 (kBounceBack, id 1007): new target = `(X, Y)` (back to where it came from).
  * mode 1 (kBounceThrough, 1008): left/right sides -> `(-X, Y)`, top/bottom -> `(X, -Y)` (leave through the opposite edge).
  * mode 2 (kBounceClock, 1009): left/right -> `(targetX, X)`; top/bottom -> `(-Y, targetY)`.
  * mode 3 (kBounceCounter, 1010): left/right -> `(targetX, -X)`; top/bottom -> `(Y, targetY)`.
  (the window is moved on the perpendicular axis by the magnitude of its entry offset; clock/counter pick the sense.) Only modes 0 and 1 are used by the shipped Lua.
* `RBounceOff::Draw` draws nothing but `ROverlay::Draw` (time accounting) and sets the done flag when `elapsed >= duration`. (F)

## 3. Transitions: RTransitionManager, ROverlay and the effect classes

### 3.1 Lua surface and constants (F)
`scripts/transitions.lua` defines `kFadeToBlack=1001 kFadeFromBlack=1002 kFadeToWhite=1003 kFadeFromWhite=1004 kCrossFadeIn=1005 kCrossFadeOut=1006 kBounceBack=1007 kBounceThrough=1008 kBounceClock=1009 kBounceCounter=1010 kTintOff=1011`.
Two entry points:
* **`DoTransition(type)`** (blocking). The manager's constructor (0x73edc) registers a native `Lua_DoTransition` (bound to the manager instance) and runs this Lua text: `function DoTransition( type ) local ret = Lua_DoTransition( type ); while( ret ~= true and ret ~= false )do ret = Yield(); end return ret; end`. `Lua_DoTransition(type)` (0x73a9c): if the top of stack is a number, it makes the transition via `GetTransition(id, "")`; if non-null it stores the calling `lua_State` in `overlay+0x50`, queues it, then `lua_yield(L, 0)`. Otherwise (not a number, or id not 1001..1011) it pushes `false` and returns 1 immediately. When the transition finishes the manager resumes that exact coroutine with the single value `true` (section 3.3). The Lua wrapper loops on `Yield()` until it sees a boolean, so a spurious wake-up (window message, script task tick) just yields again (**I**: any message delivered to the script while it waits in this loop is therefore swallowed). The optional second argument shown in comments (`DoTransition(kFadeToBlack, "SwapToModal(...)")`) is not read by `Lua_DoTransition`. (F)
* **Message form**: `PostMessage( CreateNamedMessage( kFadeFromBlack, "luaText" ) )`. `RTransitionManager::OnMessage` (0x73bc2) handles message types `1001 .. 1013` (`type - 0x3e9 < 0xd`), calls `DoTransition(type, message.name)` and returns true (consumed). The name string becomes the transition's `luastring`, run through `DoLuaString` (a command-queue entry) when the transition finishes. Ids 1012/1013 are consumed but create nothing. Shipped uses: `kFadeFromBlack` at the end of mainmenu/hiscore/credits/endgame/loading scripts (loading.lua passes `"ReadyForPrelevel();"` and `"CloseLoadingScreen(true);"`), `kTintOff` in nearly every dialog's close button, and commented-out `kCrossFadeIn/Out` in cheats*.lua.

The manager is a message listener (`AdoptMessageListener`) and a global (`_gTransitionManager`); a `RTransitionManagerMonitor` (`TAnimTask`, 16 ms, from-now) is adopted into the platform task list and calls `MonitorTransition` each tick. (F)

### 3.2 ROverlay (base `TWindow`; the common transition object; 0x73150)
Fields (F): `+0x30` last time, `+0x34` delta, `+0x38` accumulated ms, `+0x3c` its own `TClock` (started in the ctor; not modal-paused), `+0x40` duration (default **500**), `+0x44` done flag, `+0x48` transition id, `+0x4c` luastring, `+0x50` waiting `lua_State*` (0 if none), `+0x54` an `RScreenInvalidator` (adopted as a platform task so every ~16 ms the screen is invalidated).
Constructor: position (0,0), size **800x600**, adopts the invalidator, **sets byte `TWindowManager+0x25 = 1`** (cleared in the destructor; **U** what that flag gates, **I** probably "a transition overlay is up").
`Draw` (0x730a2): `if last == 0: last = clock.now; delta = now - last; last = now; elapsed += delta`. So time advances **per draw**, and the first draw contributes 0.
`GetProgress` = `min(1, elapsed / duration)` (0x730f0).
The overlay only becomes active (draws, timer runs) when it is the manager's current transition, via `TWindowManager::AdoptOverlayWindow` (it joins the overlay list drawn after the whole modal stack, see `window-system.md` 6.3).

### 3.3 Queue, completion and event blocking (F)
* `DoTransition(ROverlay*)` (0x73a4e): null -> return 0. If there is no current transition (`+4 == 0`): it becomes current and is adopted as an overlay window; return 1. Otherwise it is appended to the pending list (`+0x10`) and return 0. **Only one overlay is current; the rest wait their turn.**
* `MonitorTransition` (0x73c2e), every 16 ms: (1) destroy and drop every overlay in the "retired" list (`+0x18`, via `OrphanOverlayWindow` and the virtual delete); (2) if the current overlay's done flag is set: if its `luastring` is non-empty run `RUtil::DoLuaString(luastring)`; if `+0x50` (waiting Lua state) is set, `lua_pushboolean(true); lua_resume(L, 1)` and clear it; erase the id from an int set (`+0x20`, never inserted into by anything I found); move the overlay to the retired list; `current = null`; if the pending list is non-empty, pop the front and make it current (`DoTransition`). So an overlay stays on screen for one extra tick after it finishes (the retired list is flushed at the **start** of the next tick).
* `HandleEvent(TEvent*)` (0x735de): returns 1 (event consumed) when the event type is in the mask `0x3de0` (types 5, 6, 7, 8, 10, 11, 12, 13) **and** (a transition is current or the pending list is non-empty); otherwise 0. With the event numbering in `window-system.md` 6.2 this blocks mouse and key events during transitions; types 9 (mouse housekeeping) and 14 (unicode) are not blocked (**I**: that table is itself partly inferred; **U**: how `HandleEvent` is hooked into the manager's event path, I did not find the caller).
* **Construction has side effects (F, important):** `RBounceOff` retargets its windows, `RTintOff` starts the tint fade-out, and `RCrossFade` captures and hides the window **inside the constructor**, i.e. at `GetTransition` time, whether or not the overlay is current yet. The queue only delays the *overlay timer*, the Lua string and the Lua resume. Example (ok.lua): `PostMessage(kTintOff); DoTransition(kBounceBack); CloseWindow();`. The bounce overlay is built immediately inside `DoTransition`; the tint message is dispatched on the next message pump, builds `RTintOff` then (the dialog is still the top modal, so it finds the `VTintWindow` and starts fading it out right away) and queues behind the bounce; the script resumes when the bounce overlay finishes (max `bouncelen`, e.g. 200 ms) and then calls `CloseWindow()`.

### 3.4 GetTransition(id, luastring) (0x7361e) (F)
| id | constant | object | notes |
|---|---|---|---|
| 1001 | kFadeToBlack | `RColorFade(fade-in=true, rgb 0,0,0, alpha 1)` | |
| 1002 | kFadeFromBlack | `RColorFade(false, 0,0,0, 1)` | |
| 1003 | kFadeToWhite | `RColorFade(true, 1,1,1, 1)` | no shipped use |
| 1004 | kFadeFromWhite | `RColorFade(false, 1,1,1, 1)` | no shipped use |
| 1005 | kCrossFadeIn | `RCrossFade(true)` | commented out in Lua |
| 1006 | kCrossFadeOut | `RCrossFade(false)` | commented out in Lua |
| 1007..1010 | kBounceBack/Through/Clock/Counter | `RBounceOff(mode 0..3)` | |
| 1011 | kTintOff | `RTintOff` | |
Any other id returns null.

### 3.5 RColorFade (0x726cc, 0x7278c)
`ROverlay` + fields `+0x58` direction flag, `+0x5c..0x64` r,g,b, `+0x68` max alpha; duration set to **500**. `Draw`: `ROverlay::Draw`, `p = GetProgress()`, `alpha = flag ? p * maxAlpha : maxAlpha - p * maxAlpha`, then one full-window quad `(0,0)-(w,h)` in colour `(r,g,b,alpha)` via a 2D vertex strip; if `elapsed >= duration` set the done flag. Linear, no easing. (F) After a fade-to-black finishes the overlay is retired about one tick later (section 3.3), so the black is removed unless a new fade has been queued; the shipped scripts always follow with a `SwapToModal`/new screen that posts `kFadeFromBlack` at load (**I**).

### 3.6 RCrossFade and RCrossFadeRefresh (0x72e8c, 0x72bf6, 0x72dba)
`ROverlay` + `+0x58` is-fade-in, `+0x5c` the top modal (captured at construction), `+0x60` snapshot texture (`shared_ptr<TTexture>`), `+0x68` refresh task. Duration **250 ms** (0xfa). Constructor: `RUtil::WindowToTexture(topModal, true, white)` renders the modal to a texture (it temporarily moves the window to (0,0), draws it to a render target, restores), adopts an `RCrossFadeRefresh` (a texture-refresh listener: on a GL context loss it re-shows the window, re-renders the snapshot and hides it again), then **hides the real window (`SetFlags(0)`)**. `Draw`: `alpha = in ? p : 1 - p`; draw the snapshot centred on the window's centre (`x = left + w/2`, `y = top + h/2`) with that alpha; done flag at `elapsed >= duration`; when done **and fade-in**, `SetFlags(0x100)` shows the real window. A fade-out never re-shows it (the Lua string, e.g. `"CloseWindow();"`, is expected to close it). (F)

### 3.7 RTintOff and RTintSearch (0x8298c)
`RTintSearch` collects every `VTintWindow` in a subtree (same double recursion as `RBounceSearch`). `RTintOff(id, luastring)` constructor: for each tint window found in the top modal it takes `time` (the window's `+0x44` float; negative/zero -> 0), sets the overlay duration to the maximum, and calls `VTintWindow::TintOff()` (section 7). `Draw` only accounts time and sets the done flag. Duration is 0 (so it completes on its first draw) if there is no `VTintWindow`. (F)

### 3.8 RAlphaSpider (0x72b1a) (Dead)
A `TWindowSpider`: sets alpha on every `TImage` and `TText` descendant (recursively, `ForEachChild(..., false)` per node) to its constructor float. No caller in the binary. (F)

### 3.9 RScreenInvalidator (0x733b0)
`TAnimTask` with its own started `TClock`, `SetDelay(0x10, true, true, false)`; `Animate()` = `TWindowManager::InvalidateScreen()`, return 1 (runs forever until orphaned). Owners adopt it (platform task list for `ROverlay`; the parent modal's task list for `VTintWindow`) so that a static screen with a time-based effect keeps redrawing (the manager does not redraw a clean screen). (F)

## 4. RShrinkText (base `TText`; 0x826a8)
Fields: `+0x48` current line height, `+0x4c` maximum line height (both 0 initially). (F)
* `Init` (0x82718): `TText::Init`; then reads the `font` table (`{face, size, colour}`) again: if the table has more than one entry, `size = to_int(font[2])` is stored in both `+0x48` and `+0x4c`. It also sets a byte in the `TTextGraphic` (`+0x6c = 1`; **U** what that flag means, **I** "wrap/clip").
* `SetText(str)` (0x82818): if `max != 0`: `current = max` and `TTextGraphic::SetLineHeight(max)` (so every `SetLabel` starts again from the full size). Then `TText::SetText`. Then measure `TTextGraphic::GetTextBounds`. While **window width <= text width OR window height <= text height** (note `<=`, strict fit needed): `current -= 1`, `SetLineHeight(current)`, re-measure; if `current` reaches 0, log `"RShrinkText: Line Height Zero"` and stop. (F)
* The label given in the style is not run through this loop: `TText::Init` builds the text graphic from it directly (the loop is `SetText`'s only), so an initial label that is too long is laid out at the full font size. (F, `TText::Init` / `RShrinkText::Init` decompile.)
* So: the text always renders at the largest integer line height `<= font size` whose bounds are strictly smaller than the window in both axes. It ignores the `flags` for the loop (alignment still applies at draw). Lua keys: standard `Text` keys (`x y w h font name flags label`); sizes like `w = kMax-16`.
* Lua uses: ok/yesno title (`h=40`) and body, hud (`goal_score`, `mastery_score`), mainmenu user name, console, chooseplayer, hiscore. Note label values like `"#"..GetCurrentUserName()` (the `#` prefix = literal, not a string-table key).

## 5. RDisableButton, RDisableButtonAction, RParentButton, RTextEditDisable

### 5.1 RDisableButton (base `TButton`; 0x6da44)
Fields: `+0x6c` disabled flag, `+0x70` Lua function (the `command`). Button layer index = `TLayeredWindow::SetCurrentLayer`; **layer 3 is the disabled look** (the fourth `graphics` entry plus anything created under `SelectLayer(3)`). The button's state byte `+0x3c`: low nibble = mouse state (0 normal, 1 down, 2 over, 3 commit, as in `ButtonWindow::MouseState`), bits 4-6 = kind; `+0x3d` bit 0 = toggled-on. (F; the nibble use is consistent with `docs/re` TButton notes, kind bits I.)
`PostChildrenInit` (0x6dda8): `TButton::PostChildrenInit`; read `command` as a function (`GetFunction`); if present install a `RDisableButtonAction` as the button's command; read `disabled` (bool, default false) and if true `ToggleState(true)`.
* `RDisableButtonAction::DoAction` (0x6dd50): if the button is an RDisableButton, is **not** disabled and has a function: push the function and call it through the script object (vtable slot `+0x14`). So a disabled button runs neither the Lua command nor (since the action replaced it) anything else; with no `command` key the stock `TButton` behaviour (message/close/default) is unchanged. (F)
* `ToggleState(disabled)` (0x6db44): sets the flag; if disabled `SetCurrentLayer(3)`; if enabled re-applies the normal layer by calling the virtual `StateUpdate(kindBits, 0)`.
* `OnTaskAnimate`: if disabled and the current layer is not 3, `ToggleState(true)` (a re-assert; only runs if a window animation was started, **U** whether one is).
* `OnMouseDown`: if enabled `TButton::OnMouseDown`; then `SetCurrentLayer(enabled ? 1 : 3)`; **returns true either way** (a disabled button still swallows clicks). `OnMouseUp`: if enabled `TButton::OnMouseUp` (which fires the command); then if mouse state is normal or over: `SetCurrentLayer(enabled ? 2 : 3)`. `OnMouseMove`: `TButton::OnMouseMove`, then the same state-0/2 layer rule (`enabled ? 2 : 3`). `OnMouseLeave`: `TButton::OnMouseLeave`; state 0 -> layer `enabled ? 0 : 3`; state 2 -> `disabled ? 3 : (toggled-on bit)`. `StateUpdate`: disabled -> layer 3, else `TButton::StateUpdate`. (F)
  Net effect for the 3-state graphics used here (`up, down, over[, disabled]`): enabled hover/after-click shows layer 2, pressed shows layer 1, idle layer 0.
* Lua keys: those of `Button` (`x y w h name type=kPush|kRadio default graphics scale ...`) plus `command` and `disabled`. prelevel.lua: `type=kRadio`, `graphics={up,down,down,"anim/transparent"}`, children under `SelectLayer(3)` and `SelectLayer(-1)`; help.lua: `btn_back`/`btn_next` with an `RWindow{ image=..., imagealpha=0.2 }` on layer 3 plus a `Text`; upgrade.lua: `btn_buy_upgrade`.
* C++ callers of `ToggleState`: `RTextEditDisable` (below) and `VHelpWindow` (next button disabled on the last page, back button disabled on page 0). Nothing else I found disables the prelevel/upgrade buttons (**U**: they may start enabled and be driven by VPrelevelWindow through `SetFlags`).

### 5.2 RParentButton (Dead; 0x6eb94)
`TButton` whose `OnMouseMove` first finds `ChildWindowFromPoint`, climbs parents until one `IsKindOf(TButton)`, and only calls `TButton::OnMouseMove` if that button is itself; otherwise it calls virtual `+0x4c` (the mouse-leave/reset hook, **I**) and returns false. Meaning: hover only counts when this is the innermost button under the cursor. `RButtonedSlider` uses the same test (section 11). (F for the logic, `otool` shows the `TButton::ClassId` argument.)

### 5.3 RTextEditDisable (base `TTextEdit`; 0x70948)
Field `+0x7c` = name of the button to drive (default `""`). Keys (F): `disable` (string, default `""`) = the button name; plus the usual `TextEdit` keys (`name`, `label`, `font`, `length` (max length), `ignore` (illegal characters)). `entername.lua`: `length=20`, `ignore=kIllegalNameChars`, `disable="ok"` with `ok` being an `RDisableButton`.
* `PostChildrenInit`: `TWindow::PostChildrenInit`, store the name, then `HandleKeyHit(type=1, ch=8, handled=true)` to set the initial state.
* `KeyHit(type, ch)` (override of the pflib pre-validation hook, vtable `+0x7c`): `handled = TTextEdit::KeyHit(type, ch)` then `HandleKeyHit(type, ch, handled)`. `TTextEdit::KeyHit` is a **pre**-edit check (returns 1 for types 1 and 4, for type 0 only if under the length limit); the text is changed after it returns, so `HandleKeyHit` sees the **text before the key is applied** (**I**, from the pflib flow in `TTextEdit::KeyHitUTF8`).
* `HandleKeyHit(type, ch, handled)` (0x709e4): find the named child of the top modal; if it is an `RDisableButton`: `t = Trim(GetText())`. The button is **disabled** unless all of: `(t != "" or handled)` and `(length(t) > 1 or type != 1)` and `(t != "" or ch != ' ')`. Written out: disabled when the (pre-key) trimmed text is empty and the key was not accepted; or when the key is type 1 (**I**: backspace) and the text has length <= 1 (the backspace is about to empty it); or when the text is empty and the key is a space. Enabled otherwise. Calls `RDisableButton::ToggleState(disabled)`.
* The initial call (type 1, ch 8, handled true) enables the button iff the initial trimmed label has more than 1 character.

## 6. RScrollable, RGiveToInterior, RActiveTextLayer/RActiveText, RTimer, RScorer

### 6.1 RScrollable (base `RWindow`; 0x6fb1a) and RGiveToInterior (0x6fa02)
Fields (F): `+0xec` interior `RWindow*` (made in the ctor), `+0xf0` `TSlider*` (null), `+0xf4` position float 0..1, `+0xf8` tick (pixels), `+0xfc` vertical flag (default true), `+0x100` vector of "except" names.
Keys (`PostChildrenInit` 0x70054): `vscroll` (bool, default **true**; false -> horizontal), `except` (Lua table of window names to leave in place, 1-based), `tick` (number, default 0). Order: `RWindow::PostChildrenInit`; read keys; run `RGiveToInterior` over the **direct** children; `FitToChildren(interior)`; add the interior as a child of the scroller (virtual `+0x24`, flag 1).
`RGiveToInterior::Process(child)`: if the child's name equals an `except` entry, leave it. Otherwise: if the scroller has no slider yet and the child is a `TSlider`, remember it as **the** slider (`+0xf0`; it stays a child of the scroller) and stop; else remove the child from the scroller and add it to the interior window. So all content moves into a movable inner window sized to its children, and the first slider drives it. (F)
`SetInteriorPosition(f)` (0x6fc92): clamp `f` to [0,1], store, `TSlider::SetValue(f, true)` if a slider exists; vertical: `y = -(int)((interiorH - viewH) * f)`, clamped to `>= viewH - interiorH`; if `interiorH < viewH`: position and slider are reset to 0 and `y = 0`; `x` keeps the interior's current x. Horizontal is the same on x/width. Then `SetWindowPos(interior, x, y)`. (F)
`OnMessage`: message type **9** with a slider -> `SetInteriorPosition(slider.GetValue())`, handled (**I**: type 9 is "slider changed"; **U** confirm against the message enum). `ScrollForward`/`ScrollBack`: `SetInteriorPosition(pos +/- tick / (interiorLen - viewLen))` (no guard for equal lengths). Who calls them (wheel? arrows?): not found in the binary (**U**).
Lua (cheats_achievements.lua only): `RScrollable{ name="scroller", x=5,y=5,w=kMax-10,h=kMax-75, VSpriteWindow{...w=405}, SetStyle(App_SliderStyle), TSlider{ x=kMax-30, y=2, h=kMax-2, w=25, name="achievement_slider" } }`.

### 6.2 RActiveTextLayer (base `RWindow`) and RActiveText (plain object)
`RActiveText` is not a window. Fields (F): `+4` `TTextGraphic*`, `+8,+0xc` position (x,y), `+0x10` float 1.0, `+0x14` float 0.0, `+0x18` float 1.0 (alpha, written by `VComboText`), `+0x1c,+0x20` width/height, `+0x24` flags, `+0x28` font name, `+0x2c` size, `+0x30..+0x3c` colour. Constructor `(text, pos, w, h, flags, fontName, size, colour)`. `Draw()` draws the text graphic in the rect `(x - w/2, y - h/2, x + w/2, y + h/2)` via `TTextGraphic::Draw(rect, +0x10, +0x14, +0x18)` (**I**: scale, rotation?, alpha; the meaning of `+0x10/+0x14` is not resolved). `Update(dt)` (virtual, slot `+0xc`) base returns true; a subclass returns false to expire. `SetText` forwards.
`RActiveTextLayer` (0x69896): `RWindow` + `std::list<RActiveText*>` at `+0xec`. `PostChildrenInit`: `RWindow::PostChildrenInit` and start the 16 ms animation. `AdoptText(t)` appends. `OnTaskAnimate`: `RWindow::OnTaskAnimate`, then `Update(+0x4c delta ms)` on each; those returning false are deleted and erased. `Draw`: `RWindow::Draw` then each text's `Draw()`. `ClearText` deletes all. (F)
Lua: `RActiveTextLayer{ name="text_window", x=0,y=0,w=kMax,h=kMax }` in game.lua; C++ finds it by name (`ShopWindow`, cache at `+0x1b0`) and adopts floating texts. The only subclass I found is `VComboText` (text `"%s x%i\n+$%i"`, font name `edwardian_medium.mvec` 18 (not a file: drawn in the executable's embedded font, see the shop analysis), 150x36, rises 75 px over 500 ms while fading in, holds, fades out after 1500 ms, dies at 2001 ms; owned by the shop analysis).

### 6.3 RTimer (base `TText`; 0x70f9e)
Fields (F): `+0x48` last time, `+0x4c` delta, `+0x50` start/duration ms (int), `+0x54` elapsed ms, `+0x58` is-incrementing (default **true**), `+0x59` running (default true), `+0x5a` hide (default false), `+0x5c` clock = top modal `+0x50` at construction.
Keys (`Init`, after `TText::Init`): `isincrementing` (bool, default true), `starttime` (int ms, default 0), `hide` (bool, default false; `PostChildrenInit` then `SetFlags(0)`). `PostChildrenInit` also starts the 16 ms animation. `hud.lua`: `RTimer{ x=152,y=77,w=62,h=70,name="game_timer",label="",font={edwardian_medium,22,BlackColor},flags=kVAlignTop+kHAlignCenter,hide=false }`.
`OnTaskAnimate` (0x70f30): if running: first call sets last time; `delta = now - last; last = now; elapsed += delta`; clamp duration to >= 0; `SetText()`. `SetText()` (0x70db2): value = incrementing ? elapsed : `max(0, duration - elapsed)`; text = `"%d:%.2d"` of `value/60000` and `value/1000 - 60*(value/60000)` (minutes:seconds, truncating). `Reset()`: duration=0, elapsed=0, last=0, running=true. `PercentElapsed()` (0x17c0f4): 0 if duration <= 0, else `min(100, elapsed/duration*100)`.
How the game drives it (F, VShopHud): on level start `Reset()`, `isincrementing = IsEndlessMode()`, and unless endless `duration = level.minutes * 60000 + level.seconds * 1000` (level fields at `+0x10`/`+0x14`), then `SetText()`. Each frame `running` is cleared when a tutorial is in effect or the sprite manager is paused (its byte `+0xee`), and when it becomes true again `last = now` so paused time is not counted. `ShopWindow` reads `PercentElapsed()` for customer arrival and tutorial lockdown.

### 6.4 RScorer (Dead: Lua use is commented out) and RScore
`TText` with a smoothly rolling number: `+0x48` base int, `+0x4c` list of `shared_ptr<RScore>` (each `+8` int value, `+0xc` float multiplier), `+0x54` shown value, `+0x58` `incrementsteps` (int, default 75), `+0x5c` per-tick step (0; never written by any code I found), `+0x60` hide. `GetTotalScore = base + sum(int(value*mult))`. `OnTaskAnimate` moves the shown value toward the total by `+0x5c`, clamped to the total, via `LocalizeNumber`. Nothing creates scores or sets the step, so it can only ever show its initial number. Skip. (F)

## 7. VTintWindow (base `TWindow`; 0x82228...)
Sprite-independent window: the dim layer behind dialogs. Fields (F): `+0x30..0x3c` colour (r,g,b,a floats), `+0x40` fade flag, `+0x41` direction flag (initially **true** = fading in), `+0x44` time ms (float, default **500.0**), `+0x48` start time (0 = not started), `+0x4c` an `RScreenInvalidator` (adopted into the **parent modal's** task list in `PostChildrenInit`, orphaned in the destructor).
Keys (`PostChildrenInit`): `color` (colour, default (0,0,0,1)), `fade` (bool, default false), `time` (number ms, default 500). Lua: `VTintWindow{ x=0,y=0,w=800,h=600, color=Color(0,0,0,200), time=200, fade=true }` (ok 200 ms; yesno 200; options 300 with `Color(0,0,0,200)`; prelevel `Color(0,0,0,160)`, default time). The Lua `Color(0,0,0,200)` uses 0..255 components (**I**: the pflib `Color` helper normalises them to 0..1 floats; I did not re-check that here).
`Draw` (0x8253c): not fading: `RUtil::FillRect((0,0,w,h), colour)` (alpha-blended). Fading: needs a parent modal (else draws nothing); on the first draw `start = modalClock.now`; `t = min(1, (modalClock.now - start) / time)`; `alpha = colour.a * (fadingIn ? t : 1 - t)`; fill the window rect with that.
`TintOff()` (0x8250c): **only if `fade`**: toggle the direction flag (XOR 1) and reset `start = 0` (so the next draw restarts the ramp). Calling it twice fades back in. `RTintOff` calls it on every tint window under the top modal (section 3.7). (F)

## 8. KSplit9 (mixin), KSplit9Image, RSplit9Text, RSplit9Expander

### 8.1 KSplit9 (not a window; 0x710bc, 0x71a92, 0x71524, 0x711ce, 0x71060)
Embedded at `+0x30` inside `KSplit9Image` (`+0x48` inside `RSplit9Text`). Fields (F): `+4` parent `TSprite` (a container), `+0xc` texture, `+0x14` `x1`, `+0x18` `x2`, `+0x1c` `y1`, `+0x20` `y2`, `+0x24` padding (all ints), `+0x28` owner window. Virtual slot 0 = `InitializeSize(style)`, supplied by the owner.
`Initialize(style)`: reads `x1 x2 y1 y2 padding` (numbers, default 0, truncated to int), calls `InitializeSize(style)` (the owner sizes itself), reads `image` (string, default `""`) as a texture, then `AddSprites()`.
`AddSprites()` builds **nine child sprites once, at init**, from the texture size `(TW,TH)` and the owner size `(W,H)` (read at that moment; later resizes are **not** reflected): with `L=x1, R=x2, T=y1, B=y2`:

| piece | source rect in the texture (px) | destination rect in the window |
|---|---|---|
| top-left | `(0,0)-(L,T)` | `(0,0)-(L,T)` |
| top | `(L,0)-(TW-R,T)` | `(L,0)-(W-R,T)` |
| top-right | `(TW-R,0)-(TW,T)` | `(W-R,0)-(W,T)` |
| left | `(0,T)-(L,TH-B)` | `(0,T)-(L,H-B)` |
| centre | `(L,T)-(TW-R,TH-B)` | `(L,T)-(W-R,H-B)` |
| right | `(TW-R,T)-(TW,TH-B)` | `(W-R,T)-(W,H-B)` |
| bottom-left | `(0,TH-B)-(L,TH)` | `(0,H-B)-(L,H)` |
| bottom | `(L,TH-B)-(TW-R,TH)` | `(L,H-B)-(W-R,H)` |
| bottom-right | `(TW-R,TH-B)-(TW,TH)` | `(W-R,H-B)-(W,H)` |

Each piece is a sprite sharing the texture with the source-rect flag (`|4`) set; its transform is `translate(srcW/2, srcH/2)` composed with `scale(dstW/srcW, dstH/srcH)` and `translate(dstX, dstY)` (**I**: the half-size translate is the sprite-centre convention; the net effect is "stretch the source rect to exactly fill the destination rect"). So corners keep their pixel size only when `dst == src` (the usual case), edges and centre stretch non-uniformly with plain texture filtering. Nothing is tiled. If the dimensions make `W-R < L` the middle pieces get negative size (**U**: not handled; the Lua never does it).
`GetInnerRect()` (0x71060) = `(x1 - pad, y1 - pad, (W - x2) + pad, (H - y2) + pad)`, used by `RSplit9Text` to place its text. (F)

### 8.2 KSplit9Image (base `TWindow` + KSplit9 mixin; 0x71e60, 0x71e8e, 0x71f24)
`PostChildrenInit`: `TWindow::PostChildrenInit`, then `KSplit9::Initialize`. `InitializeSize`: reads `w` and `h` with `GetNumber` (default 0) and calls `SetWindowSize(w, h)`, i.e. the **raw numbers**: `kMax`/`kCenter` are not understood for these two keys (no shipped Lua uses them on `KSplit9Image`; `x`/`y` still use `kCenter`). `Draw`: draw the prebuilt sprite tree at the origin with the window's viewport (identity transform, full alpha, `0xffffffff` colour mod). No fader alpha (**I**: not a `TImage`, so transitions' alpha does not apply).
Lua keys in use: `image` (`ui/backgrounds/split9_dialog`: `x1=y1=x2=y2=60`; `split9_dialog_small`: 40, help.lua 30/32/37/40 for `x1/y1/x2/y2`; `gfx/textfield`: 10, 5, or 1), `x1 x2 y1 y2`, `w h x y name`, and children. `padding` is never given. The texture's real dimensions are not needed by the implementer beyond reading the PNG.

### 8.3 RSplit9Text (base `TText` + KSplit9 at `+0x48`; Dead)
`PostChildrenInit` as above. `InitializeSize`: measure the text bounds, set the text rect to `(textW + 2*pad, textH + 2*pad)` and the window size to `(textW + x1 + x2, textH + y1 + y2)`. `Draw`: nine-slice sprites, then the text in `GetInnerRect()`. (F)

### 8.4 RSplit9Expander (base `KSplit9Image`; Dead)
`InitializeSize`: `KSplit9Image::InitializeSize` (explicit `w`,`h`), then compute the maximum right edge and bottom edge of all children (in this window's client coordinates); if both are `> 0`, window size = `(maxRight + x2 - pad, maxBottom + y2 - pad)`. (F)

## 9. KGauge, KDial, KSweepDial, KBar, RCursor, BLayeredWindow

### 9.1 KGauge
Abstract `TWindow` base (vtable only). `SetPos(float)` (virtual slot `+0x74`) and `GetPos()` (`+0x78`) are the interface the game C++ calls.

### 9.2 KDial (base `KGauge`; 0x679b2, 0x676a2, 0x67750)
Fields (F): `+0x30` current angle (rad), `+0x34,+0x38` pivot `(center_x, center_y)`, `+0x3c,+0x40` needle registration offset, `+0x44` minrot, `+0x48` maxrot, `+0x4c` needle texture, `+0x54` assert flag, `+0x58` alpha (1.0), `+0x5c` overlay texture. Constants `KDIAL_DEFAULT_MINROT = 0`, `KDIAL_DEFAULT_MAXROT = 2*pi` (6.2831855).
Keys (`Init`, after `TWindow::Init`): `center_x`, `center_y` (int, default **-1337**), `needle_regx`, `needle_regy` (int, default -1337), `minrot`, `maxrot` (number rad, defaults 0 and 2*pi), `needle` (texture, default none), `overlay` (texture, default none), `assert` (bool, default true; stored only, nothing reads it), `startpos` (number 0..1, default 0, applied via the virtual `SetPos`). After reading, `needle_reg` is converted to an offset from the needle's centre: `reg = reg - texSize/2` (integer division). Unused/ignored key names in hud.lua (not in the binary's strings): `needle_scale`, `startingPos`, so the needle draws at its native size and the dial starts at position 0. (F)
`SetPos(p)`: clamp `p` to [0,1]; `angle = minrot + (maxrot - minrot) * p`; invalidate the screen. `GetPos() = (angle - minrot)/(maxrot - minrot)`. `SetAlpha(a)` clamps to [0,1] (alpha of the needle only). (F)
`PostDraw` (0x67750, runs **after** the children): if a needle exists: wrap the angle into [0, 2*pi] by repeated +-2*pi; matrix = translate(`-reg`) then rotate(angle) then translate(`center`) (the needle's registration point lands on `(center_x, center_y)` and the needle rotates about it); draw the needle texture with that matrix and alpha `+0x58`. Then, if an overlay exists, draw it unrotated at the window origin. Rotation convention: `TMat3::Rotate` is `[cos sin; -sin cos]` on row vectors, which on a y-down screen is **clockwise for positive angles** (**I**; consistent with hud.lua `minrot=-1.5, maxrot=1.5` for a needle that points up at angle 0). `KDial` has no `Draw` of its own (an empty window apart from the needle and overlay).
Lua (hud.lua `dialPower`): `x=0,y=0,w=350,h=350, center_x=108, center_y=75, needle="ui/gauges/needle", needle_regx=3, needle_regy=19, maxrot=1.5, minrot=-1.5`. Driven by `VShopHud::Update`: `SetPos(total/goal)` below the goal, `SetPos((total-goal)/(masteryGoal-goal))` after it; in endless mode `SetPos(angryThisLevel/angryLimit)` (`angry_this_level`, `angry_customer_limit` values).

### 9.3 KSweepDial (base `KDial`; 0x68406, 0x68286, 0x687d2, 0x67fce)
Fields (F): `+0x64` segment count (default **32**), `+0x68` segments in use, `+0x6c` start angle, `+0x70` end angle, `+0x74` vertex array `numsegments+1` (`TLitVert`, 0x20 each), `+0x78` index array (u16), `+0x7c` ready flag, `+0x80` sweep texture, `+0x88` `H/W` aspect, `+0x8c` W, `+0x90` H.
Keys: `sweep` (texture, default none), `numsegments` (int, default 32), plus all `KDial` keys (`center_x`, `center_y`, `overlay`, `needle`, `minrot/maxrot`, `startpos`). `W,H` are the window's size at `PostChildrenInit`. hud.lua timer: `x=154,y=49,w=62,h=62, sweep="ui/gauges/timer_sweep", center_x=31,center_y=31, overlay="ui/gauges/clock_overlay"`.
Initialisation (`PostChildrenInit`): start = end = the current angle (`+0x30`, set earlier by `KDial::Init`, i.e. `minrot + range*startpos`, 0 with defaults); aspect `= H/W`; the vertex array is `numsegments+1` white vertices; the index array is triangles `(numsegments, i, i+1)` for `i = 0..numsegments` (so vertex `numsegments` is the **fan centre**); the centre vertex position is `(center_x, center_y)` and its texture coordinate is `(center_x / W, center_y / H)`; ready flag set; `SetPos(GetPos())`.
`SetPos(p)`: `KDial::SetPos(p)`; `end = angle`; if ready, `UpdateSegments()`.
`UpdateSegments()`: `lo = min(start,end)`, `hi = max(start,end) + 0.09817477` (= pi/32); `count = 0`; `step = 2*pi / numsegments`; loop `while (lo < hi or count == 0) and count < numsegments - 1`: vertex[count] = `ThetaToWindow(lo)`, texcoord = window position / (W,H), `count++`, `lo += step`. After the loop one **closing vertex** `ThetaToWindow(hi)` (so the edge overshoots the true angle by pi/32 = 5.625 degrees) goes into vertex[count]. (F; the closing angle confirmed with `otool`.)
`ThetaToWindow(theta)` (0x67fce): the point where a ray from the centre in direction `(sin t, -cos t)` (angle 0 = up, increasing clockwise) leaves the window rectangle. With `k = -cos(t)/sin(t)` and `aspect = H/W`: `sin == 0` -> `(center_x, 0)`; `sin > 0`: `k < -aspect` -> top edge `(cx - cy/k, 0)`; `k < aspect` -> right edge `(W, cy + k*cx)`; else bottom edge `(cx + cy/k, H)`; `sin < 0`: `k > aspect` -> top edge; `k > -aspect` -> left edge `(0, cy - k*cx)`; else bottom edge. **The formulas use `cx`/`cy` where distances to the far edges should be `W-cx`/`H-cy`, so they are exact only when the centre is the window's centre** (true in hud.lua: 31,31 in 62x62). Reproduce as written. (F)
`PostDraw` (0x68286): if the sweep texture exists and `GetPos() != 0` (**nothing is drawn at position 0**): set each vertex colour to white with alpha `+0x58`, bind the sweep texture and draw `count * 3` indices as an indexed triangle list, so the texture is stretched over the whole window rect (texcoord = window position / size) and only the wedge from the start angle clockwise to the end angle (plus the pi/32 overshoot) is visible; then `KDial::PostDraw` (needle if any, overlay). (F)
Game driving (F, VShopHud): non-endless: `SetPos(1 - elapsed/duration)` (the wedge starts full and shrinks), only when the value changed; endless: `SetPos(1.0)`.

### 9.4 KBar (base `KGauge`; Dead, 0x66d76)
Keys: `basepos_x/y`, `startpos_x/y`, `endpos_x/y` (number, default 0 each), `bar` (texture), `overlay` (texture), `startpos` (0..1 position, default 0, a **different** key from `startpos_x/y`). A single textured quad (vertex type 6 with a 4-index list) whose corners are recomputed by `UpdateVerts()` from base/start/end vectors and the position; texcoords = vertex position / window size; then the overlay at the origin. I did not resolve the vertex formula (U); nothing uses it.

### 9.5 RCursor (base `KGauge`; Dead, 0x68e94)
Keys: `background`, `cursor` (textures). `SetPos(p)` clamps to [0,1]. `Draw`: background sprite centred in the window; cursor sprite at `x = cursorTexW + backgroundTexW * p`, `y = h/2`. `SetBackground`/`SetCursor` also grow the window to fit the textures. (F)

### 9.6 BLayeredWindow (base `TLayeredWindow(3)`; 0x764a0)
Constructor makes 3 layers; `PostChildrenInit` = `TWindow::PostChildrenInit` then `SetCurrentLayer(0)`. Lua: `SelectLayer(n)` before children (hud.lua `progress_gauge`: layer 0 goal marker + `goal_score`, layer 1 mastery marker, layer 2 anger marker). VShopHud switches it: layer 1 when `goal <= current_level_total` else layer 0, and layer 2 in endless mode. (F)

## 10. RAdapt and friends (Dead)
`scripts/adapt.lua` builds `RAdapt{ x=kCenter,y=kCenter,w=800,h=600,name="adapt_background",image="",xmlfile="", RSpriteLayer{name="adapt_spritelayer",...} }` plus an OK button; **no script and no C++ opens it**, so none of this runs. In short: `RAdapt::PostChildrenInit` reads `xmlfile` (default `""`), finds the child `adapt_spritelayer` (`RSpriteLayer`), parses the XML (root node `adapt`, children `text`, `image`, `anim`, `sound`, each with sub-nodes) into `RAdaptObject`s and creates them: `text` (`x y width height halign valign label font fontsize padding rotation alpha color` -> a `TText`; align words `left/center/right`, `top/center/bottom`; `color` hex), `image` (`x y source alpha scale` -> a `TImage`), `anim` (`x y source loop delay`; an animated sprite on the sprite layer, plays `"DoAnim"`, looping if `loop == 1`), `sound` (`delay source loop`; a task that plays the sound after `delay` ms). `RSpriteLayer` is a window that owns a sprite list (`AddSprite`/`RemoveSprite`, `Draw` draws them). Skip unless an `adapt` feature is wanted. I did not read `RSpriteLayer::Draw` (0x702ec) or the exact `RAdaptText` layout (U).

## 11. Remaining small classes (Dead unless noted)
* `RButtonedSlider` (`TSlider`; 0x6d90c): `OnMouseDown` sets a drag flag (`+0x79`), `OnMouseUp` clears it; `OnMouseMove` forwards to `TSlider::OnMouseMove` only while dragging or when the innermost `TButton` ancestor of the child under the cursor is the slider itself (same hover test as `RParentButton`); otherwise it calls the `+0x4c` reset hook and returns false. (F)
* `RJustified` (`RWindow`; 0x6dff0): hard-coded experiment: ignores its style and lays out a built-in lorem-ipsum string in `fonts/myriad_pro.mvec` size 15 as two `TTextGraphic`s (left and right aligned halves joined by `<br>`), drawn in the window rect. Not a feature. (F)
* `RAlert` (`RWindow`; 0x6c7cc): keys `delay` (int ms, default 1000), `allowclick` (bool, default true), `usedelay` (bool, default true); starts the 16 ms animation; after `delay` ms of accumulated time (if `usedelay`) or on mouse-up (if `allowclick`) it runs the Lua `PopModal('<top modal name>')` through `DoLuaString`, then stops. (F)
* `RState` (0x706fe): a list of `shared_ptr<RBlock>`; `GetBlock(name)` matches the block's name string at `RBlock+0xc`; `AdoptBlock` appends; `ResetBlocks/LoadBlocks/SaveBlocks` call the block virtuals at `+0x10/+0x14/+0x18` (`LoadBlocks` = reset then load; `ResetBlocks(true)` also saves); typed getters cast to `RIntBlock`/`VBinaryBlock`. It is the persistence registry used by `TSettings`, `VLevelManager`, `VUpgradeManager`, `VTutorialManager`, `VPlotItem`, `VQuestItem`, `VTutorial`, `VAchievement`; **not a UI class**; the save-format analysis should own it. (F)
* `RScreenInvalidator`, `RAlphaSpider`: sections 3.9, 3.8.

## 12. Messages handled or sent (summary)
* Sent: `RWindow` `effect` key -> `TMessage(effect, luastring)` to the window manager; `RAlert` posts nothing but runs `PopModal`; transitions resume Lua and queue Lua strings.
* Handled: `RTransitionManager::OnMessage` types 1001..1013; `RScrollable::OnMessage` type 9; `RTransitionManager::HandleEvent` blocks input event types (section 3.3).
* Lua natives added by this library: only `Lua_DoTransition` (and the Lua `DoTransition` shim). `PlayTransition` (worldmap.lua) belongs to `VWorldMap` (`VWorldMap::PlayTransition(str)`), not to this library.

## 13. Sprite-side classes (out of scope)
`VButton` (a `VSprite`; `OnMouseEnter/Leave`, `SetDisabled`, `SetSelected`, `SwitchAsset`, `LoadFromTable`, `OnClick`; used by `VMachineButton`, plots, tutorials, `VPushPin`), `BZeppelin` (the delivery zeppelin sprite, created by `VDelivery`; plays `zepp_no_box/zepp_pickup/zepp_with_box` and `pickup_zepplin.ogg` / `zepplin_loop.ogg`) and `VTintEffect` (a `VSpriteEffect` with a colour, intensity ramp and pre/post render hooks, used by `VThoughtBubble`/tutorials) are sprite classes handled by the sprite analysis; none is a Lua window type.

## 14. Comparison with what OpenConche already has, and implementation notes
Engine pieces (`src/ui/`): `Window` with virtuals `init/postChildrenInit/draw/postDraw/onAnimate/onMessage/onMouse*`, `BitmapWindow` (image, mask, scale, `takesFaderAlpha`), `TextWindow` (`setTextSize`, `textBounds`, `textFits`, `setLabel`), `LayeredWindow` (`setCurrentLayer`, `addToLayer`), `ButtonWindow` (mouse states, `layerFor`, `setMouseState`, `doCommand`, `setCommand`), `TextEditWindow`, `SliderWindow`, `RectangleWindow`, and a **choc3-style** `TransitionList` (`Transition{}` fades/swipes by window id). None of the `R*` overlays exist.

| Piece | Verdict | Notes |
|---|---|---|
| `RWindow` | thin subclass | `BitmapWindow` + `imagealpha` (set alpha), `disable` (hide), `bgcolor` quad drawn **after** the image, `effect`/`luastring` message, 16 ms `onAnimate` that accumulates `delta/elapsed` from the modal clock. The reparent-to-top-modal branch only matters for windows created before they have a parent; check how `WindowManager` builds children before copying it. |
| `RBounceWindow` | small new | needs the per-window animation tick on the **modal clock** (pauses under other modals) and `setTargetPos`/`onReachedTarget`. Implement the `p(t) = 1 - sin(pi t)/(pi t)` curve (both functions via `funcmin/funcmax`). Keep the "no target keys -> no movement" and "start time = first tick" behaviour. |
| `RShrinkText` | thin subclass | the `TextWindow` already has `setTextSize` and `textFits`; port the loop (strict `<=` test, down to 0) and reset to the max size on every `SetLabel`. Read the font table's second entry as the max size. |
| `RDisableButton` | thin subclass | override mouse handlers per section 5.1 (layer 3 = disabled; keep the click swallowed); `command` wrapper that checks the flag; expose `toggleState(bool)` for `RTextEditDisable` and `VHelpWindow`. |
| `RTextEditDisable` | small | needs a pre-edit key hook in `TextEditWindow` (type, char, accepted) and the rule in 5.3. |
| `KSplit9Image` | new widget | nine textured quads with source rects; implement as direct draws of 9 sub-rects of the texture (no sprite tree needed). Size comes from raw `w`/`h`; edges built once from init size (resizing later should keep the original geometry to match the original). |
| `KDial`, `KSweepDial` | new widgets | needle = rotate about a registration point; sweep = textured triangle fan with the ray/rectangle intersection and the pi/32 overshoot; nothing drawn at position 0. Both draw in `postDraw` (above children). Values are pushed by the shop HUD via `SetPos`. |
| `BLayeredWindow` | trivial | `LayeredWindow(3)` plus `setCurrentLayer(0)` after children. |
| `VTintWindow` | small | dim quad with a `fade` ramp on the parent modal's clock and a `tintOff()` that flips the direction and restarts. Needs a task/redraw while the ramp runs. |
| `RTransitionManager` family | new subsystem | An `OverlayManager` with: current + FIFO queue, a retired list flushed at the next 16 ms tick, per-overlay clock accumulated in `draw`, done flag, the Lua string via the script's command queue, resume of the **stored coroutine** with `true`, input blocking while active. Overlays: colour fade (linear alpha, 500 ms), cross-fade (snapshot, 250 ms, hide/show the window), `BounceOff` (side detection + 4 modes, duration = max `bouncelen`), `TintOff`. Construct-time side effects (section 3.3) must stay in the constructors. Needs a way to render a window to a texture (`RUtil::WindowToTexture`) only for cross-fade, which the shipped scripts never run. The shim `DoTransition` should be defined in the game's prelude exactly as the original's text. |
| `RScrollable`, `RActiveTextLayer` | new, small | straightforward from sections 6.1/6.2; `RActiveText` is an interface the shop code (`VComboText`) implements. |
| `RTimer` | small | `TextWindow` subclass; the shop HUD owns the semantics. |
| Dead classes | skip | RMatch3*, RAdapt*, RScorer, RCursor, RButtonedSlider, RJustified, RAlert, RSplit9Text/Expander, KBar, RParentButton, RAlphaSpider. If they are registered as placeholders the engine's `PlaceholderWindow` logging (`[unimplemented-type]`) will simply never fire for the shipped Lua. |

## Unknown / not determined
* Meaning of `TWindowManager+0x25` (set by every `ROverlay`); who calls `RTransitionManager::HandleEvent` and whether returning 1 stops the event or only the overlay's own handling.
* Exact event type names for the `0x3de0` mask (derived from `window-system.md` 6.2, which is partly inferred).
* `RWindow` reparent branch: net ownership after the remove/add/remove/add sequence (I could not tell which parent finally owns the window).
* Whether `RDisableButton` ever gets a running animation (its `OnTaskAnimate` re-assert); the exact meaning of the state-byte kind bits `(+0x3c >> 4) & 7`; whether virtual `+0x4c` is exactly `OnMouseLeave`.
* `RActiveText` floats at `+0x10/+0x14` (scale/rotation vs something else) and what `TTextGraphic::Draw(rect, a, b, c, tex)` does with them.
* Callers of `RScrollable::ScrollForward/Back`; confirmation that message type 9 is the slider-change message.
* `KBar::UpdateVerts` vertex formula; `RAdapt` text/anim layout details and `RSpriteLayer::Draw`.
* `KSweepDial` behaviour when `W-R < L` or non-centred pivots (original formula is only correct for a centred pivot; reproduced as is).
* Whether the Windows Lua differs from the Mac Lua for any of these scripts (Windows bytecode was not diffed; the Windows exe has the same class/key strings I checked).
* The sign convention of `KDial` rotation on screen (inferred clockwise from `TMat3::Rotate`).
* `RShrinkText` byte at `TTextGraphic+0x6c`.
