# Chocolatier 2: the logo editor (`LogoEditor`, `.LOGO` signs) and the buy/sell screen

Scope: facts for the two Chocolatier 2 areas after the map: (A) `LogoEditor` and the `.LOGO` port signs that the port scene skips, (B) the market, shop, haggle and numeric-entry dialogs (`ui/marketui.lua`, `ui/shopui.lua`, `ui/haggle.lua`, `ui/buysell.lua`, `ui/numericentry.lua`, `sim/market.lua`, `sim/shop.lua`).

Method and caveats. `chocotwo.exe` is packed, so **no Chocolatier 2 native is read from code**. Every Chocolatier 2 fact below is what the Lua passes to a native, reads back, or does itself. Lua is read from the bytecode in `re/choc2/pfp_extracted/` (disassembled with a workbench script). Citations `file:N` are the line numbers carried by the bytecode (they match the original source lines). "Fact" = read from bytecode, a data file or the Chocolatier 1 decompile. "Inference" = derived, not seen. Chocolatier 1 addresses are VAs in `chocolatier.exe` (`re/choc1/decomp/_unnamed.c`). Chocolatier 3 facts are quoted from `docs/re/trade-and-save.md`, `coffee-factory-and-recipes.md` (section 6), `fonts-and-text.md`. Related: `choc2-port-and-ledger.md` (section 1 `bsgAddSprite`, section 3 moods).

Lua 5.0 bytecode reading note: `TEST A B C` jumps (executes the following `JMP`) when truthiness of `R(B)` equals `C`. All the control-flow readings below were checked with that rule.

---

## 0. Headline findings

1. **`GetLogoString()` returns Lua source text, not a delimited record.** All three callers do `s = GetLogoString(); if s then f = loadstring(s); if type(f) == "function" then t = f() end; if t then gSim.logo = t end end` (`ui/logoedit.lua:118-122,130-134`, `ui/logoedit_basic.lua:139-143,156-160`). So the string is a chunk that **returns a table**, and `gSim.logo` is that table. The logo table is a flat table of short keys (section 1.3). Exact whitespace and key order are not observable.
2. **Everything the player sees in the logo is kept in `gSim.logo`**, a plain Lua table saved by the ordinary save serialiser (`sim/sim.lua:199-216`, which writes `key=%q,` for strings, `key=value,` for numbers and booleans, `key={...},` for subtables). The editor natives hold their own working copy while open and hand it back through `GetLogoString`.
3. **The sign is a 350x200 canvas.** Background `logo/bgN.jpg` is 350x200; graphics `logo/logoN.png` are 200x200; the `.LOGO` sprite is that canvas times `scale` (0.24-0.35 on factories, 0.5 on the San Francisco Baumeister sign), centred on `x,y`. Placement sliders look like "p in 0..1 across the free space" (section 1.6, inference with a Chocolatier 1 cross-check).
4. **The sign is shown only when the player owns the factory it sits on** and `gSim.rank > 0` (`sim/port.lua:75`). Click opens the Basic editor, then the port view is rebuilt.
5. **Buy/sell is all Lua over a small set of natives**: `DisplayDialog` (value returned by `CloseWindow(v)`), `GetLabel`/`SetLabel`/`SetFocus`/`EnableWindow`, `Rollover`, `TextEdit`, `DynamicWindow{ontext=true, script=...}`, `SetCharacterMood`, `PlaySound`. The numeric entry is `NumberPad`, a **Lua function** built in `ui/numericentry.lua` (not a native widget).
6. The `ontext` DynamicWindow is the receipt updater: a 1x1 invisible window that re-runs the Lua string `script` whenever a text edit changes (section 2.6; matches `TDynamicWindow` in Chocolatier 1).
7. **Chocolatier 2 haggling is a random roll against where the prices sit in their range** (`LMarket:ComputeHaggle`, `LShop:ComputeHaggle`), with mood modifiers and a quest bonus; Chocolatier 3's is a difficulty-based success flag. See section 3.

---

# Part A. Logo editor

## 1. What exists

### 1.1 Callers (fact)

| Caller | What | Source |
|---|---|---|
| Port sign click `DoPlayerLogo` | `DoModal("ui/logoedit_basic.lua")`, then `SwapToModal("ui/portview.lua")`, then `StopPulsing()` | `sim/port.lua:60-63` |
| Quest function `AwardLogo` | with an argument it runs `DoModal("ui/logoedit_basic.lua")`; with nil it returns `GetString("q_logo")` ("Design a company logo.") and `false` | `sim/quest_functions.lua:506-512` |
| Dev menu | `DoModal("ui/logoedit.lua")` (the "Logo Editor" item) | `dev/devmisc.lua:63` |
| Basic -> Expert | `PopModal("simplelogo")`, `DisplayDialog{"ui/logoedit.lua"}` (after storing the string in `gSim.logo`) | `ui/logoedit_basic.lua:155-163` |
| Expert -> Basic | `PopModal("logoedit")`, `DisplayDialog{"ui/logoedit_basic.lua"}` | `ui/logoedit.lua:117-125` |

The editors never carry state themselves between modes: each one calls `GetLogoString()`, loads it, assigns `gSim.logo`, closes, and the next one reads `gSim.logo` through its `logo=` key.

### 1.2 `LogoEditor{...}` constructor keys (fact)

Both editors build `MakeDialog{ name=..., Frame{ x=kCenter, y=..., w=700, h=..., LogoEditor{...} } }`:

| | Basic (`logoedit_basic.lua`) | Expert (`logoedit.lua`) |
|---|---|---|
| dialog `name` | `"simplelogo"` | `"logoedit"` |
| Frame | `x=kCenter, y=50, w=700, h=365` | `x=kCenter, y=kCenter, w=700, h=500` |
| `LogoEditor` keys | `x=0, y=0, w=kMax, h=kMax, logo=gSim.logo, logox=5, logoy=85, itemsAllowed=<list>` | same keys, `logoy=295` |

* `logo` is the table to edit (`gSim.logo`). Inference: the native reads the keys of section 1.3 from it at construction.
* `logox, logoy` is where the 350x200 **preview** is drawn inside the editor window (basic (5,85) to (355,285); expert (5,295) to (355,495)). Inference from `logoW=350, logoH=200` locals and the layout.
* `itemsAllowed` is a plain array of texture names (no `item/` prefix) built as: for every `LItem:KnownRecipes()` entry with `inventory + sold > 0`, `name .. "_big"` (`logoedit.lua:17-20`, basic `:17-20`). These are the 64x64 `item/<name>_big.png` product pictures (116 `_big` files exist). Inference: they extend the graphic list beyond the 12 stock graphics (sections 1.4, 1.5). The native receives the table; the Lua does not otherwise use it.
* The `LogoEditor` window has no other key; the controls are **ordinary child windows the native finds by name** (inference: the same pattern as `SignEditor`, `coffee-factory-and-recipes.md` section 6, which reacts to `kSliderValChanged` (type 9) messages from named child sliders).

Child widgets that carry names the native presumably reads (fact: names; behaviour: inference):

| Widget | name | Initial value the Lua gives |
|---|---|---|
| Slider | `background` | 0 |
| Slider | `graphic` | 0 |
| Slider | `font` | 0 |
| Slider | `textsize` | `(logo.ts - 10) / (100 - 10)` |
| Slider | `textalpha` | `logo.ta / 1000` |
| Sliders | `tColorH`, `tColorS`, `tColorV` | `logo.tch/1000`, `tcs/1000`, `tcv/1000` (hue slider rail uses `control/colorbar_v`) |
| Sliders | `textx`, `texty` | `logo.tx/1000`, `logo.ty/1000` (captions "Horizontal Position", "Vertical Position") |
| Sliders | `logoscale`, `logoalpha`, `logorotate`, `logox`, `logoy` | `logo.ls/1000`, `la/1000`, `lr/1000`, `lx/1000`, `ly/1000` (captions Size, Transparency, Rotation, Horizontal Position, Vertical Position) |
| TextEdit | `nameedit` | `label = logo.t or "BLAHBLAH"`, `length = 25`, `ignore = kIgnoreBadSignChars` |
| Toggle Button (kToggle, `CheckButtonGraphics`) | `outline` (both editors), `italic`, `bold` (Expert only) | state set after construction by `SetButtonToggleState(name, true)` when `logo.tfo`/`tfi`/`tfb` is `> 0` (`logoedit.lua:146-155`, basic `:169-170`) |

The slider values are **thousandths in the table and 0..1 on the slider** (fact, from the `/1000` constructor arguments); text size is the exception, 10..100 stored directly and mapped to slider `(ts-10)/90`. The background, graphic and font sliders start at 0 whatever the table holds. **Unknown** whether the native moves them to match the table at construction (it must, or reopening an edited logo shows sliders out of sync) and how a 0..1 slider value maps to an index (floor(v*n)? round(v*(n-1))?).

`kIgnoreBadSignChars` is the string `<>\"` (characters `<`, `>`, `\`, `"`; `ui/styles.lua:139`). Reading: `<`/`>` would be parsed as text-engine markup, and `"`/`\` would break a quoted Lua string in the logo string (inference, consistent with finding 1).

### 1.3 The logo table (fact: defaults and key names; meanings are mostly inference)

Default for a new game, `Simulator:new(name)` (`sim/sim.lua:125-129`), written before any editor runs:

| key | default | read as | evidence |
|---|---|---|---|
| `b` | `"bg4"` | background name; texture is `logo/` + `b` (`.jpg`) | `logo/bg1..8.jpg` are the only backgrounds; Chocolatier 3 `SetBackground` also prepends `logo/` |
| `l` | `"logo/logo10"` | graphic path **with directory** | the loader adds `logo/` only if `l` has no `/` (`sim.lua:304-306`, an old-save fix); so an item graphic is presumably `item/<name>_big` |
| `lx`, `ly` | 500, 500 | graphic position, 0..1000 | sliders `logox`, `logoy` = value/1000 |
| `la` | 1000 | graphic alpha 0..1000 | slider `logoalpha` |
| `ls` | 1000 | graphic scale 0..1000 | slider `logoscale` ("Size") |
| `lr` | 1000 | graphic rotation 0..1000 | slider `logorotate`. Default is the slider's top end; if the full range is one turn, that is no rotation (unverified) |
| `lf` | 0 | **unknown** (no slider, no Lua reader); candidate: "flip" | nothing reads it |
| `t` | the player name | the company text | `TextEdit nameedit` `label = logo.t` |
| `to` | 5 | text options; candidate: alignment flags (`kHAlignCenter + kVAlignCenter` = 1 + 4 = 5), changed by `Set{Left,Center,Right}Justify` | Chocolatier 1 `TLogo` default `flags` is 5 and Chocolatier 3 `SignEditor` makes its text sprite with alignment 5 (inference) |
| `ts` | 40 | text size 10..100 | slider `(ts-10)/90` |
| `ta` | 1000 | text alpha | slider `textalpha` |
| `tch`, `tcs`, `tcv` | 500, 500, 500 | text colour HSV, 0..1000 each | sliders `tColorH/S/V` |
| `tx`, `ty` | 500, 500 | text position 0..1000 | sliders `textx`, `texty` |
| `tfo` | 1 | outline flag (on) | `SetButtonToggleState("outline", true)` when `> 0`; the Lua also forces `tfo = 1` when it is nil (`logoedit.lua:22`, basic `:22`) |
| `tfi`, `tfb` | absent (off) | italic, bold flags | same pattern, Expert editor |
| font key | absent | **unknown name** (best guess `tf`, since the flags are `tfo/tfi/tfb`) | the `font` slider exists; no key in the default |

Flags are numbers; the Lua tests `flag > 0`. Absent equals off.

### 1.4 Asset tables (fact)

* **Backgrounds** `logo/bg1.jpg` to `bg8.jpg`: 8 files, all 350x200 (sizes 25.9-44.7 KB).
* **Graphics** `logo/logo1.png` to `logo12.png`: 12 files, all 200x200 RGBA. (The directory has 20 files: 8 + 12.) Chocolatier 1 had `logo1..10` and `logopos1..4.png`, `textpos.png` (builder UI), not present in Chocolatier 2.
* **Item graphics**: `item/<name>_big.png`, 64x64, only for products in `itemsAllowed` (section 1.2).
* **Fonts** `fonts/*.mvec` (9): `baldur`, `bolton`, `caliban`, `edda`, `fertigo`, `fontin`, `humanistic`, `noir-et-blanc`, `romanuncial`. `fertigo` is `standardFontName`, `caliban` is `simpleFontName` (`ui/styles.lua`). **Unknown** whether the `font` slider spans all nine or a subset and in what order; alphabetical directory order is the likely list (the Mac `TFile::GetNextFile` is sorted, `coffee-factory-and-recipes.md` Unknown 5). There are no bold/italic font files: bold, italic and outline are synthetic in the text engine (`fonts-and-text.md` 3.7: `<b>` adds a stroke of about 1.25% of the size, `<i>` shears x += y/3, `<outline color size>` strokes, default 2 px).

### 1.5 The natives (all arguments seen; effects are inference)

None of these returns a value except `GetLogoString`. All are called with no window argument, so the native acts on the **currently open LogoEditor** (inference: one at a time).

| Native | Arguments seen | Called from | Effect (inference unless stated) |
|---|---|---|---|
| `SetLogoText(text)` | the string from `GetLabel("nameedit")` | the small "OK" TextButton next to the name field (`x=460,y=20`, `default=true`), `logoedit.lua:50`, basic `:109`; the same command sets the global `logoChanged = true` (set only, nothing in the Lua ever reads it: fact) | sets `t`, re-renders the preview. The text is **not** pushed on each keystroke; the field commits on that OK or Enter (fact: no `onkey`/`ontext` use) |
| `GetLogoString()` | none | "Done", "Basic Mode", "Expert Mode" buttons | returns the Lua chunk of section 1.7; may return nil (all callers guard) |
| `RandomizeLogo()` | none | the Randomize button, last call of its closure (`logoedit_basic.lua:81`) | see 1.8 |
| `SetTextPosition(x, y)` | `(0,0.5)`, `(0.5,0.5)`, `(1,0.5)`, and `(r, 0.5)` with `r in {1, 0.5, 0}` from the randomizer | the three basic layout buttons and the randomizer | sets `tx = round(x*1000)`, `ty = round(y*1000)` (inference: arguments are 0..1, the table 0..1000) and moves the matching sliders (`textx`, `texty`) |
| `SetGraphicPosition(x, y)` | `(1,0.5)`, `(0.5,0.5)`, `(0,0.5)`, and `(1-r, 0.5)` | same | sets `lx`, `ly` likewise |
| `SetTextColor(r, g, b, a)` | the eight swatches: `(255,255,255,255)`, `(209,27,27,255)`, `(245,121,12,255)`, `(23,148,16,255)`, `(33,94,217,255)`, `(150,29,229,255)`, `(242,128,227,255)`, `(0,0,0,255)` | basic swatch buttons (24x24 at `x=370+27*i, y=205`), randomizer | converts 0..255 RGB to HSV and stores `tch/tcs/tcv`; the fourth argument is always 255 (alpha 0..255; whether it writes `ta` is unknown) |
| `ToggleOutline()` / `ToggleItalic()` / `ToggleBold()` | none | the checkboxes (`outline`, and Expert `italic`, `bold`); `ToggleOutline` also from the randomizer | flips `tfo` / `tfi` / `tfb` (0/1) and re-renders. They **toggle**, they do not set: after the Lua has done `SetButtonToggleState` the two states agree, but the randomizer's `ToggleOutline()` does not touch the button (whether the native also updates the button is unknown) |
| `SetLeftJustify()` / `SetCenterJustify()` / `SetRightJustify()` | none | Expert "Left", "Center", "Right" buttons (`x=30/130/230,y=50, scale 0.5`) | text justification; candidate `to` values 4, 5, 6 (inference, section 1.3) |

Colour swatches are the table `colorOptions` in `logoedit_basic.lua:27-37` (8 entries, last one black); the randomizer picks among the same 8.

### 1.6 Rendering a sign from the table (mostly inference; the structure is supported by three twins)

Fact side:
* The port sign is a `bsgAddSprite` table `{ x, y, image=".LOGO", logo=gSim.logo, scale, target=<building>, contents="PlayerLogoRollover()", command=DoPlayerLogo, labelx=x-120, labely=y-10 }`. `PlayerLogo(t)` (`sim/port.lua:66-82`) sets `image=".LOGO"` and resolves `target` through `LBuilding:ByName`; at layout time the returned closure refreshes `logo`, `contents`, `command`, and **only calls `bsgAddSprite` when `gSim.logo`, `gSim.rank > 0`, `target` and `target.owned` are all truthy** (`:75-78`). Nothing is added otherwise, so there is no sign and no click target before the player buys that factory.
* Placements in the six port files (centre x,y; scale): San Francisco 195,234 @0.35 (target `sf_factory`); New York 722,55.5 @0.3 (`ny_factory`); Paris 523,208.5 @0.286 (`pa_factory`); Sydney 426.5,161 @0.291 (`syd_factory`); Shanghai 100,153.5 @0.24 (`sh_factory`); Mahajanga 572,53 @0.25 (`ma_factory`). Each file writes x,y as `left + w/2`, `top + h/2` of a w x h rectangle whose size matches 350x200 x scale: Paris w=100, h=57 and 350 x 0.286 = 100.1, 200 x 0.286 = 57.2; New York 132x75 (sign about 105x60 at 0.3). **So the scaled canvas is 350x200 x scale (a 7:4 sign).**
* The Baumeister sign (`BaumeisterSign`, only `sanfrancisco`: x=341+175/2, y=55+100/2, w=175, h=100): `LQuest:Variable("bcsign")` 0..5 shows `ports/baumeister<N>.png` at scale 1; at 6 or more it switches to the player's logo with `scale = 175/350 = 0.5`, `y = 55 + 100/2 - 10`, same `contents`/`command` (`sim/port.lua:84-109`). `StartFireworks()` runs when `bcsign > 5` (`:115-121`).
* Hover: `PlayerLogoRollover()` builds a rollover text with the string key `logo_edit` ("Click to change your corporate sign"); `StopPulsing()` is called first.

Inference side (the native is not readable). The sign is the composite of:
1. the background `logo/<b>.jpg`, full 350x200;
2. the graphic `l` (200x200 for stock, 64x64 for item pictures) placed by `lx, ly` (p in 0..1: x = p x (350 - drawn width), y = p x (200 - drawn height)), scaled by `ls`, alpha `la`, rotated by `lr`. Cross-check with Chocolatier 1 (Lua `ui/logoinfo.lua`): its three fixed graphic slots are x = 0, 75, 150 (= (350-200) x 0, 0.5, 1) at y = 0 with a 200x200 graphic, and the Chocolatier 2 basic layout buttons call `SetGraphicPosition` with exactly 0, 0.5, 1. Default `lx = ly = 500, ls = 1000` thus puts a full-size graphic in the middle;
3. the text `t` through the normal text engine: font from the font key, size `ts` pixels on the 350x200 canvas, colour = HSV(`tch/1000 x 360`?, `tcs/1000`, `tcv/1000`) with alpha `ta/1000`, bold/italic/outline from `tfb/tfi/tfo` (the engine has `<b>`, `<i>`, `<outline>`: `fonts-and-text.md` 3.7; outline colour unknown), alignment from `to`, position from `tx, ty` by the same "p x free space" rule (the text box size is unknown).

Chocolatier 1 `TLogo` (the class `docs/re/choc1-natives.md` section 8 names; vtable `004e8504`, Init `00415a90`, redraw `004155f0`) is the ancestor and fixes the structure:
* It is a window that renders into an offscreen texture of `round(350 x s) x round(200 x s)` where `s = windowWidth / 350` (Init `00415a90`: `s = w/350`, surface `350s x 200s`, and the text rectangle, font size scale by `s`).
* Keys and defaults: `background` (`"logo/bg1"`), `graphic` (`"logo/logo1"`), `text` (`""`), `flags` (5), `textX` 10, `textY` 10, `textW` 330, `textH` 180, `gx` 0, `gy` 0, `font`, `fontSize` 30, `fontColor` (1,1,1,1), `dropColor` (0,0,0,1) (all in Init `00415a90`).
* Draw order (`004155f0`): background, graphic at its rectangle, then the text twice: a drop-shadow copy in `dropColor` offset by +1,+1 (and an extra +2,+2 copy when `s > 0.5`), then the text in `fontColor` at the unshifted rectangle. Text is laid out in the `textW x textH` box with `flags` (5 = centre/centre).
* Its Lua (`ui/logoinfo.lua`, `ui/logobuilder.lua`) keeps `gSim.logo = {bg, graphic, pos, align, font, fontSize, fontColor, dropColor}` plus `gSim.companyName`: **a different, index-based table**. The Chocolatier 1 build has no `GetLogoString`, `RandomizeLogo` or the other natives, so the editor natives are new in Chocolatier 2 (checked: none of the names occurs in `re/choc1/strings.tsv`).
* Chocolatier 3 has no company logo; its `SignEditor` (`coffee-factory-and-recipes.md` 6) is dead code with `sign` keys `textH/S/V/A/X/Y/Size`, `logoScale/A/R/X/Y`, `background`, `logo`, `font`, `text` and slider messages: the same fields as the Chocolatier 2 ones (`tch/tcs/tcv/ta/tx/ty/ts/ls/la/lr/lx/ly/b/l/font/t`) and strong evidence of the slider-by-name design.

Whether Chocolatier 2 has a drop shadow or draws the outline in a fixed colour is not known: the editor has a single `outline` checkbox and no colour for it.

### 1.7 The logo string grammar

Fact: a Lua chunk accepted by `loadstring` whose call returns a table; the table has the string/number keys of 1.3. Everything else is **Unknown**: whether it starts with `return`, key order, quoting (the save serialiser uses `%q`), spacing, whether numbers are written as integers (the defaults are integers) or floats, whether the font key is present before a font is chosen.

Inference for a faithful reimplementation: emit `return { b="bg4", l="logo/logo10", lx=500, ly=500, la=1000, ls=1000, lf=0, lr=1000, t="Name", to=5, ts=40, ta=1000, tch=500, tcs=500, tcv=500, tx=500, ty=500, tfo=1, tfi=0, tfb=0, tf="..." }` (the last two keys and the font key are guesses). Because the Lua only ever does `loadstring`, any chunk that returns an equal table is accepted; the engine can pick its own format. Keep integers integral and quote `t` with `%q`-style escaping.

### 1.8 `RandomizeLogo()` and the Lua-side randomiser (fact for the Lua part)

The Basic editor's Randomize button (`logoedit_basic.lua:54-82`, the `LogoRandomizer` closure) does, in this order:
1. if `fpWrite` exists, `fpWrite{"RandomLogo", gSim.weeks}` (telemetry, see below);
2. `i = bsgRandom(1, table.getn(colorOptions))`, then `SetTextColor(r, g, b, 255)` from entry `i`;
3. `roll = bsgRandom(1, 100)`; if `roll > 50` then `ToggleOutline()`;
4. `roll = bsgRandom(1, 100)`; `x = 1` if `roll <= 33`, `x = 0.5` if `33 < roll <= 66`, `x = 0` if `roll > 66`;
5. `SetTextPosition(x, 0.5)` and `SetGraphicPosition(1 - x, 0.5)` (text and graphic go to opposite sides, or both centred);
6. `RandomizeLogo()`.

So the native `RandomizeLogo` is what is left: it is the only thing that can change the **background, graphic, font** (and possibly size and alpha). Inference: it draws random picks for background (1..8), graphic (the 12 stock graphics, perhaps item pictures from `itemsAllowed`) and font, using the engine's global random generator, the same one behind `bsgRandom` (our `Rng`). Not read: the ranges, whether it touches size or alpha, whether it refreshes the three sliders. Gaps in the bytecode line numbers (lines 76-80) show removed lines between the position calls and `RandomizeLogo()`.

`fpWrite` is a PlayFirst telemetry native that may be absent: `ui/mainloop.lua` wraps it only when it exists, and every use is guarded by `if fpWrite then`. The engine can leave it undefined. Event names seen: `RandomLogo`, `BasicLogoClose`, `AdvancedLogoClose` (second field `gSim.weeks`).

### 1.9 Basic and Expert layouts (fact; the Lua builds them, nothing native)

Basic (`logoedit_basic.lua`): name field + "OK" (x=460,y=20), caption `logo_background` and slider `background` (x=370,y=75,w=310); `logo_graphic` and slider `graphic` (y=105/125); `logo_basicfont` and slider `font` (y=155/175); 8 colour swatches (y=205); `outline` checkbox (x = kMax - devButtonW/2 - 15, y=197) and caption; `logo_basiclayout` with three layout buttons at x=370, 475, 580, y=255 (`control/layoutA`, `layoutB`, `layoutA` flipped); bottom row y=320: "Expert Mode" (`logo_advanced`, x = kCenter - devButtonW), "Randomize" (`kCenter`), "Done" (`done`, name `ok`).
Layout buttons: A = `SetTextPosition(0,.5)` + `SetGraphicPosition(1,.5)` (text left, graphic right); B = both 0.5; flipped A = `SetTextPosition(1,.5)` + `SetGraphicPosition(0,.5)`.

Expert (`logoedit.lua`): name field, `background` slider, a text group (outline/italic/bold toggles at x=30/130/230, y=12; Left/Center/Right buttons at y=50; `font`, `textsize`, `textalpha`, `tColorH/S/V`, `textx`, `texty` sliders at y=80..290), a graphic group (`graphic`, `logoscale`, `logoalpha`, `logorotate`, `logox`, `logoy`), buttons "Basic Mode" (x=355) and "Done". "Done" in both editors: `GetLogoString()` -> `gSim.logo`, telemetry, `CloseWindow()`.

All strings come from `strings.xml` (`logo_*`; e.g. `logo_name` "Enter a name for your company:", `logo_basiclayout` "Click %logo_advanced% to view more options...").

### 1.10 Saving and loading (fact)

`gSim.logo` is saved as an ordinary table field `logo` (`sim.lua:274` copies it into the save table; `Simulator:LoadGame` copies it back, then `if logo.l and not string.find(logo.l, "/") then logo.l = "logo/" .. logo.l end`, `sim.lua:304-306`). Old saves without `logo` keep the default.

### 1.11 What an implementation needs, and what it must guess

Needs: `LogoEditor` window (preview 350x200 at `logox, logoy`, slider/toggle/TextEdit message handlers by name), natives of 1.5, `bsgAddSprite{image=".LOGO", logo=, scale=}` composite rendering, `SetButtonToggleState` (exists in the engine: `WindowNatives.cpp`), `kIgnoreBadSignChars` (defined by `ui/styles.lua`).
Guesses to make explicit in code: slider-to-index mapping for background/graphic/font, HSV range (`tch` 0..1000 -> 0..360?), the text box width and clamping for `tx,ty`, outline colour and thickness, `lr` mapping, the font order, `lf` and `to` meaning, the string format.

---

# Part B. Buy/sell, market, shop, haggle

## 2. Dialog flow (fact)

### 2.1 Entering a market or shop

* `LMarket:OnActivate` (`sim/market.lua:49-75`): sets `gActiveBuilding = self`; `SetAmbient`; `char = self:SelectCharacter()` (returns the first character, `SelectCharacterFreePlay`: `character[1]`). If `gSim.rank == 0 and self.name == "sj_farm" and gSim.mode ~= "free"` (the tutorial farm), it shows `ui/chartalk.lua{char, fullbody=GetString("backtotask"), ok="ok"}` instead. Otherwise it builds `items`: for each key of `self.inventory` (`pairs`, so Lua-table order, **no sort**), `LItem:ByName(key)` if it exists; plus `LItem:ByName("powder")` when `gSim.rank >= 3` and `inventory.cacao` is set (`:67-69`). Then `DisplayDialog{"ui/marketui.lua", market=self, char=char, items=items}` and `self:ExitBuilding()`.
* `LShop:OnActivate` (`sim/shop.lua:67-83`): `items` = every entry of `LItem:OwnedProducts()` (products with stock). If `#items > 0` it shows `ui/shopui.lua{shop, char, items}`; else, if `char` is set, `ui/chartalk.lua{char, body="#"..GetString("shop_noitems"), ok="ok"}` ("You don't appear to have anything to sell..."). Then `ExitBuilding()`.
* Market `inventory` is `{ itemname = true }` (a set), unlike Chocolatier 3's list filtered by `locked/available` and sorted (`trade-and-save.md` section 2). The tables are `LItem`s: both screens edit `item.price`, `inventory`, `lastprice`, `lastport`, `usetime`, `sold` **on the shared item object**, and `gShopItems = gDialogTable.items` aliases the same objects.

### 2.2 The market dialog (`ui/marketui.lua`)

Reads `gDialogTable.market`, `.char` (default `market.character[1]`), `.items`; `market.dialog` is the caption text; `market.noHaggle` disables the haggle button.
* Frame: `x = ui.xOffset + 75` (80 and wider when `#items > 30`), `y = ui.yOffset + 60`, `w = screenWidth - 150` (`screenWidth - 80 - 20` when > 30), `h = 240` plus `floor((n - 57)/3 + 0.9) x (fontHeight + 2)` when `n > 57`, `fit=true`, `color=MarketColor`. Item count steps: `n > 45` shrinks fonts to 16/14. Price fonts are 18 (`standardFontName`), 16 for small prices.
* Items are a matrix of sacks: cell = `ui.sackWidth + 5` by `ui.sackHeight + 37 + fontHeight + 5` (50 and 47 here, so 55 x about 108), `columns = floor(fieldW / cellWidth)`; when `4 < n <= columns` it halves the columns to make two rows (`:177-180`); rows are balanced by `floor((n + rows - 1)/rows)`.
* Each cell is a `Rollover{x,y,w,h, fit=true, contents=item:RolloverTarget("click_buy"), command=EnterNumber(name, gx, gy)}` containing `image/pricetag` (centred, 54x20 text named `<name>_price`, label `"#" .. bsgDollars(price)`), a sack picture `image/icon_sack`, `icon_sack_inseason` or `icon_sack_outseason` (the seasonal ones only for items with non-default `startWeek/endWeek`, chosen by `item:InSeason(bsgMod(gSim.weeks, 52))`), and the item image `item/<name>`.
* Buttons (x = fieldW - devButtonW): `haggle` (name and label `haggle`), `help` (`HelpDialog(4)`, `SmallButtonGraphics`), `ok` (label `close`, `MediumButtonGraphics`, command `CloseDialog`). `DialogLedger()`, `BuildingBanner(market)` and `char:Visual(x, y, charNameFontDark)` are added (Lua functions in `ui/uihelp.lua`, `sim/character.lua:78`).
* After `MakeDialog`, `EnableHaggle()` runs once.

`EnableHaggle` (`marketui.lua:88-94`): if `allowHaggle` (not `market.noHaggle`) then `EnableWindow("haggle", not char:IsAngry() and not market.owned and gSim.rank ~= 0)`, else `EnableWindow("haggle", false)`.
`TrackPrices` (`:73-83`, same function in `shopui.lua:69-79`): per item, `lowseen` = the lowest `price` seen and `highseen` = the highest (first value fills both); read later by the ledger.

### 2.3 `EnterNumber(name, x, y)` = the buy action (`marketui.lua:130-166`)

1. `item = LItem:ByName(name)`; `count = DisplayDialog{"ui/buysell.lua", item=item}` (the string given to `CloseWindow(v)`), `count = tonumber(count) or 0`.
2. If `count > 0`: `total = count x item.price`.
   * If `total <= gSim.money` (note: `<=`): `PlaySound("buy")`; **mood**: if `char.moodWeek ~= gSim.weeks` then `char:SetMood(char.mood + LCharacter.kPurchaseDelta)` (+10) and `char.moodWeek = gSim.weeks` (once per game week per keeper); `portName = GetString(market.port.name)`; `transactionMade = true`; `item.inventory += count`; `item.lastprice = item.price`; `item.lastport = market.port.name`; `item.usetime = gSim.weeks`; `gSim:QueueMessage(GetString("buy_notice", tostring(count), GetString(item.name), bsgDollars(item.price), portName))` ("Bought %1% sacks of %2% for %3% each at %4%."); `gSim:AdjustMoney(-total)`; `gSim:InventoryChanged()`; `gSim:FlushMessages()`.
   * Else: `DisplayDialog{"ui/okdialog.lua", body="buy_lowcash"}`.
3. `EnableHaggle()`.
4. **Tutorial exit** (`:163-165`): `if count > 0 and gSim.rank == 0 and gSim.quest and (gSim.quest.name == "tutor09" or == "tutor11") then CloseDialog()`: the market closes by itself after the first purchase in those two tutorial quests (this runs even when the purchase failed for lack of cash, because only `count > 0` is tested).

`CloseDialog` (`:113-126`): for every item with `inventory ~= 0` it stores `lastprice = price`, `lastport = market.port.name`; sets `gShopItems = nil`, `gMoreLabel = nil`; `CloseWindow()`; then `gSim:TickDay()` **if a transaction was made** (section 4.3).

### 2.4 `ui/buysell.lua`: the quantity dialog

Reads `gDialogTable.item`. `gNumberPadTarget = item.name`. `buy` mode when `item.recipe` is nil (ingredient), `sell` mode when it is set (chocolate).
Constants: Frame 460x230 at `x=290, y=ui.yOffset+85`, `color=DialogColor`, `fit=true`. `fh = bsgFontHeight(dialogFont)`.

Children, in order (`:69-84`): `item:BigRollover(5, 5, 1)` (a `Rollover` with the 64x64 `item/<name>_big` picture, or `item/unknown_big` when `item.lab` is truthy and `item.known` is not; `item.lab` is presumably the lab-discovered recipes); `Text` (x=70, y=0, h=fh, label `LabelString(item.name)`); `Text` (x=70, y=fh) with the prompt `"#" .. GetString(<buy|sell> .. "_howmany", bsgDollars(item.price))` ("At %1% each, how many sacks would you like to buy?" / "I'll pay %1% a case, how many would you like to sell?"); `Text name="subtotal"` (x = 138, y = 2 fh, w = kMax); `NumberPad(138, 3 fh, item.name)`; `TextEdit` named `item.name` (x=188, y=3 fh + 6, w=85, h=29, `length=7`, `label=""`); `TextButton` buy|sell (x = 230 - devButtonW - 5, y = 128 + 3 fh, command = okFunction); `TextButton` `cancel` (default = true, command = cancelFunction); and the receipt updater `DynamicWindow{x=0,y=0,w=1,h=1, ontext=true, script="if gNumberPadCallback then gNumberPadCallback() end"}` (section 2.6).
After building: `if not item.recipe then EnableWindow("number_max", false) end` (the Max key is disabled when **buying**, enabled when selling; in the Chocolatier 3 window system a disabled window is hidden, `window-system.md` line 187, so the Max key is probably absent when buying), then `gNumberPadCallback()` once, `SetFocus(item.name)`.

* `okFunction`: `v = GetLabel(gNumberPadTarget)`; clears both globals; `CloseWindow(v)` (a **string**, possibly empty). `cancelFunction`: clears the globals, `CloseWindow()` (nothing returned; the caller treats it as 0).
* `gNumberPadCallback` (closure over `item`, `buysell.lua:28-52`), run after every keypad press and on every text change:
  1. `v = tonumber(GetLabel(target)) or 0`;
  2. **sell clamp**: `if item.recipe and item.inventory < v then v = item.inventory; SetLabel(target, tostring(v))`. The buy branch is an empty `elseif` (the bytecode jumps to the same instruction either way, source lines 37-41 are absent): **buying is not clamped to the cash on hand**;
  3. `n = tonumber(v) or 0`; `total = n x item.price`; `s = tostring(n) .. " x " .. bsgDollars(item.price) .. " = " .. bsgDollars(total)`;
  4. buy and `gSim.money < total`: `s = "<font color='ee2222'>" .. s` (red, never closed);
  5. `n == 0` -> `s = ""`; `SetLabel("subtotal", s)`; `SetFocus(target)`.
* **Enter / Escape (fact + deliberate deviation):** the dialog's Cancel button is the only one with `default=true` (the OK button has no flag), and Chocolatier 2 has no `cancel` key. `TTextEdit::OnChar` (0xa65e0) does not consume Enter (`KeyHit` returns 0), and `TModalWindow::OnChar` (0x5ebfa) handles only Enter, pressing the `default` button found by `FindDefault::Process` (0x10a114): the original therefore cancels on Enter. The engine instead treats a `default` button whose label key is `cancel` as the Escape button and presses its first visible sibling (the OK) on Enter (`WindowManager::legacyCancelDefault`, `ModalWindow::onChar`). Test: `tests/check_choc2_enter.py`.
* The OK is not disabled when the cash is short; the purchase is refused later (2.3) with the `buy_lowcash` dialog.

### 2.5 `ui/numericentry.lua`: the pad

`require`d by `mainloop.lua`; defines the global `NumberPad(x, y, target)` (and uses `gNumberPadTarget`, `gNumberPadCallback`).
* Background `Bitmap{x = x or ui.screenWidth, y = y or ui.screenHeight, image="control/register"}` with 12 children: digit buttons 1-5 at `(21 + 28 i, 39)` for i = 0..4, digits 6-9 and 0 at `(11 + 28 i, 68)` for i = 0..4 (6,7,8,9,0), a delete button at `(11 + 5 x 28, 68)` (`control/reg_btn_del`, `_del_d`), and the Max button `InsertMaxButton{x=3, y=9, name="number_max"}` (`control/reg_btn_max_up`, `_max_down`).
* Digit key `NumberKey(n)`: if `gNumberPadTarget` is set and `string.len(GetLabel(target)) < 6`, `SetLabel(target, label .. tostring(n))`; then call `gNumberPadCallback` if set. So the pad refuses a 7th digit (the TextEdit itself allows `length = 7`), appends to the existing text with no leading-zero logic (typing 0 then 5 gives "05", read as 5), and starts from whatever the field already holds (it is empty at open).
* Backspace: if the label is non-empty, `SetLabel(target, string.sub(label, 1, len - 1))`, then the callback.
* Max: `item = LItem:ByName(target)`. If `item` and `item.recipe` (selling): `SetLabel(target, tostring(item.inventory))`. Else (buying): `v = gSim.money - 2 x Simulator.travelCost` (`Simulator.travelCost = 487`, `sim/sim.lua:9`), floored at 0, `SetLabel(target, tostring(bsgFloor(v / item.price)))`; callback. The buying branch is unreachable from `buysell.lua` because it disables `number_max` when buying (2.4), but the Lua exists.
* Each button is a `Button{typename="Button", graphics={"control/reg_btn", "control/reg_btn_d", "control/reg_btn"}}` with three layered `Text` children (named `label`, yellow, white, white fonts) built through `DoWindow`, `FitToChildren`, `SelectLayer(0..2)` (the engine has these: `WindowNatives.cpp`, `CoreNatives.cpp`).
* Keys on the keyboard go to the `TextEdit` named `item.name`, which has no `ignore` list: any character is accepted into the text (up to 7); `tonumber` turns non-numbers into 0 in the callback (the callback does **not** rewrite the field). Chocolatier 3 differs: `kNumbersOnly` (as an ignore list), `clearinitial`, `onkey = Update` (`trade-and-save.md` section 2, "Register").
* `kNumbersOnly` exists in `ui/styles.lua:137` but is not used by these two files.

### 2.6 The `DynamicWindow` `ontext` use

`DynamicWindow{x=0, y=0, w=1, h=1, ontext=true, script="if gNumberPadCallback then gNumberPadCallback() end"}` (`buysell.lua:83`). Chocolatier 2 adds the key `script` (a Lua string) next to the file path `contents` that Chocolatier 1 and the ledger tabs use. The only use of `script` in the game is this one (checked: the ledger and badge use `contents=`).
Chocolatier 1's `TDynamicWindow` (`docs/re/choc1-natives.md` section 4; vtable `004e6f9c`, Init `00409970`, rebuild `00409560`; `ontext` -> a `DynamicWindowTextUpdater` listener `004097e0`/`00409890`) rebuilds itself on every message of type 7. Reading for Chocolatier 2 (inference): on every `kTextEditChanged` message (`kTextEditChanged = 7`, `ui/Message.h`, posted by `TextEditWindow::notifyChanged`) the window deletes its children and runs `script` in its own context; at creation it builds once. The window is 1x1 and has no children after the script (it builds nothing), so it exists purely as a **text-change hook that calls `gNumberPadCallback()`**.
Engine today: `choc2::DynamicWindow` logs `ontext` and `script` as unsupported (`DynamicWindow.h:16`, `DynamicWindow.cpp:17`). What it needs: for a DynamicWindow with `ontext`, subscribe to `kTextEditChanged` anywhere in the modal and run `script` (the Lua string) with the dialog as context; keep running `contents` files as now.
Open points: **Unknown** whether the message is filtered by edit name (only one TextEdit exists in this dialog), whether `SetLabel` on a TextEdit posts the message (harmless either way because the clamp stabilises after one rewrite and the buttons also call the callback directly), whether the creation-time run happens (the Lua calls the callback explicitly after `MakeDialog` anyway).

### 2.7 The shop dialog (`ui/shopui.lua`)

Mirrors the market with a different layout, same calling convention.
* Frame `x = ui.xOffset + 30`, `y = ui.yOffset + 60`, `w = screenWidth - 60`, `h = 240`, `color=ShopColor`. Count steps (`shopui.lua:28-49`): `n > 14` adds 25 to `h` and sets the price font size to 23; `n > 20` sets it back to 18; `n > 39` uses 16 (price) and 14 (count font); `n > 42` adds `floor((n - 42)/3 + 0.9) x (fontHeight + 2)` to `h`. Price strings above 9999 get a `<font size='floor(size x 0.8)'>` prefix (`smallPriceString`).
* Four layout bodies chosen by item count (`shopui.lua:478-481`, `n` = number of items): `n <= 2` big platters (`ui.platterWidth + 2` by `platterHeight + 8`, `image/platter`), `n <= 8` the second platter layout, `n >= 15` a text list (`n > 26` three columns, else two), otherwise (9..14) the third layout (an icon grid like the market's). Each platter is a `Window{fit=true}` with `image/platter`, the item `Rollover` (`item:Rollover(x, y, scale, "click_sell", function() EnterNumber(name, gx, gy) end)`, so the click label is `click_sell`) and a price `Text` named `<name>_price`. In the list layout the price string gets the small prefix above 999999.
* Buttons: `haggle`, `sell_all` (label `sellall`), `help` (`HelpDialog(5)`), `ok` (label `close`).
* `EnableHaggle` (`:84-96`): enabled only when not angry, `shop.owned` is falsy, `gSim.rank ~= 0` **and** at least one product in `LItem:AllProducts()` has `inventory > 0`.
* `EnterNumber` (sell, `:130-159`): `count = tonumber(DisplayDialog{"ui/buysell.lua", item=item}) or 0`; clamped to `item.inventory`; if `count > 0`: `PlaySound("sell")`, `item.inventory -= count`, `lastprice`, `lastport`, `usetime = weeks`, **`item.sold += count`**, `total = count x item.price`, `QueueMessage(GetString("sell_notice", tostring(count), GetString(item.name), bsgDollars(price), portName))` ("Sold %1% cases of %2% for %3% each at %4%."), `transactionMade = true`, the once-per-week mood bump (`kPurchaseDelta`, `moodWeek`), `gSim:AdjustMoney(total)`, `InventoryChanged()`, `FlushMessages()`; then `EnableHaggle()`.
* `sell_all` (`:161-195`): for every shop item with `inventory > 0`: the same per-item steps (one `sell_notice` message each, `sold += count`), then, if the total is positive: `PlaySound("sell")`, `transactionMade`, mood bump, `AdjustMoney(total)`, `InventoryChanged()`, `FlushMessages()`, **`CloseDialog()`**. (The `sellall_confirm` string "Are you sure you want to sell me all the chocolates you own?" exists but the Lua never uses it; checked by grep over all Lua.)
* `CloseDialog` as in the market: `gShopItems = nil`, `CloseWindow()`, `gSim:TickDay()` if a transaction happened.
* After a haggle result the price `Text`s are recoloured (section 3.4) with `BetterPriceColor` for `"up"` and `WorsePriceColor` for `"down"` (the opposite of the market, because the shop pays the player).

## 3. Haggling

### 3.1 The dialog (`ui/haggle.lua`)

`DisplayDialog{"ui/haggle.lua", market=<LMarket>}` or `shop=<LShop>` plus `char=`. Returns `"up"`, `"down"` or `"none"` (the string given to `CloseWindow(action)`), or nothing on cancel.
* Frame `x = ui.xOffset + 110 + ui.charW`, `y = ui.yOffset + 50`, `404 x 370`, `color = MarketColor` or `ShopColor`; `type = "market"` or `"shop"`; `char = gDialogTable.char or building.characters[1]`.
* `intro` text = `LabelString(type .. "_intro_" .. bsgRandom(1, 10))` (10 intro strings per kind).
* **Four distinct opening lines**: a loop draws `n = bsgRandom(1, 10)` until four different ones (a set `used[n]`) have been collected, in order: `lines[1..4] = type .. "_" .. n` (market strings `market_1..10`, shop `shop_1..10`: the player's possible haggle lines, e.g. "Might you be able to make me a deal?"). The four options are buttons `option_1..4` (frames `control/haggle_field` at y = 66, 132, 198, 262), each a `Button` with a white 25% overlay and the line.
* Initial state: `EnableWindow(.., false)` for `ok`, `response`, `player_choice` (hidden until a choice is made).

`DoHaggle(choice)` (closure over `char`, `action`, `self`, `type`, `lines`; `haggle.lua:46-72`):
1. **Mood cost, before the roll**: `if not char:IsAngry() then char:SetMood(char.mood - LCharacter.kHaggleDelta) (4); if char:IsAngry() then char:SetMood(LCharacter.kAngry + 1) (26) end end`. (An unhappy keeper cannot be pushed into "angry" by haggling alone; an already angry keeper is not touched.)
2. `action = self:ComputeHaggle(char)` (section 3.2/3.3).
3. Disable `option_1..4` and `cancel`; enable `player_choice`, `response`, `ok`; disable `intro`. (`ok` and `cancel` are built at the same position, so `EnableWindow(false)` must hide, as in Chocolatier 3.)
4. `key = type .. "_" .. action .. "_" .. bsgRandom(1, 10)`, e.g. `market_down_7`, `shop_none_3`; `SetLabel("response", GetString(key, {vendor = GetString(char.name), port = GetString(gSim.port.name), market = GetString(self.name)}))` (the strings use `[vendor]`, `[port]`, `[market]`, `[player]`; the table is passed as the substitution table) and `SetLabel("player_text", GetString(lines[choice]))`.
5. `ok` button: `CloseWindow(action)`; `cancel` has `close = true`.

Every option leads to the same roll, the choice only picks the displayed line (fact: all four buttons call `DoHaggle(i)` and `choice` is used only for the text).

### 3.2 `LMarket:ComputeHaggle(char)` (`sim/market.lua:80-164`; operates on `gShopItems`)

1. `season = bsgMod(gSim.weeks, 52)`.
2. For each item in `pairs(gShopItems)`: `r = (price - low) / (high - low)` if `item:InSeason(season)`, else `r = (price - 1.5 high) / (2 high - 1.5 high)` (out-of-season prices live in `1.5 high .. 2 high`, see 4.2). `R = sum r / count`, then `R = 100 x R` (a mean percentage, 0..100 normally).
3. `if char.haggleFactor then R = R x char.haggleFactor end`.
4. `if char:IsAngry() then R = R x 0.9 elseif char:IsHappy() then R = R x 1.1 end` (angry <= 25, happy >= 70).
5. `roll = bsgRandom(1, 100)`. **Quest bonus**: if `LQuest:Variable("HaggleBonus") > 0` then `roll = R - 21` (forces the "down" branch) and `LQuest._Variables.HaggleBonus` is decremented by 1.
6. `result = "none"`. If `roll < R - 20` -> **"down"**: for each item `price = floor((price + low) / 2)` if in season, else `floor((price + 1.5 high) / 2)`; `PlaySound("haggle_price_down")`.
   Else if `R + 30 < roll` -> **"up"**: for each item `price = high` (in season) or `2 high` (out of season); `char:SetMood(LCharacter.kAngry - 10)` (15, angry); `PlaySound("haggle_price_up")`.
7. Returns the result. Prices are **written to the shared `LItem`s** (they persist until the next `PreparePrices`).

Probabilities (derived): down = clamp((R - 21)/100) roughly, up = clamp((70 - R)/100), none the rest; high prices make a discount more likely.

### 3.3 `LShop:ComputeHaggle(char)` (`sim/shop.lua:89-152`; `LItem:OwnedProducts()`)

Same shape, mirrored (prices the shop pays): `r = (high - price) / (high - low)` per owned product, `R = 100 x mean`; same `haggleFactor`, angry x0.9 / happy x1.1, roll, bonus (`HaggleBonus`, `roll = R - 21`). `roll < R - 20` -> **"up"** (the player gets more): `price = floor((price + high) / 2 + 0.5)`, plays `haggle_price_down` (the "good result" jingle; note the name is from the market's point of view); `R + 30 < roll` -> **"down"**: `price = low`, `char:SetMood(kAngry - 10)`, `PlaySound("haggle_price_up")`; else `"none"`. There is no season term.

### 3.4 After the dialog (both UIs)

`EnableHaggle()` again (an angry keeper disables the button). Then colour = `BetterPriceColor` (`<font color='4aa008'>`) or `WorsePriceColor` (`<font color='c10c0c'>`) per outcome (market: `"up"` worse, `"down"` better; shop: `"up"` better, `"down"` worse), `TrackPrices()`, and for each item `SetLabel(name .. "_price", color .. bsgDollars(item.price))` (the shop adds the small-font prefix above 9999). A missing window name must be a no-op for `SetLabel` (the engine's is).

### 3.5 `SetCharacterMood` and the face

The only call is `LCharacter:SetMood(m)`: clamps `m` to 0..100, stores `mood`, calls `SetCharacterMood(self.name, m)` (`docs/re/choc2-port-and-ledger.md` 3.2). SetMood is reached from: the purchase/sale bump (+10, once per game week per keeper), the haggle cost (-4, capped so it does not make the keeper angry), a bad haggle (set to 15), and quest actions elsewhere. The keeper's `Character` window sits inside the market and shop dialogs (`char:Visual(...)`), so the native must update the face on every `SetCharacterMood` for that name **while the market dialog stays open under the haggle dialog**. `IsHappy` is `mood >= 70`, `IsAngry` is `mood <= 25`, default 50.

## 4. Game-side numbers the dialogs rely on

### 4.1 Natives and engine behaviours used (status vs. the engine)

| Name | Use here | Notes |
|---|---|---|
| `DisplayDialog{file, ...}` | opens a modal; returns the argument of `CloseWindow(v)`; extra keys land in `gDialogTable` | exists; the return value is a **string** here (`GetLabel`) and callers `tonumber` it. Chocolatier 3's register returns a number (`trade-and-save.md`); the engine must not coerce |
| `CloseWindow(v?)` | closes top modal | see above |
| `GetLabel(name)`, `SetLabel(name, text)`, `SetFocus(name)`, `EnableWindow(name, bool)` | the whole receipt | `EnableWindow` and `SetLabel` on a missing name must be silent no-ops |
| `TextEdit{name, x, y, w, h, length, label, ignore}` | the count field | `length = 7`, no `ignore`, no `clearinitial` here |
| `DynamicWindow{ontext, script}` | 2.6 | done: runs `script` (or rebuilds `contents`) on every `kTextEditChanged`, not at creation |
| `Rollover{x,y,w,h,fit,contents,command}` | sacks, platters, `BigRollover` | exists (ledger/port) |
| `PlaySound("buy"/"sell"/"haggle_price_up"/"haggle_price_down")` | | done: `sfx/<name>.ogg` exists for all four |
| `SetCharacterMood(name, mood)` | | exists |
| `bsgDollars`, `bsgFloor`, `bsgMod`, `bsgRandom`, `bsgFontHeight` | prices, layout | `bsgFontHeight(fontTable)` is already needed by the port view (`docs/design/choc2-port-and-ledger.md` 2.1) though it is not listed in `docs/re/api-choc2.tsv`; the argument is a font table `{fontName, size, color}` |
| `HelpDialog(n)` | `ui/uihelp.lua`, arguments 4 (market) and 5 (shop) | Lua |
| `DialogLedger()`, `BuildingBanner(building)`, `NormalLedger()` | Lua in `ui/uihelp.lua` | |
| `SetButtonToggleState(name, bool)` | the logo toggles | exists |
| `fpWrite` | telemetry | leave undefined |
| `StartFireworks()` | the Baumeister sign when `bcsign > 5` | optional global, tested for existence |

### 4.2 `LItem:PreparePrices()` (`sim/item.lua:222-281`), the price generator

Run by `Simulator:Tick` (every fourth `TickDay`, section 4.3) and by `Simulator:SetPort` when the port changes (`sim/travel.lua:172`). For every item (`_ByIndex`):
1. `price = bsgRandom(low, high)`.
2. If the item has a `recipe` (a chocolate):
   * `rank < 4`: `sold <= 2000` -> `price = price + (middle - price) x sold / 2000`; `2000 < sold < 7000` -> `price = middle + (lowest - middle) x (sold - 2000) / 5000`; `sold >= 7000` -> `price = lowcost` for `truffle` and `exotic` types, else `floor(lowcost x 0.9)` (the exact branch order is the bytecode's; `rank >= 4` skips this block and keeps the random price);
   * if `gSim.port.shopOwned` then `price = price x 1.2`;
   * `price = floor(price + 0.5)`; clamp to `high` from above and 1 from below.
3. Else (an ingredient) if not `item:InSeason(season)`: `price = bsgRandom(1.5 high, 2 high)`.
4. `item.price = price`.
5. Afterwards, if `gSim.rank == 0` (tutorial): `sugar.price = 30`, `cacao.price = 75`.

`LItem:InSeason(w)`: with `startWeek < endWeek`: `startWeek <= w <= endWeek`; with `endWeek < startWeek` (wrapping): `startWeek <= w or w <= endWeek`; equal: false. Defaults `startWeek = 0`, `endWeek = 52`; `LItem:IsSeasonal()` is `startWeek ~= 0 or endWeek ~= 52` (class defaults, as written in `item.lua:212`).
**Prices are global per item, not per port** (there is no port term except `shopOwned`). `lowcost`, `middle`, `lowest`, `low`, `high` come from the item definitions and `CalculateRecipeCosts`.

### 4.3 Time, money, messages (`sim/sim.lua`)

* `Simulator:TickDay()` (`:774`): `days += 1`; `if days >= 4 then self:Tick()`; `UpdateBadge(self)`. A market or shop visit with a transaction costs one day; four days make a week. `Tick()` (`:753`): production run, `LCharacter:MoodTick()`, expiration, `LItem:PreparePrices()`, `LTip:ApplyAll()`, `days = 0`, `weeks += 1`, `GetScore`, `UpdateBadge`, a dated message. Chocolatier 3 counts one `SubTickSim()` per visit (`trade-and-save.md` 2.2).
* `AdjustMoney(n)`: `money` is unsigned 32 bit: if `4294967295 - money < n` it saturates, else `money += n`; negative results clamp to 0; then `GetScore()` and `UpdateBadge(self)`. (A second argument would call the misspelt `QueueMesssage` and fail; trade calls pass only one.)
* `InventoryChanged()`: `LFactory:ProjectProduction()`, `UpdateLedgerContents("inventory")` and `("factories")`.
* `QueueMessage(m)`: appends, sets `messageQueue`; when more than 31 messages exist it removes entry 2. `FlushMessages()`: if queued, `ConcatenateMessages()` and `UpdateLedgerContents("messages")`.

## 5. Differences from Chocolatier 3 (`docs/re/trade-and-save.md`)

| Topic | Chocolatier 3 | Chocolatier 2 |
|---|---|---|
| Register result | a number via `ModalReturn` | a string from `GetLabel`; `tonumber(...) or 0` by the caller |
| Count field | `TextEdit count`, `ignore = kNumbersOnly`, `clearinitial`, `onkey = Update` | `TextEdit <item name>`, `length=7`, no ignore/clearinitial; a hidden `DynamicWindow{ontext=true}` triggers the update |
| Pad | native keypad writes `SetLabel("count")` | Lua `NumberPad`; max 6 digits via pad; backspace key |
| Max | `floor(money / price)` buying; inventory selling | selling: inventory; buying: `floor((money - 974) / price)` but **disabled** when buying |
| Buy clamp | none | none (empty branch), red subtotal when over budget; refusal dialog on OK |
| Subtotal | `" x " .. Dollars(price) .. " = " .. Dollars(total)` | same shape with `bsgDollars` (truncating `UtilDollars`, `api-choc2.tsv`) |
| Market contents | list filtered by `locked`/`available`, sorted | `pairs(inventory)` order, no filter, `powder` added at rank >= 3 |
| Haggle | difficulty 1/2/3: down by 50/35/25 %, failure `high x 1/1.25/1.5`; result via `gHaggleSuccess` | roll vs the mean position of prices in their range, +-10 % for mood, `HaggleBonus` quest; outcomes up/down/none; keeper mood hit; `PlaySound` per outcome |
| Haggle availability | rank > 1 and not angered | not angry, not owned, rank ~= 0; shop also needs stock |
| After trade | `SubTickSim()` once | `TickDay()`: 4 days per week |
| Mood | `SetMood(name, happiness)` face by `h*3/100` | `SetMood` in Lua with thresholds 25/70, `SetCharacterMood(name, 0..100)` |
| Messages | `msg_buy_ingredient` etc. | `buy_notice`, `sell_notice` queued through `gSim:QueueMessage` |
| Shop "Sell all" | calls sell for every product, then `CloseShopWindow()` | same idea, closes the dialog; no confirm dialog |
| Tutorial hooks | | market closes itself after a purchase in quests `tutor09`/`tutor11` |

## 6. Unknown

1. All Chocolatier 2 native behaviour for the logo: string format of `GetLogoString` beyond "a chunk that returns a table", the font key name, what `lf` and `to` mean (guesses in 1.3), `lr` and size mapping, HSV scaling, how `RandomizeLogo` picks (ranges, RNG stream: assumed the global one), whether it updates sliders.
2. The `.LOGO` renderer: composite order (background, graphic, text is assumed from Chocolatier 1), text box size and clamping for `tx/ty`, outline colour, whether a drop shadow exists, whether alpha and rotation apply to the graphic only.
3. The slider-to-list mapping and order for background (8), graphic (12 + items) and font (9) and whether sliders are synchronised to the table on open.
4. Whether `SetTextColor`'s 4th argument writes `ta`; whether `ToggleOutline()` called from Lua also flips the visible checkbox.
5. `DynamicWindow` `script`/`ontext` details (2.6): creation-time run, message filtering.
6. Hide versus grey for `EnableWindow(false)` on a `Button` (`number_max`, `haggle`): Chocolatier 3 hides (flag 0x100, `api-windows.md` line 28); assumed the same.
7. `LItem` data not decoded here: `low`, `high`, `middle`, `lowest`, `lowcost` per item (`item/itemdefs.lua`), the exact `sold` branch order in `PreparePrices` is read from compressed jump targets and should be unit-tested against the Lua itself rather than this prose.
8. `ui.platterWidth/platterHeight` and the shop's four layout bodies (`shopui.lua:200-473`) are position arithmetic only; they run as Lua and were not transcribed.
9. Audio: names `buy`, `sell`, `haggle_price_up`, `haggle_price_down` (`PlaySound`), mapping to files is the audio work's.
