# Window system: how the original engine works

Source of truth: the decompiled Mac i386 binary (`re/mac/decomp/`, binary `Contents/MacOS/chocolatier-decadence-design` in the Chocolatier 3 Mac app bundle), then the Lua. Every address below is a Ghidra/Mach-O VM address (file offset = VM address - 0x1000 in `__TEXT`).

Facts are stated plainly. Anything I inferred is marked **(inference)**. Anything I could not establish is in the final **Unknown** section.

Two things about method that change how you read this document:

1. **A large part of the window DSL is not in `.lua` files.** `TWindowScript::TWindowScript` (0xa2a66) loads a precompiled, big-endian Lua 5.0 chunk (`@pflib/src/core/ScreenInit.lua`, 15010 bytes at VM 0x1db7e0, file offset 0x1da7e0). It defines `DoWindow`, `MakeDialog`, `DoModal`, `DoMainWindow`, `DisplayDialog`, `DisplaySplash`, `WaitForMessage`, `Pause`, `SetStyle`, `AppendStyle`, `Group`, `SwapToModal`, `CloseWindow`, the `k*` constants and more. I disassembled it by hand; section 0 summarises it as Lua. **The new engine must reproduce this chunk's behaviour** (it can be re-written as Lua source and loaded the same way).
2. The game's Lua is **5.0.x** (`Lua 5.0.2` string; coroutines are `lua_resume`/`lua_yield` on a `lua_State`, no `coroutine.wrap` tricks needed). The bytecode in that chunk uses a non-stock opcode numbering (4 extra opcodes before `JMP`), so the stock 5.0.2 `luac` cannot load it. Re-write it as source rather than loading it.

## 0. The embedded `ScreenInit.lua` (reconstructed)

Reconstructed from the bytecode; names are exact, control flow is faithful, variable names are mine. Line numbers are from the chunk's own line table.

Constants set as globals (all numbers unless noted):

| name | value |
|---|---|
| `kHAlignLeft/Center/Right` | 0 / 1 / 2 |
| `kVAlignTop/Center/Bottom` | 0 / 4 / 8 |
| `kPush/kToggle/kRadio` | 0 / 1 / 2 |
| `kAllLayers` | -1 |
| `kCenter` | 80000 |
| `kMax` | 160000 |
| `kDefault` | 128 |
| `kPushButtonAlignment` | `kHAlignCenter + kVAlignCenter` (= 5) |
| `kRadioButtonAlignment` | `kHAlignLeft + kVAlignCenter` (= 4) |
| `kToggleButtonAlignment` | `kHAlignCenter + kVAlignCenter` (= 5) |
| message types | `kGeneric`=0, `kCloseWindow`=1, `kDefaultAction`=2, `kButtonPress`=3, `kPressAnyKey`=4, `kQuitNow`=5, `kModalClosed`=6, `kTextEditChanged`=7, `kCommandOnly`=8, `kSliderValChanged`=9, `kSliderMouseUp`=10, `kSliderPageUp`=11, `kSliderPageDown`=12 (each is previous + 1), `kUserMessageBase`=1000 |
| string keys | `kCmdLine`="cmdline", `kLevel`="level", `kSubLevel`="sublevel", `kPause`="pause", `kMute`="mute", `kBreakNow`="breaknow", `kScore`="score", `kGameMode`="gamemode", `kGameOver`="gameover", `kNoMoreContent`="nomorecontent", `kDownloadButton`="downloadbutton", `kUpsellFrequency`="upsellfrequency", `kRestartButton`="restartbutton", `kSystemMode`="systemmode", `kMaxLevel`="maxlevel", `kCheatMode`, `kComputerId`, `kInstallKey`, `kGameName`="gamename", `kGameVersion`="version", `kEncryptionKey`, `kHiscoreLocalOnly`="hiscorelocal", `kHiscoreAnonymous`="hiscoreanon" |

The numeric message types match C++: `TModalWindow::OnMessage` tests type 1, `DispatchWindowMessage` tests type 5, `TButton::DoCommand` creates types 1 and 3, `LuaAction::DoAction` creates type 8, the modal destructor creates type 6, `TModalWindow::OnChar` creates type 2, and the unhandled-input path creates type 4.

State globals: `idtable={}`, `gParent={}` (a stack), `gCurrentDefaultStyle={}`, `gCurID=100`, `gBeginGroup=0`, `gReturnValue=nil`; later `gClosedWindows={}`, `gCommandQueue={}`, `gMessageQueue={}`.

```lua
function Color(r,g,b,a)  -- 0-255 in, 0-1 out; alpha defaults to 1
  if a then return {r/255,g/255,b/255,a/255} else return {r/255,g/255,b/255,1} end end
function FColor(r,g,b,a) -- floats unchanged; alpha defaults to 1
function Push(w) table.insert(gParent,1,w) end     -- (unused by the window path; C++ keeps its own stack)
function Pop()   table.remove(gParent,1) end
function Creating(name,t) end                       -- empty hook

function DoWindow(t)
  t.beginGroup = gBeginGroup ; gBeginGroup = 0
  GenericCreate(t)        -- C++ TWindowScript::CreateCustomWindow; creates window, pushes it on the C++ window stack
  MakeDialog(t)           -- builds the children
  PopWindow()             -- C++ TWindowScript::PopWindow
end
function DoDialogCommand(i, cmd) cmd() end         -- table.foreachi callback
function MakeDialog(t)
  local style = gCurrentDefaultStyle
  if t.name and t.name ~= "" then SetWindowName(t.name) end
  table.foreachi(t, DoDialogCommand)                -- array part, in order; each entry is a closure
  typename = t.typename                             -- NB: global
  SetDefaultStyle(style)                            -- restore style scope
  if typename then PostCreate(t, typename) end      -- C++ PostChildCreate
end
function SetBeginGroup() gBeginGroup = 1 end
function BeginGroup() return function() SetBeginGroup() end end
function GetTag(t, key, useDefault)                 -- style lookup, see section 2
  if t[key] then return t[key] end
  if not useDefault... -> GetTag(gCurrentDefaultStyle, key, true)
  if t.parent then return GetTag(t.parent, key, true) end
  return nil
end
function SetDefaultStyle(s) gCurrentDefaultStyle = s end
function SetStyle(s)    return function() SetDefaultStyle(s) end end
function AppendStyle(s) return function() s.parent = gCurrentDefaultStyle; SetDefaultStyle(s) end end
function SetFocus(name) RealSetFocus(name) end
function SetDefault(...) return function() end end  -- no-op closure
function DistributeX(n) -- returns table d[i] = (i/(n+1)) * gDialogWidth , i = 1..n   (centres of n equal columns)
function DistributeY(n) -- same with gDialogHeight
function SelectLayer(layer) return function() DoSelectLayer(layer) end end
function Group(t)       return function() MakeDialog(t) end end
function FitToChildren() return function() DoFitToChildren() end end
function CustomCreator(t) end                       -- empty
function GetString(k,a,b,c,d,e) -- nil args become ""; return _GetString(k,a,b,c,d,e)
function SetBitmap(name,image,scale) scale = scale or 1; _SetBitmap(name,image,scale) end
function SetState(key,val)                          -- booleans become 1/0
  if type(val)=="boolean" then val = val and 1 or 0 end
  _SetState(tostring(key), tostring(val)) end
function RegisterHotkey(key,button) if key and button then _RegisterHotkey(key,button) end end
function ReadFile(f) return dofile("assets/"..f) end
function ModalReturn(v) gReturnValue = v end
function CloseWindow(v) PostMessage(CreateCloseMessage()); if v then ModalReturn(v) end end
function PopModal(name) DoPopModal(name) end
function Yield() return coroutine.yield() end
function FlushMessages()
  repeat local m,cmd = HandleGetMessage(1) until not m   -- NB: stops at the first nil message; see 5
  gCommandQueue = {} ; gMessageQueue = {}
end
```

(`GetTag` shape above is approximate; the C++ equivalent `TWindowStyle::_GetTag` in section 2 is exact.)

`DistributeX/Y` read `gDialogWidth/gDialogHeight`, which `TWindowScript::InitWindow` sets every time a window is created: `gDialogWidth = <w>; gDialogHeight = <h>;` with the new window's width and height (format string `"gDialogWidth = %d; gDialogHeight= %d ;"`). So they describe the most recently created window (which is the parent at the time its children are built, because the parent was created first and its children are built inside `MakeDialog`... but each child creation overwrites them; scripts call `DistributeX` before creating children).

The blocking calls, exactly:

```lua
function WaitForCloseMessage(name)        -- ScreenInit line 608
  while true do
    local m = WaitForMessage()            -- polls every tick, see 5
    if m ~= nil then
      gLastMessage = m
      if m.type == kModalClosed and m.name == name then return end
      if m.type == kModalClosed then gClosedWindows[m.name] = true end
    end
    if gClosedWindows[name] == true then gClosedWindows[name] = nil; return end
  end
end

function DoModal(file)                    -- line 643
  local name = PushModal(file)            -- C++: creates TModalWindow named <file>; returns "%8x" id string
  ReadFile(file)                          -- runs the screen script, which calls MakeDialog{...}
  DoFitToChildren()
  WaitForCloseMessage(name)
  local r = gReturnValue ; gReturnValue = nil
  return r
end

function DoMainWindow(file)               -- line 657
  PushModal(file)
  local depth = GetWindowDepth()          -- length of the modal chain after the push
  ReadFile(file) ; DoFitToChildren()
  while true do
    local m = WaitForMessage()
    if m ~= nil then
      gLastMessage = m
      if GetWindowDepth() < depth then return end   -- main window (or something below it) was popped
    end
  end
end

function DisplayDialog(t)                 -- line 1076; t[1] = script path, other keys are parameters
  local file = t[1]
  if not file or file == "" then DebugOut("DisplayDialog called with empty script; aborting"); return end
  local name = PushModal(file)
  gDialogTable = t                        -- scripts read parameters from this global
  ReadFile(file)
  WaitForCloseMessage(name)               -- NB: no DoFitToChildren here
  local r = gReturnValue ; gReturnValue = nil
  return r
end

function SwapToModal(file)                -- line 1060
  ClearModalChildren()                    -- C++: top modal's tasks and children destroyed, size 0x0, pos 0,0
  ReadFile(file)
  DoFitToChildren()
end
```

`DoMainWindow`'s own comment in `mainloop.lua` says it exits "only if there are NO windows pushed on the stack"; the real test is `GetWindowDepth() < depth`, i.e. the chain became shorter than right after the push. A `PopModal` followed by `PushModal` (what `SwapToModal` avoids) is not seen by the loop because depth is only compared when a message arrives and the push has already happened by then **(inference on timing)**.

## 1. The window tree

### 1.1 `TWindow` layout (`re/mac/decomp/TWindow.c`)

| offset | meaning |
|---|---|
| +0x00 | vtable |
| +0x04 | parent `TWindow*` (null for the root) |
| +0x08 | children: embedded `std::list<TWindow*>` header (`next` at +0x08, `prev` at +0x0c) |
| +0x10..0x1c | rect `left, top, right, bottom`, **in the parent's coordinate space** (width = right-left) |
| +0x20 | `str name` |
| +0x24 | flags (32-bit) |
| +0x28 | unique id (global counter, incremented in the constructor) |
| +0x2c | `TAnimTask*` for window animations (lazily created) |

Constructor: parent null, empty list, rect all zero, empty name, **flags = 0x100**, id = counter++.

Flags known from the code:

| flag | meaning |
|---|---|
| 0x001 | modal container. `FindParentModal` stops at the first ancestor with this bit **and** `IsKindOf(TModalWindow)`. Set in the `TModalWindow` constructor (flags \|= 3). |
| 0x002 | can take focus (set for modals; `HandleEvent` on a mouse press walks up to the nearest ancestor-or-self with this bit and focuses it) |
| 0x004 | per-draw-pass "layered" marker used by `DoDraw` mode 1 |
| 0x100 | **enabled == visible** (`kEnabled` in `BSGHelpers.h`; `BSGShowWindow`/`BSGHideWindow` set/clear it). There is no separate "enabled" vs "visible". |
| 0x400 | "already drawn into the render target this pass" (modes 1 and 2) |
| 0x800 | modal only: this modal has an opaque child exactly as large as the screen |

`SetFlags(f)` can only change bits 8-15: it stores `(f & 0xff00) | (old & 0xff)`. If bit 0x100 changed it calls `InvalidateScreen`. It then finds the nearest ancestor modal and calls `SetOpaqueChild(this, flagged)` where "flagged" means (bit 0x800 of this window is set **and** this window is exactly the same size as that modal) (see 1.5).

Lua `EnableWindow(name, b)` = clear/set 0x100 on the window found by name; clearing it on a button also calls its `OnMouseLeave` (vtable +0x4c) to reset the pressed/rollover state. Hidden windows are skipped for drawing, hit testing and for the key/mouse "visible ancestor" walks. Children of a hidden window are never drawn (draw recursion stops) and never hit.

### 1.2 Ownership and lifetime

* `AdoptChild(child, callInit)` (0x4bd2e): if the child is not already in the list it is **pushed on the front** of the list and its parent set. If `callInit`, it calls `child->vtable[+0x10]` (`OnNewParent`, returns bool); if that returns false the child is un-adopted, `SetWindowSize` re-run on it, and `AdoptChild` returns false. The caller keeps ownership of the pointer in that case. Verified in the disassembly: the new list node is hooked before the old first node.
* `OrphanChild(c)`: removes the list node, `InvalidateScreen`, and if the child lies under a modal calls that modal's `SetOpaqueChild(c,false)`. Sets `c->parent = null`. Does not delete.
* Windows are **owned by their parent** in practice: `~TWindow` (0x4ad86) calls `vtable[+0xc]` (deleting destructor) on every child, calls `TWindowManager::OnDestroyWindow(this)`, then frees the list. It also detaches its animation task (`task->window = null`, the task is not deleted here).
* `OnDestroyWindow(w)` (0x4d116): clears the manager's hover pointer (+0x2c), clears the manager focus pointer (+0x38) if it was `w`, removes `w` from the mouse-listener list, and clears `defaultFocus` (+0x34) on every modal in the chain that pointed at `w`.
* Deferred destruction: `DestroyWindow(name)` from Lua, and popping a modal, call `TWindowManager::SafeDestroyWindow(w)` (0x4e674): clears the hover pointer, **appends `w` to the destroy list** (+0x1c), nulls the target of every queued message whose target is `w` or lies under `w`, and removes `w` from the mouse listeners. The real `delete` happens in `ClearDestroyedWindows` (0x4edc2), which `TWindowManager::HandleEvent` runs at the start of every event except types 2 and 0x11. After it deletes, it sets the dirty flag.
* `DestroyAllChildren` (0x4a4cc) runs a `ChildrenDelete` spider (0x1784a0). For each child it calls the window's `OrphanChild` (vtable +0x28: unlinked from the child list at once, so it is gone from lookups and hit tests immediately; a `TLayeredWindow` also drops it from its layer lists) and then `SafeDestroyWindow(child)` (deferred delete via the destroy list). Confirmed from the spider body; see `design-questions.md` section 6.

### 1.3 Z-order

* The first element of a window's child list is the **frontmost**. Verified three ways: `AdoptChild` pushes front; `DoDraw` walks the list from the last element to the first (so the first is painted last = on top); `NextChildWindowFromPoint` walks first to last (so the first is hit first). Consequence for Lua: **a child written later in a DSL table is created later and so is on top of earlier siblings.**
* `SetWindowDepth(EDepth)` / `_SetWindowDepth(d, sibling)` (0x4bdec): `0` = move to the back (list end); `1` = move to the front; `2` = swap one place toward the front (no-op if already first); `3` = swap one place toward the back (no-op if last); any other value = reinsert just before `sibling` (or at the back if `sibling` is not in the list). Every case calls `InvalidateScreen`. Names of the enum values are not in the headers I have; they are not exposed to Lua (no `SetWindowDepth` in the registered global list).
* The **modal chain** is a z-order too: each pushed modal becomes a *child of the previous top modal* (see 3). Modals have flag 0x001 and `DoDraw` skips children carrying bit 0x001, so modals are not drawn as ordinary children; `DrawLayer` draws them (section 6).

### 1.4 Names and lookup

* The name is a plain `str` at +0x20. It is set from the style key `"name"` in `TWindow::Init(style)` (default empty), or by Lua `SetWindowName(name)` (which writes into the window at the top of the script's window stack), which `MakeDialog` calls when `t.name` is non-empty.
* `GetChildWindow(name, depth)` (0x4a10e): if `this->name == name` return `this`; otherwise, if `depth != 0`, recurse into each child **in list order (front to back)** with `depth-1`, returning the first hit; else null. The scripts always pass depth = -1 (so effectively unlimited, because `depth-1` never reaches 0). Pre-order, depth-first, and **a window matches itself first**, so if two windows share a name the one found first in that traversal wins.
* Each Lua call picks a different search root (important when names repeat across stacked dialogs):
  * `EnableWindow`, `SetPosition`, `Transition` (`window=`): start at the **top modal**; `EnableWindow` then walks up `parent` (the previous modals) if not found.
  * `SetFocus`/`RealSetFocus`, `DestroyWindow`, `SetWindowName`, `SelectLayer`, `FitToChildren`, `PostCreate`: operate on the **top of the script's window stack** (the window currently being built, or the new top modal; see 1.6).
  * `FillWindow(name, file)` (free function 0x4984): searches from the **screen** (`TWindowManager+0xc`, the `TScreen`), then `FillWindowPtr`: `DestroyAllChildren`, then runs the Lua file with that window as the stack-top context (`TWindowManager::RunScript`: `PushWindow`, `TScript::RunScript`, `PopWindow`). Missing name: silent no-op.

### 1.5 Coordinates, size and `FitToChildren`

* `ClientToParent` adds the rect origin. `GetWindowRect` is recursive: it returns the window's rect in **screen** coordinates by adding every ancestor's origin (the root's own rect is returned as is). `GetClientRect` returns (0,0,w,h). `ClientToScreen`/`ScreenToClient` add/subtract the screen-space origin.
* `SetWindowPos(x,y)` moves the rect keeping size, then `InvalidateScreen`. `SetWindowSize(w,h)` sets right/bottom = left+w, top+h, `InvalidateScreen`, then (see flag 0x800) re-evaluates opaque-child status with the enclosing modal: the child counts as "opaque full screen" iff its size equals the screen window's size and bit 0x800 of its own flags is set.
* `FitToChildren` (0x4aa2c): runs a `GetLargest` spider (0x1784d8) initialised with the window's *current* width and height; `Process(child)` raises the maximum width to `child.right` and the maximum height to `child.bottom` (the child's right and bottom edges in the parent's coordinates, hidden children included, no padding, grandchildren ignored), then `SetWindowSize(maxW, maxH)`. So it only grows and never moves a child (confirmed from the spider body). Lua reaches it three ways: `FitToChildren()` DSL entry (calls `DoFitToChildren` -> `TWindowScript::FitToChildren` on the stack-top window), `fit=true` in a style (see `PostChildrenInit` below), and the explicit `DoFitToChildren()` calls in `DoModal`/`DoMainWindow`/`SwapToModal`.
* `GetWindowDepth` (Lua): counts the length of the parent chain starting from the top modal (`n` for the n-th modal pushed; 0 when there is no modal). It is the *modal* depth, not general tree depth.

#### Position and size encodings from styles

`TWindow::SizeAndPositionFromStyle` (0x4bff4) reads style keys `x`, `y` (default 0), `w`, `h` (default = current width/height) and `align` (default 0; value -1 is treated as 0), then calls `FixSize` once for the horizontal pair and once for the vertical pair. `FixSize` (0x4a652) interprets magic values in the 160000 = `kMax` space. What I could establish from the decompile (the parent dimension `D` is the parent's width for x/w and height for y/h; nothing is resolved when there is no parent):

* **Position `P`**
  * `P < 0`: `P = D + P` (measured from the far edge), unless a "no negative wrap" flag is passed (the decompiled flag is `in_stack_00000020`; `SetPosition` passes false so negatives wrap).
  * `40000 < P <= 106666`: centred with offset: `P = P - 80000 + (D/2 - S/2)`. So `x = kCenter` (80000) centres, `x = kCenter + n` centres then shifts by `n`.
  * `P > 106666`: far-edge anchored: `P = D + P - 160000`, i.e. `x = kMax - n` places the left edge `n` pixels from the right edge of the parent.
  * Otherwise `P` is used as is.
* **Size `S`**
  * `0 <= S <= 80000`: literal size.
  * `S > 80000`: "fill": remaining space. With `P <= 40000` (a literal position): `S = D + S - P - 160000`, i.e. `w = kMax - n` is "from `x` to `n` pixels short of the far edge" (so `w=kMax` fills the rest of the parent from `x`). With `P` in the centre band or the far band the result goes through a tail that has now been traced: `P` is first resolved to a signed offset (`P - 80000` in the centre band, `D + P - 160000` in the far band), then if that is negative `S = D + S + 2P - 160000` and `P = 0`, else `S = D + S - 2P - 160000` and `P = 2P`. For `x = kCenter, w = kMax` this gives `P = 0, S = D`. Full table in `design-questions.md` section 12.
  * `-80000 <= S < 0`: the size is negated (n = -S), `P` is resolved as for a literal size of n, and then `P = P - n` (the given position is the far edge). `S < -80000` is a degenerate path. The shipped Lua has no negative literal `w`/`h`.
* **Early outs** in `SizeAndPositionFromStyle` (keep these, they matter for ordering): if the parent exists and has zero width, any relative x/w (values <0, >40000, or w >80000) is skipped entirely; same for height. If `x > 40000` and `w == 0` (or `y > 40000` and `h == 0`) it returns without resolving. After `FixSize`, a non-zero `rect.left`/`rect.top` that was already set before overrides the computed position (the code keeps the existing `rect.left` when non-zero).
* `align` (default **-1**, treated as 0 for the bit tests): the bits are exactly the `kHAlign*`/`kVAlign*` constants: 1 = `kHAlignCenter`, 2 = `kHAlignRight` (horizontal pass); 4 = `kVAlignCenter`, 8 = `kVAlignBottom` (vertical pass). `FixSize` applies them first, and only when the position is outside the centre band (so also for literal positions): centre subtracts `S/2` from `P`, right/bottom subtracts `S` (both return unchanged if `S == 0`). `FixSize`'s "no negative wrap" flag is `align != -1`: negative positions wrap from the far edge only when `align` is absent. `SetPosition` passes false for all three flags. After `FixSize`, a non-zero current width/height is kept instead of the computed one, so the computed size only takes effect while the rectangle is still empty (the first call, in `Init`).

`TWindow::PostChildrenInit(style)` runs after all children exist: if style `fit` is true it calls `FitToChildren`, **then** `SizeAndPositionFromStyle` again. The second pass keeps a non-zero current width and height (see the end of `FixSize` below), so it cannot make an explicit smaller `w/h` win over `fit`; the result is the larger of the explicit size and the children's extent. `TModalWindow::PostChildrenInit` always calls `FitToChildren` first, then the base. `TButton::PostChildrenInit` calls base then the virtual `SetState(0)`, which runs `StateUpdate` to pick the initial image layer.

### 1.6 The script's window stack

`TWindowScript` keeps a `std::vector<TWindow*>` (+0x2c begin, +0x30 end) used as the *current window context* for every creation/lookup call from Lua.

* `PushWindow(w)` appends. `PopWindow()` removes the last **unless it is the only element** (the stack never becomes empty).
* `NewModalWindow(w)`: **clears the whole vector and pushes `w`**. Called by `TWindowManager::PushModal` and `_PopModal` every time the top modal changes (with null after the last modal is popped, which leaves `[null]`).
* `InitWindow` (creation) pushes the new window; `DoWindow` pops it after the children are built.
* `TWindowManager::RunScript/DoLuaString(window, ...)` push the window around the call, so button commands, `FillWindow` and `AskQuit()` run with a defined context.

This means a Lua function run later from a button command (after the dialog was built) sees the *top modal* as its context, not the button.

## 2. `TWindowStyle`, the DSL, and window creation

### 2.1 What a `TWindowStyle` is

`TWindowStyle` (`TWindowStyle.c`) is a 8-byte wrapper: vtable + a `TLuaTable*` (which holds the Lua state and a registry reference). It has typed getters `GetNumber(key, default)`, `GetInt`, `GetString`, `GetBool`, `GetColor`, `GetFunction`, `GetTable`; each:

1. records the Lua stack top, pushes the table (looked up through the registry by the `TLuaTable`'s light-userdata key), calls `_GetTag(key, true)`;
2. if not found returns the default; otherwise reads the value on the TWindowScript's Lua state and restores the stack.

`GetBool` accepts booleans, numbers (non-zero is true) and nil; a string gives false and logs `"PopBool used on lua string %s"`; any other type logs `"PopBool used on a type that isn't a number, a boolean, or nil. (type: %d)"` and gives false.

### 2.2 Style inheritance: `_GetTag(key, isFirst)` (0x6f07c)

The lookup order is:

1. the table itself (`gettable`, so metatables apply);
2. if missing and `isFirst`: the global `gCurrentDefaultStyle` (a plain table), and *within it* `parent` is followed;
3. if missing and not `isFirst`: the table's `parent` field, recursively.

So: **a window's own keys, then the default style in force when the window's `MakeDialog`/`PostCreate` runs, then that style's `parent` chain.** The window table's *own* `parent` field is **not** followed on the first step; only `gCurrentDefaultStyle` and its parents are. (The Lua-side `GetTag(t,key,useDefault)` used by `style.lua` does the same dance but also follows `t.parent` for plain tables.) `style.lua` therefore declares styles like `C3ButtonStyle = { parent = DefaultStyle, ... }`.

When the lookup happens matters. Window creation reads its style tags at *two* times: `Init(style)` right after creation (name, position/size, type-specific keys), and `PostChildrenInit(style)` after the children are built. `gCurrentDefaultStyle` is whatever is active at that moment: children can change it with `SetStyle`/`AppendStyle`, and `MakeDialog` restores the saved style at the end of each group, **before** calling `PostCreate`. So `PostChildrenInit` sees the style from *before* the group, not the one the children left behind.

### 2.3 `SetStyle` / `AppendStyle`

* `SetStyle(s)` returns a closure; when the enclosing `MakeDialog` runs it, `gCurrentDefaultStyle = s`.
* `AppendStyle(s)` returns a closure; when run it sets `s.parent = gCurrentDefaultStyle` **mutating the table `s`**, then makes `s` the default. Stacking `AppendStyle` calls therefore builds a chain, and re-running the same table re-parents it (a table appended twice points at itself via the second run only if the default is that table).
* Both are scoped to the enclosing group because `MakeDialog` saves `gCurrentDefaultStyle` on entry and restores it after the children. `mainmenu.lua`'s `SetStyle(C3ButtonStyle)` inside a `Bitmap { }` therefore applies to the siblings that follow it inside that `Bitmap` and ends with it.
* Style tables are plain Lua tables. The C++ never copies them. They are read lazily through the registry key held by the `TLuaTable`.

### 2.4 From DSL table to windows

Walkthrough for `Group { Text { ... }, Bitmap { ... } }` inside a screen script:

1. Evaluating the expression runs the constructors first, in argument order. `Text{...}` and `Bitmap{...}` are Lua functions created by `TWindowScript::AddWindowType(name, classId)` (0xa1ad8). It `lua_dostring`s, for each registered name, this exact template:
   `function %s( table ) Creating( '%s', table ); table.typename='%s'; return function() DoWindow( table ) end end;`
   (all three `%s` = the type name) and records `name -> classId` in a `std::map<str,int>` at `TWindowScript+0x38`. So **constructing a widget does nothing but tag the table with `typename` and return a deferred closure.** Nothing is created yet.
   * `Group(t)` is different: a closure that calls `MakeDialog(t)` without creating a window.
   * Style helper results (`SetStyle(...)`, `AppendStyle{...}`, `SelectLayer(n)`, `FitToChildren()`) are also closures sitting in the array part.
2. The outermost call is `MakeDialog(tbl)` (or `Group`/`DoWindow` from inside). `MakeDialog` saves the style, names the current stack-top window if `tbl.name` is set, then `table.foreachi(tbl, DoDialogCommand)` calls each array element **in order**. A widget closure runs `DoWindow(its table)`, a style closure edits the default style, `SelectLayer(n)` selects the layer of the stack-top window, etc.
3. `DoWindow(t)`: set `t.beginGroup`, call `GenericCreate(t)`, then `MakeDialog(t)` recursively for its children, then `PopWindow()`.
4. `GenericCreate` = `TWindowScript::CreateCustomWindow(lua_State*)` (0xa2620):
   * wraps the argument in a `TLuaTable`, stores it in the registry under a light-userdata key, builds a `TWindowStyle`;
   * reads the style string `typename` (default from the format table at the call site, empty if absent) and looks it up in the type map; **if the name is unknown the map gets a new entry with id 0 and nothing is created** (silent no-op, and then `MakeDialog` still runs the children against the current context window, so children get attached to the *parent*);
   * `TWindow::CreateFromId(id)` instantiates the C++ class by class id (factory), then `InitWindow(window)`;
   * removes the registry key (sets it to nil) and returns 0 values.
5. `InitWindow(w)` (0xa241e) in order:
   1. `stackTop->vtable[+0x24](w, true)` = `AdoptChild(w, true)`, i.e. add to the context window and call `OnNewParent`; if it returns false `InitWindow` returns 0 and **the window is not pushed and is leaked** **(inference: the code path returns without deleting)**.
   2. `w->vtable[+0x14](style)` = `Init(style)`: name from key `"name"`, then `SizeAndPositionFromStyle`; class-specific overrides read their own keys (button graphics, image path, ...).
   3. push `w` on the script's window stack.
   4. run Lua `gDialogWidth = <w>; gDialogHeight = <h>;` with the new window's size.
   5. if `TWindowScript+0x54` (an observer object) is set, call `observer->vtable[+8](w, style)`. What sets it is **Unknown**.
6. After `MakeDialog` finishes the children, `PostCreate(t, typename)` -> `TWindowScript::PostChildCreate(t, typename)` builds a fresh `TWindowStyle` from `t` and calls the stack-top window's `vtable[+0x18](style)` = `PostChildrenInit(style)`. Note that Lua only calls it when the global `typename` is truthy; a plain `MakeDialog { ... }` table has no `typename`, so a bare `MakeDialog` does **not** run `PostChildrenInit` on the modal.
7. `PopWindow()`.

Registered widget names: `Slider`, `Bitmap` (TImage), `Button`, `Text`, `TextEdit`, `Window` (plain `TWindow`) inside the constructor; `TWindowManager::AttachWindowScript` adds `TXSellWindow`; `ChocoThreeMain` (`_free/c.c`) adds the game classes (`SignEditor`, `SlotMachine`, `TightText`, `OptionsWindow`, `HiscoreWindow`, map/port/factory windows, ...). The `Text` registration reads its name from the destructor-array symbol `___tcf_0`; it resolves to "Text" (that string lives right next to the others in the binary).

### 2.5 The Lua-side custom constructors in `style.lua`

`Button`, `JukeboxCategoryButton`, `QuestSelectButton`, `MapPortButton`, `LargeLedgerButton`, `SmallLedgerButton`, `TightText`, `PlaygroundButton`, `MenuButton`, `ColorShadowText` and `MenuButtonLabel` override or extend the generated constructors. They return a closure that edits the table **at build time** and then calls `DoWindow`:

* wrap the `command` so it is a no-op while `gButtonsDisabled` is set;
* read tags with `GetTag(button, "label")` etc. (own table, then default style);
* `table.insert(button, SelectLayer(kAllLayers))`, `FitToChildren()`, `AppendStyle{font=..., flags=...}`, and one or more `Text{...}` children, so the *button's children* are label texts on each state layer (layers 0=up, 1=down, 2=over, 3=checked-over);
* `button.typename = 'Button'; DoWindow(button)`.

These Lua helpers are why a `TButton`'s visual states are child windows on layers (`TLayeredWindow`).

## 3. The modal stack

### 3.1 State in `TWindowManager`

(constructor 0x500ae; offsets used throughout)

| offset | meaning |
|---|---|
| +0x04/+0x08 | optional render target (ref-counted texture); non-null switches `DrawStack` into render-to-texture mode. Cleared on event type 0x11. |
| +0x0c | the screen window (`TScreen`), set by `SetScreen` (it is also `PushModal`ed first, so it is the bottom of the chain) |
| +0x10 | "abort drawing this frame" flag (set by `_PopModal`, cleared at the start of `DrawStack`) |
| +0x14 | time of the last draw (ms) |
| +0x18 | pointer to the **modal list** header (`std::list<TModalWindow*>`; **front = top**) |
| +0x1c | pointer to the **destroy list** |
| +0x20 | pointer to the **overlay list** (windows drawn above everything, in list order, with draw mode 0) |
| +0x24 | "drawing disabled" flag |
| +0x26 | `topModalOnly` (set by `SetTopModalOnly`) |
| +0x27 | dirty flag ("screen invalid"); constructor sets 1 |
| +0x28 | debug-outline flag |
| +0x2c | currently hovered window |
| +0x30 | mouse-listener list (windows that captured the mouse) |
| +0x38 | **focus window** (the keyboard focus and the fallback target for messages) |
| +0x3c | the `TWindowScript*` |
| +0x44 | queued messages (`std::list<TMessage*>`) |
| +0x4c | message listeners (`std::list<TMessageListener*>`) |
| +0x54 | hover handler (object with `vtable[+8](window, point)`) |

`TWindowManager::GetInstance()` is just `TPlatform::GetInstance()->GetWindowManager()`.

### 3.2 `PushModal(TModalWindow*)` (0x4e768) in order

1. clear the hover pointer.
2. If any window is in the mouse-listener list, copy the list, empty it, then call `vtable[+0x4c]` (`OnMouseLeave`) on each so buttons drop their pressed/rollover state.
3. Remember the old top (list front, or null).
4. Insert the new modal at the **front** of the modal list.
5. If there was an old top: `oldTop->AdoptChild(newModal, false)` (the new modal becomes a **child of the old top**, which is what makes the modal chain a parent chain and makes `GetWindowDepth` a parent-count), and `TClock::Pause` on the old top's modal clock (+0x50).
6. `manager.focus (+0x38) = newModal`; `newModal->vtable[+0x68](oldFocus)` = `OnSetFocus` (`TModalWindow::OnSetFocus` moves focus to the modal's `defaultFocus` child if it has one).
7. If a script is attached: `script->NewModalWindow(newModal)` (window stack becomes `[newModal]`).
8. `newModal->vtable[+0x10]` = `OnNewParent`; for a modal that is `TClock::Start` on its own clock.
9. If the modal's size is 0 wide (`right == left`) and it has a parent, `SetWindowSize(parent w, parent h)`.
10. Set the dirty flag.

Lua `PushModal(name)` (`TWindowScript::PushModal`, 0x9fc6e) allocates a 0x80-byte `TModalWindow`, sets its name to the given string (the script path), calls the manager's `PushModal`, and **returns the id formatted with `"%8x"`** (width-8 hex, space padded, from window +0x28). That string, not the name, is what `WaitForCloseMessage` waits on.

`SetScreen(screen)` (0x4e97c) stores the screen in +0x0c, `PushModal`s it, sizes it to the renderer viewport, then calls its `vtable[+0x10]`.

### 3.3 Popping

`TModalWindow::OnMessage(msg)` (0x747e4) is the only consumer of close requests: if `msg.type == 1` (`kCloseWindow`) **and** `msg.name` is non-empty, it calls `TWindowManager::PopModal(msg.name)` and returns handled; an empty name is swallowed (handled, nothing popped); any other type returns false.

`PopModal(str)` (0x4eb7e):
1. clear hover pointer.
2. Walk from the top modal *down the parent chain*, looking for the first window whose `name == s` **or** whose `"%8x"` id string `== s`. If none matches, nothing is popped and the function ends (the new-top clock is still restarted, see below).
3. If found: repeat { remember the current top; `_PopModal()`; stop when the popped one matched the target (by name or id) }. So **everything above the target is popped too, and then the target**.
4. `TClock::Start` on the new top's clock.

`_PopModal()` (0x4e9fc):
1. take the top (front) modal; save its rect (unused after).
2. `parent->OrphanChild(top)` (vtable +0x28).
3. `SafeDestroyWindow(top)` (deferred delete, see 1.2).
4. remove it from the modal list; `top->DestroyTasks()` (kills every task in the modal's own `TTaskList`).
5. If a modal remains: `script->NewModalWindow(newTop)`, set manager focus to it and call its `OnSetFocus(oldFocus)`; if none remains: `script->NewModalWindow(null)`.
6. run a `TModalPoppedSpider` over the popped window's children (0x1787bc): depth-first, it calls each descendant's `OnParentModalPopped()` (vtable +0x2c). The only implementation in the binary is `TWindow::OnParentModalPopped` (0x164d48), a no-op, so this pass does nothing (inference from the symbol index).
7. dirty flag set; **`abortDraw (+0x10) = 1`**.

The actual `delete` is deferred to the next `HandleEvent` (`ClearDestroyedWindows`). `TModalWindow::~TModalWindow` (0x74ee8) is where **`kModalClosed` is born**: it builds a message of type 6 whose name is the dying window's id as `"%8x"`, with target null, and posts it to the manager. It then destroys the modal's task list, hotkey map and clock.

### 3.4 Closing from Lua, and what the caller gets back

* `CreateCloseMessage()` (0xa1320) builds a message of type 1 whose name is the `"%8x"` id of **the window on top of the script window stack** at that moment (which, outside dialog building, is the top modal; it can be a *different* window if called while a nested `DoWindow` is in progress).
* `CloseWindow(v)` = `PostMessage(CreateCloseMessage())` and, if `v` is truthy, `gReturnValue = v`.
* `TButton::DoCommand` (0x9372a) does the same for buttons whose style has `close` true: it posts a type-1 message for `FindParentModal(button)` and sets `gReturnValue` to the **button's name** (`TScript::SetGlobalString`). So a Yes/No button returns its own name only if it has `close=true`. `ui_generic_yn.lua` instead uses `FadeCloseWindow("generic","yes")` in the button command: it runs a fade-out and calls `CloseWindow("yes")` from the `onend` callback; `ModalReturn` sets the value.
* Sequence for `local yn = DisplayDialog{...}`:
  1. `PushModal` creates and pushes the modal, `gDialogTable = t`, the dialog script runs `MakeDialog`.
  2. The coroutine loops in `WaitForCloseMessage`, yielding every tick (section 5).
  3. A button/Lua code posts a close message, sets `gReturnValue`.
  4. Next `DispatchMessages`: the close message reaches the modal -> `PopModal(idString)` -> `_PopModal`.
  5. Next event: `ClearDestroyedWindows` deletes it -> destructor posts type-6 message.
  6. That message is dispatched, delivered to the script (resumes the coroutine with `(msg, nil)`); `WaitForCloseMessage` sees `type==kModalClosed` with matching name, returns; `DisplayDialog` returns `gReturnValue` and resets it to nil.
  So **the value comes through the global `gReturnValue`, not through a message payload, and is consumed (set to nil) by the first dialog call that returns.** Closing without setting it returns nil (e.g. clicking away, `DoModal` of a screen that never calls `CloseWindow(value)`).
* Latency: at least one extra event between the close request and the caller resuming (two `HandleEvent` passes minimum). Anything that expects "dialog closed => caller continues the same frame" must keep that ordering (see 3.5).
* `gClosedWindows[name]` exists to survive a `kModalClosed` for a dialog other than the one currently being waited on (nested/overlapping modals) so the earlier waiter does not miss it.

### 3.5 `SwapToModal` / `ClearModalChildren`

`TWindowScript::ClearModalChildren` (0x9fc12): top modal: `DestroyTasks`, `DestroyAllChildren`, `SetWindowSize(0,0)`, `SetWindowPos(0,0)`. `SwapToModal(file)` then runs the new script into the *same* `TModalWindow` (same id, same chain position, same `gReturnValue` scope). This is why `DoMainWindow` survives a `SwapToModal("ui/mapview.lua")` from the main menu: the modal chain depth does not change.

### 3.6 Bottom of the chain and quitting

`TWindowManager::HandleEvent` (0x4ee4c): after `ClearDestroyedWindows`, if the modal list exists and has a first node whose window pointer is **null**, it calls `TPlatform::Exit`. An *empty* list just falls through. `type 5 (kQuitNow)` handled in `DispatchWindowMessage` also calls `TPlatform::Exit` after deleting the message.

## 4. Messages

### 4.1 `TMessage` layout and Lua messages

`TMessage`: vtable, `int type` (+4), `str name` (+8), `TWindow* target` (+0xc). `TMessage(type, name, target)`. Allocation size 0x10.

`TLuaMessageWrapper` (0x18 bytes) = `TMessage` + an embedded `TLuaObjectWrapper` at +0x10 holding the `lua_State*` at +0x14. A wrapper keeps the *Lua userdata alive* via a registry slot keyed by the light userdata at +0x10 (`LPCD::Push`/`Get`). `PostWindowMessage(luaMsg)` (0xa2248) copies `type`, `name` and `target` from the Lua-side message into a new wrapper and queues the wrapper. When such a wrapper is later handed back to Lua (`TWindowScript::OnMessage`, or `SendWindowMessage` toward a window's `defaultFocus`), the *original* Lua userdata is fetched from the registry, so **identity is preserved**. A message that never passed through Lua is pushed with `LPCD::Push`. The Lua side only reads `.type` and `.name` anywhere I saw (`ScreenInit`, `helpers.lua`); other fields **Unknown**.

`CreateNamedMessage(type, name)` (Lua) creates such a Lua-visible message (free function, no script pointer in its closure, so it cannot depend on window state). `AskQuit` uses `PostMessage(CreateNamedMessage(kQuitNow, "Quit"))`.

`PostMessage(msg)` posts to the manager's queue (target as copied, usually null). `PostMessageToParent(msg)` (0xa287a) does the same but sets `target = topModalWindow->parent` (+4), i.e. the *modal below the top* (null if the top is the screen).

### 4.2 The queue and dispatch

* `TWindowManager::PostWindowMessage(m)`: append to the queue (+0x44).
* `DispatchMessages()` (0x4e612): while the queue is non-empty, take the **first** node, unhook it, then `DispatchWindowMessage(m)`; re-read the head each iteration (handlers can post more, which are processed in the same call). **(inference: FIFO; the append position is not visible in the decompile, the pop is from the front.)**
* `DispatchWindowMessage(m)` (0x4ce2c), in order:
  1. If `m` is a `TLuaMessageWrapper`, switch to the Lua-wrapped object for identity (see above).
  2. **`type == 5` (`kQuitNow`)**: delete the message, `TPlatform::Exit()`, return.
  3. If `m.target` is non-null: skip to step 6 with that target.
  4. Otherwise offer it to each `TMessageListener` (+0x4c list) in order; if a listener's `OnMessage(m)` (vtable +8) returns true the message is deleted and dispatch ends. (`BSGTransition` registers itself here, see 6.4.)
  5. Pick the fallback target: the manager's **focus window (+0x38)**, else the top modal. If neither exists, delete the message.
  6. `TWindow::SendWindowMessage(target, m)`.
* `TWindow::SendWindowMessage(w, m)` (0x4a524): loop: call `w->vtable[+0x34]` (`OnMessage`); if it returns true delete `m` and stop. Else if `w` has the modal bit (0x001): look at the modal's `defaultFocus` (+0x34): if it exists and *its* `OnMessage` (Lua-unwrapped message) returns true, stop; **otherwise call `TWindowManager::OnScriptMessage(m, null)` and stop** (the script gets every message nobody consumed, and nothing climbs above the first modal). Else go to `w->parent` and repeat. If the chain ends without a modal, the message is dropped (not deleted; leak) **(inference from the decompile: no delete on that path)**.

So: **windows see a message before the script does; unconsumed messages all end up in the script via `OnScriptMessage`** and from there in Lua queues/coroutine. `TModalWindow::OnMessage` consumes only type 1.

### 4.3 How a button command reaches Lua

1. Mouse down on a `TButton` (`OnMouseDown`, 0x93012): if not already captured, `AddMouseListener(button)` and `SetState(1)` (down); then the hit-mask check (see 6.6). Always returns true.
2. Mouse up (`OnMouseUp`, 0x9337a) calls `Press()` (vtable +0x74) when the button is in the down state and the pointer is still inside (mask test passes).
3. `Press` (0x93cd0): `StateUpdate(3 /*pressed*/, 1)`; plays the button's click sound if it has one and the button and its ancestors up to the modal are visible; then `DoCommand()`.
4. `DoCommand` (0x9372a):
   a. if style `close` set and a parent modal exists: post the type-1 close message for that modal; `gReturnValue = button name`.
   b. build a `kButtonPress` (type 3) message with name = button name and target = button's parent (or, if the "sendToParent" bit 7 of +0x3c is set, `topModal->parent`).
   c. if the button has a command action (`TButton::LuaAction`, at +0x40): call `action->DoAction(button)` **synchronously**, see below.
   d. if the button is still under the screen window, post the type-3 message to the queue, else delete it (unless the sendToParent bit is set, then it is posted regardless). Returns 0 normally, 1 when the button was detached by the command.
5. `LuaAction::DoAction` (0x93400): builds a `kCommandOnly` (type 8) message named `LuaAction:<buttonName>` (a prefix, not a suffix), runs the Lua string `function GetButtonName() return [[<buttonName>]]; end;` (so the command can ask which button fired it), then `TWindowManager::OnScriptMessage(msg, commandFunction)` -> `TWindowScript::OnMessage(msg, fn)`.
6. `TWindowScript::OnMessage` (0xa1c08), exact branches:
   * **Coroutine not suspended** (`CoroutineActive` false = main thread not in a yield; i.e. before the chunk's coroutine started, or after it finished): runs `gMessageToQueue = <msg>; table.insert(gMessageQueue, gMessageToQueue); gMessageToQueue=nil` and, if a function was given, the same with `gCommandQueue`/`gMessageCommand`. Returns 1.
   * **Suspended, but not by `HandleGetMessage`** (state `+0x50` != 3): the message is **dropped**; if a function was given it is called immediately with `pcall` (error handler = global `_ALERT`) on the function's `lua_State`, and the call returns 0.
   * **Suspended in `HandleGetMessage`** (state == 3): push the message (a fresh `kCommandOnly`-style type-8 message named "" if `msg` was null), push the function if any, `TScript::Resume(1 or 2)`. A non-zero return from `lua_resume` (error) resets state `+0x50` to 0. Returns 1.
7. In Lua the resume lands inside `WaitForMessage` (section 5), which runs the command function in the coroutine.

So a command callback runs **inside the Main coroutine, on the C++ call stack of the mouse handler**, synchronously. It can call `DisplayDialog` (which pushes a modal and yields back to C++); everything below it on the C++ stack then continues and the event handler returns.

Hotkeys: `RegisterHotkey(key, button)` (Lua -> `_RegisterHotkey` -> `TWindowScript::RegisterHotkey`) calls `topModal->vtable[+0x74]` = `AddHotkey(key, button)`: normalises `key` and stores it in the modal's `std::map<str,str>` (+0x68); an empty button name erases the entry. `NormalizeKeyString`: lower-case; if length > 1 and starts with `@` keep the `@` prefix and skip modifier parsing; otherwise prefix `shift-`, `ctrl-` (also matches `control-`), `alt-` in that order if the text contains them (tested with `find`, anywhere in the string), then append the **upper-cased first character of the remainder** (`substr`, the exact start index is a decompiler detail I did not pin). `OnKeyDown(ch, mods)` builds the key string from the event modifiers (bit0 = shift, bit1 = ctrl, bit2 = alt are the OS modifier bits the Mac layer packs at TEvent +0x10) plus the char, normalises, looks it up; if absent it retries with the bare char; on a hit `PressButton(name)` finds the button by name under the modal (`GetChildWindow`, unlimited depth) and calls its `Press()` (vtable +0x74); a missing or non-button name logs `"ERROR: Hot key button not found. Button:'%s'"`. `TagHotKey(key, label, ...)` is a string helper: it takes the last character of the key string, finds its first occurrence in the label (searching from index 1) and wraps it in `<u>` `</u>`; no match returns the label unchanged **(the exact argument roles are partly inferred from the decompile)**. `StripKeyFromLabel` is a sibling helper, not analysed.

`TModalWindow::OnChar` (0x74d42): on Enter or Escape (the two char constants are loaded from data; presumably 13/27 **(inference)**), creates a `kDefaultAction` (type 2) message with empty name and scans children for the `default` and `cancel` buttons (style keys `default`, `cancel`): Enter presses the default button's `DoCommand`, Escape the cancel button's. If no button handled it, the type-2 message is sent with `SendWindowMessage` to the focus window or, if none, to the modal. Returns true either way.

## 5. Script suspension

### 5.1 Objects

* There is **one `TWindowScript` for the whole game**, not one per window. `TPlatform` constructs it with a null `lua_State*` (so `TScript` calls `lua_open()` and owns a fresh state), `TWindowManager::AttachWindowScript` stores it at manager +0x3c, registers `TXSellWindow`, pushes the current top window on the script's window stack if one exists, creates a Lua global holding a userdata for the manager (name resolved from a table I did not decode), and `TPlatform::AdoptTask(script)` hands it to the platform task list.
* `TScript : TAnimTask` (so it is a timed task). `TAnimTask` fields: +4 byte `readyEveryFrame`, +8 `nextTime` (ms, -1 = paused), +0xc interval, +0x10 `catchUp`, +0x11, +0x14 `TClock*`.
* **The coroutine is the `TScript`'s own `lua_State`.** `TScript::Resume(n)` is `lua_resume(L, n)` on the same `L` that `lua_open` returned. There is no `coroutine.create`. The "suspended" test (`CoroutineActive`) reads `L->ci->state & CI_YIELD (0x10)`. Everything in Lua 5.0 (`lua_State` internals are inspected directly) so **the new engine can implement this with a Lua 5.0.2 state and `lua_resume`/`lua_yield` on it**, or equivalently with a dedicated thread.

### 5.2 How `Main` starts

`ChocoThreeMain` (0x1001c) calls `TScript::RunScript("ui/mainloop.lua")` once all types are registered and sound is set up. `RunScript` loads the file (`TScriptCode::Get` cache then `luaL_loadbuffer`, chunk name = the path) and calls `RunFunction(0, 1)`:
* the chunk runs under `pcall` with `_ALERT` as handler. `mainloop.lua` ends with `return Main`.
* if the thread is **not suspended**, the result count is 1 and the result is a function, then `RunFunction` makes sure the task delay is set (`SetDelay(0, true, true, false)` when the task was paused) and calls `lua_resume(L, 0)`: **`Main` starts as the coroutine body, immediately, inside `RunScript`**, and runs until its first yield.
* If `RunFunction` is called while the thread **is** suspended (any later `RunScript`/`DoLuaString`/`FillWindow`/`TWindowManager::RunScript`), it does not run on the stack: it builds a closure with the global `GetClosure(f, ...)` (`function GetClosure( f, ... ) return function() return f( arg[1], ... arg[6] ); end; end`), stashes it in the registry, then calls `TScript::InjectFunction` (virtual; `TWindowScript::InjectFunction` = push nil, insert below, `Resume(2)`), i.e. resumes the coroutine delivering `(nil, closure)`; `WaitForMessage` runs the closure as if it were a button command. If injection fails it falls back to a plain `pcall`.
* After `Main` returns/errors the task is paused (`Animate` pauses it whenever the thread is not suspended), and errors print `"lua_resume error: %s"` through `WriteDbg`.

### 5.3 The yield sites (the only ones)

* `HandleGetMessage(timeoutOrNil)` (0xa04b6), called by `WaitForMessage`:
  * `lua_gettop == 1`: `SetDelay(tonumber(arg), repeat = true, absolute-from-now = true, false)`; else `Pause()` (no timer, resumed only by `OnMessage`).
  * sets state `+0x50 = 3` ("waiting for a message");
  * pushes the global `gYieldResult` and `lua_yield`s.
  * Because `WaitForMessage` always passes its first parameter, **gettop is always 1**, and a missing timeout arrives as nil which `lua_tonumber` turns into 0: **the coroutine is resumed on every tick of the task list** (delay 0, interval 0, catch-up as in `TAnimTask::DoTask`). It is a polling loop, not an event wait. **(inference from the decompile; the polling is visible in `WaitForCloseMessage`'s loop having no other sleep.)**
* Plain `Yield()` = `coroutine.yield()`; nothing in the shipped Lua calls it. If it were used it would be woken by the task tick with zero values (at the interval left by the last wait), by `OnMessage` with `(message[, fn])` (the state word stays 3 after the first wait), or by injection with `(nil, closure)` where the closure is never run. Details in `design-questions.md` section 1.
* `Pause(ms, waitForKey)` in Lua loops `WaitForMessage(endTime - now, true)` until `GetTimer() >= endTime`, ignoring messages except that, if `waitForKey` is truthy and a `kPressAnyKey` message arrives, it returns early; any other message is `table.insert`ed back into `gMessageQueue`. The `true` second argument means "skip the queued messages, read straight from the engine".

`WaitForMessage(timeout, skipQueue)` (ScreenInit line 840):
```lua
if not skipQueue and table.getn(gMessageQueue) > 0 then
  local n = table.getn(gMessageQueue)
  message = gMessageQueue[n]; table.remove(gMessageQueue)   -- LIFO: takes the LAST element
  return message
end
local msg, cmd = HandleGetMessage(timeout)                  -- yields; resumed with (message, function) or nothing
if cmd then gYieldResult = cmd()                            -- run the button command in the coroutine
else
  local n = table.getn(gCommandQueue)
  if n > 0 then local c = gCommandQueue[n]; table.remove(gCommandQueue); gYieldResult = c() end
end
return msg
```
`gMessageQueue` is used as a **stack** (insert at end, pop from end). In practice there are only 0 or 1 entries. Queued commands are only drained when a `WaitForMessage` call returns without a command of its own.

### 5.4 Timers

* Every wake path is `TAnimTask::DoTask` driven by `TTaskList::DoAll`. `DoTask`: if the clock is more than 1000 ms past `nextTime` it resets `nextTime = now` (so a long stall does not replay the backlog); otherwise it calls the task's `Animate` (`TScript::Animate` = `Resume(0)`), and for repeating tasks keeps looping while `nextTime <= now` (with the "catch-up" flags), else it advances `nextTime` by the interval. `TAnimTask::Ready(ctx)`: context 1 returns the `+4` flag; otherwise ready iff `nextTime != -1 && now - nextTime >= 0`.
* `TScript::Animate`: `Resume(0)`; if that returned an error code, pause the task; if the thread is no longer suspended after the resume (finished), pause the task.
* The `TScript` task has no `TClock` (uses the platform millisecond timer `TPlatform::Timer()`, the same one Lua's `GetTimer()` returns: ms since the process timer origin, saturating to uint32). Modal windows have their own `TClock` (`+0x50`) that is paused while a dialog covers the modal (`PushModal` pauses the old top, `PopModal` starts the new top) and `TAnimTask`s that were given a modal clock (window animations, via `GetWindowAnim`) freeze while covered. The script and transitions use the platform clock, so a covered dialog's fade and the script itself keep running.
* There is **no global time scale in the window system.** `fTimeScale` that appears in the strings is the particle-system function (`FTimeScale.c`), unrelated.

### 5.5 Delivering a dialog result on resume

Covered in 3.4. In one sentence: the result is `gReturnValue` (a Lua global), the "resume" is a `kModalClosed` message with the dialog's id string, delivered through `OnMessage` state 3 or the Lua queues.

### 5.6 `CloseRequested`

`TWindowScript::CloseRequested` (0xa21b2): if a movie host (`TFlashHost*` at `+0x5c`) exists it is stopped and deleted, then `TScript::Resume(0)` is called and the Lua stack restored. It exists to wake `DisplaySplash` when the movie ends or is dismissed **(inference; I did not find the caller)**.

## 6. Frame order, drawing and input

### 6.1 Outer loop

`ChocoThreeMain` (`_free/c.c`), after setup and `RunScript("ui/mainloop.lua")`:

```
while (TPlatform::GetEvent(&e), e.type != 4) {       // 4 = quit
    if e.type == 3        DoLuaString("AskQuit()")   // window-close / Cmd-Q request
    elif e.type == 0x12   toggle foreground on MapWindow, PortWindow, LedgerWindow,
                          ChocolateFactory, CoffeeFactory; BSGSoundActivate/Deactivate
    elif e.type == 0x13   TSettings::UpdateFullScreen
    TWindowManager::HandleEvent(&e)
}
BSGSoundShutdown(); DoLuaString("if Player then Player:SaveGame() end")
```

`TEvent`: `int type` (+0), `int arg` (+4: char / ext-mouse sub-type), `int x` (+8), `int y` (+0xc), `uint modifiers` (+0x10). Types I could identify: 1 idle, 2 ignored by the manager, 3 close request, 4 quit, 5 mouse down, 6 extended/other mouse button, 7 mouse up, 8 mouse move, 9 (handled via the mouse-listener list, probably leave/exit), 10 hover, 11 key down (`char`, `mods`), 12 key up, 13 char, 14 unicode char (UTF-32 in +4), 0x11 render-target invalidate, 0x12 foreground change, 0x13 fullscreen change. Mapping 5-8 = down/ext/up/move comes from the vtable slot order (`OnMouseDown`, `OnExtendedMouseEvent`, `OnMouseUp`, `OnMouseMove`, `OnMouseHover` at +0x3c,+0x40,+0x44,+0x48,+0x64) **(inference for the exact slot-to-name pairing; slots are confirmed)**. Mouse coordinates are in game pixels relative to the screen window.

`TPlatformData::GetEvent` (0x929b8): returns a queued OS event if any; if none, it runs the platform **idle task list** (`TTaskList::DoAll(list at TPlatformData+0x6a14, ctx 0)`, which includes the script task and transitions), polls modifiers, and returns a type-1 idle event. When the app is in the background it sleeps with a growing backoff (`usleep`, +10 ms per idle iteration up to 1000 ms; reset to 0 when foreground). Foreground never sleeps in the loop (the display's own vsync/buffer swap paces it) **(inference)**.

### 6.2 `TWindowManager::HandleEvent` (0x4ee4c)

Order for every event:
1. type 2: return. type 0x11: drop the render-target reference, then continue.
2. `ClearDestroyedWindows()`.
3. If the modal list is empty: return. If the top modal pointer is null: `TPlatform::Exit` and return. Otherwise pick the target by event type:
   * **type 1 (idle)**: `TModalWindow::DoModalProcess(topModal, ctx 0)` (runs the modal's own `TTaskList`), then `DispatchMessages()`, then `DrawStack()`, then `DispatchMessages()` again.
   * **types 5-8, 10 (mouse)**: if the mouse-listener list is non-empty the event is routed through the captured windows first (that is how `TButton` gets mouse-up after the pointer left it); otherwise it goes through the hit-test path (6.5).
   * **type 9**: iterates the mouse-listener list (mouse-leave style housekeeping); details **Unknown**.
   * **types 11-14 (keyboard)**: `TWindow::HandleEvent(manager.focus, ev, false)`; if there is no focus window (+0x38 null) it falls back to the top modal path.
   * **type 15**: cursor-mode handling (`GetCursorMode`); details Unknown.
   * all other types (including 0, 3, 4, 16..): `DispatchMessages()` then `TWindow::HandleEvent(topModal, ev, false)`.
4. Message pumping thus happens: before each input event (default path), twice around the draw on idle.

### 6.3 `DrawStack()` (0x4d1a0), per idle event

1. clear `abortDraw`. Return if drawing is disabled (+0x24), the screen is not dirty (+0x27 == 0), or the display is not ready.
2. **Throttle:** `now = Timer(); if now < lastDraw + 12 (foreground) or lastDraw + 12 + 0xbc = 200 (background): return;` so **max about 83 fps foreground, 5 fps background**. `lastDraw = now`.
3. Reset per-pass flags: clear 0x400 on the top modal and run a spider over its subtree (resets the render-target-pass markers).
4. If a modal exists and the display is ready: run the **platform draw-context tasks**: `TTaskList::DoAll(platformTaskList, ctx 1)` then `DoModalProcess(topModal, ctx 1)`. (Ctx 1 tasks run only if their `+4` flag is set; this is where per-frame sprite/anim tasks live **(inference)**.)
5. **Normal mode (no render target)**: `BeginDraw`; clear dirty; `DrawLayer(0)`; draw every window in the overlay list (+0x20) with mode 0; if the debug outline flag (+0x28) and a hover window exist, draw four 2-px filled rectangles (an outline) around the hover window, colour not resolved; `EndDraw(true)` (present).
6. **Render-to-texture mode (+4 non-null)**: `BeginRenderTarget`, `DrawLayer(1)`, `EndRenderTarget`; then `BeginDraw`, clear dirty, `DoDraw(screenWindow, mode 0)`, `DrawLayer(2)`, overlay windows, debug outline, `EndDraw(true)`. Mode 1 draws the opaque layer into the texture; mode 2 draws only what is above it.
7. Anything that calls `InvalidateScreen` (sets +0x27) causes a redraw on the next eligible idle. **A static screen is not redrawn at all.**

`DrawLayer(mode)` (0x4cf72): does nothing if abortDraw is set, the platform is not ready or the modal list is missing. If `mode == 2` or `topModalOnly`: just `DoDraw(topModal, mode)`. Otherwise search the modal list from the **top** for the first modal with flag 0x800 whose size equals the screen's (an "opaque full-screen" modal); draw from that modal **upward to the top**, in that order (so everything beneath the highest opaque full-screen modal is skipped); if none, draw the whole chain from the bottom to the top. In modes other than 0 the first (top) modal is skipped inside the loop. After each `DoDraw` it checks `abortDraw` and returns immediately if set.

`TWindow::DoDraw(mode)` (0x4a2cc): skip if not enabled (0x100). Mode 1: if this window has 0x4 and its parent has 0x4, mark 0x400 and fall through; mode 2: skip windows already marked 0x400. Compute `clip ∩ windowRect` (screen-space); if empty return. Push viewport = window rect when the window has non-zero width (so child coordinates are parent-relative), push clip rectangle, call `Draw` (vtable +0x1c), draw children from **last to first** (back to front) skipping any child with flag 0x001 (modals), call `PostDraw` (vtable +0x20), pop viewport and clip. Children are clipped to the parent's visible area.

### 6.4 Transitions (`BSGTransition`, `Transitions.h`)

Lua `Transition{ "<style>", window="name", time=ms, alpha=a, onend=fn, ... }` -> free function `DoTransition(TLuaTable*)` (0x394ee):
1. Find the window: `GetChildWindow(topModal, name, -1)`. **Not found: return silently** (the `onend` function is neither called nor released).
2. `time` default **100 ms** (clamped to a uint32); `alpha` default 1.0; `onend` is a `TLuaFunction*` (registry-anchored).
3. Styles (string compare, exact):
   * `fadein`: `SetWindowAlpha(w, 0)` then `FadeInTransition(alpha)`.
   * `center_fadein`: hide the window (`BSGHideWindow`), alpha 0, `FadeInTransition`, then position the window horizontally centred in the screen: `x = (screenWidth - windowWidth) >> 1`, `y` unchanged.
   * `center`: only the reposition. **No transition object is created and `onend` is released without being called.**
   * `fadeout`: `FadeOutTransition(alpha)`.
   * `swipe`: `SwipeTransition` from (`startx`,`starty`) to (`endx`,`endy`), each defaulting to the window's current x / y.
   * `path`: `CurveTransition` along `TCurve::Create(t.path)`; if the table is missing/invalid no transition is created.
   * `zoomin`: `ZoomInTransition` (requires the window to be a `TImage`).
   * any other string: `onend` released, nothing happens.
4. `StartTransition(task)`: marks the task's live flag, `TPlatform::AdoptTask(task)` (the **platform idle list**, not the modal's), `SetDelay(25 ms, repeat, from now)`: about 40 updates a second.

`BSGTransition` (0x392e6) fields: `+0x18` a `TMessageListener` sub-object, `+0x1c` **`TWindow* target` (raw pointer)**, `+0x20` `str` = `"%8x"` id of the **top modal at construction**, `+0x24` `TLuaFunction* onend`, `+0x28/+0x2c` `shared_ptr<bool>` live flag (initially true), `+0x30` duration (ms), `+0x34` start time (platform clock), `+0x38` `t` in [0,1], plus subclass fields (`alpha`, endpoints, curve, image pointer). It registers itself with `AdoptMessageListener`, so it sees every untargeted message.

`Animate` (0x39036), each 25 ms:
1. if the live flag is gone or false, return 0 (task ends);
2. `BSGShowWindow(target)` (sets 0x100 every tick, so a transition re-shows a window that script hid meanwhile);
3. `t = min(1, (now - start) / duration)` (float; `t = 1` when `elapsed >= duration`);
4. `ApplyTransition()` (virtual): fade-in sets alpha to `t * alpha`, fade-out `(1 - t) * alpha`, via `TWindowFader::Process(target)` which applies to the window and descendants; swipe `SetWindowPos(lerp(start, end, t))` (integer truncation each axis); curve samples the curve; zoom calls `TImage::SetTexture(image, texture, t)`;
5. `InvalidateScreen`;
6. if `elapsed < duration` return 1 (keep going); else call `onend` (`TLuaFunction::Call`: a bare `lua_pcall` with `_ALERT` as handler on the **script's `lua_State`**, **not** through the injection mechanism), release it, clear the live flag, return 0 (task finishes, deleted by the task list). The final frame always applies `t = 1`.
7. `OnMessage` (listener): on a **type 6** (`kModalClosed`) message whose name equals the stored modal id, and if still live: call `onend`, release it, clear the live flag. Returns false, so it never consumes the message. This guarantees `onend` fires if the modal dies mid-fade.

The destructor removes the listener and clears the live flag.

**Known bug (also recorded in the original plan):** `target` is a raw pointer and nothing nulls it when the window is destroyed. The only protection is the `kModalClosed` listener, which only covers destruction of the *whole modal*. Destroying just the target (`DestroyWindow`, `ClearModalChildren`/`SwapToModal` while a fade runs, `FillWindow`, or the modal being deleted *before* the fade's id test runs because messages dispatch a frame late) leaves the next `Animate` calling `BSGShowWindow`/`SetFlags` on freed memory. `TWindow::~TWindow` does not notify transitions (it does null `animTask->window`, but that is the `TAnimTask` at +0x2c, a different mechanism). **Reimplementation: have each window own a list of its transitions, or hold a weak reference, and cancel on destruction; keep calling `onend` when cancelled because `FadeCloseWindow`'s `onend` is what actually closes the dialog.**

### 6.5 Input dispatch and hit testing

`TWindow::HandleEvent(ev, bubbling)` (0x4b578) is entered on the *leaf* window:
* **Dispatch to the virtuals** for types 5-14, via a jump table: down/ext/up/move/hover pass the point (+8 = x,y); key down passes (`char`, `mods`); key up `char`; char `char`; unicode converts the UTF-32 value to a `str` and calls `OnUTF8Char(str)`; if that returns false and the string is one char, falls back to `OnChar`. The default virtuals return false.
* **Focus on mouse down**: for type 5 and `bubbling == false`, walk up from the receiving window to the nearest window with bit 0x002 and make it the manager focus (`vtable[+0x68]` = `OnSetFocus(old)`).
* If a window returned true (handled): stop.
* If not handled and the window **is not a modal**: build a copy of the event; for mouse events (types 5-8, 10) translate the point into the **parent's** space (add this window's origin). Then find this window in the parent's child list and `NextChildWindowFromPoint(parent, pt, next_after_this)`: the **next sibling behind** that is visible and contains the point (rect test `left <= x < right`, `top <= y < bottom`, plus the "visible ancestors" walk). If one exists, descend into it (using `ChildWindowFromPoint` to its deepest hit) and call `HandleEvent` on that; if none, call `HandleEvent` on the **parent**. So unhandled mouse events fall **through to what is behind, then up the tree**.
* If the window **is a modal** (bit 0x001): it is the end of the line. When no handler consumed it and the integer config value `overlaymouse` (read with `TPlatform::GetConfig("overlaymouse", "")`, key held in a global `char*` at 0x1d8318) is 0 or unset, a **`kPressAnyKey`** (type 4) message is posted: for event type 7 (mouse up) the name is `mouse:up`, for type 11 (key down) the name is `raw:` followed by the character; other event types post nothing. This is the source of "press any key" for `DisplaySplash`.
* `ChildWindowFromPoint(pt, depth)` (0x4b2bc): iterative descent up to `depth` levels (depth in 1..9 unrolled, deeper recurses), at each level taking the **first (frontmost) visible child under the point**; returns the deepest found, or the starting window if none. `NextChildWindowFromPoint(pt, iterator)` is the one-level primitive.
* Hit rect is the child's rect (parent space). There is **no transparency test in the generic window code**. Pixel masks exist only in `TButton`:

`TButton::ReleaseCapture(point)` (0x92ec4), misleadingly named, is the hit test and the mouse-leave: the point is in client space. If it is outside the client rect it calls `OnMouseLeave` and returns true ("outside"). If inside and the button has a click mask (style key `mask`, loaded by `SetClickMask(texture)` as an RGBA copy): it maps the point to mask pixels as `maskX = (clientW / maskW) * (x - left)` and likewise for y (**note: multiplies by clientW/maskW, i.e. assumes the mask is the window's size; a scaled button gets a wrong index**), and if `x < maskW && y < maskH` reads the pixel: the point counts as **inside iff any of R, G, B is greater than 0x13 (19)**; alpha is ignored. A dark pixel (R,G,B all <= 19), or out-of-range index, calls `OnMouseLeave` and returns true. No mask -> always inside. `OnMouseDown` always returns true (handled), even for a masked-out pixel (so it swallows the click); `OnMouseMove` returns false when outside so the event can continue to windows behind.

The `.mask.png` string in the binary (0x15bf20) is next to the texture loader's strings, together with the config key `automergemask`; I did not trace it. The shipping `style.lua` uses explicit `mask = "image/..._clickmask"` for the round buttons.

Button state machine (`TButton`): `+0x3c` low nibble = type (0 push, 1 toggle, 2 radio), bits 4-6 = mouse state (0 normal, 1 down, 2 over, 3 pressed-commit), bit 7 = send-to-parent; `+0x3d` bit0 = toggle-on, bit1 = `close`. `StateUpdate(new, old)` plays the rollover sound (style `rolloversound`, `sound`) on entering a new state other than down, and selects the layer: push buttons: layer = state (0 normal, 1 down, 2 over). Toggles/radios (type 1 or 2): state 0 uses layer `on` (0 when off, 1 when on), state 1 (down) uses `!on`, state 2 (over) uses 2 when off and 3 when on, state 3 changes nothing; this gives the 4-image `graphics` order of `style.lua` (off, on, over-off, over-on). Read from `TButton::StateUpdate` (0x93fe8); see `design-questions.md` section 5. `MouseMove` over a button with no capture: state 2 and add to the mouse-listener list. `TButton::Press` toggles the on bit for toggles and `SetOn` for radios (which clears siblings in the parent via spider).

Mouse capture: `AddMouseListener(w)` (no duplicates); the manager forwards mouse events to listeners first; listeners are removed by `OnMouseLeave`, `OnDestroyWindow`, `SafeDestroyWindow`, and are all cleared (with `OnMouseLeave` sent) by `PushModal`.

Focus and keyboard: the manager focus (`+0x38`) is set by mouse down (nearest focusable ancestor), by `RealSetFocus(name)` (looks the window up under the stack top, then `focus = found`, `found->OnSetFocus(oldFocus)`), by `PushModal`/`_PopModal`, and by `TModalWindow::SetDefaultFocus(child)` (which also re-focuses if that modal is currently focused). Key events go to the focus window first and bubble up the parent chain (not to siblings) until a handler returns true; the modal's `OnKeyDown` is where hotkeys live, and `OnChar` Enter/Escape. In `ui_*.lua` text-entry dialogs the Lua calls `SetFocus("name")` after building.

### 6.6 What the engine does per iteration (summary)

1. Platform idle tasks (`TTaskList::DoAll`, ctx 0): `TScript::Animate` (resumes Main), `BSGTransition` ticks, `TAnimTask`s from the game.
2. Type-1 idle event: `ClearDestroyedWindows`, top modal's own task list, `DispatchMessages`, `DrawStack` (throttled; ctx 1 tasks; draw), `DispatchMessages`.
3. Input events: `DispatchMessages` then routing as above. The script resumes synchronously inside these (button commands) or on the next idle (polling).

## 7. `DisplaySplash`, `StartMovie`, `StopMovie`, `IsMoviePlaying`

`DisplaySplash(movie, image, time)` (ScreenInit line 726). `time` is milliseconds (shipped values 4000/3000/4000, and 1 for "freeze on the title background"; the intro movie passes 0).

1. `name = PushModal("DisplaySplash")`.
2. Read style tags with an empty table: `translate` (default false), `allowInput` (default false), `disableAbort` (default false), `splashoverlay` (default: an empty `Group{}`). The tags come from `gCurrentDefaultStyle`, not from arguments, so a caller can set them by `SetStyle`/globals before the call.
3. `inMovie = StartMovie(movie, translate, allowInput)`. `endTime = GetTimer() + time`.
4. If **not** `inMovie` (empty path, load failure): if `image` is nil/false: return (without `PopModal`: modal leaked **(inference: no PopModal on that path)**). Else build `MakeDialog{ Bitmap{ name="splashimage", image=image, <splashoverlay> } }`, then `UpdateWindow(); Pause(50); UpdateWindow(); Pause(50)` so it paints before the next step (the shipped boot sequence loads content right after).
5. Loop while `inMovie == true or ImageLoaded("splashimage") == true` (i.e. the image is still being decoded/loaded; `ImageLoaded` returns false once the texture is ready... precisely, the loop ends when the image is loaded and no movie plays **(inference)**):
   * `m = WaitForMessage(1)`;
   * if not `inMovie` and `GetTimer() > endTime`: break;
   * if `m`: if `m.type == kCloseWindow`: break; if `m.type == kModalClosed`: if it is this splash's id break else mark `gClosedWindows[m.name]`; if `disableAbort == false` and `m.type == kPressAnyKey`: break;
   * if `inMovie` and `IsMoviePlaying() == false`: break.
6. `StopMovie()` if a movie was started; `PopModal(name)`.

So the splash ends on: timeout (images only), any key/mouse press (unless `disableAbort`), the movie finishing, or a close message. The first two shipped splashes are `.swf` files with a `.png` fallback name; the Flash host is `TFlashHost`.

* `StartMovie(path, translate, allowInput)` (0x9fdf0): empty path returns false. Else `new TFlashHost(path)`, stores it at `TWindowScript+0x5c`, calls `TFlashHost::Start(false, allowInput?)` (the decompile shows the second bool argument going into `Start`; `translate` is not visibly used here **(Unknown)**). On failure deletes the host and returns false. Returns true on success.
* `IsMoviePlaying()` -> `host && host->IsPlaying()`.
* `StopMovie()` -> `host->Stop()`, delete, null the pointer.
* `CloseRequested()` (5.6) stops and deletes the movie then resumes the script.
The reimplementation can stub the Flash player (no SWF support in the new engine) and make `StartMovie` return false so the PNG fallback and timer path run; that is exactly the "movie failed" path above.

## 8. Things the new engine must keep (checklist)

* Single script, single coroutine (`Main`), resumed (a) every tick while waiting, (b) with `(message, command)` when a message/command arrives and the coroutine is waiting, (c) with `(nil, closure)` when C++ needs to run Lua while suspended.
* Messages are delivered to windows first, then to the script; the script gets everything unconsumed, in FIFO order from the C++ queue, and the Lua-side `gMessageQueue` is LIFO.
* `kModalClosed` comes from the modal's **destructor** (deferred by one event), and `DisplayDialog`'s return value is `gReturnValue`.
* Modal chain = parent chain; draw bottom-to-top from the highest opaque full-screen modal; focus and the script window context change on every push/pop.
* First child in the list is topmost; children created later are on top.
* `enabled == visible == 0x100`.
* Style lookup: own table, then `gCurrentDefaultStyle` and its `parent` chain; `MakeDialog` restores the style before `PostCreate`.
* Draw throttling (12 ms / 200 ms), dirty-flag redraw, and deferred window destruction.
* Transition `onend` must run exactly once, including when the target or modal dies.

## Unknown

* (Resolved: the spider bodies. `FitToChildren` = `GetLargest`, `DestroyAllChildren` = `ChildrenDelete`, the post-`_PopModal` pass = `TModalPoppedSpider` (a no-op); the `SetOn` and per-draw flag-reset spiders (`TRadioButtonSpider`, `TWindowClearFlags`) were not re-read here.)
* (Resolved: `FixSize` for the `align` bits and negative sizes, see 1.5 and `design-questions.md` section 12. Only the degenerate far-band-fill and `S < -80000` branches have no known user.)
* The name-to-slot mapping of `EDepth` values 4+, and whether any Lua reaches `SetWindowDepth`.
* What installs the creation observer at `TWindowScript+0x54`.
* The meaning of `TEvent` type 9 and 15 processing in the manager, (the config key that gates the `kPressAnyKey` post is `overlaymouse`; what it means when set is unknown).
* The string constants in `TModalWindow::OnKeyDown` for modifier prefixes (I only saw the resolved `shift-`/`ctrl-`/`alt-` literals in `NormalizeKeyString`), and the two char constants compared in `OnChar` (assumed Enter and Escape).
* `TFlashHost` internals, the effect of `translate`, and how `CloseRequested` is triggered.
* How `.mask.png` and `automergemask` interact with `TTexture` loading.
* Whether `AdoptChild` failure in `InitWindow` leaks the window (decompile suggests yes).
* The append position of `TWindowManager::PostWindowMessage` (FIFO assumed).
* Whether the coroutine really is resumed once per tick while waiting without timeout, or the task clock coalesces ticks: I derived "every tick" from `SetDelay(0, ...)` and `DoTask`; I did not run the binary.
* The Windows build may differ in details (timer, throttling constants); this document is from the Mac binary only.
* `CreateNamedMessage`'s Lua metatable (`pfMessageMetatable`) beyond `type` and `name`.
