# Enhancements

Optional conveniences the original games never had: the mouse wheel scrolls lists and adjusts quantities,
Chocolatier 1's trade screen commits what is pending when it is closed, and Chocolatier 2's money counts up in finer steps. They are the one place the engine
deliberately departs from the originals' observable behaviour, so they are switchable per game and nothing in
this document applies when the switch is off.

The games' scripts are never modified, on disk or in memory. An enhancement is engine-owned Lua (an *overlay*)
that runs beside the game's scripts and reaches them only through what they already expose: globals, widget
names and natives. The engine gives the overlay two generic hooks and knows nothing about any game itself.

## 1. The switch

- Config key `enhancements`, `1` or `0`, read once at boot by the script host.
- `games.cfg` holds it per variant: `<variant>.enhancements = 0`. A missing line is on, and a rewrite keeps an
  off value (the same shape as `fixbugs`). The launcher's card for a game shows an `Enhancements`
  checkbox (a drawn square, filled when on, because the built-in font has no brackets) that toggles and saves it at
  once; a variant with no overlay (section 5) has no checkbox. Details: game-finder.md section 2.
- An interactive run takes the value of the variant it boots. The finder, the saved choice and the in-game switcher
  carry the booted variant id (`Options::variantId`), and the entry of that id in `games.cfg` is read (on when it has
  none), so a moved install or two variants that share a folder still read their own switch. A plain `--assets` run
  carries no id: its value is the entry whose `assets` folder is the booted one (`variantForAssets`), on when the
  folder is not in the library. The web build has no library and is on.
- An automated run (`isAutomatedRun`: `--headless`, `--script`, `--frames`, `--screenshot`, `--dump-windows`,
  `--log-unimplemented`, `--no-assets`, `--pick-game`) never reads the library and is **off**,
  so the suite keeps exercising the originals' behaviour. `--config enhancements=1` turns it on for a test, and
  `--config` wins over the library in every run.
- Off means: no overlay is loaded, no hook is installed, and wheel events are dropped (not routed, not logged).
- An overlay that fails to load or run is logged as `[enhancements] <error>` and then `[enhancements] off`; the
  `Enhance` global and the `Enhance*` natives are removed, no hook is installed, and the boot goes on as for off.
- The overlay is fixed at boot: changing the switch for the running game applies from its next start.

## 2. The wheel event

- `platform::Window::pollEvents` turns `SDL_EVENT_MOUSE_WHEEL` into a platform input event carrying the pointer
  position in logical coordinates and a whole number of notches, positive away from the user (up). SDL's
  `integer_y` is used, and `SDL_MOUSEWHEEL_FLIPPED` is undone. Horizontal movement is ignored. A wheel event
  with the pointer in the letterbox bars is dropped.
- `ui::InputEvent` carries `Type::Wheel` and `int wheel`.
- `InputInjector::injectWheel(x, y, notches)` pushes the synthetic SDL event, and the script driver has
  `wheel X Y N` (N signed, non-zero). The web build needs nothing: the browser's wheel arrives as the SDL event.
- `WindowManager::handleInput` passes it through the input filter like any event, then to `routeWheel`.
  With no wheel hook installed `routeWheel` does nothing. Otherwise it collects the top modal and **every shown descendant of it
  whose screen rectangle holds the pointer**, front to back (a depth-first walk in child order, a parent after its
  children; the modal's own rectangle does not bound its children, as for mouse input), and calls the hook with that list and the notch count. Captured mouse listeners are not
  consulted and hover and focus do not change.
- The wheel is not a window message: the games' scripts never see it.
- The app logs each wheel event that reaches the windows as `[input] wheel X Y notches N` (with `--log`).

## 3. The two hooks

Both are `std::function`s on the `WindowManager`, installed by the script layer only when the switch is on.
In C++: `WindowManager::WheelHook` is `bool(const std::vector<Window*>&, int)` and `CommandHook` is
`script::LuaRef(const std::string& name, const script::LuaRef* original, bool closes)` (an invalid ref is `nil`);
`src/script/Enhancements.cpp` installs them and `ScriptHost` removes them on destruction. The press of a string
result is `WindowManager::pressButtonNamed`.

**Wheel hook.** `bool(const std::vector<Window*>& under, int notches)`. The script layer calls the overlay's
`Enhance.onWheel(notches, names)` on the main Lua state under `pcall`, where `names` is the array of the
non-empty names of `under`, front to back. The handler must not yield (no dialogs, no `Pause`). Its result:

| result | meaning |
|---|---|
| `nil` / `false` | not handled; nothing happens |
| `true` | handled |
| a string | the engine presses the button of that name, once per notch |

The press happens after the Lua call has returned, through `ButtonWindow::press()`, on the first button of that
name in the top modal that is shown and enabled (the whole chain); no such button is a quiet no-op. After each
press the loop stops unless the top modal is still the same one, no close request for it is pending and the button
is found again, so a `close` button is pressed once however many notches arrived. This is how
an overlay scrolls a list whose arrows are closures: the game's own command runs and does its own clamping.

**Command hook.** Consulted in `ButtonWindow::doCommand` for a button that has a command or `close`, before
anything is posted: `Enhance.beforeCommand(name, original, closes)` is called on the main state under `pcall`
(`original` is the button's command or nil, `closes` its `close` flag) and must not yield. `nil` leaves the press
exactly as it was. A function *replaces the press*: the engine delivers that function through the script sink in
place of the command and does **not** post the close message or set `gReturnValue`; the replacement runs where a
command runs, so it may open dialogs, and it decides whether to call `original()` and whether to close with
`Enhance.close(name)`. A replacement that calls `original()` before `Enhance.close(name)` lets `original` see the
previous `gReturnValue`: the original press sets it first. The `kButtonPress` message is posted as before.

A third hook, `Enhance.textLimit(name, length, ignore)`, is asked by a non-UTF-8 TextEdit for every typed character with
the field's name, `length` and `ignore` style values. A number of 1 or more replaces how many characters the field holds
(the original holds `length - 1`); anything else keeps the original limit. The overlay's default returns nil.

A Lua error in any hook is logged once per message text and counts as "not handled" / `nil`.

## 4. The overlay

- `lua/enhance.lua` (shared) and `lua/enhance_<game id>.lua` are embedded like `host.lua`
  (`cmake/EmbedLua.cmake`, `src/script/Embedded.h`). With the switch on the host loads the shared file and then
  the game's, right after `host.lua` and before the game's entry script; a game with no file loads neither.
  The log says `[enhancements] on (<game id>)` or `[enhancements] off`.
- `enhance.lua` creates the global table `Enhance` with no-op `onWheel` and `beforeCommand`, and helpers:
  `Enhance.has(names, name)`, `Enhance.number(label)` (`tonumber` or 0), `Enhance.clamp(v, lo, hi)`.
- Natives, registered only with the switch on:
  - `EnhanceClose(name)`: what a `close` button's press does: posts `kCloseWindow` for the top modal and sets
    `gReturnValue` to `name`. `Enhance.close` is this.
  - `EnhanceWindowExists(name)`: whether the top modal has a shown window of that name.
  - `EnhanceSetText(name, text)`: sets the text of the named TextEdit in the top modal as typing leaves it: a pending
    `clearinitial` text is dropped, the caret goes to the end, `kTextEditChanged` is posted and the field's `onkey`
    callback runs (native `SetLabel` does none of that). Returns whether such a field was found. It is the
    `TextEditWindow::setTextAsTyped` member. The callback runs where the wheel hook runs, so it must not yield.
  - `EnhanceHasButtonGraphic(image)`: whether the top modal has a shown button whose idle graphic (its first state
    image) is `image`. This is how an overlay sees a game's unnamed scroll arrows.
  - `EnhanceButtonGraphic()`: while `Enhance.beforeCommand` runs, the idle graphic of the button being pressed (nil
    outside that call). This is how an overlay tells two unnamed close buttons apart.
- An overlay file only assigns `Enhance.onWheel` / `Enhance.beforeCommand`, the flags section 5 names and locals. It never wraps a game
  global, and assigns one only where section 5 says so (Chocolatier 1's ledger position, which nothing else can move). Every handler **feature-detects**: a global or window it needs that is missing
  (a mod replaced the script) means `nil`, so the game behaves as the original.

## 5. Per game

**Chocolatier 1** (`enhance_choc1.lua`)
- Ledger: the wheel over the ledger's tab area moves `gCurrentLedgerTop` by one per notch and calls
  `UpdateDynamicWindow("ledgertabs")`. The bounds are the game's own: the arrows are unnamed buttons built only when
  there is somewhere to go, so the overlay steps up only while the up arrow exists and down only while the down
  arrow exists (`EnhanceHasButtonGraphic`; messages: one line; inventory: one page).
- Trade screen (`ui/buysell.lua`): the wheel over an item's field, price tag or (platter rows, the shop) platter
  sets that item's quantity field to its value plus the notches, clamped to 0..9999 and, when selling, to `gSim.inventory[item]`, then
  `UpdateDynamicWindow("receipt")`. 0 restores the row's idle label (a blank field while the number pad types into that
  row, as after Backspace). The sack icon of a buying row has no name, so the wheel over it alone does nothing. Nothing is bought or sold: the receipt
  shows it as pending, exactly as typing does.
- Closing with something pending: the trade screen's OK (the unnamed close button whose idle graphic is the game's
  yes button, told from Sell All by `EnhanceButtonGraphic`) is replaced through the command hook. The items, kind
  and character come from `gDialogTable`, remembered at every wheel and press on the screen because the game's own
  notice and the haggle dialog overwrite it. With nothing
  pending it behaves as before. Otherwise the game's own commit runs first (what the number pad's OK does: its
  buy or sell sound, `HideNumberPad()`, then `LMarket.Buy()` / `LShop.Sell(character)`). When buying, a quantity
  still pending afterwards means the purchase was refused: the game showed its own notice and kept the quantities, and
  the screen **stays open**. Anything else, and every sale, then runs the original command and closes. Escape and Sell All are untouched. To leave without trading, clear the quantities.

- Typing: the trade screen's quantity fields are made with `length=4`, so the original holds three typed digits while the
  number pad reaches four. `Enhance.textLimit` lets a numbers-only field of `length` 4 hold four digits.

**Chocolatier 2** (`enhance_choc2.lua`)
- Ledger messages and inventory tabs: with the pointer over the ledger and its messages or inventory page showing (the
  window names `ledger` and `messages` / `inventory` under the pointer), the wheel presses `scroll_up` / `scroll_down`
  (the game's step: seven lines, or a page; an arrow the game has hidden is not pressed). The factories tab has no
  arrows. The ledger counts only while it is in the top modal, so over a dialog opened above it the wheel does nothing.
- Number pad (`ui/buysell.lua`): while the pad is the top modal (`gNumberPadTarget` names a window that exists there),
  the wheel anywhere adds the notches to that field (clamped to 0..9999999) with `SetLabel` and calls
  `gNumberPadCallback()`, which applies the game's own selling clamp and subtotal.
- Money count-up: the overlay sets `Enhance.smoothMoney = true`, which the native `Ledger` reads when it is built. The
  shared counter then moves by `max(1, gap / 20)` whole dollars every 50 ms (about a second, the rate of Chocolatier 3's
  ledger) where the original moves by `max(1, gap / 7)` every 100 ms. The quest bar keeps its 100 ms step: it moves on
  every second tick.
- There is no pending list in this game, so nothing is committed on close.

**Chocolatier 3** (`enhance_choc3.lua`, also loaded for Reforged)
- Quest log: the wheel anywhere on the dialog (`questlog` under the pointer) presses `scrollUp` going up and
  `scrollDown` going down; the game enables and shows each arrow only while there is more, so the list never
  over-scrolls.
- Register popup (`ui/ui_buysell.lua`): the wheel anywhere on the popup (`ui_register`) sets `count` to its value
  plus the notches, clamped to 0..9999999, through `EnhanceSetText`, so the game's own `onkey` (`Update`) runs exactly
  as for a typed digit: it clamps to what can be bought or sold and rewrites the subtotal. Up and down are the same
  operation with opposite signs; the `+1` button is not pressed (it has no name, and the result is identical).
- Reforged's `ui_questlog.lua` and `ui_buysell.lua` keep the same window names, `onkey` and arrows, so the same file
  works there unchanged; a mod that renames them leaves the wheel without effect.
- Nothing is committed on close: every trade is its own popup.

The Great Chocolate Chase has no overlay.

## 6. Tests

- Unit (`engine_tests`, synthetic fixtures in `tests/assets/`): wheel routing and the `under` order; both hooks
  including the string result, the replaced press with and without `Enhance.close`, and a hook error; `EnhanceSetText`
  (text, change message, `onkey`, a missing or non-TextEdit target); the
  switch off (no `Enhance` global, wheel dropped, presses and their messages untouched, destroying the host removes
  both hooks); every embedded overlay loading cleanly; a failing overlay booting as off; multi-notch presses of a
  close button; a replacement resumed into the Main coroutine; the per-variant lookup of the switch (moved install,
  shared folder); `games.cfg` round trip of the per-variant key.
- Scenarios (one per game, `--config enhancements=1`, skipped without the game's assets): the wheel in each
  place listed in section 5, and for Chocolatier 1 the three closes (nothing pending, affordable, not affordable).
  `choc1_enhance` (tests/check_choc1_enhance.py), `choc2_enhance` (tests/check_choc2_enhance.py), `vanilla_enhance` and `reforged_enhance` (tests/check_choc3_enhance.py,
  one script, two datasets). Each also runs the same wheel turns with the switch off and checks that nothing moves.
