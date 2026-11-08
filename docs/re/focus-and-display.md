# Focus, minimise, fullscreen and quit in the original (Mac build)

Sources: `re/mac/decomp/` (addresses are Ghidra vm addresses of the Mac i386 build), the original Lua in the original assets' `ui/`
(Reforged copies agree where noted). The Mac build is Carbon (`ReceiveNextEvent`, AGL), not SDL or Cocoa. Anything the
Windows build does is not covered here.

## Summary

- There is no "focus lost" hook into Lua except the one the engine itself issues: `DoLuaString("if not gUIPaused then DoModal('ui/ui_pause.lua') end")`
  (from `MapWindow` and `PortWindow` only) and `AskQuit()` on a close request. No `OnSuspend`/`OnActivate` Lua function exists
  (grep of Lua and binary strings is empty; `kAppActivate`/`kAppDeactivate` in `chocothree.h` have no sender that I found).
- The main loop does not stop the clock. `TPlatform::Timer` is wall time, so time keeps running in the background. What changes:
  specific tasks are paused and re-based on return, sound is paused, and the loop sleeps longer.
- Opening the pause dialog calls `Player:SaveGame()` (first line of `ui_pause.lua`), so losing focus on the world map or in a port
  autosaves as a side effect. Quit also saves, at the end of the event loop.
- The Mac build does **not** tear down and rebuild the GL display on focus loss. Display reconfiguration is an empty stub.
  The original plan's Windows claim cannot be confirmed or refuted from the Mac side.

## 1. Focus loss: event plumbing (`TPlatformData::QueueEvent` 0x90a28, `GetEvent` 0x929b8)

`QueueEvent` translates Carbon events into `TEvent` {type, 4 ints}; the foreground flag lives at `TPlatformData+0x9f0`
(`IsForeground` 0x8d318, `TPlatform::IsForeground` 0x59c72). Event types seen: 1 idle, 3 close request, 4 quit (exit loop),
0x12 activate (arg1 = 1 gained, 0 lost), 0x13 fullscreen changed. (Numbers are from the code; the names are mine.)

| Carbon event | Action |
|---|---|
| app activated (class `appl` kind 1) | foreground=1, `TSoundManager::AppActivate(true)` (0x6213c), queue 0x12(1); if fullscreen and cursor visible and inside content, hide cursor |
| app deactivated (kind 2) | foreground=0, queue 0x12(0), `AppActivate(false)`; if fullscreen also sends HI command `hide` so the OS hides the app |
| app hidden / shown (kinds 0x6b/0x6c, only if fullscreen) | hidden: `AppActivate(false)` + `SwapDisplayMode` (leaves fullscreen presentation, hides the window); shown: reverse, `AppActivate(true)`. A static byte remembers that we hid. These queue a plain idle event, **not** 0x12, so the Map/Port/factory hooks do not fire from hide itself (they fire from the deactivate before it). |
| window collapsing (class `wind` kind 0x42, minimise) | foreground=0, queue 0x12(0), `AppActivate(false)`, show the hardware cursor if hidden |
| window expanded (0x46) | foreground=1, queue 0x12(1), `AppActivate(true)` |
| window bounds changed (0x1b) | `BuildMouseTracker` (0x8d22c) only; nothing is re-laid out |
| window close (0x48) / Apple Quit event | sets a close-request byte (+0x9f4). `QuitAppleEventHandler` (0x8d1aa) does the same, so Cmd-Q and the close button are identical |

The Carbon constant names behind 0x42/0x46/0x48/0x6b/0x6c are my reading; the decompile only has numbers (inference).

`ChocoThreeMain` (0x1001c) is the real main loop: `GetEvent`, then per event:
- 3: `TScript::DoLuaString("AskQuit()")` (fire and forget, result ignored).
- 0x12: call `MapWindow`, `PortWindow`, `LedgerWindow`, `ChocolateFactory`, `CoffeeFactory` `ToggleForeground()` in that order, then
  `BSGSoundDeactivate()` (arg 0) or `BSGSoundActivate()`.
- 0x13: `TSettings::UpdateFullScreen` (0x35874) writes the `fullscreen` pref (1/0) from the live state.
- 4 ends the loop.
- Every event, including the above, then goes to `TWindowManager::HandleEvent` (0x4ee4c). Its per-type jump table is not recovered,
  so what the window tree does with 0x12/0x13 is Unknown.

### What each piece does on loss (`ToggleForeground` reads `IsForeground`)

- **Sound**: `TSoundManager::AppActivate(false)` marks the manager inactive and calls `SystemPause(true)` on every registered stream
  (regain: `SystemPause(false)` plus `UpdateAllVolumes`). `BSGSoundDeactivate` (0x5650) sets `gSoundDeactivated` and sends the sound
  event `"Pause"` (regain `"Unpause"`, 0x55f0), unless the user-pause flag `gSoundPaused` is set (that flag is
  `BSGSoundPause/Unpause`, used by the factories). Both are idempotent. So music and sfx stop. How `"Pause"` reaches music
  (Cadilite via `BSGSoundEvent` 0x5166) is an inference.
- **MapWindow** (`WindowToggleForeground` 0x2a53c): on loss, `StopWindowAnimation` on the map window, and if the map is the top modal
  and travel is not already paused (+0x68 == 0), runs the pause-dialog Lua line above. On gain: `StartWindowAnimation(30, repeat,
  fromNow, no catch-up)`; if the travel flag is clear and a travel task exists, it re-bases that task to 30 ms (`vtbl+0x18`). If the
  pause dialog is open (`PauseTravel` 0x2a3c4 set +0x68 = 1), travel stays paused until the dialog's OK runs `ResumeTravel` (0x2a390).
- **PortWindow** (0x2eb60): on loss `InternalPausePortAnimations(true)` (`TAnimTask::Pause` on the port animation task, +0x34), and if
  the port is the top modal, opens the same pause dialog. On gain, if +0x30 == 0 (script has not paused animations),
  `SetDelay(50 ms, repeat, fromNow, catchUp)`.
- **LedgerWindow** (0x274bc): on loss `TAnimTask::Pause` on its +0x80 task; on gain `SetDelay(50, true, true, false)`.
- **ChocolateFactory / CoffeeFactory** (0x87ae, 0x113e8): `ToggleForeground` ignores the direction and calls
  `PauseGame("game_paused")` on **both** loss and gain. `PauseGame` (Choco 0x8384) is idempotent via the paused byte (+0xdc).
  There is no game-over exception: on the results screen (state 5) a focus change shows the pause card too, and its `ok`
  (`ResumeGame`) re-enters the saved state, so the results fill comes back. Ours matches (decision recorded after review).
  First call: arrow cursor restored, `BSGSoundPause()` (only if the game state +0x64 is nonzero), `GameState "ChocolateFactoryTime"` set
  to 0 (a telemetry timer), `go` child shown and `pause` hidden, six `TScalingTask`s paused, `StopWindowAnimation` on the factory, and
  the `information` child filled with `ui/game_pause.lua` (text key from global `gPauseText`, default `game_paused`). It always ends
  with `TWindowManager::SetTopModalOnly(false)`. **Regain does not resume.** The player must press the dialog's `ok`, which runs the Lua
  global `ResumeGame` (Choco 0xc82c): `BSGSoundUnpause`, timer back to 1, show/hide, the six tasks restarted with `vtbl+4(0xf)`,
  `StartWindowAnimation(10, true, false, true)`, `SetGameState(saved)`, `SetTopModalOnly(true)`. The in-game `pause` button calls the
  same `PauseGame` (`chocolate_factory.lua:92`, `coffee_factory.lua:92`). CoffeeFactory has the same structure (ResumeGame 0x14c88);
  I did not diff the two line by line.
- **Any other window or animation**: untouched. Window animation tasks and transitions are not paused (see 2).
- **`Pause(ms)` in script** is prelude Lua over `GetTimer()` (wall clock) and `WaitForMessage` timeouts. It is not paused by focus loss:
  a 3 s `Pause` started 1 s before alt-tab ends no earlier than 2 s after regain; if the deadline passed while away, it ends on the
  next tick.
- **Transitions**: nothing focus-aware found (not paused, not cancelled). They run off `Timer`, so a fade completes on regain.

## 2. Time basis and regain

- `TPlatformData::Timer` (0x8d3d0): `gettimeofday` minus the start time stored at +0x9c4/+0x9c8, scaled and clamped to a 32-bit range
  (constants not read; presumably ms since launch, matching all uses). Wall clock, no pause, no focus awareness. `TPlatform::Timer`
  (0x59ce6) forwards to it.
- `TAnimTask` (due +8, period +0xc, fromNow +0x10, catchUp +0x11, clock +0x14): time is `TClock::GetTime` if a clock is set, else
  `TPlatform::Timer`. `Pause` (0x81262) sets due = -1 (stopped). Every resume above is `SetDelay(..., fromNow=true)` or a restart that
  re-bases on now, so those hooks resume **without catch-up**. Tasks that are not paused follow `DoTask` (0x813fe), documented in
  `factories-questions.md` section 2: lag over 1000 ms resyncs to now with one step; lag up to 1000 ms is caught up step by step. A long
  absence never causes a big burst; an absence under 1 s can.
- `TClock` (0xa3f26..): `Pause`/`Start` only shift an offset. Nothing calls them on focus change (users are modal push/pop and the sound
  manager), so modal clocks keep running in the background.
- Idle behaviour in `GetEvent`: `ReceiveNextEvent` is called with a timeout of 0.001 s (read as the double 0x3F50624DD2F1A9FC). On
  timeout (`eventLoopTimedOutErr`, -9875): if **not** foreground, a persistent counter goes up by 10 to a cap of 1000 and the thread
  `usleep`s that many ms (10, 20, ... 1000 per loop; reset to 0 once foreground). Then, if fullscreen and foreground and not hidden,
  `BringToFront`. Then the platform task list (+0x6a14) runs, and an idle event is returned. So the loop keeps ticking in the
  background, ever slower (1 Hz ceiling after about 100 iterations, about 50 s). Whether the window tree draws on that idle event is
  Unknown.
- The other task list (+0x69f8) runs inside `BeginDraw` (0x8d35c) once per flagged frame; its contents were not examined.

## 3. Fullscreen / windowed toggling

Lua: `ui_options.lua:41` (Reforged line 22) calls the global `ToggleFullScreen()` (bound to `TSettings::ToggleFullScreen` 0x35928) and
expects a boolean; false opens `ui/ui_generic.lua` with `screenswitchfailed`. There is no Lua callback after a toggle and no message.

Flow: `TSettings::ToggleFullScreen` -> `TPlatform::SetFullscreen` (0x59c8a) -> `TPlatformData::SetFullscreen` (0x8f7c4). If the requested
mode differs from the current one: acquire a display-fade reservation (15 s), fade to black over 0.5 s (blocking), call
`SetDisplay(w, h, fullscreen)` (0x8eab8), fade back over 3.0 s (non-blocking), release. Then the `fullscreen` pref is written
(`TPrefs::SetInt`, flush flag 1). On Mac this path always returns true; I saw no false path (Windows may differ). Fade constants are
float bit patterns (0x3f000000 = 0.5, 0x40400000 = 3.0, 0x41700000 = 15.0).

`SetDisplay` keeps the same logical size, window and GL context. It ends any current fullscreen session (`EndFullScreen`), begins a new
one (`BeginFullScreen`) or `RepositionWindow`, sets system UI mode (menu bar and dock hidden when fullscreen), re-attaches the AGL
drawable (`aglSetDrawable`, `aglUpdateContext`), enables vsync (`aglSetInteger` 0xde = 1), and calls `TGLDisplayHandler::SetDisplay`
(0xaafb2), which resets viewport, world/view/perspective matrices, lighting and material. The pixel format and context are created
only when null, so **nothing is destroyed**: textures, the window tree and Lua state are untouched (inference from no calls to them).
It also warps the mouse to the window centre and rebuilds the mouse tracker. Windows are **not** re-laid out: the logical size is fixed
at 800x600 (`kScreenWidth`/`kScreenHeight`, `kGameScale` 1.0 set in `ChocoThreeMain`).

A hotkey in the key path (modifier plus a configured char) also toggles `SetFullscreen` and queues event 0x13 so `UpdateFullScreen`
records it. Display Manager reconfiguration (`handleConfigDMEvent` 0x8d18c) calls `TGLDisplayHandler::RebuildDisplay` (0xa8e7c), an
empty function; `HandleDisplayMinimize` is also an empty stub. So on Mac no teardown or rebuild happens for display changes or minimise.

## 4. Quit and autosave

- Close button, Cmd-Q: `GetEvent` sees the close byte, calls `TWindowScript::CloseRequested` (stops a running `TFlashHost` and
  resumes the script, if one is active) and returns event 3, so `ChocoThreeMain` runs `AskQuit()` (`ui/mainloop.lua:24`). `AskQuit`
  does nothing if `gQuitActive` or `gTravelActive` is set (a close during travel is silently ignored). Otherwise it shows
  `ui_generic_yn` with `confirm_quit`; "yes" runs `Player:SaveGame()` and posts `kQuitNow` (type 5); the native handler calls
  `TPlatform::Exit` (0x59da6), which sets +0x9d0 and calls `QuitApplicationEventLoop`. "no" clears `gQuitActive`. There is no
  "Save & Quit" dialog in this Lua.
- Once +0x9d0 is set, `GetEvent` returns event 4 once, the loop ends, and `ChocoThreeMain` runs `BSGSoundShutdown()` then the Lua string
  `if Player then Player:SaveGame() end`. **This is an unconditional save at exit.** It corrects `trade-and-save.md` item 5 ("Quit does
  not save in the engine"); the save happens in `ChocoThreeMain`, not in `TPlatform::Exit`. It also covers `TWindowManager::HandleEvent`
  calling `Exit` when the top modal is null.
- Focus loss does not itself save. The pause dialog it opens (`ui_pause.lua` line 8, original and Reforged) calls `Player:SaveGame()`,
  so Map/Port focus loss autosaves, only when that window is the top modal and `gUIPaused` is false. Factories and other windows: no save.
- `-[FlashWindowDelegate windowShouldClose:]` and `Controller` methods belong to the embedded Flash/web window helper, not the game window.

## 5. Windows build

Not examined (no Windows decomp covered here). From the Mac side: no teardown on focus loss exists, so the original plan's note about rebuild
and the fade crash (`BSGTransition` holding a raw `TWindow*`, 0x44b760) is Windows-specific as far as these notes can tell. Relevant
to us regardless: a transition must not outlive its window, and the renderer must survive loss/regain (our `RenderReset` path
already re-uploads textures).

## Engine impact (`src/app/App.cpp`)

Current loop: no focus events; the script clock is `SDL_GetTicks() - start` always; `T::Quit` runs `AskQuit()` (matches); the post-loop
`Player:SaveGame()` exists (matches). To match the original we would need:
1. A platform "foreground changed" event with a flag, from SDL3 window focus lost/gained and minimised/restored. Dispatch order: Map,
   Port, Ledger, ChocolateFactory, CoffeeFactory `ToggleForeground`, then sound.
2. The five `ToggleForeground`s (pause/resume by re-basing with `SetDelay(fromNow)`), the pause-dialog Lua line for Map/Port when they
   are the top modal, factory `PauseGame` on both edges (resume only through the dialog), and `BSGSound*Deactivate/Activate` plus
   stream system-pause.
3. Keep the clock wall time (no pause). Optionally mimic the background throttle (10 ms steps up to 1 s per loop).
4. `ToggleFullScreen()` returns bool and writes the `fullscreen` pref; no Lua callback; keep the window tree intact (logical size
   does not change).
5. Close during `gTravelActive` is ignored by `AskQuit`; ours calls the same Lua, so it matches.

## Unknown

- What `TWindowManager::HandleEvent` (jump table at 0x4eeef not recovered) and `TWindow::HandleEvent` do with event types 0x12 and
  0x13, and whether the window tree draws on idle ticks while backgrounded.
- Exact `TPlatformData::Timer` scaling constants (assumed ms).
- Which key is the fullscreen hotkey; how Cadilite maps `"Pause"`/`"Unpause"` to music and sfx streams (`BSGSoundEvent` `str::find`
  branch semantics unclear).
- CoffeeFactory `PauseGame` details beyond structural parity; the `PausePortAnimations(bool)` Lua global (`sim/product.lua:696`) is not
  read here (presumably sets the port animation pause and +0x30).
- Whether anything sends `kAppActivate`/`kAppDeactivate` (none found).
- Everything about the Windows build.

## 6. The mouse cursor

**The boot cursor (fact).** Chocolatier 1's main loop (the exe's `004036d0`, right after `SetWindowTitle`) loads `cursor/cursor`
and hands it to the platform's cursor setter with hotspot **(1,1)**; Chocolatier 2's `Main` (re/choc2/decomp/_free/m.c) does the
same with `image/cursor` - `TTexture::GetSimple` then `TPlatform::SetCursor(..., 1, 1, 0)`. So both games run with a skinned
arrow cursor for the WHOLE session (the factory games switch to the gun aimer and back through the same setter).
Chocolatier 3 does the same inside `ChocoThreeMain` (`0001001c`, re/mac/decomp/_free/c.c): after the window-type registrations it
loads `cursor/cursor` through `Content::LoadCursor` and calls `TPlatform::SetCursor(1,1,0)` right before `BSGSoundInit` (`Main` itself only sets the title and hands
to `ChocoThreeMain`, so the call is not visible there). So ALL THREE games run with the skinned arrow for the whole
session; the factory games swap gun vs arrow through the same setter. The engine mirrors this: `GameProfile::bootCursor` +
`ScriptContext::setCursor` at app start.

**The cursor art's transparency (fact).** The shipped cursor pictures - Chocolatier 1's and 3's `cursor/cursor.png` and
`cursor/gun_aimer.png`, Chocolatier 2's `image/cursor.png` and `image/gun_aimer.png` - are 32x32 **8-bit paletted PNGs with NO
`tRNS` chunk**: their background palette entry is **pure magenta (255,0,255)**, the colour key the original's picture loader
zeroes. They are the ONLY pfp art without real transparency (a scan of Chocolatier 1's 669 PNGs: 576 paletted, 569 with tRNS,
exactly two magenta-key files - the two cursors; Chocolatier 2's and 3's cursor files are the same kind). The engine applies the
key only on the cursor path (`platform::Window::applyCursorColourKey`), never to the general image pipeline.
