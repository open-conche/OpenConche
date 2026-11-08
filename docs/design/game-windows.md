# Design: game-world windows

`PortWindow`, `MapWindow` with travel, `CharWindow`, the ledger, the sprite tree they draw with, sprite sheets and
their per-sprite animation scripts, and the game-level natives. Together they cover starting a game, the map, travel,
a port, buying and selling, the ledger, and save and reload. They build on [window-system.md](window-system.md) and
[script-host.md](script-host.md). The facts are from `docs/re/port-window.md` (PW §n), `docs/re/map-window.md`
(MW §n) and `docs/re/ledger-and-game-natives.md` (LG §n). They are not repeated here unless a design choice depends on
them.

Where the code lives: the sprite tree, `SpriteScript` and `Ticker` are in `src/sdk/`; `Curve`, `SpriteSheet` and
`SheetCache` are in `src/gfx/`; `PortWindow`, `MapWindow`, `Travel`, `PathSprite`, `World`, `LedgerHost`,
`LedgerWindow` and `DookieDropper` are in `src/games/common/`; `CharWindow` and the Chocolatier 3 natives are in
`src/games/choc3/`. Chocolatier 2 has its own ledger (`src/games/choc2/LedgerWindow.cpp`) but shares `PortWindow`,
`PathSprite`, `MoneyCounter` and `MoodFade`.

## 1. Behaviour the scripts depend on

- **The port is data, read once.** `PortWindow{ port = <Port table>, background = ... }` reads `port.buildings` and
  `port.sprites` in `PostChildrenInit` and never again (PW 1.3, 1.4). The only native it registers is
  `PausePortAnimations(bool)`.
- **Building click** is a string eval on the main script, run on mouse-up when the press and release land on the same
  building: `"gCurrentBuilding=nil;" .. name .. ":OnClick()"` (PW 1.7). Hover never calls Lua. It shows a pulsing
  highlight sprite behind the building and the label window that `portview.lua` built with the building's name. Hit
  testing uses the texture rectangle, and the highest `layer` wins.
- **Port timing.** A 50 ms task advances path sprites by a **fixed 50 ms** a tick. It also drives the intro fade (the
  background goes from 0.25 to 1 over 2 s, and labels fade out between 1 and 2 s) and the hover pulse. The task keeps
  running while dialogs cover the port (PW 1.8).
- **Sprite scripts.** Every animated sheet with more than one frame that gets `Play("DoAnim")` runs its sheet's
  script in **its own Lua state**: the embedded `animationscript.lua`, then the sheet's `luascript`. The `frames{}`
  entries are 1-based. `delay` is in ms and defaults to 16 (PW 5).
- **Characters.** `CharWindow{ name, alpha, scale, happiness }` draws frame 0 (the body) and then a face frame
  chosen by mood. A mood change cross-fades over 4 ticks of 50 ms. `SetMood(name, h)` applies to every live
  `CharWindow` with that name (PW 2, LG 6).
- **The map.** It is built once from `_AllPorts`, `Player.portsAvailable` and `Player.portName`. Each port gets a
  marker sprite whose texture depends on its state, a two-sprite label, and a native `Rollover` with
  `contents = "<port>:MapRolloverPopup()"` and click string `"<port>:OnClick()"` (MW 4). The clouds drift on a 30 ms
  window animation.
- **Travel.** `InitiateTravel{ path, type, events }` starts a 30 ms task on **wall-clock** time, minus the time spent
  in `PauseTravel`. Each tick it drops a trail dot every 12 px of arc length, picks the vehicle's frame from its
  heading, scales the vehicle for perspective, and fires the due events. Events run **inside `Main`** (injection), so
  they can open dialogs. Arrival is just the last event (`dest:EnterPort(true)`), which swaps the modal and so
  destroys the map. On the tick where `t == 1`, every remaining event fires (MW 6, 7, 8).
- **The ledger** is one window that outlives screens. `GrabLedger` moves it to the front of the current top modal,
  building it from `ui/ledger.lua` the first time. `ReleaseLedger` detaches it. A ledger destroyed with its modal is
  rebuilt by the next `GrabLedger`. Its 50 ms task animates the money counter (20 steps), flashes the quest indicator
  (250 ms toggles for 1500 ms, then it stays on "complete") and ticks the six `DookieDropper` gauges (LG 1, 2, 5).
- **Guards the Lua relies on.** `ui_pause.lua` calls `if PauseTravel then`. `UpdateLedger` calls
  `if RealUpdateLedger then`. `TickSim` calls `AnimateProduction()` unguarded. Registering all of these at boot as
  no-ops while their window is absent satisfies every caller (LG 1.1).

**Not copied from the original:**

- The globals bound to stale instance pointers (`PausePortAnimations`, `PauseTravel`, `ResumeTravel`). Ours resolve
  the instance by `WindowId`.
- The null `pCurrentPort` dereference in `InitiateTravel`. Ours logs and starts from the curve's first point.
- The divide by zero in `SetScaling` when `yFar == yNear`. Ours guards it and uses the far scale.
- The divide by zero in the sprite `Random(a, a)`. Ours returns `a`.
- A ONEWAY path sprite unlinking itself from the list being iterated. Ours sets a `removed` flag instead.
- A ledger destroyed with its modal clearing its globals to nil. Ours keeps them as no-ops.

**Kept on purpose:** the rectangle-only building hit test, the duplicate "you are here" rings (they change how a
semi-transparent ring looks), the 50 ms fixed step, the second `InitiateTravel` on the same window being ignored, and
the float round trip of money in the ledger.

Sprite states do **not** open `math`. `TAnimatedSprite::NewScript` builds a `TScript`, and `TScript::LuaInit`
(`re/mac/decomp/TScript.c:91-93`) opens only base, string and table, then registers `_ALERT`, `DebugOut`, `toparams`
and the `DumpTable` prelude. Only `TLuaParticleSystem` calls `luaopen_math` (`TLuaParticleSystem.c:2176`). No shipped
sheet script uses `math`.

## 2. Decisions

1. **`animationscript.lua` is reconstructed as Lua source**, in `lua/animationscript.lua` and embedded like
   `prelude.lua`. It is checked function by function against `re/mac/lua/animationscript.lst`, using the
   `luac`-compare method from `docs/re/embedded-lua.md` §3. The sheet scripts call `frames`, `delay` and `run` as Lua
   functions, and `run` uses `loadstring` on sheet text. A native reimplementation would still need a Lua state for
   the sheet text and would only add a second copy of the semantics.
2. **One Lua state per scripted sprite, as in the original.** The state is owned by the sprite (`SpriteScript`) and
   closed in its destructor. The natives get the `SpriteScript*` as a light-userdata upvalue, and they cannot outlive
   it because the state they live in is closed first. A shared state with one `setfenv` environment per sprite would
   save some memory but needs per-sprite closures, environments that must be released, and care that the sheet's
   `function DoAnim` lands in the right environment. The numbers are small: about 10 to 20 scripted sprites per port,
   plus the map markers, at roughly 15 KB each. No sprite state can see the main state.
3. **Fixed-step tasks run on the script clock.** That is simulated in headless mode and real otherwise, and it is the
   same clock as `GetTimer` (script-host §7). A `Ticker` reproduces `TAnimTask` with `fromNow` and no catch-up. Every
   tick does exactly what the original's tick does. Headless runs are therefore deterministic, and the step counts
   match a 60 Hz original.
4. **Layering.** `gfx` keeps the pieces with no Lua in them: `Curve`, `SpriteSheet`, `SheetCache` and the mask merge.
   The sprite tree and the animation-script host (the part that links Lua) are in `src/sdk/`, so any game can use
   them. The game windows are in `src/games/common/` when more than one game uses them, and under the game's own
   folder otherwise.

## 3. Types, ownership and lifetimes

### 3.1 Shared pieces

- **`gfx::Curve`** is the exact `TCurve` (MW 6.2). A chain of cubic Beziers over 3n+1 control points. `length()` is 0
  for fewer than 4 points and subdivides lazily with a tolerance of 0.2 (polygon length against chord). `at(t,
  &tangent)` returns the first point for `t <= 0` and the point at index `((n-1)/3)*3` for `t >= 1`, and leaves the
  tangent untouched in both cases. Transitions use it too.
- **`gfx::SpriteSheet`** is the parsed sequence XML (PW 2.3): an image, frames `{src, reg}` and the script text (a
  generated default when the XML has none). **`gfx::SheetCache`** is keyed by the resolved path and the image and mask
  overrides, as `TAnimatedTexture::Get` is. `get` probes `<name>`, `<name>.xml`, then `<name>.anm` (the compiled form
  Chocolatier 3 does not ship; it is read for other games) and returns null when there is no sheet, so callers fall
  back to a plain texture. The sheet image is `<xml dir>/<img name>`. The XML attributes `xform` and `anchor` are
  logged once if present and otherwise ignored; no shipped sheet has them.
- **JPG plus mask.** `TextureCache::load(name, mask = {})` merges alpha from a mask image: the mask's **blue channel**
  (startup-and-assets §2) becomes the alpha. With no mask given, a JPG looks for a sibling `<base>.mask.png`. The
  merged texture records its mask path, so `reuploadAll` can merge it again after a device reset. Mismatched sizes
  log once and leave the texture opaque.
- **Trimming.** Big port images (an 800x600 background plus about 30 building and scenery PNGs, roughly 10 MB per
  port) are freed when the port window dies. After any `clearDestroyed()` that deleted something, the app calls
  `sheets.trim()` and then `textures.trim()`. Nothing else is cached. Re-entering a port decodes its files again,
  which is a few tens of ms.
- **`sdk::Ticker`** (`src/sdk/Ticker.h`). `start(now, ms)` sets `next = now + ms`. `pause()` sets `next = -1`.
  `due(now)` is true when `next >= 0` and `now + 1e-6 >= next`, and it then sets `next = now + ms`. There is one call
  per frame at most, and no catch-up. The epsilon absorbs the `frame * 1000/60` rounding. The same header has
  `FixedStep`, the catch-up variant (the next due time advances by exactly `ms`, with a resync after a second behind)
  that the factory game uses.

### 3.2 Reading Lua data

The style is a view of the window's table, but the port definition, routes and `Player` are nested tables. `ui::Style`
has a read-only nested-table view: `table(key)`, `size()`, `element(i)`, `numberAt(i)`, `stringAt(i)`, `function(key)`
and `forEachTable(fn)` (pairs order, table values only). The default-style chain applies only at the top level;
sub-tables use plain `gettable`, so metatables still apply. `ScriptSink::withGlobal(name, fn)` hands the callback a
view of a global table, valid only inside the callback. `MapStyle` carries nested values for unit tests.

Game windows **copy** what they read into plain structs at once: `PortDef`, `BuildingDef` and `SpriteDef` (every key
in PW 1.3, with its default), `PortInfo`/`PlayerInfo` and `TravelRequest` for the map. Parsing is pure. Random rolls
happen while the scene is built, not while it is parsed.

### 3.3 Sprite tree (`src/sdk/Sprite.h`)

`sdk::Sprite` is `TSprite` plus `TAnimatedSprite`. It has a layer, a debug name, a position, scale, rotation, `tint`
(not inherited), `alpha` (inherited), `visible`, `topLeft`, `removed`, a texture or a sheet with a frame, and an
optional `SpriteScript` that exists only after `play()`. `PathSprite` (`src/games/common/sprite/`) adds the curve
(PW 1.9) and owns its colour-mask child. `TextSprite` (`src/sdk/`) draws the map labels.

- **Order.** `addChild` keeps children stably sorted by layer, ascending (PW 1.6). `setLayer` only marks the parent;
  `resortDirty()` re-sorts after the tick, never during it.
- **Ownership.** The window owns the root sprite with a `unique_ptr`, and the root owns everything under it.
  Non-owning `Sprite*` pointers into the tree (building and highlight, the map's vehicle, the clouds, the current
  marker, the path-sprite list) are members of the **same window**. They are declared after the root, so they are
  destroyed first, and the tree is never pruned while the window lives. Removal is the `removed` flag, which stops
  drawing and hit tests. Nothing in a sprite points outside its window except `SpriteScript`'s `SpriteContext`
  (sheet cache, RNG, log), which the App owns before the `WindowManager`.
- **Draw.** `SpriteSpec{origin, scale, rotation, alpha}` composes down the tree. Children with layer < 0 draw first,
  then the sprite, then the rest. Each sprite is drawn with `drawTexture(tex, src, dst, tint * alpha)`, where `dst` is
  `pos - anchor * scale`. The anchor is the centre for a texture, (0,0) for `topLeft`, and the frame's `reg` for a
  sheet; for a sheet, `src` is the frame's rectangle.
- **`rect()`** uses the same anchor rule, which makes it the building hit area. `hit(p)` is true for a visible,
  unremoved sprite with `p` inside `rect()`: left and top inclusive, right and bottom exclusive. Chocolatier 2 adds
  `hitOpaque`, a per-pixel test (`docs/re/choc2-port-binary.md` 1.3); Chocolatier 3 buildings do not use it.

### 3.4 `SpriteScript` (`src/sdk/SpriteScript.*`)

Each scripted sprite owns a `SpriteScript`: a `lua_State` with base, string and table only, plus `_ALERT` and
`DebugOut`. `play(fn, now)` makes the state if needed, starts `fn` on a coroutine and runs it to its first yield.
`tick(now)` resumes the coroutine when it is due.

- **Natives** (PW 5.1):

  | Native | Effect |
  |---|---|
  | `WaitLuaNextFrame(ms?)` | wakes at `now + ms`; with no argument, sleeps for ever; yields when called on the thread |
  | `SetCurrentFrame(i)` | sets the frame for `i > -2`, clamped below the frame count |
  | `SetAlpha(a)` | sets the owner's alpha |
  | `Load(xml, img, mask)` | switches `owner.sheet` |
  | `GetScript()` | returns the sheet's script text |
  | `Pause(p, depth)` | pauses, or on resume wakes in 1 ms; recurses into children to `depth` |
  | `IsPlaying(depth)` | true if this thread or a child's is active |
  | `Die()` | ends the script |
  | `RemoveSprite()` | sets `removed` |
  | `SetLayer(n)` | changes the layer; the parent re-sorts after the tick |
  | `Random(a, b)` | `a + rng % (b - a)`; `a` when `a == b` |

- **Order of setup:** the embedded chunk, then the natives, then the sheet's script text.
- **Errors.** A Lua error or a failed resume logs `[sprite-lua] <sheet>: <msg>` once and ends that script; the sprite
  keeps its last frame.
- **Runaway scripts.** A count hook (`lua_sethook`, 10^6 instructions per resume) aborts a script that never yields,
  such as `frames{}` in a `while true` loop, instead of hanging the frame.
- **Clock.** Scripts wake on the script clock. They are platform tasks, so they keep running while the port is
  covered. `PausePortAnimations` does not pause them; it pauses only the port task.

### 3.5 Timing infrastructure in `ui`

- `ModalWindow` has the pausable clock that WS 3.2 describes: `pushModal` pauses the old top's clock and `popModal`
  starts the new top's. The clock reads `platformNow - pausedTotal`.
- **Window animations** (`StartWindowAnimation`). `wm.startWindowAnimation(WindowId, ms, catchUp = false)` and
  `virtual bool Window::onAnimate()`. Here they drive the map clouds (30 ms, a fixed +30 per call, MW 3) and the mood
  cross-fade (50 ms, PW 2.5). `tickWindowAnimations(now)` iterates a copy of the entries. Each entry resolves its
  window and drops out if the window is gone or `onAnimate` returns false. A window's due test uses its modal's
  clock, so animations freeze while a dialog covers them.

### 3.6 `game::World`: instances, ledger, natives

`World` is the context's `GameState`, made by `registerGameNatives` and destroyed before the window manager and the
Lua state it holds references into. It resolves the newest port, map and factory window by id (`port()`, `map()`,
`factory()`, null when gone; each window registers itself at the end of `postChildrenInit`, and the newest wins). It
owns the `LedgerHost`.

- **Per frame**, `World::tick(now)` runs the port, then the map, then the factory, then the ledger.
- **Foreground.** `World::setForeground(fg, now)` stops the map clouds, the port task and the ledger task when the app
  loses focus and restarts them from `now` (no catch-up). On loss with the map or port as the top modal it opens
  `ui/ui_pause.lua` (when the game profile asks for it and no travel is paused). Regaining focus never resumes a
  pause; the player presses OK.
- **Ledger lifetime.** Ownership is never shared. While attached, the ledger is owned by the modal's child list and
  the host keeps only its id. While detached, the host owns it with `unique_ptr`.
- **`grab()`:**
  1. If `get()` is null, build a new ledger:
     1. Create a `LedgerWindow` and `adoptFront` it into the top modal. It is attached **before** the script runs,
        because the creation stack holds ids and detached windows resolve to null.
     2. `runFileInto(ledger, "ui/ledger.lua")` pushes the ledger as the creation top, loads the file and runs it with
        `pcall` on the calling thread. `ledger.lua` does not yield, and looks up no window by name while it is being
        built (LG 1.3), so attaching first is safe.
     3. `fitToChildren`, `prepareInstance()` (LG 1.3), and start the 50 ms task.
  2. Otherwise:
     - If the ledger is attached elsewhere, `orphan` it from its parent and `adoptFront` it into the top modal.
     - If it is detached, adopt `detached_` into the top modal.
- **`release()`** moves an attached ledger out of its parent into `detached_`. The ledger's id never changes.
  Hover, focus and listeners that point into it resolve to null while it is detached.
- **A ledger lost with its modal.** If the modal holding the ledger is destroyed without a release, the ledger goes
  with it (deferred) and `get()` returns null, so the next `grab()` builds a fresh one. This matches LG 1.4, with
  nothing dangling.
- **Shutdown.** A detached ledger holds `LuaRef`s and `wm` references, which is why `World` dies before
  `ScriptHost`, `WindowManager` and `LuaState`.
- **Natives** (Chocolatier 3, `src/games/choc3/`; every one does nothing while its window is absent):

| Native | Behaviour |
|---|---|
| `PausePortAnimations(b)` | the port stores `explicitPause` and pauses or restarts the 50 ms ticker |
| `SetMood(name, h)` | walks every modal's tree (and nothing detached) for `CharWindow`s named `name`; `setMood(h)` |
| `InitiateTravel(t)` | `map->initiateTravel(parseTravelRequest(t))`: copies the curve, types and events (functions as `LuaRef`) and runs no Lua |
| `PauseTravel()` / `ResumeTravel()` | the map's travel: `pause` / `restart(30)` (MW 8) |
| `GrabLedger()` / `ReleaseLedger()` | `world.ledger.grab()` / `release()` |
| `RealUpdateLedger(mode, data)` | LG 2 table, on `ledger.get()` |
| `SetLedgerQuestIndicator(mode)` | LG 2.2 |
| `AnimateProduction()` | each dropper's clock := 0 (LG 2.3) |
| `StartMovie` / `IsMoviePlaying` / `StopMovie` | `false` (log once) / `false` / no-op (LG 7.4); movies are not played |
| `SubmitToServer(...)` | logs once, then `runString("SwitchModes(false)")` (LG 8.3); offline |
| `GetLuaServerSubmitSetupVars(b)` | the offline string of LG 8.3, with names escaped |
| `PrepareGame(bet)` / `SelectPrize(i, final)` | the crates game (LG 10.1): `CratesGame` in `src/games/common/Crates.*`, natives in `CratesNatives.cpp` |

### 3.7 `PortWindow : ui::Window` (`src/games/common/PortWindow.*`)

- **Members:**
  - `root_` (`unique_ptr<Sprite>`, `topLeft`, the background).
  - `buildings_`: a vector of `{name, Sprite* sprite, Sprite* highlight, WindowId label, float rolloverScale}`
    (plus the hover-popup data Chocolatier 2 uses).
  - `scenery_` (`Sprite*`s that get the background tint) and `paths_` (`PathSprite*`s).
  - `int target_ = -1, pressed_ = -1` (indexes into `buildings_`, so no iterators).
  - `Ticker anim_; double startTime_, lastTime_, rolloverTime_, musicTime_; bool music_ = true, explicitPause_`.
- **`postChildrenInit`** follows PW 1.4 to 1.5 exactly, with the RNG calls in the original's order: the frequency
  roll for every entry, the image pick, the random phase, then the tint pick. All of them go through `ctx.rng`, the
  same generator as `RandRange`.
  - Label placement uses the label window's fitted width from the window tree and clamps horizontally only.
  - The highlight child has layer -1 and is hidden.
  - The window calls `world.setPort(id())` at the end.
  - The scene is read by the virtual `readScene`, which defaults to `parsePort` on the `port` key; a game that builds
    its scene another way overrides it. `fadeIn_`, `portMusic_` and `choc2Pulse_` select the per-game variations
    (Chocolatier 3 fades in and has no random port music; Chocolatier 2 pulses a `PulseSprite` behind each building
    with a `command`).
- **`onMouseMove`, `onMouseDown`, `onMouseUp`** follow PW 1.7, in window-local points.
  - `onMouseMove` always returns false.
  - `onMouseUp` cancels the highlight **before** running Lua, then calls `sink->runString(click)` and returns true
    without touching any member afterwards.
  - The `curve_editor` branch of the original is not implemented.
- **`frame(now)`**, called by `World::tick`:
  1. If `anim_.due(now)`, run `doAnimation(now)`, which is PW 1.8 steps 1 to 7 with `update(50)` on every path
     sprite. `ChangeMusic` (PW 1.10) sends `SoundEvent` cues through the audio service.
  2. Every scripted sprite: `tickScripts(now)`.
  3. Re-sort any parent that a `SetLayer` marked.
- **`draw`** draws `root_` with an identity spec at `ctx.origin`. The child windows (labels, banner) are drawn after
  it by `drawWindow`, so they end up on top, as in PW 1.6.
- **Fades.** A `PortWindow` is not a fader type, so a modal fade leaves it alone, as in the original.

### 3.8 `MapWindow : ui::Window` and `Travel` (`src/games/common/MapWindow.*`, `Travel.*`)

- **Members:**
  - `root_`, `clouds_`, `cloudCounter_`, `current_` (the current-port marker) and `vehicle_` (`Sprite*`s).
  - `markers_`: `{name, Sprite*}` pairs.
  - `std::unique_ptr<Travel> travel_`, declared **after** `root_`.
  - The keys of MW 2.
- **Build.** `init` builds the clouds and the background and does the random cloud phase. `postChildrenInit` reads
  `_AllPorts` and `Player` with `withGlobal` and `forEachTable` into `PortInfo`/`PlayerInfo` and calls
  `collectPortData`:
  - Each port gets a marker sprite whose texture or sheet depends on its state (`markerLook`); `new`, `factory` and
    `factory_stall` get `play("DoAnim")`.
  - Each port gets two `TextSprite`s (black, with a white child at +2,+2) using `fonts/fertigo.mvec` at size 20.
  - The ring logic is kept as it is.
  - The rollovers are made with `RolloverWindow::makeNative(rect, contents, clickString, popupX, popupY)` and adopted
    front-first. A Rollover built this way runs `clickString` with `runString` when it has no `command` function.
  - It then starts the 30 ms window animation for the clouds and calls `world.setMap(id())`.
- **`Travel`** is the 30 ms task (MW 7). It holds the curve, the vehicle `Sprite&`, the modes, the events and the
  scaling, and owns no sprites and runs no Lua itself. The owner passes `Hooks` to `tick`: `setMode` (load the
  vehicle's sheet), `addDot` and `fire` (run one event). `tick` returns `Running`, `Finished` or `OwnerGone`.
  `pause()` stops the ticker; `restart(now)` re-baselines the clock and restarts it, so paused time is lost.
  - The vehicle and the dots are children of `root_`. `Travel` holds `Sprite&`, which is valid because `travel_`
    dies before `root_`. The vehicle stays hidden until the first completed tick.
  - `fire` runs an event's action through `sink->runInScript(action)`. That is an injection into `Main` when it
    waits, and a `pcall` otherwise.
  - **After each event the owner reports whether it is destroyed, and the loop returns `OwnerGone` straight away.**
    The arrival event swaps the modal, which queues the map for destruction. The object stays valid until the next
    `clearDestroyed`, which never runs inside a native or an injection.
  - A finished travel stays in `travel_`, so a later `InitiateTravel` on the same window is ignored (MW 6.3, kept),
    and `PauseTravel` on it is a harmless no-op.
  - Losing the foreground re-bases an unpaused travel on regain, so the flight does not jump over the time away.

**How `InitiateTravel` fits the coroutine.**

1. A click on a marker's popup releases the mouse, and `PopupWindow::onMouseUp` calls `runString("<port>:OnClick()")`.
2. `Main` is waiting, so `Port:OnClick` runs inside `Main`. It calls `InitiateTravel`, a plain native that only copies
   data, so nothing re-enters.
3. `OnClick` returns, and `Main` yields back into `HandleGetMessage`.
4. On later frames `World::tick` reaches the travel task, which injects each event:
   - A tick event (`PauseTravel(); TickSim(1); ResumeTravel()`) pauses and restarts within the same `now`, so no
     travel time is lost.
   - An event that opens a dialog yields `Main` while the travel stays paused. Its `ResumeTravel` runs on the frame
     where the dialog closes.
   - An event that fires while `Main` is suspended inside an earlier event's dialog nests, as in the original.

### 3.9 `CharWindow : BSGWindow` (`src/games/choc3/CharWindow.*`)

- **Fields:** the character name, `scale`, the sheet or texture, and a `MoodFade` (`shown`, `target`, `timer`;
  `src/games/common/MoodFade.h`, shared with Chocolatier 2). The window's alpha is the character's alpha: `init` sets
  it from the `alpha` key, and fades set it through `setSubtreeAlpha`, because the window takes fader alpha.
- **Image lookup** (PW 2.2): `characters/<name>.xml` through the `SheetCache`, else the PNG, else `temp_char`.
- **Size** comes from the size of frame 0 times `scale`.
- **`setMood(h)`** uses the frame table in PW 2.5. A change starts a 50 ms window animation. `onAnimate` adds 50 to
  the timer, and at 200 it switches to the target frame and stops.
- **`draw`** follows PW 2.4: the body, then the shown face, then the target face at `alpha * timer / 200`, all with
  the registration point placed at `(frame0.w * scale / 2, frame0.h * scale / 2)`. The BSGWindow frame and fill are
  not drawn.

### 3.10 `LedgerWindow : BSGWindow` and `DookieDropper : RolloverWindow` (`src/games/common/Ledger.*`)

- **`LedgerWindow`** holds:
  - a `MoneyCounter` (`display`, `target`, `delta`; `MoneyCounter.h`, shared with the Chocolatier 1 and 2 ledgers),
    with each target passed through `float` as LG 2 says.
  - `flashQuest_ = true` and `flashTimer_ = 0`.
  - `droppers_`, a vector of `WindowId`s collected in document order.
- **Children** are found by name when they are used (`money`, `day`, `rank`, `score`, `questText`,
  `ledger_questgoals`, `map_port`, `port_map`).
- **Text** comes from the existing `Date`, `Dollars` and string-table helpers in C++. `RealUpdateLedger` follows the
  LG 2 table exactly, with exact-match modes.
- **`DookieDropper`** is a DSL type. Its factory is its window name, its keys are those of LG 5, `update()` reads
  `Player.factories`, `_AllProducts` and `Player.supply` with `withGlobal`, and `postDraw` follows LG 5.2. Its product
  is built by `buildItemAppearance` (LG 5.1) from `custom/` layers.
- Chocolatier 2's ledger is a different window (`src/games/choc2/LedgerWindow.cpp`, a tabbed `Ledger` with a shared
  session and its own 100 ms counter rule). It is documented in the Chocolatier 2 design docs.

## 4. Order of work per frame

```
1  driver.update(now); input events -> wm.handleInput      // building clicks and popup clicks inject Lua here
2  wm.tickTransitions(now)                                  // 25 ms steps, platform clock
3  wm.tickWindowAnimations(now)                             // modal clocks: map clouds, mood fades
4  world.tick(now):
     port:   anim_ (50 ms fixed step: intro fade, hover pulse, update(50) per path sprite); sprite scripts
     map:    sprite scripts (markers); travel tick (30 ms, wall-clock; events inject into Main)
     factory
     ledger: 50 ms (money, quest flash, droppers)
5  host.tick(now)                                           // Main's timeout and poll resumes
6  pump: clearDestroyed (if anything died: sheets.trim(); textures.trim()), dispatchMessages
7  draw; screenshot; expect checks
```

**Invariant:** `clearDestroyed` runs only at frame steps. It never runs inside a native, an injection or a
`World::tick` callback, and that is what makes the `isDestroyed()` checks after Lua sufficient.

**Headless cadence** (16.67 ms frames):

| Task | Fires | Effect |
|---|---|---|
| Port task | every 3 frames | 50 ms per tick, real time |
| Travel | every 2 frames | 33.3 ms of travel per tick, about 90 ticks per week |
| Clouds | every 2 frames | +30 per call, so 90% of nominal speed, as the original at 60 Hz |
| Sprite scripts | on the first frame at or after their wake time | |

## 5. Harness additions (`src/app/ScriptDriver`)

- **`expect-lua <timeout-ms> <expr>`.** Every frame it evaluates `return (<expr>)` with `pcall` on the main state. It
  is never injected and never yields. It passes when the result is truthy. An error counts as false and is logged
  once. Example: `expect-lua 8000 Player.portName == "bali" and not gTravelActive`. This is the main state probe for
  travel, buying and selling, and reloading.
- **`expect-sprite <name-prefix> [min-count] [timeout-ms]`.** It counts visible sprites that are not `removed`, in
  shown game windows, whose debug name starts with the prefix (`WindowQuery::countSprites`):
  - Debug names are `bg`, `building:<name>`, `highlight:<name>`, `scenery:<image basename>`, `path:<image basename>`,
    `marker:<port>`, `label:<port>`, `ring`, `vehicle`, `dot` and `clouds`.
- **`dump`** prints `wm.dump()` at that frame. `--dump-windows` and `dump` both append each game window's sprite tree,
  one line per sprite: `sprite <name> layer=<n> pos=<x,y> scale=<s> frame=<i>/<n> alpha=<a> tint=<r,g,b,a>`, plus
  `hidden` or `removed` when they apply, with values rounded to 0.1. A
  `travel t=<0..1> time=<ms> paused=<b> events=<fired>/<n> dots=<n> mode=<name>` line is added when a travel exists.
- **No travel fast-forward.** Headless time is simulated, so a one-week trip is about 180 frames and runs in well under
  a second. `wait 3500` is enough.

## 6. Tests

Each area is tested headless; the real-data runs use the Reforged assets, and their screenshots are looked at by eye.
Targeted unit tests are in `tests/test_curve.cpp`, `test_sheets.cpp`, `test_sprite.cpp`, `test_port.cpp`,
`test_map.cpp`, `test_ledger.cpp` and `test_crates.cpp`.

**Unit tests** (doctest):

- **Curve:** a straight `{A,A,B,B}` is one piece of length |AB|; a curved Bezier's length is within 0.5% of a dense
  sampling; `at` leaves the tangent untouched at `t <= 0` and `t >= 1`, and returns the end-point index rule for
  7 and 10 points.
- **`PathSprite::update`:**
  - LOOP wraps; BOUNCE folds at 1; ONEWAY ends at the curve's end and sets `removed`.
  - The speed interpolates between far and near, using the y from the previous update.
  - The scale extrapolates.
  - With 16 frames, a tangent of (0,-1) gives frame 0, (1,0) frame 4, (0,1) frame 8 and (-1,0) frame 12.
- **`PortDef`** parsing from a nested `MapStyle`: the defaults, the motion strings, `behind`, and `scale` defaulting
  to -1 for path sprites.
- **Port behaviour:**
  - The highest layer wins, and a tie keeps the first.
  - The right and bottom edges of a building are exclusive.
  - `rolloverScale` is capped at 0.2.
  - The pulse at `rolloverTime` 0, 750, 1500 and 2250.
  - The intro brightness and label alpha at 500, 1500 and 2050 ms.
  - Labels are clamped to the window width.
  - Mouse-up on a different building does nothing.
  - The click string is exact.
- **Ticker:** due at `next`, re-armed from `now`, one tick per call, paused never fires.
- **Travel**, with fake hooks:
  - The dot count is `floor(t * len) / 12`.
  - Events fire in list order and stop at the first future one.
  - `t == 1` fires every remaining event.
  - Pause and restart discard the paused time.
  - An event that destroys the owner stops the loop (ASan clean).
  - A second `InitiateTravel` is ignored.
- **SpriteScript:**
  - The default `delay` is 16.
  - `frames{1,2}` sets frames 0 then 1, 16 ms apart.
  - `fadeout(4)` reaches alpha 0 in 4 frames.
  - `run{}` switches the sheet.
  - An error ends only that sprite.
  - The instruction hook stops `while true do end`.
  - `math` is nil in the sprite state.
  - The main state has no `frames`.
- **Sheets:** `image/travel_air.xml` has 16 frames with per-frame regs; `characters/main_alex.xml` gives 4 frames and
  its regs; the alpha of the merged `travel_air` texture; the default script text when there is no `<script>`.
- **Sprite tree:** `RecordingRenderer` draw order, with negative-layer children first and a stable layer sort.
- **Character:** the mood frame for n = 4 and n = 3 at h 0, 33, 34, 66, 67 and 100; the crossfade takes 4 ticks;
  retargeting mid-fade restarts it; `SetMood` reaches two windows with the same name and skips other names.
- **Ledger:**
  - The money counter reaches its target in 20 ticks without overshooting.
  - A retarget mid-count recomputes the step.
  - The flash toggles every 250 ms for 1500 ms and then stays "complete".
  - Grab, release and grab keep the same id and state.
  - A ledger destroyed with its modal is rebuilt by the next grab (ASan).
  - The `RealUpdateLedger` mode table, including an unknown mode.
- **Crates:** the factor table (sum 5.125x the bet, three zeros, rounding to hundreds).

**Scripted real-data runs** (CTest, each with a `check_*.py` that greps the log and dump):

- **`reforged_port_zurich`** (`check_port.py`): the screenshot at 0.5 s and at 3 s; moving over `zur_market` gives
  `expect-sprite highlight:zur_market`; a click gives the market dialog (`expect-window` on its name); path sprites
  move between screenshots.
- **`reforged_map`, `reforged_travel`, `reforged_kitchen`** (`check_map.py`, one case each): the map, a marker popup,
  and entering the current port. The travel case clicks an available port chosen from `Player.portsAvailable`, waits
  for `gTravelActive`, expects the `vehicle` and `dot` sprites, takes a mid-flight screenshot, then waits for
  `Player.portName == "<dest>"` and the port view, and checks that `Player.time` advanced by the route's weeks.
- **`reforged_ledger`** (`check_ledger.py`): the ledger in the port shows the money, date and rank; a purchase
  animates the money; port to map to port keeps the same ledger id.
- **`reforged_endtoend`** (`check_endtoend.py`): a new game, travel, buying and selling, the ledger dialogs, save,
  quit and reload.
- **`reforged_crates`** (`check_crates.py`), **`reforged_new_user`** (the first quest dialog shows a character) and
  **`reforged_focus`** (the foreground pause).

## 7. Open points

- **Frame quantisation.** Script delays snap to whole frames: a delay of 85 ms is about 100 ms at 60 Hz. The original
  re-arms from `now` the same way, but at its own loop rate, which is unknown. If waves look slow next to the
  originals, re-arm from the scheduled time instead.
- **Map labels.** The `TTextSprite` look (layers -1 and 3, the 0.75 alpha, the anchor under flags 4 and 6) is settled
  by `docs/re/game-windows-questions.md` §2 and §5.
- **`_AllPorts` iteration order** follows our Lua 5.0.2 `pairs`. It affects only which rollover is in front where
  two overlap, and the number of rings.
- **Not implemented:** the `curve_editor` branch of the port window, frame `xform`/`anchor` in sequence XML, movies,
  and saving travel progress (the original saves none, MW 11).
