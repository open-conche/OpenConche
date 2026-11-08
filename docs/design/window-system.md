# Design: window system

`src/ui/` holds the window tree, the DSL-to-window creation, the modal stack, message dispatch, input, focus and
transitions. `src/gfx/` provides the renderer interface this design draws through. Lua and the coroutine belong to
[script-host.md](script-host.md). Facts come from `docs/re/window-system.md` (cited as WS §n) and
`docs/re/api-windows.md`, and the prelude is `lua/prelude.lua`.

## 1. Behaviour the scripts depend on

- **Children and z-order.** Children are kept in a list whose **first element is frontmost**. A new child goes to the
  front, so siblings written later in a DSL table are on top. Drawing goes from the last child to the first, and hit
  testing from the first to the last (WS 1.3).
- **Name lookup** is pre-order and depth-first in list order, and a window matches itself first. The first match
  wins. Each native has its own search root (WS 1.4):
  - The top modal, walking down the modal chain if not found: `EnableWindow`, `FillWindow`, `SetWindowPos`,
    `SetAlpha`. The original searches the whole screen for the last three; the engine approximates that as
    "top modal down the chain".
  - The top modal only: `SetPosition`, `Transition`, `GetLabel`, `SetLabel`, `SetCommand`, `ImageLoaded`,
    `_SetBitmap`, `SetLayeredLabel`, `GetButtonToggleState`/`SetButtonToggleState`.
  - The top of the script window stack: `RealSetFocus`, `DestroyWindow`, `SetWindowName`, `DoSelectLayer`,
    `DoFitToChildren`, `PostCreate`.
- **Flag 0x100 means visible and enabled at once.** `EnableWindow(name, false)` hides the window. Hidden windows and
  their subtrees are neither drawn nor hit. The main menu relies on this to swap `free_play` with its
  `free_play_disable` silhouette (WS 1.1).
- **Style lookup** checks the window's own table (`gettable`, so metatables apply), then `gCurrentDefaultStyle` and its
  `parent` chain. The table's own `parent` is not followed. Keys are read at `Init`, right after creation, and at
  `PostChildrenInit`, after the children. `MakeDialog` restores the default style *before* `PostCreate` (WS 2.2).
- **Positions.** `x`, `y`, `w`, `h` and `align` go through `FixSize`: negatives measure from the far edge,
  `kCenter + n` centres, `kMax - n` anchors to the far edge, and a size above 80000 fills the remaining space
  (WS 1.5). `PostChildrenInit` does `fit` first and then size/position, so explicit sizes win. `ModalWindow` always
  fits its children first.
- **The script window stack** is the context for creation. `InitWindow` adopts the new window into the stack top,
  calls `Init`, pushes the window and sets `gDialogWidth` and `gDialogHeight`. `PopWindow` never pops the last entry.
  Every modal push or pop resets the stack to `[top modal]` (WS 1.6).
- **Modal chain.** Each pushed modal's parent is the previous top. `GetWindowDepth` is the length of that chain, and
  the screen at the bottom counts. `PushModal(name)` returns the `"%8x"` id string.
  `PopModal(s)` / `DoPopModal(s)` finds the first modal from the top whose name or id string equals `s`, and pops
  everything above it and then the modal itself. If nothing matches, nothing happens. A modal is destroyed one event
  later, and **`kModalClosed` with its id string is posted from its destruction** (WS 3.2, 3.3).
  `ClearModalChildren` empties the top modal and sets its size and position to 0 (WS 3.5).
- **Messages** are FIFO. Handlers can post more, and those are processed in the same pass. `kQuitNow` exits. Windows
  see a message before the script does: target (or focus, or the top modal), then up the parents. At the first modal,
  the modal consumes `kCloseWindow` (non-empty name: `PopModal(name)`; empty name: swallowed). Otherwise the modal's
  `defaultFocus` gets it, and if that does not consume it, the script does (WS 4.2).
- **Buttons.** On mouse down a button captures the mouse. On mouse up inside it, `Press` runs, then `DoCommand`
  (WS 4.3):
  1. With `close` set: post `kCloseWindow` for the parent modal and set `gReturnValue` to the button's name.
  2. Build a `kButtonPress` message named after the button.
  3. If the button has a command, deliver `kCommandOnly` plus the command to the script **synchronously**.
  4. If the button is still attached, post the `kButtonPress` message.

  Mask hit test: inside when any of R, G or B is above 19 (`HitMask::kThreshold`). A masked-out mouse down is still
  swallowed.
- **Unhandled input** falls through to the next sibling behind under the point, then to the parent. At a modal,
  an unhandled mouse up or key down posts `kPressAnyKey`, unless the `overlaymouse` config is set (WS 6.5).
- **Keys** go to the focus window and bubble up to the modal. The modal handles hotkeys (`normalizeKeyString`), and
  Enter/Escape press the `default`/`cancel` button or else send `kDefaultAction`. A mouse down focuses the nearest
  focusable ancestor (WS 4.3, 6.5).
- **Transitions** show the target on every step. They set alpha on the target and all its descendants. The default
  time is 100 ms. A missing window at the call means a silent no-op and `onend` is never called (WS 6.4).
- **Not copied:**
  - The transition's raw `TWindow*`. Here a transition holds a `WindowId` and runs `onend` exactly once when its
    window disappears, as WS 6.4 recommends.
  - `TButton`'s mask index (`clientW / maskW * x`). It is wrong for scaled buttons, such as the main menu's
    `scale = 90/128`. We map the point by proportion.
  - An unknown `typename` silently creating nothing and then letting `PopWindow` pop the parent. Types the engine
    does not implement get a placeholder window instead.
- **Not implemented.** The engine does not implement the original's overlay windows, its debug hover outline, or its
  cursor modes (platform event type 15, `GetCursorMode`). The `overlaymouse` config only gates `kPressAnyKey`. The
  platform layer can set a custom image cursor (`platform::Window::setCursor`), which the games use.

## 2. Types and ownership

`WindowId` is a `std::uint32_t`, never reused, 0 meaning none (`src/ui/Message.h`). The classes, in `src/ui/Window.h`:

- `Window`: a node of the tree. Its constructor takes the next id from the manager and registers in the manager's id
  map; its destructor unregisters. `parent()` is a raw pointer (a parent always outlives its children),
  `children()` is a `vector<unique_ptr<Window>>` with `[0]` frontmost, `adoptFront` and `orphan` change it, and `find`
  is the pre-order lookup. State: `name`, `rect` (in parent coordinates), the flag bits (`shown` is flag 0x100,
  `focusable` 0x002, modal 0x001) and `alpha`. Virtuals, all no-ops by default: `init`, `postChildrenInit`, `draw`,
  `postDraw`, `onMessage`, the mouse handlers (`onMouseDown/Up/Move/Leave`), `onKeyDown`, `onChar`, `onText`,
  `onSetFocus`, `onAnimate` (window animations) and `onForeground`.
- `ModalWindow`: adds `defaultFocus`, the hotkey map, the script path it was pushed for, and a `ModalClock` that is
  paused while another modal covers it. It handles `kCloseWindow`, hotkeys, Enter and Escape.
- `Screen`: the bottom modal, 800x600, never popped.
- `PlaceholderWindow`: a plain window that keeps what the style table says (name, laid-out rectangle, label) and draws
  nothing. It is the plain `Window` type and the stand-in for any type without a class.

`WindowManager` (`src/ui/WindowManager.h`) owns everything: the modal chain (`modals_`, `[0]` = Screen, `back()` = top),
the deferred-delete list `destroyed_`, the id map `byId_`, the message queue, the transition list, the window
animations, focus and hover ids, the mouse listeners, the creation stack (as ids), the type factories, and non-owning
pointers to the texture cache, sprite sheets and text system. The `ScriptSink` it calls is set by the script host.

Ownership rules, which are what stop dangling pointers:

1. **Children are owned by their parent with `unique_ptr`; modals are owned by `modals_`.** A modal's `parent()` is the
   modal below it. That is valid because popping always goes top-down, and a popped modal's parent pointer is cleared
   when it moves to `destroyed_`. Modals are not in their parent's child list, so drawing and name lookup step from
   one modal to the next explicitly (WS 1.3 has the original skipping flag-0x001 children instead).
2. **Everything outside the tree holds a `WindowId`, never a pointer**, and resolves it through `window(id)` each time
   it is used: focus, hover, mouse listeners, creation stack, message targets, transitions, window animations,
   `defaultFocus`. A destroyed window resolves to null, which reproduces what `OnDestroyWindow` and
   `SafeDestroyWindow` clear by hand (WS 1.2).
3. **Lua never runs while the tree is being iterated.** Input routing finds the target first and then calls handlers
   on it. Anything a handler does to the tree is safe because destruction is deferred: `destroyWindow` and
   `popModal` move the window to `destroyed_` and mark it unshown, and `clearDestroyed()` deletes it at a defined
   point (§6). After a call that may run Lua (`press`, `deliver`), the caller re-resolves ids before touching
   anything.
4. The `Style` views exist only during a native call. Windows copy values out of them. Lua functions are kept as
   `script::LuaRef`, owned by the window or transition, so they are released before `LuaState` dies (see the
   script-host §2 order).

`ScriptSink` (`deliver`, `runInScript`, `callProtected`, `setGlobalString` and a few helpers) and `WindowQuery`
(`isShown(name)` and the harness queries) are the only interfaces. `ui` does not include `ScriptHost`.

## 3. From DSL table to windows

The constructors are Lua. For each type, `AddWindowType(name)` runs the exact template
`function %s( table ) Creating( '%s', table ); table.typename='%s'; return function() DoWindow( table ) end end;`
and records `name -> factory` (WS 2.4). The natives underneath (`src/script/WindowNatives.cpp`) are:

- `GenericCreate(t)`: builds a `LuaStyle` over `t`, reads `typename`, and asks the manager for a window of that type
  (`createWindow`: a registered factory, then a built-in widget, else a `PlaceholderWindow`). A type with no class is
  logged once as `[unimplemented-type] MapWindow`. Then `initWindow`:
  1. `creationTop()->adoptFront(w)`.
  2. `w->init(style)`.
  3. Push the id onto the creation stack.

  `GenericCreate` then sets the globals `gDialogWidth` and `gDialogHeight`.
- `PostCreate(t, typename)`: `creationTop()->postChildrenInit(style)`.
- `PopWindow()`: pops unless one entry is left. `SetWindowName(n)` names the stack top.
- `Style` (`src/ui/Style.h`): `number`, `string`, `boolean`, `numbers`, `strings`, `function` and nested table views,
  implementing `_GetTag` exactly (§1) in `script::LuaStyle`. It reads `gCurrentDefaultStyle` at call time. `boolean`
  follows the `PopBool` rules. `MapStyle` is the fixed key/value implementation for tests and windows made from C++.
- Layout (`src/ui/Layout.cpp`): `resolveAxis` resolves one axis of `FixSize`, including the early-outs in WS 1.5, and
  `sizeAndPositionFromStyle` applies it to a window. `resolveAxis` is a pure function with a unit-test table.
  Combinations the RE note did not trace fall back to the literal value.
- `fitToChildren` grows the window to the union of its children's right and bottom edges (hidden ones included), never
  shrinking and never moving children (WS 1.5, inference).

Widget types are `Window` subclasses in `src/ui/widgets/`, registered as built-ins:

| Type | What it is |
|---|---|
| `Window` | plain window (a `PlaceholderWindow`) |
| `Bitmap` | `image`, `scale`, flips |
| `BitmapTint` | an image with a tint |
| `Rectangle` | a filled rectangle |
| `Button` | a layered window with a state machine, `graphics`, `mask`, `close`, `default`, `cancel`, `command`, toggle and radio types |
| `Text`, `TightText` | label, font, flags, laid out by `gfx/text` |
| `TextEdit` | text input |
| `Slider` | a slider |
| `BSGWindow` | a window with an optional fill and frame |
| `Rollover` | a hover popup (`PopupWindow` is its popup) |
| `OptionsWindow` | the Options dialog's native part |
| `CreditsWindow` | scrolling credits |

Game windows (`src/games/`) register their own factories. A type with no class stays a placeholder.

**Layers** (`LayeredWindow`, the base of `Button`): `DoSelectLayer(n)` sets the layer that newly adopted children
join; -1 means all layers. `setCurrentLayer(n)` hides the children and shows the ones listed for layer n, so drawing
and hit testing need no layer check beyond the shown flag. The displayed layer follows the button state
(`ButtonWindow::layerFor`): a push button shows 0 up, 1 down, 2 over; a toggle or radio uses 0/1 for off/on and 2/3
for over off/on. `SetLayeredLabel` sets the text of `label<N>` on each of the four layers.

## 4. Modal stack

`pushModal(name)` does, in order (WS 3.2):

1. Clear hover. Send `onMouseLeave` to the hovered window and to each mouse listener, and clear the list.
2. Pause the old top's clock.
3. Append the new modal and set its parent to the old top. A zero-width modal takes its parent's size.
4. Focus the new modal (`onSetFocus` moves focus to `defaultFocus`).
5. Reset the creation stack to `[new]`.

The Lua `PushModal` returns `idString(id)`, which is `"%8x"`.

`popModal(s)` searches from the top for a modal whose name or id string equals `s` (the Screen never matches). It pops
top-down until it has popped the match. Each popped modal is detached (parent cleared, moved to `destroyed_`), and
focus and the creation stack are reset to the new top. The new top's clock is started. `clearDestroyed()` deletes the
queued windows in the order they were queued, and for each `ModalWindow` posts `Message{kModalClosed, id string}`
after it is deleted. The message is posted there, not from a destructor, so nothing posts into a manager that is being
torn down.

`clearModalChildren()` destroys the top modal's children (deferred), sets its size and position to 0, and marks the
modal so that the next script file loaded becomes its script path (`SwapToModal`).

Input goes only to the top modal (§5). Drawing covers every modal from the bottom to the top, so dialogs show what is
under them.

## 5. Messages, input and focus

- `post(Message)` appends to `queue_`. `Message` is `{type, name, target}`; the type constants match the prelude's
  `k*` values.
- `dispatchMessages()` pops from the front until the queue is empty, with a guard of 10,000 per call that logs and
  stops. For each message (`handle`):
  1. `kQuitNow` requests app exit.
  2. Message listeners for the type run (a window can register one for a message that does not climb through it).
  3. Resolve the target; if it is missing, use focus, else the top modal.
  4. Climb through `onMessage` to the first modal, then the modal's `defaultFocus`, then
     `sink->deliver(msg, nullptr)`, as in WS 4.2.

  `PostMessageToParent` sets `target` to the top modal's parent. `sendMessage` handles a message at once without
  queueing it.
- `handleInput(ev)` takes events in logical coordinates (floored to integers). Only the left button clicks; a press
  in the letterbox bars is ignored. For a mouse event:
  1. Captured windows (mouse listeners) get the event first, in capture order.
  2. Otherwise, from the top modal, descend through the **first shown child whose rect contains the point** to the
     deepest such window. A mouse down focuses the nearest focusable ancestor.
  3. Call the handler. If it does not handle the event, try the next shown sibling behind under the point (the point
     is translated into that sibling's space), then the parent, as in WS 6.5.
  4. Hover follows the window that handled a move. At the modal, an unhandled mouse up posts `kPressAnyKey`
     named `mouse:up`.

  `press`/`doCommand` run after the descent. While the command runs, the button can be detached but is not deleted,
  because deletion is deferred. `doCommand` re-checks that the button is still attached before posting `kButtonPress`.
- Keys: `KeyEvent{name, shift, ctrl, alt}` (the SDL key name) goes to the focus window (or the top modal) and bubbles
  up to the modal. The modal's `onKeyDown` handles hotkeys; an unhandled key down posts `kPressAnyKey` named
  `raw:<char>`. For Enter, Escape and Tab the character event follows, and the modal's `onChar` presses the
  `default` or `cancel` button, else sends `kDefaultAction` to the focus window. Which button counts follows
  `FindDefault`: the last enabled, visible match wins.
- Text: typed UTF-8 text (`InputEvent::Type::Text`, from SDL text input) goes to the focus window and bubbles up.
  Only `TextEdit` takes it, and the platform's text input is on only while the focus window wants it
  (`textInputWanted`).
- `RealSetFocus(name)`: looks the name up under the stack top. A missing name clears focus, as in the original.

Some per-game switches change the routing to match the original game being run: the C2 fall-through point rule
(`setLegacyFallThroughPoint`), the C2 sprite alpha hit test, the C2 popup fader, C2 `Rollover` options, and C2's
Enter/Cancel `default` quirk. They are set from the selected game at startup; the reasons are in the `docs/re/`
notes cited at each setter in `WindowManager.h`.

## 6. Order of work per frame

The script host's §3 frame order, with the window steps expanded (`App::run`):

1. For each input event: `clearDestroyed()`, `dispatchMessages()`, `handleInput(ev)`.
2. Tick: `tickTransitions(now)`, `tickWindowAnimations(now)`, then `clearDestroyed()` and `dispatchMessages()`, then
   the script poll. After a destruction the image caches are trimmed (`TextureCache::trim`, sheets too).
3. `draw()`, then `dispatchMessages()`.

`draw()` draws every frame. The original's dirty flag and 12 ms throttle (WS 6.3) give the same picture.
`drawWindow(w, ctx)` (`src/ui/Draw.cpp`) does:

1. Skip `w` if it is not shown.
2. Compute the screen rect as the parent's origin plus `w.rect`, and intersect it with the current clip. If the result
   is empty, return (children too).
3. Push the clip, call `w.draw(ctx)`, then draw the children from the **last to the first**, then `postDraw`, then pop
   the clip. A window that paints outside its rectangle (`BSGWindow`'s frame) does so in `drawOutside`, right after
   `draw`, with only the ancestors' clip in force.

Each window draws with its own `alpha` (fades set alpha on whole subtrees, §7), so there is no alpha multiplication.
Modals are drawn bottom to top. The opaque-full-screen skip (flag 0x800) is an optimisation and is left out.

Window animations (`StartWindowAnimation`) call `Window::onAnimate` when the interval is due on the owning modal's
clock, so they freeze while a dialog covers the modal. With catch-up they run once per due step.

## 7. Transitions (`src/ui/Transition.h`)

A `Transition` has a target id, start time, duration, a kind (`FadeIn`, `FadeOut`, `Swipe`, `Path`, `ZoomIn`), the
kind's parameters and an optional `onend` `LuaRef`. `TransitionList` holds them.

The `Transition{...}` native follows WS 6.4 step by step: lookup under the top modal, `time` defaulting to 100 and
clamped, the styles, `center` and unknown styles releasing `onend` without calling it, and `fadein` setting alpha to 0
at once. A new transition goes into the list.

`tick(now)` runs once per frame. For each transition:

1. Resolve the target.
2. **If the window is gone: take its `onend` out, log it, and remove the transition.**
3. Otherwise show the window, compute `t = min(1, elapsed/duration)` and apply the effect. When `t == 1`, take its
   `onend` and remove the transition.

The `onend`s collected this way run with `callProtected` **after** the loop, so they can start transitions,
destroy windows or close dialogs safely. "Taken out" means moved, so each `onend` runs exactly once. A transition never
touches a window that has gone, and it never outlives its window by more than one tick. That also covers the case the
original handled with its `kModalClosed` listener. Chocolatier 2's popup fader advances in fixed steps (`stepMs`)
instead of smoothly.

## 8. Renderer, textures, masks, fonts

`gfx::Renderer` (`src/gfx/Renderer.h`) is an abstract interface. Its SDL3 implementation is `platform::SdlRenderer`,
built on `SDL_RenderGeometry`, so no SDL code lives outside `src/platform`. `gfx::RecordingRenderer` records calls for
tests. The interface has `pushClip`/`popClip` (intersecting), `drawTexture` (source and destination rectangles, tint,
blend, rotation, flips), `drawGeometry` (glyphs, particles), `fillRect` and `outputScale` (output pixels per logical
pixel, for text rasterisation).

- **Coordinates** are logical 800x600 floats. Clips are logical too. `DrawContext` carries `Renderer&`, the screen
  origin, the window's alpha and the current clip.
- **Render targets.** The interface declares `createTarget`/`setTarget`, but `SdlRenderer` does not implement them
  (each logs an error and does nothing). Nothing needs them: fades set per-window alpha on a subtree
  (`TWindowFader`), and the original's render-target mode only caches the opaque layer (WS 6.3).
- **`TextureCache`** (`gfx`) maps a resolved lowercase path to `std::shared_ptr<Texture>`. It applies the probing
  order `.png .jpg .jp2` and the `?options` suffix. Windows hold the `shared_ptr`. The cache keeps what it needs to
  re-upload every live texture after a device reset (`reuploadAll`). `trim()` drops entries no window uses and runs
  after windows are destroyed. A `.mask.png` beside a JPEG, or an explicit mask, supplies its alpha at load.
- **`HitMask`** is a CPU-side RGB copy, loaded through the cache (`loadMask(path)`). It needs no GPU, so it works
  headless. It is used by `Button`'s `mask` key. `AlphaMask` serves the C2 sprite hit test.
- **Fonts and text** live in `src/gfx/text/` (`Font`, `TextLayout`, `GlyphAtlas`), per `docs/re/fonts-and-text.md`.
  `Text` and `TightText` lay out through the `TextSystem` the manager points at; glyphs are rasterised at the
  renderer's output scale. Without a text system (tests with no renderer) a label is stored and returned but not drawn.
- **The window, its size, fullscreen and the letterbox** are in [display.md](display.md): one logical 800x600 space
  presented through SDL's letterbox mode, a resizable window, borderless desktop fullscreen, and text rasterised at the
  output scale.

## 9. Tests

- **Unit tests** (doctest, no window): `tests/test_ui.cpp` (tree, ids, modals, dispatch, natives, style chain),
  `tests/test_layout_draw.cpp` (`resolveAxis`, layout, draw order and clipping with `RecordingRenderer`, `SetAlpha`,
  `Bitmap`), `tests/test_buttons.cpp` (button layers and masks, hotkeys, Enter/Escape, hit testing, fall-through,
  capture, focus), `tests/test_transitions.cpp` (each style, `onend` exactly once on completion, on `DestroyWindow` of
  the target, on `SwapToModal` and on a popped modal; `time=0` finishing on the next tick).
- **Scripted headless tests**: tiny asset folders under `tests/assets/`, driven by `.script` files and the
  `tests/check_*.py` runners.
  - `expect-window` proves that a window exists and is shown (script-host §8), and `expect-label` checks a label.
  - `screenshot-logical` plus `check_scene.py` colour probes at known points prove the drawing.
- **Real-data checks**: the main menus of each game at all three display modes, compared by eye with a reference crop
  of the original menu (not shipped), and clicks at scale 2 and in fullscreen that open Options (`expect-window` on an
  Options window name).

## 10. Choices and deviations

- Every frame is drawn, without the dirty flag and throttle.
- Every modal is drawn, without the opaque skip.
- Fades multiply no alpha; they set it on subtrees.
- Placeholders stand in for types with no class.
- Name lookup from the screen is approximated as "top modal down the chain".
- Messages carry no Lua identity.
- A transition whose target is destroyed by `SwapToModal` still runs `onend`. If that `onend` calls `CloseWindow` (as
  `FadeCloseWindow` does) it can close the *new* screen. The original crashed in that case instead; the engine logs it.
