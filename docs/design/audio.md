# Design: audio

Sound effects, ambience and looping music with SDL3 and stb_vorbis. Facts come from `docs/re/audio.md` (AU §n) and
`docs/re/audio-events.tsv` (the tsv), and they are not repeated here. Chocolatier 2 and Chocolatier 1 music is in
`docs/re/choc2-audio.md` and `docs/re/choc1-natives.md`. Full CADI (the original's beat-quantised starts and general
timeline engine) is not implemented: an arrangement is a fixed set of stages on a timeline (AU §3, §7.7).
The Macintosh build has no CAD: its table is compiled from `cadi/cadilite.spec` and is described in `docs/design/mac-audio.md`.

## 1. Behaviour the scripts depend on

- **`SoundEvent(name)`** is the only audio native that Chocolatier 3's Lua calls during play. It takes one string,
  returns nothing, never errors and never yields. Unknown names are silent. Natives call the same entry point
  (factories, slots, crates, ledger, fireworks, the recipe designer).
- **Dispatch, as in `BSGSoundEvent` (AU §1):** `ui_click` plays `cadi/ui_click.ogg`. A name starting with `firework`
  plays `sfx/<name>.ogg`. These two are the "direct" path. Every other name goes to the CADI event table, including
  `Pause`/`Unpause` and the 12 Reforged calls of `SoundEvent("cadi/ui_click.ogg")`, which match nothing and stay silent.
- **Button `sound=` / `rolloversound=`** and the typing clicks (`sfx/sfx_typing_0N.ogg?group=1000`) are file paths on
  the direct path, through `WindowManager::playSound` to `Audio::playFile`.
- **Prefs, per user, in per mille:** `soundlevel` 750, `musiclevel` 650, `ambientlevel` 500, `soundoff` 0.
  `ToggleSoundMute()` and `SaveVolumes()` are in `src/script/UserNatives.cpp`.
- **Mutual exclusion:** a new music event replaces the current music, and a new environment replaces the current one.
  `Stop_Music`, `Stop_Environments` and `Stop_<loop>` stop them.
- **Pause (AU §5):** losing focus pauses every voice. Factory `PauseGame`/`ResumeGame` (when state != 0) pause only
  the CADI voices and music, so button clicks in the pause dialog still play. Regaining focus does not undo a game pause.
- **Headless runs make no sound, are deterministic, and do not change any game RNG roll.**

## 2. Decisions

1. **One device stream, our own mix.** `SDL_OpenAudioDeviceStream` with input spec f32 stereo 44100 and a get-callback
   that calls `Mixer::render`. We don't use one `SDL_AudioStream` per voice. Fades, loops, pause classes, group gains
   and stage handover are then all in one pure function, which unit tests drive without SDL, and which headless
   runs pump from the simulated clock. SDL converts to the device's rate and format. Every Chocolatier 2 and 3 file is
   44.1 kHz, so the mixer never resamples. Chocolatier 1's `audio/music/menu.ogg` is 48 kHz (the other 74 files are
   44.1 kHz), so a music *stream* at another rate (8 to 96 kHz) is resampled inside `OggStream::nextFrame` by linear
   interpolation. Fully decoded sounds (`decodeOgg`, `Pcm`) reject any other rate and log it.
2. **SDL stays in `src/platform/`.** `platform::AudioDevice` owns the SDL stream. `src/audio/` is SDL-free.
3. **Decode:** SFX, ambience beds and sweeteners are decoded fully (`stb_vorbis_decode_memory`) to int16
   and kept in a cache that never evicts. Windowed runs decode all of them on one low-priority worker right after start
   (`Audio::startPrefetch`; `pcm()` joins a running decode, or takes one not yet started, so nothing that decodes
   successfully is decoded twice, and nothing decodes on the audio thread); headless runs decode on first use. All
   non-music audio is small (decoded: 43 MB for the 120 sounds the table reaches, resident from boot in windowed runs).
   Music streams: the ogg bytes are read once into a byte cache that never evicts (the whole of `cadi/audio` is
   9.9 MB), and each playing stem has its own `stb_vorbis` decoder over those bytes, which runs on the audio thread.
4. **Threading:** the main thread owns all logic (event lookup, RNG, timers, opening decoders, logging). The audio
   thread only runs `Mixer::render`. They share the `Mixer`, guarded by the SDL stream lock, which SDL holds during the
   callback (`SDL_LockAudioStream`). This is not lock-free. That's acceptable because the main thread holds the lock
   only for pointer-sized edits (`Mixer`'s `Guard`, re-entrant). The audio thread never allocates, frees, does I/O or
   logs. Finished music tracks are marked done, and the main thread unlinks them under the lock and frees them after it.
5. **No device, or headless:** the same `Mixer` is rendered into a discard buffer by the main loop, with
   `frames = floor(nowMs * 44.1) - rendered`. Headless and the windowed path then share one code path, voice end times
   are exact, and runs are deterministic. Headless runs also set `Audio::setSkipDecode`: music streams keep their
   positions but decode nothing and render silence, so track timing is unchanged. A failure to open the device logs
   one line, `[audio] no audio device (<SDL error>): running silent`, and drops into the pump mode.
6. **The event table is generated at build time:** `tools/gen_audio_events.py` reads the tsv plus the tables the
   engine owns, and writes `src/games/choc3/audio/generated/AudioEvents.inc`. The CTest `audio_events_up_to_date` runs
   it with `--check`. The generator cross-checks its inputs and fails on any inconsistency.
   - The tsv columns used are `event`, `group`, `kind`, `channel_volume` (empty = 1.0), `files`,
     `intermittent_files`, `interval_ms`, `envelope_ms`, `volvary`, and the `fadeNNNN` in `notes`. `called_from` is
     ignored. `docs/re/audio-music-clips.tsv` (clip names, fades, AU §10.2) and `docs/re/audio-port-music.tsv` (per-stem
     clips of the `port_music` cues) supply the rest.
   - `src/games/choc3/audio/music.tsv` (hand-written): for each arrangement, `sting`, `layers`, `intro`, `body` and
     `ending`, plus `under` (the layer under the sting from 0), `tail` (`portmusicFADE`, after the layers), and the
     clip's `loop`, `length` and `handover` (layers' entry), all in samples and checked by the generator against
     `audio-music-clips.tsv` (AU §10.5). The tsv's `files` order is not sting-first (`belize_`, `las_vegas_`), so the
     roles are spelt out here. Files are resolved per event through `resource_path` / `reforged_file` as a (primary,
     fallback) pair (AU §10.9).
   - `src/games/choc3/audio/music-aliases.tsv`: `event -> arrangement, part (start|intro|outro), master fade, source
     fade`. Each row is explicit. There is no prefix matching: CADI matches names exactly (AU §2, §10.2). Rows are the
     exact `ArrangementClip` names plus the Reforged `music_key`s, and each row's note says where it comes from.
7. **Names match exactly and are case-sensitive.** An unknown name logs `[audio] unknown event <name>` once per name and
   is silent. Known-silent names log nothing: `Stop_conveyor_belt_loop`, the three `* Volume` events (native-only;
   from Lua they'd silence a group, so ignoring them is safer), `bsg_splash`/`slot_accept`.
8. **Gain**, linear: `out = sample × voiceGain × ramp × group[g] × master`, hard-clipped to [-1, 1] after the sum.
   - CADI voices: `voiceGain = channel_volume × sweetener spread`, and `group[g] = level[g]`, linear 0..1 (AU §10.6:
     no root gain and no Master Bus factor).
   - Direct voices: `group = min(1, sfxLevel)` (TSound clamp).
   - `master` is 0 when muted, else 1. Mono files feed both channels. There is no pan or pitch.
9. **Retrigger policy (inferred, each rule one `if`):**
   - One-shots always add a voice.
   - A loop SFX that is already running restarts (AU §7.3).
   - An environment or music event equal to the one playing, and not fading out, is ignored, so `main_menu` after
     Options doesn't restart the theme. A `start` while the same arrangement is still in its intro is ignored too.
   - A crossfade fades the old track out over the new clip's source fade and the new one in over its master fade, else
     1000 ms each (a stated 0 is a cut; AU §10.6); the arrangement's own fade is not used.
   - `Stop_Music` fades out over the playing clip's master fade, else 1000 ms (whether the original fades is not known).
10. **`port_music1..3`** are the `C3 Gameplay 1..3` stem cues (AU §10.4). Each stem is a few clips on the cue's
    timeline with its own start, length and volume (`docs/re/audio-port-music.md`), and the whole cue loops at its
    length. `PortWindow::changeMusic` sends `Stop_Music` and, 10 to 15 s later, a random `port_music<n>`, and repeats
    on 40 to 60 s gaps, with the original's RNG draws.
11. **Audio has its own RNG stream** (seeded with seed+3 in `App`) for sweeteners, the `launch` layer picks and the
    sweetener volume spread, so audio can't shift the game's rolls. Fireworks keep `fxRange`.
12. **Don't keep the factory's pause past the factory.** When `FactoryWindow` is destroyed, it clears a game pause it
    set, so quitting from the pause dialog never leaves music frozen. The original leaves CADI paused (AU §10.7); this
    is a deliberate deviation.

## 3. Types, ownership and lifetimes (`src/audio/`)

| Type | Role | Owner, lifetime |
|---|---|---|
| `Pcm` | int16 interleaved, 1 or 2 channels, frame count. `decodeOgg(bytes) -> optional<Pcm>` | `SoundCache` |
| `SoundCache` | `path -> unique_ptr<Pcm>` and `path -> bytes`, never evicted, misses logged once, optional prefetch worker | `Audio` |
| `OggStream` | `stb_vorbis*` over cached bytes. `read(float*, frames)`, `rewind()`, resamples 8 to 96 kHz | its `MusicTrack` |
| `Source` | `{const Pcm*; OggStream*}`, one of the two set: what a voice or stage reads | value |
| `Voice` | source, position, loop, voiceGain, ramp, `Group`, `cadi` flag, `tag` (event index) | fixed `std::array<Voice, 64>` in `Mixer` |
| `MusicStage` | sources (at most 9), `loop`, `startFrame`, `lengthFrames`, `gain`, placed on the track's timeline | `MusicTrack` |
| `MusicTrack` | stages, track ramp, `endFrames` / `loopFrames`, `done` | built on the main thread; the `Mixer` holds raw `current`/`fading`; `Audio` owns them in `tracks_` |
| `Mixer` | voices, two track slots, `group[4]`, `master`, `systemPaused`, `cadiPaused`, `musicPaused`. `render(float*, frames)` | `Audio` |
| `EventTable` | `resolve(name) -> {def, index, part, fades}`, aliases resolved to `{arrangement, part}`; `EventTable::empty()` | static data from a game's `.inc` |
| `Audio` | the service: `event(name)`, `playFile(path)`, `setLevels(sfx, music, env, muted)`, `setForeground(bool)`, `setGamePaused(bool)`, `update(nowMs)`, `dump()`, plus the file-driven music calls (§4) | `App`, declared before `ScriptContext` and destroyed after it |
| `platform::AudioDevice` | opens the SDL stream, callback -> `Mixer::render`, `lock()` RAII (no-op in pump mode) | `App`. Destroyed first, which stops the callback before anything it reads |

- **Groups:** `Sfx` (CADI sfx), `Env`, `Music`, `Direct` (TSound groups 0 and 1000). `?group=1001` maps to `Music`.
  Other `?group=` values map to `Direct`.
- **Pause classes:** `systemPaused` freezes every voice and track. `cadiPaused` freezes the voices with `cadi=true` and
  all tracks. `musicPaused` freezes the tracks only. A frozen voice keeps its position and outputs nothing.
- **Voice cap:** if all 64 voices are busy, the new one-shot is dropped and logged once. The original has no cap, but
  the array must not allocate.
- **Tags and stopping:** voices from an event carry the event's table index. `Stop_X` ramps tag X to 0 over the
  release ms (immediately when the event has no envelope), then frees it. The environment owns the bed voice (tag =
  env). Its sweeteners are untagged one-shots that run to their end.
- **Script access:** `ScriptContext::audio` (`audio::Audio*`, set by `App`; null in unit tests). `script::soundEvent(ctx,
  name)` (`src/script/AudioBridge.h`) is the one entry point every call site uses. With no audio service it keeps a
  logged `[unimplemented]` stub.
- **UI access:** the `WindowManager` sound hook calls `Audio::playFile`. `setVolumeHook` previews slider levels live
  (`App.cpp` maps the slider to the sfx, music or environment level). `SaveVolumes` stores the three sliders and then
  re-applies the stored prefs.
- **Prefs:** `script::applyAudioPrefs(ctx)` reads the four prefs (defaults when there is no user) and calls `setLevels`.
  It runs at boot after users load, after any native that changes the current user, in `SaveVolumes` and when
  `ToggleSoundMute` unmutes. Muting only zeroes the master, so unsaved slider levels survive it; unmuting re-applies the
  stored prefs (AU §10.7 `BSGSoundMute`).

## 4. Event semantics (main thread, `Audio::event`)

| kind | action |
|---|---|
| direct (`ui_click`, `firework*`, paths) | one-shot `Direct` voice, `cadi=false` |
| `oneshot` | one-shot `Sfx` voice from `files[0]` |
| `oneshot_layered` (`launch`) | a RandomSelector (AU §10.6): one random file per trigger from the event's files, never the one picked last time, and never a child whose voice is still playing |
| `loop` | restart the tagged loop with an attack ramp from `envelope_ms` |
| `ambience_loop` | if it's the current env, ignore. Otherwise release the old bed and start the bed looping with an attack ramp. Arm the sweetener timer: every `U(interval)` ms of *audio time*, play one random `intermittent_files` entry as an `Env` one-shot at gain `channelVolume × (1 - volvary·u)` (inferred, AU §10.6) |
| `music` / alias | build a `MusicTrack` for the arrangement and part (below), then crossfade (decision 9): the old track goes to `fading` and ramps to 0, the new one ramps from 0. A third switch during a fade cuts the old `fading` track |
| `stop` | `Stop_Music`: fade the current track. `Stop_Environments`: release the bed and disarm the sweeteners. `Stop_<loop>`: release the tag |
| `builtin` `Pause`/`Unpause` | `setGamePaused(true/false)` |
| unknown / `unknown` kind | log once, silent |

**Music parts.**
- `start` of a port arrangement is one clip on a timeline (AU §10.5): the sting plays once from 0 with the `under` layer
  beneath it until the hand-over, the layers then play once, `portmusicFADE` follows them, and the track ends at the
  clip's length (or loops at it when `music.tsv` says so). The hand-over, the layer repeats and the clip volumes are read
  for `lima` only and assumed for the other ports. An arrangement without a sting (`map_`) loops its layers; a single
  file loops (`main_menu_`, `reykjavik_`).
- `start` of a `port_music` cue builds one stage per stem clip and loops the cue at its length.
- `intro`: the intro plays once, then the body loops. The body clip actually starts at 355,164 samples rather than at
  the intro file's end (AU §10.5); the engine uses the file's end.
- `outro`: the ending plays once, then the track ends.

"Audio time" advances in `update(nowMs)` only while the relevant pause class is off. Sweeteners therefore wait out a
pause, and headless runs give identical timings.

**File-driven music (Chocolatier 1 and 2).** These games have no event table (`EventTable::empty()`). `Audio` offers
`playMusicFile(path, loop, fadeIn, fadeOut)`, `stopMusic(fade)`, `setMusicPaused`, `setAmbientFile(path)` (a bed in the
environment group; `""` stops it, the bed already playing is left alone, a replacement crossfades over 300 ms, a guess)
and `musicPlaying()`. A missing file logs once and leaves the music stopped. The state machines on top are
`Choc2Music` (`SetMusicState`, `SetAmbient`, `PauseMusic`, `ResumeMusic` in `src/games/choc2/`; the rotation uses the
audio RNG; fades and the choice after a one-shot are inferred, `docs/re/choc2-audio.md`) and `Choc1Music`
(`src/games/choc1/`, `docs/re/choc1-natives.md`).

## 5. Order of work per frame

1. SDL events (focus: `World::setForeground` -> `Audio::setForeground`).
2. The Lua tick and window messages: `SoundEvent`, buttons and natives call into `Audio`. Each call takes the lock
   once, briefly.
3. `World::tick` (factories: `pauseGame` -> `setGamePaused(true)` when state != 0, `resumeGame` -> `false`).
4. `Audio::update(nowMs)`: sweetener timers, reaping done tracks (unlink under the lock, free outside it), and in pump
   mode `Mixer::render` into the discard buffer.
5. Draw. With a device, the callback runs whenever SDL asks.

## 6. Logs, dump and headless

- Each `event()` writes one line through `diag::logLine` (the `--log` file only, never stdout):
  `[audio] <name> -> <action> <files>`, for example `[audio] map_screen -> music map_ (alias) fade 3000/1000`
  (master fade in, source fade out). The file-driven calls write `[audio] music file <path> loop|once fade <in>/<out>`,
  `[audio] music stop fade <ms>` and the matching `ambient` lines.
- `Audio::dump()` is appended to the script driver's `dump`, so `expect-dump` can assert on it. Lines are sorted, with
  three decimals:
  ```
  audio levels sfx=0.750 music=0.650 env=0.500 mute=0 pause=none|focus|game|focus+game
  audio music main_menu_ stage=1/1            (or: audio music none; " paused" is appended while music is paused)
  audio env zurich                            (or: audio env none)
  audio loop travel_plane
  audio fired buy=1 cadi/ui_click.ogg=3 launch=2 ...   (cumulative counts per name or path)
  ```
- **Missing CADI files:** if no event file exists under `cadi/audio/` (or its fallback) at boot, `App` logs once:
  `[audio] cadi/audio/ not found (vanilla data): CADI events are silent`. Direct sounds still play. The vanilla game
  gets its Oggs from its `.CAD` container through `CadSource` as `cadi/audio/<name>` (AU §10.9), so the line appears only
  when that container is missing.

## 7. Tests

- `tests/test_audio.cpp` (doctest, synthetic and real `Pcm`/Ogg): decoding (shape, rejected rates, corrupt input), the
  cache and its prefetch worker; the mixer (exact gains, mono and stereo, loop wrap, ramps, release, the 65th voice,
  pause classes, timeline stages, `endFrames`/`loopFrames`, crossfade sums, slot limits); `OggStream` equals
  `decodeOgg` sample for sample, rewind and resampling; the event table (exact lookup, every alias targets an
  arrangement, music roles use only the arrangement's own files, every `Stop_` target exists, every tsv row is in
  the table, clip timing, `port_music` timelines); `Audio` (one-shots, layered picks, loops, environments,
  seed-deterministic sweeteners that wait out a pause, the dump, music builds and crossfades, skip-decode timing); and
  the Chocolatier 2 file-driven calls. Real-file cases are skipped without `--assets`.
- `tests/test_audio_wiring.cpp`: prefs conversion, the `SoundEvent` native, the no-audio stub, prefs reaching the mixer
  (user selection, `ToggleSoundMute`), pump mode on a fake clock, identical `[audio]` logs for equal seeds, the mixer lock
  discipline, and the focus/game pause truth table.
- `tests/test_choc2_music.cpp` and `tests/test_choc1_audio.cpp`: the Chocolatier 2 and Chocolatier 1 music state
  machines on a fake clock.
- `tests/check_audio.py` (CTests `reforged_audio`, `vanilla_audio`; headless scenarios): menu music, rollover and click
  events, a slider drag changing one level live, the mute box, OK storing the level, focus loss and regain; a game run
  through the chocolate factory (events, a `Stop_*` ending a loop, game pause surviving focus regain); Help and back
  keeping the menu music; port, map, flight and landing music and environments; two identical runs giving identical
  `[audio]` lines. Every run requires no `[lua-error]` and no `[unimplemented] SoundEvent` line.
- `tests/check_audio_names.py` (CTest `audio_names`): every literal `SoundEvent("...")` in the Lua trees and every port
  `cadikey`/`music_key` is in the table or on a documented-silent list.
- `tests/check_choc2_audio.py` (CTest `choc2_audio`): Chocolatier 2 menu, port, travel and mini-game music from the
  `audio ...` dump lines and the `[audio]` log.
- `audio_events_up_to_date`: `AudioEvents.inc` matches its inputs.

What tests cannot check is how it sounds. `AGENTS.md` lists what to listen for on a real device.

## 8. Behaviour that is a guess

The fade timings were set by ear against the original and may be off, as may the other inferred choices below. Each is a single branch.

- The retrigger rules of decision 9, and whether `Stop_Music` fades or cuts (AU §11).
- The crossfade lengths when a clip states none (1000 ms), and the 300 ms bed crossfade of the file-driven path.
- The sweetener volume spread, and which layers of `launch` play (one file per trigger).
- Port arrangement timings other than `lima` (hand-over, layer repeats, clip volumes), and where an intro's body starts.
- The aliases that map Reforged `music_key`s to arrangements, where no exact `ArrangementClip` name exists.

## 9. Limits

- No real CADI: no beat-quantised starts and no general timeline engine; arrangements are the fixed stage layouts of
  `music.tsv` and `audio-port-music.tsv`.
- No per-voice pan or pitch.
- No smoothing ramp on slider changes: gain changes once per callback block, so a faint zipper is possible.
- The SDL device itself is never paused; the mixer flags do it.
- Lock hold time must stay pointer-sized: nothing may decode, log or allocate inside the lock.
