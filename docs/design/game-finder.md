# The game finder: picking a game

The engine runs several games (Chocolatier, Chocolatier 2, Chocolatier 3 and the Chocolatier 3 community edition,
"Reforged"), but ships none of their assets. A user who just got the binary needs a way to say where their games are
without reading a README for `--assets <folder>`. The finder is that way: a launcher screen on first launch, and the
same screen as an in-game switcher afterwards.

The engine's name is **OpenConche**: the conche is the machine that kneads raw chocolate into something smooth, which
is an honest metaphor for what the engine does to the game data, and it is not a Chocolatier game's name, so engine
folders can never be confused with the games' own. The name applies to everything engine-level: the binary, the
config folder, the launcher's window and message-box titles. The games' own window titles are the games'.

## 1. Launch policy: what happens when no `--assets` is passed

An explicit `--assets` always wins. Otherwise `App::run` goes, in order:

1. `--finder` forces the finder even when a valid saved choice exists (the CLI way to change game). If `--assets`
   was also passed, the finder's successful pick replaces it.
2. The library's `last` entry (`games.cfg`, section 3). It is valid when the saved game variant claims the saved
   folder (re-resolved from `dir`, falling back to the saved `assets` path). A stale entry is ignored; the others stay.
3. The finder window, as the launcher. Closing it or its Exit button ends the process.

Do we always show the chooser? No. A user with one game boots straight into it, and the chooser appears only when
there is nothing valid to boot. The zero-CLI way back is the in-game switcher (section 2a), so "I installed a second
game" needs no argument, no config editing and no relaunch ritual.

Automated runs (`isAutomatedRun`: `--headless`, `--script`, `--frames`, `--screenshot`, `--dump-windows`,
`--log-unimplemented`, `--no-assets`, `--pick-game`) skip both the library and the finder. They never read the
machine's home folder, so test runs stay deterministic, and a CI run never opens a dialog. Without assets they get a
warning line and the test scene. `--finder` in an automated run is a failure (exit 1), not a quiet exit.

The test scene (the colour-box screen of the no-script-host loop) is a test fixture, not a destination. It is reached
only by asking for it (`--no-assets`, `--test-scene`, `--test-text`; the headless CTests `check_scene`, `check_text`
and `check_cli` run on them) or by an automated run without assets. An interactive run whose `--assets` folder mounts
but holds no game (the registry's warned last-resort fallback claims it for a module whose mainloop is absent) quits
at once with exit 3 and "no game in '<folder>' (ui/mainloop.lua is missing)".

There is no developer default asset search: no config file means the launcher, with nothing in between, including
for runs from inside this repository. The remaining choc3-shaped defaults are listed in section 5.

## 2. The finder window

A native OS window, 800x600 logical, **painted entirely at runtime**. No PNG, no generated table and no data file is
embedded: the binary ships alone and the launcher appears before any game asset exists. It is drawn with the engine's
built-in 5x7 pixel font (`builtinFont`, `src/gfx/text/Font.cpp`, authored in this repository and turned into an
in-memory `.mvec` at runtime; no font file is shipped and no licence beyond the repository's own applies) through a
`FontCache` with no `AssetFS`. No game font is touched and no asset is mounted. The built-in font has no parentheses
or pipes, so labels read "Chocolatier 3 - Reforged" and lists use "/" separators. Typography is sizes and colours only
(no bold, italics or second face).

The screen is painted at run time rather than baked into a PNG: its words are live text, the art is only a
backdrop and button plates, and the same constants draw, hover and hit-test every control, so art and hit zones
cannot disagree.

Layout, top to bottom, using a solid backdrop (28,15,10), a one-step-lighter card fill, and grey buttons with a
one-pixel lighter border:

- Heading "SELECT A GAME", then one **card per installed game**, 0 to 5 cards in `kFinderVariants` order (680x66 at
  x=60, y = 106 + i*72). Only games with a library entry appear; there are no placeholders. A card shows the game
  name (16pt, bright), the configured directory beneath it (10pt, dim, elided to the 480-wide box by keeping the
  path's tail, since the end of a path carries the information; the full path shows in the footer while the card is
  hovered, the screen's one tooltip), a 128x32 **Play** button on the right, and a small X (22x22, inverted fill on
  hover) that unregisters the game. The X only erases the games.cfg entry (and clears `last` if it named that game);
  it never changes the disk. The list is rebuilt in place.
- Play is gold when the entry still validates (`bootFromLibrary`): the click boots it, no dialog. A white Play whose
  entry went stale asks, through the folder dialog, where the game went (the re-locate flow). Hovered words are
  underlined with the layout engine's own `<u>` markup.
- One global **+ ADD GAME** button under the list (or under "No games added yet." when the list is empty). It opens
  the OS folder picker, then runs detection (below) over the picked folder for each variant in order; the first match
  names the game, so the user never has to say which game they picked. Adding only registers: `last` is untouched
  and nothing launches, so the user keeps choosing (Play, another add, or Exit). A duplicate (same variant) is
  refused with a message, except that the same game from a different folder re-points the entry. A folder that
  matches nothing gets "No supported game was found in '<folder>'."
- An information-only line near the foot, "SUPPORTED GAMES: Chocolatier 1, 2 and 3 / Chocolatier 3 -
  Reforged / The Great Chocolate Chase", so all five stay visible (it must fit one 680 px line; a unit test checks).
- A contextual footer, which re-asks the library (it re-validates `last`) rather than trusting the stored line.
  With no playable chosen game: "Select a game to get started. Your choice will be remembered and launched
  automatically next time." With one: "[game] will launch automatically next time. Press Ctrl+Shift+G or click the
  in-game badge to choose another game."
- A top-right button (660,30 108x28): "Exit" in the standalone launcher (the process ends; closing the window does the
  same), "Back to game" in the in-game switcher (the finder closes and the game keeps playing). It fires on the mouse
  release, so the game under the switcher never receives a MouseUp for a press it did not see.

One hit-test helper serves hover and click, so the two cannot disagree.

### Variants

| Variant id | Module | The folder the user then picks |
|---|---|---|
| `choc1` | choc1 | the Chocolatier install (its `assets/` is found inside) |
| `choc2` | choc2 | the Chocolatier 2 install |
| `choc3` | choc3, vanilla | either Chocolatier 3 tree without `community/account.lua` |
| `choc3-reforged` | choc3, community | a `Chocolatier-Decadence-Reforged` checkout |
| `greatcc` | greatcc | the Great Chocolate Chase install (Windows) or `.app` bundle (Mac) |

### Resolving and validating a picked folder

The folder picker is `SDL_ShowOpenFolderDialog` (non-blocking, callback in the event loop; a scope guard calls
`Window::cancelFolderPick` on every return, and the callback writes through a weak pointer to shared state, so a
dialog that outlives the screen writes into nothing). The engine finds the assets root itself, so the user picks
whatever folder they think of as "the game":

```
resolveAssetCandidates(picked), in order, first that validates wins:
  1. picked/assets                        Windows/Linux installs, a Reforged checkout
  2. picked/Contents/Resources/assets     the Mac .app bundle, picked directly
  3. picked/*(one level)/assets           picking the folder that holds the install
  4. picked/*(one level)/Contents/Resources/assets      ditto, Mac
  5. picked itself                        the assets folder picked directly
```

Children are visited sorted by name (so first-match detection is deterministic), dot folders are skipped, and names
convert through `pathToUtf8`. Each candidate is validated by mounting a throwaway probe `AssetFS` and running the
registry's own `detectGame` on it. The probe covers the root and the `data`, `ui`, `community` and `scripts` folders,
plus the index of `assets.pfp` without its data and no audio containers. It is built once per candidate, not once per
variant:

- the detected module must be the variant's module;
- a `choc3` variant must match the tree's community marker (`community/account.lua` means Reforged, exactly
  `Choc3Module::defaultUserLeaf`'s rule, so the save folder split follows): vanilla mounts `Chocolatier3-Vanilla`,
  Reforged mounts `Chocolatier3`;
- the tree must contain the module's mainloop script (`GameProfile::mainloop`), so a bare folder of loose art cannot
  half-boot.

A failed pick shows the OS message box with the reason (detected game vs. variant, missing scripts, no candidates)
and returns to the screen. Cancel in the folder picker returns to the screen. A Play click on a valid entry sets
`assetsDir` and `game` alone (every other option, such as `--scale`, `--userdir`, `--seed`, stays) and continues the
boot with the resolved directory; the normal mount's `detectGame` then confirms it. Playing also records `last`.
`FinderOutcome` (Picked, Cancelled, Exit, Failed) documents this contract.

### 2a. The game switcher (Ctrl+Shift+G, in game) and the badge

The switcher is the same finder window opened inside the running game's window and renderer
(`FinderHost{window, renderer, uploader}`). It must not create a second `SdlRenderer`: the constructor would steal
the window's flush hook from the game's renderer and the destructor would clear it, leaving the game presenting
stale batches. A click on Play with a valid entry boots that game in one click; a stale entry falls back to the
folder dialog, and ADD GAME registers a newly installed game.

Entry points, all one action (`openSwitcher`), handled in `App` before any game window sees the event:

- The chord: Ctrl or Cmd (`InputEvent::gui`, SDL's KMOD_GUI) plus Shift plus the key named `"G"` (SDL names letter
  keys in capitals). It never reaches the game, and key repeats are ignored.
- The badge: a 320x40 strip at the top right (kBadgeRect 466,14; 14 logical px off the corner) reading "OpenConche:
  click to switch the game, or press Ctrl+Shift+G any time" in two centred 14pt lines. It is drawn after the window
  tree, over whatever the game drew, in the built-in font through a `FontCache` with no `AssetFS`, so it renders
  identically over every game. It is a welcome strip, not permanent: drawn and clickable for the first 10 s of the
  run (wall clock from the loop's start, after boot; ten seconds so the sentence can be read), then gone. The chord
  remains. The click is swallowed before the game sees it.
- Headless runs never draw the badge, but its hit test stays active so `check_switcher` can pin the chain. The
  switcher refuses ("the finder needs a display"), and the click then falls through to the game, so scripted
  playtests can click real UI under the badge rect (the Reforged kitchen and devmenu playtests do).

On a successful pick `App` ends the run through the normal exit path, including the game's exit save, and `main()`
boots the chosen game in the same process (`App::takeReboot`: a full engine rebuild in a fresh `App`, so no state
leaks across games). `userDir` is reset so the next game resolves its own default user folder, unless
`--userdir` was given (`Options::userDirExplicit`), which survives the switch.

## 3. The game library (OpenConche/games.cfg)

The engine's preference folder is `platform::defaultUserDir("OpenConche")`: the same three locations SDL's
`SDL_GetPrefPath` would pick (%APPDATA%, .local/share, ~/Library/Application Support). The one-level helper is kept
because SDL's (org, app) pair would add a nesting level, and the per-game user folders (`Chocolatier3` and friends,
named after the games so existing profiles stay found) already use it.

`games.cfg` is a library, not a last pick: one `last = <variant>` line naming the game to boot, and a `dir`/`assets`
pair per game the finder has validated.

```
last = choc3
choc1.dir = /Applications/Chocolatier          <- what the folder picker returned
choc1.assets = .../Chocolatier/assets          <- the tree the engine resolved from it
choc3.dir = ...                                 <- every registered game stays
choc3.assets = ...
```

`dir` is saved so a moved install is re-resolved (candidates again) on the next launch; `assets` is the direct
fallback. A stale `last` entry (folder gone, re-resolution unplayable) is ignored and the launcher is shown, which
keeps the other entries. The parser takes `key = value` or `key=value` lines, tolerates CRLF, comments, lines without
`=` and duplicates, and keeps UTF-8 paths; a missing file loads as an empty library, and a `last` naming a game with no entry is logged and ignored. The library is
engine-level, not per-game, is only written by the interactive finder, and is not affected by `--userdir` (which
overrides the game user folder only).

## 4. Headless / scripted picks

`--finder --pick-game <variant> --pick-dir <folder>` is the finder without the screen: resolve, validate, log
(`[finder] variant <v>, assets <dir>` or `[finder] <why>`), and exit 0 (playable) or 3 (nothing playable for the
variant). An unknown variant is a usage error (exit 2). It never writes the library, so an automated run cannot
persist a choice.

`--add-game <folder>` is ADD GAME without the screen, for systems with no folder dialog (SDL3 on Linux shows one only
through the desktop portal or `zenity`; without either, the finder reports the reason in a message box and the
`[finder] no folder dialog` log line, and names this option). It resolves and detects exactly as the button does,
stores the folder as an absolute path, registers the game in `games.cfg` and exits: 0 added or already listed, 3 no
supported game found, 1 the list could not be saved. `last` is untouched. Unlike `--pick-game` it does write the
library, so it is not an automated run and needs no `--userdir`.

## 5. Remaining choc3 defaults

- `Registry::detectGame`'s two last-resort choc3 fallbacks (unknown `settings.xml` game value: warn and assume; no
  `settings.xml`: the claim pass, then choc3) each log their reason, and `--game` overrides. They are the boot
  chain's only silent choc3 default.
- `Args`' `choc3Default` for `--import-prefs` is correct by definition (the import is a Chocolatier 3 feature).
- `GameProfile::mainloop` defaulting to `ui/mainloop.lua` is an SDK default, not a choc3 one (an SDK-3.4 tree
  overrides it; every shipped Chocolatier uses it).
- `App`'s `modules().front()` pre-detection module is the registry's first registered module, commented as such.

## 6. Tests

- `tests/test_finder.cpp` (unit): the variant table, `resolveAssetCandidates` layouts and order,
  `validateAssetsForVariant` against the real trees, `bootFromLibrary` with pre-filled options and the re-pick
  fallbacks, library save/load with every game kept and a moved install, and the parser corners.
- `tests/check_finder.py` (CTest `finder`, present only where the original installs exist; read only): the
  `--pick-game`/`--pick-dir` path against the real game trees (each variant's acceptance and refusal, the community
  marker split, a missing folder, an unknown variant), plus a planted `games.cfg` that an automated run must ignore
  (test scene, no `[finder] saved choice`) and `--finder --headless` exiting 1.
- `tests/check_switcher.py` (CTest `switcher`): boots vanilla to the menu headless, then drives the chord, its Cmd
  variant and a click inside the badge rect, each reaching the switcher and logging the needs-a-display guard.
- The GUI loop itself (folder dialog, hover, cards) is checked by hand; the dialog cannot be driven headless. A
  windowed `--screenshot` run can pixel-probe the badge (dark strip, white text inside the rect).

## Limits

- No drag-and-drop of a folder onto the window.
- There is no dedicated "game moved" repair dialog: a stale entry silently drops out of the launch policy, and
  its card's Play asks for the folder again.
