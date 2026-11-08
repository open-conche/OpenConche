# Chocolatier 1: OptionsDialog, CreditsWindow and the Text clip rule

Facts come from the unpacked exe (`re/choc1/decomp`, `objdump` of the Chocolatier 1 Windows exe (`Chocolatier.exe`)) and the compiled `ui/options.lua`
and `ui/credits.lua`. Marked fact / inference.

## 1. OptionsDialog (TOptions, vtable 004e8844)

- **Lua** (`ui/options.lua`, fact): `Bitmap image/half` at (190,186) holding `OptionsDialog{x=43, y=33, w=330, h=152}` with `Text` labels
  (`opt_sfxlevel`, `opt_ambientlevel`, `opt_musiclevel`), `Slider` children named **`sfxslider` `ambientslider` `musicslider`** (value 0),
  toggle `Button`s **`fullscreen`** and **`mute`**, `ok` (calls `SaveVolumes()` then `PopModal('options')`) and `opt_credits`
  (`DoModal('ui/credits.lua')`). The `fullscreen` command is `if not ToggleFullScreen() then DisplayDialog{'ui/okdialog.lua', body='opt_nofullscreen'}`;
  `mute` calls `ToggleSoundMute()`.
- **The mute box is `mute`, not `soundoff`** (fact: `PostInit` 004186b0 does `find("mute")`; `choc1-natives.md` section 9 said `soundoff`, which is the pref
  it reads, not the child). PostInit sets: `fullscreen` to the display state (a platform query, 0041c030), `mute` to `pref soundoff == 1`, and each slider
  to `pref/1000` with the defaults 750 (0x2ee) (the sfx one is read in the code; the other two follow the same pattern; `soundlevel`,
  `musiclevel`, `ambientlevel`).
- **Sliders apply live** (fact: the message handler 00418c60 answers a slider's value message by calling the mixer's level setter for the
  named channel, ids 0x3e8..0x3ea); `SaveVolumes` (00418a10) only writes the prefs (value times 1000, truncated). OK is the only thing that stores
  them. Same as Chocolatier 3's `ui::OptionsWindow`.
- **Reuse decision:** the contract equals Chocolatier 2's `Options` (child names `fullscreen`/`mute`/three sliders, same prefs, same live handler), so
  the class was lifted to `games/common/OptionsPanel` (`game::OptionsPanel(wm, ctx, luaTypeName)`: choc3's `ui::OptionsWindow` plus the fullscreen
  and mute initialisation). `choc2::Options` and `choc1::OptionsDialog` are one-line subclasses naming the Lua type. Chocolatier 2's behaviour is unchanged
  (its tests and goldens pass). `SaveVolumes`, `ToggleFullScreen`, `ToggleSoundMute` were already real (`script/UserNatives.cpp`), the volume defaults
  come from the profile (`{750,750,750}`), so the dialog needed no new native.

## 2. CreditsWindow (TCredits, vtable 004e6cb4)

- **Init 00407b00 (fact):** keys `time` (+0x58), `intropause` (+0x5c), `columngap` (+0x54), `columnwidth` (+0x60), `font`, `fontsize` (16), `fontcolor`,
  `headercolor` (`%02x%02x%02x%02x`), `file`. It reads the file by lines (up to 1023 characters), drops each line's last character, splits at the first `|`
  into left and right columns (the centre column gets a blank line), else a full-width line goes to the centre column (blank in left and right);
  `[`/`]` become `<font color='<header>'>` / `</font>`; spaces and tabs are trimmed (`FUN_004077b0`). Then one more line, **"Chocolatier version "
  + the `version` state value**, is appended as a full-width line. Then floor(window height / fontsize) blank lines pad each column. Three text layouts
  (column width `columnwidth`, centre width `2*columnwidth + columngap`). `H` = the left column's text height / 2 (an exe quirk: the last half is never
  scrolled into view; the engine keeps Chocolatier 3's `kCreditsScrollFullHeight` deviation: H = the whole height).
- **Draw 00408470 (fact):** the clock starts at the first draw; `e = now - start`; `e = e < intropause ? 0 : e - intropause`; `scroll =
  (e mod time) * H / time` pixels. So: nothing moves for `intropause` (3000 ms), then the whole column scrolls past in `time` (20000 ms) at a
  constant rate of H / time px per ms, and it **loops** (the pause is only before the first cycle). It never closes itself; the dialog's OK closes it.
  Columns are drawn at x = `columngap` (left), `w/2 + columngap/2` (right) and `columngap` (centre) with the draw rectangle's top at `-fontsize`.
  This is Chocolatier 3's `ui::CreditsWindow` exactly, so Chocolatier 1 derives from it (`games/choc1/CreditsWindow`).
- **What Chocolatier 1 adds:** (a) the Lua `file` is `assets/credits.txt`, relative to the install folder (the file is the loose
  `assets/credits.txt` next to `assets.pfp`), while the engine's asset root is that `assets` folder, so a leading `assets/` is dropped (the first
  run showed `[credits] cannot open 'assets/credits.txt'` and an empty window); (b) the version line. **Inference:** the number is the part after
  the last comma of settings.xml's `gameversion` (`dl,pf,1.0`, so "Chocolatier version 1.0"); the exe builds the `version` state while it
  reads the settings and the split was not decoded. (c) CRLF line ends are single-spaced (as for Chocolatier 3; the exe's reader on Windows was not checked).

## 3. The Text clip rule (the port's pause label)

`Text{x=0,y=30,w=56,h=30, label="menu" (-> "Pause"), font={bolton 28}, flags=kVAlignCenter+kHAlignCenter}` draws "Pause" on one line. Read in the exe:

- `TText` has **no clip, no scissor and no font shrink**. Its draw (`00446560`, `FUN_00447aa0`) brackets a `Begin2d`/`End2d` pair and draws the lines of its
  `TextGraphic` into the rect `{0,0,w,h}`; the only bounds are vertical: it stops at the first line whose bottom (line top + font size) is below `h`
  or above 0.
- The layout (`FUN_0044a4d0`, the `RenderText`) breaks a line when `lround((outlineL + 1 + outlineR) * 4 + x) > 4 * w` (constants read: 1.0, 4.0, 0.5):
  at the last space, else before the character that overflowed. Glyph metrics (`FUN_00479fb0/0047a000/0047a050`: advance, minX, maxX at record offsets
  0x18, 0x10, 0x12 + 0x12 per glyph in 1/128 units), the font scale `0.8 * size * 4 / (ascent + descent)` (`DAT_00529584/88` = 0.15, 0.05), the ascent from
  `W`, the descent from ASCII without `_` (the `y`-only path is behind `DAT_005331d4`, which is never written) and the first-glyph pad `4 - minX * scale`
  all equal the engine's `gfx/text` (checked by computing the pen of "Pause" from `fonts/bolton.mvec`: 226.6 quarter pixels after the `e`, against the
  limit 224 - 4 = 220).
- The exe keeps an overflowing *last* character on its line (`0x44a9cc..0x44a9d7`), so it draws "Pause" on one line; the engine does the same for
  Chocolatier 1 (`docs/re/choc1-text-flags.md`). The overflow test itself does fire: it includes the first-glyph pad and the `+1` margin (`44a959..44a99a`),
  and the pen of 226.6 quarter pixels exceeds the limit 220, so the text would not fit in 56 px without the keep-last rule.

## 4. Separate pulsers (UtilPulseImage and the Rollover popup)

`Choc1World::pulses` entries carry `popup`. `UtilPulseImage` restarts only a non-popup entry of that name; the Rollover popup creates and erases only its own
(`popup == true`) one; the image's tint is reset on close only when no other pulser of that name is left. Both run (as two `TImagePulser` tasks would) and
set the same tint, so while both live the tint follows whichever ticked last.

## 5. The checkbox buttons' size and the rail piece's orientation

Two defects of the options dialog read against the exe; both were engine-side, not Lua-side.

**(1) `fullscreen`/`mute` lost their width (labels never drew).** The Lua (fact, `ui/options.lua` 28/37) gives the checkbox buttons
`w=120, h=26` with `scale=26/32` graphics, and their `Text` children sit at `x=26` with `w=h=kMax` (160000), so a 120-wide parent
resolves the label to 94x26. Our `ButtonWindow::init` fit the image layers **first** (`FUN_00422280`'s semantics, 26x26 from the scaled
bitmaps) and `SizeAndPositionFromStyle` never overwrites a non-zero size, so the button stayed 26 wide and the label resolved `w = 0`
("`[text] layout into a 0x26 box ignored`"). Read in the exe (fact): `TButton::Init` calls `FUN_00421d20` (name + `FUN_004216c0`,
the style size, **before** the graphics loop) and `FUN_00422280` after it, and `FUN_00422280` seeds `GetLargest` with the window's
**current** size — the fit is grow-only. So the order is: style w/h first (on an empty rect), images after, fit keeps 120x26.
The engine does the same (`Window::init` before `fitToChildren`, one fit). A button **without** style w/h is unchanged: the style
fix leaves the rect at 0 and the images (or the labelled Button wrapper's own `FitToChildren` over its Text) give the size — the `ok`
button still measures 108x24 (the 180x40 `control/std_up` at scale 0.6). This is the shared `TButton`, so Chocolatier 2 and 3 are
affected in principle; their menu/options/endtoend screenshots were compared and only the intended ones changed (Chocolatier 2's
options dialog, whose checkboxes have the same 120x26 pattern, and two Reforged quit-confirm labels re-centring inside their now
correct style width); `build/baseline_out` was refreshed for those six files.

**(2) The slider rail covered the label text.** `sliderStyle` (`ui/styles.lua`, fact) is `railtop/railmid/railbot/control/sliderrail*`,
`sliderimage`, `yoffset=-1`; the rail art is 24 wide with a 9-pixel visible band at image x 12..20. TSlider draws every piece through
`FUN_0043c570` + `FUN_0043bdc0` (`FUN_0043cc50` walks top/mid/bot): for a horizontal slider the piece transform is the matrix at
`0x525e04` = `(0,1,1,0)` — screen x from the image's y (the cap at the left), screen **y from the image's x, mapping image x=0 to the
top** of the piece rect (`((H - art width) / 2 = 2)`-top, art width 24 in a 29-high window). Our `-90`-degree rotation alone maps image
x to **-y** (the bottom), which put the visible band at slider-local y ~6..14 instead of ~14..22 — straight across the label Text's
last rows (the label box is 5..25 at the slider's 10..39). The engine source-flips the horizontal piece (`hflip`), placing the band
at the bottom as in the exe; the knob (never rotated, at `yoffset`) and vertical sliders are unchanged. Verified in pixels: the sfx
label's ink (global y 227..238) and the rail band (243..251) no longer intersect.
