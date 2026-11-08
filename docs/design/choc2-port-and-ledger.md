# Design: Chocolatier 2's port, ledger, characters and travel

How Chocolatier 2's window classes `Port`, `Ledger`, `DynamicWindow`, `Character`, `Sprite`, `AnimationWindow`,
`Map` and `MapTravel` are built. The Lua-side facts are `docs/re/choc2-port-and-ledger.md` (cited as PL §n) and
`docs/re/choc1-natives.md` (C1 §n). The native behaviour is the Mac binary's where `docs/re/choc2-port-binary.md`,
`choc2-ledger-binary.md` and `choc2-map-binary.md` read it (called **fact (binary)** below; the Windows `chocotwo.exe` is the
same game but packed); where they leave a gap, the behaviour is an **inference**, said so here and in the code. Chocolatier 3
output stays byte-identical: every change to a shared class is data-driven, and its defaults are Chocolatier 3's behaviour.

## 1. What the scripts depend on

Entering a port runs `ui/portview.lua`, which builds a `Port` from layout closures, opens `sf_bchq`'s `chartalk` dialog with a
`Character`, and shows a `Ledger`. The Lua depends on the following.

- **`Port{x y w h xOffset yOffset, Group(layout), Window{name="buildinglabels"}}`** (PL 1.1). The scene comes only from
  `bsgAddSprite(t)` calls that run while the `Port` is built; the native has no window argument, so the window being built is
  the target (PL 1.2). The background is a sibling `Bitmap`.
- **The sprite table** (PL 1.3; the keys read are fact (binary), `choc2-port-binary.md` 1.2): `image x y path time loop bounce
  hold scale htile far near farscale nearscale hflip random`. x,y is the centre; the array order is the only ordering (no
  `layer`); `random` and `imagelist` are already resolved in Lua. A building adds `labelx labely contents command`; `command`
  is a closure that opens dialogs, so it runs in the script coroutine as a button command does, and `contents` is a Lua
  expression whose `MakeRollover{...}` builds the hover popup.
- **The ledger** (PL 2): `Ledger{name x y w h data=gSim showQuestY hideQuestY}`, built by `NormalLedger()` in every port and map
  modal (not a singleton), with three `DynamicWindow` tab bodies (`init=false`), a `badge` `DynamicWindow`, a radio group
  `btn_messages|factories|inventory` and the `quest` Bitmap with `quest_text`. The natives are `SetLedgerContents`,
  `UpdateLedgerContents`, `SetLedgerMode`, `ResetLedger`, `UpdateBadge`, `SetQuestHint`, `UpdateDynamicContent`, `bsgGetMaxScroll`
  and `bsgSetStartLine`. `AdjustMoney` sets `gSim.score` through `GetScore` and calls `UpdateBadge(gSim)`.
- **`DynamicWindow{name x y w h contents init ontext script}`** has Chocolatier 1 semantics (C1 §4): it deletes its children
  and runs the `contents` file with itself as the window being built.
- **`Character{x y w h char=<LCharacter>}`** (PL 3): faces are `characters/<n>.png` plus an optional
  `_pleased/_neutral/_displeased.png`; `SetCharacterMood(name, 0..100)`.
- **`Sprite{x y image rotation tint scale}`** and **`AnimationWindow{fps}`** (PL 4).
- **Travel** is driven week by week from the Lua coroutine (PL 5.3): `InitiateTravel(leg)`, then for each week
  `TravelOneWeek(type)`, `Pause(1900)` and `while StillTravelling() do Yield() end`.

## 2. Decisions

### 2.1 `bsgAddSprite` into `PortWindow`

A pure per-game adapter turns each table into the `BuildingDef`/`SpriteDef` that `PortWindow` (shared with Chocolatier 3,
`src/games/common/PortWindow.*`) already builds from. `choc2::Port` collects the adapter's output and hands it to `PortWindow`
in place of `parsePort`. `PortWindow` has no game checks, only optional data fields and two protected flags.

- `choc2/PortScene.{h,cpp}` has `fromAddSprite(style, order, textureInfo)`, returning a `BuildingDef` or a `SpriteDef`. Key map:

  | Chocolatier 2 key | Result |
  |---|---|
  | `command` or `contents` present | `BuildingDef`: `image` (a full texture name; empty means Chocolatier 3's `ports/<port>/<name>`), `labelX/labelY`, `contents` (string), `command` (a `LuaRef`). The name comes from `contents`' `ByName('<n>')` or the image's basename and is used only for dumps. |
  | `x`, `y` | centre-anchored, plus the Port's `xOffset/yOffset` (0 at 800x600) |
  | array order | `layer = order`: draw order is call order, so the last added wins a hit test |
  | `path`, `bounce`, `loop` | path points, and Bounce or Loop mode |
  | `time` | fact (binary, 1.2): a path sprite with no `time` runs 10000 ms (`kDefaultPathMs`) |
  | `far near farscale nearscale scale` | perspective, also for static sprites and buildings; `scale` is the default of both scales and explicit ones win; clamped at 0 below only |
  | `htile` | the sprite is repeated across the port at the raw texture width |
  | `random` | ignored: `frequency = 100`, random start phase |
  | `hflip` | mirrors the sprite |
  | `.xml` sheet on a path sprite | `headingFrames = false`: a walk cycle played by its own `DoAnim` script, never heading frames |
  | `image == ".LOGO"` | a building whose picture is the player's logo, rendered by `LogoRenderer` from the `logo` table (a `.LOGO` without one is skipped and logged) |

- **Path arithmetic** (fact (binary), `choc2-port-binary.md` 1.4): the scene clock grows 40 ms per task tick;
  `t = (phase + T) mod (time + hold)`; `hold` is a wait at the **start** of each cycle; a bounce is an out-and-back inside one
  `time` (no wait at the far end). Art faces left, so a sprite is mirrored while it moves right (a bounce's return leg counts
  as the opposite direction). `PathSprite` carries this behind `artFacesLeft` and a Chocolatier 2 timing mode; its default
  formula (`pathPhase`) is Chocolatier 3's and unchanged.
- **Additions to `PortWindow`**, each defaulting to Chocolatier 3's behaviour: a protected hook `readScene(style)` (default:
  `parsePort(style.table("port"))`); protected flags `fadeIn_` and `portMusic_` (true; `choc2::Port` clears both: inference,
  Chocolatier 2 has no background sprite to tint and its music is `SetMusicState`); a building's `command` is delivered through
  `sink->deliver(nullptr, &cmd)` on a copy of the ref, the same path a button takes; a building's `contents` opens a popup through
  `ui::openContentsPopup` (`src/ui/widgets/InputWidgets.h`, factored out of `RolloverWindow`), destroyed when the hover moves.
- **Hit test** (fact (binary), 1.3): the sprite rectangle, then the rendered alpha at the pointer must be at least 192
  (`WindowQuirks::spriteHitAlpha`). The hover popup fades its background rectangle to 0.8 and runs its command on any release
  (2).
- **Scene task** (`choc2::Port`, fact (binary) 1.5, 1.6): a 40 ms task. The `buildinglabels` child shows until the first tick at
  or after 3900 ms and is then hidden at once, with no fade; until then every building with a `command` pulses in sync
  (a 1300 ms triangle, +20 px; drawn from a flat white silhouette of the building at its alpha). Any hover popup or
  `StopPulsing()` ends the pulse for all but the hovered building and hides the labels. Inference: the look of the pulse
  beyond what the binary's arithmetic fixes.
- **`StartFireworks`** (fact (binary) 7) creates five `FireworkSprite`s (the machine Chocolatier 3 shares), attached between two
  sprites of the layout; it replaces Chocolatier 3's `StartFireworks` binding.
- `choc2::Port` registers with `World` so `World::tick` drives `frame()`. The foreground pause dialog is a profile flag, off for
  Chocolatier 2. `bsgAddSprite` walks up from the creation top to the first `choc2::Port`; with none (or after the scene was
  built) it logs `[native-warning]` once.

### 2.2 Buildings, popups, pulsing, labels

The click is the `command` closure; `StopPulsing()` is the Lua's first statement in it. The popup is `contents` run as a
Rollover's would be; it closes on hover change, on a press, and when the popup's own listener sees the pointer leave. The Lua
positions the label Frames, so the engine does not move them.

### 2.3 `DynamicWindow` and the ledger built on it

**`game::DynamicWindow : ui::Window`** (`src/games/common/DynamicWindow.{h,cpp}`, shared with Chocolatier 1). It reads
`contents` and `init` (default true). `rebuild()` destroys the children, runs `contents` with the window pushed on the
creation stack (`script::runFileInto`, `src/script/RunFile.h`) and restores the stack. `fitToChildren` runs after the build, as
growth only (**deviation from Chocolatier 1's `TDynamicWindow`**, which takes its size from the keys: the ledger badge is made
without `w` and `h`, and a 0x0 window clips and hit-tests nothing; fact (binary) that the original also fits after each build).
A tab body the ledger hides is emptied, so a name lookup such as `scroll_up` finds the shown body. `ontext` and `script` are
supported as in C1 (rebuild, or run the `script` string, on every `TextEdit` change; the first build is not a change; a change
that arrives while the window's own script or build runs is ignored and counted). A rebuild requested during the same window's
build is ignored and logged (`building_`), because re-entering would destroy the windows the script is filling.
`UpdateDynamicContent(name)` rebuilds every `DynamicWindow` named `name` in the whole modal chain, in tree order.

**`choc2::Ledger : ui::Window`** (`src/games/choc2/LedgerWindow.{h,cpp}`). It is new because Chocolatier 3's `LedgerWindow` reads
`Player`, owns droppers and is a singleton. It has no drawing of its own (fact (binary): `TLedger::Draw` is `TWindow::Draw`); it
keeps the money counter and finds its children by name on each use, because a rebuild replaces them. It reuses
`game::MoneyCounter` (`common/MoneyCounter.h`) and `common/LedgerText.h`.

State the original keeps in class statics lives in `choc2::Session` (`choc2/Session.h`), owned by `Choc2World` and destroyed
after the ledgers: the remembered tab, the quest bar's Y, one shared money counter, hint and mode. The money is stepped once per
tick however many ledgers are alive (deliberate deviation: the original steps once per live ledger, so a dialog ledger over the
port's doubles the speed; `choc2-ledger-binary.md` 12.2). The natives find ledgers by walking the modal chain; with none they do
nothing (`SetLedgerContents` still records the tab).

| Native | Effect | Basis |
|---|---|---|
| (build) | `data` kept as a `LuaRef`; the remembered tab is shown, or **Factories** when none (`ResetLedger` clears it after every load); the quest bar goes to `hideQuestY` on first build and slides when there is a hint; a 100 ms task steps the money and the quest bar | fact (binary) `PostChildrenInit` order (`choc2-ledger-binary.md` 2); the 100 ms rate is inferred from `TRedrawTask`'s rate 10 (the delay semantics are not decoded) |
| `SetLedgerContents(tab)` | records the tab; shows only that body and **rebuilds** it; sets `btn_<tab>` on and the others off | inference (C1 `UtilSetLedgerTab`) |
| `UpdateLedgerContents(tab)` | rebuilds the body only if it is the one shown | inference |
| `SetLedgerMode(m)` | `"port"` shows `to_map` and its label and hides `to_port`; `"map"` reverses; on every ledger in the chain | inference (C3 `portview`/`mapview`) |
| `ResetLedger(money)` | the shared counter jumps to the amount, the quest line clears, the remembered tab and bar Y reset | fact (binary) |
| `UpdateBadge(t)` | targets `t.money` with a fixed step `max(1, gap / 7)` per tick and writes the money, date, rank and score Texts. It does **not** rebuild the badge: that is `UpdateDynamicContent("badge")` | fact (binary) 4 |
| `SetQuestHint(s)` | sets `quest_text` to `"#" .. s` (the literal-label convention); the bar slides to `showQuestY` (non-empty) or `hideQuestY`, 26 px per 100 ms tick | fact (binary) 3 for the step; the slide is 3 ticks |
| `bsgGetMaxScroll(name)` | the `Text`'s first line of the last page (0 when all fit); -1 for a missing window or a non-`Text` | fact (binary) 12.1 |
| `bsgSetStartLine(name, n)` | stores `n` unchanged as the 0-based first line (the Lua passes a 1-based `messageTop`; the leading `#` line hides at the top, and the last page is one line short, as in the original) | fact (binary) 12.1 |

The score line is `GetString("formatScore", bsgDollars(t.score))`, or `""` at rank 0 (fact (binary) 4). `TextWindow` has a start
line and `maxStartLine()` (`src/ui/widgets/TextWidgets.h`); Chocolatier 2 does not clamp the start line when drawing. Inference
(binary 12.4, unknown): whether a lone `\n` makes an empty first line in the original's `RenderText`.

### 2.4 `Character`

**`choc2::Character : ui::BSGWindow`** (`choc2/Character.{h,cpp}`). It reads `char.name`, `char.mood` (default 50) and the
character's `appearance` (the pictures are `characters/<appearance>`, falling back to the name; fact (binary) 3.1). It draws the
body (`characters/<n>.png`, 172 x 240) unscaled at the window's origin, then the face PNG with its top-left at the body's
top-left (exact for 34 of 45 characters, within 2 px for most of the rest; PL 8.1). Faces are used only when all three exist.

- **Mood faces** (fact (binary) 3.2): displeased when `mood <= 25`, neutral below 50, else pleased (so a character at its
  default mood 50 shows the pleased face; `kHappy` 70 is never read). The swap is instant: no cross-fade.
  `SetCharacterMood(name, m)` sets the mood of every live `choc2::Character` with that name. `game::MoodFade`
  (`common/MoodFade.h`, 200 ms on 50 ms ticks) belongs to Chocolatier 3's `CharWindow` and is not used here.
- `CharWindow` itself is not subclassed: its sheet frames and `name`/`happiness` keys are Chocolatier 3 specific.
- **Unknown (binary doc):** the face for the roughly 65 body-only characters. The original asks for `temp/error` with no check;
  the engine draws no face.

### 2.5 `Sprite` and `AnimationWindow`

- **`choc2::SpriteWindow : ui::Window`** owns one `game::Sprite` and exposes it through `spriteRoot()` for dumps. `image` is a
  texture, or a sheet if it ends in `.xml`; `rotation` is in degrees, `tint` a colour, `scale` a number. The registration point
  (a texture's centre, a sheet's frame point) sits at (x,y) in the parent, and the window's rectangle is the image's bounds
  around it. A sheet's `DoAnim` plays, ticked by a 10 ms window animation (enough: the scripts time themselves from `nowMs`).
- **`AnimationWindow`** is a plain container that records `fps`; its `Sprite` children animate themselves. It is not
  `LayeredWindow` (PL 4.2).

### 2.6 Map and travel

- **`choc2::Map`** is a container. The Lua builds the icons (Rollovers with `Sprite`s), labels and click closures. The `ports`,
  `routes` and `current` keys the Lua puts in front of it with `AppendStyle` are parsed only for dumps and tests; nothing is drawn
  from them. `colormask` shows `image/map_color`'s pixels through the alpha of the union of the visited ports'
  `ports/map_mask_<port>` images (fact (binary) `choc2-map-binary.md` 1.1); the list and the picture are rebuilt once per
  `ResetColorMap`, when the next `Map` is made. Inference: the blend used when several masks are drawn into one target, and the
  picture's vertical orientation. Escape runs `DoModal('ui/pausedialog.lua')` and every key is consumed; losing the app focus
  while a trip runs opens the same dialog (fact (binary); the meaning of messages 1000/1001 is an inference).
- **`choc2::MapTravel : ui::Window`** reads `xOffset`/`yOffset` and owns a root `Sprite` with the vehicle and the trail dots.
  - `InitiateTravel(leg)` builds the curve over `leg[1..n]` (3n+1 control points) plus offsets, sets `weeks`, loads the vehicle
    from `image/icon_<leg.type>` (a missing one is logged once and no vehicle is drawn). Dots of an earlier leg stay on the map.
    `InitiateTravel` is also a Chocolatier 3 native with another arity, so `api-choc2.tsv` has a row that overrides it.
  - `TravelOneWeek(type)` reloads the icon if `type` differs and moves the vehicle from `week/weeks` to `(week+1)/weeks` of the
    curve over `WeekMover::kWeekMs = 2000` ms (fact (binary) 1.2) on a 40 ms tick counted on the map modal's clock, so a dialog
    opened mid-week holds the train. The vehicle is shown only while a week runs, mirrored while the tangent points right (art
    faces left), never rotated; a trail dot is dropped every 160 ms of leg time (`choc2/TravelWeek.{h,cpp}`, pure so it is
    testable without a window). `choc2/TravelSounds.*` plays the per-type ambient loop and one-shot (fact (binary) 1.4).
  - `StillTravelling()` is true while a week animates, and **false when no `MapTravel` is in the chain**, so the Lua loop cannot
    hang if the map dies. All three natives find the `MapTravel` by walking the modal chain on each call.
  - Unknown (binary doc): the vehicle's initial position argument in `TravelOneWeek`; whether `TCurve::GetPoint` parameterises
    by arc length (the engine's `Curve::at` does).
- The shared `Travel` class is not reused: its duration comes from an event list and its clock subtracts pauses, which
  Chocolatier 2 has neither of.

### 2.7 Production animation and the rest

`StartProductionAnim`/`StopProductionAnim` set a flag that makes `FactoryStatus` pictures bob and rotate during travel (fact
(binary) 6; see the minigames doc 2.4). `ResetColorMap` is real (2.6).

## 3. Ownership and lifetimes

- **`choc2::Port`**: `pending_` is plain data until `readScene`; after that `PortWindow`'s root sprite owns everything.
  `BuildingDef::command` is a `LuaRef` owned by the window, like a button's command; it dies with the window, before the Lua
  state (`docs/design/script-host.md` section 2). The popup is a child of the top modal, held only by `WindowId`.
- **`DynamicWindow` children** are ordinary children. A rebuild from a child's own command is safe because deletion is deferred
  (`Widgets.cpp`); the `building_` guard covers the one unsafe case.
- **`choc2::Ledger`** holds a `LuaRef` to `data` and `WindowId`s only, and finds children by name on each use. `Session` holds
  plain values and is destroyed after the ledgers (`SessionHolder` is a base declared before `World`).
- **`MapTravel`** owns its sprites; natives reach it by walking the chain, never through a stored pointer.
- **`Character` and `SpriteWindow`** own their textures and sprite through `shared_ptr` and `unique_ptr`; their animations are
  window animations, stopped when the window dies.

Per frame there is nothing new beyond `World::tick` driving `choc2::Port::frame` (as for `PortWindow`) and the window animations
(the ledger tick, the port task, `SpriteWindow` scripts, the `MapTravel` week).

## 4. Tests

**Unit** (`tests/test_choc2.cpp`, `test_choc2_ledger.cpp`, `test_choc2_travel.cpp`, and `test_sprite.cpp` for `PathSprite`):
- The adapter (`fromAddSprite`): a cloud strip, a bouncing car with a sheet, perspective keys on moving and static sprites and on
  buildings, a path sprite without `time` (default plus one note), `.LOGO`, a table with `command` and `contents`.
- `pathPhase` (four phases with a hold; `hold = 0` is the old formula) and the Chocolatier 2 timing (bounce round trip in one
  `time`, hold at the start, art mirrored when moving right).
- A Lua `Port{ function() bsgAddSprite{...} end }`: the expected buildings and paths; a click runs the closure; hover opens one
  popup and moving off closes it; the pulse runs until `StopPulsing`; pulse arithmetic and the white silhouette; popups cap a
  Rectangle's fade at 0.8; `bsgAddSprite` outside a Port logs and does not crash; `StopPulsing` and `ResetColorMap` are silent
  without a Port or Map.
- `Character`: the face for a mood, the instant swap, a character without the three faces, `appearance`.
- `DynamicWindow`: builds at init but not with `init=false`; a rebuild replaces the children; a rebuild from a child's own
  command is safe; a nested rebuild is ignored and logged; `UpdateDynamicContent` reaches windows sharing a name.
- The ledger: one tab shown and the radio set; `UpdateLedgerContents` rebuilds only the shown tab; the tab persists into a
  second `Ledger`; `ResetLedger`; the badge counter reaches its target; the quest slide; `SetLedgerMode`; start line and max.
  `MoneyCounter` gives the same sequence as the old `LedgerWindow` code (Chocolatier 3's `test_ledger.cpp` proves that).
- Travel: `TravelOneWeek` keeps `StillTravelling` true for 2000 ms, the vehicle ends at `curve.at((k+1)/weeks)`, the dots follow
  the 160 ms rule, `StillTravelling` with no `MapTravel` is false. `bsgDate`/`bsgShortDate` per the binary.

**Scenarios** (assets-gated): `check_choc2_story.py` (the intro into `ui/portview.lua`; the `chartalk` dialog shows a
`Character`; the `Port` detail shows the expected `buildings=N` and `paths>0`; hover shows a building's popup; click opens its
dialog; screenshots at two times differ in the sky band), `check_choc2_ledger.py` (the welcome message, the badge texts, the
Factories tab, the ledger screenshot) and `check_choc2_travel.py` (`to_map`, a one-week neighbour at rank 0 where there is no
encounter, `--seed` fixed, the destination banner text, a mid-trip screenshot). The allow-lists in these scripts name only the
audio natives and cursors still unimplemented.

**Chocolatier 3 unchanged.** A change to a shared class runs `unit`, `stubs_up_to_date`, `vanilla_mainmenu`,
`reforged_port_zurich`, `reforged_ledger`, `reforged_endtoend` and `reforged_focus` plus `choc2_mainmenu` and `choc2_story`;
their screenshots stay byte-identical. The full suite is not run per change.

## 5. Open points

- **Inferred behaviour with no binary evidence:** the 100 ms ledger rate (inferred from the task's rate), the pulse's look, the
  popup placement with non-zero `ui.xOffset` (the original uses `labelx/labely` raw; the binary doc leaves it unknown), the colour-mask
  blend and orientation, the face for characters without face files, and the vehicle's initial position. A reference capture of
  the original would settle them.
- **Shared-class edits:** `PortWindow`, `PathSprite`, `TextWindow`, `RolloverWindow`, `CharWindow` and `LedgerWindow` each took
  a default-preserving field or a mechanical extraction; the Reforged scenarios above guard them.
- **Not implemented:** `bsgSetGlobalLabel` (dev only, arguments unknown) and texture filtering mode 1 of `Port::Draw` (meaning
  not decoded).
