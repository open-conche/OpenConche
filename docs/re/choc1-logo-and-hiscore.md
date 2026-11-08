# Chocolatier 1: the Logo builder (TLogo) and the high-score screen (THiscore)

The last two placeholder pieces of the C1 UI: the window type `Logo` behind `ui/logobuilder.lua`, `ui/logo.lua` and
the port sign, and the C1 dressing of the shared offline high-score screen. Facts from the Chocolatier 1 Windows exe (`Chocolatier.exe`)
(`re/choc1/decomp`, `objdump`) and the compiled Lua (a workbench script over `re/choc1/pfp_extracted/ui`).
Marked fact / inference as everywhere else.

## 1. The Lua side (fact, the bytecode)

* `ui/logoinfo.lua` defines the model tables and the two builders over `gSim.logo` (an **index-based** table: `bg`
  1..8, `graphic` 1..10, `pos` 1..4, `font` 1..6, `align` 1..9, `fontSize`, `fontColor`, `dropColor`) plus
  `gSim.companyName`:
  * `LogoBackgrounds = {bg1..bg8}`, `LogoGraphics = {logo1..logo10}`,
    `LogoGraphicsPositions = {{0,0}, {75,0}, {150,0}, {-800,-800}}` — pos 4 hides the graphic; the x values are
    `(350-200) * {0, 1/2, 1}` and they match Chocolatier 2's basic `SetGraphicPosition` slots
    (`docs/re/choc2-logo-and-trade.md`).
  * `LogoFonts = {stdFontFace, noir-et-blanc, romanuncial, edda, humanistic, baldur}` (`ui/styles.lua` 11-17), all
    `fonts/*.mvec` paths; the font sizes `kLogoFontSizeSmall..ExtraLarge` = 45/55/65/75 (`ui/logoinfo.lua` 20-23).
  * `LogoFontAlign` = the nine `kHAlign{kLeft,kCenter,kRight} + kVAlign{Top,Center,Bottom}` sums; align 5 is
    centre/centre, equal to TLogo's default `flags` 5.
  * `ShowPlayerLogo(t)` builds the window spec: `x, y, w` (350), `h` (200), `background = "logo/" .. bgN`,
    `graphic = "logo/" .. logoN`, `gx, gy` from the pos table, `text = gSim.companyName`, `textX/Y/W/H` 20/20/310/160,
    `flags = LogoFontAlign[align]`, `font = LogoFonts[font]`, `fontSize, fontColor, dropColor` from the model —
    then `return Logo(show)` (`:60`). While `gLogoBuildStage ~= nil` (the tutorial) it returns a `Group` with the
    `Logo` plus a transparent button over it (click: `DoModal("ui/logobuilder.lua"); SwapToModal("ui/portview.lua")`).
  * `PlayerLogo(t)` (`:68-88`) places the sign on a port building: on `t.building` (resolved, `owned`) at the
    building's x/y with `h = 200 * w / 350`, x/y clamped up at 0, down to `ShowPlayerLogo`.
* `ui/logo.lua` is the DynamicWindow body: `MakeDialog{ ShowPlayerLogo{x=0,y=0,w=262,h=150} }`. The builder's preview
  is the DynamicWindow `name="logo", contents="ui/logo.lua"`; the port data files (`data/sanfrancisco.lua` ...) call
  `PlayerLogo` for the factory sign.
* `ui/logobuilder.lua` (a 425-instruction chunk) runs the six-stage tutorial (`gLogoBuildStage` 1..6, Done/Back/Next
  buttons; `EnterCompanyName`, `SelectBackground`, `SetLogoImage`, `SelectImage`, `DoColorSelect`, `ColorButton`,
  `SelectFont`, `LogoComplete` label closures): every choice writes `gSim.logo.<key>` and calls
  `UpdateDynamicWindow("logo")`, which rebuilds the `ShowPlayerLogo` spec. Default model
  `{bg=2, graphic=10, pos=4, font=1, align=5, fontSize=Medium, fontColor=Yellow, dropColor=Brown}`. **No natives
  beyond `UpdateDynamicWindow`** — the whole editor is Lua.

## 2. TLogo the window class (fact; vtable 004e8504)

* **Init 00415a90** names and fixes the size first (`FUN_00421d20`, the TButton lesson again), then reads, with
  defaults: `background` ("logo/bg1"), `graphic` ("logo/logo1"), `text` (""), `flags` (5), `textX` (10), `textY`
  (10), `textW` (330), `textH` (180), `gx` (0, a **float**), `gy` (0, float), `font` (""), `fontSize` (30),
  `fontColor` (1,1,1,1), `dropColor` (0,0,0,1). `s = windowWidth / 350`;
  **canvas = (350*s + 0.5) x (200*s + 0.5), truncated** (the `fistp`-chop pattern), i.e. the 262-wide preview
  renders at 262x150 and the 106-wide port sign at 106x61; `textW`, `textH` and `fontSize` are re-rounded the same
  way (`ROUND(v*s+0.5)`, chop) - and **`textX` and `textY` too** (the stores at `0x415f84..0x415fa4`
  run `fild textX; fmul s; fadd 0.5; fistp-chop`, overwriting the raw values right after the canvas is built; keeping
  `textX`/`textY` raw would put the factory sign's company text bottom-right of the sign, since the raw placement (20,20)
  is not the centre of a 106x61 canvas). `gx`, `gy`
  keep their raw values.
* **The redraw 004155f0** runs once at init: it makes an offscreen image (`FUN_0042ae10`, the surface the object
  holds at +0x90), sets it as the render target, then:
  1. draws the background image **centred** at `(bgW*s/2, bgH*s/2)` with the draw size `natural*s` — for a 350x200
     art that is exactly the whole canvas (the image Draw of `FUN_004155f0`'s calls takes a centre and a scale;
     visible in `0x41572f..0x4157ad`: `w*0.5`, `h*0.5`, both `*s`);
  2. draws the graphic the same way, centred at `((gW/2 + gx)*s, (gH/2 + gy)*s)` (`0x4157db`,`0x415805`), so
     `gx=75` moves a 200-wide graphic 75*s = 56 px right at the preview's scale;
  3. builds two TextGraphics through `FUN_0044ad20` (the box is the scaled `textW x textH`, the flags the `flags`
     key, the size the scaled `fontSize`; one in `fontColor`, one in `dropColor`) and draws the **drop** copy at
     `+1,+1` and — only when `s > 0.5` (`0x415846: fld 0.5; fcomp scale`) — another at `+2,+2`, then the text in
     `fontColor` at the ALREADY-SCALED `(textX, textY)` (Init overwrote the fields; see above) — `FUN_00447aa0`,
     `0x415782..0x4158b0`;
  4. restores the target. Missing images skip their draw (the null ref checks of `0x415774`, `0x415802`).
* **Draw 00415410** (vtable slot) blits the whole canvas over the window's rect.

**Implementation** `games/choc1/Logo.{h,cpp}`: a CPU composite into an offscreen `ImageData`
(TextureCache::makeFromPixels), drawn as one texture over the rect. The exe's text layout is the engine's
TextLayout (`build` + `glyphRuns` + the coverage blit) at the scaled box; Chocolatier 1's `keepLastOverflowChar`
quirk applies to the company text. **INFERENCES:** bilinear resampling where the exe let its renderer filter; the
canvas keyed `"$logo-<window id>"` (a reallocation per rebuild, as the exe's one surface per window); markup in
`text` (bold/outline in the runs) honoured although the builder forbids those characters.

**Not shared with Chocolatier 2:** `games/choc2/LogoRenderer` renders the C2 key-based spec (HSV text colour,
free-space placement, rotation, alpha, `<b>/<i>/<outline>` markup), which does not match TLogo's contract
(absolute gx/textX boxes, RGBA colours, the pos table, no rotation). Only the pixel-compositing idiom
(over/sample/blit) is mirrored, kept private to choc1 so the green choc2 code is untouched.

## 3. THiscore's C1 dressing (fact)

`ui/hiscore.lua` builds the whole screen in Lua (`MakeDialog` with `HiscoreWindow{x=0,y=0,w=kMax,h=kMax}` over a
`Frame`, a `header`, a `gamemode` Text labelled `#GAMEMODE` — a string the table does not have —, the left panel
with rows `1..10` / `name<i>` / `score<i>` / `p1_<i>`, the right panels, `view`/`viewlocal`/`submit` buttons), and
opens with `bsutil:LoadGame(GetCurrentUserMode()); bsutil:LogScore()` (an SDK-side adapter; the engine's
`LogScore(score, a, b)` is shared). The window itself is the shared offline `game::HiscoreWindow`
(`choc3/HiscoreWindow.{h,cpp}`), with two C1-only deviations read in `THiscore`'s update (`0x416150`-area):

* the score column is `formatScore` — the installed table holds `"%1%/week"` — with the score in the money format
  of `FUN_0041e0e0` (the same `formatOnes/formatThousands/formatMillions/...` tables `UtilDollars` reads, hence the
  row constructor's placeholder `#$99,999,999`), **capped at 999,999,999** first
  (`if (999999999 < score) score = 999999999`);
* the `gamemode` label (the name exists twice in the exe's lookup strings, `gamemodeleft/right` beside it) is not
  fed by the Lua: it reads the player's mode and shows `empire_mode` ("Story Mode") or `free_mode`
  ("Free Play Mode").

`games/choc1/HiscoreWindow.{h,cpp}` subclasses the shared window: `scoreTextOf` (now virtual) and `updateText`
(virtual) set those; the row name, the `"%d."` rank, the p1 icon and the offline state machine are the shared
behaviour. The exe's info line (`score_info_format`-style with the weeks) is C3's: C1's table has no
`score_info_format` and the C1 rows carry no `info<i>` column.
