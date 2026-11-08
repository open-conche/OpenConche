# Vanilla (Windows v1.0 `assets.pfp`) against Reforged: what differed on first boot

Data: the Chocolatier 3 Windows install's `assets/` (`assets.pfp` + loose `strings.xml`, `settings.xml`, `credits.txt`, `splash/`, `cadi/`). Run with
`openconche --headless --assets <windows-install>/assets ...`. Test: CTest `vanilla_mainmenu` (`tests/check_vanilla_mainmenu.py`,
registered only when the assets exist).

The vanilla reference is the retail Windows install (Lua compiled 2009-01-27, `hiscoreanon` 1).

## How far it gets

The whole loop runs. Survey: 19 headless runs, every step reached with no Lua error (opening dialogs, inventory/recipes/quest
log/awards/pause, chocolate minigame via the tutorial, shop Sell All, station, market register, map and travel to Douala, Save & Quit and
resume with an identical save table, crates via `lua DisplayDialog`, in-game Options). Vanilla has no Free Play or game-over globals.
The first boot path, with no engine-side stubs beyond the usual sound/movie ones:

1. Boot: the game's bytecode `ui/mainloop.lua` runs, `ui/mainmenu.lua` is shown at frame 14 (`expect-window ui/mainmenu.lua`).
   The title art reads "Chocolatier: Decadence by Design", the menu buttons are Start Game / Help / High Scores / Options / Save & Quit.
2. Start Game (the big button, centre at 400,345), name entry (`ui/ui_entername.lua`), Return: the port view (`ui/portview.lua`,
   Zurich, 1946) with Alex Fletcher's opening dialog. There is no difficulty dialog in vanilla v1.0 (Reforged's `ui/ui_difficulty.lua`
   comes after the name there).
3. Unimplemented natives logged: `StartMovie` (the vanilla intro movie, skipped as in the plan), `SoundEvent`, `ButtonSound`. Nothing else.

Covered since by `vanilla_factory` / `vanilla_endtoend` (`tests/check_vanilla_endtoend.py`): the opening quest dialogs, the factory and the chocolate minigame (autoshoot scores 74), shop sell-all (74 x $85), the station's $5,000, a market purchase, the flight to Douala, the pause menu's Menu / Main Menu / Save & Quit, and the resume with an identical save table. High Scores is done: see below. Not tried: Awards (FireworksWindow, in progress) and the other minigames. Differences seen in play are listed after the table.

## Differences hit

| Area | Difference | What was done |
|---|---|---|
| XML | 89 XML entries inside the pfp (all the data and sprite XML, e.g. `items/*.xml`, `ports/ports.xml`, `quests/quests.xml`, `ports/animations/*.xml`, `characters/*.xml`, `controls/rollover.xml`) and the loose `settings.xml` are `ZXML`; the five `quests/*_strings.xml` tables and the loose `strings.xml` are plain. Format: `ZXML`, u32 compressed length, u32 raw length, zlib stream. Reforged has none. Symptom: `LoadIngredients: 0`, then `portroutes.lua:8: attempt to index global 'baghdad'` because `ports/ports.xml` listed no ports. | `assets::Xml::parse` (bytes and text overloads) now inflates it with stb's zlib decoder (already in the tree) into a buffer of the declared raw length (capped at 64 MB), as the original's `uncompress` does, and fails with `ZXML: bad zlib data` on a mismatch. Unit test in `tests/test_pfp.cpp`. `strings.xml` is plain text, not ZXML. |
| Splash | `splash/distributor_logo` does not exist as a texture in vanilla (the installer has `.swf` splashes, not loaded). | Logged as `[gfx] texture ... not found`, harmless; logos are skipped per the plan. |
| Main menu layout | Vanilla's story button is the large centre one (rect 325,270,150,150), Reforged's scripts click at x=310. | Test clicks 400,345. No engine change. |
| Flow | No difficulty dialog after name entry. | Test goes straight to `portview.lua`. |
| Bytecode | 32-bit chunks with the game's opcode numbering. | Already done in commit 194db56 (`docs/re/pfp.md`). |

## Differences seen in play

- No difficulty dialog; Start Game is at 400,345. Menu buttons are only Start Game / Change Player / Help / High Scores / Options / Save & Quit; no Free Play.
- Story starts with $0, 100 sugar + 100 cacao, rank 1, `Player.mode` nil (Reforged starts with $20,000). Alex's $5,000 comes from the station quest; the first goal is $10,000.
- The tutorial gates buildings with hint dialogs; the factory is already owned and `ui_factory.lua` opens straight from the tutorial.
- Shop has "Sell All" only (no product register, Haggle hidden).
- Market register +1/+10/+100/+1000 at x=578, y=206/231/256/281; Buy at 474,337.
- Badge buttons: Map 123,415, Menu 63,553, Recipes 56,382, Inventory 124,360, Quests 193,382, Awards 206,553.
- Pause menu Options / High Scores / Help / Main Menu / Resume at x=400, y=209/254/299/344/389, with no Save. Save & Quit is on the main menu (683,417; confirm Yes 313,396). Main Menu from pause leaves the port modal under the title screen.
- Resume opens on the map.
- Quest dialogs nest up to 3 `ui_quest_offer` modals, closed together.
- A mid-flight "factory stalled" `ui_generic.lua` dialog (Ok 400,401).
- Douala trip: 1 week, $409.
- Same Lua file set as Reforged.

Harmless log lines on these paths: Awards logs `FireworksWindow`
(being implemented), crates log the sparkle no-op (assets missing from Chocolatier 3, `particles.md`), `splash/distributor_logo` missing,
Awards `medal_image` empty when there are no medals.

## Things that did not differ (checked)

- No missing natives beyond the stubs above; no prelude change was needed. `lua/` is untouched.
- Strings: the loose vanilla `strings.xml` loads through the same table code; the menu and the opening dialog render their text.
- No Lua 5.0 `string.match` or other missing-library failures surfaced in the boot, menu and new-game path (the game's own chunks are
  written for 5.0).
- High Scores (main menu button, 400,475) works with the vanilla data and the same native `HiscoreWindow`: the vanilla `ui/hiscore.lua`
  needed no change. Its right panels are `rightpanel` and `rightpanelsmall` (hidden by the engine), the headers `local` / `global`,
  and it has the same row children (`name1..10` and so on). `topplayers` does not exist in the vanilla screen either. Checked by
  `tests/check_hiscore.py --dataset vanilla`. Reforged is different only in that its main menu button is the Community hub
  (whose High Scores button runs `DisplayDialog "ui/hiscore.lua"`) and that its screen re-draws the rows from `GetLabel`.
- Deliberate deviation: under local-only (the engine's default, `hiscorelocal` forced on) the original hides the `local` header and shows
  `topplayers`, which the vanilla screen does not have, so it shows no title at all. We show "Local High Scores" instead. With
  `--config hiscorelocal=0` the right panels follow hiscore.md 2.5; `view` ends in state 5 with the error text (its own flag is shown, but
  it sits inside the hidden `rightpanel`, the original's quirk), and `tests/check_hiscore.py` covers that run.
- Online, cookbook and cloud code was not reached on this path, so nothing needed stubbing yet. When the online screens are
  opened, apply the plan's rule (stub to a safe value, never ship a bridge).

## Remaining

- (Fixed: `Window` is now a real plain built-in and logs nothing.) `[unimplemented-type] Window` was logged once at menu creation (`nativeGenericCreate` with an empty `typename`, i.e. a plain container window created through the generic native, which maps to a placeholder; it is the `Window{name="buttons"}` container of `mainmenu.lua`; Reforged logs the same line, the window exists in the dump with all its children, so it is harmless noise from `WindowNatives.cpp`).
- The vanilla port Lua files (`ports/<port>/<port>.lua`, `ports/portroutes.lua`) load fine, but their content was not compared with Reforged.
