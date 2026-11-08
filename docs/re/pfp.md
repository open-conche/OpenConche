# assets.pfp and its Lua bytecode

Confirmed on the Chocolatier 3 Windows install's `assets/assets.pfp` (compiled 2009-01-27, 15,401,100 bytes; the figures below were taken on an older
2009-01-16 installer build of 15,400,585 bytes that differed only in `sim/player.lua` and `sim/building.lua`).

## Archive

- `PFPK`, u32 count (1509), then per file: u8 name length, name bytes, u32 offset, u32 size, little-endian. The directory ends at byte 52,743, which is also the first file's offset; data follows contiguously and the last entry ends exactly at end of file. Nothing is compressed.
- Names: all already lowercase, `/`-separated, no backslashes, no duplicates (case-insensitively), none with a leading `./`. 1508 contain a directory part; `xsellstyle.lua` is at the top level. `AssetFS::normalize` leaves every name unchanged.
- 122 files end in `.lua`; every one is a precompiled chunk (starts `1B 4C 75 61`). The rest are png/jpg/xml/anm/etc.
- Engine side: `PfpSource` (`src/assets/AssetSource.h`) reads the whole archive into memory. `AssetFS` adds it automatically when the `--assets` directory contains `assets.pfp`, after the loose directory source, so a loose file overrides an archive entry of the same case-insensitive path (and the loose files are what `list`, `listBySuffix` and `resolve` report first).

## Bytecode

Header of every chunk: `1B 'Lua' 50 | 01 (little-endian) | 04 (int) | 04 (size_t) | 04 (Instruction) | 06 08 09 09 (OP A B C bits) | 08 (lua_Number) | test number 3.14159265358979e7 as a little-endian double`. So the chunks were written by a 32-bit build: `size_t` and `Instruction` are 4 bytes. Everything else equals a 64-bit build's header apart from those two widths.

Two differences from stock Lua 5.0.2 that the loader handles (patch `patch_lua_undump32` in `cmake/PatchLua.cmake`):

1. `size_t` (string lengths) and `Instruction` may be 4 bytes in the file; a 64-bit build widens them on load. Any other width still fails with the stock "virtual machine mismatch" error.
2. The game's opcode numbering, the same as in `docs/re/embedded-lua.md` section 3: opcodes from POW on are 4 higher than stock (opcodes seen in the archive: 0..15 and 21..38; 16..20 never occur). The loader maps 20..38 back to 16..34 and raises `bad opcode` for 16..19. All binary chunks are treated as this dialect; the engine has no use for stock-numbered bytecode.

Check: all 122 chunks load with `luaL_loadbuffer`; 104 of the 106 that also exist as loose Mac sources are identical (code, constants, params, nups, nested protos) to what our Lua compiles from that source, the other two differ (the first is `sim/building.lua`, function 25, constant count), i.e. the Windows and Mac sources differ a little. Debug info (line numbers, local names) is kept in the chunks, so error messages carry line numbers.

The comparison checks instruction words and counts (params, nups, number of constants and protos), not constant values. The two known source differences are `sim/building.lua` function 25 (it lacks the Mac source's `Player:AutoSave()` call) and `sim/player.lua` main (3 instructions and 1 proto differ); the test requires every other compared chunk to be identical.

The patched loader expects the game's opcode dialect, so chunks written by our own `luac` or `string.dump` (stock numbering) no longer load. Nothing in the engine or the game data uses dumped chunks. Opcodes 16..19 and anything above 38 are rejected with `bad opcode`; a size_t or Instruction width other than 4 or the native width is reported with the byte read from the header.
