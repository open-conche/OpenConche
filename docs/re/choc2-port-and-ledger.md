# Chocolatier 2: the Port, Ledger, Character, Sprite, AnimationWindow and Map/travel interfaces

Scope: facts for the Chocolatier 2 port, ledger and travel (`docs/design/choc2-port-and-ledger.md`; the window classes `Port`, `Ledger`, `Character`, `Sprite`, `AnimationWindow`, `Map`). `chocotwo.exe` is packed, so nothing here is read from the Chocolatier 2 native code. Every Chocolatier 2 fact is what the Lua *passes to* a native or *reads back*, taken from the bytecode in `re/choc2/pfp_extracted/` (disassembled with a workbench script; call sites are cited as `file:line`, the line numbers being the ones the bytecode carries). Chocolatier 1 facts come from `re/choc1/` (addresses are VAs in the Windows exe). Chocolatier 3 facts are quoted from `docs/re/port-window.md`, `map-window.md` and `ledger-and-game-natives.md`.

Conventions: "Fact" = read from bytecode, a data file, or the decompile. "Inference" = derived, not seen. Anything about what a Chocolatier 2 native *does inside* is an inference unless a Chocolatier 1 or 3 twin is cited. Lua sources named below are relative to `pfp_extracted/`.

---

## 0. Headline findings

1. **Chocolatier 2 `Port` is driven from Lua through `bsgAddSprite(table)`, not through a style key holding the `Port` table as Chocolatier 3 does.** `ui/portview.lua` runs each entry of `gSim.port.layout` (an array of "layout items") and each item calls the native `bsgAddSprite` with a plain table (`sim/port.lua:36,78,97,108`, `sim/building.lua:183`). The layout items are buildings and `SpriteLayer`s, both defined in Lua. The `Port{...}` constructor itself carries only geometry (`x y w h xOffset yOffset`) plus children (`ui/portview.lua:100`).
2. Buildings are *the same sprite table kind* as scenery: a building contributes `{x, y, image="ports/<building>", labelx, labely, contents, command}`. `contents` is a Lua expression string that builds the hover popup and `command` is a Lua closure that runs on click. Chocolatier 3 instead evals the string `<building>:OnClick()` on click and has no hover callback (`port-window.md` 1.7). So hover and click are Lua-supplied per sprite here.
3. **The travel animation is Lua-driven week by week** (`TravelOneWeek(type)`, `Pause(1900)`, `StillTravelling()` loop in `sim/travel.lua:98-100`), not a Lua-scheduled event list on a `TravelTask` as in Chocolatier 3 (`map-window.md` 6.3, 7). Chocolatier 1's `AnimateTravel` took one table with the legs (`re/choc1`, `FUN_00417da0`); Chocolatier 2 splits it into `InitiateTravel(leg)` per leg and `TravelOneWeek(type)` per week.
4. **Characters are PNG sets, not sprite sheets.** `characters/<name>.png` (a 172x240 body for keepers) plus optional `<name>_pleased/_neutral/_displeased.png` face overlays (45 of 244 characters have the trio). Chocolatier 3 uses `characters/<name>.xml` with 1 body frame and 3 face frames (`port-window.md` 2.2). Mood thresholds are in Lua (`sim/character.lua:17-21`).
5. The ledger is a **Lua-built `Ledger{}` window** with three `DynamicWindow` tab bodies and a fourth `badge` dynamic window, rebuilt on demand (`ui/ledger.lua`). Chocolatier 3's `LedgerWindow` is a native singleton (`GrabLedger`/`ReleaseLedger`) with `RealUpdateLedger(mode,data)`; neither global appears anywhere in Chocolatier 2's Lua. The Chocolatier 1 ancestor of the dynamic tab bodies exists (`TDynamicWindow`); the C1 ancestor of `Ledger` as a class does not.
6. The Chocolatier 1 binary has **no** port, ledger or character window class. C1 ports are built entirely in Lua from `Bitmap`/`Rollover`/`SimpleAnimation` windows (`ui/portview.lua`, `LayoutPort`). C1 only has `TMapScreen`, `TDynamicWindow`, `TRolloverPopup`, `TSimpleAnimation`, `TImagePulser`, and the money counter. See section 7.

---

## 1. `Port{...}`

### 1.1 Constructor (fact: `ui/portview.lua:100-106`)

```
Port{ x = kCenter, y = kCenter, w = kMax, h = kMax,
      xOffset = ui.xOffset, yOffset = ui.yOffset,
      Group(portLayout),                 -- the closures built from gSim.port.layout (below)
      Window{ x=kCenter, y=kCenter, w=kMax, h=kMax, name="buildinglabels", Group(labels) } }
```

* Keys the native consumes (by name in the Lua): `x y w h xOffset yOffset`. `xOffset`/`yOffset` are `ui.xOffset`/`ui.yOffset` (`ui/uiconstants.lua`: `gRealXOffset`, `gRealYOffset`, the letterbox offsets). There is no `background`, `port` or `musicDelay` key (compare Chocolatier 3, `port-window.md` 1.2).
* The background is **not** a `Port` key. It is a sibling `Bitmap{name="port", x=kCenter, y=kCenter, image="ports/<port.name>"}` (the 800x600 `ports/<port>.jpg`) that lies under the `Port` in the same `bsgScreen` group (`ui/portview.lua:91-95`). `ports/sf_darksky` is added as a child `Group` of that Bitmap only for `sanfrancisco` while `LQuest:Variable("bcsign") > 5` (`:77-85`).
* The full dialog is `MakeDialog{ bsgScreen{ Bitmap"port", Port{...}, Bitmap"ports/banner"+Text, NormalLedger() } }` (`:86-114`). The banner is Lua: `Bitmap{ image="ports/banner", x=297+ui.xOffset, y=ui.yOffset }` containing `Text{ x=19, y=14, w=169, h=20, flags=kVAlignTop+kHAlignCenter, label=LabelString(port.name), font={standardFontName,kSixteen,DefaultColor} }`. `ports/banner.png` is 206x34. (Chocolatier 3's banner is at x=275 and a different font; `port-window.md` 1.2.)
* After building the dialog it calls, in order: `gViewMode="port"`, `SetLedgerMode("port")`, `SetMusicState("normal")`, `gSim.port:SetAmbient()`, `bsgTriggerAd()`, `StopProductionAnim()` (`ui/portview.lua:116-121`).

### 1.2 Layout items and how `bsgAddSprite` is reached

`ui/portview.lua:19-70` iterates `ipairs(gSim.port.layout)`. For each `item`:
1. If `type(item) == "function"`, call it and use the result (used for `PlayerLogo`, `BaumeisterSign`, `Fireworks`, which are closures; `:21-25`).
2. Else skip the item when `item.startweek` is set and `startweek > gSim.weeks`, or when `item.endweek` is set and `gSim.weeks > endweek` (`:23-25`). (Fact: 14 `startweek` and 6 `endweek` keys in `ports/*.lua`.)
3. If `item.PrepareForLayout` exists: `labely = item.labely or item.y`; `labelx = item.labelx or item.x`; `labelx = min(labelx, ui.screenWidth - 125)` (clamped); then `result = item:PrepareForLayout(labelx - 50, labely - bsgFontHeight(popupFont)/2)` when both are known, else `item:PrepareForLayout()` (`:26-37`). It also builds a hover-label `Frame` for the building name (`Frame{ x=labelx-50, y=labely-fh/2, h=fh, fit, empty, corner/left/top="control/rollover_*", ornament="", Rectangle{ h=kMax, color=PopupColor, inset=2, alpha=0.8, TightText{label=item.name} } }`) and `table.insert(labels, ...)`; those `Frame`s end up in the `buildinglabels` window (`:40-57`).
4. If no layout closure came back and the item has `OnActivate`, a dev fallback `TextButton` at x=5, y=20 (+45 each) is made whose command calls `item:OnActivate()` (`:61-67`). Inference: only buildings without coordinates hit this.
5. The closure goes into `portLayout`, which `Group(portLayout)` runs as children of the `Port` window. **Inference:** running each closure inside the `Port` constructor is what makes `bsgAddSprite` add to that `Port` (there is no window argument).

Layout items are produced by two Lua constructors (fact, `sim/port.lua`, `sim/building.lua`):

* `SpriteLayer(t)` (`sim/port.lua:26-45`): if `t.image` is a string it is prefixed `ports/`. Returns `{ PrepareForLayout = f, startweek, endweek }`. `f()`: if `t.imagelist` is a table, `t.image = "ports/" .. imagelist[bsgRandom(1, #imagelist)]` (re-rolled each time the port is built); if `t.random` is set, a `bsgRandom(1,100)` roll decides: `roll <= t.random` returns a closure that calls `bsgAddSprite(t)`, otherwise a no-op closure (`:35-38`); with no `random` it always adds. So `random = 50` means "present about 50 percent of the visits". The table passed to `bsgAddSprite` is the very `t` the port file wrote.
* `LBuilding:PrepareForLayout(labelx, labely)` (`sim/building.lua:181-191`): requires `self.x` and `self.y`; returns a closure that calls `bsgAddSprite{ x=self.x, y=self.y, image="ports/"..self.name, labelx=labelx, labely=labely, contents=self:NameRolloverTarget(), command=function() StopPulsing(); self:OnActivate() end }` (`:183-186`). `NameRolloverTarget()` returns the string `(LBuilding:ByName('<name>')):NameRollover()`; `NameRollover()` calls `StopPulsing()` then `MakeRollover{...}` with a `Rectangle(PopupColor, alpha 1, inset 2)` and `TightText{label=name}` (`sim/building.lua:115-127`).
* `PlayerLogo` and `BaumeisterSign` (`sim/port.lua:64-112`) build a building-like table with `image=".LOGO"`, `logo=gSim.logo`, `contents="PlayerLogoRollover()"`, `command=DoPlayerLogo` (`DoModal("ui/logoedit_basic.lua")` then `SwapToModal("ui/portview.lua")` and `StopPulsing()`), `labelx=x-120`, `labely=y-10`, and call `bsgAddSprite` with it. The Baumeister sign swaps to `image="ports/baumeister<LQuest:Variable('bcsign')>"` (0..5) and, for `bcsign >= 6`, to the player's logo with `scale = 175/350`, `y = 55+100/2-10` (`:84-112`). `".LOGO"` is a magic image name meaning "render `logo` here". **Unknown:** how the native renders it (the Chocolatier 3 `SignEditor`/logo renderer is the likely twin).

### 1.3 The `bsgAddSprite` table

One table type serves static scenery, animated scenery, path sprites, buildings and the logo. Keys seen (fact: key counts over all 20 port files, `sim/port.lua`, `sim/building.lua`):

| key | seen in | meaning (fact = shape; meaning is inference) |
|---|---|---|
| `image` | all | texture. Prefixed `ports/` by `SpriteLayer`. A name ending `.xml` is a sprite sheet (e.g. `camelwalk.xml`, `car_01.xml`); `.LOGO` is the player logo. `ports/<port>/` subdirectories are **not** used in Chocolatier 2; the images are flat in `ports/` (`ports/ca_pyramidmask.png`). |
| `x`, `y` | 200 each | position. Fact: ports write them as `left + w/2`, `top + h/2` of the image rectangle (`ports/cairo.lua:38-52`: `557 + 243/2`, `73 + 76/2`), so **x,y are the centre** of the picture (as in Chocolatier 3 buildings). For animated sheets they are the registration point (inference, as in C3). |
| `path` | 139 | array of `{x,y}` pairs (flattened in the bytecode as consecutive constants). Counts seen: 4 points (cloud strips) and 7 (planes, boats). Form a chain of cubic Beziers, `3n+1` points (inference; matches Chocolatier 3 `TCurve`, `port-window.md` 1.9). Presence selects a moving sprite. |
| `time` | 109 | path duration in **milliseconds** (cairo: 200000, 240000 for clouds; 64000 camels; 16000 cars). |
| `loop` | 37 | true on clouds. In C3 the equivalent is `motion = "loop"`. Here `loop`/`bounce` are booleans. |
| `bounce` | 53 | true -> ping-pong along the path (C3 `motion="bounce"`). |
| `hold` | 73 | ms to wait at an end (cairo planes 25000, boats 15000, camels 12000/8000, cars 4000/5000). **No C3 twin**: C3 has no `hold`. Inference: the sprite sits invisible or parked for `hold` ms between runs. Unknown semantics. |
| `scale` | 86 | uniform scale (0.25 planes, 0.4 boats, 0.38/0.42 camels, 0.6 cars). |
| `imagelist` | 46 | resolved in Lua to a single `image` (see above), so the native never sees the list. Lists are globals from `ui/styles.lua:145`: `portPlanes` (aircraft_03/04/05), `portAirships` (aircraft_01/02), `portBoats` (boat_01/02/04), `portCars` (car_01.xml, car_02, car_03.xml, car_04.xml, car_05.xml, car_08), `portTrains` (train_01/02/03). |
| `htile` | 27 | tile horizontally with this period (C3 twin: `htile`, four extra copies at +-htile, +-2*htile; `port-window.md` 1.5). Cairo: `htile=true` on a cloud (a bool here, a number in C3; **inference:** the native treats true as a default period, probably the window width; unverified). |
| `far`, `near`, `farscale`, `nearscale` | 24 each | perspective rows and scales for path sprites (abidjan: `far=200`, `near=600`, `farscale=0.4`, `nearscale=1`). C3 twin: `yFar`, `yNear`, `scaleFar`, `scaleNear`. Key names differ. |
| `random` | 9 | consumed by Lua (the percent roll above); also left on the table, so the native might read it too (C3 uses `random` as "randomise start phase", default true). **Unknown** whether the C2 native reads it. |
| `labelx`, `labely`, `contents`, `command` | buildings | hover popup anchor, hover popup expression, click closure (see 1.2). |
| `logo`, `target`, `scale`, `y` | logo signs | `.LOGO` handling. |

Behaviour that depends on a window not in the Lua: whether layers are by array order, whether `x,y` of path sprites are ignored, the draw order between buildings and scenery, the cross-fade intro. **Unknown.** The only ordering the Lua gives is the order of `layout` (scenery and buildings are interleaved in file order; see `ports/cairo.lua:29-67`, where `ca_foreground` is the last item). Inference: array order is the draw order (later on top), because Chocolatier 2 files have no `layer` key at all (count 0 in all port files) whereas Chocolatier 3 data carries `layer`.

### 1.4 Building constructors in `ports/<port>.lua` (fact)

Each port file does, in order (`ports/cairo.lua`): `DefineCharacter{ name=... }` for every keeper (7 in cairo), then one `DefinePort{ name, available=false (cairo), ambient="sfx/metropolitan_loop.ogg", layout={...} }`.

`layout` entries (counts over all files): `SpriteLayer` 243, `EmptyBuilding` 31, `Market` 27, `Shop` 13, `GenericBuilding` 11, `Factory` 6, `Laboratory` 1, `PlayerLogo` 6, `Speakeasy` 2, `Fireworks` 1, `BaumeisterSign` 1. Building keys: `name` (also the image name under `ports/`), `character` (a name, or a table of names for several), `ambient` (a loop name such as `sfx/farm_loop.ogg`, `sfx/other_loop.ogg`; run on `OnActivate` through `SetAmbient`), `x`, `y` (centre, `left + w/2`), `inventory` (Market: `{ mango=true, sugar=true, ...}`), `labelx`, `labely` (13 uses), `startweek`/`endweek`, `dialog`, `target` (6, on `PlayerLogo`), `price`, `action` (on characters), `appearance`, `evil`, `dispatchRank`, `wait`.

`LBuilding.new` (`sim/building.lua:54-75`): requires `name` ("Building defined with no name" via `bsgDevWarning`), warns on duplicates, stores `_ByName[name]` and `_ByIndex`. A string `character` becomes `{name}`. `EmptyBuilding` (`:19-34`) inherits from the `_empty` building: `owner = character[1]`; the `_empty` building's characters are appended to the new one's character list (the generic passers-by). `PostProcessCharacters` turns names into `LCharacter` tables (`:80-89`), and `PostProcessAll` does it for `character`, `rank0`..`rank5` and `owner` (`:91-106`).

`Port.new` (`sim/port.lua:160-193`): `bsgDevWarning` if no name or duplicate; `hidden` ports get `available=false` and are recorded in `_Hidden`; otherwise `available` defaults to `true`; `Routes[name] = {}`; stored in `_ByName` and `_ByIndex`. `LPort.ambient` default `"sfx/port_loop.ogg"`; `SetAmbient(self)` calls the native `SetAmbient(self.ambient)` and then `SetMusicState("silent")` if `hidden` else `SetMusicState("normal")` (`:198-205`). `LPort:PostProcessAll` (`:210-228`) assigns `mapx`, `mapy`, `labelpos` from `LPort.Locations[name]` (from `ports/portlocations.lua`: 24 entries `{x, y, kHAlignLeft|kHAlignRight}`, e.g. abidjan `{363,193,kHAlignRight}`), builds all routes (`BuildCompleteRoute`), and calls `SetPort(port)` on every building in the layout.

### 1.5 Messages `Port` posts to Lua

Fact from the Lua side only: **the Lua gets nothing posted by name.** All interaction is closures placed on the sprite tables:
* Click on a building sprite -> the `command` closure of that sprite runs (`StopPulsing()`, then `building:OnActivate()`; `sim/building.lua:186`, `:200-219`). `OnActivate` sets `gActiveBuilding`, `SetAmbient`, `SelectCharacter`, shows `ui/chartalk.lua` or runs `character:DoAction()`, then `ExitBuilding` (`sim/building.lua:200-241`).
* Hover on a building sprite -> the string in `contents` is evaluated and the resulting `MakeRollover{...}` dialog is shown as a popup anchored at `labelx,labely` (inference, from `MakeRollover` usage elsewhere; the evaluation uses a Lua string, exactly as `Rollover{contents=...}` in `ui/ledger.lua:61` and `ui/map.lua:70`). Also `StopPulsing()` is called at the start of every popup builder (`sim/building.lua:120`, `sim/port.lua:51`).
* `StopPulsing()` (no arguments, no return): called on building click and popup creation (`sim/building.lua:120,186`, `sim/port.lua:51,63`, `sim/quest_functions.lua:520`, `ui/mainmenu.lua:106`). Chocolatier 1 has the matching mechanism: `UtilPulseImage(name, count)` starts a `TImagePulser` that pulses a `BitmapTint` for `count` ticks of 100 ms (10-step grey cycle `1.0 .9 .8 .7 .6 .6 .7 .8 .9 1.0`, `choc1-natives.md` 5); C1's only call is `UtilPulseImage(name, 30)` in `sim/port.lua`. **Inference:** Chocolatier 2's pulsing building highlight is a native started by something that is not visible in the Lua (hover or `Quest` hints) and `StopPulsing` cancels it. The ordering "pulse is cancelled when the popup or dialog opens" is the only fact. Chocolatier 3's pulse is the hover highlight (alpha/scale triangle wave with period 3000 ms, `port-window.md` 1.8); whether Chocolatier 2 pulses the same way is **unknown**.
* No building-enter animation or transition is called in the Chocolatier 2 Lua at all (no `Transition`, no `TransitionIn`): `grep` over the disassembly finds neither global. (Fact.)

### 1.6 What `Port` draws (inference from the Chocolatier 3 twin and the data)

Background jpg (sibling Bitmap), then the sprites the layout added, then the `buildinglabels` child window, banner and ledger. There is no engine-side intro fade evidence; Chocolatier 2 has hover `Frame` labels built in Lua (`ui/portview.lua:40-57`) that C3 also shows for the first second after arrival (`port-window.md` 1.8). **Unknown** whether Chocolatier 2 repeats the "show all names for one second then fade" behaviour.

### 1.7 Hit testing

No fact. The Lua provides sprite rectangles only through the image sizes. By inference from Chocolatier 3 (`port-window.md` 1.7: rectangle test of the building texture, highest `layer` wins) the Chocolatier 2 native hit-tests the sprite's image rectangle at x,y as centre; since Chocolatier 2 has no `layer` key, overlapping buildings would resolve by array order (last added wins; unverified).

### 1.8 Other `Port`-adjacent natives

* `StartProductionAnim()` / `StopProductionAnim()` (no args): `StopProductionAnim()` is called when the port view is built (`ui/portview.lua:121`) and on a new game (`ui/mainmenu.lua:232`). During travel `sim/travel.lua` brackets the encounter and disaster dialogs: `StartProductionAnim()` at departure (`:72`), `StopProductionAnim()` before `Disaster` and before the encounter building `OnActivate`, `StartProductionAnim()` after each (`:82-89`, `:84`), `StartProductionAnim()` after each stall check (`:109`), `StopProductionAnim()` on arrival (`:113`). In Chocolatier 3 the twin is `LedgerWindow::AnimateProduction()` (the ledger's factory gauges, `ledger-and-game-natives.md` 2.3). **Inference:** the same (animate the ledger's/map's factory icons while the map moves; stop while a dialog is up). **Unknown** which window owns it.
* `ResetColorMap()` (no args): `ui/mainmenu.lua:233` (new game) and `sim/travel.lua:162` (in `Simulator:SetPort` when the port is being visited for the first time). Pairs with the map's `colormask` Bitmap (section 5): **inference** that a visited port is painted into a colour mask on the map and `ResetColorMap` clears it for a new game; the file `image/map_color.jpg` exists next to `image/map.jpg`.
* `SetAmbient(name)`: the string passed is a `sfx/<name>_loop.ogg` path (`ports/cairo.lua:24`), not a bare name; `""` stops it (`ui/mainmenu.lua:230`).

---

## 2. `Ledger{...}`

### 2.1 Constructor (fact: `ui/ledger.lua:49-105`)

```
Ledger{ name="ledger", x=ui.xOffset, y=ui.yOffset + ui.screenHeight - 310, w=ui.screenWidth, h=310,
        data = gSim, showQuestY = 0, hideQuestY = 53,
        <children> }
```
(310 is `fullLedgerHeight`, `ui/ledger.lua:9`.) Keys the native may read: `name x y w h data showQuestY hideQuestY`. `data` is `gSim`: the native treats the Lua `gSim` as the source of money/weeks/rank/score. Chocolatier 3's twin reads `Player` globals every call instead (`ledger-and-game-natives.md` 2).

Children in order:
1. `Bitmap{ name="quest", x=235, y=0, image="image/ledger_quest" }` (587x129) with a `Rollover{ x=44, y=62, w=497, h=39, contents="gSim:RolloverCurrentQuest()" }` that holds `Text{ name="quest_text", x=0,y=0,w=497,h=39, label=<quest summary>, font=dialogFont }` (`:60-64`). The quest text starts as `"#" .. gSim.quest:SummaryText()` (`:12-14`).
2. `Bitmap{ x=258, y=310-210, image="image/ledger" }` (540x208) holding:
   * `Window{ name="contents", x=21, y=40, w=ui.ledgerWidth(498), h=ui.ledgerHeight(157) }` with three `DynamicWindow{ name, x=0,y=0,w=kMax,h=kMax, contents=<file>, init=false }`: `messages` -> `ui/ledger_messages.lua`, `factories` -> `ui/ledger_factories.lua`, `inventory` -> `ui/ledger_inventory.lua` (`:70-74`). `init=false` means the body is *not* built when the window is created (inference; key name).
   * A `BeginGroup()` radio group of three `Button{ type=kRadio }`: `btn_messages` (x=1,y=3), `btn_factories` (170,3), `btn_inventory` (396,3), each with `control/btn_<name>_up/_down/_over/_down` graphics and a `command` that calls `SetLedgerContents("messages"|"factories"|"inventory")` (`:78-91`); and four `Text` labels with string keys `lbl_messages`, `lbl_factory`, `lbl_status`, `lbl_inventory` (`:94-97`).
3. `Bitmap{ x=0, y=310-238, image="image/ledger_badge" }` (263x237) containing `Group(DynamicWindow{ name="badge", contents="ui/ledger_badge.lua" })` (`:101-104`).
4. Outside the Ledger (a separate group before it): five `Button`s at the left: `to_map` (158,26, `control/globe_*`; command `SwapToModal("ui/map.lua")`), `to_port` (158,26, `control/port_*`; command `SwapToModal("ui/portview.lua")`), `recipe` (75,12; `DoModal("ui/recipebook.lua")`), `pause` (16,26; `DoModal("ui/pausedialog.lua")`), each with a white `Text` caption (`:26-46`). The group's `SETLISTO` takes 7 members.

`NormalLedger()` is a global (declared in `ui/uihelp.lua`; the portview and map call it as the last child; not decoded further).

### 2.2 Natives called around it

All arguments are shown as the Lua passes them.

| Native | Args | Call sites | Notes |
|---|---|---|---|
| `SetLedgerContents(mode)` | `"messages"`, `"factories"`, `"inventory"` | tab buttons `ui/ledger.lua:81,86,91`; `ui/mainmenu.lua:105` (`"messages"` at game start); `sim/travel.lua:105` (`"factories"` before the "factory stalled" dialog); `sim/factory.lua:229` (`"factories"`); `sim/quest_functions.lua:317` (a mode variable, then `Pause(5)`) | Switch the visible tab. **Inference:** show the `DynamicWindow` of that name (hide the others) and set the radio button (the C1 twin `UtilSetLedgerTab(name)` sets every `Button` named `<name>` on, `choc1-natives.md` 3). |
| `UpdateLedgerContents(mode)` | the same three names | `sim/sim.lua:502` (`"messages"`), `:533` (`"inventory"`), `:534` (`"factories"`); `sim/travel.lua:335,386`; `sim/quest_functions.lua:708`; `ui/factory_status.lua:95`; `ui/ledger_inventory.lua:135,147` | Rebuild that tab's `DynamicWindow` (re-run its `contents` script). Inference from the name and the C1/C3 twins (`UpdateDynamicWindow`). **Unknown:** whether it rebuilds only if that tab is showing. |
| `SetLedgerMode(mode)` | `"port"` (`ui/portview.lua:117`), `"map"` (`ui/map.lua:140`) | | Inference: shows `to_map` or `to_port` (the button that leads to the other screen) and hides the other; compare Chocolatier 3 `mapview`/`portview` toggling `map_port`/`port_map` (`ledger-and-game-natives.md` 2 rows 6/6b). `ui/map.lua:73-74` also disables `to_port`/`to_port_label` while travelling (`EnableWindow`). |
| `ResetLedger(money)` | number (`gSim.money or 0`) | `sim/sim.lua:412`, in `Simulator:Load*` after `CountAllKnownRecipes` | Reset the displayed money without counting up. Inference: the C3 `newplayer` mode (`UpdateMoney` snap) or C1's `SetMoneyDisplay(n)` snap (`choc1-natives.md` 5). |
| `UpdateBadge(gSim)` | the `gSim` table | `sim/medals.lua:74,89`; `sim/quest_functions.lua:533`; `sim/sim.lua:528,765,777`; `sim/travel.lua:150` | Refresh the `badge` dynamic window and the money counter: **inference** (the badge file reads `gSim.rank`, `gSim.weeks`, `gSim.days`, `gSim.medals`). |
| `UpdateDynamicContent(name)` | a window name | `sim/medals.lua:88`, `ui/recipebook.lua:25,26`, `ui/recipebook_type.lua:21` | Chocolatier 2's name for C1's `UpdateDynamicWindow(name)`: rebuild every `DynamicWindow` called `name`. The ledger's tabs use `UpdateLedgerContents` instead; the badge is rebuilt through `UpdateBadge`. |
| `SetQuestHint(text)` | string; `""` clears | `sim/quest.lua:411` (`SummaryText()` or `""`), `:461` (`""`) | Writes the quest line and slides the quest bar. **Inference:** `showQuestY = 0` and `hideQuestY = 53` are the y of the `quest` Bitmap when a hint is present and when empty (the bar is hidden behind the `ledger` picture at y=100 by sliding 53 px down). Slide duration, easing and whether the text lives in `quest_text` or inside the native: **unknown.** |
| `bsgGetMaxScroll(textName)` | name of a `Text` window | `ui/ledger_messages.lua:13,31,41`; `sim/sim.lua:488` | Returns a number. Lua does `top = bsgGetMaxScroll("messageText") + 1`, i.e. it returns the **0-based last valid start line**, and Lua keeps `gSim.messageTop` 1-based (min 1). The `+ 1` appears at every site. `sim/sim.lua:493` compares `max + 1` with 0 (so a result of -1 means "nothing to scroll"; inference). |
| `bsgSetStartLine(textName, line)` | name, 1-based line | `ui/ledger_messages.lua:21,25,35,42` | Scrolls the named `Text` so that `line` is the first visible line. |
| `bsgSetGlobalLabel(name, text)` | name, string | only `dev/dev.lua:34` (`"dev_current"`, quest name) | Dev-only; like `SetLabel` on the first window with that name anywhere in the tree (inference). Not needed for gameplay. |

`ui/ledger_messages.lua` (fact): `messageLineScroll = 7`. Builds `Text{ name="messageText", x=0, y=10, w=ledgerWidth-26, h=ledgerHeight, label=gSim.messageText, font=dialogFont, flags=... }`, and two `Button`s `scroll_up` (x=ledgerWidth-24, y=48) and `scroll_down` (x=ledgerWidth-24, y=86) with `control/scrollup_*`/`scrolldown_*` graphics. `updateScrollButtons` enables `scroll_up` only if `gSim.messageTop > 1` and `scroll_down` only if `messageTop < bsgGetMaxScroll("messageText") + 1`. Up clamps to 1, down clamps to the max. A "scroll to bottom" function sets `messageTop = max + 1`. The message text is `table.concat(gSim.messages, "\n")` set with `SetLabel("messageText", text)` (`sim/sim.lua:482-497`); messages are capped (`table.remove(messages, 2)` once `getn > 31`, `:479`).

`ui/ledger_badge.lua` (fact): fonts `standardFontName` 30 (money, `BlackColor`) and 16 (status). `Text` names and positions (all x=41, w=162): `money` (y=110, h=font height), `rank` (y=138, h=18, label `"rank_" .. gSim.rank`), `date` (y=156, h=18, label `"#" .. bsgDate(gSim.weeks, gSim.days)`, a **two-argument** `bsgDate`), `score` (y=176, h=118, empty at build). Medal `Bitmap`s at each `Simulator._Medals` entry `{x,y}` using `image/medal_<name>` for owned medals; the freshly awarded medal (`gNewMedal`) also gets `Sprite{ x=mx, y=my, image="glitz/sparkle.xml" }` and a `MedalRollover`. The Text children are looked up by name by the native, which writes the money counter and the "score per week" line (inference from C3: `RealUpdateLedger` writes `money`, `rank`, `day`, `score`; here `date`).

### 2.3 What carried forward from Chocolatier 3, what did not

Same idea, different packaging: C3's `LedgerWindow` is a native `TBSGWindow` that finds children `money`, `day`, `rank`, `score`, `questText` and `DookieDropper`s by name. C2's `Ledger` is the same name-finding native but with `data=gSim` and no droppers (the factories tab is a Lua `DynamicWindow`). See the table in section 7.

---

## 3. `Character{...}` and moods

### 3.1 Constructor (fact: `sim/character.lua:78-92`, `LCharacter:Visual(x, y, font)`)

```
Window{ x=x, y=y, w=ui.charW(191), h=ui.charH(240), fit=true,
        Character{ x=8, y=0, w=ui.charW, h=ui.charH, char=self },
        Bitmap{ x=0, y=231-11 (=220), image="image/name_tag",   -- 188x40
                Text{ x=17, y=15, w=156, h=19, font=charNameFontDark, label=LabelString(self.name),
                      flags=kVAlignCenter+kHAlignCenter } } }
```
* `char` is passed **the whole `LCharacter` table `self`**, not its name string (`sim/character.lua:86`, bytecode `SETTABLE R9["char"] := R0`; the label right below uses `self.name`). So the native reads `char.name`, and possibly `char.mood`. Chocolatier 3's `CharWindow` takes `name` and `happiness` keys (`port-window.md` 2.2). **Unknown:** which fields of the table the native reads (inference: `name` for the image path, `mood` for the initial face). `ui/chartalk.lua:10` reads `char.name` only for the dialog name.
* `Visual` callers: `ui/chartalk.lua:40`, `ui/marketui.lua:275`, `ui/shopui.lua:504`, `ui/haggle.lua:49` (mood logic), `ui/factory_minigame.lua:189`, `ui/factory_out.lua:49`, `ui/factory_status.lua:166`, `ui/gamble_bonus.lua:259`, `ui/gamble_dice.lua:203`, `ui/intro.lua:45`.
* Assets (fact): `characters/<name>.png` always exists: 244 PNGs. 45 characters have `<name>_pleased.png`, `_neutral.png`, `_displeased.png` as well (e.g. `ab_marketkeep`: body 172x240, faces 118x141, 119x138, 118x141; `ca_shopkeep` faces 126x111, 126x111, 129x110). Characters with moods: keepers, `gambler`, `sf_tutor`, `villain01`. Characters without: `traveler01..16`, `taster_*` (five expression PNGs, selected by name), `announcer`, `talker`, `lender`. No `characters/*.xml` sheets exist. Where the face overlay is placed over the body is **unknown** (it must be an offset the native knows or derives, or encoded in the file; not visible to Lua).

### 3.2 Moods (fact: `sim/character.lua`)

* `LCharacter.kHappy = 70`, `kAngry = 25`, `kHaggleDelta = 4`, `kPurchaseDelta = 10`, `kDefault = 50` (`:17-21`).
* `new` (`:38-72`): `defmood = t.defmood or kDefault`; `mood = defmood`; `moodWeek = 0`; `action` becomes a one-element table if a function; every non-function action entry gives `bsgDevWarning("Character <name> contains a bad action: ...")`; `quests = {}`, `endQuests = {}`.
* `IsHappy`: `mood >= kHappy` (70). `IsAngry`: `mood <= kAngry` (25). `IsNeutral`: `kAngry < mood < kHappy` (`:125-135`).
* `SetMood(m)`: clamps `m` to 0..100, stores `self.mood = m`, then calls the native `SetCharacterMood(self.name, m)` (`:184-191`; the only call site of the native is `sim/character.lua:190`).
* `MoodTick()` (`sim/sim.lua:755`, called once per week of the sim tick): every character whose `mood` differs from `defmood` moves 1 toward it (`:117-121`).
* Mood events: purchase/sale `SetMood(mood + kPurchaseDelta)` (`ui/marketui.lua:141`, `ui/shopui.lua:150,185`); haggling `kHaggleDelta` (`ui/haggle.lua:50`); `sim/market.lua:159`, `sim/shop.lua:147`; quest rewards (`sim/quest_functions.lua:559-663`).
* Saves keep only characters whose `mood ~= defmood` (`BuildSaveTable`, `:164-172`).
* **`SetCharacterMood(name, mood)`**: native. Chocolatier 3's twin `SetMood(name, happiness)` selects a face frame by `h*3/100` (`ledger-and-game-natives.md` 6), but Chocolatier 2 passes the 0..100 number and keeps its own thresholds in Lua, so the C2 native needs *some* mapping from number to one of three PNG overlays. **Unknown** whether it uses 25/70 (Lua constants) or thirds (C3). The safest reading: faces `displeased` for `mood <= 25`, `pleased` for `mood >= 70`, else `neutral`, because `IsAngry`/`IsHappy` use exactly those and `ui/haggle.lua` branches on `IsAngry` (inference).

### 3.3 `chartalk.lua` (fact: `ui/chartalk.lua`)

`DisplayDialog{ "ui/chartalk.lua", char=<LCharacter>, fullbody=<text> | body=<key>, ok="ok" , yes=, no= }` (callers: `sim/building.lua:208,213`, `sim/character.lua:217,220`). The script reads `gDialogTable`: `char`, `yes`, `ok` (default `"done"`), `no`, `name` (default `char.name`, else `"chartalk"`), `body` (via `LabelString`) or `fullbody` (prefixed `"#"`). Width 580, height 280. Layout: `Frame{ x = ui.xOffset + (ui.screenWidth-580)/2, y = 50 + ui.yOffset, w=580, h=280, color=CharacterColor }` containing `Text{ x=10, y=5, w=580-ui.charW-10, h=280-55, label=body, font=dialogFont, kHAlignLeft+kVAlignCenter }` and `TextButton`s `yes`, `no`, `ok` (`close=true`; `ok` `default=true`) at `y = 280-45`. Then `char:Visual(ui.screenWidth - (ui.screenWidth-580)/2 - ui.charW + ui.xOffset, ui.yOffset, charNameFontDark)` (character at the right edge) and `BuildingBanner(gActiveBuilding)`. Buttons not supplied are disabled with `EnableWindow(name,false)` (`:46-50`; only the supplied ones stay enabled; the exact conditions are in lines `:46-50`).

Equivalent Chocolatier 3 usage is `ui_chartalk`-like; the structure carries over via `Frame`+`Text`+`Character`+`Button`.

---

## 4. `Sprite`, `AnimationWindow`, `bsgAddSprite`

### 4.1 `Sprite{...}` (window class used in dialogs, not the port)

Fact (all uses): `Sprite{ x, y, image [, rotation] [, tint] [, scale] }`.
* `image` is a path without extension, or an `.xml` sprite sheet: `"image/port_current"`, `"image/port_new.xml"`, `"image/factory.xml"`, `"image/factory_idle.xml"`, `"image/icon_shop"`, `"image/icon_lab"`, `"image/gun_shoot.xml"`, `"image/trayring"`, `"item/cacao"`, `"glitz/sparkle.xml"` (`ui/map.lua:83`, `ui/ledger_badge.lua:18`, `ui/help2.lua:27-39`, `ui/help7.lua:25-28`, `ui/help10.lua:70`, `ui/recipebook_type.lua:66`).
* `rotation` in **degrees** (`ui/help7.lua:25`: `rotation = 180` for `gun_shoot.xml`) - inference from the value.
* `tint` a `Color` table (`ui/help7.lua:27`).
* No `w`/`h`; size is the image's. `x,y` is the **centre/registration point**, not top-left: `ui/map.lua:83` places a 32x32 marker at `x=y=kPortIconRadius(17?)`. Fact: `ui/map.lua:6-12` sets `kPortIconSpacing=13`, `kPortIconRadius=17` and `Sprite{ x=kPortIconRadius, y=kPortIconRadius }` inside a `Rollover{ w=h=kPortIconRadius }`; so x,y of 17 for a 32x32 image positions the registration point (16,16) near the window origin (inference; the `port_new.xml` frame has `regx=16 regy=16`).
* Sprite sheets: see 4.3.

### 4.2 `AnimationWindow{...}`

Fact: `AnimationWindow{ x=5, y=5, w, h, fps=20, <children> }` (`ui/help2.lua:19`, `help7.lua:13`, `help10.lua:15`, `ui/recipebook_type.lua:106`). Children are `Bitmap`s, `Text`s, `Sprite`s and `Group`s. Only `fps` is a new key. **Inference:** it draws its children like a plain window and invalidates/redraws at `fps` so that `Sprite` sheets inside animate; there is no frame table, so it is not a "one child per frame" cycler (the design table's `LayeredWindow` guess is not supported by these call sites: the children are static pictures and texts of a help page). Chocolatier 1's twin is `SimpleAnimation` (`image`/`anim`/`fps=10`/`random`/`path`; `choc1-natives.md` 5), which is a single image.

### 4.3 Sprite-sheet XML (fact: `ZXML` files)

`ports/camelwalk.xml`, `car_01/03/04/05.xml`, `waterfall.xml`, `image/port_new.xml`, `secret_port_new.xml`, `glitz/sparkle.xml`, `image/factory.xml` ... are `ZXML` wrapped: `"ZXML"`, `u32 compressedLen`, `u32 rawLen`, then a zlib stream starting at byte 12 (`startup-and-assets.md` 2). The decompressed XML has the *same* shape as Chocolatier 3's sheets:

```
<sequence name="..." source=".\car_01_*">
  <img regx="63" regy="34" name="car_01.png" w="0" h="0"/>
  <framelist>
    <frame regx="58" regy="28" name="car_01_01.png" w="116" h="56" x="0" y="0"/>
    <frame ... x="0" y="57"/>
  </framelist>
  <script luascript="function DoAnim() delay(140) while true do frames { 1,2 } end end"></script>
</sequence>
```
`regx/regy` per frame is the anchor inside the frame, `x,y,w,h` the rect inside the sheet PNG, `luascript` the animation script (`delay(ms)`, `frames{...}` 1-based; same as `port-window.md` 6). Examples: `image/port_new.xml` 2 frames of 32x32, `delay(500)`, loops `frames{1,2}` (the pulsing "new port" marker, 500 ms per frame); `car_01.xml` 2 frames `delay(140)`. `camelwalk.xml`: 467x450 sheet of many frames (regs 54..59 x 55..56). So the C3 `TAnimatedTexture::ParseFromXML` reader applies unchanged.

### 4.4 `bsgAddSprite(t)` - argument shape

`bsgAddSprite(table)`, no return, called with the sprite or building table (section 1.3). The exact key list the native reads is **unknown**; the keys that exist in the tables passed are listed in 1.3. Required in practice: `image`, `x`, `y`.

---

## 5. Map and travel

### 5.1 `Map{...}` and `MapTravel{...}` (fact: `ui/map.lua:121-137`)

```
MakeDialog{ bsgScreen{
   AppendStyle{ ports=LPort._ByName, routes=gSim.port:GetRoutes(), current=gSim.port.name },
   Map{ x=0, y=0, w=kMax, h=kMax,
        Bitmap{ x=kCenter, y=kCenter, name="map", image="image/map",
                Bitmap{ x=0, y=0, name="colormask", w=kMax, h=kMax }, Group(portIcons), Group(portLabels) } },
   MapTravel{ x=0, y=0, w=kMax, h=kMax, xOffset=ui.xOffset, yOffset=ui.yOffset },
   NormalLedger() } }
```
* **The style carries data**: `AppendStyle{ ports, routes, current }` before `Map` puts the whole `LPort._ByName` table, the current port's route table (`gSim.port:GetRoutes()`: a table keyed by destination name of route tables, each `{ totalWeeks, [1]=leg, [2]=leg, ... }`) and the current port name into the style stack. So `Map` (or `MapTravel`) reads three style keys `ports`, `routes`, `current`. Chocolatier 3's `MapWindow` took Lua globals `Player`/`_AllPorts` instead and had `background`, `yFar`, `yNear`, `farScale`, `nearScale`, `cloudY`, `cloudTime` keys (`map-window.md` 2); none of those keys occur in Chocolatier 2's Lua, so travel scale and clouds are either fixed in the native or absent. **Unknown.**
* The window is split in two classes: `Map` (background + children) and `MapTravel` (the vehicle animation). `MapTravel` is what `InitiateTravel`, `TravelOneWeek` and `StillTravelling` talk to (inference from names and from `ui/map.lua:134`; the natives are called from `sim/travel.lua`, not from `ui/map.lua`).
* `image/map.jpg` is the map; `image/map_color.jpg` is probably what `colormask` uses (it has no `image` key). Both exist in `image/`. `ResetColorMap()` pairs with it (1.8).
* Port icons (fact, `ui/map.lua:20-86`): `kPortIconSpacing=13`, `kPortIconRadius=17`. For each port in `LPort:AllPorts()`: marker `(mapx-17, mapy-17)`. A `Bitmap{ image="image/port_current" }` is added at the current port. Icon image chosen: `image/port_unavailable` (not available, or hidden), `image/factory.xml` (or `image/factory_idle.xml` if the factory's `idle` flag is set) when `factoryOwned`, `image/icon_shop` for `shopOwned`, `image/icon_lab` for `labOwned`, `image/port_available` when available and visited, otherwise `image/secret_port_new.xml` (hidden) or `image/port_new.xml` (available, not yet visited). A `Rollover{ x=iconx+1, y=icony-6, w=17, h=17, xAbsolute=mapx+offset+ui.xOffset, yAbsolute=mapy+offset+ui.yOffset, contents=port:RolloverTarget(), command=<click>, Sprite{x=17,y=17,image=...} }` is made per port. The popup offset: `xAbsolute = mapx + (travelTag flip ? -travelTagWidth : 0) + ui.xOffset`, `yAbsolute = mapy - travelTagHeight/2 + ui.yOffset`; the tag flips left when `mapx + travelTagWidth >= screenWidth` (`:21-30`; `ui.travelTagWidth=210`, `travelTagHeight=100`).
* Labels: `Text{ w=140, h=20, font=portLabelFont(standardFontName kEighteen DefaultColor), label="#<outline color='ffffff' size=1>" .. GetString(port.name), flags=port.labelpos }`; x/y from `mapx/mapy` with `labelpos` (`kHAlignRight`: x -= 151 and x -= 145 variants; `kHAlignLeft`: x += 3 ...), clamped to >= 0. Only for available, non-hidden ports (`:88-115`).
* Click on an icon (closure `main.0`, `ui/map.lua:71-80`): if the port is available (and no travel in progress): disable `to_port`/`to_port_label`; if it is the current port call `gSim:EnterPort()`, else `gSim:TravelTo(port)`; re-enable both afterwards.
* After building: `gViewMode="map"`, `SetLedgerMode("map")`, `SetMusicState("normal")`, `gSim.port:SetAmbient()`, `bsgTriggerAd()` (`:139-143`).
* `gTravelActive` is a Lua global set to `false` in `ui/map.lua:13`, to `true` in `Simulator:TravelTo` (`sim/travel.lua:15`) and `nil` on arrival (`:125`).
* The travel-tag rollover for a port (`LPort:MapRollover`, `sim/port.lua:237-294`): popup text lines "#<name>"; for an unavailable port `travel_freedisabled` (free-play) or `travel_disabled`; for the current port `travel_here`; else, with `r = gSim.port:GetRoute(port.name)`: `travel_time` (weeks; with the `airship` quest variable > 0, `floor(totalWeeks/2 + 0.5)`), `travel_price` (`totalWeeks * gSim.travelCost`, 0 with the airship), `travel_expensive` if the player cannot pay, `travel_click` if they can. It is a `MakeDialog` of two `Text`s (x=5/51) over `Bitmap{ image="image/traveltag" (210x100), fit, hflip=<tag flipped left> }`.

### 5.2 Routes (fact: `ports/portroutes.lua`, `ports/portlocations.lua`, `sim/travel.lua:184-293`)

* `LPort.Locations[port] = { mapx, mapy, labelpos }` (24 entries).
* `DefineRoute(a, b, ...)`: `(a, b, via)` with a string third arg = go via that port (route table `{ via = viaName }`, completed later); or `(a, b, leg1, leg2?)` where a leg is a table `{ weeks, type, {x,y}, {x,y}, ... }`; `totalWeeks = leg1.weeks + leg2.weeks`. 190 `DefineRoute` calls; 0 `DefaultRoute` calls in the shipped data. `DefaultRoute(a, b, weeks=1, type="boat")` builds a straight leg `{ weeks, type, {ax,ay},{ax,ay},{bx,by},{bx,by} }` (4 control points, a cubic Bezier with doubled end points; the same encoding as Chocolatier 3 `map-window.md` 6.1).
* Leg `type` strings in the shipped data: `ship` 40, `train` 10, `raft` 6, `camel` 3, `hiker` 2, `canoe` 1, plus `boat` (DefaultRoute default) and `airplane` (the airship quest synthesises one leg, `sim/travel.lua:26-34`). `weeks` per leg: 1 (32 legs) or 2 (30 legs). Control-point counts range up to 16 (five segments), e.g. abidjan to buenosaires has 7 points.
* `ReverseRoute`, `BuildCompleteRoute(a,b)` (finds reverse or via routes), `AppendRoute` as in Chocolatier 3's `GetRoute` (`map-window.md` 6.1); they set `compound=true` on merged routes.
* **Travel price** (`sim/travel.lua:11`, `sim/port.lua:259`): `route.totalWeeks * gSim.travelCost`; zero when `LQuest:Variable("airship") > 0`. `gSim.travelCost` is computed in Lua (not decoded here).

### 5.3 Travel natives (fact: call sites in `Simulator:TravelTo`, `sim/travel.lua:9-129`)

Sequence (mid-function parts omitted):
1. Look up `route = gSim.port:GetRoute(gSim.port.name, toPort.name)` (`:10`), `price = route.totalWeeks * gSim.travelCost` (0 with the airship). If `gSim.money < price` stop (`:13`).
2. `gTravelActive = true`, `gDestinationPort = toPort` (`:15-16`).
3. With the airship (`LQuest:Variable("airship") > 0`) replace the route by one synthesised leg of `weeks = floor(totalWeeks/2 + 0.5)`, `type="airplane"`, path `{from.mapx,mapy}` x2, `{to...}` x2 from `LPort.Locations` (`:20-35`).
4. Encounter odds (`:41-67`): `meetingOdds = 200` (per 1000), `meetingTime = totalWeeks + 1`; `disasterOdds = 1000 * totalWeeks / 52`, `disasterTime = totalWeeks + 1`; the `_travel` building `ForceEncounter` may force one (`:49-50`); rank 0 forces odds to 0 (`:57-62`); a disaster needs `gSim.weeks - gSim.disaster > 40` and `bsgRandom(1,1000) <= odds`; if the origin or destination is hidden, `meetingTime` is a random week `bsgRandom(1, totalWeeks - 1) + 1` (`:65-67`); the exact expressions were not fully traced (see Unknown).
5. `StartProductionAnim()` (`:72`).
6. For each leg `k` of the route (`ipairs(route)` over the array part): `InitiateTravel(leg)` (`:78`; the `leg` table is `{weeks, type, [1..n]={x,y}}` as built by `DefineRoute`/`DefaultRoute`), then for each week `i = 1 .. leg.weeks` (`:79`):
   * increment a running week counter; if it equals the disaster week: `StopProductionAnim()`, `Simulator:Disaster(self)`, `StartProductionAnim()`; if it equals the encounter week: `StopProductionAnim()`, the `_travel` building's `OnActivate()`, `StartProductionAnim()` (`:81-89`).
   * on the very last week of the last leg: `SetMusicState("normal", "music/<dest.name>_sting.ogg")` (`:94-95`).
   * `TravelOneWeek(leg.type)` (`:98`), then `Pause(1900)` (a coroutine sleep of 1900 ms, `:99`), then `while StillTravelling() do Yield() end` (`:100`; `Yield` is the coroutine yield global), then `self:Tick()` (the weekly sim tick, `:101`), then the stalled-factory check: if `self.stall` and not `self.stallwarn` and `rank > 0`: `stallwarn=true`, `SetLedgerContents("factories")`, `DisplayDialog{"ui/okdialog.lua", body="factory_stalled", ledger=true}` (`:103-106`); then `StartProductionAnim()` (`:109`).
7. `StopProductionAnim()` (`:113`); charge the fare (`AdjustMoney(-min(price, money))`, `:116-119`); `gTravelActive=nil`, `gDestinationPort=nil`; `Simulator:ArriveAtPort(toPort)` (`:125-127`).
8. `Simulator:ArriveAtPort(port)` (`:131-156`): `SetPort`, `PrepareTravelPrices`, `QueueMessage(GetString("port_arrived", GetString(port.name)))`, medals check (`ports` medal; `gNewMedal="ports"`, `AwardMedal("ports")`, `UpdateBadge(gSim)`), `EnterPort`. `Simulator:SetPort(port)` calls `ResetColorMap()` when the port had not been visited (`:162`) and sets `visited = available = true`, `LItem:PreparePrices()`, `LTip:ApplyAll()`. `Simulator:EnterPort()` is `SwapToModal("ui/portview.lua")` (`:178`).

Native shapes as called:
* `InitiateTravel(leg)`: argument one table (a route leg). No return used. Starts a leg: the vehicle follows `leg[1..n]` as a Bezier chain (inference). **Vehicle art (fact: files):** `image/icon_<type>.png` exists for exactly the leg types in the data: `airplane`, `camel`, `canoe`, `hiker`, `raft`, `ship`, `train` (static PNGs, no sheets, no `icon_boat`), plus `image/traveldot.png`. Inference: the native loads `image/icon_<type>` for the leg's `type`. Chocolatier 1 loaded `image/icon_boat.xml`, `icon_train.xml`, `icon_airship.xml` and `image/traveldot` in `TMapScreen` (`FUN_00418000`, strings in the ctor). Unknown: whether the icon is rotated to the heading (Chocolatier 3 picks a rotation frame from a sheet; Chocolatier 2's icons are single images), and whether trail dots are still laid.
* `TravelOneWeek(type)`: argument the leg's type string. No return. Moves the vehicle along the leg for one week. Cadence: `Pause(1900)` then wait until `StillTravelling()` is false: so a week takes about 2000 ms (inference; Chocolatier 1's `AnimateTravel` used `weeks * 2000` ms per leg, `FUN_00417da0`: `round(time * 2000)`). Chocolatier 3 uses 3000 ms per week (`map-window.md` 10). A leg of `weeks = 2` is two calls, each advancing half (inference).
* `StillTravelling()` -> boolean: true while the one-week animation is running.
* `PauseTravel()` / `ResumeTravel()`: **not called anywhere in Chocolatier 2's Lua** (zero call sites in `ui/` and `sim/`). The C1 twins exist as natives (`FUN_004165e0` registration of `PauseTravel`/`ResumeTravel`, `choc1-natives.md`). Chocolatier 2 handles dialogs in the middle of travel by stopping the Lua coroutine (the encounter and disaster dialogs run inside the travel coroutine) so no pause is needed. Facts: `ui/ledger.lua` buttons (`recipe`, `pause`) call no pause helper.

### 5.4 Chocolatier 1 ancestor (`TMapScreen`, `re/choc1`)

* Class `TMapScreen`, ctor `00418000` (7350 bytes), vtable `TMapScreen::vftable`. Registers `AnimateTravel`, `PauseTravel`, `ResumeTravel`; destructor `004172f0` re-registers them as nil (`choc1-natives.md` 1). Loads `image/icon_boat.xml`, `image/icon_train.xml`, `image/icon_airship.xml` and `image/traveldot`.
* `AnimateTravel(table)` (`00417da0`, thunk `004165a0`): reads `endName` (string) and a table of legs; for each leg builds a `LinearPath` object (`FUN_00417650`, vtable `LinearPath::vftable`) from its `path` key, a duration `round(leg.time * 2000)` ms (`time` defaults to 1.0) and `vehicle` (default `"boat"`). Lua call: `AnimateTravel{ endName, legs = { {time, vehicle, skip, path = { {x,y}, {x,y} } } } }` (Chocolatier 1's archive, `sim/travel.lua:276-291`). C1 paths are straight (two points) and the airship leg is `vehicle="airship"`.
* C1's `ui/map.lua` is `MapScreen{ x=0,y=0,w=800,h=600 }` on top of `Bitmap{ image="image/map" }` plus `LPort:LayoutMap()` windows built in Lua.

### 5.5 What `map.lua` draws, in order

`Bitmap map` (image/map), `Bitmap colormask`, port icons and labels groups, `MapTravel` (vehicle, trail), the `NormalLedger`. Trail dots (`image/traveldot`, one per 12 px in Chocolatier 3, `map-window.md` 7) are **not visible from the Lua**, but `image/traveldot.png` exists in the Chocolatier 2 data (so the dots very likely carry forward; inference). There is no `worldmap_clouds` image in Chocolatier 2's `image/` (fact: `ls image | grep cloud` is empty), so the Chocolatier 3 cloud strip does not carry forward.

---

## 6. Other Chocolatier 2 facts used by these windows

* `NormalLedger()`, `BuildingBanner(building)` (used by `ui/chartalk.lua:41`), `LabelString(key)` (a `GetString` with `#` handling), `MakeRollover{...}`, `TightText`, `TextButton`, `DisplayDialog`, `DoModal`, `SwapToModal` are Lua-defined (`ui/uihelp.lua`, `ui/styles.lua`).
* `bsgDate(weeks, days)` has two arguments here (`ui/ledger_badge.lua:33`), three in Chocolatier 3.
* `SetMusicState("normal", file)` file argument is `music/<port>_sting.ogg` for the arrival sting (`sim/travel.lua:95`). `SetMusicState("silent")` for hidden ports (`sim/port.lua:202`).
* `ui.xOffset`, `ui.yOffset` are the letterbox offsets that every free-positioned Chocolatier 2 window adds (`ui/uiconstants.lua`); `ui.charW=191`, `charH=240`, `travelTagWidth=210`, `travelTagHeight=100`, `ledgerWidth=498`, `ledgerHeight=157`, `scrollArrowW=24`, `scrollArrowH=48`, `devButtonW=180`.

---

## 7. What Chocolatier 3 code can be reused, and what differs

Ancestors: Chocolatier 1 (`re/choc1`) has `TMapScreen` (map, `00418000`), `TDynamicWindow` (ledger tab bodies, `00409ad0`, vtable `004e6f9c`, `UpdateDynamicWindow` `00409740`), `TRolloverPopup` (popups, `0041b960`), `TSimpleAnimation` (`004016d0`), `TImagePulser` (`0041ae50`, tick `0041aa30`), the money counter (`00403320`, `SetMoneyDisplay` `00403090`, `SetMoneyDisplayTarget` `00403240`), `UtilSetLedgerTab` (`0041ee10`). It has no port, ledger or character window. The Chocolatier 2 classes are descendants in role, but the Chocolatier 3 classes are the closest *implemented* match.

| Chocolatier 2 | Chocolatier 3 class | Same | Different |
|---|---|---|---|
| `Port` | `PortWindow` (`port-window.md` 1) | Sprite tree on a root background; centre-anchored building sprites; hover highlight; path sprites on cubic Beziers; sprite sheets (`ZXML`, `regx/regy`, `delay`/`frames` script); `htile`; perspective `far/near` scale; rectangle hit test (inference); click -> Lua | Data source: bsgAddSprite calls from Lua layout closures instead of the style key `port` holding a `Port` table. Click dispatch via a per-sprite `command` closure, not `<name>:OnClick()` eval. Hover popup via per-sprite `contents` string. Background is a sibling `Bitmap`, not a `background` key. Key names: `time`, `loop`, `bounce` (bool), `far`, `near`, `farscale`, `nearscale`, `hold`, `random` (percent, in Lua) vs `motion`, `yFar`, `yNear`, `scaleFar`, `scaleNear`, `frequency`. No `layer`, no `behind`, no `masks`/`tints` (mask images are separate `SpriteLayer`s: `ca_pyramidmask`). The ambient name is a path (`sfx/x_loop.ogg`). No `musicDelay`/random port music: music is `SetMusicState`/`SetAmbient`. |
| `Ledger` | `LedgerWindow` (`ledger-and-game-natives.md` 1-3) | Finds named `Text` children (`money`, `rank`, `score`, date), money counter animation, quest indicator, "production" animation | Built in Lua as a window with `data=gSim`, three tab bodies, `SetLedgerContents`/`UpdateLedgerContents`, no singleton `GrabLedger`/`ReleaseLedger`, no `DookieDropper`, quest bar sliding (`showQuestY`/`hideQuestY`), scrolling text API. |
| `Character` | `CharWindow` (`port-window.md` 2) | Fixed texture layers, per-name list for `SetCharacterMood`, mood -> face overlay with ~200 ms cross-fade (inference for C2) | Assets are PNG sets not sheets; `char=<table>` key, not `name`; no `happiness` or `scale` or `alpha` keys passed; the overlay offsets unknown; thresholds 25/70 vs thirds. |
| `Map` + `MapTravel` | `MapWindow` + `TravelTask` (`map-window.md`) | Port markers per state, Rollover popups at `xAbsolute/yAbsolute`, port labels, Bezier routes with doubled end points, `InitiateTravel`, `gTravelActive`, heading-frame vehicle (inference) | Style keys `ports/routes/current`; two classes; per-week `TravelOneWeek`/`StillTravelling` instead of an event list; `PauseTravel`/`ResumeTravel` absent; ~2000 ms/week not 3000; route legs carry `type` per leg and weeks per leg (not `time`/`type` list); 8 vehicle types; `ResetColorMap` and `colormask`. |
| `Sprite` | `BitmapTint` / sprite sheets in `gfx` | `x y image`, `.xml` sheets | `rotation`, `tint` keys; centre-anchored. |
| `AnimationWindow` | none (nearest: plain `Window`) | | `fps` only. |
| `DynamicWindow` | `TDynamicWindow` in Chocolatier 1 (`choc1-natives.md` 4) | Keys `x y w h name contents`; rebuild by name | `init=false`; `UpdateDynamicContent` is the same as C1's `UpdateDynamicWindow` (name changed). |

---

## Unknown

* What `bsgAddSprite` reads beyond the keys listed; draw order; hit-test; hover and click timings; how `.LOGO` is rendered; whether `htile = true` means a default period; `hold` semantics; whether the `random` key is also read natively.
* The `Port` and `Map` native internals (the whole binary is packed). Nothing about layer sorting, pulse timing or the label fade has been confirmed for Chocolatier 2.
* `SetCharacterMood`'s thresholds, the face overlay position over the body, whether `Character` reads `char.name` / `char.mood`.
* The `Ledger` native: which fields of `gSim` it reads, how `SetQuestHint` slides the bar, `SetLedgerMode` exact effect, what `UpdateLedgerContents` does if the tab is not showing, counter animation speed and `ResetLedger`'s exact effect.
* `StartProductionAnim`/`StopProductionAnim` owner and effect; `ResetColorMap` and `colormask` texture; the `Map` icon animation speed (`port_new.xml` is 500 ms per frame in its script).
* The per-week duration (about 2000 ms is inferred from `Pause(1900)` and Chocolatier 1) and whether vehicle icons rotate.
* The airship `ForceEncounter` / disaster probability expressions in `sim/travel.lua:41-67` were read only partially.
* `NormalLedger`, `BuildingBanner` and `MakeRollover` bodies (in `ui/uihelp.lua`).

---

## 8. Data facts for the port scene

Measured on `re/choc2/pfp_extracted/` (scripts and `.lst` listings are in the session scratchpad, not in git). Lua line numbers are source lines from the bytecode line table.

### 8.1 Character face placement (answers §7 Q1)

* **Sizes (fact).** 45 characters have all of `<n>.png`, `<n>_pleased.png`, `<n>_neutral.png`, `<n>_displeased.png`. Bodies are 172x240 (a few 170x240). Faces are smaller and vary per character: width 100..154, height 83..152. The three moods of one character have slightly different sizes.
* **The body already has a blank face (fact, by eye).** The body is a head with no features (flat skin tone, hair present). The face PNG holds hair, face and neck cropped to its own opaque extent, with a transparent margin on its left and top (for `man_marketkeep` the opaque part of the face starts at (35,22), where the body's head also starts).
* **Rule: the face PNG is drawn with its top-left at the body's top-left, (0,0) (fact for 34 of 45, near-fact for 10, unresolved for 1).** Method: slide each mood's face over the body; score = mean colour difference over the pixels opaque in both, plus 300 x the fraction of the face's opaque pixels that fall on a transparent body pixel. The scores are noisy (the face has features the body lacks, so the colour term is never near 0, 8.9 at best), but the offset is stable across the three moods.
  * 34 characters: best offset (0,0) for all three moods.
  * Within 1 to 2 px of (0,0) (probably noise, not a real offset): `ca_marketkeep` (0,1 for displeased only), `cay_marketkeep` (1,2), `ma_marketkeep` and `sh_marketkeep` (1,1; displeased 0,0), `mos_shopkeep` (1,0), `sf_bchqkeep` (0,2), `syd_shopkeep` (0,2), `villain01` (1,0), `cay_shopkeep` (5,0 / 0,0 / 6,0, mixed).
  * Unreliable: `ab_farmkeep` (6,15 / 9,3 / 6,16) and `ja_farmkeep` (49,76 / 48,77 / 45,82). For `ja_farmkeep`, (0,0) gives a colour error of 2.3 but 63% of the face's opaque pixels land on transparent body pixels, so the body's head is not where the face expects; the two bodies may differ from the face art. Treat both as unknown and use (0,0).
* **Inference:** the face and body come from one authoring canvas with the face layer cropped on the right and bottom only, so the engine needs no per-character offset: draw the face at the body's (0,0). Replace the design's "top centre on top centre" fallback with this. The residual 1 to 2 px cases are not worth a table.
* No mood-specific offset was found (the same offset won for all three moods in every character except as listed).

### 8.2 `camelwalk.xml` and the `car_0*.xml` sheets

* **One direction only (fact).** Each sheet has a single `<framelist>` and one `DoAnim` script; there are no left and right sequences and no sequence names (the sheets are `<sequence>` files with one list, no `name` per list).
  * `camelwalk.xml`: 14 frames in `framelist` (frame sizes 107..122 x 106..112, regs about 54..59 x 55..56), but `DoAnim` is `delay(100)` then loops `frames { 1,2,3,4,5,6,7,8,10,12,14 }` (11 frames, 100 ms each; frames 9, 11 and 13 are never shown).
  * `car_01.xml`: 2 frames 116x56, reg 58,28, `delay(140)`, `frames{1,2}`.
  * `car_03.xml`: 2 frames 126x54 and 126x52, `delay(120)`.
  * `car_04.xml`: 2 frames 125x51 and 125x50, `delay(80)`.
  * `car_05.xml`: 2 frames 122x56, `delay(100)`.
  * `car_02` and `car_06` to `car_12` have only a PNG, no sheet (static images). `car_01`, `03`, `04`, `05` are the only sheet cars.
* **Direction is not selectable in the sheet.** The Lua has no key that picks a direction. The camel paths in `cairo.lua:55`, `casablanca.lua:60`, `sahara.lua:29,32,35,50` run left to right (for example cairo: x from -40 to 839, and -63 to 236 and beyond), and all carry `bounce`. Whether the engine mirrors a sprite when a `bounce` path reverses is **unknown** (see Unknown below). One sprite key exists that looks like a mirror: `hflip`, used once (`sanjose.lua:62`, the `waterfall.xml` sprite), so the native does know a horizontal flip. Sprite-key counts over all 243 `SpriteLayer` calls: `image` 197, `path` 139, `time` 109, `x`/`y` 104, `scale` 80, `hold` 73, `bounce` 53, `imagelist` 46, `loop` 37, `htile` 27, `farscale`/`nearscale` 24, `near` 24, `far` 20, `startweek` 10, `random` 9, `endweek` 6, `ambient` 3, `wait` 1 (`bombay.lua:37`, value 20000), `hflip` 1.

### 8.3 `bsgAddSprite` path sprites with no `time`

243 `SpriteLayer` calls in `ports/*.lua`: 139 have a `path`. 109 of those have `time`; **30 do not**. The other 104 `SpriteLayer`s have no `path` and no `time` (static layers, not counted).

The 30 path sprites without `time` (port file:line, image; other keys in brackets):

* `buenosaires.lua:48` car_07 (far 180, near 377, farscale .15, nearscale .4); `:50` car_12 (same); `:59` car_11 (farscale .13); `:66` imagelist, scale .5
* `cairo.lua:33` imagelist, scale .25, hold 25000; `:44` boat_01, scale .4, hold 15000, bounce
* `casablanca.lua:47` car_07, `:49` car_12, `:56` car_07 (far 160, near 320, farscale .1, nearscale .5); `:68` imagelist, scale .5, hold 5000, bounce
* `manila.lua:57` car_07 (far 250, near 600, farscale .35, nearscale 2)
* `moscow.lua:29` imagelist, scale .25, hold 12000; `:64` imagelist, scale .5, hold 2000, bounce; `:67` imagelist, scale .5, hold 3000
* `newyork.lua:59` train_01 (scale .2, hold 3000); `:61` train_01 (hold 10000); `:73` train_07 (far 255, near 400, farscale .3, nearscale 4.5, hold 8000); `:75` train_06 (same, no hold); `:92` car_07 (far 255, near 400, .1/.45, hold 4000); `:94` car_12; `:96` train_08 (.1/.65); `:98` car_07 (hold 2500); `:100` car_12; `:104` car_12 (far 250); `:106` car_07 (far 250, hold 1500)
* `sanjose.lua:39` car_07 (far 197, near 250, .1/.55)
* `shanghai.lua:47` car_07, `:49` car_12 (far 212, near 300, .05/.3); `:61` car_10 (scale .2); `:67` car_07 (scale .3)

Pattern (fact): every timeless sprite is a vehicle (`car_*`, `train_*`, `boat_01`) or an `imagelist` of vehicles (`portCars`, `portPlanes`, `portBoats` style lists), and almost all use `far/near/farscale/nearscale` (perspective) or a plain `scale`. The sprites that do have `time` are mostly clouds (`clouds_0*`), boats, planes and camels.

`time` distribution of the 109 that have it (ms; count): 2700 1, 2800 1, 3000 1, 3500 2, 3700 1, 5000 2, 6000 2, 7000 1, 8000 3, 9000 2, 10000 6, 11000 1, 12000 5, 13000 2, 14000 2, 15000 2, 16000 4, 17000 2, 18000 1, 19000 1, 20000 7, 22000 2, 25000 1, 30000 3, 35000 1, 40000 1, 43000 1, 45000 2, 50000 1, 59000 1, 60000 2, 61000 1, 62000 1, 64000 1, 80000 1, 90000 2, 95000 1, 100000 1, 103000 2, 110000 1, 115000 1, 120000 1, 130000 2, 140000 2, 150000 5, 160000 1, 170000 1, 180000 2, 200000 3, 210000 1, 215000 1, 220000 1, 238000 1, 240000 4, 245000 1, 270000 5, 370000 1, 380000 1. Range 2.7 s to 380 s, median about 30 s, mode 20 s (7) and 10 s (6).

**Inference for a default** (not a fact): traffic that has `far/near` or `scale` and no `time` is most plausibly either time-from-speed in the native or one fixed default. Evidence: the vehicles that have `time` and are in the same ports (the cars and boats with `time`) sit mostly in 8 to 30 s. A default of 10000 to 20000 ms is a reasonable pick; no source states it. The native's real default is **unknown** (packed binary).

### 8.4 `image/map.jpg` vs `image/map_color.jpg`

* **No (fact).** Both are 800x600 RGB JPEGs, but `map.jpg` is not grey: its channels differ by up to 100 levels. It is a flat-colour region map (pastel continents on blue ocean: North America pink-violet, South America orange, Africa yellow-green, Europe teal, Asia pink, Australia tan, Antarctica grey-blue) with the same graticule and borders. `map_color.jpg` is the shaded relief colour map (greens, browns, snow).
* A greyscale of `map_color` does not reproduce `map.jpg` either: mean absolute difference 34.7 (Rec. 601 luma), 36.0 (Rec. 709), 39.0 (channel mean); correlation of the red channel of `map.jpg` with the grey value only 0.47 to 0.56.
* **Inference:** `map.jpg` is the visible base the Lua draws as `Bitmap map` (the "political" map), and `map_color.jpg` is what `ResetColorMap`/`colormask` is about, or the reverse. Which one is drawn when is **unknown**; the Lua only names `image/map` (see §5). Both images exist, so the engine must pick by what the Lua passes, not derive one from the other.

* **Probable mechanism for `colormask` and `ResetColorMap` (inference):** `ports/` holds 16 `map_mask_<port>.png` (800x600, palette PNGs with a `tRNS` alpha: the RGB is white everywhere and the alpha is 255 only over that port's region, with soft edges). Together with `colormask` having no `image` and `SetPort` calling `ResetColorMap()` on a port's first visit, the likely behaviour is: `colormask` shows `image/map_color.jpg` (the shaded relief) through the union of the visited ports' masks, so regions turn from the flat `map.jpg` colours to relief as they are visited, and `ResetColorMap` rebuilds that picture. Engine: `Map.cpp` composes the picture in pixels once after a reset (`ColorMapState`), independent of `TextureCache`'s blue-channel masks.

### 8.5 `NormalLedger` and what it reads

* **`NormalLedger` is `dofile("ui/ledger.lua")`** (`ui/uihelp.lua:153-154`, tail call, returns the chunk's result, which is the `Ledger{...}` group). **`DialogLedger`** (`ui/uihelp.lua:146-150`) sets the global `gDisableBadgeButtons = true`, runs the same file, then sets it back to nil. Effect (`ui/ledger.lua:20`): with `gDisableBadgeButtons` set, the four buttons below and their captions are **not built**; the ledger body is still built. With it unset (`NormalLedger`), the buttons are built.
* **Captions** (`GetString` keys, then English text from `assets/strings.xml`), all `Text` with `flags = kHAlignCenter + kVAlignBottom`, `font = standardWhiteFont`:
  * `GetString("map")` = "Travel", `name="to_map_label"` (`ui/ledger.lua:29`)
  * `GetString("city")` = "Port", `name="to_port_label"` (`:34`)
  * `GetString("recipes")` = "Recipes", unnamed (`:39`)
  * `GetString("pause")` = "Pause", unnamed (`:45`)
  * Tab labels, direct `label` strings (the `Text` resolves them): `lbl_messages` = "Messages", `lbl_factory` = "Factory", `lbl_status` = "Status", `lbl_inventory` = "Inventory" (`:94-97`), font `LedgerLabelFont` = `{standardFontName, kEighteen, DefaultColor}` (`:7`), flags `kVAlignCenter + kHAlignCenter` (the inventory label is `kHAlignRight`).
* **Button graphics** (`<name>_up/_down/_over`): `control/globe_*` (`to_map`, x=158 y=26), `control/port_*` (`to_port`, same place), `control/recipe_*` (`recipe`, x=75 y=12), `control/pause_*` (`pause`, x=16 y=26); tab buttons `control/btn_messages_*`, `btn_factory_*`, `btn_inventory_*` (radio; the "down" graphic is also the fourth state). Commands: `SwapToModal("ui/map.lua")` (`:28`), `SwapToModal("ui/portview.lua")` (`:33`), `DoModal("ui/recipebook.lua")` (`:38`), `DoModal("ui/pausedialog.lua")` (`:44`), `SetLedgerContents("messages"|"factories"|"inventory")` (`:81,86,91`).
* **Keys read from `gSim` by the ledger Lua** (fact):
  * `ui/ledger.lua:12-13`: `gSim.quest` and `gSim.quest:SummaryText()` (guarded: only if `gSim` and `gSim.quest` exist; the label is `"#"..summary`, else `"#"`); `:55`: `data = gSim`.
  * `ui/ledger_badge.lua:14-16`: `Simulator._Medals` (list of `{x,y,...}`) and `gSim.medals`; `:32`: `gSim.rank` (label `"rank_"..rank`); `:33`: `gSim.weeks`, `gSim.days` (`bsgDate(weeks, days)`). Also the global `gNewMedal` (sparkle on a fresh medal).
  * `ui/ledger_messages.lua:9`: `gSim.messageTop`; `:48,52`: `gSim:ConcatenateMessages()` and `gSim.messageText` (the text shown).
  * `ui/ledger_inventory.lua:12`: `gSim.inventoryTop`; `:58` `LItem.OwnedIngredients`, `:100` `LItem.OwnedProducts` (not on `gSim`).
  * `ui/ledger_factories.lua:16`: `gSim.rank` and `LFactory.weeksPerColumn`; the rest comes from `LBuilding.ByName(...)` fields (`owned`, `product`, `rate`, `idle`, `visited`, `price`, `port`, `InfoRolloverTarget`) and `LItem.ByName(...).inventory`/`.name`, `LFactory.globalNeeds`.
* **Style keys read:** `ui.xOffset`, `ui.yOffset`, `ui.screenHeight`, `ui.screenWidth` (`:53`), `ui.ledgerWidth`, `ui.ledgerHeight` (`:70`); fonts `standardFontName`, `kTwentyEight`, `kEighteen`, `DefaultColor`, `standardWhiteFont`, `dialogFont` (the quest text); flags `kHAlign*`, `kVAlign*`; `kMax`, `kRadio`. `fullLedgerHeight` is the literal 310 (`:9`). These are all Lua globals; `AppendStyle` is not used by the ledger.
* **Natives the ledger calls or needs (by construction, not by call):** `Ledger{}` (reads `data`, `showQuestY`, `hideQuestY` only by name), `DynamicWindow`, `Button`, `BeginGroup`, `Group`, `Bitmap`, `Rollover`, `Text`, `Window`, `GetString`, `SwapToModal`, `DoModal`, `SetLedgerContents`. The native may also read `gSim.money`, `gSim.weeks` and others directly (not visible from the Lua; **unknown**).
* **Window structure** is as in §2.1; this section only adds the button gating, the exact caption text and the key lists with line numbers.

### 8.6 Unknown (new in this section)

* Whether a sprite flips when a `bounce` path reverses (all camel paths go left to right; the sheets have one facing). A capture of Cairo or Sahara would settle it.
* The native default for a path sprite without `time`, and what `wait` (`bombay.lua:37`, 20000) and `hflip` do exactly.
* Which of `map.jpg` and `map_color.jpg` is drawn when, and what `colormask` uses.
* The true face offset for `ab_farmkeep` and `ja_farmkeep` (measurement unreliable), and whether the native shifts faces at all (the 1 to 2 px cases look like noise).
* The native reads of `gSim` by `Ledger{data=gSim}` beyond the Lua-visible ones.
