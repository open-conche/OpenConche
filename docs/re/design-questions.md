# Answers to the open questions in the script-host and window-system designs

Source of truth: the decompiled Mac i386 binary (`re/mac/decomp/`, index `re/mac/index.tsv`, strings `re/mac/strings.tsv`, binary the Chocolatier 3 Mac app bundle's `Contents/MacOS/chocolatier-decadence-design`), then the Chocolatier 3 Mac app bundle's `Contents/Frameworks/`, then the Lua in the original install's `assets/` and the Reforged mod's `assets/`. Addresses are Ghidra/Mach-O VM addresses.

Method note. Ghidra's output hides most string constants in this binary because the code is position-independent (strings are reached as `ebx + constant`). Where a string below is quoted from a PIC function, I resolved it by taking the PIC base from the function's own `get_pc_thunk` call site in the `otool -tv` disassembly and looking the address up in `strings.tsv`. For virtual calls I read the vtables straight out of the Mach-O. Items marked **(inference)** are not directly visible in the code. Everything in "Unknown" is genuinely unresolved.

---

## 1. What resumes a plain `coroutine.yield` (the prelude's `Yield`), and with which values

Nothing in the shipped Lua calls `Yield()` (`grep` over the original install's `assets/` and Reforged finds no use), so this is about what the engine would do, not about game behaviour.

Facts:

* `Main` runs as the body of the `TScript`'s own `lua_State` (there is no `coroutine.create`). `TScript::RunFunction` (0x7b41e) starts it with `lua_resume(L, 0)`. `TScript::CoroutineActive` (0x79d70) tests `L->ci != base_ci && (ci->state & CI_YIELD)`.
* `TScript::Resume(n)` (0x7ad8c) is the only place that calls `lua_resume` after start. It first runs the deferred-delete list for `TLuaTable`s, calls `lua_resume(L, n)`, and on a non-zero return prints `"lua_resume error: %s"` through `WriteDbg` (which writes to `std::cerr`) and pops the message. It does not look at anything the coroutine yielded.
* The callers of `Resume`:
  1. `TScript::Animate` (0x7ae4a): `Resume(0)`, run by the task list. If it returns an error the task is paused, and if the thread is no longer suspended afterwards the task is paused too.
  2. `TWindowScript::OnMessage` (0xa1c08): `Resume(1)` or `Resume(2)` (message, optional command function). Only when the coroutine is suspended **and** the state word at `TWindowScript+0x50` equals 3.
  3. `TWindowScript::InjectFunction` (0xa0566): pushes nil under the closure and calls `Resume(2)`.
  4. `TWindowScript::CloseRequested` (0xa21b2): `Resume(0)`.
* The state word `+0x50` is written in three places only: the constructor sets it to 1, `HandleGetMessage` (0xa04b6) sets it to 3, and `OnMessage` sets it to 0 when `Resume` returned an error. **It is never set back to anything else after a normal return from a wait.** So after the first `WaitForMessage`, state stays 3 for the life of the script, whether or not the script is actually inside `HandleGetMessage` at the moment.
* The timer. `HandleGetMessage` calls `TAnimTask::SetDelay(tonumber(arg), repeat=true, absolute=true, false)` when `lua_gettop == 1` (always true from the prelude, which passes its first argument even when nil) and `Pause()` otherwise. That delay is not cleared when the wait returns. A script that then does a plain `coroutine.yield()` therefore stays on the task list and is woken at the **last wait's interval**: typically 0 (every tick), or the remaining time of the last `Pause(ms)` (for example 3000), repeating.

Therefore a plain `Yield()` from `Main` is resumed by:

| waker | values `Yield()` returns |
|---|---|
| task tick (`Animate`) | nothing (zero values, i.e. nil) |
| `OnMessage` for any posted message (state is 3 after the first wait) | `(message)` or `(message, commandFunction)`. When `msg` was null (a bare command), the message is a fresh type-8 (`kCommandOnly`) message named `""`. |
| injection (`RunFunction` while suspended and `nresults < 2`: `DoLuaString`, `FillWindow`, hotkey and rollover chunks) | `(nil, closure)`. The closure is **not run**, because only `WaitForMessage` runs it. RunFunction still reports success, so the injected work is silently lost. |
| `CloseRequested` | nothing |

What the C++ side does with yielded values: `HandleGetMessage` yields one value (the global `gYieldResult`), and a plain `coroutine.yield(a, b)` yields whatever it is given. `Resume` ignores all of it. `gYieldResult` is written by the prelude (`gYieldResult = command()` in `WaitForMessage`) and the C++ only ever *pushes* it as the yield value; nothing reads it. The only string reference to it in the binary is in `HandleGetMessage`. So it is effectively write-only state.

Consequence for the new engine: a Lua 5.0 state with `lua_resume`/`lua_yield` on the same state, resumed with `(message[, fn])` from `OnMessage`, zero values from the tick, and `(nil, closure)` from injection, reproduces all of this. If you keep `Yield()` it should behave exactly like a one-shot `WaitForMessage` that does not run commands. Since nothing uses it, leaving it as the stock `coroutine.yield` is faithful.

Unknown: whether the platform timer coalesces ticks when the delay is 0 ("every tick" is an inference from `DoTask`).

---

## 2. `toparams`, and what `_ALERT` does

Both are registered by `TScript::LuaInit` (0x79e50), which opens only the base, string and table libraries and then registers `_ALERT`, `DebugOut`, `toparams`, and compiles the `DumpTable` prelude.

**`toparams(t)`** is `ParamsFromTable` (0x7b300). It wraps its table argument in a `TLuaTable` (which stores it in the registry), reads `n = lua_getn(t)` (Lua 5.0's array length: the `n` field if present, else the border), pushes `t[1]`, `t[2]`, ... `t[n]` in order (`TLuaTable::PushValue(i)`), removes the registry slot, and returns `n`. In other words it is `unpack(t)`. Nothing in the shipped Lua or Reforged calls it (the strings only appear at the registration), so a plain `unpack` is a correct implementation.

**`_ALERT(msg)`** is `LuaError` (0x79bf2). It takes the string at the top of the stack and calls `WriteDbg("Lua error: %s", msg)` (format string at 0x15c0d4), which formats with `vasprintf` and writes the line plus a newline to `std::cerr`. It returns 0 values. It is used as the `errfunc` (stack slot 1) of the engine's own `lua_pcall`s: `RunFunction`, `PopupWindow`/`OnMessage` command calls, `TLuaFunction::Call` (transition `onend`). Because it returns nothing, the error value that `pcall` hands back is nil **(inference from Lua 5.0 errfunc semantics, which adjust the handler's results to one value; not checked against this binary's `luaD` code)**. `RunFunction`'s own `lua_resume` of `Main` logs `"lua_resume error: %s"` the same way when it fails.

Related: `DebugOut` (`LuaDebugOut`, 0x79c2c) is a 5-byte function that does nothing, and `TScript::DoLuaString` (0x7bcac) logs `"Lua Error: %s\n\twhen %s"` and calls a global `LuaErrorMessage` if one is defined.

---

## 3. Loose files versus `.pfp` entries

**Loose files win.** Evidence: `TFile::ScanForResources` (0x536ac) calls `ScanFolderForResources("assets", "")` (0x52db0). That function, before it lists a folder, calls `TFlatFile::InitFlatFiles(folder, map)` (`0xa69c6`), and only then walks the directory and inserts loose files.

Order of operations, all into one `std::map<str,str>` (lowercase key to value):

1. `InitFlatFiles(folder)`: lists the folder, keeps names containing `.pfp`, sorts them (plain `str` ordering), and for each calls `ReadFlatFile` (0xa6540). `ReadFlatFile` opens `"::assets/" + name` (so the archive is always read from `assets/`, regardless of the folder being scanned), checks the 4-byte magic `PFPK`, appends the archive's path to a global vector (its index in that vector becomes the archive number), reads a 4-byte count, then for each entry: 1-byte name length, name, 4-byte offset, 4-byte size. The entry's key is the lowercased name; its value is the string `@<archiveIndex>:<offset>,<size>` (`@%d:%ld,%ld`). Later archives (by sort order) and later duplicates overwrite earlier ones.
2. Then each loose file `f` is inserted with key `prefix + lowercase(name)` and value the real path under `assets/`, **overwriting** any existing entry with that key. Directories are recursed into, skipping `.svn`.
3. `ScanFolderForResources` skips any loose file whose key is in the mask set read from `final.txt` (or `firstpeek.txt` when the first-peek flag at the binary's flag byte is set) in the working directory (files over 100000 bytes are ignored). Tokens are split on space, tab, CR, LF. The token `-*` clears the set; other tokens are lowercased and inserted. The mask only suppresses loose files; it has no effect on `.pfp` entries.
4. After the `assets` pass, `user:` and `common:` roots are scanned the same way with the prefixes `user:` and `common:`, unless `TPlatform::GetConfig("activex", "")` equals `"1"`.

Opening (`TFile::Open` 0x5456e): the name goes through `TranslateResource`; a result starting `@` is opened as a `TFlatFile` (it opens the archive file, parses `index:offset,size` back out of the string, and seeks to the offset), `&` is an in-memory file, anything else is a plain file.

Edge cases:

* A path that is not in the map is "not found" even if it exists on disk, except the `::path` form and the `user:`/`common:`/`desktop:` prefixes (these are translated with an `access()` check). The map is built once at startup (and again after the folder globals are set).
* `TranslateResource` lowercases the name first, then does the map lookup, then `::`, then (only if the existence flag is true and the name is in the mask set) returns not found, then `user:`/`common:`/`desktop:`, then strips a leading `assets/` or `assets\` and retries.
* The key for a loose file is the path relative to `assets/` with `/` separators (the exact string building in the recursion is **inference** from how the Lua names resolve, for example `ui/mainloop.lua`).

For the new engine: build one case-folded map, load `.pfp` entries first (sorted by archive name), then loose files over them. Do not fall back to the disk for names that were not in the scan.

---

## 4. `CurrentTime` and `GetTimer`: units and origin

Both call the same function. `GetTimer` is bound with the `TPlatform` instance as the object and the member `TPlatform::Timer` (0x59ce6); `CurrentTime` (0x4338) calls `TPlatform::GetInstance()->Timer()`. `TPlatform::Timer` forwards to `TPlatformData::Timer` (0x8d3d0).

`TPlatformData::Timer` reads `gettimeofday`, subtracts the `timeval` saved at `TPlatformData+0x9c4` (written once by the `TPlatformData` constructor at 0x8df88, so the origin is **process start**, early in platform init), multiplies the difference in seconds by **1000.0**, clamps to `[0.0, 4294967295.0]`, and converts to `uint32` (with the usual 2^31 split for the unsigned conversion; fraction truncated).

So the unit is **milliseconds since process start**, as an unsigned 32-bit value that saturates at 4294967295 (about 49.7 days) rather than wrapping. The constants were read from the binary (1e-6, 1000.0, 2147483648.0, 4294967295.0). Reforged's `Player:AutoSave` uses `CurrentTime()` against `4 * 60 * 1000`, consistent with milliseconds; `cloud.lua` multiplies by 1000 to build a stamp.

The `TScript` task and transitions use the same platform clock. Dialogs (modals) own separate `TClock`s that pause while covered; those do not affect `GetTimer`.

---

## 5. How `TLayeredWindow` assigns layers; `SelectLayer` and `kAllLayers`

`TLayeredWindow` (derives from `TWindow`, constructor 0x4c9be) has: `+0x30` current layer (initially 0), `+0x34` layer count (the constructor argument), `+0x38` an array of that many `std::list<TWindow*>`. Only `TButton` uses it, with a count of **4** (`TLayeredWindow::TLayeredWindow(4)` in `TButton`'s constructor). Layer numbers are 0 up, `-1` is `kAllLayers`.

* `AdoptChild(child, callInit)` (0x4cace): calls `TWindow::AdoptChild`; if that fails, returns false. Otherwise: if the current layer is `-1`, it appends the child to **every** layer's list; otherwise it appends it to the list of the current layer only. The child is still an ordinary child in the tree's child list. There is no bounds check on the current layer (an out-of-range `SelectLayer` would index past the array; the Lua never does that).
* `OrphanChild(child)` (0x4c754): calls `TWindow::OrphanChild`, then removes the first occurrence of the child from each layer list.
* `SetCurrentLayer(n)` (0x4c7ca): **returns immediately if `n` equals the current value.** Otherwise it stores `n`, then (with `n == -1` replaced by 0 for the next step) runs a recursive `TWindowClearFlags` spider (0x178660) over all children that clears the visible bit `0x100` on each child and every descendant, then sets bit `0x100` on every window in the list for layer `n`, and invalidates the screen. Consequences: showing a layer shows exactly the windows adopted under that layer or under `-1`; `SetCurrentLayer(-1)` hides everything and then shows what is in layer 0's list (which, because `-1` adoption adds to all lists, includes the all-layers children, but not windows adopted only under layers 1 to 3).
* `TWindowScript::SelectLayer(n)` (0xa08d0), behind the Lua native `DoSelectLayer`: takes the window on top of the script's window stack; if it is `IsKindOf(TLayeredWindow)` it calls `SetCurrentLayer(n)`; otherwise it does nothing.

How a button gets its layers (`TButton::Init`, 0x941b8): after reading its style it reads the `graphics` table; for index `i` from 0 (pushing buttons, `type == 0`: indices 0 to 2, three images; toggles and radios: 0 to 3, four images), it reads `graphics[i+1]` as a string; if non-empty it builds a `TImage` named `image<i>` and calls `SetCurrentLayer(i)` then `AdoptChild(image, true)`, so each state image lives on its own layer. After the loop it calls `FitToChildren`, reads `command`, and finally calls `SetCurrentLayer(-1)`. Children created later by the Lua `Button` constructor (label `Text` windows) are adopted under whatever layer the DSL selected with `SelectLayer(...)`: `kAllLayers` first, then 0, 1, 2, 3 as the `style.lua` snippets show.

`TButton::PostChildrenInit` (0x93212) runs the base and then calls the virtual `SetState(0)` (vtable +0x7c, 0x92e80), which stores the mouse state in `+0x3c` bits 4 to 6 and calls `StateUpdate(newState, oldState)` (vtable +0x78, 0x93fe8). Because the current layer is `-1` at that point, the call to `SetCurrentLayer` is not skipped: everything is hidden, then the initial layer is shown.

`StateUpdate` layer arithmetic (read from 0x93fe8). The mouse state is 0 normal, 1 down, 2 over, 3 pressed-commit; `on` is bit 0 of `+0x3d`:

| button type (`+0x3c & 0xf`) | state 0 | state 1 (down) | state 2 (over) | state 3 |
|---|---|---|---|---|
| 0 push | layer 0 | layer 1 | layer 2 | unchanged |
| 1 toggle or 2 radio | layer `on` (0 off, 1 on) | layer `!on` (1 off, 0 on) | layer 2 if off, 3 if on | unchanged |

So the four `graphics` for a toggle or radio are: [1] normal-off, [2] normal-on (also pressed-while-on), [3] over-off, [4] over-on; pressing a button that is off shows layer 1 (the "on" image) while the mouse is down. The click sound (style `sound`/`rolloversound`, played through `TSound::Play`) is triggered in `StateUpdate` when the new state differs from the old, is not 1 and the old state is not 3, and the button and its parents are visible.

Edge case to keep: because `SetCurrentLayer(n)` is a no-op when `n` is already current, a script that ends a build with `SelectLayer(k)` and then sees an initial state that selects `k` would leave the other layers visible. The shipped button constructors end on `kAllLayers`, so it does not arise.

---

## 6. `TWindowSpider` bodies: `FitToChildren` and `DestroyAllChildren`

`TWindow::ForEachChild(spider, reverse)` (0x4a24c) walks the child list from the front (the frontmost window first) when `reverse` is false, saving the next pointer before the call so the callback may unlink the current child, and stops early if `spider->Process(child)` (vtable slot +8) returns false. The spiders here have no named symbols; I found them through their vtables and the index names `GetLargest` and `ChildrenDelete`.

**`FitToChildren`** (0x4aa2c) builds a `GetLargest` spider (0x1784d8) with two fields initialised to the window's **current width and height**, runs it over the direct children, and calls `SetWindowSize(maxW, maxH)`. `GetLargest::Process` raises `maxW` to `child.right` (rect +0x18) and `maxH` to `child.bottom` (rect +0x1c) when they are bigger, and returns true. So:

* it only grows, never shrinks;
* it measures each child's right and bottom edge in the parent's coordinates, not the child's size and not its left/top, so a child at x = 100 with width 50 asks for 150;
* it considers hidden children too (no visibility test), does not look at grandchildren, and adds no padding or margin;
* it does not move any child.

Callers (`TWindow::PostChildrenInit` when `fit` is true, `TModalWindow::PostChildrenInit` always, `TButton::Init`, `Rollover::PostChildrenInit`, `PopupWindow::SetContents`, `FitToChildren()` from Lua) are unchanged from `docs/re/window-system.md`.

**`DestroyAllChildren`** (0x4a4cc) builds a `ChildrenDelete` spider (0x1784a0) holding the window and the `TWindowManager`, and runs it. `ChildrenDelete::Process(child)` does two things: calls the window's `OrphanChild(child)` (vtable slot +0x28; for a layered window this also removes the child from the layer lists), then `TWindowManager::SafeDestroyWindow(mgr, child)`, and returns true. So the children are **unlinked immediately** (they disappear from the tree, from `GetChildWindow`, and from hit testing, and the screen is invalidated), but the C++ objects are only deleted later, at the next `ClearDestroyedWindows` (deferred, same list). `SafeDestroyWindow` also clears the hover pointer, nulls the target of every queued message aimed at the window or its subtree, and removes it from the mouse listeners. Grandchildren die with their parent's destructor.

**The pass after `_PopModal`**: `_PopModal` (0x4e9fc) ends by running `TModalPoppedSpider` (0x1787bc) over the popped modal. For each direct child it first recurses (the same spider over the child's children), then calls the child's `OnParentModalPopped()` (vtable slot +0x2c). `TWindow::OnParentModalPopped` (0x164d48) is a 5-byte no-op and no other class in the index overrides it, so **the pass has no effect in this binary** **(inference: based on the symbol index; an override without a symbol would not show)**. It can be omitted.

---

## 7. Key sets read by each window type

The lookup is `TWindowStyle::Get*` in `Init` or `PostChildrenInit` (the style chain is the table, then `gCurrentDefaultStyle` and its `parent`s, see `docs/re/window-system.md` 2.2). Every type below also gets the **base keys** from `TWindow::Init` (0x4c616) and `TWindow::PostChildrenInit` (0x4c594):

* `name` (string, `""`), `x`, `y` (number, 0), `w`, `h` (number, default current width/height), `align` (number, default **-1**, see section 12), and in `PostChildrenInit`, `fit` (bool, false).

Defaults are in parentheses. "Number" returns a double that the code truncates to int where used. Types listed with "(no own keys)" add nothing.

### Button (`TButton::Init`, 0x941b8; the Lua `Button` in `style.lua` wraps it)
`sound` (""), `rolloversound` (""), `mask` (""; texture name for the click mask), `type` (0; low nibble: 0 push, 1 toggle, 2 radio), `scale` (1.0; image scale), `sendToParent` (false), `beginGroup` (false; sets window flag 0x8), `close` (false), `default` (false), `cancel` (false), `on` (false; initial toggle state), `tooltip` (read, `""`, apparently unused: the string is read and discarded **(inference)**), `graphics` (table of image names, 1-based, see section 5), `hflip`, `vflip`, `rotate` (bools, applied to every state image), `command` (function; becomes the `LuaAction`). The Lua constructor adds `label`, `textfactory` and similar keys that only `style.lua` reads.

### Slider (`TSlider::Init`, 0x89b7a)
After `TWindow::Init` (and only the first time, a one-shot flag at `+0x34`): `w`, `h` (number, 0; used only for orientation), `hslider` (number default -1, read first; if it is below 0 the orientation is horizontal iff `h < w`, otherwise `hslider` is re-read as a bool), `railtop`, `railmid`, `railbot` (strings, `""`; rail images), `sliderimage`, `sliderrollimage` (strings, `""`), `name` (string, `""`; sets the window name), `scale` (1.0), `yoffset` (0), `value` (0; calls `SetValue(value, true)`). No min, max or step keys exist in `Init`. The shipped uses are `Slider { x, y, w, name }`.

### TextEdit (registered as `TCustomTextEdit`, then `TTextEdit`, then `TText`)
`Main` (0x2952c) calls `AddWindowType("TextEdit", TCustomTextEdit::ClassId())` right before `ChocoThreeMain`, which **replaces** the `TTextEdit` registration made by the `TWindowScript` constructor. (Order is an inference: `TWindowManager::AddWindowType` dereferences the manager's script pointer, so the script, whose constructor registers `TTextEdit`, must already exist when `Main` makes the call.) So the Lua `TextEdit` is `TCustomTextEdit` (0x205ae):

* `TCustomTextEdit::Init`: `clearinitial` (bool, false; stored inverted), `enablewindow` (string, `""`; name of a window enabled only when the field has text), `onkey` (function), `widthlimited` (bool, false).
* then `TTextEdit::Init` (0x8c31c): `password` (bool, false), `length` (number, **10**), `ignore` (string, `""`; characters to reject), `utf8` (bool, false).
* then `TText::Init` (0x55550): `font` (table: [1] font name, default `__DEFAULT`; [2] size, default 16, and 12 if that is 0; [3] colour, default opaque white), `name`, `label` (string, `""`; a leading `#` means literal, otherwise looked up in the string table unless it is all digits and dots), `defflags` (0), `flags` (default `defflags`), `padding` (0), `rotation` (0), `rotationOriginX`, `rotationOriginY`, `rotationAlign` (0). A size of 0 by 0 logs "ERROR: Attempting to create text window of size(0,0)" and creates no text.

TextEdit reads `clearinitial` and `onkey`: the market screen's `TextEdit { ..., clearinitial=true, onkey=Update }` is read.

### Rectangle (`TRectangle::PostChildrenInit`, 0x3c45c; no `Init` override)
`color` (colour; default the window's current colour, which the constructor sets to opaque white (1,1,1,1)), `alpha` (number; default the colour's alpha component). Then the four corner vertices are rebuilt from the final size. The constructor also registers the global `SetRectangleColor(windowName, colour)` once.

### BSGWindow (`TBSGWindow::PostChildrenInit`, 0x6516; no `Init` override)
`swallowmouse` (bool, false; note the key is all lowercase while the field is `swallowMouse`), `frame` (string, `""`; `.xml` is appended if missing, loaded as an animated texture), `color` (colour; default (1,1,1,0), alpha 0 so the fill is invisible by default), `fill` (bool; default **true**, the constructor value). If the colour's alpha is within 0 and 1 it calls `SetAlpha(alpha, true)`. `Character` and `PopupWindow` derive from this.

### BitmapTint (`TBitmapTint::Init`, 0x3bafa)
Calls `TImage::Init` first (0x866e2), whose keys are `image` (""), `mask` (""), `scale` (1.0), `alpha` (bool), `mipmap` (bool), `hflip`, `vflip`, `rotate` (bools). Then its own: `image`, `mask`, `scale`, `alpha` (bool; texture flag 2), `mipmap` (number, non-zero sets texture flag 1), `tint` (table of four numbers [1]..[4], each default 1.0, so `tint = Color(r,g,b,a)`), `dx`, `dy` (number, default **-9999**), `hflip` (bool), `w`, `h` (number, default -1; if both are above 0 the window is resized to them). When both `mask` and `image` are given it loads a merged texture; if only `image`, a plain texture.

### TightText (`TTightText::Init` 0x38ca8, `PostChildrenInit` 0x38d38)
`Init` runs `TText::Init` (all Text keys above), then forces the window size to **800 by 600**, then reads `pad` (number, default **3**). `PostChildrenInit` runs the base, measures the text bounds, and sets the window size to `(boundsWidth + pad, boundsHeight)`. The Lua `TightText` constructor in `style.lua` sets `w = 800, h = 600` itself, which is what lets the text be created at all (a zero-size text window is refused).

### Rollover (`Rollover::Init`, 0x34338; `PostChildrenInit` 0x341ca)
Base keys, then `xOffset`, `yOffset` (number, 0), `xAbsolute`, `yAbsolute` (number, **-1**), `contents` (string, `""`), `command` (function). `PostChildrenInit` runs the base then `FitToChildren`. (`fit=false` in the Lua is read by the base, but the Rollover then fits anyway, which only grows it.)

### CharWindow (`Character::PostChildrenInit`, 0x78ba; derives from `TBSGWindow`)
BSGWindow's keys, then `name` (character name, also the window name since the base reads it), `alpha` (1.0), `scale` (1.0), `happiness` (number, default **50**). It loads `characters/<name>.xml` as an animated texture, else `characters/<name>.png`, else `characters/temp_char`; grows the window to the scaled image size if that is bigger; then `SetMood(happiness)`.

### XSellWindow (`TXSellWindow`, registered by `TWindowManager::AttachWindowScript`, 0x4e480)
No `Init` or `PostChildrenInit` override: only the base keys. The constructor (0x9eafe) registers two Lua globals on every construction, `SetupXSell` and `LaunchGameLink`. `SetupXSell` (0x9ed48) reads `xsell/xsell.xml` (a `xsell` node with `gameinfo` children, `sitelink`, `centertext`; attribute `linktext`) and fills or hides the child windows named `descrip1..8`, `xsellbutton1..7`, `gamesite`, `centertext1`, `centertext2` that the Lua dialog built. No shipped Lua builds an `XSellWindow` (grep finds none), and `CheckConfig("xsell")` is true only if `xsell/xsell.lua` exists, so a port can leave it unimplemented.

Callback-function keys across all types: `command` (Button, Rollover, Anim), `onkey` (TextEdit), `onstopped`, `onpayout` (SlotMachine), `onend` (transitions). See section 11.

---

## 8. The name of the `kCommandOnly` message

It is a **prefix**, not a suffix. `TButton::LuaAction::DoAction` (0x93400) builds the type-8 message with name `"LuaAction:" + buttonName` (the string `LuaAction:` is at 0x15c90c; the disassembly passes the literal as the left operand and the button name as the right). Example: a button named `ok` posts a message named `LuaAction:ok`. It then defines `function GetButtonName() return [[<buttonName>]]; end;` in the script state, and calls `OnScriptMessage(msg, commandFunction)`. The Lua only reads `.type` and `.name`, and no shipped Lua compares the name, so the exact string is cosmetic. The message created by `OnMessage` for a null message has the empty name.

---

## 9. The config key behind `kPressAnyKey`

The gate is not in `DisplaySplash` (which only has the Lua-level `disableAbort` tag). It is in C++, `TWindow::HandleEvent` (0x4b578), in the branch for a window with no further parent (the modal at the end of the chain). It calls `TPlatform::GetConfig(key, "")` and `to_int`; **if the value is 0 (including unset), a `kPressAnyKey` message is posted**. The key is read through a global `char*` variable at 0x1d8318, which points at the string **`overlaymouse`** (the neighbours in that table are `verbosedebug`, `automergemask`, `utf8mode`, `textspriterender`...: a block of config key names).

Posting rules, same function:

* event type 7 (mouse up): message type 4 (`kPressAnyKey`) with name **`mouse:up`**;
* event type 11 (key down): name **`raw:` followed by the character** (the byte at event +4);
* any other unhandled event: no message.

The config map (`TPlatform::GetConfig`, 0x5a1f0) is a `std::map<str,str>` filled by `SetConfig` (0x5a5fc) from the start-up code in `PlayFirstExec`; `GetConfig` returns the stored string or the default when the key is absent. `IsEnabled` is `to_int(GetConfig(key, "")) != 0`. So for the port: leave `overlaymouse` unset (0) and unhandled mouse-up and key-down events reaching the modal post `kPressAnyKey`, which is how the splash and the "press any key" Lua see them.

Unknown: what `overlaymouse` means when set, and which command-line or preference route sets it.

---

## 10. How `Rollover` runs `contents`

`contents` is a **string of Lua source**, not a function, run fresh on every hover. (`command` is a function; `contents` is not.)

1. `Rollover::OnMouseMove` (0x345b4) acts only if the static `rolloversEnabled` is true (initial value and the setter `EnableRollovers` were not located; the Lua never calls it). It builds a `TPopupTestRect` from the Rollover's screen rectangle and a new `PopupWindow` (0x70 bytes; a `TBSGWindow` subclass), adopts it into the **top modal** with `AdoptChild(popup, callInit=false)`, then calls `PopupWindow::SetContents(contentString)` (0x2e7f2).
2. `SetContents` calls `TWindowManager::DoLuaString(popup, str)` (0x4cc36): `PushWindow(popup)` (so the popup is the DSL's current window), `TScript::DoLuaString(str, -1, 0)`, `PopWindow()`.
3. `TScript::DoLuaString` (0x7bcac) first runs the Lua string `LUA_PATH='<stored path>' ;` (it re-applies the script's `LUA_PATH` global), then `luaL_loadbuffer(str)` with the string itself as chunk name, then `RunFunction(0, 1)`. Per section 1, since the mouse event arrives while `Main` is suspended in a wait, `RunFunction` takes the **injection** path: it wraps the chunk with `GetClosure` and resumes the coroutine with `(nil, closure)`, so `WaitForMessage` runs it as a command inside `Main`. If the coroutine is not suspended (it has not started or has finished) it is a plain protected call.
4. Back in `SetContents`: `FitToChildren()`, `SetAlpha(0, true)`, then a `FadeInTransition(popup, 1.0, 125 ms, no onend)` (0x7d = 125). So the popup fades in over 125 ms.
5. Back in `OnMouseMove`: the popup is positioned with `Reposition(x, y)`. The anchor is the mouse point converted to screen coordinates plus (`xOffset`, `yOffset`), unless `xAbsolute`/`yAbsolute` is >= 0, in which case that coordinate is used as is. `Reposition` (0x2e5a4) then keeps the popup inside the screen window minus a 4 pixel margin (if it would overflow to the right it is moved left by the overflow and pushed down 32 pixels; if it would overflow the bottom it is moved up). The popup gets the Rollover's click function (shared pointer) or, when there is none, the `clickString`, and is added as a mouse listener.
6. The popup stays while `TPopupTestRect::KeepVisible` (the mouse inside the Rollover's rectangle) holds; `PopupWindow::OnMouseMove` kills it when the test fails. `OnMouseUp` kills it and runs the click function or evaluates `clickString` through `DoLuaString`. `Kill` removes the mouse listener, orphans the popup, hides it and calls `SafeDestroyWindow`. `OnMouseDown` swallows the click.

What the chunk looks like in the shipped Lua: `contents = ing.name .. ":MarketRolloverContents()"`, that is, a statement calling a method on a global ingredient object, which ends in `return MakeDialog { BSGWindow { ... Group(text) } }`, building its windows into the popup because the popup is the stack-top window. For the port: evaluate it with `luaL_loadbuffer` and run it on the main state's coroutine through the same injection path used for commands, with the popup pushed on the script's window stack. A plain `pcall` from the host would also work while `Main` is suspended in a wait, but would differ if the script were mid-call; injection is what the original does.

---

## 11. Does any window type have a Lua draw or update callback?

**No.** The complete list of function-typed style keys (`TWindowStyle::GetFunction` call sites in the index): `command` (`TButton::Init`, `Rollover::Init`, and the `TAnimatedSprite` init), `onkey` (`TCustomTextEdit::Init`), `onstopped` and `onpayout` (`SlotMachine`), and the `onend` of transitions (`BSGTransition`, a `TLuaFunction` run by a bare `pcall`). There is no key or string for "draw", "update", "onupdate" or "ondraw" anywhere in the binary's strings. All window drawing is C++ (`Draw`/`PostDraw`).

`FillRect(rect, color)` (0xa0c44) is a plain native that the Lua may call; it is not restricted to a draw callback: there is no Lua draw callback, so it always runs outside any draw. The new engine should treat it as a call into the renderer with the current window's clip, and since the original draws every frame from C++, a Lua-driven `FillRect` only makes sense on a window the C++ keeps redrawing. I did not find a caller in the shipped Lua.

The particle scripts (`particles.lua`, `Update`/`Anim`) and `animationscript.lua` do have Lua per-frame functions, but they belong to sprite and particle objects, not window types.

---

## 12. `FixSize`: the combinations not traced before

`TWindow::FixSize` (0x4a652) from the disassembly. Signature as called: `FixSize(this, int* pos, int* size, uint (TWindow::*getDim)() , bool A, bool B, bool C)`. The getter is called on `this->parent` (width for the horizontal pass, height for the vertical pass; if there is no parent, nothing is resolved after step 1).

How `SizeAndPositionFromStyle` (0x4bff4) calls it:

* `align` is read with **default -1** (the default argument is the double -1.0). `-1` is treated as 0 for the bit tests.
* `C = (align != -1)`: **negative positions are wrapped from the far edge only when `align` is absent** (or -1). If `align` is given at all, even `0`, a negative `x` stays negative. `TWindowScript::SetPosition` passes A = B = C = false, so it wraps negatives and applies no alignment.
* Horizontal pass: `A = align & 1` (**kHAlignCenter**), `B = (align >> 1) & 1` (**kHAlignRight**). Vertical pass: `A = (align >> 2) & 1` (**kVAlignCenter**), `B = (align >> 3) & 1` (**kVAlignBottom**). So the "second flag" is the right/bottom flag, and "bit 2" is the value `kVAlignCenter` = 4. `kHAlignLeft`, `kVAlignTop` are 0. The other direction's bits do not leak across.

Constants: centre band is `40000 < P <= 106666`; "far band" is `P > 106666`; `160000` is `kMax`, `80000` is `kCenter`; the size thresholds are `S > 80000`, `0 <= S <= 80000`, `S < 0`. D is the parent dimension.

**Step 1 (alignment, before anything else).** If P is outside the centre band (that is, `P < 40000` including negatives, or `P > 106666`): if A is set and `S == 0`, return with nothing changed; if A, `P = P - S/2` (C integer division, towards zero); else if B and `S == 0`, return; if B, `P = P - S`. So `x = 100, align = kHAlignCenter` centres the window on x = 100, and `align = kHAlignRight` puts the right edge at x = 100. With the far-band form it works on the raw encoded value (for example `x = kMax - 10` with right alignment gives a right edge 10 pixels in from the right).

**Step 2.** If there is no parent, stop. Otherwise D = the parent's width or height.

**Step 3, by size S:**

* `0 <= S <= 80000` (literal size):
  * `P > 106666`: `P = D + P - 160000`.
  * centre band: `P = P - 80000 + (D/2 - S/2)`.
  * `P <= 40000`: if C is false and `P < 0`: `P = D + P`; otherwise unchanged.
* `S > 80000` (fill; `w = kMax - n` encodes `S = 160000 - n`):
  * `P <= 40000` (literal): if C is false and `P < 0`: `P = D + P`; then `S = D + S - P - 160000`. (So `w = kMax` fills from x to the far edge; `w = kMax - 40` stops 40 pixels short.)
  * centre band: `P = P - 80000`, then the **tail** below.
  * far band: `P = D + P - 160000`, then the tail.
  * Tail: if `P < 0`: `S = D + S + 2P - 160000`, `P = 0`; else `S = D + S - 2P - 160000`, `P = 2P`. For `x = kCenter`, `w = kMax` this gives `P = 0`, `S = D` (fills the parent). For `x = kCenter + n` it gives `P = 2n`, `S = D - 2n` (so the right edge is at D). The far-band tail result is odd (`P = 2*(D + P - 160000)`); this looks like an unintended path and no shipped Lua combines `x = kMax - n` with a fill width. **(inference: faithful to the code, but meaning unclear)**
* `-80000 <= S < 0` (negative size): `S` becomes `-S` (call it n), then P is resolved exactly as in the literal-size case with that n (including the negative wrap rule and the centre formula), and finally `P = P - n`. So a negative size means "the given position is the far edge". The shipped Lua has no negative `w` or `h` literal (grep), so this path is exercised only by computed values.
* `S < -80000`: `S` becomes `-S`; for `P <= 40000` (with the negative wrap rule first) it sets `S = P` and `P = 160000 - P`; for the other bands it resolves P as in the fill case and takes the tail. This is degenerate and I would not implement it beyond not crashing.

**After both passes** (`SizeAndPositionFromStyle`): these rules matter for ordering and were only partly noted before:

* The existing rectangle left/top, when non-zero, **replaces** the computed x/y, so a window is only moved if its old left (or top) is 0. The rectangle is translated by (x - left, y - top).
* If the window's current width is non-zero, the computed width is **discarded** and the current width is kept (same for height). The computed size is only applied while the rectangle is still empty, which is the case in `Init` (the first call) but not in the second call from `PostChildrenInit`. So the second pass cannot change a non-zero size: `fit` followed by explicit `w`/`h` does **not** let the explicit size win. Net effect for `fit=true` with explicit `w`: the window ends at the larger of the explicit size and the children's extent (because `FitToChildren` only grows).
* Early outs before `FixSize`: with a parent whose width (or height) is 0, any relative width/x value (`w < 0`, `x < 0`, `w > 80000`, `x > 40000`) skips the whole function; and `x > 40000` with `w == 0`, or `y > 40000` with `h == 0`, returns. The `40000` compare there is **unsigned** (`cmpl $0x9c40; ja` at 0x4c28a), so a negative `x` with `w == 0` (or negative `y` with `h == 0`) also returns before either axis is resolved.

---

---

## Unknown

* What `overlaymouse` does when it is non-zero, and how `PlayFirstExec` fills the config map (command line versus preferences).
* Whether `GetConfig` keys are case-folded on lookup.
* Whether the unwinding value of a failed `pcall` is really nil after `_ALERT` (Lua 5.0 errfunc result adjustment, not checked in this build).
* The exact string construction of resource-map keys in the recursion of `ScanFolderForResources` (the relative-path result is certain from how the Lua opens files; the code path is partly inferred). Whether sub-folder `.pfp` files are ever loaded (they would be read from `assets/` by name, so effectively only top-level ones).
* The far-band-with-fill and very negative size branches of `FixSize` (traced, but I could not find a user).
* `TButton`'s `tooltip` use (read and apparently discarded).
