# Window / script Lua API (Mac binary `chocolatier-decadence-design`)

Sources: `re/mac/decomp/TWindowScript.c`, `TWindowStyle.c`, `TWindow.c`, `TText.c`, `TButton.c`, `TImage.c`, `TSlider.c`, `TTextEdit.c`, `_free/c.c` (`ChocoThreeMain`), and the embedded Lua prelude. Addresses are Ghidra addresses (file offset + 0x1000). Constructors of window types and the dialog helpers are NOT all native: most are in an embedded Lua chunk (see section 1).

## 1. Where the API comes from

1. `TWindowScript::TWindowScript(lua_State*)` (0x000a2a66) registers about 40 native globals via `lua_pushcclosure` (member-function closures), then calls `AddWindowType` for six built-in types, builds a `pfMessageMetatable` (`__gc`, `__index`), and finally runs an embedded precompiled Lua 5.0 chunk named `gScreenInit` (source name `pflib/src/core/ScreenInit.lua`, 0x3aa2 bytes, at Ghidra 0x1db7e0, file offset 0x1da7e0, big-endian bytecode, 5.0 header with a 5.1-style tagged constant pool and shifted opcodes). I disassembled it; everything in section 3 is read from that bytecode (source line numbers 8 to 1210 are in the chunk's line info).
2. `TWindowScript::AddWindowType(str name, int classId)` (0x000a1ad8) stores name -> class id in an `std::map<str,int>` (this+0x38) and does a `lua_dostring` that defines a global constructor function. The format string is:
   `function %s( table ) Creating( '%s', table ); table.typename='%s'; return function() DoWindow( table ) end end;` (tabs/newline between statements, text as in strings.tsv 0x15d0e4).
   So `Bitmap{...}` does not create a window. It calls the hook `Creating('Bitmap', t)` (default no-op, ScreenInit proto 7), sets `t.typename = 'Bitmap'`, and returns a closure. The window is created only when that closure is run by a parent `MakeDialog` (via `DoDialogCommand`) or by `DoWindow`.
3. `ChocoThreeMain` (0x0001001c) adds game types (section 4) and the globals `SetMood`, `GrabLedger`, `ReleaseLedger`, `Transition`, `kScreenWidth`=800, `kScreenHeight`=600, `kGameWidth`=800, `kGameHeight`=600, `kGameScale`=1, `gVersionString`. It then runs `ui/mainloop.lua`. `TWindowManager::AttachWindowScript` (0x0004e480) also registers `XSellWindow` (TXSellWindow) and one more native whose name I did not resolve.
4. `TScript` itself (TScript.c) is the Lua state wrapper (`Resume`, `RunScript`, `DoLuaString`, `PopString`, `PopColor`, `GetGlobal*`/`SetGlobal*`). It exports no window API. Coroutine plumbing is described in section 5.

## 2. Native globals registered by TWindowScript

"top modal" = `TWindowManager::GetTopModalWindow()`. "Child lookup" = `TWindow::GetChildWindow(top, name, -1)`, a recursive-by-name search in the top modal window's tree. Several functions walk to the next modal in the chain (`*(window+4)`, the parent) until the name is found. Those are marked "walks chain". Signatures come from the registered dispatcher template plus the decompiled bodies.

| Lua name | Native | Args -> returns | Behaviour |
|---|---|---|---|
| `GenericCreate` | `CreateCustomWindow` 0xa2620 | `(table)` -> nothing | Wraps the table, reads `typename` through `TWindowStyle`, looks it up in the type map, calls `TWindow::CreateFromId`, then `InitWindow`. Unknown type name: no window is created and nothing is raised. |
| `PostCreate` | `PostChildCreate` 0xa080a | `(table, typename)` -> nothing | Calls the current window's virtual `PostChildrenInit(style)` (vtable slot +0x18) with a style built from the table. This is the point where `raw:` fit-to-children and size/position are applied. |
| `PopWindow` | 0xa10b2 | `()` | Pops the script's window stack, never below depth 1. |
| `SetWindowName` | 0xa10d8 | `(name)` | Sets the name of the window on top of the script's window stack. |
| `FillRect` | 0xa0c44 | `(rect, color)` | `rect` = `{x1,y1,x2,y2}` (indices 1..4, clamped to 0..~40000 and intersected with the current window's clip). `color` = `{r,g,b,a}` floats 0..1 (indices 1..4). Pushes a clip, fills, pops. No Lua draw callback exists in this engine (see `design-questions.md` section 11); the older "only valid during a draw callback" remark was a guess. |
| `GetTimer` | `TPlatform::Timer` 0x59ce6 (bound with the `TPlatform` instance) | `()` -> number | Milliseconds since process start (`TPlatformData` construction), clamped to `[0, 4294967295]`. Same function as `CurrentTime`. See `design-questions.md` section 4. |
| `GetConfig`, `IsEnabled` | static `TPlatform::GetConfig` 0x5a1f0 and `TPlatform::IsEnabled` 0x1634f0 | `(key, default)` -> string; `(key)` -> bool | `GetConfig` returns the stored string or the default when the key is absent. `IsEnabled(key)` is `to_int(GetConfig(key, "")) != 0`. Both are registered in the `TWindowScript` constructor (0xa2a66), not by a window. |
| `DoSelectLayer` | `SelectLayer` 0xa08d0 | `(int layer)` | If the current script window is a `TLayeredWindow`, calls `SetCurrentLayer(layer)`. `kAllLayers` = -1. |
| `EnableWindow` | 0xa18a8 | `(name, bool enable)` -> bool | Walks chain. Found: sets/clears flag 0x100 (enabled), for buttons also calls a virtual (+0x4c, likely "reset hover/pressed"), invalidates the screen, returns true. Not found: false. |
| `GetButtonToggleState` | 0xa1994 | `(name)` -> bool | Walks chain. Returns bit 0 of the button's state byte (+0x3d). Non-button or not found: false. |
| `SetButtonToggleState` | 0xa00e6 | `(name, bool)` | Walks chain. `TButton::SetOn(bool)` if the child is a button. Silent otherwise. |
| `RealSetFocus` | `SetFocus` 0xa085a | `(name)` | Child lookup under the script's current window, sets the manager's focus window (+0x38) and calls the new window's `OnSetFocus(old)` (vtable +0x68). No null check on the lookup in the focus assignment, so a missing name clears focus. |
| `UpdateWindow` | 0xa00d2 | `()` | `TWindowManager::InvalidateScreen`. |
| `StringToClipboard` | `TPlatform` | `(str)` | Platform clipboard. |
| `PushModal` | 0x9fc6e | `(scriptName)` -> string id | Allocates a `TModalWindow`, stores `scriptName` in its name field (+0x20), pushes it on the window manager. Returns the id formatted `"%8x"` of the window's id field (+0x28). This string is what `WaitForCloseMessage(id)` waits for. |
| `ClearModalChildren` | 0x9fc12 | `()` | On the top modal: destroys tasks and all children, size and position to 0. Used by `SwapToModal`. |
| `GetWindowDepth` | 0xa05ca | `()` -> int | Count of modals in the chain from top (0 if none). |
| `DoPopModal` | `TWindowManager::PopModal(str)` | `(str returnValue)` | Pops the top modal. Argument is the return value string (see `PopModal` wrapper). |
| `HandleGetMessage` | 0xa04b6 | `(delay?)` -> yields | Core of blocking waits. With 1 arg: `TAnimTask::SetDelay(arg, ...)` (timeout); with none: `TAnimTask::Pause`. Sets script state (this+0x50) to 3 (waiting for message), pushes global `gScreenInit`-registered table value and `lua_yield`s. Resumed by `OnMessage` (section 5). Returns to Lua: `(message, command)` pair, see `WaitForMessage`. |
| `CreateNamedMessage` | 0xa13f8 | `(int type, str name)` -> message | New `TMessage` with `type` and `name`. |
| `CreateCloseMessage` | 0xa1320 | `()` -> message | Type 1 (`kCloseWindow`), name = `"%8x"` of the top modal's id, so it addresses the current modal. |
| `PostMessage` | `PostWindowMessage` 0xa2248 | `(message)` | Copies type/name/extra and enqueues on `TWindowManager::PostWindowMessage`. Delivery is later, to listeners and the target modal's script (section 5). |
| `PostMessageToParent` | 0xa287a | `(message)` | As `PostMessage`, but the extra field (+0xc) is set to the top modal's parent id so only the parent receives it. |
| `_GetString` | `LookupString` 0x9ff1e | `(id,p1,p2,p3,p4,p5)` -> string | If `id` starts with `#` returns `id` minus the `#`. Otherwise `TStringTable::GetString(id, p1..p5)` from the platform's string table (substitutions). Lua wrapper `GetString` pads missing params with `''`. |
| `StartMovie` | 0x9fdf0 | `(path, bool, bool)` -> bool | Creates a `TFlashHost`, starts it (second arg of Start is `false`, third is the script's third arg). Empty path: false. Start failure: deletes the host, false. |
| `IsMoviePlaying` | 0x9feb6 | `()` -> bool | |
| `StopMovie` | 0x9fede | `()` | Stops and frees the flash host. |
| `ImageLoaded` | 0xa0af8 | `(name)` -> bool | Child lookup on top modal. True if it is a `TImage` whose texture pointer is non-null. Used to wait for an async image. |
| `DestroyWindow` | 0xa0920 | `(name)` | Child lookup from the script's current window. Calls the parent's virtual (+0x28, remove child) and `SafeDestroyWindow` (deferred destroy). |
| `SetPosition` | 0xa09a2 | `(name, x, y)` | Top modal child lookup. `x`,`y` go through `FixSize` (so `kCenter`, negative offsets and `kMax` work), then `SetWindowPos`. |
| `TagHotKey` | 0xa01b0 | `(label, key, ...)` -> string | Underline the hotkey char in a label with `<u>...</u>`. Implementation looks off by one in the decompile; treated as unused in Reforged. |
| `StripKeyFromLabel` | 0xa05f8 | `(label)` -> `(label, key)` | Finds `&`, removes it, returns the label and the following char as hotkey (or empty). |
| `SetCommand` | 0xa1614 | `(buttonName, function)` | Child lookup on top modal; if a `TButton`, installs a Lua-function action (stored in the registry). |
| `GetLabel` | 0xa14a2 | `(name)` -> string | No modal: `""`. Missing: `name.."_is_not_found"`. `TText`: its text. `TButton`: its label. Otherwise `name.."_is_not_a_TText_window_or_TButton"`. |
| `SetLabel` | 0xa179e | `(name, text)` | `TText` -> `SetText`; `TButton` -> `SetLabel`. Silent when not found. |
| `_SetBitmap` | `SetBitmap` 0xa1142 | `(name, imagePath, scale)` | `TImage` child on top modal; loads `TTexture::Get(path)` and `TImage::SetTexture(tex, scale)`. |
| `_RegisterHotkey` | 0x9fd3a | `(hotkey, buttonName)` | Ignored if hotkey is empty or no modal; else calls the top modal's virtual (+0x74) `RegisterHotkey(key, button)`. |
| `FitToChildren` (native `DoFitToChildren`) | 0xa05ac | `()` | `TWindow::FitToChildren` on the script's current window. |
| `EnableStringTable` | 0x9fd24 | (C++ only) | Toggles whether `TText::Init` localises labels; not registered to Lua. |

## 3. Lua-level API from the embedded prelude (`gScreenInit`)

Decoded from bytecode. Argument names are the debug locals. "Yields" marks calls that suspend the script coroutine.

### Constants
`kHAlignLeft`=0, `kHAlignCenter`=1, `kHAlignRight`=2, `kVAlignTop`=0, `kVAlignCenter`=4, `kVAlignBottom`=8, `kPush`=0, `kToggle`=1, `kRadio`=2, `kAllLayers`=-1, `kCenter`=80000, `kMax`=160000, `kDefault`=128, `kPushButtonAlignment`=kHAlignCenter+kVAlignCenter, `kRadioButtonAlignment`=kHAlignLeft+kVAlignCenter, `kToggleButtonAlignment`=kHAlignCenter+kVAlignCenter. Message types: `kGeneric`=0, `kCloseWindow`=1, `kDefaultAction`=2, `kButtonPress`=3, `kPressAnyKey`=4, `kQuitNow`=5, `kModalClosed`=6, `kTextEditChanged`=7, `kCommandOnly`=8, `kSliderValChanged`=9, `kSliderMouseUp`=10, `kSliderPageUp`=11, `kSliderPageDown`=12, `kUserMessageBase`=1000. State keys: `kCmdLine`='cmdline', `kLevel`, `kSubLevel`, `kPause`, `kMute`, `kBreakNow`, `kScore`, `kGameMode`, `kGameOver`, `kNoMoreContent`, `kDownloadButton`, `kUpsellFrequency`, `kRestartButton`, `kSystemMode`, `kMaxLevel`, `kCheatMode`, `kComputerId`, `kInstallKey`, `kGameName`, `kGameVersion`='version', `kEncryptionKey`, `kHiscoreLocalOnly`='hiscorelocal', `kHiscoreAnonymous`='hiscoreanon' (the k-name is the lowercase string of the same name). Globals initialised: `LUA_PATH='?;../?;../../?'`, `idtable={}`, `gParent={}`, `gCurrentDefaultStyle={}`, `gCurID=100`, `gBeginGroup=0`, `gReturnValue=nil`, `gClosedWindows={}`, `gCommandQueue={}`, `gMessageQueue={}`, `gDialogTable`. (Whether `kCenter` etc. numeric values equal these exact numbers: yes, read from the constants; the 40000/80000/160000 thresholds in `TWindow::FixSize` agree.)

### Functions
| Function | Args | Returns | Semantics |
|---|---|---|---|
| `Color(r,g,b,a)` | 0..255 numbers, `a` optional (default 1) | `{r/255,g/255,b/255,a/255}` or alpha 1 | |
| `FColor(r,g,b,a)` | floats 0..1 | `{r,g,b,a}`, alpha default 1 | |
| `Push(window)` / `Pop()` | | | `table.insert(gParent,1,window)` / `table.remove(gParent,1)`. |
| `DoWindow(window)` | table | | `window.beginGroup=gBeginGroup; gBeginGroup=0; GenericCreate(window); MakeDialog(window); PopWindow()`. This is the closure body returned by every typed constructor. |
| `DoDialogCommand(i, cmd)` | | | Calls `cmd()`. |
| `MakeDialog(dialogcommands)` | table whose array part is child closures | | Saves `gCurrentDefaultStyle`. If `t.name` is non-empty, `SetWindowName(t.name)`. `table.foreachi(t, DoDialogCommand)` creates the children in order. Sets global `typename = t.typename`. Restores the default style. If `typename` is set, calls `PostCreate(t, typename)`. |
| `Creating(type, table)` | | | Hook, empty by default. Overridable. |
| `SetBeginGroup(style)` | | | Sets `gBeginGroup=1`. `BeginGroup()` returns a closure that calls it, so `BeginGroup()` placed in a child list marks the next window as a group start. |
| `GetTag(tab, tag, ...)` | | value or nil | `tab[tag]`; else (first call only, no extra arg) `GetTag(gCurrentDefaultStyle, tag, true)`; with the extra arg it follows `tab.parent`. This is the Lua twin of `TWindowStyle::_GetTag`. |
| `SetDefaultStyle(style)` | table | | Assigns `gCurrentDefaultStyle`. |
| `SetStyle(style)` | table | closure | The closure calls `SetDefaultStyle(style)` when run as a child of a group. Place it in a child list to change style for the following siblings. |
| `AppendStyle(style)` | table | closure | Closure sets `style.parent = gCurrentDefaultStyle` then `SetDefaultStyle(style)`, so new tags inherit from the previous style. |
| `SetFocus(name)` | | | `RealSetFocus(name)`. |
| `SetDefault(default)` | | closure | Returns an empty closure (parsed and ignored here). |
| `DistributeX(n)` / `DistributeY(n)` | int | table | Array of `n` positions `i/(n+1)*gDialogWidth` (or `gDialogHeight`), i = 1..n. `gDialogWidth/Height` are set by `InitWindow` to the new window's size right after creation. |
| `Group(t)` | table | closure | Closure runs `MakeDialog(t)` (no window of its own). |
| `FitToChildren()` | | closure | Closure calls `DoFitToChildren()`. |
| `SelectLayer(layer)` | | closure | Closure calls `DoSelectLayer(layer)`. |
| `GetString(id,p1..p5)` | | string | Missing params become `''`, then `_GetString`. |
| `SetBitmap(name,image,scale)` | | | scale default 1, then `_SetBitmap`. |
| `ReadFile(fileToRead)` | | | `dofile('assets/'..fileToRead)` (tail call). The dialog script is executed in the new modal's context, calling the constructors. |
| `DoModal(fileToRead)` | | **yields**; returns `gReturnValue` | `id=PushModal(f); ReadFile(f); DoFitToChildren(); WaitForCloseMessage(id); r=gReturnValue; gReturnValue=nil; return r`. |
| `DisplayDialog(t)` | table, `t[1]` script path | **yields**; returns `gReturnValue` | If `t[1]` is nil or `''`: `DebugOut('DisplayDialog called with empty script; aborting')` and return nothing. Else `PushModal`, sets `gDialogTable=t`, `ReadFile`, `WaitForCloseMessage`, returns and clears `gReturnValue`. Dialog scripts read their parameters from `gDialogTable`. Note: no `DoFitToChildren()` here, unlike `DoModal`. |
| `DoMainWindow(fileToRead)` | | **yields**; returns nothing | `PushModal`; remembers `d=GetWindowDepth()`; `ReadFile`; `DoFitToChildren`; loops `WaitForMessage()` (setting `gLastMessage`) until a message arrives and `GetWindowDepth() < d`. Used for the main window that stays in place while children come and go. |
| `WaitForCloseMessage(id)` | string id from `PushModal` | **yields** | Loop: `m=WaitForMessage()`; `gLastMessage=m`; ends when `m.type==kModalClosed and m.name==id`. Marks any other `kModalClosed` name in `gClosedWindows[name]=true`. Also returns immediately if `gClosedWindows[id]` was already set (then clears it). |
| `ModalReturn(value)` | | | `gReturnValue=value`. |
| `CloseWindow(param)` | optional value | | `PostMessage(CreateCloseMessage())`; if `param` is truthy `ModalReturn(param)`. |
| `PopModal(r)` | | | `DoPopModal(r)`. |
| `SwapToModal(name)` | script path | | `ClearModalChildren(); ReadFile(name); DoFitToChildren()` reusing the top modal. |
| `WaitForMessage(delay, deferQueue)` | delay in timer units, optional; `deferQueue` bool | **yields**; returns the message table (or nil on timeout) | If `deferQueue` is falsy and `gMessageQueue` has queued messages, pops and returns the last one immediately (`table.getn` index, `table.remove` pops the end) with no yield. Otherwise calls `HandleGetMessage(delay)` which yields. On resume it receives `(message, command)`. If `command` is non-nil it is executed (`gYieldResult=command()`), else if `gCommandQueue` is non-empty its last entry is removed and called. Returns the first value of the resume (`message`). |
| `Yield()` | | | `coroutine.yield()` (tail call). |
| `FlushMessages()` | | | Repeatedly `HandleGetMessage(1)` until it returns nothing, then clears `gCommandQueue` and `gMessageQueue`. |
| `Pause(time, waitForKey)` | time in `GetTimer` units, bool | **yields** | `endTime=GetTimer()+time`; loops `WaitForMessage(endTime-GetTimer(), true)`; ends on timeout (nil). Received messages are re-inserted into `gMessageQueue` (so they are not lost), except that if `waitForKey` and the message type is `kPressAnyKey` it returns early. |
| `DisplaySplash(splashMovie, splashGraphic, time)` | | **yields** | Pushes a modal named `DisplaySplash`; reads tags `translate`, `allowInput`, `disableAbort` from `{}` via `GetTag` (default false); `StartMovie(movie, translate, allowInput)`. Without a movie, builds a `Bitmap{name='splashimage', image=splashGraphic}` group, `UpdateWindow`, `Pause(50)` x2. Loops until the movie ends, the image loaded (`ImageLoaded('splashimage')`) plus timeout, a `kCloseWindow`, a matching `kModalClosed`, or (unless `disableAbort`) `kPressAnyKey`. Stops the movie and `PopModal('DisplaySplash')`. |
| `CustomCreator(s)` | | | Empty stub. |
| `SetState(key, value)` | | | Booleans become 1/0, then `_SetState(tostring(key), tostring(value))` (native defined elsewhere). |
| `RegisterHotkey(hotkey, buttonName)` | | | Calls `_RegisterHotkey` only if both are truthy. |

`SwapToModal`, `PushModal`, `PopWindow` etc. are therefore either direct natives (`PushModal`, `ClearModalChildren`, `PopWindow`, `EnableWindow`, `GetButtonToggleState`, `SetButtonToggleState`, `SetLabel`, `GetLabel`, `PostMessage`, `CreateNamedMessage`) or the prelude wrappers above (`SwapToModal`, `DisplayDialog`, `MakeDialog`, `SetStyle`, `AppendStyle`, `SetBitmap`, `SelectLayer`, `GetTag`, `BeginGroup`, `CloseWindow`, `RegisterHotkey`). The prelude defines `SetBitmap`; the native is `_SetBitmap`.

### Message objects
`pfMessageMetatable` `__index` (`IndexMessage` 0x9fabc) exposes exactly two fields: `type` (number) and `name` (string). Any other key is nil. `__gc` calls the C++ delete. Created by `CreateNamedMessage`/`CreateCloseMessage` and delivered to scripts by `OnMessage`.

## 4. Window DSL constructors

All constructors take one table. Common mechanics (all types): read via `TWindowStyle`, so every key is looked up first in the table itself, then in `gCurrentDefaultStyle`, then that style's `parent` chain (`TWindowStyle::_GetTag`, 0x6f07c). Numbers via `lua_tonumber`; bools accept boolean or non-zero number (strings warn and give false); colours via `TScript::PopColor`; `GetTable` for nested tables (e.g. `font`). Children (array part, positional entries) are closures run by `MakeDialog`. `name` is applied by `MakeDialog` through `SetWindowName` and also read by `TWindow::Init` (default `""`).

Keys common to every window (`TWindow::Init` 0x4c616, `SizeAndPositionFromStyle` 0x4bff4, `PostChildrenInit` 0x4c594):
- `name` (string, default ""), `x` (0), `y` (0), `w` (current width, 0), `h` (current height, 0), `align` (number, default 0; bit 0 and bit 2 select how `x` and `y` anchor: if `align == -1` it is treated as 0). `raw` (bool, default false): when true `PostChildrenInit` calls `FitToChildren` first. Size/position are applied in `PostChildrenInit`, after children exist, which is why windows can size to children.
- Special values: `kCenter` (80000) added to a coordinate centres it, `kMax` (160000) stretches to the parent edge, negative values measure from the far edge, values above 40000 are treated as special markers. A window that ends up with 0 size is rejected in `SizeAndPositionFromStyle`. Detailed arithmetic lives in `TWindow::FixSize` (0x4a652); the exact handling of combinations is in the window-internals document, not here.
- `typename` is set by the constructor wrapper.

Per type (class id registered with `AddWindowType`):

| Constructor | Class | Keys read (beyond common) |
|---|---|---|
| `Window` | `TWindow` | common only. |
| `Group` | not a window | Plain Lua function (section 3). Takes a table; children only. |
| `Text` | `TText` (0x55550) | `font` (table: index 1 string font name, default `__DEFAULT`; index 2 point size, default 16 then 12 if 0; index 3 colour, default white) , `name`, `label` (string; leading `#` means literal, otherwise looked up in the string table unless it is all digits/dots; string tables can be disabled by `EnableStringTable`), `defflags` (default 0), `flags` (default `defflags`), `padding` (0), `rotation` (0), `rotationOriginX`, `rotationOriginY`, `rotationAlign` (0). Flags combine `kHAlign*`, `kVAlign*` and wrap bits (exact extra bits not decoded). A size of (0,0) logs `ERROR: Attempting to create text window of size(0,0)` and creates no text. Also accepted by the Lua layer (read elsewhere, not in `Init`): `color`, `font` sub-keys via `TLuaTable::GetString/GetNumber/GetColor`. |
| `Bitmap` | `TImage` (0x866e2) | `image` (path), `scale`, `mipmap` (bool), `hflip`, `vflip`, `rotate`. Exact defaults and `image` reading are in the image loader; `image` string key confirmed from Lua usage (`DisplaySplash`). |
| `Button` | `TButton` (0x941b8) | `graphics` (nested table of state images), `mask`, `scale`, `sendToParent`, `beginGroup` (set by `BeginGroup()`), `close` (closes the window on click), `default`, `cancel`, `tooltip`, plus `label`, `command`, `style`-type keys (`kPush`/`kToggle`/`kRadio`) whose exact key names I could not resolve from the dump. |
| `Slider` | `TSlider` (0x89b7a) | `w`, `h` (orientation only), `hslider`, `railtop`, `railmid`, `railbot`, `sliderimage`, `sliderrollimage`, `name`, `scale` (1.0), `yoffset` (0), `value` (0). No range keys. |
| `TextEdit` | `TCustomTextEdit` (0x205ae; `Main` re-registers the name over `TTextEdit`) | `clearinitial`, `enablewindow`, `onkey`, `widthlimited`, then `password`, `length` (10), `ignore`, `utf8`, then every `Text` key. |
| `Rollover` | game class (0x34338) | `xOffset`, `yOffset` (0), `xAbsolute`, `yAbsolute` (-1), `contents` (a string of Lua source run on every hover, injected into the main coroutine), `command` (function), plus base keys; always fits to children. See `design-questions.md` section 10. |
| `BitmapTint` | `TBitmapTint` (0x3bafa, is a `TImage`) | `image`, `mask`, `scale`, `alpha`, `mipmap`, `hflip` (also `vflip`, `rotate` via `TImage`), `tint` (table of four numbers, default 1), `dx`, `dy` (default -9999), `w`, `h` (default -1); also `BTSetTint`, `BTSetOffset`, `BTSetBitmap`, `BTSetFlip` natives. |
| `Rectangle` | `TRectangle` (0x3c45c) | `color` (default white), `alpha` (default the colour's alpha), plus base keys. |
| `BSGWindow` | `TBSGWindow` (0x6516) | `swallowmouse` (false), `frame` (`.xml` appended), `color` (default (1,1,1,0)), `fill` (default true), plus base keys. |
| `CharWindow` | `Character` (0x78ba, a `TBSGWindow`) | BSGWindow keys, `name` (character), `alpha` (1), `scale` (1), `happiness` (default 50). Used in `ui_market.lua`, `ui_quest_offer.lua`. |
| `TightText` | `TTightText` (0x38ca8) | every `Text` key, then size forced to 800x600, then `pad` (default 3); `PostChildrenInit` shrinks to the text bounds plus `pad`. |
| `XSellWindow` | `TXSellWindow` (0x9eafe; registered by `AttachWindowScript`) | Base keys only. The constructor registers the globals `SetupXSell` and `LaunchGameLink`. Unused by the shipped Lua. |
| `OptionsWindow`, `HiscoreWindow`, `CreditsWindow`, `MapWindow`, `PortWindow`, `ChocolateFactory`, `CoffeeFactory`, `CratesGame`, `CurveEditor`, `DookieDropper`, `FireworksWindow`, `RecipeCreator`, `RecipeDesigner`, `SignEditor`, `SlotMachine`, `SubmitWindow` (`TServerSubmit::Register`) | game windows | One global constructor each; keys specific to each game. |
| `Dialog`, `Popup` | | There is no `Dialog` constructor. "Dialogs" are modals created with `DoModal`/`DisplayDialog` that run a script. `TDialog.c` exists in the binary but nothing registers it as a Lua type. |

Note: `Creating(type, table)` hook is called before `typename` is assigned and before any child closure runs.

## 5. Control flow, blocking and messages

- Each `TWindowScript` is a Lua coroutine wrapper and a `TAnimTask`. Script state at this+0x50: 1 initial, 3 = waiting in `HandleGetMessage`. Only `HandleGetMessage` (hence `WaitForMessage`) and `Yield` (plain `coroutine.yield`) yield. Everything else that blocks (`DoModal`, `DisplayDialog`, `DoMainWindow`, `Pause`, `WaitForCloseMessage`, `DisplaySplash`, `FlushMessages`) does so by looping on `WaitForMessage`.
- `HandleGetMessage(delay)`: with a numeric argument sets a timeout (`TAnimTask::SetDelay(delay, ...)`), else pauses the task until a message wakes it. On timeout the script is resumed with nil. Arguments to the resume: `(message, command)`.
- `TWindowScript::OnMessage(msg, fn)` (0xa1c08): if the coroutine is NOT active, the message and optional command function are appended to the global Lua tables `gMessageQueue` (via `gMessageToQueue`) and `gCommandQueue` (via `gMessageCommand`), where the next `WaitForMessage` will find them. If the coroutine is active but not waiting (state != 3), a button command (`fn`) is run immediately through a protected call with `_ALERT` as error handler and nothing resumes. If state == 3, the message (or a synthetic message of type 8 = `kCommandOnly` when `msg` is null) and the command function are pushed and the coroutine is resumed with 1 or 2 values. If the resume returns an error, state resets to 0; a normal return leaves it at 3.
- `TWindowScript::InjectFunction` (0xa0566): pushes nil under the top value and resumes with 2 values, used to run a function inside the script.
- `CloseRequested` (0xa21b2): if a movie is running, stops it and resumes the script with 0 args.
- Button commands: `TButton::LuaAction` stores a Lua function; click triggers `OnMessage(msg, fn)` with `msg` a type-8 message named `LuaAction:<button>`; hence `command` functions run in the script context at the next wait, or immediately when not waiting.

## 6. Reforged usage (selection)

Counts and file lists for each global are in `docs/re/lua-globals-reforged.tsv`. In short: `Text` (587 uses), `Color`, `Bitmap`, `Button`, `Group`, `Rollover`, `BitmapTint`, `CharWindow`, `kVAlignCenter` appear in nearly every `ui/*.lua` and `dev/*.lua`; `MakeDialog`, `DoModal`, `DisplayDialog`, `SwapToModal`, `CloseWindow`, `SetLabel`, `GetLabel`, `EnableWindow`, `GetTag`, `SetStyle`, `AppendStyle`, `FillWindow` and `SelectLayer` are used by the `ui/` files starting with `style.lua`, `helpers.lua`, `mainmenu.lua`, `ui_generic_yn.lua`, `ui_options.lua`. I did not cross-check each call site's argument shape.

## Unknown

- `FillWindow`, `SetLayeredLabel`, `SetWindowPos`, `SetAlpha`, `HideWindow` and similar names in the Reforged globals list are not in the Mac TWindowScript natives or ScreenInit prelude. They may be in `RegisterBSGLuaHelpers` (0x4494, helpers analyst) or are Reforged additions; not verified.
- Defaults of `Text` flags bits and of `Image.scale`.
- Opcode names for Lua opcodes 16 to 22 in the custom bytecode (not used by ScreenInit), so the decode of the prelude assumes only the opcodes that occurred. A mistake in TEST polarity would only affect the `GetTag` and `DisplaySplash` branch details.
- `DisplayDialog` behaviour when a dialog script yields before pushing children, and message routing details of `TWindowManager::PostWindowMessage`.
