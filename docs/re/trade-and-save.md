# Trading, saving, quitting and the clock (Phase 3 end-to-end run)

Scope: what the engine must provide for "start Free Play or Story, see the map, travel, enter a port, buy and sell, open the ledger; new game, save, quit, reload works" beyond `port-window.md`, `map-window.md`, `ledger-and-game-natives.md`, `api-globals.md` and `api-windows.md`. Sources in order of trust: the Mac decompile (`re/mac/decomp/`, addresses are Mach-O VM addresses), then the Reforged Lua in the Reforged mod's `assets/`. "(inference)" marks anything not read directly. The Lua files cited are Reforged's; the original game's shop and market files were not compared.

Read-only checks against the current engine (`src/script/*Natives.cpp`, `generated/Stubs.inc`) were made by grepping registrations; I did not run anything.

## 0. Headline findings

1. **Nothing that the buy, sell, haggle, factory-purchase, quest and difficulty dialogs call is missing from the TSVs.** I checked every capitalised global the Reforged Lua uses and never defines (`docs/re/lua-globals-reforged.tsv`) against all five TSVs; what is left over is building and scenery names (`zur_market`, `cap_car1`...) and particle-script names, none of them natives.
2. **The only native in the whole trading and save path that is still a stub is `SoundEvent`.** It is a harmless no-op that logs `[unimplemented]` on first hit. `Dollars`, `RandRange`, `Floor`, `Mod`, `Date`, `GetLabel`/`SetLabel`/`EnableWindow`, `PostMessage`, `CreateNamedMessage`, `CurrentTime`, all user natives, `SaveGameString`/`LoadGameString`, `WriteToFile`/`ReadFromFile`, `CheckConfig`, `bsgGetString` are registered as real natives (`ScriptHost::boot` registers the generated stubs first and then the real natives over them, so a `STUB(...)` line in `Stubs.inc` does not mean the name is still a stub). Window types `Rollover`, `TightText`, `BSGWindow`, `CharWindow`, `TextEdit` are real. Natives still without a real implementation (checked by grepping for the quoted name outside `Stubs.inc`): `SoundEvent`, `GetState`, `ToggleFullScreen`, `QuitToMainMenu`, `QueryMuteGame`/`QueryRestartMode`/`QueryJumpToMenu`, `TagHotKey`, `StartGame`/`PauseGame`/`ResumeGame`/`PrepareGame`/`SelectPrize`, slots, fireworks, recipe designer setters, `BTSet*`, `FillRect`, `SetRectangleColor`, the hiscore/submission setters. None is reached by the Phase 3 path except `SoundEvent` (`ToggleFullScreen` only from Options).
3. **`_MONEY_SIGN` and `_NUMERIC_SEPARATOR` are not Lua globals.** They are two C `char` variables in the binary's `__data` section (symbols at 0x001d8020 and 0x001d8021, from `nm`). I read the file bytes: they hold `$` (0x24) and `,` (0x2c). No function in the decompile writes them, so they are constants. The engine's `globalOr(L, "_MONEY_SIGN", "$")` lookups are harmless but can be replaced by constants.
4. **There is no `EncodeSaveString`.** Reforged has only a local `DecodeSaveString` (`loadstring` plus `pcall`) and a hand-written serializer (`AppendTableToString`, `BuildSaveGameString`). The save is plain Lua source text. The engine needs nothing beyond stock Lua 5.0.2 `string.format`, `tostring`, `pairs` and `loadstring`.
5. **The `kQuitNow` handler does not save.** `AskQuit` saves in Lua, then posts `kQuitNow`; the native handler only asks the platform to leave the event loop. See focus-and-display.md §4: after the event loop ends, `ChocoThreeMain` runs `if Player then Player:SaveGame() end` unconditionally, so every exit (quit button, close box, Cmd-Q) saves once more.
6. **The ledger's date text is not the Lua `Date()`.** `LedgerWindow::UpdateLedger` calls the C++ `Date` (0x32a4) directly, so Reforged's `Date` override in `ui/helpers.lua` never reaches it. The two formulas disagree by 1-3 days after the first year; see section 7.

## 1. Natives and window types on the Phase 3 path

Status column: R = real in the engine (registered over the stub), S = stub, T = in a TSV, M = missing from every TSV. Addresses are Mach-O VM addresses (the TSV "source address" column); see the TSVs for the full argument notes.

| Native | Args -> returns | Address | Status | What the trading/save path needs |
|---|---|---|---|---|
| `Dollars` | number -> string | 00003680 (`re/mac/decomp/_free/d.c`) | R, T | section 2.2 |
| `Floor` | number -> number | 00003290 | R, T | C `floor`; also used as `Ceil(x) = Floor(x + 0.99999)` in the shop/market keeper-text sizer |
| `RandRange` | number lo, number hi -> number | 0000317c (`_free/r.c`) | R, T | section 2.3 |
| `Mod` | number a, number b -> number | 000031bc | R, T | unsigned remainder; `Mod(Player.time, 52)` gives the season week, `Mod(y, 4)` etc. in Lua date code |
| `Date` | number weeks -> string | 000032a4 | R, T | only the ledger's `day` text reaches it (section 7) |
| `CurrentTime` | -> number | 00004338 | R, T | ms since process start; used for `Player.lastSave` and the Free Play profile token |
| `GetNumUsers`, `GetCurrentUser`, `GetCurrentUserName`, `GetUserName`, `CreateNewUser`, `SetCurrentUser`, `IsNameInUse` | see `api-globals.tsv` | 00035e0e, 000357f8, 00035ca6, 00035d7c, 000361dc, 000360ec, 00036b2c | R, T | section 3 |
| `SaveGameString(s)` / `LoadGameString()` | string -> nothing / -> string | 00003a36 / 00004aa4 | R, T | section 3.3 |
| `WriteToFile(name, data)` / `ReadFromFile(name)` | -> nothing / -> string | 00003de0 / 00003ede | R, T | Free Play sidecar, section 3.4 |
| `SetCurrentGameMode(n)` | number -> nothing | 00035b6e | R, T | no effect on save storage (section 3.3) |
| `CheckConfig(key)` | string -> bool | 00003b24 | R, T | `"dev"` must be false, else `Player:IsDevModeActive()` taints the save as unranked |
| `GetLabel` / `SetLabel` | name -> string / name, text | 0xa14a2 / 0xa179e | R, T | the register dialog stores its whole state in the `count` text field (section 2.1) |
| `EnableWindow(name, bool)` | -> bool | 0xa18a8 | R, T | must return false and do nothing for a missing name (`ui_buysell.lua` asks for `number_max`, which does not exist) |
| `RealSetFocus` (via `SetFocus`) | name | 0xa085a | R, T | called after every digit in the register dialog |
| `Transition` | table | 000394ee | R, T | `FadeCloseWindow` is `Transition{"fadeout", ..., onend = CloseWindow(v)}`; `OpenBuilding` uses `CenterFadeIn`/`Center` |
| `PostMessage`, `CreateNamedMessage`, `CreateCloseMessage` | | 0xa2248, 0xa13f8, 0xa1320 | R, T | section 4 |
| `bsgGetString`, `bsgStringExists`, `_GetString` | | 00005ccc, 00006316 | R, T | section 2.4 |
| `SoundEvent(key)` | string | 00005166 | **S**, T | called by market (`buy`), shop (`sell`), haggle, `Building:OnClick`, `Player:SetMoney` (`money_in_account`). A silent no-op is correct for now |
| `Rollover` window | contents, command | 0x34338 | R, T | section 2.5 |
| `TightText` window | Text keys + pad | 0x38ca8 | R, T | section 2.5 |
| `BSGWindow`, `CharWindow`, `TextEdit`, `Text`, `Button`, `Bitmap`, `BitmapTint` | | | R, T | used by every dialog below |

Rows added to `api-globals.tsv`: `_MONEY_SIGN`, `_NUMERIC_SEPARATOR` (kind `c-data`; the stub generator skips non-`function` rows).

## 2. The market, shop, register, haggle, factory-purchase and quest dialogs

### 2.1 Call graph

A port building click runs the string `gCurrentBuilding=nil;<building>:OnClick()` (`port-window.md` 1.7). `Building:OnClick` (`sim/building.lua:922`) then:

1. `Player:AutoSave()` (calls `CurrentTime`; saves if 4 minutes of real time have passed since `Player.lastSave`; section 3.5).
2. `SoundEvent(cadikey)`.
3. The priority chain: quest completion and offers, aftermath dialogs, special orders, `CheckOutOfMoney()`, hints, tips, low-priority quests. Each of these is a `DisplayDialog` of `ui/ui_character_generic.lua`, `ui_character_yesno.lua`, `ui_quest_offer.lua` or `ui_quest_telegram.lua`.
4. `self:EnterBuilding(char, somethingHappened)` unless `Player.buildingsBlocked[name]`: `Market:EnterBuilding` -> `DisplayDialog{"ui/ui_market.lua", market=, char=}`; `Shop:EnterBuilding` -> `ui/ui_shop.lua`; `Factory:EnterBuilding` -> `ui/ui_factory.lua` when owned, otherwise in Free Play only the purchase flow below; `Casino:EnterBuilding` -> slots (Phase 4).
5. Generic character greeting or `building_empty` dialog; `self:HandleQuestCompletion(false)`; `SoundEvent(port.cadikey)`.

Everything is plain Lua on top of the dialog loop, and every `DisplayDialog` yields the `Main` coroutine, so `OnClick` must run inside it (the port click path already injects it there).

**Market (`ui/ui_market.lua`).** The dialog builds a grid of sacks from `market.inventory`, filtered by `(not ing.locked) or Player.ingredientsAvailable[name]` and sorted by `IngredientOrderFunction` (`a.name < b.name`, a plain `table.sort`). Per sack: a scaled `Bitmap`, a `Rollover{contents = "<name>:MarketRolloverContents()", command = ...}` and a `Text` named `<name>_price`. Layout variants for 1-3, 4-10, 11-16 and 17+ items use only `Floor` arithmetic. The Rollover command opens the register (`ui/ui_buysell.lua`) with `buy = ing`; on return `count` is the number bought. Buttons: `haggle` (enabled when `Player.rank > 1` and the merchant has not been angered), `ok`/`exit` (default and cancel), `help`.

On exit, `CloseMarketWindow` records `Player.lastSeenPort`, `lastSeenPrice`, `lowPrice`, `highPrice` (using `Floor(price / modifier)`), then **`SubTickSim()` once if anything was bought**, then `FadeCloseWindow("ui_market", "ok")`. `OpenBuilding("ui_market", market)` runs `CenterFadeIn` (or `Center` if `gCurrentBuilding` is already set) as a `Transition`.

**Shop (`ui/ui_shop.lua`).** Mirror image: `for code in pairs(Player.products)` filtered by `prod:GetInventory() > 0 and shop.buys[category.name]`, then `table.sort(products, ProductOrderFunction)` (`a.code < b.code`), so the iteration order of `pairs` does not leak into the layout. A `Rollover` per crate opens the register with `sell = prod`; `SellProduct` calls `prod:Sell(n)`, and `EnableWindow(prod.code, false)` once the stock hits 0, which needs the crate's `Bitmap`/`Button` to be named after the product code. A "sell all" button calls `SellProduct(prod, amount, true)` for every product and then `CloseShopWindow()`. `CloseShopWindow` also calls `SubTickSim()` after a transaction.

**Register (`ui/ui_buysell.lua`).** All state lives in the `count` `TextEdit` (`length = 7`, `ignore = kNumbersOnly`, `clearinitial = true`, `onkey = Update`) and the `subtotal` `Text`:

* `max` = `Floor(Player.money / price)` when buying, `item:GetInventory()` when selling. If the price is 0 (`Ingredient:GetPrice` returns `Player.itemPrices[name] or 0` for an unknown item), `Player.money / 0` is `inf` and `Floor(inf)` is `inf`; `tostring(inf)` is not a number the field can hold. This cannot happen in normal play (every market ingredient has a price after `Player:SetPort`).
* The keypad writes `tostring(n)` back through `SetLabel("count", ...)`; `Update()` re-reads it with `GetLabel("count")` and `tonumber`. `SetFocus("count")` is called every time. The subtotal is `" x " .. Dollars(price) .. " = " .. Dollars(total)` or `""` when the count is 0.
* `kNumbersOnly` (`ui/style.lua:29`) is an **ignore list, not a whitelist**: it names punctuation, Latin-1 symbols and letters. It does not contain the space, so a typed space is accepted into the field, `tonumber(" ")` is nil and the next comparison raises "attempt to compare nil with number". The original behaves the same unless its TextEdit refuses a space (not checked; the engine's `typeCharacter` inserts it). It is only a script error logged by the host, not a hang.
* OK -> `onOk(count)` (the closure from market/shop runs the purchase), then `LedgerUpdateDisplay` if defined (**nothing defines it**, so skipped), then `FadeCloseWindow("ui_register", count)`. `CloseWindow(v)` calls `ModalReturn(v)` whenever `v` is truthy, and `0` is truthy in Lua, so a zero-count OK returns `0`, a Cancel (`FadeCloseWindow("ui_register")`) returns nil. The market checks `count and count > 0`. **The return value must stay a Lua number**: `ModalReturn` stores the Lua value as-is (`gReturnValue`); do not stringify it (the stub signature `DoPopModal(returnValue:str)` is about the pop path, not this one).

**Haggle (`ui/ui_haggle.lua`, `sim/market.lua`, `sim/building.lua:1052`).** Pure Lua: `RandRange(1, count)` picks text variants, `RandRange(1, 2)` and `RandRange(1, 4)` drive the choices, `EnableWindow` toggles the four option buttons, `SoundEvent("negative_haggle")` etc. The result goes through the Lua global `gHaggleSuccess`. Market effects: success moves every ingredient price toward its low by 50% / 35% / 25% (difficulty 1/2/3) with `Floor`; failure sets it to `Floor(high * 1.0/1.25/1.5)`. These are written back with `SetLabel(ing.name .. "_price", ...)`; for locked-and-hidden ingredients no such window exists, so `SetLabel` on a missing name must be a no-op (the engine's is).

**Factory purchase (Free Play only; `Factory:EnterBuilding`, `sim/factory.lua:224`).** `GetPurchaseDialogue` text, `DisplayDialog{"ui/ui_character_yesno.lua", char=, building=, text="#"..text}` (returns `"yes"`/`"no"`), then `self:Purchase(price)` (`Player:SubtractMoney`, `MarkOwned`), a generic dialog and `ui/ui_factory.lua`. Natives: `Dollars`, `SetLabel`, `CloseWindow`, `FillWindow`, `CheckConfig` (the last two in `ui_factory.lua`). The factory screen's minigames (`RunMinigame`, chocolate/coffee factory windows) are Phase 4 and not on this path unless the player presses Make.

**Quest and character dialogs.** `ui_character_generic/yesno`, `ui_quest_offer`, `ui_quest_telegram`, `ui_telegram`, `ui_generic`, `ui_generic_yn` use only `MakeDialog`, `SetStyle`, `Text`, `Button`, `CharWindow`, `SetLabel`, `EnableWindow`, `CloseWindow`, `GetString`, `Floor`, `DebugOut`. `port-window.md` 2.6 covers how a dialog draws its character.

### 2.2 `Dollars` (00003680)

Exact behaviour, read from `re/mac/decomp/_free/d.c`:

* If `|x| >= 0.5` (and x is not NaN): format `|x|` with `%.0f` (C rounding, so exact halves round to even: 2.5 -> "2"), group the digits in threes from the right inserting `,`, put `$` in front, put `-` in front of that for negative `x`. Result for -1234.4 is `-$1,234`.
* Otherwise (including NaN and anything with `|x| < 0.5`, such as -0.4): the **literal** `"$0"`. Note this path does not use `_MONEY_SIGN`.
* `_MONEY_SIGN` is `$` and `_NUMERIC_SEPARATOR` is `,` (section 0, item 3). Reforged never changes them for other languages.
* The result is built in a 35-byte stack buffer, which is far more than any money value the game reaches (inference from the buffer size; the digit limit was not worked out exactly).

The engine's `formatDollars` (`src/script/Formats.cpp:26`) matches all of this. It also uses `%.0f` and returns `"$0"` for NaN; it uses `fabs` rather than the original's two-branch form, with the same result. `inf` gives `$inf` in both (inference; digits "inf" are grouped as text).

### 2.3 `RandRange` and prices

`RandRange(lo, hi)` (0000317c): `lo + (int)((((double)(int)hi - (double)(int)lo) + 1.0) * RandDouble())` using the global randomizer. Each argument is cast to `int` first, which drops fractions toward zero. The shop price code relies on this: `Player:RecalculatePricesForCurrentPort` passes fractional bounds for owned shops (`low = low * 1.2`, then `low + (high-low) * .66`), and `RandRange(low, high)` truncates both. The registered parameter type is `uint` in the decompile signature; by the convention of `Mod` the binding probably clamps negatives to 0 (inference; no Reforged call passes a negative).

`TRandom::RandDouble` (001635f8) is MT19937 (`Rand32` at 000587f0 is the standard twist/temper; tempering mask 0x13a58ad<<7 = 0x9d2c5680) combined as the usual 53-bit recipe: `((a >> 5) * 2^26 + (b >> 6)) * 2^-53`. The engine uses `std::mt19937` plus `uniform_real_distribution`, which is **not** the same sequence and differs between standard libraries. The game never depends on matching the original sequence (the seed comes from `rand()` at startup, `startup-and-assets.md`), so this only matters for cross-platform deterministic tests. To get one stream on every platform, implement `genrand_res53` directly.

What Reforged does with prices (so the engine knows the calls; none of it is native):

* Ingredient price on arrival: `RandRange(low, high)` with the in/out-of-season pair, bounds `Floor`ed after the difficulty multiplier (x1.15 at difficulty 2, x1.30 at 3), then `Floor(price * Tips.GetPriceModifier(...))`.
* Product price: `RandRange(price_low, price_high)` after the difficulty penalty (x0.90 / x0.80), blended toward the middle and then the low as `NumberSold()` grows (decay thresholds 2000/`decay_mod` and 7000/`decay_mod`), `Floor`ed at the end. At Rank 1 in Zurich/Douala some prices are overwritten with fixed values (sugar 7, cacao 11, b01 85, ...).
* In-Lua inconsistency worth knowing when testing: `Market:ComputeReasonableness` uses x1.25/x1.50 for ingredient cost where arrival pricing uses x1.15/x1.30. Not an engine matter.
* `StoryMode` sets `Player.difficulty` **after** `Player:Reset()` has already run `SetPort("zurich")`, and the second `SetPort("zurich")` is a no-op (`self.portName == portName`). So Zurich's first prices are rolled with difficulty 1 whatever the player picked. Observed in the Lua, not an engine issue.
* All arithmetic uses doubles. `string.format("%d", x)` in Lua 5.0.2 truncates a fractional double; the debug strings rely on that.

### 2.4 Strings (what the lookups must do)

The buy/sell/haggle text does not use the native `GetString` formatting except as a fallback. Reforged replaces `GetString`/`GetText` in `ui/helpers.lua` and resolves keys itself:

* `GetBaseString(key)` calls **`bsgGetString(key)`** first. It treats `""`, `nil`, `"#####"` and a result equal to the key as "missing" and then tries the old native `GetString` (captured as `_originalGetString`, i.e. the prelude `GetString` -> `_GetString`), accepting only a non-empty result that is not `#####` and not the key. Missing results are cached. `HasString(key)` (used to probe `key_1`, `key_2`, ... variants and the merchant dialogue candidates) is the same test. **Requirement:** `bsgGetString` for an unknown key must return one of `""`, `"#####"` or the key itself, and must not raise.
* `GetText(key, ...)` uses the Lua 5.0 implicit `arg` table (`arg.n`) and substitutes `%N%` and `-->N<--` itself (`ProcessParameters`), expands `%name%` through `GetBaseString`, resolves `[D|easy|medium|hard]` and `[Q|unit|e|m|h]` by `Player.difficulty`, and turns `%%` into `%`. All with stock `string.gsub` and `string.gfind` (5.0.2 names). `string.gsub` returns two values; `return string.gsub(...)` in `GetTextParams`/`GetText` therefore returns two, as in the original, and callers wrap or ignore it. Keep stock library behaviour.
* The BSG layer's skipped-character quirk noted in the workbench decision log (`Bought <b>%%1% sacks of %2%</b>%...`) matters **only** to `msg_buy_ingredient`, and **no Reforged Lua references that key** (`grep` finds it only in `strings.xml`). The purchase confirmation the player sees is `GetMerchantDialogue("market_thanks...")`: a weighted pick among `market_thanks_<quest>_<char>_<item>`, `..._<char>`, `..._<holiday>`, `_first`, `_haggle_good`/`_bad` candidates that exist (checked with `HasString(k .. "_1")`), using `RandRange(1, totalWeight)` (weights 50/25/20/15/10/5, seasonal item 40, holiday 25) and then `RandRange(1, n)` among numbered variants, then `{merchant}`, `{item}`, `{quantity}`, `{price}`, `{total_price}`, `{unit}` substitution (`SubstituteTextParams`, pattern `{(.-)}`). So the on-screen check promised in the workbench decision log should look at the keeper bubble text, not `msg_buy_ingredient`.
* `date_format` is `%1% %2%, %3%`, `score_format` is `%1%/week`, `rank_N` are plain names, `month_N` English names in `strings.xml`.

### 2.5 `Rollover` and `TightText` in these dialogs

* Every sack, crate and the register icon is a `Rollover` with `contents` = a string of Lua such as `sugar:MarketRolloverContents()` or `_AllProducts['b01']:ShopRolloverContents()` (the register builds `item.name .. ":BuySellRolloverContents()"` for ingredients and the `_AllProducts[...]` form for products). The engine's `RolloverWindow` already evaluates the string with the popup as the window under construction and fits it (`InputWidgets.h`). The called methods return `MakeDialog{ BSGWindow{ fit=true, color=rolloverColor, frame="controls/rollover", Bitmap{...}, AppendStyle{...}, Group(text) } }`. So the popup needs `BSGWindow` with `frame`, `color`, `fit`, and `Group` of `TightText { x=64, y=0..., label = "#<b> " .. name .. "</b>" }` rows (`<b>` is supported by the markup layout, `fonts-and-text.md`).
* `TightText` labels here always start with `#` (literal text, no string lookup) and contain `<b>`; the later rows are `"# " .. inventory`, `"# " .. priceRange`, `"# " .. lastSeen`, `"# " .. GetString("click_buy"/"click_sell")`. The row `y` values are 16 px apart; `TightText` forces its width to the text width plus pad 3 (`TextWidgets.h`).
* Market sacks have `fit = false` with a fixed `w/h` of 64/38/32/30 depending on layout; shop crates have `fit = true`. The fit behaviour comes from `Rollover` itself ("always fits to children", `api-windows.md`); `fit = false` in the Lua is a key the constructor ignores or honours; I did not check which. Unknown (section 8).
* Nothing in these dialogs depends on hover timing beyond the 125 ms popup fade already implemented.

## 3. Save and load

### 3.1 What the save string is

`Player:BuildSaveGameString()` (`sim/player.lua:1623`):

1. `SyncIntegrityWithRuntime()`, `HighScoreModel:EnsureStats` and `UpdatePersonalBest` (Lua).
2. Builds a list of strings starting with `"return {"`.
3. `AppendTableToString(Player, list)`: `for k, v in pairs(t)`; a **number key is written without its key** (positional); any other key as `key=` (the key is concatenated as-is, so it must be a valid Lua identifier and not a keyword); string values with `string.format("%s%q,", key, v)`; numbers and booleans with `tostring`; tables as `key={`, recursion, `},`; functions are skipped; other types are skipped. So `Player`'s methods vanish and everything else, including `mode`, `time`, `money`, `stringTable`, `lastSave`, `freePlayProfileId`, is saved.
4. Appends `deliveries={` + one `{...}` per active quest that has `GetSaveTable` + `}}`.
5. `table.concat` of everything: no whitespace and a trailing comma after every item. A new game gives roughly 9.5 KB (`build/test_out/port/user_zurich/profiles/Tester/save.txt`, 9,605 bytes, checked); the late-game size is not known.

Load is `Player:LoadGame(mode)` -> `LoadNativeStoryTable()` -> `SetCurrentGameMode(0)`, `LoadGameString()`, `DecodeSaveString`: `loadstring(s)`, then `pcall` of the resulting function, accepted only if it returns a table. A parse error returns nil and the caller prints "No Story Mode save data found" and returns false.

### 3.2 What the engine must preserve

* **Stock Lua 5.0.2 `string.format("%q")`.** In 5.0.2 it escapes `"` and `\` with a backslash, a newline as backslash + newline, `\r` as `\r`, and NUL as `\000`; every other byte (including UTF-8) is written raw. The matching lexer reads backslash-newline back as a newline. The engine uses the stock library, so this is already right; do not substitute a 5.1+ library (`%q` differs, `\r`, `\0`) and do not re-encode strings on the way in or out.
* **`tostring(number)`** is `%.14g`, so non-integers lose precision and large values print as `1e+15`. All counts and money in the game are integers well below 1e14. `inf` and `nan` would print as bare words that parse as global variable reads (nil), silently dropping the field: a stock-Lua property, nothing for the engine to add.
* **`pairs` order** matters in exactly one place for the file: numeric keys are written positionally, so a table with integer keys that are not exactly `1..n` (a sparse array, or integer keys that live in the hash part) is compacted or reordered on reload. Stock 5.0.2 iterates the array part `1..n` first, then the hash nodes in node order; string hashes depend only on the bytes (5.0 `luaS_hash`), so the order is deterministic across runs. It need not match the original; it only has to be stable and be the stock implementation. Player arrays that must survive (`itemRecipes`, `activeTips`, `pendingAnnouncements`, `pendingSpecialOrders`, `deliveries`, `medals`) are proper sequences built with `table.insert`.
* **Keys must be identifiers.** A string key such as `"b-01"` or a keyword (`end`, `and`, `continue` in this patched Lua) would make the file unparsable. Reforged uses `end_` for that reason. Not an engine concern, but a good first check if a reload fails.
* **Limits of the stock parser** (Lua 5.0.2: 200 nested C calls in the parser, 262143 constants per function, flush of table constructors every 32 items) apply to a `return {...}` chunk. Nesting depth is about 8, the constants limit is far above 9.5 KB worth of distinct keys; whether a very late save (thousands of distinct strings) can hit the constants limit is unverified.
* **The prefs write is synchronous.** `SaveGameString` stores the value under the pref name `savegame` for the current user with the flush flag set (`TPrefs::SetStr(..., 1)` -> `SavePrefs`), so the save is on disk when the call returns. `UserStore::saveSave` (temporary file + rename) matches. The original prefs file is encrypted; the engine's is not, by design.

### 3.3 Story slot and `SetCurrentGameMode`

`SaveGameString` and `LoadGameString` (both read in `_free/s.c`/`l.c`) take no mode: they use the current user (`TSettings::GetCurrentUser`, -1 means no user: save is a no-op, load gives `""`) and the pref `savegame`. `SetCurrentGameMode` (00035b6e) does nothing in either path. That is why Reforged observed "`LoadGameString()` returned the Story payload in both modes" (its comment in `LoadGame`): it is by design, not a bug in the port. Reforged's Free Play therefore never uses the native slot; it uses the sidecar below. The engine's `nativeSetCurrentGameMode` (stores a pref only for mode 0) is fine.

### 3.4 Free Play sidecar

* File name: `"reforged_freeplay_" .. SafeFileToken(profileId) .. ".choco3"`; `profileId = tostring(self.freePlayProfileId)`, created once by `Player:EnsureFreePlayProfileId()` as `"<name token>_<GetCurrentUser()>_<CurrentTime()>"` and stored in the Story save. `SafeFileToken` lowercases and maps everything outside `[a-z0-9_-]` to `_`, trims, caps at 32 characters.
* Write: `WriteToFile(fileName, saveString)`; the return value is only compared with `false` (the original returns nothing, i.e. nil). The engine returns a bool and refuses reserved names (`users.txt`, `prefs.txt`, `scores.txt`, `profiles/`, `*.tmp`, anything with `:` or a leading slash). The generated name cannot collide.
* Read: `ReadFromFile(fileName)` must return `""` when the file is absent (Reforged checks via `DecodeSaveString`). The original's missing-file behaviour was not verified (`api-globals.tsv`: "presumably empty"); the engine returns `""`.
* `NextSaveFileName` lists every top-level `*.choco3`, so sidecars also show in the dev save menu. Only `dev/dev_saves.lua` calls it.
* `CurrentTime()` is saved inside both saves (`Player.lastSave`) and is relative to process start. `LoadGame` ends with `self.lastSave = CurrentTime()`, so a stale value from the previous process is overwritten before `AutoSave` looks at it.

### 3.5 Autosave

`Building:OnClick` -> `Player:AutoSave()`: `elapsed = CurrentTime() - (lastSave or CurrentTime())`; saves when `4*60*1000 - elapsed <= 0`. With a headless fixed-step clock, `CurrentTime` must advance with the script clock (it does: `Clock.nowMs`); in a scripted run shorter than four minutes of simulated time no autosave fires, so a test that expects a save must trigger one (pause menu, Quit, or a profile creation).

### 3.6 What the Lua needs at startup and after

* `Main` (`ui/mainloop.lua`) runs `LoadIngredients`, `LoadProducts`, `LoadPorts`, `LoadQuests`, routes, character data. Then: `if GetNumUsers() > 0 then Player:LoadGame("story") else Player:Reset() end`. Only after that: `Player:ReloadStrings()`. **So `LoadGame`/`Reset` run before the Reforged string files are loaded.** `Player:LoadGame` ends with `UpdateLedger("newplayer")`; the wrapper calls `UpdateLedgerFactoryCovers` (`EnableWindow` on six `*_factory_cover` names, no window exists: must be a no-op) and `_AllQuests[Player.questPrimary]:GetSummary()` (strings not yet loaded: lookups must not raise), and skips `RealUpdateLedger` because no ledger exists (the wrapper tests `if RealUpdateLedger then`, section 1.1 of `ledger-and-game-natives.md`).
* `Player:Reset(restoreTable)` itself needs only data already loaded (`_AllPorts`, `_AllCategories["user"]`, `_AllQuests` for `CreateDeliveryQuest`, `_IngredientOrder`) and the natives `DebugOut`, `GetCurrentUserName`, `CurrentTime` (indirectly), `CheckConfig("dev")` (through `IsDevModeActive`), `PrepareTravelPrices`/`RandRange` via `SetPort`. It reads `Player.options.language` (default `"en"`).
* `Player:ReloadStrings()` calls `LoadQuestFileList(outTable)` (real) and `bsgLoadStringFile` for `strings.xml`, `dialogue_strings.xml`, `catalogue_strings.xml`, `kitchen_strings.xml` and one `<quest>_strings.xml` per quest file, and `ClearStringCache()` first (Lua). Missing files are ignored by the native.

## 4. The exact sequences

### 4.1 First launch, no profile, "Story Mode" (`ui/mainmenu.lua`, `StoryMode`)

1. `GetNumUsers() == 0`: the Free Play and Change Player buttons are disabled (`Update`).
2. `DisplayDialog{"ui/ui_entername.lua"}` -> name string or nil (uses `IsNameInUse`, `GetLabel`, `SetFocus`). If non-nil: `DisplayDialog{"ui/ui_difficulty.lua"}` -> `1`, `2` or `3`, or nil. The difficulty dialog uses `Bitmap`, `Button{graphics = {}}`, `SetBitmap`, `FadeCloseWindow("difficulty_select", level)`, `CenterFadeIn`; no new natives.
3. `CreateNewUser(name)` (new index = old count; becomes current), `Player:Reset()`, `Player:SetGameMode("story")`, `Player.name = GetCurrentUserName()`, `Player.stringTable.player = Player.name`, `Player.difficulty = choice`, `Player:SetPort("zurich")` (no-op, see 2.3), `Player:SaveGame()`.
4. `Player:SaveGame()` with `GetNumUsers() > 0`: `SetGameMode(self.mode or "story")` (calls `SetCurrentGameMode`), `EnsureFreePlayProfileId()`, `BuildSaveGameString()`, then for Story `SetCurrentGameMode(0)` + `SaveGameString(s)`, finally `self.lastSave = CurrentTime()`.
5. Because `GetNumUsers() > 0` now: `Player:IsFreePlay()` false -> `SetGameMode("story")`, `gButtonsDisabled = true`, `SoundEvent("ui_click")`, four `FadeOut{... time=500}` and a fifth on `main_background` whose `onend` runs `LaunchStorySequence`.
6. `LaunchStorySequence`: `Player:SetGameMode("story")`; `q = _AllQuests["tut_01"]`; if complete or active, `SwapToModal("ui/mapview.lua")`; otherwise `SoundEvent("Stop_Music")`, `SoundEvent("zurich_sting")`, `SoundEvent("zurich")`, `SwapToModal("ui/portview.lua")` and `QueueCommand(function() q:Offer(main_alex, zur_station) end)`. A new game takes the second branch (the tutorial offer in Zurich).

Cancelling the name or difficulty dialog skips to step 5 with `GetNumUsers()` still 0, so nothing happens. A cancelled difficulty dialog after a name was typed leaves no user (the user is created only after both).

### 4.2 Resume (profile exists)

At launch `Player:LoadGame("story")` (section 3.6). The menu's `Update` (run at the end of the 800 ms button fade-in) enables Free Play and shows `GetString("welcome", GetCurrentUserName())`. "Story Mode": step 5 above, with `if Player:IsFreePlay() then Player:SaveGame(); if not Player:LoadGame("story") then return end end` first when a Free Play company is resident. Then `LaunchStorySequence` as above: `tut_01` is complete or active for any saved game past the first minutes, so the map opens.

### 4.3 Free Play (`FreePlayMode`; there is no `LaunchFreePlay`)

1. `GetNumUsers() == 0`: a `ui_generic` dialog with `freeplay_requires_story` and return.
2. `Player:SaveGame()` (saves whichever mode is resident: Story -> native slot, Free Play -> sidecar).
3. `Player:LoadGame("free")`: loads and repairs the Story table (needed: a missing Story save refuses Free Play), reads the sidecar (`LoadFreePlaySidecarTable`; must have `mode == "free"`), `Player:Reset(freeTable)`, `SetGameMode("free")`, `SyncFreePlayFromStory(storyTable)`, `RebuildCreationProducts()`, migrations (which call `SaveGame` if they changed anything). With no sidecar: `Player:CreateNewFreePlay(storyTable)` (`Reset()`, `SetGameMode("free")`, profile id, `options.noQuests = true`, `SetPort("zurich")`, starting money 20,000/25,000/30,000 by difficulty, story unlock sync, `NormalizeFreePlayShopOwnership`, `MigrateFreePlayV3/V4`, `CharacterMobility:Initialize`) then `SaveGame()`. Both end with `UpdateLedger("newplayer")`, `self.lastSave = CurrentTime()` and return true.
4. `SoundEvent("ui_click")`, fades, `onend` -> `SwapToModal("ui/mapview.lua")`.

Natives used: the user and file natives above, `CurrentTime`, `CheckConfig`, `Dollars` (log text), `RandRange` (character mobility and price rolls), `Transition`. Nothing new. Free Play pause menu extras (`RestartFreePlay`, `hiscore_details.lua`) are Lua; `RestartFreePlay` ends with `SwapToModal("ui/mapview.lua")` from inside the pause dialog.

### 4.4 "Menu" in game -> pause -> "Main Menu"

The ledger's `menu` button runs `PauseButton` -> `DisplayDialog{"ui/ui_pause.lua"}`. `ui_pause.lua` calls `Player:SaveGame()` on open ("hard-commit"), `PauseTravel()` if defined, and its `main_menu` button runs `SwapToModal("ui/mainmenu.lua")`. That swap happens in the **pause dialog's** modal: the old map/port modal (with the ledger attached to it) stays on the chain underneath. Choosing Story/Free Play again then does `SwapToModal("ui/mapview.lua")` in that same upper modal, and `GrabLedger` detaches the ledger from the lower modal and re-parents it (`ledger-and-game-natives.md` 1.2 handles `pInstance` with a parent). I did not trace that path in the decompile or the window manager; it only works if `DisplayDialog`'s pause modal accepts `SwapToModal` (it does in the prelude: `ClearModalChildren; ReadFile; DoFitToChildren`) and the `DoMainWindow` loop in `Main` is not returned to (inference).

### 4.5 Quit (`AskQuit`, `ui/mainloop.lua`)

Reached only from the main menu's quit button. Steps:

1. Return at once if `gQuitActive` or `gTravelActive`.
2. `gQuitActive = true`; `DisplayDialog{"ui/ui_generic_yn.lua", text = "confirm_quit"}` -> `"yes"` or other.
3. On `"yes"`: `Player:SaveGame()`; `PostMessage(CreateNamedMessage(kQuitNow, "Quit"))` (kQuitNow = 5, `CreateNamedMessage` returns a Lua-visible message userdata). `gQuitActive` stays true (the program is ending).
4. Otherwise `gQuitActive = false`.

Native side (`window-system.md` 4.2 and 3.6; `TWindowManager::DispatchWindowMessage` 0x4ce2c): a type-5 message is deleted and `TPlatform::Exit` (00059da6) runs. That calls `TPlatformData::Exit` (0008d562), which sets an "exit requested" byte in the platform object (+0x9d0) and calls `QuitApplicationEventLoop`. **It does not save the game or the prefs.** On the way out the destructor `~TPrefs` (0005733e) records a lifetime stamp pref (`sCreateSavedLifeTime`) and calls `SetStr(..., 1)` (flush). Nothing else is written. Engine requirement: on `kQuitNow` stop the main loop at the end of the current frame (the engine sets `quit_`), do **not** treat the Lua coroutine's return from `AskQuit` as an error, and make sure the `UserStore` has nothing buffered (its writes are synchronous). Message delivery order is FIFO and the post happens after `Player:SaveGame()` returned, so the save is complete before exit.

What the original does when the window's close button or the OS quit event arrives is not known (the platform event loop was not traced for this task): whether it also posts a `kQuitNow`, saves, or prompts.

## 5. Time and the clock

* `Player.time` is a count of **weeks**, 1 at the start of a new game (`Player:Reset`: `t.time or 1`), incremented by `TickSim(ticks)` (`sim/sim.lua`). Nothing in the engine adds to it. `Player.subticks` (0-4, saved) counts quarter-weeks: `SubTickSim()` adds 1, refreshes the ledger (`UpdateLedger("all")`) and calls `TickSim(1)` when it reaches 4, `TickSim` resets it to 0. `SubTickSim` is called once when the player leaves a market or shop after at least one transaction (section 2.1, market and shop); it is not called by travel.
* `TickSim` order: `Player.time = Player.time + ticks`; `subticks = 0`; `Player:UpdateHolidays()` (Lua date components); `CharacterMobility:Update`; **`AnimateProduction()` (native, nil before the first ledger exists)**; `Player:RunFactories()`; `UpdateSpecialOrders()`; `Player:ExpireInventory()`; `Tips.Update()`; `Player:UpdateSupplies()`; `UpdateLedger("all")`. A tick before the ledger has been created raises an error at `AnimateProduction`; `ledger-and-game-natives.md` 1.1 recommends registering it always.
* Season week: `Mod(week, 52) + 1`, in `Ingredient:IsInSeason` and `Ingredient:Buy`. No other time constants in the engine. `Mod` must return an integer remainder; for the non-negative integers here that is plain `%`.
* There is **no interest, no rent and no wage mechanic** in the Reforged Lua (a case-insensitive search for "interest" finds only dialogue word lists). The weekly engine effects are factory output, inventory expiry, orders and tips, all Lua.
* **Rank is not computed by the engine.** It is set by quest completion (`sim/quest_functions.lua:116`, `Player:SetRank(self.rank)`), which calls `HighScoreModel:RecordRank` and `UpdateLedger("rank")`. The native ledger only displays `rank_%lu` from the string table and shows/hides the score line (rank 1 hides it). No thresholds exist in native code.
* Score on the ledger: `floor(money / time)` (native, `ledger-and-game-natives.md` 2) -- the only game formula in native code besides `Date`.

## 6. `Date` versus the Lua date

Native `Date(weeks)` (000032a4, `api-globals.tsv`; engine `dateFromWeeks`): `days = weeks*7 + 171`; `year = 1946 + days/365` (integer division, **365-day years, no leap days**); the day in year `days % 365` is walked through the month table, with February 29 days when the year *offset* `days/365` passes a Gregorian test (so offset 0, the year 1946 itself, counts as a leap year; this is why week 1 still prints June 27). Reforged's Lua `Date` and `Player:GetDateComponents` start at day 178 and use **real** Gregorian leap years of the printed year. Replicating both formulas in Python (my own check of the documented native formula against the Lua code, not a run of either):

| Week | Lua `Date` | Native `Date` |
|---|---|---|
| 1 | 1946-06-27 | 1946-06-27 |
| 27 | 1946-12-26 | 1946-12-26 |
| 28 | 1947-01-02 | 1947-01-03 |
| 100 | 1948-05-20 | 1948-05-22 |
| 157 | 1949-06-23 | 1949-06-25 |
| 300 | 1952-03-20 | 1952-03-23 |
| 500 | 1956-01-19 | 1956-01-22 |

Consequence: the ledger's `day` text is written by the native on every `UpdateLedger("time"/"all"/"newplayer")`. `ui/ledger.lua` only sets the first label from the Lua `Date`; after that the native text wins, and the holiday logic (`UpdateHolidays`, Lua dates) can disagree with the ledger by up to three days in the first decade. If the engine wants the Reforged date on the ledger it must call the Lua global `Date` from `RealUpdateLedger` instead of the native (a deviation; the original calls the native). Keep the native for fidelity unless review decides otherwise.

## 7. Blocking issues for buying and selling, and what is fine

Blockers: **none found in natives.** To make the flow work check that:

1. `Rollover` popups run `<ingredient>:MarketRolloverContents()` where `<ingredient>` is a global named like the ingredient (`Ingredient:Create` does `_G[t.name] = t`); `LoadIngredients` (real) must therefore have created the globals before the dialog. A missing global is only a Lua error in the popup.
2. `DisplayDialog` must return numbers unchanged (2.1 register).
3. `EnableWindow`/`SetLabel` for absent names are no-ops and `GetLabel("count")` returns the field text (the engine's `GetLabel` returns `<name>_is_not_found` for a missing window; the register would then call `tonumber("count_is_not_found")` -> nil; this only happens if `count` is absent).
4. `bsgGetString` of an unknown key returns `""`/`"#####"`/the key.
5. `SubTickSim` after the first transaction updates the ledger through `RealUpdateLedger("all")`; the ledger lives under the port modal while the market dialog is on top. `RealUpdateLedger` works on the ledger object's own children, so it is independent of the modal chain; `EnableWindow` walks the whole modal chain (`findInChain`), which is what lets the Lua wrapper's factory-cover updates work from inside a dialog.
6. Fades: `Transition` on a window that is not in the **top** modal returns without calling `onend` (`api-globals.tsv`): `FadeCloseWindow("ui_register")` therefore only works while the register is the top modal, which it is.

Known script-level quirks that are not engine bugs: a space typed into `count` raises a Lua error; `LedgerUpdateDisplay` is never defined; the `msg_buy_ingredient` string is unused.

## 8. Unknown

* Whether the original's `TextEdit` refuses a typed space when `ignore` is set (it matters only for the register's "attempt to compare nil with number" error).
* How the original handles window close or OS quit (does it post `kQuitNow`, save, or ask?). Not traced.
* Whether `RandRange` clamps negative arguments to 0 as `Mod` does (the registered signature is unsigned). No Reforged caller passes a negative value.
* Whether `fit = false` on a `Rollover` or `fit = true` on a crate `Rollover` changes the size or hit rectangle in the original (`Rollover` is documented as "always fits to children"; the Lua passes both).
* Exact late-game save size and whether `loadstring` of it can hit Lua 5.0.2's constant-table limit; the original's maximum size for the `savegame` pref (the `SavePrefs` chunk code at 00056bac was not read for limits).
* The original's behaviour for `ReadFromFile` on a missing file (the engine returns `""`).
* The path `pause -> Main Menu -> Story/Free Play` with the lower modal's ledger parent (4.4) was only reasoned, not traced in the window manager, and was not run.
* Whether the engine should keep `std::mt19937` + `uniform_real_distribution` or switch to `genrand_res53` for cross-platform determinism (a decision, not a fact).
* The Windows executable might differ from the Mac one for `_MONEY_SIGN` and the date code; only the Mac binary was read.
