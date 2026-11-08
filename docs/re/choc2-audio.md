# Chocolatier 2 audio natives: SetMusicState, SetAmbient, PauseMusic, ResumeMusic

Status: call sites read from the Lua 5.0 bytecode of `re/choc2/pfp_extracted/` (disassembled with a workbench script).
The native bodies live in `chocotwo.exe`, which has not been decompiled, so everything about what the natives do is **inference** from call
sites, file names, durations and Chocolatier 1 (`docs/re/choc1-natives.md` section 6). Line numbers below are the `[line]` column of the
bytecode listing (source line in the original `.lua`). Closure names are those given by the bytecode's local/global names.

## 1. Every `SetMusicState` call (13 sites, none anywhere else)

The native takes `(state)` or `(state, file)`. `file` is a path relative to the asset root (`music/...ogg`), not to an `audio/music/` prefix as in Chocolatier 1.

| # | Call | File:line | Situation |
|---|------|-----------|-----------|
| 1 | `("normal", "music/gameplay4.ogg")` | `ui/mainmenu.lua:229` | Main menu build, immediately followed by `SetAmbient("")` (:230) then `bsgTriggerOffer()`. The menu music is gameplay4. |
| 2 | `("normal")` | `ui/portview.lua:118` | Entering the port view (after `SetLedgerMode("port")`), then `gSim.port:SetAmbient()` (:119). |
| 3 | `("normal")` | `ui/map.lua:141` | Entering the map view, then `gSim.port:SetAmbient()` (:142). |
| 4 | `("normal", "music/<dest.name>_sting.ogg")` | `sim/travel.lua:95` (`Simulator:TravelTo`) | In the leg loop, once per trip: only when the remaining-legs counter is 0 (last leg) and the week counter equals the leg's `weeks` (the last week). It runs right before `TravelOneWeek`. `<dest.name>` is the destination port's name, so the file is e.g. `music/paris_sting.ogg`. |
| 5 | `("normal", "music/medal_fanfare.ogg")` | `sim/medals.lua:63` (`Medals:Award(name)`) | Awarding a medal for the first time, before the `ui/medal_award.lua` dialog is shown. |
| 6 | `("silent")` | `sim/port.lua:202` (`LPort:SetAmbient`) | The port is `hidden` (secret port). |
| 7 | `("normal")` | `sim/port.lua:203` (`LPort:SetAmbient`) | Any other port. |
| 8 | `("factory_wait")` | `ui/factory_minigame.lua:213` (dialog build) | The factory mini-game dialog has just been built, before the player presses Start (intro text, "start"/"cancel"/"help" buttons). Followed by `SetAmbient("")` (:214). |
| 9 | `("")` | `ui/factory_minigame.lua:19` (`StartGame`) | Start pressed: the game proper begins (`ResumeMinigame()` follows). |
| 10 | `("")` | `ui/factory_minigame.lua:32` (`RestartGame`) | "try again" after the game ended: `ResetMinigame()` then this, then `ResumeMinigame()`. |
| 11 | `("factory_wait")` | `ui/factory_minigame.lua:86` (`MinigameEnd`) | The game is over; the results panel shows (`performance`, `end_panel`, resume/restart/done buttons). |
| 12 | `("normal")` | `ui/factory_minigame.lua:38` (`CancelGame`) | Cancel from the intro: window closes. |
| 13 | `("normal")` | `ui/factory_minigame.lua:44` (`CompleteGame`) | "done" after a finished game: sets `gSim.warnOnMiss=false`, closes the window, reads `GetProduction()`. |

Findings:

* **`"resume"` is not a music state.** The only `"resume"` strings in the factory script are `EnableWindow("resume", ...)` (a button name, :215 and the pause handlers). The complete set of state strings is `"normal"`, `"silent"`, `"factory_wait"` and the empty string `""`.
* The empty string is a real state (calls 9 and 10). By elimination (inference) it means "mini-game action music": `minigame.ogg` (83.1 s) is in `music/` and no Lua names it.
* No other screen sets a state: the lab (`ui/lab_game.lua`), dice (`ui/gamble_dice.lua`), market/shop UIs and the medal dialog only call `PlaySound`. Hence the lab and the casino play the current "normal" music.
* `gameplay1..3.ogg` and `minigame.ogg`/`minigame_wait.ogg` are never named in Lua. The only Lua-named music files are `gameplay4.ogg`, `medal_fanfare.ogg` and `<port>_sting.ogg`. So "normal" without a file must make the native choose among the gameplay loops itself (inference; Chocolatier 1's `BackgroundMusic` picked randomly among `gameplay1..3`).

## 2. The `music/` folder (27 files, all Ogg Vorbis, 44100 Hz stereo)

Durations from the last Ogg page granule position.

| File(s) | Length | Notes |
|---------|-------:|-------|
| `gameplay1.ogg` | 65.35 s | |
| `gameplay2.ogg` | 67.81 s | |
| `gameplay3.ogg` | 60.56 s | |
| `gameplay4.ogg` | 60.04 s | named by the main menu |
| `minigame.ogg` | 83.09 s | |
| `minigame_wait.ogg` | 10.14 s | short, likely a loop bed for the intro and results |
| `medal_fanfare.ogg` | 6.40 s | named by `Medals:Award` |
| `<port>_sting.ogg` x 20 | 4.64 s (newyork) to 5.54 s (manila) | abidjan 5.14, amazon 4.75, bombay 4.72, buenosaires 4.97, cairo 4.99, casablanca 5.13, cayenne 5.32, fiji 4.89, himalayas 4.97, jakarta 5.44, mahajanga 4.70, manila 5.54, moscow 5.01, newyork 4.64, paris 4.70, sahara 5.51, sanfrancisco 4.83, sanjose 5.19, shanghai 5.40, sydney 5.09 |

Count: 4 gameplay + minigame + minigame_wait + medal_fanfare + 20 stings = 27. There is a sting for every one of the 20 port files in `ports/` (the other `ports/*.lua` files, `portlist`, `portlocations`, `portroutes`, `special`, are data), so `music/<dest.name>_sting.ogg` always resolves.

## 3. Proposed state to file mapping (INFERENCE)

* `"normal"` + file: stop whatever music is playing (fade, 1 s linear is the Chocolatier 1 rule and a reasonable default), play `file` **once**. Applies to the stings and the fanfare (4 to 6.4 s one-shots). After a one-shot ends the music returns to the normal gameplay rotation (a sting on arrival is followed by ordinary port music; there is no other way for music to resume since the port view later calls `SetMusicState("normal")`, see below).
* `"normal"` + `gameplay4.ogg` from the main menu: it is the menu track and, being a 60 s file, must loop (see section 6).
* `"normal"` alone (port view, map view, port ambience, mini-game exit): ensure the gameplay rotation is playing. If a normal-state track is already playing, do nothing (inference: otherwise every port/map/building return would restart the music; Chocolatier 1's `StartMusic` also ignores a request for the playing file). If a sting or fanfare is still playing, it should probably not be cut; this ordering matters because `ArriveAtPort` runs the port view's `SetMusicState("normal")` right after the sting was started (see section 5, unknown 3).
* `"silent"`: fade out and stop (hidden port: no music, only its ambience).
* `"factory_wait"`: loop `minigame_wait.ogg` (10 s). Used for the pre-game intro and the results panel.
* `""` (empty): loop `minigame.ogg` (83 s) while the game runs.
* `"normal"` is the only state that leaves the mini-game; its calls (12, 13) are issued before `CloseWindow`.

## 4. `SetAmbient`

All values pass one string argument (not Chocolatier 1's `{name=, loop=}` table):

| Argument | Where | Meaning |
|----------|-------|---------|
| `""` | `ui/mainmenu.lua:230`, `ui/factory_minigame.lua:214` | Stop the ambience. |
| `self.ambient` of a port | `sim/port.lua:199` (`LPort:SetAmbient`), reached from `ui/portview.lua:119`, `ui/map.lua:142`, `sim/building.lua:234` (after the keeper dialog and `ExitBuilding`), `ui/factory_status.lua:14` and `:47` | Port ambience. Default `LPort.ambient = "sfx/port_loop.ogg"` (`sim/port.lua:15`). No port file overrides the port-level key (`ambient=` there is a building/sprite key). The map view uses the port's ambience too. |
| `building.ambient` | `sim/building.lua:197` (`LBuilding:SetAmbient`), called from `OnActivate` at `sim/building.lua:202`, `sim/shop.lua:69`, `sim/market.lua:51` | Entering a building (shop, market, generic building, factory, ...). |

The values are **paths relative to the asset root** and include `sfx/` and `.ogg`. Values seen (from `ports/*.lua` and the sim classes):
`sfx/port_loop.ogg` (port default), `sfx/chocolate_shop_loop.ogg` (`LShop.ambient`), `sfx/marketplace_loop.ogg` (`LMarket.ambient`),
`sfx/other_loop.ogg`, `sfx/farm_loop.ogg`, `sfx/metropolitan_loop.ogg`, `sfx/saloon_loop.ogg`, `sfx/village_loop.ogg`, `sfx/seaside_loop.ogg`,
`sfx/remote_loop.ogg`, and `sfx/market_loop.ogg` (used twice in `ports/paris.lua`; **no such file exists in `sfx/`**: the folder has `marketplace_loop.ogg`; the native must tolerate a missing file, or this is a dormant bug).
Existing loop files (all 44.1 kHz stereo, 10.0 to 10.9 s): chocolate_shop 10.03, farm 10.40, marketplace 10.73, metropolitan 10.36, other 10.36, port 10.87, remote 10.17, saloon 10.70, seaside 10.15, village 10.22.
So `SetAmbient(path)` should play `path` looped on the ambient category, replacing the previous one; `""` stops. (Inference: Chocolatier 1 ignored a repeat of the current name; since the port calls `SetAmbient` after nearly every visit, the same rule avoids restarting the bed.)
Note `LPort:SetAmbient` also issues the music state, so every return to the port, map or a factory-status screen re-asserts `normal`/`silent`.

## 5. `PauseMusic` / `ResumeMusic`

Only the factory mini-game calls them (no focus, window or pause-menu use in Lua):

* `PauseMusic()`: `MinigamePause` (`ui/factory_minigame.lua:52`, the pause button, which is `PauseMinigame` from the engine), `MinigameWarn` (:63, ingredient wasted, label `factory_waste`) and `MinigameColor` (:74, first-colour hint, `factory_color`, which also sets `gSim.firstcolor`). The first two show the label `factory_paused`/`factory_waste` and enable resume/restart/done in the `info_dialog`.
* `ResumeMusic()`: `ResumeGame` (:25), resume button, followed by `ResumeMinigame()`.
* `RestartGame` (:29-33) does not call `ResumeMusic`; it replaces the music with `SetMusicState("")`.
* The pause calls do not touch ambience; the ambience is already `""` in the mini-game (`SetAmbient("")` at :214), so whether `PauseMusic` pauses ambient too is moot for Lua. Nothing in Lua says focus loss pauses music; that is native or absent (the Chocolatier 3 focus design in `docs/re/focus-and-display.md` is a separate mechanism). Whether the pause is "hold position and continue" (stream pause) rather than "stop" is inferred from the name and the Resume pair.

## 6. Looping and re-queueing

No Lua in Chocolatier 2 calls `StartMusic` or `QueueMusic`, and nothing re-queues music (no timers or callbacks mention music). The 60 s menu track and the mini-game tracks therefore must **loop in the native** (or the music falls silent after one play; the game cannot rely on a Lua re-queue). Stings and the fanfare are one-shot. Ambient beds loop in the native (they are 10 s clips, no Lua re-calls them).

## 7. Volume prefs

`ui/options.lua` has three sliders named `sfxslider`, `ambientslider`, `musicslider` (labels `opt_sfxlevel`, `opt_ambientlevel`, `opt_musiclevel`), a `mute` toggle button (`ToggleSoundMute()`, :40) and the OK handler calls `SaveVolumes()` (:44). The native applies the live values (Chocolatier 1 used prefs `soundlevel`, `musiclevel`, `ambientlevel`, default 750, and `soundoff`; for Chocolatier 2 the pref names are inference). Categories 1000/1001/1002 (effects/music/ambient) are Chocolatier 1 facts; assume the same split. `PlaySound` takes a bare name (`sfx/<name>.ogg`) or a path, and `LoadSound` (`ui/mainloop.lua`) preloads; those two are already implemented.

## 8. Unknown

1. Exact native behaviour of every state (crossfade, fade length, loop points): the exe is not decompiled.
2. Which gameplay loop plays for `"normal"` with no file (random? sequential? gameplay4 always?). The Lua never names `gameplay1..3`.
3. What happens after a one-shot (sting, fanfare) ends: gameplay resumes (needs the native to remember the normal state), and whether the arrival sting is cut by the following `SetMusicState("normal")` from the port view.
4. What the native does when an ambient name has no file (`sfx/market_loop.ogg` in `ports/paris.lua`); every sting file exists.
5. Whether `""` is really `minigame.ogg` and `factory_wait` really `minigame_wait.ogg` (both by elimination only).
6. Whether `PauseMusic` also pauses ambience, and whether any focus-loss path exists in the exe.
7. The exact C2 pref key names and default levels.
8. Whether `SetMusicState` with the same state/file as the current one restarts.

Related: `docs/re/choc2-port-and-ledger.md` (port/map call order), `docs/re/choc1-natives.md` section 6.
