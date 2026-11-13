# OpenConche — notes for coding agents and operators

This engine runs the shipped script code and assets of **Chocolatier 1**, **Chocolatier 2: Secret Ingredients** and
**Chocolatier 3: Decadence by Design** unchanged: it reimplements the platform *under* the games — the window
system, the embedded Lua 5.0.2 host, the graphics, the audio and the natives the scripts call. Fidelity to the
originals' observable behaviour is the project's rule: read `docs/design/` and
`docs/re/` (the reverse-engineering facts, every statement marked
**fact**, **inference** or **unknown**) before changing behaviour anything depends on.

The top-level `README.md` says what the project is; `BUILDING.md` covers building and testing. This file is the operator's manual: how to build, run,
script, play-test and diagnose the engine, and what to know before debugging it.

## Ground rules

- **The games' own files are read-only.** Never write into a game install, an original game's user folder
  (`%APPDATA%\PlayFirst`, `~/.local/share`, `~/Library/Application Support`) or a Reforged checkout; all engine
  state goes through `--userdir`.
- **Automated runs never touch a real profile.** The scripting flags (`--headless`, `--script`, `--run`,
  `--screenshot`) refuse the default user folders; always pass `--userdir <scratch dir>`.
- **No game data in git.** Decompiled output and raw extractions are not committed; what belongs in the tree is
  written documentation only (`docs/re/`, tagged as above). Tests use small synthetic fixtures
  (`tests/assets/`), never new copies of shipped data.
- Never force-push.

## Build and test

    cmake -S . -B build -G Ninja && cmake --build build
    ctest --test-dir build -j4        # the full suite; the reforged_* walks are the long ones
    ./build/engine_tests              # unit tests alone; -tc="*name*" selects a case
    cmake --build build --target openconche

The first configure needs network (SDL 3.x, Lua 5.0.2, doctest and pugixml are fetched). Lua 5.0.2 is pinned by
SHA-256 `a6c85d85f912e1c321723084389d63dee7660b81b8292452b190ea7190dd73bc` in `CMakeLists.txt`. Build options:
`CHOCO_GAME_CHOC1` / `CHOCO_GAME_CHOC2` (both default ON) build the per-game modules; with one off, the
remaining build answers the missing game's `--game` with a one-line refusal. `CHOCO_GAME_GREATCC` (The Great Chocolate Chase, default ON; `docs/design/greatcc.md`) builds `src/games/greatcc/`; its tests read `CHOCO_GREATCC_ASSETS` (the Windows install's `assets/`) and `CHOCO_GREATCC_MAC_ASSETS` (the Mac bundle's); both are empty by default and the tests are skipped without them. `fixbugs = 0` in `games.cfg` (or `--config fixbugs=0`) switches that game's original-bug fixes off; automated runs never read `games.cfg` and default to on. `-DCHOCO_WARNINGS_AS_ERRORS=OFF`
(default ON, `-Wall -Wextra` / `/W4`). The suite's game tests point at local game copies through
`CHOCO_VANILLA_ASSETS` / `CHOCO_REFORGED_ASSETS` / `CHOCO_CHOC2_ASSETS` / `CHOCO_CHOC1_ASSETS`
(`CMakeLists.txt`); the tests whose assets are missing are skipped.

Release archives: `-DCHOCO_RELEASE=ON` (`cmake/Distribution.cmake`) and the `dist` target zip the executable into
`build/release/dist/`, one archive per platform; the version is `project(... VERSION ...)` in `CMakeLists.txt` and
`openconche --version` prints it. BUILDING.md has the commands.

Web build (`cmake/Web.cmake`): with Emscripten installed, `emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
-DCHOCO_WEB_GAMES="choc3=<assets dir>;choc2=<assets dir>"` then `cmake --build build-web --target web` makes `build-web/web/`,
a static site: the engine as WebAssembly, `packaging/web/index.html` and one data package per listed game (Emscripten's
file_packager; delete `<id>.data` to repack after the assets change). `index.html` without a query is a chooser listing the packaged games; `index.html?game=<id>` boots that game with
`--assets /assets/<id> --userdir /user/<id>`; the user folder is an IDBFS mount in the browser's IndexedDB, flushed after
every write (`platform::syncUserDir`), and the run's end goes back to the chooser. The browser owns the frame loop (`App.cpp`'s
`Session::step` through `emscripten_set_main_loop`); there are no threads (the audio prefetch worker is skipped), so any
static file server will do. The in-game switcher (badge and chord) is off there: the chooser is the way between games. A tablet gets its on-screen keyboard from a hidden input the page focuses on a tap while the
engine wants text (`chocoTextInput`, fed back through `choco_web_text` / `choco_web_key` in `platform/Window.cpp`).
No tests run in that configuration; the native build is the one the suite covers.

Screenshots are written by SDL3's own `SDL_SavePNG`. `third_party/stb_image.h` (v2.30, public domain, PNG and
JPEG only, from nothings/stb commit 31c1ad37456438565541f4919958214b6e762fb4) decodes textures and hit masks.

## Run

    build/openconche [--assets dir | --no-assets] [--userdir dir, required for automated runs] [--scale 1..8] [--fullscreen] [--headless]
                [--display-size WxH] [--script file] [--screenshot path] [--log-unimplemented file] [--frames n]
                [--dump-windows] [--config key=value] [--log file] [--log-input] [--cheatmode] [--run file.lua]
                [--import-prefs prefs.dat [--import-into-default]]

- `--scale n` (1 to 8, fractions allowed: `--scale 1.5` is 1200x900, `--scale 2` is 1600x1200) sets the window size at start; if it plus the window frame does not fit the usable desktop the next whole number down that fits is used and logged.
  The window is resizable (minimum 800x600) and the 800x600 picture is letterboxed to whatever size it gets and text is rasterised again at the
  new output scale. Each size change logs `[display] resized, output WxH, scale S`.
- `--fullscreen` is borderless desktop fullscreen, 4:3 letterboxed with black bars. Alt+Enter and F11 toggle it, and so do the OS's own controls (the macOS green button, a window menu); every change is logged and the preference saved.
- `--headless`: SDL dummy video driver (forced even if `SDL_VIDEO_DRIVER` is exported) and the software renderer. Headless and
  windowed share one drawing path straight to the backbuffer with 4:3 letterboxing; the dummy window is created at the real
  output size (800x600 at scale 1, 1600x1200 at scale 2) and is not hidden. Fullscreen (`--fullscreen`, F11, Alt+Enter) is
  emulated by resizing that window to `--display-size WxH` (default 2560x1440). Time is simulated at 1/60 s per frame.
- The letterbox bars are black; only the 800x600 logical area is cleared to the scene background.
- `--screenshot path` writes a PNG of the last frame and quits: after the script when `--script` is given, after frame n
  with `--frames n`, otherwise after the first frame. PNGs are the full output including bars.
- `--frames n` quits after n frames.
- `--no-assets` skips the default search below and shows the test scene (a one-line message on stderr); combined with `--assets` it is a usage error (exit 2).
- `--assets` is the game data. Without it, an interactive run boots the launcher's saved choice (`OpenConche/games.cfg`, see docs/design/game-finder.md) or shows the game launcher when there is none; an automated/headless run never opens a dialog (and never reads the machine's library), warns on stderr and shows the test scene. A bad `--assets` directory exits 2 with a message. `--userdir` defaults to
  `~/Library/Application Support/Chocolatier3` (macOS), `%APPDATA%\Chocolatier3` (Windows) or `$XDG_DATA_HOME/chocolatier3`; it is created only by the first write to a `user:` path.
  `--log-unimplemented file` writes `name<TAB>count` for every stubbed native that was called, most frequent first, at exit.
  `--seed n` seeds `RandRange` (default 1 when headless, time-based otherwise; the seed is logged).
- `--game choc3|choc2|choc1` says which game the assets are; without it the engine detects it from `settings.xml` (`game`) and
  `ui/uihelp.lua` / `data/ports.lua` and logs `[game] ...`. Each game has its own default `--userdir` (`Chocolatier2`, `Chocolatier1`
  next to `Chocolatier3`), its own close-request script and window title. Chocolatier 2 and 1 run straight from the original
  install: `openconche --assets <your-choc2-install>/assets`, `openconche --assets <your-choc1-install>/assets`.
- **Automated runs need `--userdir`.** `--headless`, `--script`, `--frames`, `--screenshot`, `--dump-windows`, `--log-unimplemented` and `--no-assets` mark a run that is not a human playing; without `--userdir <scratch dir>` they exit 2 with a one-line message naming the default folder, so a stray test run cannot overwrite real saves. With those flags `--userdir` also must not be the default folder of any game itself (a plain windowed run may pass it). `--help` is exempt, and so is `--run` (human play-testing on your own profile).
- Exit codes: 0 ok, 1 runtime failure, 2 usage or script error.
- `--dump-windows` prints the window tree (modal chain, then children front to back, parent-relative rects) at exit.
- With a script host running, each frame draws the game's window tree in full instead of the test scene.
  `--config key=value` (repeatable) overrides a `settings.xml` value; Reforged's `DebugOut` logging is on with `--config cheatmode=1`.
- Received input is logged to the `--log` file only, e.g. `[input] mouse-down 400 300 button 1` (logical coordinates); see `--log-input`.

## Reproducing a bug headless (fast, no game window)

1. Build `openconche` (or `engine_tests`, if the case fits a unit test).
2. Copy the preamble of `tests/check_choc2_trade.py` (a new Story game through the tutor to the live
   San Francisco port) — or `check_map.py`'s for Chocolatier 3 — into `x.script` and add steps.
3. Run `build/openconche --headless --assets <game assets> --userdir <scratch dir> --script x.script`.

The commands that matter: `dump` (the window tree with labels and money), `lua <code>`, `expect-lua`,
`expect-window`, `click x y`, `key <name>`, `type <text>`, `wait <ms>`, `screenshot-logical`. The scripted
playtests in `tests/check_*.py` are the map from these commands to the whole CTest suite; the two
invariants that make refactors safe are the boot-trace goldens (`tests/golden/`, refreshed only on purpose) and, for
output regressions,
`python3 -I tools/compare_test_out.py <baseline> <out>`.

A hang or window-state puzzle: F10 writes a diagnostics report, `--log <file>` keeps input and windows for the
run; see "Diagnosing a hang" below.

## Script files (`--script`)

One command per line; `#` starts a comment. The whole file is checked before the window opens.

Run scripts with `--headless --userdir <scratch dir> --script file`; `--script` without `--userdir` is refused (exit 2) so it never touches the real profile.

    click x y          move, press and release the left button
    move x y           mouse motion        down x y / up x y   press / release left button
    key <name>         SDL key name, optional modifiers: key alt+Return, key F11, key Escape. A printable key
                       (key a, key shift+a) also types its character into a focused text field
    type <text>        types the text (UTF-8, to the end of the line, no # comments) as OS text input would
    lua <code>         runs the code in the game script, as the app's own DoLuaString does (a test hook)
    wait <ms>          simulated time in headless mode, real time otherwise
    screenshot <path>  PNG of the whole output, bars included
    screenshot-logical <path>  PNG of the 800x600 logical area only (downscaled if the output scale is not 1)
    resize W H         the window is dragged to W x H window pixels (whole numbers, 1 to 16384); the picture is letterboxed
                       to it and `[display] resized, output WxH, scale S` is logged. Ends the frame, like `key`.
    click-window x y   (also move-window, down-window, up-window) the same in window coordinates, no logical mapping
    expect-window <name> [timeout_ms]
                       waits (default 5000 simulated ms) until a window of that name is shown, else fails the run with
                       exit 1. Searches the top modal down the chain; a modal matches by its script path
                       (ui/mainmenu.lua) or its current name. Without a script host it only logs and continues.
    dump               prints the window tree; diag  writes the diagnostics report (see "Diagnosing a hang")
    quit               stops the app directly (it does not send a window-close event)

Coordinates are logical (800x600) unless the command says window. Commands are injected with `SDL_PushEvent` in window
coordinates and converted back with `SDL_ConvertEventToRenderCoordinates`, so they take the same path as real input.
`key` and `resize` end the frame so the next command sees any display change. Mouse events outside the picture are logged with
`(outside)` and carry `inArea=false`. A script that ends without
`quit` quits. Unknown commands or key names are rejected at parse time (exit 2).

## Play-testing fast

To reach a screen without playing through its unlocks, start the real windowed game with Reforged's own dev toolbar:

    build/openconche --cheatmode                                  # same as --config cheatmode=1
    build/openconche --cheatmode --run tools/playtest/casino.lua

`--cheatmode` sets the Playground SDK's `cheatmode` setting, which each game reads its own way: Chocolatier 3 as `CheckConfig("dev")` (vanilla's `dev/dev_menu.lua` and Reforged's `ui/debug.lua` are both gated on it), Chocolatier 1 as `UtilCheckCheatMode()` (`gCheatsEnabled`: the main menu's Toggle Medals and Delete All Players buttons and the port's `ui/cheatbar.lua`); Chocolatier 2's scripts never ask; The Great Chocolate Chase reads it as `IsEnabled(kCheatMode)`, which enables the Cheats buttons of its map and level screens, and its `DoCheatGame` / `DoCheatMap` natives refuse (logged, answer `Invalid Cheat`) without it. The game's developer keys need no setting except Ctrl+D: Ctrl+C in a level opens its console (`setvalue`, `setpos`, `addminutes`, `getupgrade` ... see `docs/re/greatcc-debug-tools.md`), Ctrl+S / Ctrl+T on a level or the world map overlay the mouse position and sprite or the texture list, `--config zones_debug=1` draws the zone points and `--config game_debug=1` shows the level's Debug button; Ctrl+D on the main menu acquires every achievement and works only with `--cheatmode` (an engine addition, logged as `MainDebug`); the Cheats dialog's Choose Achievements window needs `--cheatmode`. In Chocolatier 3 it shows the toolbar along the top of the port and map views (and marks the campaign unranked, which is fine for testing):
`Q:<quest>` (quests), Inventory, Ingredients and Recipes (click an entry to unlock or lock it; Unlock All), Ports (click a port, then
Teleport Here / Toggle Lock / Toggle Hidden; Unlock ALL), Buildings (`[o]` owns a shop or factory, `[b]` blocks it, clicking the
name teleports and visits), Vars, Saves, Utils (+1 week, +1/4 week, reload port, restart, Product Creator = the test kitchen, Crates,
Slot Machine, Tip Manager, Debug Console, Community Cookbook) and Money (+/- amounts, set exact). Escape closes a toolbar page.
The Curve Editor opens but needs `SetAnimSprite`/`UpdateAnimTime`, which are logged stubs. Reforged references `dev/dev_products.lua`,
which does not exist in the mod, but no toolbar button opens it, so nothing fails.

`--run <file.lua>` runs the file's Lua in the game script (like the script command `lua`) the first time the port view or map view is
the top window (it waits while a quest or other dialog is open above it), logs a `[run] <file>` line, and logs (never dies on) a Lua error. **F9** reads the file again and runs it again, so edit it
while the game runs. F9 does nothing before the first run, and while a dialog is open above the view it only logs
`[run] skipped: a dialog is open` (the snippets swap the view, which would replace the dialog): close the dialog first. On a Mac laptop
F9 needs the `fn` key unless function keys are set to standard. `--cheatmode` is just `--config cheatmode=1`: both share one override list,
and the last one given wins. Ready snippets in `tools/playtest/`, each checked headless by `reforged_run` / `reforged_playtest_snippets`:

| file | does |
| --- | --- |
| `casino.lua` (Reforged only: `las_casino`) | unlocks Las Vegas, flies there, opens the slot machine selection (`ui/ui_slotselect.lua`) |
| `factory.lua` | $1m, owns the Zurich factory, 50 of every ingredient, rank 3, opens `ui/ui_factory.lua` |
| `designer.lua` | every ingredient unlocked and stocked, opens the test kitchen (`ui/ui_kitchen.lua`; press its design button) |
| `rich.lua` | +$10m, every port, recipe and ingredient unlocked |
| `freeplay.lua` (Reforged only: `LoadGame("free")`, `Player.mode`) | saves the Story game, loads the Free Play session and opens its map |

The snippets run in the game's own script, so they can use any game function (`Player:AddMoney`, `lasvegas:Unlock()`,
`DisplayDialog{...}`); write your own the same way. Free Play on the main menu needs a saved Story game: run `Player:SaveGame()`
(or `freeplay.lua`, which does it and switches directly).

## Importing the original profile

The original game keeps all its players, their settings and their saves in one encrypted file, `prefs.dat`. The engine can read a **copy** of it (`docs/re/prefs-and-saves.md` has the format). On Windows the file is

    %APPDATA%\PlayFirst\chocolatier-decadence-design\prefs.dat

On a Mac the original kept it in a folder under `~/Library/Preferences/` named after the app bundle; the exact folder name is a guess from the decompiled code, so look in that folder for `prefs.dat`. The importer was written from the decoded format and synthetic test files; it has not been run on a real `prefs.dat`, so treat the result with suspicion and keep the backup it makes.

1. **Copy the file first**, to a scratch folder, and import the copy. The importer only ever opens its source for reading, but a copy means a mistake of yours cannot touch the original.
2. Run it with a user folder of your choice (it never starts the game):

       build/openconche --import-prefs /scratch/copy/prefs.dat --userdir /scratch/choco-user

   then play with `--userdir /scratch/choco-user`. The user folder must not be the folder holding the copy (or inside it), nor lie under a `PlayFirst` folder, so an import can never write into the original game's folder. To import into your real profile (the default user folder) pass `--import-into-default` and leave out `--userdir`. If the target already holds files, the importer first copies it to `<userdir>-backup-<YYYYMMDD-HHMM>` (UTC) next to it, and stops if that copy fails.
3. It prints a summary: users found and imported, saves imported or refused, prefs mapped and ignored, one line per user (new or replaced, and whether their save was imported, overwritten or kept), warnings. It then reads the folder back and exits 1 if the result is not what it meant to write (for example a read-only folder). A user in the target with the same name as an imported one is overwritten (that is what the backup is for); other users are kept. Each user's save becomes their engine save, the current player is the one the original had selected, and the sound settings and the anonymous score name come across. The fullscreen setting is stored in the profile too, but the engine does not read it at startup (use `--fullscreen`, F11 or Alt+Enter). A save that is only a never-started empty player is refused. A truncated or damaged file imports everything before the damage and says so.

Not imported: the high scores (`hiscore.dat`; its XML layout is not decoded), the old XML prefs format, and `.choco3` files (plain text; copy them into the user folder by hand).

## Listening to the audio

Sound effects and ambience play through one SDL3 output stream (44.1 kHz stereo). `--headless` and runs where no output device opens (the log says `[audio] no audio device (...): running silent`) mix into a discard buffer instead, so scripted runs stay deterministic. To listen, run the real windowed game on a machine with speakers, e.g. `build/openconche --assets Chocolatier-Decadence-Reforged/assets --log run.txt`, and watch `run.txt` for the `[audio]` line of every sound. On the main menu you should hear the menu theme looping, a soft rollover tick as the pointer crosses a button and a click when you press one. Open Options: dragging a slider changes that volume as you drag (Sound is the clicks and effects, Music the theme, Ambient the background beds), the mute box silences everything, and OK keeps the levels for the current profile. Start a new game and enter Zurich: the city bed plays under the port music and its sting. Open a chocolate factory and press Go: a countdown, a bell as play starts, the factory theme, ticks and shots, and a whistle near the end; Pause (or switching to another window) freezes the music and effects, while the pause dialog's button still clicks. Music streams from the Ogg files, and every sound effect and bed is decoded on one background thread right after start (the log says `[audio] prefetched N sounds, M KB, in T ms`, about 150 ms of one core, roughly 45 MB), so neither switching tracks nor the first flight should stall; headless runs decode on first use instead. Listen for these. The menu theme loops without a gap or click at its loop point, and opening Options or Help and coming back keeps it playing from where it was (it must not restart). Entering Zurich fades the menu theme out over about 1.5 s while the port sting fades in over about 3.3 s, with the city bed underneath; the sting hands over to the map layers after about 6 s and the music then ends on its own (a port arrangement is one pass, 30 to 85 s depending on the port, not a loop), leaving only the bed. About 25 s after you arrive the music is stopped with a short fade, and 10 to 15 s later one of three longer full-instrument cues (the `port_music` cues, different drums, brass and piano entering block by block roughly every 20 s) fades in and loops, then is stopped and restarted at random 40 to 60 s and 10 to 15 s gaps. Opening the world map swaps to the two-layer map loop and drops the bed. A flight fades the plane loop in and out over about 1.6 s, and landing brings the destination's bed and sting. Start a chocolate or coffee game: the short intro, then the body loops; the chocolate game ends with its closing section, and when you close the results the port bed returns. Pausing (Pause button or switching windows) freezes the music, and resuming continues mid-note. Check that effects are not buried by the music, and that the cues' last fraction of a second before they loop is quiet. Whether the real game does exactly these fades, and the exact stem entries of each cue, are recorded as decisions in `docs/design/audio.md`; they were set by ear against the original and may be off.

The Macintosh Chocolatier 3 assets (`--assets <mac install>/Contents/Resources/assets`, the folder holding `cadi/cadilite.spec`) build their sound table from that spec, so they play the Mac file names (`cadi/audio/Main_Menu_v2.ogg`, `launch.ogg`, ...); the load prints one `[audio] cadilite.spec: N events, ...` line and a line per event with no engine equivalent. Music is one file per event: it restarts with a cut each time the event fires and stops with `Stop_Music`, a port plays its sting once and then only the bed until the port timer sends a `port_music<n>` cue, and ambience is a single bed with no sweeteners. These names are silent on the Mac, as in the original, and log `[audio] unknown event <name>` once: `start_bell`, `ingredient_success_N`, `Stop_win_money`, `Stop_machine_spin`, `Stop_conveyor_belt_loop`, `machine_spin`, `Stop_travel_plane`, and the building beds `casino_ambience`, `test_kitchen`, `saloons`, `train_station`, `plantation`, `factory` and `shop`; the `crate_*` sounds are named by the spec but their files are not shipped. `tests/check_audio.py --dataset mac` (CTest `mac_audio`) checks this from the log, and `tests/check_audio_names.py` lists the Mac Lua names against the table.

Chocolatier 2 (`build/openconche --assets <your-choc2-install>/assets --log run.txt`) uses plain files instead of events. The main menu loops `music/gameplay4.ogg`. Start a new game: the port's bed (`sfx/metropolitan_loop.ogg` in San Francisco) loops softly under a gameplay track (`gameplay1` to `gameplay3`, a different one each time one ends, with a 1 s fade between them). Open the map and travel: the destination's 5 s sting plays once, and is not cut when the port appears; a gameplay track fades in after it. Buying, selling and entering buildings must not restart the music, and a building's own bed replaces the port's with a short crossfade (the Paris building that names the missing `market_loop.ogg` is silent and logs `missing file` once). Open a factory to reach the mini-game dialog: a short loop plays while the intro shows, the faster `minigame` track starts with Start, Pause freezes the track while the bed keeps playing, Resume carries on, and closing the game brings the gameplay music back. Every change is one `[audio] music ...` or `[audio] ambient ...` line in the log. The fade lengths and the choice of tracks are inferences recorded with the RE facts in `docs/re/choc2-audio.md`; they may be off.

## Diagnosing a hang

If the game stops responding (a frozen map, a dialog that never closes), run it with a log file:

    openconche.exe --scale 2 --log openconche.log

- `--log-input` additionally logs `[input] mouse-move` lines to the log file; by default only mouse down/up, key and text events are logged (a play session otherwise writes ~13,000 motion lines).
- `--log <file>` writes every line the engine logs (`[lua-error]`, `[unimplemented]`, `[input]` (never on the console), `DebugOut` from Lua, script
  driver output, `[travel]` lines) to the file as well as to the console. Each line starts with `[+<ms>ms]`, the milliseconds
  since the log was opened; the file is truncated at start and flushed after every line, so it is complete even if the game
  has to be killed. It does not depend on stdout, so it also works without a console. It is off by default.
- **F10** (unused by the game) writes a diagnostics report to the log between `BEGIN diagnostics` and `END diagnostics`
  lines: the engine clock; the `Main` coroutine (suspended in `WaitForMessage` or a plain yield, when it wakes, a Lua
  traceback, the Lua message and command queue lengths); the modal stack; pending window messages; running transitions and
  window animations (window id and interval); `gTravelActive`, `gQuitActive` and `gUIPaused`; the flight in progress
  (origin, destination, progress, paused, events fired); and the full window tree, as the `dump` script command prints it.
  Press F10 more than once, a few seconds apart, to see what is changing.
- Travel logs from the map window itself, at the moment things happen: `[travel] started (x,y) -> (x,y) <ms> ms, <n> events`,
  `[travel] event i/n handler start` and `handler returned` around each event's Lua, `[travel] arrived` when the last event
  has returned, and `[travel] cancelled` if the map is torn down before that. A flight that hangs shows `handler start`
  with no `handler returned` (the handler is stuck), or `started` and no events (the clock stopped); a trip that was left
  shows `cancelled`.
- If `Main` (the game's main coroutine) dies on a Lua error, the log gets `[script] Main died: <message> - the game will no
  longer respond` followed by the traceback of the failing call, and every later report starts that section with
  `MAIN IS DEAD`. After that nothing in the UI reacts; the clock and the window tree still draw.
- The report also shows `foreground=` and `backgroundDelayMs=` (a backgrounded window sleeps longer each loop).
- If the log file cannot be opened the game still starts, prints the reason to stderr and shows a message box.
- Scripted runs use the `diag` command for the same report (`--headless --userdir scratch --script s.txt --log out.log`).

What to send: after the hang, press F10 once or twice, close the game, and send `openconche.log` (plus a note of what you
clicked last). If the window does not respond at all, F10 will not either; the `[+...ms]` lines up to the hang, and the
`[travel]` lines in particular, are still in the file.

## Engine orientation (what to know before debugging)

- **Layout.** Engine core: `src/{platform,gfx,audio,assets,ui,sdk,script,app}`. Games:
  `src/games/{common,choc3,choc2,choc1}` plus `games/Registry`; each game is a `GameModule` with its own
  `module.cmake`. `sdk/` is the sprite tree, `Ticker` and users; `common/` is gameplay shared by the games. Who
  may include whom is enforced by CTest `layering` (`tools/check_layering.py`).
- **The UI tree is drawn in full every frame** (`UpdateWindow` is a no-op, `src/script/WindowNatives.cpp`),
  exactly like the originals. "Stale display" bugs are about *which window* got the data, not dirty flags.
- **Stacked modals each own their windows.** A dialog over the port view has its own `Ledger`, and the port
  view's is still underneath: natives that act on "the" ledger or a named window must consider the whole modal
  chain (`Ledger::all`, `src/games/choc2/LedgerWindow.cpp`), not just `topModal()`. Windows under a modal
  have a stopped modal clock, so their animations do not run.
- **The game Lua is the code, and it is mostly unreadable**: Chocolatier 1 and 2 ship compiled bytecode inside
  `assets.pfp`; Chocolatier 3 ships plain text (vanilla and Reforged alike). Read the games' logic through the
  RE fact files, which cite `file.lua:line` wherever a claim is made.
- **Unimplemented natives log, they do not crash.** `tools/gen_stubs.py` generates the stub tables from
  the RE facts; a stub logs `[unimplemented]` once — a new line in a scripted run is a finding, not noise.
- **Original art direction matters.** Chocolatier 2's vehicle and animal art faces left and is mirrored when
  moving right (`PathSprite::artFacesLeft`, set only for Chocolatier 2); all three games skin the mouse cursor
  with their own arrow art, and the shipped cursor PNGs are the only pfp art whose transparency is a pure
  magenta colour key, applied on the cursor path only.

### Source layout in detail

`src/app` (main, args, App loop, ScriptDriver), `src/assets` (`AssetFS` case-insensitive file index over layered `AssetSource`s, pugixml `Xml` helper, `Content` loaders and `StringTable`), `src/script` (Lua host: `LuaState`/`LuaRef`, `bind<Fn>`, loaders, the SDK natives, the stub mechanism, `GameModule`, `ScriptHost`; no SDL), `src/sdk` (Playground SDK classes: sprite tree, `Ticker`, users, hiscores), `src/games` (`Registry`, `common/` shared gameplay, and one module per game: `choc3/`, `choc2/`, `choc1/`, each with a `module.cmake`; `-DCHOCO_GAME_CHOC2=OFF -DCHOCO_GAME_CHOC1=OFF` leaves a game out; `tools/check_layering.py` enforces who may include whom), `src/ui` (`Window` tree, `WindowManager`: modal chain, creation stack, deferred destruction, message queue and dispatch; no SDL, no Lua headers), `src/gfx` (SDL-free renderer interface, `TextureCache`, `HitMask`, `RecordingRenderer`; `platform::SdlRenderer` implements it, see `--test-scene`), `src/platform` (all SDL code: `Platform` owns SDL init/quit and is created first, `Window` takes it and exposes an opaque `rendererHandle()`; PNG writing), `lua/` (`prelude.lua`, `host.lua` and `animationscript.lua`, embedded at build time), `tools/` (`gen_stubs.py` regenerates `src/games/choc3/generated/Stubs_choc3.inc` and `src/games/choc2/generated/Stubs_choc2.inc` with `python3 -I`, `--check` runs in CTest; `gen_audio_events.py`, `check_layering.py`, `compare_test_out.py`, `playtest/`), `tests/` (doctest unit tests, the `check_*.py` scenarios, `scripts/`, `assets/` fixtures, `golden/`), `cmake/` (`EmbedLua.cmake`, `PatchLua.cmake`).

## Platform notes

On Linux CMake sets `SDL_UNIX_CONSOLE_BUILD`, so SDL configures without X11/Wayland development packages (headless runs
need only the dummy driver; install those packages for a real window). PNG paths are UTF-8 on Windows (`SDL_SavePNG` takes
them that way) and script and asset paths go through `pathFromUtf8`; environment lookups (`%APPDATA%`) use the wide API.

On Windows `openconche` is a GUI-subsystem program (`WIN32_EXECUTABLE`) with `SDL_main` as its entry point (`src/app/main.cpp`
is the only file that includes `SDL_main.h`), so `argv` is UTF-8 and non-ASCII paths in `--assets`, `--userdir`, `--log`,
`--script` and the default user dir work. It has no console window. `src/platform/Console.cpp` handles the consequences:

- At startup it attaches to the parent console (`AttachConsole`, a no-op when there is none) and points stdout/stderr at
  it, unless they are already redirected to a file or pipe (decided by `GetFileType`, since a non-null handle is not
  proof of usable stdio). So `--dump-windows`, `--log-unimplemented` and error output print from cmd or PowerShell.
  Started from Explorer there is no console and that output is dropped.
- A GUI-subsystem exe is not waited for by an interactive cmd or PowerShell prompt: the prompt returns at once and
  output arrives late. Use `start /wait openconche ...` (or redirect output) to get the exit code; batch files and pipes do
  wait. CTest is unaffected, since it waits on the process and captures redirected output.
- Without a console, a usage error, `--help`, a bad `--assets` (the asset-mount error) and an unreadable `--script` show
  a message box (not for `--headless`, which must never block). "No game assets found" also shows a warning box.
- `--log <file>` needs no console. If the log cannot be opened a warning box says so and the game runs without it.

## Lua notes

The game's Lua has a `continue` statement that stock 5.0.2 lacks (Reforged's `sim/port.lua` uses it, and the Mac binary's
token table has it between `break` and `else`). `cmake/PatchLua.cmake` adds it to the fetched sources at configure time:
it jumps to the end of the enclosing loop body.
