# Great Chocolate Chase (GREATCC) audio: VSoundManager, VSound, fades, voice-over, volume prefs

Tags: **F** = fact (decompile, symbols, file bytes; cited), **I** = inference, **U** = unknown. Sources: `re/greatcc/decomp/{VSoundManager,VSound,VSoundFade,VSoundCallBack,VVOCallBack,VSoundManagerUpdate,TSound,TSoundInstance,TSoundStream,TSoundManager,TSoundManagerData,TSettings,TOptions,TButton,VButton,VSprite,VAssetManager,RAdapt,RAdaptSoundTask}.c`, `re/greatcc/GreatChocolateChase_i386` (float constants read from the data sections; the disassembly dump used for loop-flag arguments is `re/greatcc/scratch/audio/text.s`), the Mac `assets/` tree (`assets.xml`, `scripts/*.lua`), and the Windows `pfp_extracted` tree. Chocolatier 3 comparison: `docs/re/audio.md`, `docs/re/choc2-audio.md`, `docs/re/audio-events.tsv`, `re/mac/decomp/`. Addresses are Ghidra/file VAs (Mac i386). The Windows exe is not decompiled; Windows-only behaviour is **U** unless stated.

## 0. Big picture

* **F** GREATCC has no CADI, no `SoundEvent`, no event table, no `cadi/` folder. Every sound is a plain Ogg file played by name. The Choc3 audio model (`docs/re/audio.md`: CADI mixer + `TSound` direct path) does not apply. `grep SoundEvent` over the GREATCC decompile and Lua finds nothing; `strings.tsv` has no CADI strings.
* **F** Two layers. Pflib `TSound` / `TSoundInstance` / `TSoundStream` / `TSoundManager` (shared SDK classes, Mac Sound Manager backend in this build) do decoding, voices and per-group volume. On top, the game class `VSoundManager` (singleton, `_gSoundManager`) keeps the play lists, the music slot, the voice-over queue and the fades, and registers the Lua natives.
* **F** Shipped assets: 37 Ogg files, 5.1 MB: `audio/music/*.ogg` (7, each one mono 48 kHz, 68-90 s) and `audio/sfx/*.ogg` (30, 44.1 kHz). The Windows `assets.pfp` holds the same 37 names (`re/greatcc/pfp_extracted/audio`). No `audio/vo`/voice files exist. **F** (`find audio`).
* **F** The shipped Lua never calls `PlaySFX`, `PlayMusic`, `PlayVO`, `PurgeSounds` or `PurgeVOQueue`. All sound playback is started from native C++ (sections 5 and 6). Lua only supplies file names (button `sound=`, location `music_01=`), calls `PauseSounds`, `SaveVolumes`, `ToggleSoundMute`.

## 1. Lua natives (audio)

Registered into the global table of the window manager's script state (`TWindowManager::GetScript()+0x18`, `lua_settable` on LUA_GLOBALSINDEX) by the `VSoundManager` constructor (0x7e85c), which runs from `main` right after `RTransitionManager::CreateTransitionManager` and before the cursor is set (`_free/m.c:155-156`). **F**

| Lua global | Native | Args (LPCD) | Returns | Shipped Lua use |
|---|---|---|---|---|
| `PlaySFX(name, loop)` | `VSoundManager::PlaySFX(str,bool)` | string, bool | `shared_ptr<TSoundInstance>` (userdata, class registration not examined **U**) | none |
| `PlayMusic(name, loop, fade)` | `PlayMusic(str,bool,bool)` | string, bool, bool | TSoundInstance | none |
| `PlayVO(name, interrupt, skipIfBusy)` | `PlayVO(str,bool,bool)` | string, bool, bool | nothing | none |
| `PurgeSounds()` | `PurgeSounds()` | - | - | none |
| `PurgeVOQueue()` | `PurgeVOQueue()` | - | - | none |
| `PauseSounds(pause, group)` | `PauseSounds(bool,int)` | bool, int | - | `pause.lua:40,146`, `prelevel.lua:267,329` (all with `kSFXSoundGroup` = 1000, `style.lua:25`) |
| `ToggleSoundMute()` | `TSettings::ToggleSoundMute` (0x3c1f6) | - | - | `options.lua:306` (checkbox `options_muteall_checkbox`) |
| `SaveVolumes()` | `TOptions::SaveVolumes` (0x2b8e6) | - | - | `options.lua:348` (OK button) |
| `playsound(name, loop)` | `VSoundManager::PlaySFX` bound by `VSprite::NewScript` (0x49c3a) into each sprite's own script state | string, bool | TSoundInstance | none found in shipped data (**I**: meant for `.anm`/sprite scripts) |

* **F** All the bool args are `lua_toboolean`-style (LPCD `Call<VSoundManager,...>`), so a missing arg is false. (I, not decoded in detail.)
* **F** `kMusicSoundGroup = 1001`, `kSFXSoundGroup = 1000` are defined in `scripts/style.lua:25-26`; they are the group ids used by C++ (section 4).
* **F** `lua-undefined-globals.tsv` lists `PauseSounds`, `SaveVolumes`, `ToggleSoundMute`, `kDefaultButtonSoundOut`. The first three are the natives above; `kDefaultButtonSoundOut` (used twice in `pause.lua:111,135` as a button `sound=`) is defined nowhere, so those two buttons have `sound = nil` and are silent (**F**: grep).
* `PlaySFX`, `PlayMusic`, `PlayVO`, `PurgeSounds`, `PurgeVOQueue` do not appear in `lua-undefined-globals.tsv` only because the Lua never reads them; they exist as globals anyway.

## 2. Class layout and lifecycle

### VSoundManager (0x40 bytes, `operator new(0x40)` in `CreateSoundManager` 0x7ec5c) **F**

| Offset | Member | Meaning |
|---|---|---|
| +0x04 | `list<shared_ptr<VSound>>` ACTIVE | every SFX/music/playing VO |
| +0x0c | `list<shared_ptr<VSound>>` VOQUEUE | VOs waiting (not yet playing) |
| +0x14 | `shared_ptr<VSound>` CURMUSIC | the current music track |
| +0x1c | `shared_ptr<VSound>` CURVO | the VO that is playing now (null = idle) |
| +0x24 | `VSoundManagerUpdate*` | per-frame task |
| +0x28 | `map<str,vector<str>>` | constructed and destroyed, never read in any decompiled function (**I**: unused leftover) |

* `CreateSoundManager` deletes an existing singleton first. `DeleteSoundManager` deletes and nulls it (called at shutdown `_free/m.c:403`). The destructor orphans the update task, clears ACTIVE, calls `PurgeVOQueue`. **F**
* **F** `VSoundManagerUpdate` (0x7e688) is a `TAnimTask` adopted by `TPlatform::AdoptTask`; `SetDelay(0x10 ms, true, true, false)`: it runs every 16 ms. `Animate` calls `VSoundManager::Update()` when the singleton exists and returns true while it does (task dies when the manager is gone).
* **F** `GetSound(name, group)` = `TSound::Get(name, cache=1, group)`; if the result is null and the byte `SOUNDMANAGER_REPORT_MISSING` (data 0x215261, initial value 1 **F** read from `__data`) is non-zero it logs `VSoundManager: Asset Missing (%s)` with `WriteDbg`. Nothing is raised; callers silently skip.
* **F** `IsSoundMuted()`: reads int pref `curuser` (default -1, user -1), then `soundoff` (default 0) for that user; muted when it equals exactly 1.

### VSound (0x24 bytes, held in `shared_ptr` through `VSound::Create`) **F**
Fields: +4 weak self (`GetRef`), +0x0c owned `TSoundCallBack*`, +0x10 `shared_ptr<TSound>`, +0x18 `shared_ptr<TSoundInstance>` (null until played), +0x20 **done flag** (byte, 0 at construction).
* `Play(loop)`: if the TSound exists, `TSound::Play(loop)` -> instance stored in +0x18, then `TSoundInstance::SetCompleteAction(+0x0c callback)`. Returns the instance. Calling it again replaces the instance.
* `Pause(b)` -> `TSoundInstance::Pause`; `GetGroupId()` = instance group or **0** when no instance (`TSoundInstance::GetGroupId` returns -1 when the stream is gone); `Kill()` = `TSoundInstance::Kill`; `AdoptCallBack` deletes the previous callback and takes ownership of the new one.
* VSound is only a tiny wrapper: the done flag is the single piece of lifecycle state.

### Callbacks
* `VSoundCallBack(weak_ptr<VSound>)` (0x80194), `OnComplete`: lock the weak pointer, if alive set its done flag := 1. `VVOCallBack` (subclass, 0x80280) `OnComplete`: run the base, then **reset the manager's CURVO to null** (so the next queued VO can start on the next `Update`). Both are `TSoundCallBack`s (pflib: holds a map of instance->TSound registrations, fires `OnComplete(instance, sound)` from `TSoundData::StreamFinished` when a non-looping stream ends). **F**
* **F** `PlaySFXWithCallBack` etc. create the default `VSoundCallBack` themselves when the caller passes null; a caller-supplied callback replaces it. No shipped code supplies one (no caller of the `*WithCallBack` variants other than the wrappers).

### Per-frame `Update()` (0x7f058), every 16 ms **F**
1. **VO start**: if CURVO is null and VOQUEUE is not empty: push the front VSound onto ACTIVE, pop it from the queue, set CURVO to it, `VSound::Play(loop=false)`.
2. **Reap**: walk ACTIVE; every VSound whose done flag is non-zero is `Kill()`ed and erased. Entries with a null pointer or done==0 stay.

## 3. Playing: exact behaviour

### PlaySFX(name, loop) (0x7efe8 -> `PlaySFXWithCallBack` 0x7ee12) **F**
1. `GetSound(name, group 1000)`. Null -> return a null instance (silent, no error).
2. Create the default `VSoundCallBack`, create the `VSound`, append it to ACTIVE, give it the callback, `VSound::Play(loop)`. No fade, no volume change (the stream starts at `GetVolume(1000)`, section 4).
3. The sound is **not** deduplicated: calling it N times creates N overlapping voices. No cap on voices anywhere in the game code (pflib limit **U**; **I**: one Mac `SndChannel` per stream).
4. All shipped native callers pass `loop=false` except `BZeppelin::Pickup` (`zepplin_loop.ogg`, loop=true). **F** (the immediate argument read from the disassembly at each call site; the Ghidra symbolisation of the zero immediate looks odd but is a literal 0).

### PlayMusic(name, loop, fade) (0x7f120 -> `PlayMusic` 0x7f4b4) **F**
1. **Same-track shortcut**: if CURMUSIC exists and has a `TSound` and `TSound::GetName()` equals `name`, return CURMUSIC's current instance immediately. Nothing restarts, no fade, no volume change. (The name stored on a TSound is the path as passed to `Get`, without a `?group=` suffix.)
2. `GetSound(name, group 1001)`. Null -> return null; **the current music is left untouched**.
3. Create the VSound with the default callback, append to ACTIVE, `VSound::Play(loop)`.
4. If `fade == false`: set CURMUSIC's done flag := 1 (it is killed on the next `Update`, up to 16 ms later; the new track starts at once at full volume, so there is a tiny overlap).
5. If `fade == true`: `FadeSound(CURMUSIC, fadeIn=false)` (skipped when CURMUSIC is null), then `FadeSound(new, fadeIn=true)`.
6. CURMUSIC := the new VSound. Return its instance.
* Every shipped call is `PlayMusic(path, true, true)` (loop, fade): `TMainMenu::PostChildrenInit` 0x26dd6 and `ShopWindow::LevelSetup` 0x48508. **F**

### FadeSound(vsound, fadeIn) (0x7ed50) **F**
* Null VSound -> returns false.
* If `IsSoundMuted()`: no fade is created; the VSound's done flag := `fadeIn XOR 1` (a fade-out ends the sound at the next `Update`, a fade-in leaves it as is). Returns false.
* Else spawn `VSoundFade(vsound, fadeIn, 1000 ms)`, adopt the task. **The fade duration is the constant 1000 ms; there is no per-call duration.** Returns true.

### VSoundFade (0x34 bytes, a `TAnimTask`) **F**
* Constructor: target level `v0` := `TSoundManager::GetVolume(group of the instance)` read **once** at construction (= master x group volume; 0 if the VSound has no instance); owns a private `TClock` started now; start time field = 0; if `fadeIn`, immediately `SetVolume(0.0)` on the instance; `SetDelay(16 ms, true, true, false)` (runs every 16 ms).
* `Animate` each tick: stop (return false) if the VSound or its instance is gone or **the VSound's done flag is set**. Otherwise: on the first tick latch the start time (re-latched while it is still 0); `f = min(1.0, elapsed_ms / 1000.0)`; fade-in volume `= f * v0`, fade-out volume `= v0 - f * v0`; apply with `TSoundInstance::SetVolume`. When fading out and `f >= 1`, set the VSound's done flag := 1 (so `Update` kills it, if it is still in ACTIVE). Returns `f < 1.0` (so the task ends after the tick that reaches 1.0).
* **Curve**: linear in the *volume value* passed to `TSoundStream::SetVolume`, which then maps it to gain by squaring (section 4). So the audible gain curve of a fade is quadratic in time: gain = (f * v0)^2 for fade-in. **F** (constants in section 4).
* Fade-in always ends exactly at `v0` captured at the start; a slider move during the fade does not retarget it (stale target) **F**; any later `UpdateAllVolumes` snaps all streams to the current `GetVolume(group)`.
* There is no fade-out when the app closes a music track by other means; `PurgeSounds` and kills are immediate.

### Voice-over (queue; unused by the shipped game)
* **F** `PlayVO(name, interrupt, skipIfBusy)` (0x7f794 -> `PlayVOWithCallBack` 0x7f5ac). `GetSound(name, group 1000)` (VO shares the **SFX** group). Proceed only if the sound loaded and (`skipIfBusy == false` or VOQUEUE is empty). Then: if `interrupt` and (VOQUEUE non-empty or CURVO set) -> `PurgeVOQueue()`. Create the VSound with a `VVOCallBack`, append to **VOQUEUE** (not ACTIVE, not played yet). `Update` starts it when no VO is playing (section 2). So VOs play strictly one after another, non-looping; the next one starts on the first 16 ms tick after the previous one's `OnComplete`.
* `PurgeVOQueue()` (0x7f532): if CURVO is set, `Kill()` it and null CURVO (the dead VSound stays in ACTIVE until its flag is set and `Update` reaps it; a killed stream may never fire `OnComplete`, so it can linger there **I**); then clear VOQUEUE.
* **F** No C++ caller of `PlayVO` exists, no Lua caller, and the game ships no VO audio. **RVOQueue** (`re/greatcc/decomp/RVOQueue.c`: `AddPath`, ctor) exists only as three 1-byte `.eh` clones with no code: dead class. `audio/vo` and `kVO*` do not exist.

### PurgeSounds (0x7e84e) and PauseSounds (0x7e742) **F**
* `PurgeSounds()`: `ACTIVE.clear()` (drops the manager's references; **it does not call `Kill`** on anything). CURMUSIC, CURVO and VOQUEUE are untouched. Called from native code at: `ShopWindow::LevelSetup` (right after the modal windows are popped and before the music starts), both `ShopWindow` destructors (after `VAssetManager::ReleaseAssets`), `TSettings::QuitToMainMenu` (0x3c57a). **I** (stream lifetime): a TSound keeps every live `TSoundInstance` in its own set until the stream finishes or `KillInstance` runs (`TSound::Play` inserts, `TSound::KillInstance` erases and calls `TSoundStream::Kill`); `~TSoundStream` does not kill. So dropping the VSound reference from the list does not by itself silence a looping stream; looping music that was in ACTIVE keeps playing via CURMUSIC, and a one-shot just plays to its end. Whether a faded-out track that was purged from ACTIVE keeps playing silently (volume 0, looping) after its fade ends is **U** (see section 10).
* `PauseSounds(pause, group)`: for each non-null VSound in ACTIVE, if `group < 0` or the VSound's group id equals `group`, `VSound::Pause(pause)`. Shipped uses pass 1000, so SFX (and any playing VO, same group) are paused/unpaused while **music (1001) keeps playing**. VOQUEUE entries are not touched. The pause calls are made by pause.lua when the pause modal is created (`PauseSounds(true,1000)` at file end, line 146) and by its Resume button (`PauseSounds(false,1000)`, line 40); likewise `prelevel.lua` (the pre-level upgrade screen, lines 329/267) and no call in `endlevel.lua`. **F**
  The matching `TSoundManager::PauseAllSounds(pause, group)` exists in pflib and is used by the platform layer (section 7), not by this Lua.

## 4. Channel and volume model (pflib, exact)

### Groups ("type" ids) **F**
* `TSound::Get(path, cache, group)`: an empty path returns null at once. If the path contains `?`, the text after `?` is searched for `group=`; the int that follows **overrides** the group argument, and the string is cut at `?`. The cache key is the string `"%s;%d;%d"` formatted from (path, **1**, group) (string at 0x152a68): the same file at two different groups is two TSounds. `InternalGet` returns an existing one. The `cache` argument is never read (**F**: no use of it in the function's disassembly); every sound is fully decoded to memory (below).
* Group ids in use: **1000** SFX (VSoundManager `PlaySFX` and `PlayVO`; `assets.xml` `type="1000"`), **1001** music (`PlayMusic`), **0** `TButton` UI sounds (`TButton::SetSound` -> `Get(path, 1, 0)`), **-1** (0xffffffff) for `RAdaptSound` and `TAssetMap` paths that ask for none (`RAdaptSoundTask::Animate`, `TAssetMap::AddAsset`).
* `TSoundInstance::GetGroupId()` returns the stream's group (`+0x4c`).

### TSoundManager (0x24 bytes; `TSoundManagerData` 0x28) **F**
* Master volume `+0x0c` of the data object, **initial 1.0** (ctor stores 0x3f800000). Per-group map `<int, float>` (`SetTypeVolume`).
* `GetVolume(group)`: if the group is **not** in the map it returns `master` alone (unknown groups are at full master volume, not 0). If it is in the map: `master * map[group]`. The Choc3 `TSoundManager::GetVolume` (`re/mac/decomp/TSoundManager.c:167`) has the same find-then-return-master shape, which contradicts the statement in `docs/re/audio.md` s.4 that an unset group reads 0.0 (flagged for that doc's owner).
* `SetTypeVolume(group, v)`: store, then `UpdateAllVolumes`. `SetVolume(v)`: set master, `UpdateAllVolumes`. `UpdateAllVolumes`: for every registered stream `SetStreamVolume`.
* `SetStreamVolume(stream)`: `v = GetVolume(stream group)`; then `TSoundStream::SetVolume(v +/- 0.05)` followed by `TSoundStream::SetVolume(v)` (the first call adds 0.05 when `v < 0.5`, subtracts 0.05 otherwise; constants 0.5 and 0.05 read from `__literal4`). A nudge-then-set workaround; the final value is `v`. **F**
* `PauseAllSounds(pause, group)`: `TSoundStream::Pause(pause)` for every registered stream (all when group == -1, else only that group); on unpause also `UpdateAllVolumes`. `AppActivate(active)`: ignores no-ops; deactivate -> `SystemPause(true)` on each stream; activate -> `SystemPause(false)` + `UpdateAllVolumes`. `+0x22` = app-active flag (initial 1). **F**
* The manager is created by `TPlatform::InitSound` (0x5d70), stream list = vector<shared_ptr<TSoundStream>>; `HandleNotifications` (once per platform tick, `TPlatformData` main loop) reaps finished streams and restarts looping ones (`TSoundManager::InternalHandleNotifications`). **F** (outline), details **U**.

### Stream gain (Mac backend; **different from Choc3**) **F**
`TSoundStream::SetVolume(v)` (0x100348): if `v < 0.0` (or NaN) gain := 0; else if `v <= 1.0` gain := `v * v`; else gain := 1.0. The raw (unsquared, clamped) `v` is stored in the stream data (+0x50); the squared gain is sent to the Mac Sound Manager as a `volumeCmd` (command 0x2e) with the left and right 16-bit words both equal to `int(gain * 256.0)` (256.0 read from `__literal4`; 0x100 is unity). So:
* **audible gain = (master x groupVolume)^2**, a quadratic curve. A slider at 0.75 is gain 0.5625 (about -5 dB); at 0.5 it is 0.25 (-12 dB). Choc3's Mac `TSoundStream::SetVolume` (`re/mac/decomp/TSoundStream.c:89`) clamps to [0,1] and stores it linearly (no squaring). The Windows GREATCC backend is **U** (the exe imports `DSOUND.dll`/`WINMM.dll`; not decompiled), **I**: the same SDK source with a DirectSound backend probably maps the same `v` through the same formula, but this build's quadratic mapping is Mac-backend code, so treat the exact Windows curve as unverified.
* `TSoundStream::Play` also applies this gain at start (it reads `GetVolume(group)` itself), so a sound starts at the right level without waiting for `UpdateAllVolumes`; if the app is inactive (`TSoundManager+0x22 == 0`) the stream is only flagged pending and starts when the app is activated (`TSoundStream::Play` 0x101e80).

### Loading and decoding (`TSound::Get`, 0x3748) **F**
* The path must contain the substring `.ogg` (checked with `str::find`, string at 0x1529d0) or the result is null **with no log**: e.g. `hiscoresubmit.lua:334` has `sound = "audio/sfx/buttonclick"` (no extension), so that one button is silent (I: assuming `TButton` passes the string unchanged, which `TButton::SetSound` does).
* The file is opened with `_pf_open` (pack-aware); not found: `WriteDbg("OGG file not found: %s")` -> null. `ov_open_callbacks` failure: logs `OGG file could not be opened` -> null.
* Decoded size: info = channels (`+4`) and rate (`+8`); length = `ov_time_total`; buffer bytes = `int(2 * (length + 0.1) * channels * rate)` (0.1 is a double constant, **F**); 16-bit signed little-endian PCM via `ov_read` (word 2, signed 1, bigendian 0), the file handle closed after decode. **Every sound, including the 85 s music tracks, is fully decoded into RAM.** Music is mono 48 kHz, i.e. about 8 MB of PCM per track.
* `TSound::Play(loop)` (0xd5180): null data -> log `Play of uninitialized sound`; no sound manager -> log `TSound error: No Sound Manager`. Seeks the decoder to 0, creates a `TSoundStream` (0x78 bytes; the loop flag goes in the constructor), a `TSoundInstance` (0x24 bytes), inserts it in the TSound's instance set, `TSoundStream::Init(rate, channels, ...)`; then `TSoundStream::Play(false)` if `loop` or the sound is **at least 1.0 s** long (double constant 1.0, **F**), else `TSoundStream::Play(true)` (meaning of the flag **U**, **I**: "short sound, play from memory in one go"). Registration with the manager happens in the stream constructor.
* `TSound::Kill()` kills every instance of that sound; `TSoundInstance::Kill` -> `TSound::KillInstance` -> removes it from the set and `TSoundStream::Kill(false)`.

## 5. Which sound plays when (the "event" table)

There are no named events. The call sites below are the whole table (**F**: each string literal is in the cited function; each `PlaySFX` call is `loop=false` unless noted). Files are under `audio/sfx/`. Trigger descriptions are the function names plus **I**.

| File | Played by (function) | Trigger (I) |
|---|---|---|
| `buttonclick.ogg` | Lua `sound=` of most button styles (`style.lua` `kDefaultButtonSound`, `chocshopstyle.lua`, `hud.lua`, `pin.lua`: ~25 uses); `VMachineButton::OnClick`; `VButton::OnClick` (when the button's `sound` style string is non-empty) | click; UI buttons go through `TButton` (group 0), in-level `VButton` through `PlaySFX` (group 1000) |
| `menu.ogg`, `score_board.ogg` | `VCustomer::OnArrival` | customer arrives at the counter (two sounds) |
| `customer_arrives.ogg` | `ShopWindow::UpdateCustomers` | new customer spawned |
| `customer_leaves.ogg` | `VCustomer::ServeCustomer`, `VCustomer::Payout` | served / paid |
| `chomp.ogg` | `VCustomer::ComplimentaryService` | free sample eaten |
| `master.ogg`, `goal.ogg` | `ShopWindow::AddCombo`, `ShopWindow::OrderComplete` | combo / order complete (both files appear in both functions) |
| `box_station.ogg` | `VDelivery::Activate` | delivery station |
| `disposal.ogg` | `VDisposal::Activate` | trash |
| `sauce_arm_extend.ogg` | `VDrink::Activate`, `VSauce::Activate` | dispenser arm out |
| `sauce_spray.ogg`, `sauce_arm_retract.ogg` | `VDrink::OnResume`, `VSauce::OnResume` | after the minigame |
| `sauce_clink.ogg` | `VDrink::Update`, `VSauce::Update` | once, when the dispenser animation reaches frame 23 (> 0x16) (guarded by a per-object flag) |
| `infusion_machine.ogg` | `VInfuse::Activate` | infuser |
| `barrel_trap_door.ogg` | `VInfuse::Drop`, `VPress::Drop` | drop |
| `machine_noise.ogg`, `glug_machine.ogg` | `VIngredient::Activate` / `OnResume` | ingredient machine |
| `pickup_drop.ogg` | `VMachine::AttachChocolate`, `VMachine::DetachChocolate` | chocolate moved |
| `robot_arm.ogg` / `spray.ogg`, `robot_arm_2.ogg` | `VMixer::Activate` / `OnResume` | mixer |
| `stamp.ogg` | `VPress::Unlock` | press |
| `topping_machine.ogg`, `topping.ogg` | `VTopping::Activate` | topping machine |
| `purchase_upgrade_button.ogg` | `VUpgradeTab::PurchaseUpgrade` | upgrade bought |
| `pickup_zepplin.ogg` | `BZeppelin::GrabBox` | zeppelin crate grabbed |
| `zepplin_loop.ogg` | `BZeppelin::Pickup`, **loop=true** (0.71 s file) | zeppelin passing |
| `macaw.ogg`, `negative.ogg`, `phonograph.ogg` | **none**: listed in `assets.xml` only; no decompiled string reference and no Lua reference | unused (**I**) |

**Music** (all `PlayMusic(path, loop=true, fade=true)`):
* `audio/music/theme.ogg` (84.9 s): `TMainMenu::PostChildrenInit`, every time the main menu window is built. After a `QuitToMainMenu` (which calls `PurgeSounds` first) the same-track shortcut makes it a no-op if `theme` is still CURMUSIC; otherwise it crossfades (1 s). **F**
* Per location: `ShopWindow::LevelSetup` fetches the location style table (`GetLocationStyle`), reads string key **`music_01`** (default `""`, so an empty name is `GetSound`'s null path and nothing changes) and plays it. `scripts/game.lua:144-199`: `location_grytviken` -> `grytviken.ogg` (68.3 s), `location_iquitos` -> `iquitos.ogg` (90.0 s), `location_gaberoun` -> `gaberoun.ogg` (74.6 s), `location_krakatoa` -> `krakatoa.ogg` (81.2 s), `location_namche` -> `namche.ogg` (73.5 s), `location_kamchatka` -> `kamchatka.ogg` (69.3 s). Only `music_01` is read; no `music_02`. **F**
* **No other screen sets music**: the world map, pre-level, end-level, options, hiscore etc. never call `PlayMusic`, so they keep whatever is playing (the menu theme, or the last location's track). Level restarts in the same location hit the same-track shortcut: the track keeps playing uninterrupted. **F** (grep: only the two `PlayMusic` call sites).
* Not-in-`assets.xml`: music is never preloaded; its first `GetSound` decodes it synchronously (a load hitch of an 8 MB decode) at the start of the level / main menu. **F**/**I** (hitch size).

## 6. assets.xml sound entries (`VAssetManager::LoadFromNode` 0x842c0)

* **F** `<assets>` root; children `sound`, `texture`, `animatedtexture` (others ignored). `<sound file="..." type="..." memory="...">`: `file` = asset key and path (relative to the assets root); `type` = `to_int(attr)` = the **group id** (all 30 shipped entries use `1000`); `memory` is compared to the string `"true"` and passed as the `cache` argument to `TSound::Get`, which ignores it (every sound is decoded in memory anyway). A missing `type` is `to_int("")` = 0 (group 0). The loaded TSound is registered with `AddAsset(file, sound)`.
* **F** Shipped list: exactly the 30 files in `audio/sfx/` (all with `memory="true"`, `type="1000"`); no music entries, no VO entries. They are preloaded into the cache keyed `"<path>;1;1000"`, which is the key `PlaySFX`/`GetSound(.., 1000)` later looks up, so SFX plays do not decode at play time. (**F** key format; the preload happens when `VAssetManager::LoadAssetFile` runs, level load.)
* **F** `RAdapt::ParseSoundNode` (0x69c08, `<sound>` node of an adapt description): children `loop` (`==1` -> looping), `delay` (int ms, sets the task delay), `source` (path). The task (`RAdaptSoundTask::Animate` 0x6beb6) gets `TSound::Get(source, 1, -1)` and plays it **once at first run** (loop flag from the node). Group -1, so it is only scaled by master. No shipped data uses `<sound>` (**F** grep).
* `TButton` style keys `sound` / `rolloversound` (shared SDK, see `docs/re/audio.md` s.4 "Buttons"): path fetched at style parse with `TSound::Get(path, 1, 0)`; played with `TSound::Play(false)` from `TButton::StateUpdate` (0xa9d66: when entering a non-pressed state different from the old state, the slot's sound is set, and the button is enabled) and `TButton::Press` (the click slot). `kDefaultButtonSound` = `audio/sfx/buttonclick.ogg` (`style.lua:22`). `VButton` (in-level buttons) reads style key `sound` (default empty -> silent) and plays it through `PlaySFX`. **F**

## 7. Pause, focus and mute

* **Game pause** (pause modal, pre-level screen): `PauseSounds(true, 1000)` pauses all active SFX voices; Resume/close calls `PauseSounds(false, 1000)`. **Music keeps playing** at full level while paused. **F**
* **Window focus / minimise** (`TPlatformData` Carbon event handlers, 0xc5xxx-0xc6xxx): `TSoundManager::AppActivate(false/true)` (every stream `SystemPause`) and `TSoundManager::PauseAllSounds(true/false, -1)` at the display-mode/window events (lines 1684-1799 of `TPlatformData.c`). Streams started while inactive start only on activation. These act on **all** streams, music included. **F**. The Windows event wiring is **U** (**I**: the same pflib SDK code compiled for Windows).
* **Mute** (`TSettings::ToggleSoundMute`, 0x3c1f6): reads int pref `soundoff` (default 0) of the current user, writes `soundoff = (old != 1)` (persisted), then `TSoundManager::SetVolume(0.0)` when it was not muted, `SetVolume(1.0)` when it was. It touches the **master** only: the group volumes (slider values, including unsaved slider moves) are neither re-applied nor reset. Unlike Choc3, there is no `QueryMuteGame`-driven `BSGSoundMute`; `TGameState::QueryMuteGame` exists but nothing in the audio path calls it (**F** grep). The mute checkbox is initialised in `TOptions::PostChildrenInit` from `soundoff == 1`. **F**
* While muted `VSoundManager::FadeSound` skips fades (section 3); sounds still start (master 0 means gain 0) and still run their callbacks, so game-flow callbacks that depend on a sound finishing keep working (none are used by shipped code).
* `options_muteall_checkbox` is a toggle button; pressing it only runs `ToggleSoundMute`. The only readers of `soundoff` are `VSoundManager::IsSoundMuted`, `TSettings::SetSoundForCurrentUser`, `TOptions::PostChildrenInit` and `CreateNewUser`. **F**

## 8. Volume prefs

| Pref (per user, int) | Meaning | Default | Written by | Read by |
|---|---|---|---|---|
| `soundlevel` | SFX volume x1000 (groups **1000 and 0**) | **750** (0x2ee) | `TOptions::SaveVolumes`: `(int)(sliderValue * 1000.0)` (truncate) | `SetSoundForCurrentUser`, `TOptions::PostChildrenInit`, `CreateNewUser` |
| `musiclevel` | music volume x1000 (group **1001**) | **750** (0x2ee) | same | same |
| `soundoff` | 1 = muted | 0 | `ToggleSoundMute` | `IsSoundMuted`, `SetSoundForCurrentUser`, `PostChildrenInit`, `CreateNewUser` |
| `curuser` | current user index (default -1, user -1) | - | `SetCurrentUser`, `CreateNewUser` | `IsSoundMuted`, `SetSoundForCurrentUser` |

* **F** There is **no ambient level and no `ambientlevel` pref** in GREATCC (no CADI). Defaults are 750/750, not Choc3's 750/650/500. Slider widgets: `options_sfxlevel_slider`, `options_musiclevel_slider` (`options.lua:179,233`), fill images `sound_fill` / `music_fill`, mute toggle `options_muteall_checkbox`, tutorials toggle `options_tutorials_checkbox`, fullscreen toggle `options_fullscreen_checkbox`. Slider values are floats 0..1.
* **F** `SetSoundForCurrentUser` (0x3c866; runs from `SetCurrentUser` for a valid user and from `InitGameToSettings` 0x3f70c): `SetTypeVolume(1000, soundlevel/1000.0)`, `SetTypeVolume(0, soundlevel/1000.0)`, `SetTypeVolume(1001, musiclevel/1000.0)`; then `SetVolume(master)` with **1.0 if `soundoff == 0`, else 0.0** (any non-zero soundoff mutes, but `IsSoundMuted` and the UI test `== 1`).
* **F** Live apply: `TOptions::OnMessage` (0x2be04, message type 9 = slider changed): the SFX slider sets groups 1000 and 0 to the slider value and resizes `sound_fill` to `textureWidth * value`; the music slider sets group 1001 and resizes `music_fill`. Values are applied immediately and stored only by `SaveVolumes` (OK button; `options.lua` shows only that button as the closing path, so there is no revert, **I**).
* **F** `CreateNewUser` (0x3e83e) copies `soundlevel`, `musiclevel`, `soundoff` from the current user to the new user (reads with defaults 750/750/0).
* **F** `settings.xml` has no audio keys. Persistence is the per-user prefs file (`user:prefs.dat`, `TPrefs`).

## 9. Differences from the existing OpenConche audio service (`src/audio`)

* Our `Audio` is built around the Choc3 model (event table, CADI-lite, Direct/Music/Ambient groups, ambient level). GREATCC needs none of that: just **play a file by name** in one of three groups.
* Missing in ours (needs adding or a GREATCC module path): `PlaySFX/PlayMusic/PlayVO/PurgeSounds/PurgeVOQueue/PauseSounds` Lua natives and the sprite-script `playsound`; the VSound list + done-flag reaping + 16 ms update; the music slot with same-track shortcut and 1 s crossfades; the VO queue; `PauseSounds(pause, group)` group-selective pause.
* Differences to apply: no ambient slider/pref (our `kVolumeSliders` rows are for Choc3 names `sfxlevelslider/musiclevelslider/ambientlevelslider`; GREATCC uses `options_sfxlevel_slider`, `options_musiclevel_slider`); defaults 750/750 (our `volumeDefaults` profile for Choc3 is 750/650/500); `ToggleSoundMute` must touch the master only and not re-apply prefs (ours re-applies stored prefs on unmute, `UserNatives.cpp:116`); unknown groups read as master (not 0); gain curve (master x group)^2.
* `Audio::playMusicFile(path, loop, fadeIn, fadeOut)` already offers loop + crossfade with separate ms; GREATCC uses 1000/1000 and reads the target level once.
* `Audio::playFile("?group=N")`: our mapping (1001 Music, else Direct) matches the group-id semantics, but GREATCC SFX at 1000 and buttons at 0 both follow the SFX slider.

## 10. Unknown / not determined

* **U** Windows backend: exact gain curve (quadratic here for the Mac Sound Manager build), whether Windows uses `TSoundStream::Play(true)` the same way, focus handling hooks. The exe imports `DSOUND.dll` and `WINMM.dll` (strings); no decompile.
* **U** Meaning of the `bool` in `TSoundStream::Play(bool)`: `false` for loops or sounds at least 1.0 s, `true` for short one-shots (and for the restart of a looping stream in `InternalHandleNotifications`). Probably "single-buffer vs spooled", not needed to reproduce what is audible.
* **U** What happens to a music track after its fade-out when it is no longer in ACTIVE (after `PurgeSounds`): the VSound is destroyed but the stream may keep looping silently at volume 0 (and wake up audibly at the next `UpdateAllVolumes` such as a slider move, which resets every stream to `GetVolume(group)`). I did not trace `TSoundStream::~TSoundStream`/`Kill` far enough to say whether the stream is stopped on destruction of the last `TSoundInstance` reference. A reimplementation should simply stop the faded-out track at the end of the fade (no audible difference unless the original bug triggers).
* **U** How `PlaySFX`/`PlayMusic` push their returned `shared_ptr<TSoundInstance>` into Lua (userdata metatable) and whether the sprite-script `playsound` is ever used by a shipped `.anm` script; the shipped Lua/`.anm` files contain no call.
* **U** `TSoundStream` details: sample-accurate looping on the Mac channel (restart from the notification loop, up to one tick of gap), `StopStreamDelayed`, `SetNextSoundInfo`.
* **U** Mac vs Windows data differences: the 37 file names match; `assets.xml` is ZXML in the pfp and was not decoded here (the Mac copy is plain XML); Lua in the pfp is bytecode and was not compared. No audio difference is known.
* **U** The map member at `VSoundManager+0x28` (`map<str,vector<str>>`) is constructed and destroyed but never used.
* **U** Which `VCustomer`/`ShopWindow` events exactly trigger each sound beyond the function names (e.g. why `master.ogg` and `goal.ogg` appear in both `AddCombo` and `OrderComplete`); decompile of those functions is the reference if exact trigger conditions matter (audio level only: SFX are fire-and-forget).

## 11. Implementation notes for OpenConche

1. **Module layer, not event table.** Add a GREATCC-module audio facade (new small class, e.g. `games/greatcc/VSoundManager`) over the existing `choco::audio::Mixer`/`SoundCache`/`OggStream`, with: `ACTIVE` list of `{TSound, instance, group, doneFlag}`, `CURMUSIC`, VO queue + `CURVO`, a 16 ms updater (use the engine's task/tick), the done flag set by the stream-complete callback, and the exact `Update()` order of section 2. Bind the Lua globals of section 1 (`PlaySFX`, `PlayMusic`, `PlayVO`, `PurgeSounds`, `PurgeVOQueue`, `PauseSounds`, plus the sprite-script `playsound`). `ToggleSoundMute` and `SaveVolumes` already exist in `src/script/UserNatives.cpp`; they need the GREATCC variants (two sliders, master-only mute).
2. **Groups and levels.** Three groups: 1000 (SFX, VO), 1001 (music), 0 (UI buttons); no ambient. Pref names/defaults: `soundlevel` 750, `musiclevel` 750, `soundoff` 0. Group volume `g = master * level`; unknown group = master. Stream gain = `g*g` clamped to [0,1] (this is the Mac-build behaviour; the Windows curve is unverified, choose it knowingly). `TSettings::SetSoundForCurrentUser` semantics: levels are set at user selection and on `InitGameToSettings`, the sliders apply live, `SaveVolumes` persists. Mute sets master 0/1 only.
3. **Music.** `PlayMusic(path, true, true)`: if the current track's name equals `path`, do nothing; else crossfade 1000 ms linear in the *volume value* (fade-in: `f*v0`, fade-out: `v0*(1-f)`, `v0` = group volume read when the fade starts; then square for the gain), killing the old track when its fade ends, and a new track that fails to load leaves the old one playing. Per location use the `music_01` style key (`location_<name>` table in `game.lua`); main menu uses the literal `audio/music/theme.ogg`; no other screen touches music. Decode music on first use (8 MB mono PCM each at 48 kHz) or stream it; there is no behavioural difference.
4. **SFX.** Decode `assets.xml` sounds at level load (group = `type`), play with no cap and no dedupe; a missing or non-`.ogg` path is silent (log once). `loop` only for `zepplin_loop.ogg`.
5. **Pause.** `PauseSounds(true, 1000)` pauses SFX/VO voices only; music keeps playing. Platform focus loss pauses everything (`AppActivate`); streams started while inactive begin on reactivation.
6. **Do not implement**: RVOQueue, the `map<str,vector<str>>` member, `RAdaptSound` (no shipped use, but `<sound>` adapt nodes with `loop`/`delay`/`source` are trivial to support if the RAdapt owner wants them: play once after `delay` ms).
7. Pflib behaviour that differs from the Choc3 notes: unknown group reads as master (`docs/re/audio.md` s.4 says 0.0), and the stream gain is squared in this Mac build while Choc3's Mac `TSoundStream::SetVolume` is linear. Check which one the shared `Mixer` code assumes before sharing it.
