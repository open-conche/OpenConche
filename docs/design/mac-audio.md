# Design: Chocolatier 3 Macintosh audio (CADI-lite)

The Mac build of Chocolatier 3 has no CAD file. Its audio is `assets/cadi/cadilite.spec` (483 lines, 116 event
blocks) plus 98 loose Oggs in `cadi/audio/`. When that spec is present, the choc3 module builds the SoundEvent table
from it at startup, so the Mac assets play what the Mac build played, using the Mac file names. The Windows CAD table
(`docs/design/audio.md`) is used only when the spec is absent.

Sources: `re/mac/decomp/TCadiLite.c` (`loadSpecFile`, `procEvent`, `procHardcodedEvent`), `TLiteSoundEvent.c`,
`_free/s.c` (`snarfGroup`), `_free/_.c` (`_CadiInit`, `_CadiEvent`), `_free/b.c` (`BSGSound*`), the shipped spec,
and the Mac Lua. Labels: **fact** (read in the decompile or the files), **inference**, **unknown**.

## 1. The spec format as the Mac build reads it

Loading happens once (`_CadiInit` builds `TCadiLite("cadi")`). The file is `cadi/cadilite.spec` (**fact**).
* The file is read line by line (buffer 10,240 bytes). Trailing whitespace is trimmed. A line holding only whitespace
  ends the current event, and so does end of file (**fact**).
* `:"name"` in column 0 opens an event. If an event is already open (no blank line in between), that is an error.
  Any other line outside an event is an error. There is no comment syntax (**fact**).
* Inside an event, each line is one entry, run in order. The `:` and `"` checks look at column 0 only. Tokens are
  split on space, tab and comma (**fact**).
  - `"other"`: call another event. It must be defined on an earlier line, which rules out cycles. An undefined callee
    or one defined later is an error (**fact**).
  - `play <group> <file> [loop]`: `play` is matched exactly. `loop` is matched case-insensitively, and a fifth token
    is an error. The sound is `TSound::Get("cadi/" + file, cached=1, group)`, decoded fully and shared per path
    (**fact**). Each play starts a new instance, so a second play layers over the first (**fact**, AU s.4). The `loop`
    flag is passed to `TSound::Play`, though the decompiler lost that argument (**inference**).
  - `stop <file>`: stops at once every sound that a `play` line with exactly this file string started. This uses a
    case-sensitive map keyed by the text as written (**fact**). Whether the stop fades is **unknown**; no fade code is
    seen.
* Event names are matched exactly and case-sensitively, so `"stop Music"` and `"stop music"` are two different
  events (**fact**). When a name is defined twice, the first definition wins and the duplicate is silently dropped,
  because `loadSpecFile` discards `addSoundEvent`'s error (**fact**). The shipped spec repeats `board_clear_bonus`,
  `pull_lever` and `spin_stop`. The second `spin_stop` (the one with `"stop Sfx"`) is dead.
* Groups: `_CadiInit` registers `Sfx`, `Music` and `Environments` first, as ids 1000, 1001 and 1002. These are the
  ids that `BSGSet{Sound,Music}Level` set (**fact**; the numbering is an **inference** from the map size). Any other
  group name gets a new id whose level is never set, so it reads as 0.0 and is inaudible (**inference**, AU s.4).
  The only such line in the shipped spec is `button_click`'s `play sfx` (lower case). No caller fires `button_click`.
* A load error makes the Mac build exit (**fact**). The engine never exits for one (section 3).

Runtime (`procEvent`, **fact**): a defined name runs its entries. Any other name goes to `procHardcodedEvent`, which
splits on a space or `_`:
* `<G> Volume` sets group G's level.
* `Stop <G>` kills every sound in group G.
* `Stop all` kills everything.

Anything else is ignored. A log is written only when `activate_CADILITE_event_log.txt` exists. As a result:
`Stop_Music` kills the whole Music group, and `Pause`, `Unpause`, `Stop_travel_plane`, `Stop_win_money`,
`Stop_machine_spin` and `Stop_conveyor_belt_loop` do nothing on the Mac.

**Shipped content (fact).**
* One-shots are `play Sfx` lines.
* Every music event is `"stop music"` followed by `play Music <file>`. `loop` is set only on `main_menu`,
  `map_screen`, `chocolate_making_game{_intro,,_outro}` and `coffee_making_game_{intro,outro}`. Stings,
  `port_music1..3` and `coffee_making_game` play once.
* Every port, plus `bank` and `market`, is `"Stop_Environments"` followed by one `play Environments <bed> loop`. There
  are no sweeteners.
* `win_money` is `"stop Sfx"` (which stops `win_money.ogg`) followed by a play, so it restarts.
* `mainmenu` plays `mainmenumusic.ogg` in Sfx. Nothing fires it.
* The `crate_*.ogg` files are named but not shipped.

## 2. What the engine builds

`audio/CadiLite.{h,cpp}` (engine layer; it holds no game names) parses the text into blocks, then compiles each event
into one `EventDef`. Compiling first flattens calls into an ordered list of stops S and plays P:

| Flattened shape | EventDef |
|---|---|
| P = `play Sfx f`, S empty | `Oneshot` f (or `Loop` when `loop`) |
| P = `play Sfx f`, S = {f} | `Oneshot` f, `restart = true` (releases its own voice first; `win_money`) |
| P = `play Music f [loop]`, S only Music files | **new** `Kind::MusicFile` f, loop flag |
| P = `play Environments f loop`, S only Env files | `AmbienceLoop`, files {f}, no sweeteners, no envelope |
| P empty, S covers every Environments file | `Stop`, `StopGroup::Environments` (`Stop_Environments`) |
| P empty, S covers every Music file | `Stop`, `StopGroup::Music` |
| P group other than the three | `Silent` (inaudible on the Mac too) |
| anything else (`mainmenu`, `"stop Sfx"`, `"stop Music"`) | `Unknown`, one load warning; once inlined, never fired |

For every entry, `fileRes` is `{"cadi/" + file}`. `channelVolume` is 1.0, because the Mac has no per-event gain
(**fact**). Group gains stay at level only, as `Audio::applyGroupGains` already does.

`EventTable` gains `explicit EventTable(std::vector<EventDef>)`, which takes no aliases and builds `byName_`. `Audio`
gains:
* `Kind::MusicFile`: `playMusicFile("cadi/" + f, loop, 0, 0)`. Every fire restarts the track from 0 with a cut,
  because the Mac runs "stop music" and then plays (**fact**). There is no retrigger-ignore, no `Part`, and
  `music-aliases.tsv` is not read. The Windows-only `startMusic` rules stay untouched.
* `EventDef::restart`, used by `Oneshot`: `mixer_.release(index, 0)`, then the event plays tagged with its index.

The choc3 module adds the **native rows** from its compiled table: every row of kind `Direct`, `Volume` or `Builtin`,
and the `Silent` rows in `Group::Direct` (`ui_click`, `firework1..5`, the typing clicks, `cadi/ui_click.ogg`, the
three `<G> Volume` rows, `Pause`/`Unpause`). These model `BSGSoundEvent` and the hard-coded events, which the spec
does not hold. It also synthesises `Stop_Music` (`StopGroup::Music`), which the Mac hard-codes. If a spec event has the
same name as one of these rows, the spec event wins; the shipped spec has no such clash. `Pause`/`Unpause` stay
`Builtin` on purpose: the factory pause silences audio as it does with the Windows data. This is a chosen difference,
because the Mac ignores those two names.

**Ownership.** `GameModule::audioEvents()` becomes `std::unique_ptr<const audio::EventTable> makeAudioEvents(const
assets::AssetFS&) const`. The default returns null. choc3 returns either the compiled spec or a copy of
`choc3::audioEvents()`, which stays for the tests. `App` owns the table in a member declared before `audioSvc`, so the
table outlives the `Audio` that refers to it. If the spec is not there, null makes `App` use `EventTable::empty()`.
Startup does this once; there is no per-frame work.

**Errors (decision).** A malformed line is logged once as `[audio] cadilite.spec:<line>: <message>` and skipped, and
loading continues. A bad call entry is dropped with a warning. Audio is optional, as it is for a bad CAD in `AssetFS`.

**`AssetFS` has no Mac name aliases.**
* Every Mac event names its own file, so none are needed.
* Chocolatier file names do not belong in `assets/`, the engine layer.
* Aliases would make Mac assets play `ingredient_success_N`, `launch_*` variants and others that the Mac build never played.
* If `cadi/cadilite.spec` cannot be read, choc3 falls back to the Windows table. Its names then find no Mac files and each logs `[audio] missing file` once; a readable spec with bad lines still builds a table from the good ones.

## 3. Names the spec lacks

These use the existing path: `Audio::event` logs `[audio] unknown event <name>` once and stays silent.
* Lua (Mac tree): `machine_spin` (although `machine_spin.ogg` ships), `Stop_travel_plane`, `casino_ambience`,
  `test_kitchen`, `saloons`, `train_station`, `plantation`, `factory`, `shop`.
* Native: `start_bell`, `ingredient_success_N`, `Stop_win_money`, `Stop_machine_spin`, `Stop_conveyor_belt_loop`.

All of these are silent on the Mac (**fact**). `travel_plane` is a one-shot here (4.5 s) and ends by itself. `Stop_Sfx`,
`Stop_all` and the space forms are not synthesised, because no caller sends them.

## 4. Mapping of the Windows music machinery

| Windows (audio.md) | Mac table |
|---|---|
| `Kind::Music`, roles, stems, clip timeline | none; `MusicFile` streams one file |
| Retrigger of the playing arrangement ignored | restart from 0 (Options/Help → `main_menu` restarts the menu music) |
| `Part` intro/start/outro via `music-aliases.tsv` | three separate events, each loops its own file |
| Crossfades from clip fades | cuts (0/0); `Stop_Music` cuts (`musicFadeMs_` = 0) |
| Port: sting, then layers, then `portmusicFADE` | sting once, then silence until `PortWindow::ChangeMusic` sends `port_music<n>` (once each) |
| Ambience: one bed plus random sweeteners | bed only; same index again = ignored (the Mac restarts the bed, which is inaudible for a loop) |

## 5. Layering (`tools/check_layering.py`)

`audio/CadiLite.*` includes nothing under `games/` and contains no banned token: no `ui_click`, no `firework`, no
`AudioEvents.inc`. The native-row merge and the existence check for `cadi/cadilite.spec` live in
`games/choc3/AudioEvents.cpp`. `app/` keeps its two allowed `games/` includes, because the new hook is on `GameModule`.

## 6. Slices (each tested alone; targeted tests only)

1. **Parser, compiler, engine kinds.** Add `audio/CadiLite.{h,cpp}`, the `EventTable(std::vector<EventDef>)`
   constructor, `Kind::MusicFile` and `EventDef::restart`. Nothing is wired yet.
   Tests are in `tests/test_cadilite.cpp`, with fixture `tests/assets/audio/cadilite/cadilite.spec` (about 40 lines,
   synthetic, using the existing `sine_*.ogg`). They cover:
   - the grammar: blank-line separation, trailing spaces, `LOOP` in any case, comma separators, the first duplicate
     wins, a call to an event defined later is rejected, a bad directive is skipped with its line number, and the
     `:` check is column 0 only;
   - the compile table rows of section 2, one assertion each;
   - with `Audio`: firing a `MusicFile` twice gives two `music file ... fade 0/0` lines; `Stop_Music` empties
     `audio music`; ambience A then B leaves one env; a restart one-shot leaves one voice; an unknown name logs once.
2. **Wiring.** Add `GameModule::makeAudioEvents`, `App` ownership, the choc3 spec path and the native rows. Update
   `test_choc2.cpp:143-145` to the new hook.
   - A unit test on the real spec is skipped when `CHOCO_MAC_ASSETS` has no `cadi/cadilite.spec`. It checks that
     there are 0 load errors and 3 dropped duplicates, and the kind of `main_menu`, `lima_sting`, `port_music2`,
     `lima`, `Stop_Environments`, `Stop_Music`, `win_money`, `ui_click` and `Pause`.
   - `tests/check_audio.py --dataset mac` (menu and music modes, with Mac expectations) runs as CTest `mac_audio`,
     registered only `if(EXISTS .../cadi/cadilite.spec)`. It checks:
     - the menu plays `cadi/audio/Main_Menu_v2.ogg`, and after Options OK that file has fired twice;
     - the map plays `map_screen`;
     - entering a port gives its sting, then its bed;
     - no `[unimplemented] SoundEvent` line appears.
   - Run `reforged_audio` and `vanilla_audio` to show the Windows path is unchanged.
3. **Cleanup.**
   - Remove `AssetFS::mountMacAudioAliases` and its declaration; the Mac assets no longer need it (rerun `mac_audio`).
   - Extend `check_audio_names.py` with `--lua <mac tree>`, so that every Mac literal is checked against the spec, with
     section 3's list as the documented-silent set.
   - `re-analyst` moves section 1's facts into `docs/re/audio.md` s.0/s.4, which still say the spec is unavailable.

## 7. Risks and what is left out

* **Open questions for `re-analyst`.** (a) Does `TSound::Play`'s argument at `TLiteSoundEvent::procEvent` carry the
  loop flag? (b) Does `TSound::Kill` cut or fade? (c) What does `TSound::Get` return for a missing file (`crate_*`)?
  The shipped game starts, so that case does not reach the fatal "fails to load" check. If (b) is a fade, change the
  0 ms fades in `MusicFile`/`Stop` to match.
* Left out on purpose:
  - the Mac `CADILITE_event_log.txt` (the `[audio]` log covers it);
  - `<G> Volume` with arbitrary groups;
  - `Stop_Sfx`/`Stop all`;
  - interpreting `mainmenu`.

  No caller reaches any of them.
* A cut with no fade can click. The mixer's existing behaviour at fade 0 is accepted; nothing new is added.
