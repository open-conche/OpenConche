# Design: display

The window, its size and scale, fullscreen, the letterbox and how text stays sharp. The platform code is
`src/platform/Window.cpp`, `src/platform/DisplayMode.{h,cpp}` and `src/platform/SdlRenderer.cpp`; the text path is in
`src/gfx/text/`. Facts about the original come from `docs/re/focus-and-display.md` (cited as FD §n). The window tree,
input routing and the renderer interface are in [window-system.md](window-system.md).

## 1. What the original does

The games are laid out in a fixed 800x600 space (`kScreenWidth`/`kScreenHeight`, `kGameScale` 1.0, FD §3) and never
re-laid out: a fullscreen switch keeps the window, the GL context, the textures and the window tree, and only moves the
viewport (FD §3). The whole 800x600 frame, text included, is stretched to the screen. The Options "Full Screen" toggle
calls `ToggleFullScreen()`, expects a boolean (false opens the `screenswitchfailed` dialog) and stores the `fullscreen`
pref from the live state (FD §1 event 0x13, FD §3).

## 2. One logical space, scaled at presentation

- **Layout and input are logical.** Every window, hit test and script coordinate is in 800x600 logical units
  (`kLogicalWidth`/`kLogicalHeight`). Nothing in `src/ui`, `src/games` or the Lua knows the output size, exactly as in
  the original.
- **SDL's logical presentation does the mapping.** The renderer runs with
  `SDL_SetRenderLogicalPresentation(800, 600, LETTERBOX)`. SDL scales the backbuffer to the largest 4:3 rectangle that
  fits the output, centres it, and converts mouse events back with `SDL_ConvertEventToRenderCoordinates`. A click in
  the bars arrives with a logical position outside 800x600 and `inArea == false`; the window system ignores it.
- **Sprites are the games' own 800x600 art, scaled on the GPU** (linear filtering). They cannot be sharper than the
  shipped pictures.
- **Text is rasterised at the output resolution.** `TextLayout::drawImpl` reads `Renderer::outputScaleXY()`, which is
  the letterbox rectangle's size over 800x600 (`SDL_GetRenderLogicalPresentationRect`), and asks the glyph atlas for
  glyphs at that scale. Glyphs come from the games' own font outlines (`.mvec`), so text is sharp at any output size,
  integer or not. This is the one place where the engine shows more than the original could: the original stretched an
  800x600 raster, and its text blurred with everything else.
- **One source of truth.** The engine never re-derives the letterbox: the renderer, the text scale, the input mapping
  and the `[display]` log lines all read SDL's own rectangle (`SDL_GetRenderLogicalPresentationRect`). SDL fills the
  output along one axis and floors the other side to whole pixels, so 1600x1000 gives a 1333-wide picture at x 133.5
  and the logged scale is 1333/800. Only the scenario harness computes the rectangle for itself (`letterbox()` in
  `tests/check_scene.py`, the same rule), because its job is to predict click positions and bar edges without asking
  the engine.

## 3. The window

- **Initial size** is `800*scale x 600*scale` for `--scale 1|2`. `chooseScale` lowers the scale until the window fits
  the usable desktop (with room for the frame), and a second pass after creation uses the real border sizes.
  `--scale` sets only the initial size.
- **Resizable.** The window has `SDL_WINDOW_RESIZABLE` and a minimum size of 800x600, so the picture is never shown
  below 1x. Dragging the frame or corner, maximising, and a tiling window manager's own placement all just change the
  output size. `SDL_EVENT_WINDOW_RESIZED` and `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` re-apply the logical presentation
  and surface as one `InputEvent::Type::Resized` per poll with the output pixel size; the App logs
  `[display] resized, output WxH, scale S`. Nothing is re-laid out and no texture is rebuilt.
- **High-DPI.** `SDL_WINDOW_HIGH_PIXEL_DENSITY` is set, so on a Retina or scaled desktop the output is in device
  pixels and the scale reflects them (an 800x600-point window on a 2x display renders text at 2x).

### Aspect ratio: letterbox

The game is 4:3. A window or display whose shape does not match gets black bars: no distortion, no cropping, and the
same picture on every desktop, including a maximised window and a tiling window manager, which give the window
whatever size they choose.

- `SDL_SetWindowAspectRatio` is not used. In the SDL this engine builds against (3.4) only the Cocoa and X11 backends
  implement it, and under X11 it is a size hint that window managers are free to ignore; the Windows and Wayland
  backends have no implementation. One setting with three behaviours is worse than none. The letterbox produces the
  same picture everywhere, and loses nothing: the bars are the pixels a 4:3 lock would have taken off the window anyway.

## 4. Fullscreen

- **Mode: borderless desktop fullscreen.** `SDL_WINDOW_FULLSCREEN` with no exclusive display mode set, so SDL uses the
  desktop's own resolution and the picture is letterboxed inside it (2.4x at 1440p, 1.8x at 1080p). An exclusive mode
  switch would buy nothing (the game cannot use more than 4:3 at the desktop's pixel density) and would cost a mode
  change on every Alt+Tab, a dark screen while the display re-syncs, and on macOS a fight with Spaces.
- **The OS controls agree with the game.** On macOS, SDL maps desktop fullscreen of a resizable window onto Spaces
  fullscreen, which is what the green traffic-light button does, so the button, Alt+Enter, F11, `--fullscreen` and the
  Options toggle all set and clear the same SDL flag. On Windows and Linux the window menu's fullscreen and the
  maximise button give a borderless window the size of the work area; the picture letterboxes in it like any other
  resize.
- **State and the pref.** `Window::isFullscreen()` is SDL's flag, never a shadow copy. Every change is reported as one
  `FullscreenChanged` event, whichever side started it: the hotkeys and `toggleFullscreen()` report at their place in
  the event sequence, and `SDL_EVENT_WINDOW_ENTER_FULLSCREEN` / `LEAVE_FULLSCREEN` report a change the OS made (the
  green button, the window menu, leaving a Space). The window keeps the last reported state and emits only on a
  difference, so a toggle that SDL then confirms is reported once. The App writes the `fullscreen` pref from each
  report (FD §1 event 0x13 does the same), so a player who leaves fullscreen with the OS control starts windowed next
  time, and the Options checkbox, which reads the live state, shows what is on screen. The Options toggle saves the
  state it applied (`fullscreenState()`), not SDL's live flag: a Spaces transition on macOS is asynchronous and the
  flag can lag the request. Two toggles in quick succession can make the late OS events report the intermediate
  state and then the final one; the final state is right. A request the desktop refuses (Cocoa reports
  `windowDidFailToEnterFullScreen` and SDL clears the flag before `SDL_SyncWindow` returns) is a failed switch: the
  flag after the wait decides, the state and the pref stay as they were, the hotkey reports nothing and logs
  `[display] fullscreen on refused by the desktop`, and the Options toggle returns false, which shows the game's own
  "screen switch failed" dialog (FD §3). A Space can be refused when the app is not the active application, which is
  what a scripted run from a shell is; a player pressing the key in the focused window is.
- **`--fullscreen`** creates the window with the flag; `--scale` still decides the windowed size the player returns
  to. Leaving fullscreen through SDL restores the previous windowed size, including a size the player dragged to.
- **Headless** has no desktop. Fullscreen is emulated by sizing the dummy-driver window to `--display-size` (default
  2560x1440) and back to the current windowed size; the same logical presentation applies, so the letterbox, the
  input mapping and the screenshots are the ones a real display of that size would give. The script command
  `resize W H` sizes the window the same way, applied at its place in the event queue so a click queued before it is
  still mapped with the old size; it ends the frame like `key`, so the next command sees the new size. A `resize`
  while emulated fullscreen is on changes the output but not the fullscreen state or the windowed size to return to,
  as a real desktop does.

## 5. Text across size changes

The glyph atlas keys each glyph on the exact raster scale, so a new output size misses and rasterises again at the new
scale, which is the point. What must not happen is the old scale's glyphs piling up until the atlas overflows mid-frame.
`GlyphAtlas::setOutputScale(scale)` is called by the App once per frame, right after `beginFrame()` when nothing is
batched: when the scale differs from the one last given and the atlas holds entries, the atlas is emptied there and
then (entries cleared, pages zeroed and marked whole-dirty, page count trimmed to `kMaxPages`). A continuous drag
therefore re-rasterises the visible text once per distinct size, pages never exceed `kMaxPages`, and the atlas only ever
holds glyphs of the current scale. The overflow wipe at `beginDraw()` stays as the backstop for a single scale that
needs more than four pages.

## 6. Tests

- **Unit** (`tests/test_display.cpp`, `tests/test_text.cpp`): `chooseScale` fallback; the atlas emptied by a scale
  change and left alone by an equal one, and bounded pages over fifty alternating scales.
- **Scenarios** (`tests/check_scene.py`, `tests/check_text.py`): `scene_scale1/2` (windowed, the hotkeys),
  `scene_fullscreen_<WxH>` (bars, predicted positions, bar clicks logged `(outside)`), `scene_resize` (the `resize W H`
  script command: 1600x1000 with side bars and clicks mapped through the new letterbox, 1600x1200 at exactly 2x, back
  to 800x600), and `text_scene_resize`, which proves the sharpness claim: in the paragraph region of a 1600x1200 shot,
  strong luminance steps (over 60% of the sharpest step in the 800x600 shot) appear between the two columns of a pixel
  pair, where a stretched 800x600 frame cannot put one (nearest repeats the column, bilinear leaves at most half the
  step there; the test stretches the 800x600 shot both ways and checks it finds none), and the region holds more
  strong steps than the 800x600 shot. The text tests need a game's fonts and skip (exit 77) without the Reforged assets.
- **By hand**: drag the corner on each desktop, press the green button or maximise, toggle Options "Full Screen", and
  leave fullscreen with the OS control; the Options checkbox and the log must agree at every step.

## 7. Choices and limits

- No `SDL_SetWindowAspectRatio` (§3).
- No exclusive fullscreen mode and no resolution setting; the desktop's resolution is the only one used (§4).
- Sprites stay the shipped 800x600 art, scaled; only text gains from the output resolution (§2).
- A size below 800x600 is refused by the minimum size, not letterboxed down: the games' smallest controls are a few
  pixels wide at 1x and are not usable smaller.
