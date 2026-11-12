#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "assets/AssetFS.h"
#include "gfx/Types.h"

namespace choco::gfx::text {

// One outline segment. Coordinates are in font units (the file's 9.7 fixed point divided by 128), y grows downward,
// so ink above the baseline has negative y. See docs/re/fonts-and-text.md section 1.
struct Segment {
  bool quad = false;  // false: line to p; true: quadratic with control point c, ending at p
  PointF c, p;
};
struct Contour {
  PointF start;
  std::vector<Segment> segs;
};

// Glyph metrics in font units.
struct GlyphMetrics {
  float minX = 0, maxX = 0, minY = 0, maxY = 0, advance = 0;
};

// A parsed `.mvec` (magic "MVC4").
//
// Outline program opcodes, as settled from the shipped files, the Playground font tool's writer and the game's reader:
//   02 01        start of the glyph's fill pass (the only layer; always the first two bytes of a non-empty glyph)
//   04 x y       MOVETO, begins a contour           05 x y        LINETO
//   06 cx cy x y quadratic CURVETO                   08            end of glyph
//   03 nn        a second two-byte op shaped like 02 nn; no tool writes it and no shipped glyph uses it: skipped
//   00 01 07     jump-table slots never emitted by any tool: skipped as single bytes
//   09..ff       the game loops back to read the next byte: skipped as single bytes
// The writer emits exactly: 02 01, then per contour 04 + (05 | 06)*, and 08. Quadratic runs between two off-curve
// points are expanded with implied on-curve midpoints by the writer, so the file only holds explicit controls.
// All operands are s16 little endian in 9.7 fixed point. Open contours are closed implicitly when filled.
class Font {
 public:
  // Null (and *error set) if the bytes are not a well-formed MVC4 file: bad magic, truncated table, code points not
  // strictly ascending, an outline offset outside the file or inside the table, or a program that does not end in 08
  // before the end of the file.
  static std::shared_ptr<const Font> fromBytes(std::vector<std::byte> bytes, std::string name,
                                               std::string* error = nullptr);

  const std::string& name() const { return name_; }
  std::uint64_t id() const { return id_; }  // unique per Font object, for cache keys
  std::size_t glyphCount() const { return glyphs_.size(); }

  // Index of the glyph for a code point, -1 if absent (binary search over the sorted table).
  int glyphIndex(std::uint32_t codepoint) const;
  bool has(std::uint32_t codepoint) const { return glyphIndex(codepoint) >= 0; }
  std::uint32_t codepoint(int index) const { return glyphs_[static_cast<std::size_t>(index)].cp; }
  GlyphMetrics metrics(int index) const;
  // Outline of a glyph in font units; empty for a blank glyph such as the space.
  std::vector<Contour> outline(int index) const;

  // Byte offset of a glyph's outline program in the file and its length including the closing 08.
  std::uint32_t programOffset(int index) const { return glyphs_[static_cast<std::size_t>(index)].offset; }
  std::size_t programLength(int index) const;

  // Font units. Ascent = -minY('W'); descent = max maxY over ASCII except '_' (see the RE notes, 1.3).
  float ascent() const { return ascent_; }
  float descent() const { return descent_; }
  // Vertical squash that fits tall glyphs into the line box (RE 1.3); 1 for glyphs that already fit.
  float glyphScaleY(int index) const { return scaleY_[static_cast<std::size_t>(index)]; }

  // Pixels per font unit for a line box of `size` pixels: the em box (ascent + descent) is 0.8 * size tall, and faces
  // with "lithos" in the name are shrunk by 0.7.
  float pixelScale(float size) const;
  // Distance from the top of the line box to the baseline for that size, in pixels.
  float baseline(float size) const;

 private:
  struct Rec {
    std::uint32_t cp;
    std::int16_t minX, maxX, minY, maxY, advance;
    std::uint32_t offset;
  };
  Font() = default;
  std::string name_;
  std::uint64_t id_ = 0;
  std::vector<std::byte> data_;
  std::vector<Rec> glyphs_;
  std::vector<float> scaleY_;
  float ascent_ = 1, descent_ = 0.25f;
  bool lithos_ = false;
};

// Builds MVC4 bytes from hand-made outlines: tests, and the built-in fallback font. Values are font units and are
// quantised to 1/128. The glyph list is sorted by code point.
struct GlyphSpec {
  std::uint32_t cp = 0;
  float advance = 0;
  std::vector<Contour> contours;
  // Bounds are computed from the points unless set here (a blank glyph that still defines the metrics).
  bool explicitBounds = false;
  float minX = 0, maxX = 0, minY = 0, maxY = 0;
};
std::vector<std::byte> buildMvec(std::vector<GlyphSpec> glyphs);

// A monospace test/fallback font: every ASCII code point 0x20..0x7e has the given advance. With `boxes` each glyph but
// space is a filled box (60% of the advance wide, ascent tall); otherwise glyphs are blank. 'W' (ascent = 75 units)
// and 'y' (descent = 25 units) always have the right extents so the metrics are sane.
std::shared_ptr<const Font> makeMonospaceFont(std::string name, float advance = 60, bool boxes = false);

// The built-in fallback: a 5x7 pixel font with capitals (lower case shares them), digits and some punctuation, plus
// U+FFFD. Always available; shared by every FontCache.
std::shared_ptr<const Font> builtinFont();

// Fonts by name through the asset files, with the original's fallback chain: the name as given, then
// `fonts/default.mvec`, then a built-in blank font that keeps metrics sane. "__DEFAULT" is the built-in directly.
// Results are cached under the requested name; each failing name is logged once.
class FontCache {
 public:
  explicit FontCache(const assets::AssetFS* fs = nullptr) : fs_(fs) {}
  std::shared_ptr<const Font> get(std::string_view name);
  std::shared_ptr<const Font> builtin();
  // `fonts/default.mvec` through the same chain: where glyphs missing from a run's font are looked up.
  std::shared_ptr<const Font> defaultFont();
  // The font at the end of the chain, in place of the built-in one: the original has a font compiled into its executable
  // there (RE 1.8), and a game whose code names a font file that does not exist draws with it.
  void setEmbedded(std::shared_ptr<const Font> font) { embedded_ = std::move(font); }
  std::size_t size() const { return cache_.size(); }

 private:
  const assets::AssetFS* fs_;
  std::map<std::string, std::shared_ptr<const Font>> cache_;
  std::shared_ptr<const Font> embedded_;
};

}  // namespace choco::gfx::text
