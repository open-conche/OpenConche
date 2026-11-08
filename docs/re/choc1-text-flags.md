# Chocolatier 1: text flags and the "Pause" label (the exe draws it on one line)


Facts come from the Chocolatier 1 Windows exe (`Chocolatier.exe`) (disassembled with capstone over the PE; Ghidra output in `re/choc1/decomp/_unnamed.c`)
and the compiled `ui/ledger.lua` / `ui/styles.lua` (a workbench script). Labels: **fact** (read), **inference**, **unknown**.
The exe does not break "Pause" into "Paus" / "e"; it keeps the overflowing last character (section 2). Related: `docs/re/choc1-options-credits.md` section 3.

## 1. The Lua (fact)

`ui/ledger.lua` builds the pause button (line 58) and, in the map branch, a disabled copy (line 44). Both Texts are identical:

```
Button{ name="pause", graphics=pauseButtonGraphics, x=0, y=122, command=<closure>,
        Text{ x=0, y=30, w=56, h=30, label="menu", font=pauseButtonFont, flags=kVAlignCenter + kHAlignCenter } }
```

* The Button has no `label` key, so the `ui/styles.lua` Button wrapper (which would add its own `Text`, `FitToChildren`, `defflags`) does nothing here.
  The Text is an array child of the Button table and is placed in Button coordinates. The engine dump agrees: `Text rect=0,30,56,30` inside
  `Button "pause" rect=0,122,73,90`.
* `pauseButtonFont = { stdFontFace (fonts/bolton.mvec), 28, BrownColor }` (`ui/styles.lua` line 116).
* `label="menu"` resolves through the string table to "Pause".

### Flag values (fact, with one inference)

The constants are registered by the exe's embedded `gScreenInit` chunk (string table at file text `0x527275..0x527391`, a precompiled Lua constant
table). The numeric constants stored are `kToggle=1.0`, `kRadio=2.0`, `kVAlignCenter=4.0`, `kVAlignBottom=8.0`, `kAllLayers=-1.0`, `kCenter=80000.0`
(double `0x40f3880000000000`), `kMax=160000`, `kDefault=128.0`. `kHAlignLeft`, `kHAlignCenter`, `kHAlignRight` and `kVAlignTop` have no
number of their own in the table, which is how a constant table de-duplicates `0`, `1.0`, `2.0` already present (kPush, kToggle, kRadio). **Inference:**
`kHAlignLeft=0, kHAlignCenter=1, kHAlignRight=2, kVAlignTop=0`. They equal Chocolatier 3's values, and the exe's own flag tests below
agree. So **flags = 4 + 1 = 5** (the dump's `flags=5`): centre vertically, centre horizontally. The engine decodes the bits correctly; this is not a
flag-decoding bug.

Where each bit is consumed (fact, `FUN_0044a4d0`, the TText layout, object field `+0x68` = per-line justify, `this[0x1a]` = window flags):

* per line, justify `& 1` (`0x44aab3`): x origin = `w/2` (integer division) and fractional offset `-lineW*0.5`; `& 2` (`0x44aace`): x origin = `w - rightOutline - 1`
  (`[esp+0x4c]`, the outline of the last run), offset `-lineW`; else left: x = leftOutline (`[esp+0x50]`).
  A `<center>/<left>/<right>` tag overrides the flags (`[esp+0x68] != -1`).
* whole block, `flags & 4` (vertical centre): block y origin `h/2` (integer), shift `-(int)(total*0.5)`; `& 8`: origin `h`, shift `-total`; else 0.
  `total` = sum of the line heights.
* Then `x = origin + offset + startPad*0.25 + 0.5`, where `startPad = 4.0 - minX_first*scale` (`0x44ab23..0x44ab37`, constant `0x4fd258` = 0.25).

These are exactly what `src/gfx/text/TextLayout.cpp` (`build`, the `switch (l.justify)` and the `kVAlignCenter` branch) does. The engine's flag handling,
the anchor (the Text's own rect, not the Button or its parent) and the Button's child placement are all correct.

## 2. TText layout for "Pause" in the exe (fact)

Inputs: box w=56 h=30, size 28, font scale `0.8*28*4/(215.15+68.55)` quarter-pixels per font unit (constants `0x529584`=0.15, `0x529588`=0.05, read-only
floats; `FUN_00479ef0 = 1-0.15-0.05`, so 0.8).

* Pen after the `e` (including the first-glyph pad `4 - minX*scale`) = **226.6 quarter pixels**. The overflow test is
  `lround((outlineL + 1 + outlineR)*4 + pen) > 4*w` (`0x44a959..0x44a99a`; constants `0x4e6580`=1.0, `0x4e65a0`=4.0, `0x4e7508`=0.5), with both outlines 0:
  `231 > 224`, so **the `e` overflows**.
* **What the exe does on overflow** `0x44a99e..0x44a9e5`:
  1. If a space candidate with a non-zero saved pen exists, the line ends before that space (pen rewound) and the scan resumes after the space.
  2. Otherwise the line's last index is set to `idx - 1` (the overflowing character is excluded) ...
  3. ... and then, unconditionally (`0x44a9cc..0x44a9d7`): **if the current character is the last one of the string (`idx == length-1`, in bytes, which for a
     multibyte final character is its last byte), the line's last index is set back to `idx`**, i.e. the final character stays on the line even though it
     overflowed. The loop then advances to `idx+1 == length` and ends: **one line**, "Pause", whole.
  4. The pen is not rolled back in the no-space case (only the space case restores the saved pen), so the line width used for centring includes the `e`.
* Result for "Pause": one line, `lineW = (lround(226.6)) >> 2 = 56`, total height 28.
  * x origin = 56/2 = 28, offset = -56*0.5 + startPad*0.25 + 0.5, so the glyph pen starts at `startPad*0.25 + 0.5` from the text box's left edge.
  * y: block origin `h/2 = 15`, shift `-(int)(28*0.5) = -14`, so the line's top is at y = 1 inside the Text rect (button y = 31).

### Expected pixels in the original (derived with the engine's own glyph code on a box wide enough not to wrap, then shifted for w=56)

Measured by drawing the same Text with w=60 (no overflow, one line) and w=200 in a scratch run, ink threshold red < 150:

| quantity | relative to the Text rect (0,30) | relative to the Button origin |
|---|---|---|
| left-most ink column of the "P" (its foot serif) | x = 2 | **x = 2** |
| right-most ink column of the "e" | x = 56 | x = 56 (one column past the 56-wide rect) |
| width of the drawn string "Pause" | 55 columns (pen width 56 px) | 55 |
| top ink row of the glyphs (cap height of P, ascender-free word) | y = 7 | **y = 37** |
| bottom ink row (baseline) | y = 21 | y = 51 |
| line top / line bottom | y = 1 / 29 | 31 / 59 |

The ink spans button y 37..51 and x 2..56, so it sits in the lower middle of the 73x90 lilac disc (disc centre is about y 50) and "Pause" fits.
That matches the reported "whole word, inside the disc". The one-column overshoot at x=56 is the box edge only.

## 3. The wrap rule for a final overflowing character, and the engine

**Rule (fact):** when no space candidate exists, the exe excludes the overflowing character from the line's end index but restores it when it is
the last character of the string, so a final overflowing character stays on the line. "Pause" is 56.65 px of pen in a 56 px box and overflows only on its
last letter, so the exe shows "Pause" on one line. Not the flags, not the anchor, not the Button child offset, not the baseline.

A *non-last* overflowing character with no space candidate is neither on the line nor on the next one: the line ends at `idx-1` and the next line starts
at `idx+1`, so that character is dropped (`add edi,1` at `0x44a9de` after the exclusion). Read from the disassembly; not seen in a shipped string.

**Engine:** `TextStyle::keepLastOverflowChar`, set from `WindowQuirks::keepLastOverflowChar` (only `Choc1Module`) through
`WindowManager::setKeepLastOverflowChar` into `TextWindow`. In `TextLayout::Builder::addGlyph`, in the `overflow && !first` branch with no valid space
candidate, a glyph that ends the string is added to the current line and the line is closed, as the `overflow` tail does for a first glyph; the pen includes
the glyph, so the width and the centring match the exe. Chocolatier 2 and 3 keep the option off and their output unchanged: whether their binaries share the
rule is **unknown** (`docs/re/fonts-and-text.md` 3.4 item 5 documents their rule). The non-last-character drop is a deliberate deviation: the engine keeps
that character and starts the next line with it (`TextLayout.h`), because reproducing the exe would drop a glyph from the text; it can only show in a
non-English string with a long unbroken word in a narrow box.

Result for the pause button: `Text{w=56,h=30,label="\35Pause",font={bolton 28},flags=5}` gives one line, `lineCount()==1`, `bounds()` height 28, a glyph
run of 5 glyphs, P foot at x=2, ink rows rect 7..21 (button y 37..51, x 2..56; asserted by `check_choc1_story.py`). A `\35` in the label is the
literal-`#` escape in a Lua string (a bare `#` starts a script comment in the `lua` command line).

## 4. Pen/rounding details

* `lineW` = `(ftol(pen ± 0.5) + (sign bits & 3)) >> 2` (`0x44aa93..0x44aaa7`); the engine's `lround(widthQ) >> 2` matches for positive pens.
* The line object: `[+8]` x origin int, `[+0xc]` float offset, `[+0x34]` line width px; the text block `[+0xc]` = `h/2`, `[+0x14]` = total height, `[+0x60]` = `-(int)(total*0.5)`.
* Draw (`FUN_00447aa0`): line x = `line[2] + scale*line[3]` and the block stops at the first line whose bottom is `> h` or `< 0` (per line, not per glyph).
* The exe's `TText` draw brackets `Begin2d/End2d` with rect `{0,0,w,h}`. The engine's window clips the text to its rect (observed: glyph tops cut at the rect top);
  whether the exe also scissors there is **unknown**. It does not matter for "Pause" except that the last ink column (x = 56) lies 1 px outside a
  56-wide rect.

## Unknown

* Whether the Chocolatier 2 / 3 binaries contain the same "last character stays" rule (not checked; the option stays C1-only).
* Whether the exe scissors text to its rect.
* The non-last-character drop (section 3) is read from assembly only.
