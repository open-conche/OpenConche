# prefs.dat, user records and saves (original on-disk formats)

Scope: what the original game writes for its players, so the engine can import a real profile. Sources: Mac decomp
`re/mac/decomp/TPrefs.c`, `TEncrypt.c`, `TBlowfish.c`, `TSettings.c`, `TPfHiscores.c`, `_free/{s,l,n,w,r,b,p}.c`,
`TPlatformData.c`, the Mac Mach-O the Chocolatier 3 Mac app bundle's `Contents/MacOS/chocolatier-decadence-design` (read only, for the key and the tables),
the Windows exe strings (`re/win/strings.tsv`, raw bytes of the Windows `chocolatier-decadence.exe`), and the Lua that calls
the natives. Addresses are Mac (Ghidra = file VA; image base 0x1000, so file offset = VA - 0x1000 in the TEXT segment).
Parser and writer: a workbench script.

## 0. Verification status (read first)

* **No real `prefs.dat`, `hiscore.dat` or `.choco3` from the original game was available to the author.** Searched: the Windows install,
  the Mac app bundle, the workbench, `~/Library/Application Support/`,
  `~/Library/Preferences`, `/Library/Preferences`. The only user data found is the engine's own plain-text profile
  (`Chocolatier3/`, a backup copy of it), which is a different format (section 9). `%APPDATA%\PlayFirst` was not present and was not touched.
* So the parser is **verified against the code, not against a real file**:
  * The Blowfish implementation reproduces the standard ECB test vectors (key 0 / pt 0 -> `4ef997456198dd78`; key all-ones ->
    `51866fd5b85ecb8a`), and its pi-derived P array and four S-boxes are byte-identical to the 4168 bytes in the Mac binary
    (P at 0x1a6180, S-boxes at 0x1a6de0, 0x1a69e0, 0x1a65e0, 0x1a61e0 for S0..S3).
  * The key string and framing were read from the binary and the write path, and a writer that follows `SavePrefs` exactly
    round-trips through a reader that follows `LoadPrefs` exactly (`prefs_dump.py --selftest`).
  * A synthetic vector is in section 10. A real file is still needed to confirm the points listed under Unknown.

## 1. Where the files live

* All of `prefs.dat`, `hiscore.dat`, `*.choco3` are opened through the virtual path prefix `user:` (strings `user:prefs.dat`
  0x15b62c, `user:hiscore.dat` 0x15af24, `user:` 0x1582bc; Windows `user:prefs.dat` 0x57076c). `TFile::Open` (0x5456e) maps
  the prefix to the per-user directory (`TFile::mUserDir`, 0x21fb14) and creates missing directories (mkdir 0777 on Mac).
* **Windows** (inferred from the profiling scripts, which read `%APPDATA%\PlayFirst\chocolatier-decadence-design\logfile.txt`;
  the exe has the literal `PlayFirst\\` at 0x57578c): `%APPDATA%\PlayFirst\chocolatier-decadence-design\`. Folder name is the
  game name, config `kGameName` = `chocolatier-decadence-design` (set by `PlaygroundInit` 0x29294). Files directly in it:
  `prefs.dat`, `hiscore.dat`, `logfile.txt`, `*.choco3`.
* **Mac** (`TPlatformData` ctor, TPlatformData.c ~line 700): `FSFindFolder(kUserDomain 0x8005, 'pref')` + a bundle
  Info.plist string -> `~/Library/Preferences/<bundle string>/` (mkdir 0755; this is also the debug-log dir passed to
  `ResetDbg`). A second "common" dir `/Library/Preferences/<literal>/<bundle string>/` (mkdir 0777) and `~/Desktop` also
  exist. Which of these is `mUserDir` is **inferred** (the `user:` code sits next to the 0x29f8 buffer); the Info.plist key
  and thus the literal folder name was not resolved (there is no Info.plist here).
* Only these per-user-account files exist in vanilla. There is **no per-user file and no per-slot file**: all players live
  in the single `prefs.dat` (section 4). The word "slot" does not appear in the code; a player is a numbered section.
* `.choco3` (string at 0x1582c4; Windows 0x558968) is only: (a) the suffix `NextSaveFileName`
  (0x40a2) filters for when it lists `user:` (it returns the stem, extension stripped, "" when exhausted), (b) a suffix
  Lua appends in `Player:SaveGameToFile`/`LoadGameFromFile` (the vanilla `sim/player.lua` 351-377) and the pause-menu
  debug buttons (`usability_1.choco3`...), (c) the dev save menu. The file content is whatever `WriteToFile(name, text)`
  (0x3de0) writes: **the plain, unencrypted, uncompressed text of the Lua save string, no header, no length prefix**
  (`TFile::Open(..., 3)` then `TFile::Write` of `c_str`/`length`). `ReadFromFile` (0x3ede) reads it back in 1024-byte pieces
  appended into a string (so an embedded NUL ends a piece). A normal game never writes a `.choco3`; a real profile has
  none unless the player used the debug menu. Reforged's `reforged_freeplay_*.choco3` sidecars are the same plain text
  (`docs/re/trade-and-save.md` 3.4).

## 2. Cipher: Blowfish-ECB, fixed key, PKCS-like padding

* `TEncrypt::TEncrypt(str)` (0x9e3ac) takes the config string `kEncryptionKey` ("encryptionkey", config key at 0x15b1d8).
  `PlaygroundInit` (0x29294) sets it from the global `_ENCRYPTION_KEY` (data 0x1d816c -> string at 0x159a3c):
  **`H32kwmdbCAAA`**. The same 12 characters are in the Windows exe (VA 0x55ad58), so both platforms share the key.
* Key handling: if the string is not 12 characters long it is padded/extended (constant at 0x15cf58, `AAAAAAAAAAAA`), and
  if its last 3 characters differ from `AAA` they are replaced by `AAA`. For this key neither applies. The 12 characters
  are standard Base64 (alphabet `A-Za-z0-9+/`, `=` pad; the Windows copy is at 0x574548) decoded to `Base64DecodeGetLength(12)` = 9 bytes:
  **`1f 7d a4 c2 67 5b 08 00 00`**. `TBlowfish::Init` (0xa764e) uses it as a standard Blowfish key, cycled over the 18 P words
  (4 key bytes per word, wrapping at 9 bytes).
* `TBlowfish` (0xa70b6..) is textbook Blowfish: 16 rounds, big-endian 32-bit words, standard pi tables, ECB (no IV, no
  chaining). Encrypt = `EncryptString` (0xa7172), decrypt = `DecryptString` (0xa73c6).
* Padding: `EncryptString` always appends `8 - (n mod 8)` bytes (1..8), each holding the pad count. Ciphertext length is
  therefore `(n & ~7) + 8`. `DecryptString` takes the last decrypted byte as the pad count and returns `len - pad`; if
  that is negative or larger than `len` it returns size 0 (treated as failure). A ciphertext whose length is not a
  multiple of 8 is zero-extended before decrypting.
* `TEncrypt::EncryptBinary/DecryptBinary` (0x9e64c, 0x9e786) have an optional Base64 wrapper (arg 5). TPrefs always passes
  `false` (raw binary). `DecryptStr` (0x9e8a6) is the Base64 form; the hiscore dialogs use it, prefs do not.
* No checksum or MAC on prefs.dat. Endianness: the cipher is big-endian inside, the container fields are little-endian
  (below), because the Mac binary under study is **i386** (Intel, little endian), same as Windows. The Mac and Windows
  builds are the same SDK code with the same constants and tags; nothing in the code differs by word size (all lengths are
  explicit u16/u32). A PowerPC build, if one ever existed, is not covered.

## 3. prefs.dat container

A flat sequence of chunks until EOF. Chunk header, 8 bytes, native little endian:

| offset | size | meaning |
|---|---|---|
| 0 | 4 | tag: `IKEY`, `USER` or `KVPR` (strings at 0x15b61c, 0x15b624, 0x15b614). `ReadChunk` only looks at the first letter (`I`, `U`, `K`) |
| 4 | u32 | payload length in bytes, must be 1..0x400000 (4 MiB) else the chunk is rejected |
| 8 | n | payload |

`ReadChunk` (0x567e8) reads the 4-byte tag, the length, the payload; if the first tag letter is not `U` it decrypts the
payload in place (section 2) and cuts it to the decrypted length. Failure (short read, bad length, bad padding) returns
false and `LoadPrefs` silently stops there, **keeping everything read so far** (no error is shown).

Payloads (after decryption):

* **`IKEY`**: the game identity key, lowercase ASCII string plus a trailing NUL (`ToVec`, 0x179640, copies `length+1`
  bytes). Selects which top-level set the following chunks belong to. The map is `std::map<str, vector<section>>`.
* **`USER`** (the only unencrypted chunk): payload is exactly 2 bytes, u16 section index (max 0x400, else load stops).
  Selects the section the following `KVPR` chunks go to; the section vector is grown so the index exists.
* **`KVPR`**: `u16 K`, then `K` bytes of key (the key text plus its NUL, so `K = strlen+1`), then the value bytes to the end
  of the payload. The value of every pref is a NUL-terminated string (`ToVec` again). Integers are decimal text via
  `"%d"` (format at 0x1588dc) and parsed with `str::to_int`. `str` comparison is `strcmp` based (0x504ca), so the NUL in
  loaded keys and values is harmless. No other type exists.

**Order written by `SavePrefs` (0x56bac), only if the dirty flag (`this+0x2c`, ctor arg) is set:**
1. `IKEY` with `this+0x28` (the current identity, see below).
2. For each top-level map entry (sorted by `strcmp`): `IKEY` with that key, then for section `j = 0 .. size-1`:
   `USER` (len 2, `j`), then one `KVPR` per entry of that section (the section map is also `strcmp`-sorted).
3. The file is opened with `_pf_open(path, 2)` and written in one `TFile::Write`; there is no temp file or rename.
Section `0` is the **global** prefs. Section `i+1` is **user i**. So in a normal file there are two identical leading
`IKEY` chunks (the second from the loop) - confirmed in the generated vector. Every `Set*` call with flush flag 1 rewrites
the whole file (all the game code passes 1).

**`LoadPrefs` (0x57d78) behaviour:**
* Identity: `this+0x28` starts as config `kGameName`-like value or `unknown` if empty (ctor 0x58412, string at 0x15b6b8), then
  is replaced by the lowercased contents of the file `install.txt` (opened with `_pf_open`, string 0x15b658) when that
  exists and is non-empty (the PlayFirst installer key, `pfinstallkey`, 0x15b66c). If `install.txt` exists but is empty,
  the identity is taken from the file's first `IKEY` instead. In the normal retail case with no `install.txt` the
  identity is `chocolatier-decadence-design` (inferred from the config value; not observed in a real file).
* First chunk must start with `I`, otherwise nothing is loaded. Later `IKEY` chunks switch the current set; the `U`
  chunk index resets to 0 only at load start (it is not reset by `IKEY`; the writer always emits `USER 0` after each
  `IKEY`, so files from the game are consistent). Any chunk with another first letter aborts loading.
* The active set used by `GetStr`/`GetInt` is the one for the identity in `this+0x28`. A file written under a different
  `IKEY` is therefore invisible (inference from the call structure: `this+0x1c` is set from the map entry for the
  identity). To import, use the file's own first `IKEY` as the identity (that is what `prefs_dump.py` does).
* If the first two bytes of the file are `<` `>` (cmpb 0x3c/0x3e at 0x58009) the file is **old XML prefs**:
  `LoadXMLPrefs` (0x5759a) imports it and `SavePrefs` rewrites it in the binary form. The XML layout is not decoded here.
* Missing/empty file: starts with an empty set; the identity set exists, `GetNumUsers` = 0.

## 4. Users, per-user prefs, current user (`TPrefs`, `TSettings`)

* `TPrefs::GetStr(key, default, user)` (0x56f50) / `GetInt(key, default, user)` (0x56ffe): section index `user + 1`; **`user = -1` is
  the global section 0**. Out of range index or missing key -> default. `GetInt` formats the default with `%d`, calls
  `GetStr` and parses (so an unparsable value is 0).
* `SetStr(key, value, user, flush)` (0x57166) grows the section vector if `user+1 >= size`, stores `ToVec(value)` and calls
  `SavePrefs` when `flush`. `SetInt` (0x5725e) = `SetStr("%d")`.
* `GetNumUsers` (0x56710) = `sections - 1` (0 when there are 0 or 1 sections). Lua `GetNumUsers()` = `TSettings::GetNumUsers`
  (0x35e0e). **Users are just sections 1..N; `UserName(i)` / `GetUserName(i)` (0x35d7c) = pref `name` of user `i` (default
  "")**, indices 0-based, in file order. There is no separate user list or id.
* `TPrefs::DeleteUser(i)` (0x57104) erases section `i+1` if `i < GetNumUsers()`; the later users shift down. It does not
  save by itself. `TSettings::DeleteUser` (0x36aea) then selects `i-1` (or the last user if that is < 0) via
  `SetCurrentUser`, which writes `curuser` with flush, persisting the deletion.
* `CreateNewUser(name)` (0x361dc): new index `n = GetNumUsers()`; writes `name`, then copies `soundlevel`
  (default 750 = 0x2ee), `musiclevel` (650 = 0x28a), `ambientlevel` (500), `soundoff` (0) from the current user (read with the
  current user index, defaults as listed) into the new section; then `SetCurrentUser(n)`. `ChangeCurrentUserName` (0x35bd8)
  writes `name` of the current user. `IsNameInUse` (0x36b2c) is an exact `str ==` scan of all names.
* `SetCurrentUser(i)` (0x360ec): only if `i < GetNumUsers()`; writes global `curuser` = `i`, then (if `i >= 0`) pushes the
  name to `TPfHiscores::SetProperty(1, name)` and `SetSoundForCurrentUser`. `GetCurrentUser` (0x357f8) reads global `curuser`
  with default **-1** (no user; `SaveGameString` becomes a no-op and `LoadGameString` returns ""). `GetCurrentUserName`
  (0x35ca6) reads `curuser` with default 0, then that user's `name`.

Keys found in the code (value is always decimal/NUL text):

| key | section | default | where |
|---|---|---|---|
| `curuser` | global | -1 | TSettings 0x357f8, 0x360ec, TOptions |
| `fullscreen` | global | 1 | TSettings 0x35874-0x35928 (set 0/1 by toggle), `IsFullScreen` 0x3585c reads it, default 1 |
| `name` | user | "" | TSettings 0x35d7c, 0x361dc |
| `savegame` | user | "" | `SaveGameString` 0x3a36, `LoadGameString` 0x4aa4 |
| `soundlevel`, `musiclevel`, `ambientlevel` | user | 750, 650, 500 | TSettings 0x361dc, TOptions (sliders write `int(float*1000)`) |
| `soundoff` | user | 0 | TOptions, TSettings |
| `_anonusername` | user | "" | `TServerSubmit` (anonymous online-score name) |
| `_pflifetime` | global | "" | `~TPrefs` (0x5733e) writes `sCreateSavedLifeTime`: Base64(Blowfish) under a machine-GUID derived key (`sEncryptWithMachineGUID` 0x564b4, `sGetLifetimeEncryptionKey` 0x560b8); DRM/trial timing, **ignore on import** |

There is no `hiscoreusername`, no difficulty pref and no gamemode pref in the original: the closest are `_anonusername`
(submit name, per user) and the remembered account/medal data inside `hiscore.dat` (section 6). Difficulty is part of the
Lua save string. `gamemode` seen in the engine's own profile is an engine key. `SetCurrentGameMode` (0x35b6e) is a no-op
except for hiscore property 2.

## 5. The save string

* `Player:SaveGame` (the vanilla `sim/player.lua` 322) calls `SaveGameString(Player:BuildSaveGameString())` only if
  `GetNumUsers() > 0`. `BuildSaveGameString` returns Lua source of the form `return {<fields>deliveries={...}}`.
  `Player:LoadGame` does `loadstring(LoadGameString())` and calls the result.
* `SaveGameString(str)` (0x3a36) = `TPrefs::SetStr("savegame", str, currentUser, flush=1)`. **The save is stored inside
  `prefs.dat` as the `savegame` KVPR of that user's section**, Blowfish-encrypted like any KVPR, not as a separate file.
  There is no compression, no length prefix of its own, and no extra encoding: plaintext is `u16 9`, `savegame\0`, the
  Lua text, a NUL. The whole chunk (key + text) must stay below 4 MiB or `ReadChunk` rejects the chunk on the next load
  (silently ends loading, losing all later chunks). Real saves are a few KB (`docs/re/trade-and-save.md` quotes about 9.5 KB).
* `LoadGameString` (0x4aa4) = `GetStr("savegame", "", currentUser)`; "" when no current user.
* One save string per user. No save slots: starting a new game overwrites it. `Free Play`/`SetCurrentGameMode` do not select
  a second string in vanilla (`docs/re/trade-and-save.md` 3.3).

## 6. hiscore.dat (adjacent, same cipher)

`TPfHiscores::LoadPreferences` (0x45f88) / `SavePreferences` (0x41400), file `user:hiscore.dat`, no container: the file is
`Blowfish-ECB(xml_text + NUL)` (padded as in section 2) immediately followed by 8 bytes = `Blowfish-ECB(4-byte checksum)`
(same key via `TEncrypt` at `TPfHiscores+0xa4`; the XML has the remembered account, local score tables and medals;
`hiscore.xml` (string 0x57081c on Windows) is a separate plain XML in the install dir). The writer XORs the ciphertext bytes into a 4-byte
lane accumulator (`i mod 4`) but, as decompiled, encrypts a **zero** word as the trailer, and the reader decrypts it and
accepts when it is 0 (message "HiScores: Checksum did not match!", 0x15b198, otherwise). So in practice the trailer is
the encryption of four zero bytes; this is decompiler output and may hide an aliasing artefact. `prefs_dump.py FILE
--hiscore` decrypts such a file. The XML attribute vocabulary is documented only as far as `docs/re/hiscore.md`.

## 7. Lua-visible natives that depend on this

| Lua call | Native | Expects |
|---|---|---|
| `GetNumUsers()` | 0x35e0e | number of user sections |
| `UserName(i)` / `GetUserName(i)` | 0x35d7c | `name` pref of user i, 0-based, "" if none |
| `GetCurrentUser()` | 0x357f8 | `curuser`, -1 when none |
| `SetCurrentUser(i)` | 0x360ec | ignored if `i >= GetNumUsers()` |
| `CreateNewUser(name)` / `CreateNewUser` | 0x361dc | appends, copies sound prefs, selects it |
| `DeleteUser(i)` | 0x36aea | removes, selects `i-1` or last |
| `IsNameInUse(name)` | 0x36b2c | exact compare |
| `ChangeCurrentUserName`, `GetCurrentUserName` | 0x35bd8, 0x35ca6 | see section 4 |
| `SaveGameString(s)` / `LoadGameString()` | 0x3a36 / 0x4aa4 | per-user `savegame` pref |
| `WriteToFile`, `ReadFromFile`, `NextSaveFileName` | 0x3de0, 0x3ede, 0x40a2 | plain `user:` files, `.choco3` suffix |

## 8. Import plan for the engine

1. Read `prefs.dat`; if it starts `<>` report "old XML prefs, unsupported".
2. Parse with the section 3 rules; use the first chunk's `IKEY` as the identity; `users = sections[1:]`.
3. Map: user `name` -> `users.txt` name; `savegame` (text up to the NUL) -> that user's `save.txt`; the other user keys
   (`soundlevel`... same 0-1000 scale the engine uses) -> per-user `prefs.txt`; global `fullscreen` -> `prefs.txt`;
   global `curuser` -> `current`. Drop `_pflifetime`. `prefs_dump.py FILE --export DIR` does exactly this (folder names
   `userN`; the engine's `sanitiseFolder` may rename them).
4. Never write back into the original file. The original only checks the key and the framing, so a file rewritten by
   `prefs_dump.build_prefs` is readable by the game, but this is untested against the real game.

## 9. Engine layout, for comparison (not the original)

`src/game/Users.h`: `users.txt` (`choco3-users 1`, `current N`, `folder<TAB>name`), `prefs.txt`, `profiles/<folder>/save.txt`,
`profiles/<folder>/prefs.txt`, `hiscore.dat` plain text. The backup profile `Chocolatier3-backup-20261102-2116` uses it
(users Cow and Cheater; sound levels in the same 0..1000 scale, `gamemode` an engine-only key).

## 10. Synthetic test vector

the workbench script's `--make-vector` mode writes a 542-byte file, sha1
`1c113baca62b1f2f0ad9d1747ead191ae0ee45bf`, game `chocolatier-decadence-design`, global `curuser=1`, `fullscreen=1`; user 0
`Ada` with `savegame` = `return {name="Ada",money=1234,time=17,}` and sound prefs 750/650/500/0; user 1 `Bob` with
`_anonusername=bobby`, sound 400/650/500, `soundoff=1`. It begins `IKEY 20 00 00 00 dd20e7c70766fe82...`. This is generator-side
only; the game itself has not read it.

## Unknown

* **No real sample was available**, so these are unconfirmed against bytes: the identity string actually written by the
  retail build (and the role of `install.txt`); whether any shipped file still has two leading `IKEY`s as the code
  implies; real `savegame` lengths.
* Which exact directory `mUserDir` is on Mac (bundle Info.plist key and the folder name) and on Windows beyond the
  `chocolatier-decadence-design` game-name inference.
* `TFile::Open` mode numbers (0 read, 2/3 write) and whether write truncates; the exact read size of the first
  `TFile::Read` in `LoadPrefs` (assumed 4, the tag).
* The XML `LoadXMLPrefs` format (old versions) and the XML vocabulary and real checksum behaviour of `hiscore.dat`.
* How `kEncryptionKey` could differ in other Playground games or the Steam build; the Reforged exe does not contain the key
  string, so whether Reforged still writes the original `prefs.dat` is unverified. (Reforged's `.choco3` sidecars and
  `savegame` strings are plain Lua text either way.)
* The pad logic for ciphertext not a multiple of 8 on load is taken from `DecryptString`; no valid file contains it.
