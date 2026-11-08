# Design: script host

The part of the engine that owns the one Lua 5.0.2 state, loads the game's Lua from `--assets`, registers the
natives, runs `Main` as a coroutine from the frame loop and delivers messages to it. The window tree, modal stack and
message dispatch to windows are in [window-system.md](window-system.md); how a game plugs in is in
[engine-and-games.md](engine-and-games.md). The facts used here come from
`docs/re/window-system.md` (cited as WS §n), `docs/re/api-windows.md`, `docs/re/api-globals.md`,
`docs/re/startup-and-assets.md` (SA §n) and `docs/re/embedded-lua.md`.

## 1. Behaviour the scripts depend on

- **One state, one coroutine.** There is one `TWindowScript` for the whole game, not one per window or dialog file
  (WS 5.1). The game's entry script (`ui/mainloop.lua` for the Chocolatier games, `GameProfile::mainloop`) runs as a
  plain chunk and returns `Main`, which is then resumed as the coroutine body straight away and runs until it first
  yields (WS 5.2). Dialogs (`DoModal`, `DisplayDialog`, `SwapToModal`) run their file with `dofile` *inside* the same
  coroutine. Each dialog's state is its `ModalWindow`, the script window stack and the globals `gDialogTable` and
  `gReturnValue`. Nothing else is kept per dialog.
- **Libraries:** base (which includes `coroutine` in 5.0), string and table only. No `math`, `io`, `os`, `debug` or
  `loadlib` (SA 1.7). Reforged relies on `math` being absent (`helpers.lua:512`).
- **Only two yield points:** the native `HandleGetMessage` (called by the prelude's `WaitForMessage`) and
  `coroutine.yield` (the prelude's `Yield`, which nothing in Reforged calls). `DoModal`, `DisplayDialog`,
  `DoMainWindow`, `Pause`, `WaitForCloseMessage`, `DisplaySplash` and `FlushMessages` are prelude Lua that loop on
  `WaitForMessage`.
- **Waiting is polling.** `WaitForMessage` always passes its first argument, so `HandleGetMessage` always sees one
  argument. A nil timeout becomes `tonumber(nil) = 0`, so the coroutine is resumed every tick with no values. A number
  is a timeout in ms on the same clock as `GetTimer` (WS 5.3, 5.4). The menu's `QueueCommand` fade-in depends on this:
  `gCommandQueue` is only drained by a `WaitForMessage` that resumes without a command.
- **Resume values:** `(message, command)` for a message or button command, `(nil, closure)` for injected code (WS 5.2),
  nothing on timeout.
- **`OnMessage` branches** (WS 4.3 step 6), kept exactly (`ScriptHost::deliver`):
  1. If the coroutine is not suspended (not started, running, or dead): append the message to `gMessageQueue` and the
     command to `gCommandQueue`, with the same size bookkeeping as `table.insert`.
  2. If it is suspended but not in `HandleGetMessage` (a plain `Yield`): drop the message, and `pcall` the command on
     the main state.
  3. If it is suspended in `HandleGetMessage`: resume it with `(msg or kCommandOnly "", fn?)`.
- **Code run while the coroutine is suspended**, such as `AskQuit()` on a close request or
  `if Player then Player:SaveGame() end` on exit, is *injected*: the coroutine is resumed with `(nil, closure)`. When it
  is not waiting in `HandleGetMessage` the code is run with `pcall` instead (WS 5.2).
- **Transition `onend`** is a bare `pcall` on the script state, not an injection (WS 6.4). It must not yield. The
  scripts only call `CloseWindow` and `Update` from it.
- **A dialog script's top level cannot yield**, because `dofile` is a C call. Reforged knows this
  (`community_hub.lua:122`). Our `dofile` is a C function too, so it behaves the same: the script gets the stock 5.0
  error. A yield that Lua refuses (inside `pcall`, `dofile`, `table.sort`) is not recorded as a stop of `Main`
  (`canYield` in `ScriptHost.cpp`).
- **Messages** are userdata whose `__index` exposes only `type` (number) and `name` (string); every other key is nil.
  Modal ids are `"%8x"` strings (8 characters wide, padded with spaces).
- **Files:** lookup is case-insensitive through a lowercase map of everything under the asset root (SA 3). A leading
  `assets/` is stripped (`ReadFile` does `dofile('assets/'..f)`). `require` follows stock 5.0.2 semantics on that file
  layer with `LUA_PATH='?;../?;../../?'`, which the prelude sets, and `_LOADED`, plus one deliberate deviation: when
  every template misses and the name has no `.lua` suffix, it retries with `.lua` appended (Reforged's catalogue History
  panel requires bare names; `docs/re/sweep-findings.md` §1). `_LOADED` and `_REQUIREDNAME` keep the original
  name and a `[script] require ... (.lua fallback)` line is logged. `dofile` always re-runs the file. Text and 5.0
  bytecode both load (§4, the Lua patches).
- **Startup** (SA 1, 2): natives, then the prelude, then the constants `kScreenWidth` and the rest plus
  `gVersionString`, then the window types, then the entry script. `settings.xml` goes into the config, `strings.xml`
  into the SDK string table.

Not copied: the original builds XML data as generated Lua source (`SimpleXmlToLua`), which breaks on a `"` in text. We
build the tables directly with the Lua API. Whole files are read into memory, never `fseek(-1)`.

## 2. Types and ownership

All of this is in `src/script/` unless a path says otherwise.

```
App owns, created in this order, destroyed in reverse:
  Platform, platform::Window, gfx::Renderer, gfx::TextureCache
  assets::AssetFS
  script::LuaState          // lua_State*, lua_close in dtor
  ui::WindowManager         // holds LuaRefs (button commands, onend), so must die before LuaState
  script::ScriptContext     // owns ctx.gameState, which dies before the window manager and Lua
  script::ScriptHost        // refs the context; dtor clears wm.setScriptSink
```

- **`LuaState`** owns the `lua_State*` and nothing else. It counts live `LuaRef`s and aborts in its destructor if any
  survive, in release builds too, so an ownership mistake fails loudly.
- **`LuaRef`** is a move-only RAII registry reference (`luaL_ref`/`luaL_unref` on the main state) with
  `push(lua_State*)`. Anything C++ keeps from Lua is held in one: button commands, transition `onend`, the coroutine
  thread, `Rollover` contents. `ui` uses only `LuaRef` and `Style` (window-system §3) and never includes the Lua headers.
- **`ScriptContext`** is a struct of references (`AssetFS&`, `WindowManager&`, `Clock&`, `Rng&`, `Config&`, `StubLog&`,
  `Log&`) plus the app services a native may need (audio, the fullscreen and cursor callbacks, the string table, the
  user store) and the `GameModule*` the context runs. Each native gets it as a light-userdata upvalue, so there are no
  globals. It outlives the state's use because its members do.
- **`GameModule`** (`GameModule.h`) is a game's side of boot: `registerStubs`, `registerNatives`, the two `register*Extras`
  hooks that keep the registration order, `registerWindowTypes`, `registerGlobals`, plus a `GameProfile` of plain data
  (entry script, close-request Lua, exit-save Lua, window title, quirks). `ScriptHost::boot` fails without a module.
- **`ScriptHost`** registers everything, boots, and owns the coroutine. It implements `ui::ScriptSink`, the window
  side's whole view of the script: `deliver` (the `OnMessage` branches), `runInScript` (inject or `pcall`),
  `callProtected` (onend, rollover contents), `runString` (`DoLuaString`: compile, then `runInScript`),
  `setGlobalString` (`gReturnValue` = button name), `withGlobal` (a read-only view of a global table such as `Player`) and
  `userPref`. Its states are `CoState { NotStarted, Running, WaitingMessage, Yielded, Dead }`.

Main runs on a thread made with `lua_newthread`, not on the main state as in the original. This is the same Lua
semantics, but `pcall`s from C++ (`onend`, rollovers) then run on the main state while the coroutine is suspended,
which is clean in 5.0.2. The thread is anchored in the registry through a `LuaRef`.

**Messages** (defined in `ui/Message.h`, see window-system §5): `struct Message { int type; std::string name; WindowId target; }`.
The Lua userdata holds a `Message` by value, so its `__gc` only runs the destructor and points at nothing. We do not
keep Lua identity for posted messages; the scripts only read `.type` and `.name`.

### The binding helper

```cpp
template <auto Fn> void bind(lua_State*, ScriptContext&, const char* name);  // Fn: R(ScriptContext&, Args...)
void bindRaw(lua_State*, ScriptContext&, const char* name, lua_CFunction);   // for the yielding natives and loaders
```

Argument types and their conversion follow the original's lenient LPCD rules. Natives never raise an error for an
argument type:

| C++ parameter | Accepts | Otherwise |
|---|---|---|
| `double`, `float` | number, numeric string (`lua_tonumber`) | 0 and a warning |
| `int` | as above, truncated toward 0 | 0 and a warning |
| `std::uint32_t` | as above, clamped to [0, 2^32-1] | 0 and a warning |
| `bool` | boolean, number (non-zero is true), nil (false) | false and a warning (as in `GetBool`) |
| `std::string` | string, number | `""`; nil gives no warning, other types do |
| `std::optional<T>` | nil or none gives `nullopt`, else as for T | |
| `TableArg` | table; a view of the stack slot that is valid only during the call | invalid view and a warning |
| `LuaRef` | function or table, kept by the native | empty ref and a warning |
| `ColorArg` | `{r,g,b[,a]}` floats, a defaults to 1 | white and a warning |

Returns: `void` gives 0 values; `bool`, numbers, `std::string` and `std::optional<T>` (nil when empty) give 1;
`std::tuple<...>` gives N. A warning is logged once per (native, argument index, Lua type), with the caller's
`file:line` from `lua_getstack(L,1)`. For example:
`[lua-arg] SetLabel arg 2: expected string, got table (ui/ui_options.lua:88)`.

Rules for natives, because Lua 5.0 is built as C and its errors `longjmp`:
1. Never call `lua_error` or `luaL_check*`.
2. Call back into Lua only through `lua_pcall` (the error is logged and the native continues).
3. The helper catches every C++ exception at the boundary, logs `[native-error] Name: what` and returns no values.

The loader replacements (`dofile`, `loadfile`, `require`) are the exception to rule 1: they hold no C++ objects across
a call that can raise, so they raise the stock errors.

An allocation failure inside Lua is treated as fatal. Every name registered through `bind`, `bindRaw` or an alias is
recorded in `ctx.nativeNames`.

## 3. Coroutine and per-frame work

Boot runs the entry script with `pcall`. If the result is a function, it becomes the body of `thread_` and is resumed
at once with 0 arguments, still inside `boot()`, as in the original.

`HandleGetMessage(timeout)` (`bindRaw`): if `lua_gettop == 1`, sets
`wakeAtMs_ = now + max(0, tonumber(arg))` (nan counts as 0), otherwise `wakeAtMs_ = -1` (wait only for a message). It
records `waitFrame_ = frameNo`, marks the pending yield as `WaitingMessage`, pushes `gYieldResult` and returns
`lua_yield(L, 1)`. Our `coroutine.yield` replacement marks `Yielded` and then yields, so the host knows why the
coroutine stopped. When `lua_resume` returns 0 and neither function marked a yield, `Main` has returned: the state
becomes `Dead` and `mainReturned()` is true.

`resume(nargs)` sets `Running`, calls `lua_resume(co_, nargs)`, and on error logs `[lua-error] <msg>` and
`[script] Main died: ...`, counts the error and sets `Dead`. 5.0 leaves no traceback after a failed resume, so a
patched hook (`lua_resumeerrorhook`, §4) takes the traceback of Main's frames while they are still live; it is logged
with the death message and shown by `diag`. A dead `Main` is not restarted. The frame loop, drawing and input carry
on, messages are queued with a log line once, and a close request quits directly.

`tick(now)` advances the script clock, calls `ctx.gameState->tick(now)`, then resumes with no values when
`state_ == WaitingMessage`, `wakeAtMs_ >= 0`, `now >= wakeAtMs_` and `frameNo > waitFrame_`. The last condition means a
wait that starts during frame N times out no earlier than frame N+1, so a script cannot spin inside one frame. A
`Yielded` coroutine is resumed with no values on the next tick (inference: the original task resumes it with
`Resume(0)`).

Frame order, owned by `App.cpp`'s `Session::step` (window-system §6 has the window-side detail):

```
1  ScriptDriver.update(now); pumpWindows()          // clearDestroyed + dispatchMessages
2  per event from window.pollEvents(): routeInput   // pumpWindows, then the input; may resume Main synchronously
3  wm.tickTransitions(now); wm.tickWindowAnimations(now); pumpWindows()
   host.tick(now); pumpWindows()                    // Main resumes after the transitions step
4  audio update; draw
5  screenshot if pending; present; expect-window checks run in step 1 of the next frame
```

A close request (`SDL_EVENT_QUIT` from the user) runs `GameProfile::closeRequest` (`AskQuit()` for Chocolatier 3) with
`runString` while `Main` is alive and the close global exists, and quits directly otherwise. A second close request while the quit dialog is up,
and one that arrives mid-trip, are handled as `App.cpp` describes. `kQuitNow` (window-system §5) and the end of the
script run (`Main` returned) cause the app to exit. On exit, if the game state was touched (`gameStateTouched()`: a
save was loaded or stored), it runs the profile's `exitSave` (`if Player then Player:SaveGame() end` for Chocolatier 3),
so the never-initialised `Player` is not saved. Shutdown then follows the order in §2. The harness's `quit` command stops
the app directly, without going through `SDL_EVENT_QUIT`; otherwise it would open the quit dialog.

## 4. Boot sequence

`ScriptHost::boot`, after `LuaState` has opened the state:

1. `LuaState`: `lua_open`, then `luaopen_base`, `luaopen_string` (followed by `installGameStringMeta`, §7 of
   `docs/re/embedded-lua.md`), `luaopen_table`.
2. `module.registerStubs`: the generated stub table for the game (§6).
3. Replace `dofile`, `loadfile`, `require` and `coroutine.yield` with host C functions (§5, §3).
4. Register `_ALERT` (logs `[lua-error]`), `DebugOut` (logs `[debug]` with all arguments stringified; the original is a
   no-op, and logging helps us) and the message userdata type. `toparams` stays a generated stub.
5. Register the natives over the stubs, in this order: core (`GetTimer`, `CurrentTime`, `RandRange`) and the module's
   core extras, window, content and the module's content extras, text, user, audio, then `module.registerNatives`, then
   `HandleGetMessage`, then `module.registerWindowTypes` (the built-in window types and the game's, through
   `AddWindowType`'s exact Lua template, window-system §3).
6. Load `lua/prelude.lua` and `lua/host.lua`, both embedded into the binary at build time (`cmake/EmbedLua.cmake`) so
   `--assets` holds only game data. The prelude is the original's script layer reconstructed with its function and
   parameter names. `host.lua` has `DumpTable` and the one deliberate change to the prelude: a `DisplaySplash` wrapper
   that returns at once when `splashMovie ~= ""`. That skips the three logos and still shows
   `DisplaySplash("", "image/title_background", 1)`.
7. Read `settings.xml` into `Config` and `strings.xml` into the string table. `hiscorelocal` is set to `1` (a `--config`
   value still wins): there is no online play, so the high-score screen is the local one. Set the constants
   `kScreenWidth=800`, `kScreenHeight=600`, `kGameWidth=800`, `kGameHeight=600`, `kGameScale=1`, then
   `module.registerGlobals`, then `gVersionString = "ver " .. config.version` (Chocolatier 2's `settings.xml` has no
   version, so its profile uses the `gametitle` string instead).
8. Load the entry script, `pcall` it and start `Main`.

Failing to load the prelude, `host.lua` or the entry script is an engine bug: `boot()` returns false. A Lua error while
running the entry script or later is logged and the engine keeps running with `Main` dead.

**Lua patches.** The Lua sources are fetched pinned by SHA-256 and patched at configure time by `cmake/PatchLua.cmake`
(applied to the extracted copy under the build directory, idempotent, never to the tarball). The game's Lua is a
modified 5.0.2 and the scripts need the differences (`docs/re/embedded-lua.md`):
- a `continue` statement (Reforged's `sim/port.lua` uses it);
- hexadecimal numerals as the game's lexer reads them (`community/json.lua` has `[0x80] = 0x20AC`);
- loading of vanilla's 32-bit bytecode on 64-bit hosts, with the game's shifted opcode numbering (chunks written by
  stock-numbered `luac` do not load);
- per-type default metatables, with the string library as the default for strings;
- `lua_resumeerrorhook`, called from `luaD_throw` for an error that ends a coroutine, so Main's traceback survives.

## 5. Asset file system (`src/assets/AssetFS`)

`AssetFS(assetsRoot, userDir)` indexes the game's files once and answers `exists`, `resolve`, `readFile`, `readText`,
`probe`, `listBySuffix` (the original's `BuildFileList`, sorted), `list(dir)` and `writeFile`/`writeText` (`user:` names
only, creating directories).

- The index is a `map<lowercase key, entry>` over every source mounted: the directory under `--assets` first, then a
  vanilla `assets.pfp` archive (`PfpSource`, read whole, `docs/re/pfp.md`) and any `.cad` archives. Earlier sources win
  on equal paths, so loose files override the archive. Separators are `/`; `.svn` and `.git` are skipped.
- `resolve` does, in order:
  1. Normalise `\` to `/`, lowercase, look up the key.
  2. A `::` prefix means a real path relative to the working directory (the original's cwd-relative
     `::assets/settings.xml`); if it does not exist and starts with `assets/`, the rest is looked up in the index.
  3. `user:` is resolved against the user directory on every call.
  4. Otherwise the file is not found; the Lua loaders also retry once with a leading `assets/` stripped.

  Real paths never leave this class except for logging. Writes are refused unless they resolve under `--userdir`.
- Asset extension probing (`.xml .anm .jpg .png .spx .ogg .mesh .lua`) and texture probing (`.png .jpg .jp2`) are
  `probe` on top of the index, used by `gfx` and `assets`, not by Lua file loading.
- Lua loading (`Loaders.cpp`): `loadChunk(name)` reads the whole file and calls `luaL_loadbuffer` with chunk name
  `"@" + name`, skipping a UTF-8 byte order mark and a first line starting with `#` as `luaL_loadfile` does.
  - `dofile(name)`: load, then `lua_call` with multiple returns. On failure it raises the stock message.
  - `loadfile(name)` returns the chunk, or nil and a message.
  - `require` is a line-by-line port of 5.0.2 `luaB_require` (`_LOADED`, `LUA_PATH` templates split on `;` with `?`
    substituted, `_REQUIREDNAME`) with `loadChunk` as its loader, the `.lua` retry from §1 and no environment variable
    fallback, for determinism.

## 6. Stub generator

`tools/gen_stubs.py`, run with `python3 -I`, reads the API tables in `docs/re` and writes one `.inc` per game, which are
committed so the build needs no Python:

| Output | Source |
|---|---|
| `src/games/choc3/generated/Stubs_choc3.inc` | `api-globals.tsv`, `api-windows.tsv` |
| `src/games/choc2/generated/Stubs_choc2.inc` | `api-choc2.tsv` |
| `src/games/choc1/generated/Stubs_choc1.inc` | `api-choc1.tsv` (its `engine` column) |

`gen_stubs.py --check` runs as a CTest and fails when any of them is out of date.

- **Rows that become stubs (Chocolatier 3):** `kind == function` whose source is not a Lua chunk and whose name the
  prelude or `host.lua` does not define. The script skips sources matching `ScreenInit`, `gScreenInit`, `particles.lua`
  and `animationscript`: particle-state and sprite-state functions stay out of the main state. `GetTimer`, `GetConfig`,
  `IsEnabled` and `StringToClipboard`, which the original has but the tables lack, are added by the script.
- **Constructors** become `WINDOW_TYPE` entries (window-system §3), plus a list of game window types registered by
  `ChocoThreeMain` that are not rows (SA 2.4) and `XSellWindow`. Constants and objects are ignored: the prelude and host
  define them.
- **Row kinds in the `.inc`:** `STUB(name, Ret::Kind, args)`, `WINDOW_TYPE(name)` and `API_NAME(name)` everywhere; the
  Chocolatier 2 and 1 files add `ALIAS(name, target)` (bound as the Chocolatier 3 native `target` under another name when
  that target is real, else a logged stub: `registerAliasRows`) and Chocolatier 2 adds `GLOBAL_NUMBER`. The per-game
  tables mark hand-written natives `@native` (an `API_NAME` only).
- **The return kind** is parsed from the `returns` column:
  - `string` gives `""`
  - number words give `0`
  - `bool` gives `false`
  - `table` gives `{}`
  - `(none)` and `nothing` give no values
  - anything else gives nil
- **A stub** is one `lua_CFunction` with the row index as an upvalue (`registerStubRows`). It increments a counter,
  logs `[unimplemented] Name` the first time, and returns its safe value. `--log-unimplemented file` writes
  `name<TAB>count` lines sorted by count at exit, which includes exits through `kQuitNow` and the end of the script run.
- **Real implementations** go in per-module registration functions (`registerCoreNatives`, `registerWindowNatives`, ...,
  and each game's `registerNatives`) that run after the stubs and overwrite the globals. Replacing a stub is one `bind`
  line plus its function; the stub row stays and is overwritten. Tests check that every real native's name appears in
  the API tables or a short allowlist (`AddWindowType`, `dofile`, `loadfile`, `require`), so a typo cannot leave a stub
  in place, and that every `API_NAME` of the Chocolatier 1 and 2 tables is a function after boot.
- **Cross-check:** the generator also reads `docs/re/lua-globals-reforged.tsv` (and the Chocolatier 2 equivalent) and
  prints the names the scripts use that neither the tables nor the prelude define, so they get added before anyone
  guesses.
- Natives that the original registers only when their owning window is constructed (`GetScore`, `StartGame`,
  `PausePortAnimations`...) are registered at boot (§10).

## 7. Determinism

- Headless: the script clock is `frame * 1000 / 60` ms, and `GetTimer` and `CurrentTime` return it as `uint32`.
  Windowed: SDL ticks since the run started.
- `RandRange` and the original's `rand()` uses (crates) go through one `Rng` (`std::mt19937`, with its own `uniform()`
  so seeded runs match across standard libraries). The sprite scripts and the particle effects have separate streams
  seeded from `seed + 1` and `seed + 2`, so they cannot shift the game's rolls. `--seed n` sets it. Headless defaults to
  1; windowed defaults to time-based, and the seed is logged.
- No environment variables are read (`LUA_PATH` included), the file index is sorted, and frame pacing never affects
  logic.

## 8. `expect-window`

`expect-window <name> [timeout-ms]`, with a default timeout of 5000 simulated ms. Each frame (`ScriptDriver::update`)
it asks `ui::WindowQuery::isShown(name)`. The name is searched from the top modal down the chain, pre-order, matching
the modal itself first (WS 1.4), and the window and all its ancestors must have the visible flag. A modal's name is its
script path, so `expect-window ui/mainmenu.lua` works as soon as the modal stack exists. On success it logs
`[script] ok expect-window name (frame N)`. On timeout, or as soon as `WindowQuery::hasScriptError(name)` reports that
the window's script logged a Lua error, it logs `FAIL`, the driver marks itself failed, and the run quits with exit code
1. Without a script host the command fails, and a run that stops while it is pending fails too. `ScriptDriver` gets a
`WindowQuery&` so it does not depend on `ui`.

## 9. Tests

All run headless.
- **Unit tests** (`tests/test_host.cpp`, `tests/test_bind.cpp`): the argument coercion table, warn-once, tuple returns,
  exception containment, `require` caching and `LUA_PATH`, the `.lua` fallback, the yield-across-`dofile` error matching
  stock, `continue`, hex numerals and the default metatables. A tiny `mainloop.lua` from `tests/assets/` covers timeout
  resumes, polling every frame, `Pause(100)` lasting 6 to 7 frames, the `gCommandQueue` drain, an error in `Main` not
  stopping the loop, injection versus `pcall`, the `OnMessage` branches, messages exposing only `type` and `name`,
  movie splashes skipped, and the stub table (each stub returns what its row says, logs once, counts, and renders sorted
  by count). The RNG is checked for determinism per seed. `tests/test_boot_trace.cpp` records the native registration
  order.
- **Driver tests** (`tests/test_script.cpp`): the script format, simulated waits, `expect-window` timeout and failure
  cases.
- **Scripted checks** (`tests/check_*.py`, run by CTest when the game assets are present): `check_boot.py` and
  `check_mainmenu.py` run Reforged headless to `expect-window ui/mainmenu.lua` with no `[lua-error]`; `check_cli.py`
  covers exit codes and the unimplemented-natives file. `gen_stubs.py --check` guards the generated tables.

## 10. Known limits

- **Natives registered early:** natives that the original registers only once their window exists are registered at
  boot. A script doing `if GetScore then` sees one too soon. No script is known to do so.
- **`pairs` order:** iteration over tables keyed by tables or functions depends on addresses. Lua 5.0 hashes pointers,
  so the order can differ between runs. It is not fixed here.
- **Tick rate:** the original resumes `Main` on every idle-loop tick, which can be faster than our per-frame resume.
  Polling loops that count iterations instead of time would run slower. None
  are known.
- **Unknowns:** what resumes a plain `coroutine.yield` and with which values (the host resumes with none); the body of
  the original's `toparams` (a stub here, returning nil); the units and origin of `CurrentTime` (taken as ms, like
  `GetTimer`).
- **Not provided:** per-dialog Lua states, sandboxing, hot reload, the `debug` library, Lua identity for posted messages,
  and lazy native registration.
