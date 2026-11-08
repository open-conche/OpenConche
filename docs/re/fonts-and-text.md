# Fonts and text drawing in Chocolatier 3

Sources, in order of trust: the Mac binary decompile in `re/mac/decomp/` (`TMvecFont.c`, `TFontData.c`,
`TTextRun.c`, `TTextState.c`, `TTextGraphic.c`, `TText.c`, `TTightText.c`, `TTextEdit.c`, `TCustomTextEdit.c`,
`TPfMiniVecDraw.c`), floats and strings read directly out of the Mach-O (PIC offsets resolved by hand),
`TightText.h` and `customtextedit.h`, Reforged's converters in
the Reforged mod's `development/tools/fonts/`, and the Lua in the Reforged mod's `assets/`.
The Playground tool `Playmate Font Maker.exe` was checked with `strings` only (see 1.7).

Everything below is "Fact" (read from the binary) unless marked **Inference** or listed under Unknown.
Addresses are Mac image addresses. Where I say "string at ..." I resolved the PIC-relative literal myself.

## 1. The `.mvec` file format (`MVC4`)

### 1.1 Layout (little endian)

| Offset | Size | Meaning |
|---|---|---|
| 0 | 4 | magic `"MVC4"`. `TMvecFont::TMvecFont` compares 4 bytes; on mismatch it returns with 0 glyphs and the font is unusable. |
| 4 | u16 | glyph count N |
| 6 | 6 | reserved, all zero in all 14 shipped files. The loader never reads them. |
| 12 | 18*N | glyph table, sorted by ascending code point (required: lookup is a binary search) |
| after table | ... | glyph outline programs, addressed by the table's offset field |

Glyph table record (18 bytes), field offsets from the record start:

| +0 | u32 | Unicode code point (UTF-32 value; shipped files go up to U+2F9F4) |
| +4 | s16 | minX |
| +6 | s16 | maxX |
| +8 | s16 | minY |
| +10 | s16 | maxY |
| +12 | s16 | advance |
| +14 | u32 | byte offset of the outline program, from the start of the file |

All s16 values are **9.7 fixed point**: real value = raw / 128.0 (the binary multiplies by the float 0.0078125
at `0x1ab7a4`). Range is therefore -256.0 .. +255.99. Y grows **downward** (screen convention): glyphs sit above
the baseline at negative Y. `minY` is the top (most negative), `maxY` the bottom. Reforged's `ttf_to_mvec.py`
flips TTF Y for this reason (`--flip-y`).

The engine keeps the whole file in memory and uses the table in place (`GetGlyphData(i)` = base + table[i].offset).
Index lookups outside `0 <= i < N` return 0 / 0.0 (advance, minX, maxX) or 1.0 (scaleY).

### 1.2 Outline program (one per glyph)

A byte stream interpreted by `TPfMiniVecDraw::Draw` (jump table of nine entries, opcode byte 0..8; anything
above 8 loops back to read the next byte). Coordinates are s16 / 128 as above. Opcodes (Reforged's reader and
writer, and the engine's operand sizes, agree):

| Op | Bytes | Meaning |
|---|---|---|
| `02 01` | 2 | start of glyph / fill pass. The engine reads one operand byte; if it is 0 it is skipped, otherwise it enters a fill-pass setup (**Inference**: operand = number/ID of fill layer). The shipped converter always writes `02 01` as the first two bytes of any non-empty glyph. |
| `03 nn` | 2 | second 2-byte op with the same shape (**Inference**: another layer/pass selector). Never written by the converter. |
| `04 x y` | 5 | MOVETO (begins a contour) |
| `05 x y` | 5 | LINETO |
| `06 cx cy x y` | 9 | CURVETO, **quadratic** Bezier (one control point, then end point) |
| `08` | 1 | end of glyph |
| `00`, `01`, `07` | - | jump-table slots, not emitted by any tool (`00` and `01` appear to be reset/continue handlers; not decoded) |

The converter appends an explicit closing LINETO back to the contour start unless `--no-close`; the engine's
edge builder works on filled regions so closed vs open contours should both render. An empty glyph (space) is
just `08`.

### 1.3 Em size, ascent, descent

There is **no em size field**. The unit scale differs from file to file (see the table in section 5): `arial.mvec` is
about 1.0 unit per em, the Fertigo-derived files about 233.26 units per em (the number in Reforged's
`--scale-xy`). The engine makes this irrelevant by normalising on load (`TMvecFont` constructor, 0xcd66a):

* **Ascent** = `-minY('W')` (the glyph for code point 0x57, looked up by binary search; if 'W' is missing the
  ascent is computed from a bogus index and is effectively garbage, so every font must contain 'W').
* **Descent**: a global flag byte (`0x21f4c1`, zero in a normal run; nothing in the code I decoded sets it)
  selects the method. With the flag 0 (normal): descent = the maximum `maxY` over every code point in 0..0x7f
  that exists in the font, skipping 0x5f (`_`). With the flag nonzero: descent = `maxY('y')` (0x79).
  (Reforged's README says "y for descenders"; the real default scans all of ASCII except underscore.)
* `GetAscent`/`GetDescent` return those floats; all are in the font's own units.
* `GetLinePercent()` = 1.0 - 0.15 - 0.05 = **0.8**.
* `GetLeading(top)` = with `s = 0.20`, `base = (ascent+descent)*s/(1-s)` = 0.25*(ascent+descent); returns
  `0.15*base/0.20` (= 0.1875*(a+d)) when `top` is true, `0.05*base/0.20` (= 0.0625*(a+d)) when false.
* **Per-glyph vertical squash** (`GetGlyphScaleY`, array filled in the constructor): for each glyph compute
  `up = -minY/128 / (0.75*base + ascent)` and `down = maxY/128 / (0.25*base + descent)`. If either exceeds 1.0
  the glyph's Y scale is `1 / max(up, down)`, otherwise 1.0. So tall glyphs (accents, CJK) are squashed
  vertically, not horizontally, to fit the line box that Section 3 defines. (Applied at draw time as the Y scale.)
* Other members: `forceBold` (byte at +0x18, default false) and `boldScale` (float at +0x1c, default 1.0); a
  `SetBoldOverride(bool,float)` setter exists but I did not find its caller. Object size 0x20.

### 1.4 Lookup

`GetGlyphIndex(codepoint)`: binary search over the copied code point array; returns -1 if absent. There is no
cmap/range logic and no fallback inside the font object.

### 1.5 Kerning

None. No pair table exists in the format or the loader. Reforged's README says the same ("No kerning tables
supported"). Spacing is purely per-glyph `advance` (plus the bearing handling in 3.4).

### 1.6 Hinting

None. Outlines are scaled by floats and rasterised at quarter-pixel positions (section 2).

### 1.7 Playmate Font Maker (Windows tool)

`strings` on `Playmate Font Maker.exe` shows a Qt app whose only format-specific strings are `MVC4`,
"Export Mvec Font File", "Glyph index %d is larger than maximum glyphs %d", "Mismatched glyph counts." and
"ERROR: Should never reach here during glyph contour loop!". Its help text calls the format "an outline format".
Nothing mentions kerning, hinting, or an em-size field; this agrees with "no kerning, no hints, no em field".
The tool's `Character Sets/*.txt` files are just code point lists. I did not run Ghidra on it; operand layouts
above come from the game binary and the Reforged scripts.

### 1.8 Embedded fallback font and file lookup (`TFontData::GetMvec`, 0x83c76)

`TFontData` is a `std::map<str, TMvecFont*>` cache owned by the platform object. `GetMvec(name)`:

1. cache hit: return it (a cached NULL stays NULL).
2. If `name == "__DEFAULT"`: use the **embedded** font (a 0x12c2c byte copy of an MVC4 blob compiled into the
   binary).
3. Otherwise `pf_open(name)` (the virtual file system, so it goes through the pack/overlay search path). If that
   fails, try `"fonts/default.mvec"`. If that fails too, use the embedded blob.
4. Insert into the cache under the **requested** name (so a missing font permanently maps to a fallback object)
   and return it. If construction yields NULL it logs `Font not found: %s`.

So any bad font path silently becomes `fonts/default.mvec` (not in the Reforged `assets/fonts/` listing; probably
absent, in which case the embedded font is used). A font name is passed through unchanged; `style.lua` already
includes the `fonts/` prefix and `.mvec` suffix.

## 2. Rasterisation (`TPfMiniVecDraw`)

Text is **not** textured quads from a glyph atlas. Each glyph outline is flattened and scan-converted on the CPU
by `TPfMiniVecDraw` (a "pflib" mini vector drawer), and the result is submitted as coloured, alpha-varying
horizontal spans (two triangles each) through `TRenderer::DrawVertices`, or written straight into a locked
texture's pixel buffer (`FlushToPixelBuffer`) when a target texture is supplied. TText passes no texture, so
screen spans are drawn.

Facts:

* **Sub-pixel grid is 1/4 pixel**: the pen is kept in quarter-pixel units; `Start()` splits the destination into
  an integer pixel origin and a 0..3 sub-pixel fraction (`x*4 & 3`, `y*4 & 3`); `Draw()` passes that fraction on.
  Pixel-space rendering result for a given glyph depends on (glyph, sub-x 0..3, sub-y 0..3, X scale, Y scale,
  two stroke widths, italic flag, rotation cos/sin). That tuple (0x24 bytes) is the key of a **glyph span cache**:
  a hash table of 0x7f7 buckets, chained, keyed by the glyph program pointer, with a linear compare of the 9-dword
  tuple. A hit replays stored spans (x0, x1, y, coverage index, layer) without re-rasterising; a miss rasterises,
  records spans into a growing 8-byte-record list (blocks of 0x2000), then replays them. The cache is unbounded.
* **Curve flattening**: each quadratic is converted to exactly **3 line segments** (t = 1/3, 2/3, 1); no
  adaptive subdivision. Coefficients: A = P0 - 2*P1 + P2, B = 2*(P1 - P0), point = A t^2 + B t + P0.
* **Italic is a synthetic shear**: when the run's italic flag is set, every edge point is x += y / -3
  (y is negative above the baseline, so tops lean right): slant 1:3 (about 18.4 degrees). The matching layout
  offsets are in 3.4.
* **Edges** (`SetEdge`): each segment is stored with Y ordered low-to-high and a direction flag; segments with
  both ends in the same integer row are dropped; each is linked into a per-row list (row index offset by half a
  3200-row buffer, so up to +-1600 sub-rows). Rotation about origin applies a 2x2 matrix when the TText's
  rotation is non-zero.
* **Fill rule and AA (Inference, from the accumulator code in `HLineBatchFlush`)**: four horizontal accumulators of
  3200 cells each (one per sub-row 0..3 within a pixel) hold signed winding deltas that are summed left to right
  (non-zero style accumulation); a run of constant accumulator value becomes one span. The span colour index is
  `sub-row * 32 + level`, where `level` is the accumulator value; `SetColor` builds a 32-entry palette per layer:
  entries 0..15 have alpha `(16*(k+1) * A) >> 8` (A = the colour alpha 0..255) and entries 16..31 have
  `(255 * A) >> 8`. Four sub-rows are each drawn one pixel high with these partial alphas and blend
  over each other, giving about 4 vertical x 4 horizontal-position levels of coverage. A second 4-entry palette
  (alphas A*{0x11,0x28,0x49,0x80}>>8) exists for the other layer. I did not fully decode how the levels map to
  coverage, so reproduce the look with a normal 4x4-supersampled non-zero fill; do not treat the 16/32-level
  tables as specification.
* **Outline/stroke layer**: `SetEdgeOutline` turns a segment into a rectangle (6 edges) of half-width
  `4.0 * width / scale` in glyph units, i.e. a stroke `width` pixels each side, drawn as the second layer. It is
  used for both `<outline>` and synthetic bold (3.7). Stroke ends are flat.
* **Pixel centre calibration**: `Calibrate()` sets offsets 0.5, 0.5, 0.5 (pixel-centre bias) used when emitting
  vertices.
* Output pixel format when writing to a texture: BGRA bytes, straight-alpha blend (`dst = (dst*(255-a) + src*a)/255`)
  unless the "premultiplied/replace" flags are set. Colour alpha comes from the text colour times the TText alpha.
* Sizes: there is no fixed set of sizes. Any integer pixel size is rasterised on demand (cache key includes scale).

### Mapping a font `size` to pixels

`size` in `font = { face, size, color }` is the **line-box height in pixels** (an integer; `7.5` in Lua is
truncated to 7 by `(int)`; `0` becomes 12). Per run (`TTextRun` ctor, 0x83fba):

```
scale (quarter-pixels per font unit) = 0.8 * size * 4 / (ascent + descent)     // x4 because the pen is in quarter pixels
if the face name contains the substring "lithos": scale *= 0.7                  // legacy special case
run.height (px)        = size
run.baseline (qpx)     = (int)( scale * (ascent + GetLeading(top=true)) )
```
So the em box (ascent+descent) is 0.8*size pixels tall; the line box is `size` px: 0.15*size above the em box and
0.05*size below. Baseline y from line top = 0.15*size + 0.8*size*ascent/(ascent+descent) px. Horizontal and
vertical glyph scale are equal (except GetGlyphScaleY squash).

## 3. Layout (`TTextGraphic`, `TTextRun`, `TTextState`, `TText`)

A `TText` window owns a `TTextGraphic` (+0x30). `TTextGraphic::RenderText()` (0x842d0) re-lays out the whole
string whenever text, font, size, colour, flags or line-height change (`SetText/SetFont/SetFlags/SetLineHeight/
SetColor` all call it, only if the value changed). The result is a vector of lines; each line has runs.

### 3.1 Units and fields

Layout works in **quarter pixels** (4 units = 1 px) horizontally. `TTextGraphic`: +0x18 width (px), +0x1c height (px),
+0x40 font face string, +0x44 font size, +0x48..0x54 colour (4 floats), +0x58 first visible line (float),
+0x5c vertical offset (px), +0x64 flags, +0x34 line vector, +0x20/+0x24 cos/sin rotation. `TText`: +0x34 scale
(1.0), +0x38 alpha (1.0), +0x3c "padding" (extra pixels added after every line), +0x40 scroll fraction (0.5),
+0x44 pressed-link id.

### 3.2 Style keys read by `TText::Init` (0x55550)

`font = { face, size, color }` (positional 1,2,3; defaults `"__DEFAULT"`, 16 if absent (`TText::Init` starts at 0x10) and 12 only if the value read is 0, colour (initialiser r=0,g=0,b=1,a=0 in the binary, then replaced by the style's colour; Lua always supplies one). `name` (window name, read first) and `label` (the text),
`defflags` (default flags), `flags` (defaults to `defflags`), `padding`, `rotation`, `rotationOriginX`,
`rotationOriginY`, `rotationAlign`. Window size 0 in either axis logs
`ERROR: Attempting to create text window of size(0,0): named '%s', label '%s'` and no text is created.

Label resolution (only when the window manager's string-table switch is on, which is the normal state):
a leading `#` means literal text (the `#` is stripped); a label made only of digits and `.` is literal; anything
else is a **string-table key** looked up through `TStringTable::GetString`. This is why Lua writes
`label = "#" .. text`.

Flag bits: `kHAlignLeft = 0`, `kHAlignCenter = 1`, `kHAlignRight = 2`, `kVAlignTop = 0`, `kVAlignCenter = 4`,
`kVAlignBottom = 8` (doubles read from the embedded `ScreenInit.lua` constant table). They are added with `+`.
Tested bits in RenderText: `flags & 1` centre, else `flags & 2` right, else left; `flags & 4` vertical centre,
else `flags & 8` bottom. (Both `kHAlignCenter+kHAlignRight` would resolve to centre.)

### 3.3 Text markup (parsed by `TTextState::ParseTag`, 0x81a4a)

A tag is recognised only where the character is `<` and a closing `>` exists within the first 128 chars of
normalised tag text; otherwise `<` is an ordinary character. The tag text is normalised: spaces outside quotes
are removed, text is lower-cased until the first `=` and kept as is afterwards (so values like colours keep case).
Tag names are matched by **prefix** on the start of the normalised text, in the order of the table below:

| Tag | Effect |
|---|---|
| `<br pad='N'>` (prefix `br`) | ends the line; optional `pad` integer adds N extra pixels to that line's height |
| `</p>` | ends the line (no pad) |
| `<a id='x' color='rrggbb'>` ... `</a>` | link. Stores link id; link colour from `color` (hex, `#` optional; default is the current font colour). `</a>` clears it. Hit-testing and hover colour: 3.9 |
| `<b>` `</b>` | bold on/off (synthetic, 3.7) |
| `<i>` `</i>` | italic on/off (synthetic shear 1:3) |
| `<u>` `</u>` | underline on/off |
| `<cursor>` | place the text-edit caret here (3.8, section 4) |
| `<center>` `<left>` `<right>` | start a justification block: pushed on a 32 deep stack, forces a line break. Values: left 0, center 1, right 2 |
| `</center>` `</left>` `</right>` | pop the justify stack (clamped at 0), forces a line break |
| `<font size='N' color='rrggbb' face='path'>` ... `</font>` | push a font entry (copies the current one, overrides given attributes); max depth 32 (`</font>` pops, clamped at 0). `size` is atoi; `color` is hex with optional `#`; if the colour string is exactly 8 characters (no `#`) it is `rrggbbaa` and the last byte is alpha, otherwise alpha is 255. `face` is any font path. |
| `<outline color='rrggbb' size=N>` | start a stroke around glyphs: colour (alpha 1.0), `size` pixels (default **2**). Used throughout `style.lua` as `<outline color='ffffff' size=2>`. |
| `</outline>` | clear outline (size and colour zeroed) |
| `<tab pos='N'>` | set the pen X to `N*4` quarter pixels (N pixels from the line's left; replaces the current x) |

Because matching is by prefix and checks are sequential, an unknown tag that starts with one of these names is
mis-parsed (e.g. `<bold>` would be treated as `<b>`). An unrecognised tag that matches no prefix (like `<x>`) is
consumed silently as a zero-width tag (the parser still returns success; nothing is drawn).

Plain-text escapes: a backslash makes the **next code point literal** and is itself not drawn (`\<` shows `<`,
`\\` shows `\`). A real newline byte (0x0a) ends the line. There is no `%n%` markup and no `|`-column support in
the text engine (any `|` in Lua text is just a glyph). Strings passed from Lua that contain `\n` after Lua's
own escape processing are real newlines. (Reforged Lua also builds colour tags with
`string.format("<font color='%02x%02x%02x'>", ...)`.)

Input text is UTF-8; code points up to 4 bytes are decoded by hand (no validation of continuation bytes).

### 3.4 Line breaking and widths (RenderText)

State per line: pen `x` in quarter px, `lastBreak` (x at the last space and the string index after it).

1. Per character (after tags): decode UTF-8 code point. Backslash handling as in 3.3.
2. If the code point is a **space (0x20)**: record a break candidate: index after the space and the pen x
   *before* the space's advance. A candidate recorded while x is 0.0 is ignored (leading space cannot break).
   Spaces are otherwise ordinary glyphs with their own advance (no collapsing, trailing spaces still advance).
3. Glyph lookup order: run font (or its stored fallback), then the engine default `fonts/default.mvec` if the
   char is missing. On switching font because of a missing glyph the run is split at that character; when the
   primary font has the glyph again it splits back. If neither font has it: the character occupies no width
   and draws nothing. (`WriteDbg "ERROR: FONT missing in text when attempting to render string %s."` if the run
   has no font at all.)
4. Advance rule: before adding a glyph, add the stored "overhang correction" from the previous glyph. With
   `adv` = advance and `maxX` = glyph bounding-box right edge, both in font units:
   if `maxX <= adv`: `width = adv*scale`, correction = 0;
   else (glyph pokes past its advance): `width = maxX*scale`, next correction = `scale*(adv - maxX)` (negative).
   **The first glyph of a line** adds `-scale*minX + 4.0` quarter px (compensate left bearing, plus one pixel).
5. **Overflow test** after each glyph: the line overflows if
   `(leftOutline + 1.0 + rightOutline)*4 + x` rounded is greater than `4 * boxWidth`, where leftOutline is the
   outline size of the first run on the line and rightOutline the latest run's. (So outlines shrink the usable
   width.) On overflow:
   * if a space break candidate exists on this line: break there. The line ends before the space and the pen
     is rewound to the candidate's x; the scan resumes after the space; runs created after the candidate are
     discarded.
   * else (a single overlong word): the line is broken **before the character that overflowed** (it moves to
     the next line); if only one char fits it still takes it so no infinite loop (`lastBreak == current` case).
     A global byte (`0x1d9200`, normally 0) flips that to "break after the overflowing character".
   There is no hyphenation and no breaking at `-`, `/` or CJK boundaries: only at spaces, newlines and overlong
   words (CJK text therefore wraps by the overlong-word rule, character by character).
6. A newline code point, `<br>`, `</p>` or a justify tag ends the line and the line is finished. A final line with
   no glyphs gets zero height (a `<br>` immediately after another gives an empty line only if it contained a tag
   that counted).
7. Italic: when italic turns on, x += `(scale/3)*descent`; when it turns off, x += `(scale/3)*ascent`
   (matching the 1:3 shear in section 2). The draw pass applies the same offsets so spans line up.
8. Line metrics: `line.height = max(run.size) + pad` (pixels), `line.baseline = max(run baseline)`. A line with no
   counted characters has height and baseline 0.
9. Total text height = sum of line heights (note: the `TText.padding` is *not* in this sum; it is added when
   drawing, between lines). `GetLineCount` = number of lines.

Run records are 100 bytes: font ptr, scale, size, baseline, fallback font ptr, colour (4 floats), bold/italic/
underline/cursor bytes, string range (start, end index), x start/end in px (>>2, rounded half away from zero),
link id string + link colour, outline size + colour, tab position.

### 3.5 Alignment and the text rectangle

Per line, with `lineW` the final x (quarter px), `startPad = -scale*minX_of_first_glyph + 4.0`, using the
justification currently on the stack (a `<center>` etc. block applies from the line **after** the tag) or else the
window `flags`:

* Left: origin x = round(first-run outline size) px; line offset 0.
* Centre: origin x = `boxWidth / 2` (integer division); line offset = `(-(lineW>>2)) * 0.5`.
* Right: origin x = `boxWidth - round(last-run outline size) - 1`; line offset = `-(lineW>>2)`.
* all: line offset += `startPad * 0.25 + 0.5`.

Vertical: `flags & 4`: block origin y = `boxHeight/2` and shift = `-totalHeight*0.5`; `flags & 8`: origin
y = `boxHeight`, shift = `-totalHeight`; else 0 / 0. The block's left/right extents are tracked for bounds.
Nothing clips: the box height only stops drawing (see 3.6). Text may overflow the box in either axis.

`TTextGraphic::GetTextBounds(scale)` returns the tight rect of the text: left = min over lines of
`line.originX + line.offset + firstRun.xStart`, right = max over lines of `... + lastRun.xEnd`, top =
`scale*yShift + block y origin`, bottom = top + `totalHeight*scale`; if no lines, left = 0. `TText::GetTextBounds`
calls it with scale 1.0. **This is what `TTightText` uses.**

### 3.6 Drawing (`TTextGraphic::Draw`, 0x8293c)

`TText::Draw` calls `Draw(clientRectOrigin, clientHeight, scale=1.0, padding, alpha, noTexture)`.
Alpha < 0.0 draws nothing (`param_3 < 0` early exit uses the float constant 0.0 as threshold). Steps:

* Lines are walked from `firstVisibleLine` (`+0x58`, truncated to int; the scroll fraction supplies it); a line
  whose y is below the client height stops the loop; a line above y < 0 is skipped. After each line, y advances
  by `line.height + padding` (padding is `TText.padding`, scaled by the zoom).
* Per run: colour = run colour (link colour while the link under the mouse is `Rollover`ed, i.e. when the
  graphic's current link id equals the run's link id); alpha scaled by the TText alpha.
* If the run has an outline, the first layer uses outline colour and width and the glyph is drawn on top in
  the fill colour. Outline widths are in pixels each side.
* Pen per glyph: x advances `advance*scale` (font units * quarter-px scale) along the rotation direction;
  glyph drawn at `round(x)` quarter px. Underline: a 1 px-high box (`DrawBox`) from the run start to the end
  of its measured width, at baseline + 8..11 (quarter px relative values 8 and 0xb in the call). Caret: see 4.
* Y scale of each glyph = `scale * GetGlyphScaleY(glyph)` (section 1.3).

### 3.7 Synthetic bold, shadows, outlines, colour/alpha

* Bold (`<b>` or `forceBold`): the glyph is additionally stroked with half-width
  `size * zoom * 0.0125 * boldScale` pixels in the **fill** colour (two layers: stroke + fill). So bold is a
  ~1.25% of font height thicker outline, not a separate face.
* Outline (`<outline ...>`): stroke layer in the outline colour. The Lua theme uses white, size 2.
* **Shadows are not an engine feature.** `style.lua` `ColorShadowText` stacks three `Text` windows (alpha 40
  black at y+4, black at y+2, then the colour at y) to fake a shadow.
* Colour: floats 0..1 from Lua `Color(r,g,b,a)`; hex colours in tags are `/255`. Alpha: text colour alpha *
  `TText::SetAlpha` value * the palette alpha ramp from section 2. The pixel colour is straight-alpha blended.

### 3.8 Scrolling

`TText::SetScroll(frac)`: `firstVisibleLine = (GetMaxScroll + scrollBias) * frac`, where `scrollBias` is +0x40
(default 0.5, so it rounds). `GetMaxScroll` walks lines from the bottom adding `line.height + padding` until
the sum exceeds the box height, and returns the count that did not fit.

### 3.9 Links and `Pick`

`Pick(point)` walks visible lines; for the line containing `point.y` it returns the link id of the run whose
x-extent contains `point.x`, else the empty string. `OnMouseDown` stores that id; `OnMouseUp` finds a
**`TButton` with that window name** in the nearest modal parent and calls `DoCommand` on it (logs a warning if
missing or not a button). `Rollover` re-lays/redraws with the link colour for the hovered id.

### 3.10 `TTightText` (`TightText.h`, 0x38ca8)

It is **not** a shrink-to-fit font. `Init`: run `TText::Init`, force the window size to 800x600, and read
`pad` (default **3**) into `padding` (+0x48). `PostChildrenInit`: after children are built it asks the text
for its bounds and sets the window size to `(right - left + pad, bottom - top)` of `GetTextBounds`. So the
window shrinks to the measured width plus `pad` and the measured height. Lua constructs it through
`t.typename = "TightText"; t.w = 800; t.h = 600` (see `style.lua` ~line 300), so the text is wrapped at 800 px
wide first, then the window is trimmed. A parent's `FitToChildren` therefore sees the trimmed size only if
it runs after `PostChildrenInit`; I did not trace that order (see Unknown).

### 3.11 What Lua calls and expects

`Text{ x,y,w,h,name,label,font={face,size,color},flags,defflags,padding,rotation,... }`; `TightText`;
`AppendStyle{ font=..., flags=... }` which sets inherited keys for later `Text`; `GetString(key)` for string-table
labels; the `kHAlign*`/`kVAlign*` constants; `Color(r,g,b,a)` (0..255 in Lua). Text content is read back with
`GetText` (`TText::GetText` returns the graphic's string). Buttons build `Text` children named `label`/`highlight`.

## 4. Text input (`TTextEdit`, `TCustomTextEdit`)

`TTextEdit` derives from `TText`; the text shown is the real string plus caret handling in `UpdateText`
(0x8ae76). Fields: +0x48 max length, +0x54 text, +0x58 caret byte offset, +0x5c "registered/focused",
+0x5d `utf8` flag, +0x5e `password`, +0x5f blink phase, +0x60 `ignore` string, +0x64 `TClock`.

Style keys: `password` (bool), `length` (number, default **10**), `ignore` (string of characters to swallow),
`utf8` (bool). `Init` disables the string table while building so the label is literal.

* **Caret drawing**: the caret is not an overlay; `UpdateText` inserts the `<cursor>` tag into the displayed
  string at the caret byte offset, and RenderText/Draw draws a vertical box (1 px wide at scale 1, height from
  -ascent to +descent of the run font) at that pen position. Blinking: `TClock::GetTime() % 800 < 0x191 (401)`
  means caret hidden (plain string shown), otherwise shown. Only the focused edit (window manager's focus
  pointer) blinks; others show the plain string. `OnTaskAnimate` calls `UpdateText` every frame
  (so blink is evaluated per frame). Whenever the phase flips, the screen is invalidated.
* **Password**: when set, the displayed string is one `*` per **byte** of the text (not per character).
* **Max length**: `KeyHit(type 0, ch)` rejects when `!utf8 && maxLen <= len+1`, i.e. a non-UTF-8 field holds at
  most `length - 1` characters (so the default holds 9). With `utf8` on, `KeyHitUTF8` counts code points and
  rejects when `count >= length` (holds `length`). Reject = a virtual call with type 5 (**Inference**: a reject notification; its sound, if any, was not found).
* **Allowed characters** (`OnUTF8Char`, 0x8cc26): needs focus; control characters (< 0x20) are rejected;
  bytes >= 0x80 (non-ASCII) are rejected unless `utf8` or `password` is set; characters appearing in the
  `ignore` string are silently swallowed (treated as handled); the character must exist in the edit's font
  or in `fonts/default.mvec` (`KeyHitUTF8` glyph test), else rejected. Tab asks the owner to move focus to the
  next registered edit; Enter (`\r`) and backspace go to the virtual `KeyHit` types 2 and 1.
* **Editing**: left/right arrows move the caret by whole UTF-8 characters; Delete removes the character after the
  caret; backspace removes the previous character (UTF-8 aware). Ctrl+V / paste (virtual key + modifier bit 2)
  takes the clipboard string; with `utf8` it is truncated to `length` code points, characters in `ignore`
  are removed, control characters < 0x20 stripped, then the field is cleared and the text inserted via a
  `KeyHit(0,' ')` check. Without `utf8` it is accepted only if `SafeString()` approves.
  Every accepted change posts a window message of type 7 (`kTextEditChanged`) named after the window.
* **`TCustomTextEdit`** (`customtextedit.h`): style keys `clearinitial` (bool; if false, the initial text is
  cleared on the first key press or first mouse-up), `enablewindow` (name of a window enabled only when the
  field has text; toggles flag `0x100` on that window), `widthlimited` (bool: reject a new character when
  the free width in the client rect minus the text bounds width is < 20 px, instead of using the character
  limit alone), `onkey` (Lua function called after each `OnChar`). On a successful insert it plays
  `sfx/sfx_typing_%02d.ogg?group=1000` with a random 01..04.

## 5. Fonts shipped in the Reforged mod's `assets/fonts/`

Header values read with a script run via `python3 -I` (`ascent` and `descent` computed as in 1.3, in the file's own units;
`units/em` is the divisor to convert to ems = ascent+descent over 0.9453, which is only valid for the
Fertigo-derived fonts; for others it is shown as approximate). All headers have the reserved 6 bytes zero, and
all glyph tables are strictly sorted.

| File | Bytes | Glyphs | First..last cp | ascent('W') | descent | advance(space) |
|---|---|---|---|---|---|---|
| arial.mvec | 305656 | 1419 | 0x20..0xfffc | 0.719 | 0.211 | 0.281 |
| arima.mvec | 484315 | 1120 | 0x0d..0xfb02 | 165.62 | 53.41 | 49.92 |
| choco3.mvec | 102384 | 489 | 0x20..0xf002 | 84.71 | 28.10 | 67.06 |
| fertigo.mvec | 218070 | 565 | 0x20..0x1ef9 | 166.32 | 57.15 | 58.38 |
| fertigo-italic.mvec | 122557 | 368 | 0x20..0xfb04 | 0.711 | 0.234 | 0.242 |
| fertigo-script.mvec | 215306 | 369 | 0x00..0xfb04 | 0.711 | 0.234 | 0.242 |
| gabriela.mvec | 197075 | 566 | 0x0d..0xfb02 | 167.48 | 64.84 | 64.15 |
| nanummyeongjo.mvec | 12550126 | 11882 | 0x20..0xffe6 | 171.98 | 53.98 | 63.78 |
| notoserif-sc.mvec | 31452988 | 30928 | 0x20..0x2f920 | 169.82 | 63.45 | 59.72 |
| notoserif-tc.mvec | 20876655 | 20748 | 0x20..0x2f9d4 | 169.82 | 63.45 | 59.72 |
| shipporimincho.mvec | 15948538 | 15363 | 0x00..0x2f9f4 | 169.35 | 59.95 | 60.18 |
| srisakdi.mvec | 273604 | 727 | 0x00..0xfb02 | 177.50 | 89.25 | 64.50 |
| trirong.mvec | 189492 | 724 | 0x0d..0xfb02 | 163.29 | 56.91 | 60.88 |
| yatraone.mvec | 105939 | 439 | 0x00..0xfb02 | 155.35 | 62.05 | 81.64 |

Notes: there is no em field, so none of these numbers is an "em size"; the fonts differ in unit scale
(arial and the two small Fertigo variants are about 1 unit/em, the rest about 230 units/em). Because layout
normalises by ascent+descent, unit scale does not affect the rendered size. Fonts without 'W' (none here)
would break. `arial.mvec` includes 0x0d, `fertigo-script` etc. include code point 0; harmless.
(The original game shipped only `fertigo.mvec`, `choco3.mvec` and a default; the others are Reforged additions.)

### `ui/style.lua` font usage (the Reforged mod's `assets/ui/style.lua`)

`labelFontName` defaults to `fonts/choco3.mvec`; `standardFont` to `fonts/fertigo.mvec`; `uiFontName = standardFont`
(overridden per language elsewhere in `languages/*`, e.g. arima, gabriela, trirong, srisakdi, yatraone, noto serifs).
Size is the line-box height in px. All colours are `Color(r,g,b,a)`.

| Name | Face | Size |
|---|---|---|
| dialogFont | standard | 30 |
| ScoreFont | standard | 20 |
| StandardButtonFont | standard | 18 |
| MenuButtonFont | standard | 22 (white) |
| DialogTitleFont | standard | 20 |
| DialogBodyFont, dialogBodyFont | standard / ui | 18 |
| DookieDropperFont, ...CounterFont | standard | 14 |
| characterBodyFont | ui | 16 |
| rolloverInfoFont | ui | 18 |
| characterNameFont | label | 12 (colour 164,23,5) |
| characterNameSmallFont | label | 7.5 (truncated to 7) |
| nameplateFont | label | 30 (white) |
| portLabelFont | ui | 18 |
| factoryStatusFont | label | 20 |
| portNameRolloverFont | label | 18 |
| roundButtonFont | label | 17 |
| buttonFont | ui | 17 |
| questUnselectedFont / questSelectedFont | ui | 16 / 18 |
| jukeboxCategoryFont (+Highlight) | label | 13 |
| mapPortButtonFont (+Highlight) | label | 25 |
| LedgerButtonFont (+Highlight/Down) | label | 25 |
| LedgerButtonLabelFont | ui | 15 |
| SmallLedgerButton* | label | 18 |
| SmallLedgerButtonLabelFont | ui | 15 |
| a fixed-style entry at line 143 | `fonts/arial.mvec` | 14 |
| `ColorShadowText` | standard | caller `size` (kMenuButtonFontSize = 22) |

`DefaultStyle = { font = DialogBodyFont }`. Buttons build `Text` children `label`, and `highlight` with label
`"#<outline color='ffffff' size=2>" .. text`.

## Unknown

* Exact meaning of opcodes `03 nn`, and `00`, `01`, `07` in the outline program, and of operand `01` in `02 01`.
  Only `02 01 (04|05|06)* 08` appears in tools.
* Exact coverage maths of the span rasteriser (how accumulator levels 0..31 relate to coverage, order of the
  four sub-row layers, and the second 4-level palette). Behaviour was only inferred.
* Whether the descent flag (`0x21f4c1`) is ever set; and who calls `TMvecFont::SetBoldOverride` or sets `forceBold`.
* What the global `0x1d9200` byte (overlong-word break after vs before the overflowing char) is bound to (a config
  key). Default zero was assumed.
* `TText` virtual slot at +0x74 (called in `TText::Create` with the label) is assumed to be `SetText`; not verified.
* The rule for `TextSpider`, `TTextSprite` (separate sprite-based text path with `SetLineHeight`, `SetStartLine`,
  `Create`) and `SignEditor::SetFont` was not read. They share `TTextRun`/`TMvecFont` but may differ in layout.
* Whether `FitToChildren` on a parent runs before or after `TTightText::PostChildrenInit`; I did not trace
  `TWindow::FitToChildren`.
* Rotation (`TTextGraphic::SetRotation`, `rotationAlign`) was not decoded beyond the rotation matrix use.
* The content of `fonts/default.mvec` and the embedded 0x12c2c-byte fallback font (it is a normal MVC4 blob in
  the Mach-O at address `0x10f61c + base`; I did not extract it).
* The `<br pad>` exact accumulation in line height and the "empty line" case where `local_74 == 0` are as
  described but not tested against real strings.
* Playmate Font Maker was not decompiled; the assumption that it writes exactly the layout above rests on
  Reforged's tools and the game loader.
