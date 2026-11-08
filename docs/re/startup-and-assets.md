# Startup, data loading, files and fonts (Mac binary, Chocolatier 3)

Sources: Mac decompile (`re/mac/decomp`), the Chocolatier 3 Mac app bundle's `Contents/Frameworks/`, Reforged and original Lua. Addresses are Ghidra vm addresses
from `re/mac/index.tsv`. Items marked (inference) are not confirmed by code. Section 6 lists what is unknown.
Note: this binary's embedded Lua chunks are big-endian (PowerPC-built) Lua 5.0.2 bytecode.

## 1. Startup sequence

1. `PlayFirstExec(argc, argv, callback, flag)` (0008face) is the process entry. It builds `TPlatformData` (0x6ab0 bytes)
   then `TPlatform`. It stores `ChocoThreeMain` (0001001c) as the callback, appends argv to a config string, handles
   `-url=` style args, creates the Carbon window and sound, and runs the Carbon event loop. The callback is invoked from there.
2. `TPlatformData::TPlatformData` (0008df88):
   - `srand(time)`.
   - `chdir()` to the bundle's Resources directory. Asset root is therefore `<Resources>/assets`.
   - `TFile::ScanForResources()` (first pass; user:/common: folders still empty).
   - Creates `TWindowManager`, `TAssetManager`, `TStringTable`.
   - Folders via `FSFindFolder` (inference on domain constants): "common" = `/Library/Preferences/<something>/<bundle name>` (mkdir 0777);
     "user" = `~/Library/Preferences/<bundle name>` (mkdir 0755); "desktop" = `~/Desktop`. The middle component literal was not resolved.
3. `TPlatform::TPlatform` (0005b18e), in order:
   - Config defaults `memorymax=100000000`, `texturememorymax=100000000`; `-url=` and `--stringtest` arg parsing (`stringtest=1`).
   - `utf8modeinit` config copied from the `version` key.
   - Random seed array (4 words from `rand()`), creates the `TWindowScript`, `computerid` config.
   - **Reads `::assets/settings.xml`**: root `<settings>`; every child element becomes a config key = element name, value = text
     (`SetConfig(name, content)`). Reforged keys: `game`, `gameversion`, `serverversion`, `cheatmode`, `locale`, `hiscorelocal`, `hiscoreanon`.
     `::` prefix means a real filesystem path relative to cwd, bypassing the resource map.
   - Sets the three folder globals (user/common/desktop), then `TFile::ScanForResources()` again (now includes user:/common:).
   - `TStringTable::LoadStringTable("strings.xml")`; if OK, enables the string table in the window manager.
   - Then merges every resource under prefix `user:` and then `common:` whose name starts with `string` and contains `.xml` (`MergeStringTable`).
   - Product name/version written to config (`gamename`, `version`).
4. `ChocoThreeMain` (0001001c):
   - Constructs `BSGStringTable` (registers Lua `bsgLoadStringFile`, `bsgStringExists`, `bsgGetString`); `Content::Initialize()`
     (registers `LoadPortFileList`, `LoadQuestFileList`, `LoadIngredients`, `LoadProducts`).
   - `RegisterBSGLuaHelpers` (00004494) registers: `Date`, `DayOfYear`, `Dollars`, `Mod`, `SetLayeredLabel`, `SetAlpha`, `RandRange`, `Floor`, `Pow`,
     `FillWindow`, `SetWindowPos`, `LoadGameString`, `SaveGameString`, `CheckConfig`, `LogScore`, `WriteToFile`, `ReadFromFile`, `NextSaveFileName`, `CurrentTime`.
   - Globals set: `kScreenWidth=800`, `kScreenHeight=600`, `kGameWidth=800`, `kGameHeight=600`, `kGameScale=1.0`,
     `gVersionString = "ver " .. config("version")`. Also `SetMood`, `GrabLedger`, `ReleaseLedger`, `Transition`.
   - Registers window types by name: BitmapTint, BSGWindow, CharWindow, ChocolateFactory, CoffeeFactory, CratesGame, CreditsWindow, CurveEditor,
     DookieDropper, FireworksWindow, MapWindow, PortWindow, RecipeCreator, RecipeDesigner, Rectangle, Rollover, SignEditor, SlotMachine, TightText,
     OptionsWindow, HiscoreWindow; `TServerSubmit::Register`.
   - Loads cursor `cursor/cursor`, `BSGSoundInit`, `TSettings::SetSoundForCurrentUser`.
   - **`TScript::RunScript("ui/mainloop.lua")`**, then the event loop: event 3 (close) runs Lua string `AskQuit()`; event 0x12 toggles foreground/sound;
     event 0x13 `TSettings::UpdateFullScreen`; event 4 ends. On exit it runs `if Player then Player:SaveGame() end`.
5. `TScript::RunScript` (0007b8da): empty name or missing file (`TFile::Exists`) returns silently. Otherwise `TScriptCode::Get` compiles the file
   (or fetches from a cache keyed by name in `TAssetManager`), `luaL_loadbuffer`, then `RunFunction(0 args, 1 result)`.
   `RunFunction` (0007b41e) pcalls with `_ALERT` as error handler. **If the single result is a function, it is resumed as a coroutine**
   (`TAnimTask::SetDelay(0)` if no delay pending, then `lua_resume(L, 0)`), which is how `Main` runs. Errors in resume are logged
   (`lua_resume error: %s`). Later wake-ups come through `TScript::Resume` (resume with N args; on error logs and pops).
6. `TScriptCode::Get` (0007a334) compiles in a throwaway `lua_open()` state: runs `LUA_PATH='assets/?;../?;?' ` via dostring, `luaL_loadfile`, then
   `luaU_dump` to a byte string stored in the asset cache. So scripts are cached as bytecode after the first load.
7. `TScript::LuaInit` (00079e50): opens only **base, string, table** libraries (not math, io, os). Adds `_ALERT`, `DebugOut`, `toparams`, `DumpTable`
   (Lua helper defined by dostring), and `_SCRIPT` (lightuserdata). `math` is only opened for particle scripts. Reforged notes `math.abs` is absent;
   use `Floor`, `Pow`, `Mod`. Base lib is Lua 5.0.2 (`_VERSION`), with extras `newproxy`, `towstring`, `lua_getdefaultmetatable` (modified Lua).
   `DebugOut` is a native closure, so exists before `Main`; Reforged `ui/debug.lua` wraps it (`gOriginalDebugOut`).
8. TWindowScript constructor adds window/UI natives (PostCreate, GetLabel, SetLabel, GetConfig, IsEnabled, PushModal, PostMessage, CreateNamedMessage,
   StartMovie, IsMoviePlaying, StopMovie, ImageLoaded, Slider, Bitmap, Button, TextEdit, Window ...) and runs the compiled pflib chunk `ScreenInit.lua`
   embedded in the binary. That chunk defines the `k*` constants (`kCloseWindow`, `kPressAnyKey`, `kModalClosed`, `kQuitNow`, `kMax=160000`, `kCenter=80000`,
   `kHAlignLeft/Center/Right=0/1/2`, `kVAlignTop=0`,`kVAlignCenter=4`,`kVAlignBottom=8`, `kAllLayers=-1`, `kUserMessageBase=1000`...), `Color`, `MakeDialog`,
   `DoModal`, `DoMainWindow`, `DisplaySplash`, `DisplayDialog`, `WaitForMessage`, `Yield`, `Pause`, `Group`, style functions etc. Window-system detail is another analyst's.
9. `require` / `dofile` / `loadfile` are stock Lua 5.0.2 functions (`luaB_require` 000c3134) running on the patched file layer (section 3).

### DisplaySplash(splashMovie, splashGraphic, time)
Lua in ScreenInit.lua (decoded from bytecode; control flow is approximate, constants exact):
- `id = PushModal("DisplaySplash")`; reads style tags `translate`, `allowInput`, `disableAbort`, `splashoverlay` via `GetTag({}, name)`, defaulting to false.
- `inMovie = StartMovie(splashMovie, translate, allowInput)` (global `inMovie`). `endTime = GetTimer() + time`.
- `StartMovie` returns false at once when the path is `""`. Otherwise it creates a `TFlashHost` (a WebKit-hosted Flash player writing temp files) and
  returns whether `Start` succeeded, else destroys it and returns false.
- If no movie: build `MakeDialog{ Bitmap{ name="splashimage", image=splashGraphic, <overlay group> } }`, then `UpdateWindow(); Pause(50); UpdateWindow(); Pause(50)`
  (two forced frames). The graphic name has no extension; extension probing applies (section 3). `if not splashGraphic then return`.
- Loop: `WaitForMessage(1.0)` (the 1.0 argument meaning is unverified) until: a `kCloseWindow` or `kModalClosed` message whose name equals `id`
  (records `gClosedWindows[name]=true`); or a `kPressAnyKey` message when `disableAbort` is false; or, with no movie, `GetTimer() > endTime`;
  or, with a movie, `IsMoviePlaying()` becomes false.
- Cleanup: `StopMovie()` if a movie ran, then `PopModal(id)`.
- Consequences: `DisplaySplash("", "image/title_background", 1)` shows the image for 1 ms and keeps a modal splash window until `PopModal`. The `.swf` case: for our engine
  treat `StartMovie` as returning false and fall through to the image for the given time. Reforged passes `.jpg` as first arg for two logos; its original-engine behaviour
  for a non-swf path in `TFlashHost` is unknown, so treat as "not a movie" (inference). `kPressAnyKey` is posted by the window manager (other analyst).

## 2. Data loading used by `Main`

Order in original `Main` (Reforged adds debug logging and a few extra steps): 4 splashes; `LoadIngredients()`; `LoadProductCategories()` (Lua, `dofile("items/categories.lua")`);
`LoadProducts()`; `LoadPorts()`; ensure `_AllCategories.user` via `CreateCategory{name="user"}`; `LoadQuests()`; `dofile("ports/portroutes.lua")`;
`table.sort(_IngredientOrder, IngredientOrderFunction)`; `PrepareCharactersForBuildings()`; `PrepareCharactersForQuests()`; (Reforged also `dofile("characters/character_data.lua")`
and `ApplyCharacterData()`); `AssignProductCategories()`; sort each `cat.products` by `code`; `GetNumUsers() > 0` ? `Player:LoadGame()` (Reforged `("story")`) : `Player:Reset()`;
Reforged then `Player:ReloadStrings()`, language/font globals, `require("ui/style.lua")`, then `while true do DoMainWindow("ui/mainmenu.lua") end`.

| Name | Where | Notes |
|---|---|---|
| LoadIngredients, LoadProducts | engine `Content` | read `ingredients.xml`, `products.xml` |
| LoadPortFileList(t), LoadQuestFileList(t) | engine `Content` | fill Lua array `t` with script paths |
| LoadPorts, LoadQuests | Lua `sim/port.lua:121`, `sim/quest.lua:128` | call the FileList function, `dofile` each |
| CreateCategory, ApplyCharacterData (`characters/character_data.lua:1196`), PrepareCharactersForBuildings/Quests, AssignProductCategories, Player:Reset/LoadGame/ReloadStrings | Lua | `sim/*.lua` |
| GetNumUsers, GetCurrentUserName, CreateNewUser, SetCurrentUser, DeleteUser, IsNameInUse, ChangeCurrentUserName, GetUserName, GetCurrentUser | engine `TSettings` (ctor 0003649a) | wrap `TPrefs` |
| bsgLoadStringFile / bsgStringExists / bsgGetString | engine `BSGStringTable` | |
| ClearStringCache, HasString, GetString/GetText wrappers | Lua `ui/helpers.lua` | Reforged-only caches over `bsg*` |

### Content (ContentManager.h, 00019b08..0001ad04)
- `BuildFileList(key)`: iterates the whole lowercased resource map (`TFile::GetNextFile("", ..., recursive)`), keeps every resource key that **ends with** `key`
  (e.g. `ports.xml` matches `ports/ports.xml`), sorted lexicographically.
- `LoadDataFile(key)`: for each matched `.xml` file calls `LoadDataFileXML` (CSV path is a stub).
- `LoadDataFileXML`: parse with `TXmlNode`; root element must be `<root>`. For each child: `<ingredient>` -> Lua string `CreateIngredient {…}` run via `DoLuaString`;
  `<product>` -> `CreateProductFromXML {…}`; other children ignored. `SimpleXmlToLua` builds `{k="v",k="v",}` where each child element name is the key and
  its text is a double-quoted string (no escaping, so a `"` in text would break it); nested elements become nested tables. **All values are strings**;
  the Lua side converts (`tonumber`, "true"). For ingredients it also registers an empty asset named `"items/" .. <child "name" text>` in the Content asset map (purpose: unknown; inference: texture lookup cache seeding).
  Ingredient fields seen: name, code, category, recipe_family, kitchen_category, unit_type, alcohol, origin, locked, season_start/end, price_low/high(_notinseason).
  Product fields: code, category, then ingredient-name children with a count value (e.g. `<cacao>1</cacao>`); Lua puts them in `recipe`.
- `LoadListFile(table, key, node)`: for each file matching key, parse it, root `<root>`; the directory prefix is the file's path up to the first `/` found at index >= 4 (inference from the call: it keeps the leading directory).
  For each child named `node` (`port` for ports, `file` for quests): the name is the child `<name>` content if present else the node's own text; a trailing `.lua` is stripped;
  candidate path = `<dir>/<name>.lua` (hm: the engine first tries `<dir>/<name>/<name>.lua`? Not confirmed; see Unknown). If `TFile::Exists` the path is appended to the Lua array (1-based).
  Reforged `ports/ports.xml`: `<root><port><name>capetown</name></port>...`; `quests/quests.xml`: `<root><file>tutorial.lua</file>...`.
  Reforged port scripts live at `ports/capetown/capetown.lua`, so the engine must produce that path for name `capetown`. Reproduce: try `ports/<name>/<name>.lua`, then `ports/<name>.lua` (inference; the decompile shows two Exists checks with the second a fallback concatenation).
- Port scenery globals: pure Lua. `CreatePort(name)` sets `_G[name]`; `CreateBuilding`/`EmptyBuilding` set `_G[buildingName]`; `CreateSprite(name, port)` (`sim/port.lua:559`)
  sets `_G[name] = { image = "ports/<port>/<name>" }` and appends to `port.sprites`. So `capetown` comes from `CreatePort("capetown")` and `cap_car1` from
  `CreateSprite("cap_car1", capetown)`; later fields (`images`, `path`, `tints` ...) are plain table assignments. No engine support needed beyond `dofile`.
  Ingredients and characters similarly register `_G[name]`.
- Characters: `characters/*.xml` are **not character data**. They are sprite-sheet descriptors (section 3). Character data is Lua (`characters/character_data.lua`, `ApplyCharacterData`).

### Player:ReloadStrings (Reforged, sim/player.lua:379)
Files: `strings.xml`, `dialogue_strings.xml`, `catalogue_strings.xml`, `kitchen_strings.xml`, plus each quest Lua name with `.lua` replaced by `_strings.xml`
(e.g. `quests/tutorial_strings.xml`). `ClearStringCache()` (Lua caches only; the engine map is never cleared, later loads overwrite), load root files with `bsgLoadStringFile`,
then if `options.language ~= "en"` load `languages/<lang>/<same names>` on top. Original Mac Lua has no such function (quest.lua:90 loads quest strings directly).

## 3. Files and assets

### TFile resource system (000524ae..0005456e)
- Resource map: lowercase relative path -> real path. `ScanFolderForResources("assets", "")` walks `assets/` recursively, skipping `.svn`; keys are lowercased, relative to `assets/`
  (`ui/mainloop.lua`). `user:` and `common:` roots are scanned the same way (keys prefixed `user:`/`common:`). Optional `final.txt` (or `firstpeek.txt` in firstpeek mode) in cwd
  (<100000 bytes) is a whitespace-separated list of names via `InternalAddFileMask` (`-*` clears it) that are **skipped by the loose-file scan** (it has no effect on `.pfp` entries). Lookup is therefore case-insensitive: `TranslateResource` lowercases the incoming name (and mutates it).
- `TranslateResource(name, mustExist)` order: map hit -> that path; leading `::` -> strip and use the real path if readable; `user:` / `common:` / `desktop:` prefixes map to the
  folder globals + `/` + rest (checks `access(...,6)` when `mustExist`, caches in the map); a leading `assets/` or `assets\` is stripped and retried; otherwise empty (not found).
- `TFile::Open` modes < 2 (read) require existence; mode >= 2 (write) auto-creates missing directories (`mkdir 0777`) for `user:`/`common:` style paths. Names starting `@` open a flat archive (`TFlatFile`);
  names starting `&` open an in-memory file (`TMemoryFile::AddMemoryFile`). `pf_open/pf_read/pf_seek/pf_getc...` adapt TFile for C code (Lua, zlib users, XML).
- `TFlatFile` (000a6298, `InitFlatFiles`/`ReadFlatFile`) reads `.pfp` archives (string `.pfp` at 0015d5a8). Format `PFPK`, u32 count, then per entry u8 name length, name, u32 offset, u32 size (verified against `ReadFlatFile`). `InitFlatFiles` runs at the start of each `ScanFolderForResources`: `.pfp` files in the folder are read in name-sorted order into the same map (value `@<archive>:<offset>,<size>`), and loose files are inserted afterwards, so **loose files override `.pfp` entries** and later archives override earlier ones. See `design-questions.md` section 3. Vanilla only.
- Lua file loading: `luaL_loadfile` (000a5850) opens via `pf_open` (so the resource map applies, e.g. `require("ui/style.lua")` finds `assets/ui/style.lua` case-insensitively), peeks the first byte:
  if printable/space it is read as text, otherwise it re-opens binary for a precompiled chunk. So plain `.lua` and Lua 5.0 bytecode both work. No `#` line skipping seen.
- `require(name)` (stock 5.0.2): global `LUA_PATH` else env else `?;?.lua`; here `name` already includes `.lua`, so the first template `?` hits. `_LOADED[name]` caches, so each file runs once. The main state's `LUA_PATH` global is set by pflib ScreenInit to `?;../?;../../?`. `dofile(name)` always re-runs. Both fail with a Lua error if not found.
- Asset extension probing (`TAssetMap::AddAsset`, 000649be): name is first normalised (`\` to `/`); a `?options` suffix is split off; if the exact file does not exist, tries suffixes in this order:
  `.xml, .anm, .jpg, .png, .spx, .ogg, .mesh, .lua`. Type by final extension: jpg/png -> texture; xml/anm -> animated texture (`TAnimatedTexture`); ogg/spx -> sound; lua -> script (cached `TScriptCode`); mesh -> mesh.
  Failure logs "Failed to find asset %s with any standard extension".
  Because `.xml` is first, `image="characters/cap_shopkeep"` picks the sprite-sheet XML before the PNG.
- Texture loading (`TTextureData::LoadFile` 0006ffb2): tries exact name, else appends `.png`, `.jpg`, `.jp2` in that order. Option substrings after `?`: `slow` (flags |=0x30), `alpha`, `mipmap` (|=8), one more (|=0x10, string unresolved).
  JPEG decode via libjpeg, PNG via libpng, JP2 via a JPEG2000 loader. A JPEG may take its alpha from a companion `<name>.mask.png` when a config flag (key unresolved) is nonzero and the file exists: alpha = the mask's blue channel (byte index 2 per RGBA pixel), requires square equal sizes (inference from the loop condition).
  Textures are padded to power-of-two (width/height doubled until >= size; max 0x400 unless `slow` flag) and centered (offsets `(pow2 - w)/2`). Whether `.mask.png` hit masks for buttons are loaded here or by `TImage`/`Button` code is not resolved.
- Sprite-sheet XML (`TAnimatedTexture::ParseFromXML` 0006bec0): root `<sequence name= source=>` (the `source` pattern like `cap_shopkeep*.png`), child `<img regx regy name w h>`, `<framelist>` with `<frame regx regy name w h x y ...>` children (x,y,w,h pixel rect in the sheet; regx/regy registration point; optional `xform` six space-separated floats; an optional scale/alpha float defaulting to 1.0; unspecified regx/regy/xform copy the previous frame), and optional `<script luascript="function DoAnim() ...">`. Errors: "does not contain a sequence/img/framelist node", "does not contain any frames!". Max frame size tracked. `.anm` is a separate text format (`ParseFromANM`), unused in shipped data. Default script generated when none: `function DoAnim() delay(60); frames{1,2,...} end`.
  Attribute-name-to-field mapping beyond the names seen in data files was not recovered from the decompile (names are passed in registers).
- `TXmlNode` (not a real XML parser): tokenizer `pfGetToken` is UTF-8 aware, whitespace separated, `<`, `>`, `=` are single-char tokens, double-quoted strings are one token, then entities (`&amp; &lt; &gt; &quot; &apos; &#N;`) are decoded. Element text = tokens joined by **single spaces** (whitespace collapses). `<?xml`, `<!DOCTYPE`, `<!--` tags are skipped. Files starting with the 4-byte magic `ZXML` are `[magic][u32 compressedLen][u32 rawLen][zlib data]`. The original fseeks back one byte per char (slow), per the original plan; read the whole file instead.
  `GetChild(name)` returns the first child with that name; `GetNextChild` iterates in order; `GetAttribute(name)` returns "" if absent.

### Prefs, users, saves (TPrefs 00056710..00058412; TSettings)
- Prefs file: `user:prefs.dat`. If it begins with the two characters `<` ... `>` (XML style, first byte `<`, second `>`? check was `local_30=='<' && local_2f=='>'`, probably a quirk of byte order) it is imported with `LoadXMLPrefs` then rewritten. Otherwise chunked binary: each chunk = 4-byte tag, u32 length (max 0x400000, nonzero), payload. Tags: `IKEY` (first chunk; identity key string, lowercased, from the `data1` resource), `USER` (u16 user index, max 0x400), `KVPR` (key/value pair: u16 key length, key bytes, then value bytes). All chunks except `USER` payloads go through `TEncrypt::DecryptBinary` (encrypted, padded to 8-byte multiples: size `(n & ~7) + 8`). The cipher is not recovered (`encryptionkey` config, `TEncrypt`). `SavePrefs` writes: `IKEY` chunk, global `KVPR` chunks, then for each user: `USER` (len 2, u16 index) followed by that user's `KVPR` chunks. Saving only happens if the dirty flag (byte at +0x2c) is set.
- `TPrefs::GetNumUsers` (00056710), `DeleteUser`, `GetStr/GetInt/SetStr/SetInt(key, default/value, user, ...)`. Lua `GetNumUsers`, `GetCurrentUserName` etc. are TSettings wrappers (details of name storage not decompiled here).
- `SaveGameString(str)` / `LoadGameString()` store the save blob per current user (and `NextSaveFileName`, `.choco3` extension string present); exact key names and file layout not decompiled.
- `WriteToFile(name, text)` / `ReadFromFile(name)` (registered by `RegisterBSGLuaHelpers`): bodies not read here. Reforged's transport comments say they are not plain cwd-relative calls; assume they use the `user:` folder (inference).
- Mac locations: prefs/saves under `~/Library/Preferences/<bundle name>/` ("user:"), shared under `/Library/Preferences/.../` ("common:"). hiscore data: `user:hiscore.dat`.

### Strings
- Two layers. `BSGStringTable` (map<str,str>, used by `bsgGetString`): `LoadStringFile(path)` ignores missing files; parses via `TXmlNode` Excel-2003 spreadsheet XML `Workbook > Worksheet(first) > Table > Row > Cell > Data`.
  For each `Row`: first `Cell/Data` text = key, second = value (second missing = empty string). Empty key skipped. Later loads overwrite. Text is whitespace-collapsed by the tokenizer.
- `GetString(key)`: leading `#` returns the rest literally. Else map lookup, falling back to the SDK `TStringTable` (from `strings.xml`). Then expands `%name%` by recursive lookup; `%%` becomes `%`; an unresolved nested name yields `-->name<--` (confirmed by `ProcessParameters` in Reforged `ui/helpers.lua:327`, which replaces both `%N%` and `-->N<--`); the Lua fills these markers in with arguments itself; missing top level returns `#####`.
- SDK `TStringTable` (`MergeStringTable` 000490c2) reads `strings.xml` with a lighter scanner: optional `ZXML` compression, then finds each `<Row`, takes the text of the first `<Data ...>` as key and the second as value (no whitespace collapse, entities decoded). `GetString(key, a1..a5)`: `%1%`-`%5%` insert positional args (an arg of form `%key%` is looked up), `%name%` inserts another entry, `%%` is a literal percent, unmatched `%` logs "Non-matched percent"; if config `stringtest` is nonzero all non-space characters become `x`; missing returns `#####` and logs ` ##### : Missing string for label '%s'` unless the table's quiet flag is set.
  `TWindowScript::LookupString(key, 5 args)` is the Lua-facing wrapper: leading `#` literal, else `TStringTable::GetString`.
- Language: the engine has no language switch of its own; Reforged Lua picks `Player.options.language` (default `en`) and layers `languages/<lang>/*` over root files, and sets fonts per language (section 4). `settings.xml` `<locale>` exists but its consumer was not located.

## 4. `.mvec` fonts

- Header `MVC4`, `u16 glyph count`; then a record table per glyph, little-endian: `u32 unicode`, `i16 minx, maxx, miny, maxy, advance` (all divided by 128.0 to get em units), plus a `u32` offset to the glyph's outline data (from `ttf_to_mvec.py` lines 154-163, 289-303 and `mvec_to_ttf.py`; table layout order not fully re-verified). Outline coordinates are `i16` (`<h`) in 1/128 units.
- Engine class `TMvecFont` (000cd35c..000cd66a, ctor takes the loaded bytes): accessors `GetGlyphIndex(codepoint)`, `GetAscent`, `GetDescent`, `GetLinePercent`, `GetLeading(bool)`, `GetGlyphData/Advance/MinX/MaxX/ScaleY(idx)`, `GetForceBold`, `GetBoldScale`, `SetBoldOverride(bool,float)`. Glyphs are vector outlines tessellated and drawn by the renderer (not bitmap atlases). Exact tessellation and the line-height formula (ascent/descent/leading/line percent) were not decompiled.
- Names: `fonts/fertigo.mvec` (default, string in binary), `fonts/default.mvec` fallback, `fertigo.mvec`. Reforged data: fertigo, choco3, fertigo-italic, fertigo-script, arial, plus language fonts in `mainloop.lua`: ja shipporimincho; zhs notoserif-sc; zht notoserif-tc; ko nanummyeongjo; ru/uk/sr/bg gabriela; el arima; hi notoserifdevanagari + yatraone label; bn notoserifbengali; th trirong + srisakdi label; default `standardFont = fonts/fertigo.mvec`, `labelFontName = fonts/choco3.mvec`. Reforged `ui/style.lua` consumes the globals `standardFont` / `labelFontName`.
- Wrapping, alignment (`kHAlign*`, `kVAlign*` flags above), kerning and `TText`/`TTightText` layout: not analysed here.

## 5. What the Lua calls expect (summary)
- `LoadIngredients()`, `LoadProducts()`: no args, no return; side effect is `CreateIngredient`/`CreateProductFromXML` calls with string-valued tables, in file order, ingredients file matched by suffix `ingredients.xml`.
- `LoadPortFileList(t)`, `LoadQuestFileList(t)`: fill `t[1..n]` with existing file paths in sorted-file, in-file order; skips missing files silently.
- `DisplaySplash`: blocks the coroutine; returns nothing.
- `GetNumUsers()`: integer. `GetCurrentUserName()`: string.
- `bsgGetString(key)`: string (`#####` if missing); `bsgStringExists(key)`: boolean; `bsgLoadStringFile(path)`: nothing.

## 6. Unknown
- Exact path construction in `Content::LoadListFile` (directory/file naming for ports, e.g. whether `ports/<name>/<name>.lua` is tried first); verify on a debugger or by re-reading the substr arguments.
- The common/user Preferences subfolder literal; Info.plist key used as folder name.
- Prefs cipher (`TEncrypt`), `IKEY` content derivation, per-key names in `KVPR`, `LoadXMLPrefs`, `SaveGameString`/`LoadGameString`/`NextSaveFileName` file layout and `ReadFromFile`/`WriteToFile` root folder.
- `TSettings` Lua functions' bodies (user creation, name storage).
- `WaitForMessage(1.0)` argument semantics; `time = 50` branch effect in DisplaySplash; opcode table of the embedded Lua build had 4 extra opcodes between 12 and 24 (my decode used a guessed shift), so DisplaySplash flow is approximate.
- `TFlashHost` behaviour for non-swf paths (Reforged passes `.jpg`).
- Config key controlling JPEG+`.mask.png` alpha combination; where button hit masks are read.
- Sprite-sheet XML attribute names beyond those seen in data; `.anm` format.
- mvec glyph outline encoding, line height, kerning, wrapping, alignment code.
- (The `.pfp` format and precedence are now verified, see `design-questions.md` section 3.)
- Whether `locale` in settings.xml is consumed anywhere.
