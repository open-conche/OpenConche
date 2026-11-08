# Embedded Lua chunks in the Mac binary

Source: `chocolatier-decadence-design.app/Contents/MacOS/chocolatier-decadence-design` in the Chocolatier 3 Mac app bundle (file offsets below). Tool: a workbench script (run on that binary, it writes `<name>.lst` and `<name>.luac` per chunk). Reconstruction of the window prelude: `lua/prelude.lua`. The API these define is described in `docs/re/api-windows.md` section 3 and `docs/re/api-globals.md`.

## 1. Where the chunks live

The scan for `1b 4c 75 61 50` finds exactly three chunks. (A fourth `\x1bLua` hit at 0x15c9ac is not a chunk: it is followed by version byte 0, a plain string.) Each chunk parses to exactly its stated end with no trailing bytes; file offset + 0x1000 = Ghidra/vm address.

| chunk name (debug source) | file offset | length | loaded by |
|---|---|---|---|
| `@pflib/src/core/animationscript.lua` | 0x1d7420 | 0xd93 | `TAnimatedSprite::NewScript` (own Lua state) |
| `@pflib/src/core/particles.lua` | 0x1d8700 | 0x20d2 | `TLuaParticleSystem` |
| `@pflib/src/core/ScreenInit.lua` | 0x1da7e0 | 0x3aa2 | `TWindowScript` constructor (global state) |

The chunks keep full debug info (line numbers, local names, upvalue names), which is what makes a faithful reconstruction possible.

## 2. Header

All three headers are identical (the 12 bytes after the signature, then the test number):

`1b 4c 75 61 | 50 | 00 | 04 | 04 | 04 | 06 08 09 09 | 08 | 41 7d f5 e7 68 93 ... (8-byte double)`

- version 0x50 (Lua 5.0), endianness byte 0 = **big-endian** (PowerPC host)
- sizeof(int) = 4, sizeof(size_t) = 4, sizeof(Instruction) = 4
- instruction fields: op 6 bits, A 8, B 9, C 9 (Bx/sBx = 18 bits, sBx bias 131071): the stock 5.0.2 layout
- lua_Number: 8 bytes, IEEE double, big-endian; test number decodes to 31415926.535897933 (stock `TEST_NUMBER` 3.14159265358979323846E7)
- The function format after the header is stock 5.0.2: source name (empty string for nested protos, which inherit it), line defined, nups, numparams, is_vararg, maxstacksize, line info, locals (name, startpc, endpc), upvalue names, constants (type byte 0 nil, 3 number, 4 string), nested protos, code. Note 5.0.2 order is lines, locals, upvalues, constants+protos, code.

## 3. Opcode numbering

RK operands: an operand `x` in B or C is a register if `x < 250` and constant `x - 250` otherwise (stock `MAXSTACK` = 250, not 256).

The numbering is stock 5.0.2 for opcodes 0 to 15, then **everything from POW onward is shifted up by 4**. Four extra opcodes occupy 16 to 19.

| n | name | n | name | n | name |
|---|---|---|---|---|---|
| 0 | MOVE | 12 | ADD | 25 | EQ |
| 1 | LOADK | 13 | SUB | 26 | LT |
| 2 | LOADBOOL | 14 | MUL | 27 | LE (inferred) |
| 3 | LOADNIL | 15 | DIV | 28 | TEST |
| 4 | GETUPVAL | 16-19 | **unknown extra ops** | 29 | CALL |
| 5 | GETGLOBAL | 20 | POW (inferred) | 30 | TAILCALL |
| 6 | GETTABLE | 21 | UNM | 31 | RETURN |
| 7 | SETGLOBAL | 22 | NOT (inferred) | 32 | FORLOOP |
| 8 | SETUPVAL | 23 | CONCAT | 33 | TFORLOOP (inferred) |
| 9 | SETTABLE | 24 | JMP | 34 | TFORPREP (inferred) |
| 10 | NEWTABLE | | | 35 | SETLIST |
| 11 | SELF | | | 36 | SETLISTO |
| | | | | 37 | CLOSE (inferred) |
| | | | | 38 | CLOSURE |

Operand modes are the stock ones (ABC, ABx for LOADK/GETGLOBAL/SETGLOBAL/SETLIST/SETLISTO/CLOSURE, AsBx for JMP/FORLOOP/TFORPREP).

### How this was established

1. Opcode histogram per chunk: numbers 0 to 15, 21, 23 to 26, 28 to 32, 35, 36, 38 occur; nothing above 38. The relative frequencies already fit stock order shifted by 4 (RETURN 31 is the most common after GETGLOBAL; CALL 29; JMP 24).
2. Type consistency over all 3 chunks (the tool reports violations at the end of every `.lst`; there are zero): GETGLOBAL/SETGLOBAL Bx always indexes a string constant, LOADK/CLOSURE/RK constants are in range, JMP/FORLOOP targets lie inside the function, every EQ/LT is followed by a JMP, CALL's A+B fits maxstacksize, UNM at 21 appears once, in animationscript `fadeout` (`alphadelta = -alpha / numberOfFrames`), which is the only way to make sense of that instruction (A=B=1, followed by DIV).
3. The decisive check: `lua/prelude.lua` was compiled with stock Lua 5.0.2 `luac` (built from `build/_deps/lua502-src`) and compared function by function with the Mac chunk after translating opcode names through the table above. Every prototype of ScreenInit (the main function, 40 top-level functions and the nested closures) matches: identical instruction sequence, operands (including jump offsets), constant tables and order, local variable names, parameter counts, vararg flag, nups and maxstack. That would be impossible unless the mapping is right for every opcode ScreenInit uses (0 to 15, 23 to 26, 28 to 32, 35, 36, 38). Only the 4 extras 16 to 19 and the "inferred" rows above are not exercised.
4. The compiler that produced the chunks is therefore stock 5.0.2 code generation (the same `while` loop inversion, `and`/`or` jump patching, constant ordering) with a renumbered/extended opcode enum. The extra four opcodes are never emitted in these chunks, so the stock compiler did not use them; they may have been added to the VM only (unknown).

## 4. ScreenInit.lua contents

See `lua/prelude.lua` (one comment per function). Summary: constants `k*` (alignment, button kinds, message types, state key strings), globals `LUA_PATH idtable gParent gCurrentDefaultStyle gCurID gBeginGroup gReturnValue gClosedWindows gCommandQueue gMessageQueue gDialogTable`, and the functions `Color FColor Push Pop DoWindow DoDialogCommand MakeDialog Creating SetBeginGroup BeginGroup GetTag SetDefaultStyle SetStyle AppendStyle SetFocus SetDefault DistributeX DistributeY WaitForCloseMessage DoModal DoMainWindow ModalReturn DisplaySplash FlushMessages WaitForMessage Yield ReadFile CloseWindow CustomCreator GetString SetBitmap PopModal SwapToModal DisplayDialog SelectLayer Group FitToChildren Pause SetState RegisterHotkey`. Definition order in the chunk is as in the prelude. Behaviour details and the native functions it relies on are in `api-windows.md`.

Points worth knowing that are visible only at bytecode level:
- Several variables are accidentally global: `typename` (MakeDialog), `divisor` and `d` (DistributeX), `message` (the queued-message branch of WaitForMessage), `gYieldResult`, `gLastMessage`, `inMovie` (DisplaySplash). Scripts could read them; `prelude.lua` keeps them global.
- `DistributeX` and `DistributeY` differ: X uses globals, Y locals.
- `DisplaySplash` is `if cond then while true do ... end end`, not `while cond do`: the loop's condition is evaluated once. Also `if inMovie then time = 50 end` sets a parameter that is never read again.
- `FlushMessages` loops with `repeat ... until not message` and has an empty `if message then end`.
- `Pause` re-queues every message it receives onto `gMessageQueue` (end of the array), and `WaitForMessage` pops from the end, so re-queued messages are replayed last-in first-out.
- `DoModal` calls `DoFitToChildren()`, `DisplayDialog` does not.
- `ReadFile` is `dofile('assets/' .. name)`; relative to the working directory.
- `GetTag` relies on the 5.0 implicit `arg` table for its third parameter.
- The top-level has one dead local (`currentWindow`).

## 5. particles.lua (state opened with `math`)

Constants: `kBlendNormal`=0, `kBlendOpaque`=1, `kBlendAdditiveAlpha`=2, `kBlendSubtractive`=3, `kBlendMultiplicative`=4, `kBlendMode`="blendmode".
Metatable machinery (functions are anonymous, installed with SETTABLE): `ValueMT` with `__lt __le __add __sub __div __mul __unm __index` (arithmetic on "value" objects, which may be numbers or per-particle expressions), a second set of the same operators in `ParticleMemberMT` plus its `Init` and `Anim` fields.
Defined globals, one line each:
- `CreateValue` : wraps a number or expression into a value object.
- `fGreater`, `fGreaterEqual` : comparison helpers.
- `Init(t)`, `Anim(t)` : call `CompileParameters(t)` then `AccumulateAnimStream()` (natives defined by the C++ particle system).
- `CompileParameters(t)` : `table.foreachi` over t calling `CompileParam`, filling a global `sizes`.
- `Spawn(t)` : calls native `CreateParticles(t)` then `Create()`.
- `Value(x)`, `Vec2(a,b)`, `Vec3(a,b,c)`, `Color(...)` : build value objects with `setmetatable(..., ValueMT)`. Note `Color` here replaces ScreenInit's `Color` in the particle state only.
- `SetBlendMode(m)`, `SetTexture(name)` (calls native `_SetTexture`).
- `NewParticleGenerator()` : returns a closure using `ParticlesPerSecond`, `math.*` (this is why the particle state opens `math`); `NewTimeout(t)` : closure-based timer.
- `RequireVelocity`, `SetUpGravity`, `Update`.
- `GUI_Slider`, `GUI_Texture`, `GUI_Select`, `GUI_PickTexture` : editor GUI helpers (used by the particle editor).
Full disassembly: `re/mac/lua/particles.lst`.

## 6. animationscript.lua

Globals initialised: `alpha`=1, `alphadelta`=0, `alphacount`=0 (and a local 16 constant used by `frames`). Functions, per sprite script:
- `delay(n)`, `pause(n)`, `showFrame(...)` : wait via native `WaitLuaNextFrame`.
- `fadeout(n)`, `fadein(n)` : set `alphadelta` to `-alpha/n` and `(1-alpha)/n`, and `alphacount`.
- `setAlpha(a)` : sets `alpha`, calls native `SetAlpha`.
- `frames(...)`, `loop(...)`, `run(...)` : drive frame sequences (run is the main sequencing function, 27 lines of source).
- `die()` -> native `Die`; `halt()` -> `Pause(1, -1)`; `isPlaying(x)` -> native `IsPlaying`; `stop(...)` -> native `Pause`; `random(a,b)` -> native `Random`; `endReferences` : empty.
Full disassembly: `re/mac/lua/animationscript.lst`.

## 7. Per-type default metatables

Verified in the Mac binary (the game's Lua is statically linked; addresses are Mac virtual addresses). Stock 5.0.2 has one `_defaultmeta` for tables and no metatable for anything else; the game's Lua keeps one default metatable per type.

- `global_State` has `TObject defmt[11]` at G+0xa8, indexed by type tag.
- `f_luaopen` (0xb219f) leaves `defmt[0]` (nil) nil and gives `defmt[1..10]` each a fresh empty table, whose own metatable is the `_defaultmeta` sentinel. `luaH_new` (0xb42e4) reads G+0xe8 for new tables' default metatable; that address is `defmt[LUA_TTABLE]` (0xa8 + 5×12 = 0xe4, table pointer at 0xe8), so in the game every table created after `f_luaopen` (the globals table and the registry included) starts with the shared empty `defmt[5]` as its metatable, and `getmetatable({})` returns that table (and the placeholder itself after `setmetatable(t, nil)`, since the game's `lua_getmetatable` has no sentinel check). **Our engine leaves tables stock**, so `getmetatable({})` is nil here. No shipped Lua observes the difference: the only `getmetatable` call in the vanilla assets or Reforged is `community/json.lua:120`, which compares the result with `arrayMarker` and is false in both engines.
- `luaT_gettmbyobj` (0xb6574): tables and userdata do `luaH_getstr(mt, ename)`; every other type returns `&G->defmt[ttype(o)]`, the metatable object itself, for every event. (`luaT_getmetatable`, 0xb65b4, is the lookup of the same table by type.)
- `luaV_gettable` (0xb82a7): for a non-table, `tm = gettmbyobj(TM_INDEX)`; if nil, fall back to `luaT_getmetatable(o)` and index that table; type error only if that is NULL. So only nil raises. `luaV_settable` (0xb6eb1) is the same with `TM_NEWINDEX`: if `tm` is a table, the store goes into it.
- `lua_setdefaultmetatable(L, t)` (0x809c4): `defmt[t] = top` if it is a table; pops. `lua_getdefaultmetatable` is at 0x8095c. `lua_getmetatable` (0x60bde) returns `defmt[type]` for non-table/userdata values.
- The only caller of `lua_setdefaultmetatable` is `luaopen_string` (0xc438c): `luaL_openlib("string", strlib)`, then `setdefaultmetatable(L, LUA_TSTRING)` with the `string` table itself, then a second `openlib` adding `pack` and `unpack`. `lualex` is also in the first list. `string.lualex`, `string.pack` and `string.unpack` are NOT implemented in the engine.
- `TScript::LuaInit` (0x79e50) calls `luaopen_string`, so the sprite and sheet script states have it too.

Visible results: `("x").len == string.len`, `s:len()` works, `("x").foo` is nil, `("x").foo = 1` stores into `string`, `(3).x` is nil, `(true).x` is nil, `print.x` is nil, `getmetatable("x") == string`, `getmetatable(3)` is an empty table, and `"a" .. t` with a `__concat` on `t` raises (the first operand's handler is the string table, not a function; `t .. "a"` works). `nil.x` still raises.

Engine: `cmake/PatchLua.cmake` `patch_lua_defaultmeta` (marker `choco default metatables v2`) adds `defmt` and the two API functions and changes `luaT_gettmbyobj`; stock `luaV_getnotable`/`luaV_settable` then follow the returned table, so `lvm.c` is not touched. `script/LuaCompat.h` `installGameStringMeta` is the `luaopen_string` tail, called by `LuaState` and `SpriteScript`. Tables keep the stock `_defaultmeta`.

## Unknown

- What opcodes 16 to 19 do. They never occur in these chunks. The VM implementation in the binary (the `luaV_execute` equivalent) would settle it and is not analysed here.
- Whether 20 (POW), 22 (NOT), 27 (LE), 33 (TFORLOOP), 34 (TFORPREP), 37 (CLOSE) are exactly as stock; they are placed by the shift-by-4 pattern and are not exercised by any chunk (the 5.0 `for ... in` and `<=` happen not to appear).
- The text of the comments in the original ScreenInit.lua source. The line numbers show large gaps (for example lines 289 to 465 before `SetBeginGroup`), obviously documentation comments. `prelude.lua`'s line numbers are not intended to match.
- Whether `FlushMessages`' empty `if message then end` was an empty branch or had a commented-out body.
- Exact source spacing/idioms that compile identically (for example `0 < n` versus `n > 0`, `not x` versus `x == nil` are distinguishable and were matched; formatting of table constructors is not).
- particles.lua and animationscript.lua were summarised from names and constants only; the semantics of their internal tables (`sizes`, `CompileParam`) are not worked out.
- The compiled-in `.luac` files in `re/mac/lua/` are the raw chunks; loading them in a stock Lua is impossible because of the opcode numbering (and big-endian), which is why the engine needs `prelude.lua` as source.
