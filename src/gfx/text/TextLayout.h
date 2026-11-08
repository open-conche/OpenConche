#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gfx/Renderer.h"
#include "gfx/text/Font.h"
#include "gfx/text/GlyphAtlas.h"

namespace choco::gfx::text {

// Text window flags (docs/re/fonts-and-text.md 3.2). Horizontal and vertical parts are added together.
enum TextFlags : int {
  kHAlignLeft = 0,
  kHAlignCenter = 1,
  kHAlignRight = 2,
  kVAlignTop = 0,
  kVAlignCenter = 4,
  kVAlignBottom = 8,
};

struct TextStyle {
  std::shared_ptr<const Font> font;
  float size = 12;  // line box height in pixels; truncated to an integer, 0 means 12
  ColorF color;
  int flags = 0;
  int padding = 0;           // extra pixels between lines when drawing (not part of bounds())
  FontCache* fonts = nullptr;  // for <font face=>; without it faces cannot be switched
  // Chocolatier 1 (0x44a99e-0x44a9d7): an overflowing glyph that ends the string and has no space candidate stays on the line
  // instead of starting a new one. The exe's other no-candidate case (a non-last glyph is dropped) is not implemented.
  bool keepLastOverflowChar = false;
};

// Fonts and the glyph atlas as one unit, installed once so TextLayout::draw(renderer, origin, alpha) can find them.
class TextSystem {
 public:
  TextSystem(const assets::AssetFS* fs, TextureUploader* uploader) : fonts(fs), atlas(uploader) {}
  ~TextSystem();
  FontCache fonts;
  GlyphAtlas atlas;
  static void install(TextSystem* t);  // pass null to uninstall
  static TextSystem* current();
};

// A laid-out string: markup (3.3), wrapping (3.4), alignment (3.5). Immutable once built.
class TextLayout {
 public:
  TextLayout();
  ~TextLayout();
  TextLayout(TextLayout&&) noexcept;
  TextLayout& operator=(TextLayout&&) noexcept;
  TextLayout(const TextLayout&) = delete;
  TextLayout& operator=(const TextLayout&) = delete;

  // w x h is the text window. Lines are not clipped, but drawing stops at the first line whose bottom is below h and
  // skips lines that end above 0 (TTextGraphic::Draw); wide text may still overflow to the right.
  static TextLayout build(std::string_view utf8, const TextStyle& style, int w, int h);

  // TTightText: the window shrinks to the text's measured width plus `pad` (default 3) and its height.
  static RectI measure(std::string_view utf8, const TextStyle& style, int w, int h, int pad = 3);

  // Origin is the window's top-left in logical coordinates. Uses the installed TextSystem, else logs once and draws
  // nothing. Glyphs are rasterised at renderer.outputScale().
  void draw(Renderer& r, PointF origin, float alpha = 1.0f) const;
  void draw(Renderer& r, GlyphAtlas& atlas, PointF origin, float alpha = 1.0f) const;

  // TTextGraphic::Draw with a first visible line (the credits scroll): lines before `firstLine` are skipped, the first
  // one's top is placed at the window's top edge moved up by `shiftPx`, and the box height still ends the walk.
  void drawScrolled(Renderer& r, GlyphAtlas& atlas, PointF origin, float alpha, int firstLine, float shiftPx) const;

  // The link whose colour is shown (<a id=... color=...>); none by default, so links look like plain text.
  void setHoveredLink(std::string_view id);

  // Tight rectangle of the text relative to the window origin (RE 3.5).
  RectI bounds() const;
  int lineCount() const;
  // Glyph pen positions, for tests: logical x of each drawn glyph's pen, y of its baseline, and the code point.
  struct GlyphPos {
    std::uint32_t cp;
    float x, y;
    int line;
  };
  std::vector<GlyphPos> glyphPositions() const;

  // One glyph as the draw path would place it at output scale 1 with the window's top-left at `origin`: the pen's x in
  // fractional pixels (quarter-pixel grid), the baseline's y rounded to a whole pixel, and what the draw reads to
  // rasterise and colour it. For drawing into a CPU image (the Chocolatier 2 logo); lines the draw would skip are left
  // out, as in draw().
  struct GlyphRun {
    const Font* font = nullptr;
    int glyph = 0;
    float x = 0;
    int baselineY = 0;
    float size = 12;
    ColorF color;
    bool bold = false, italic = false;
    int outline = 0;
    ColorF outlineColor;
  };
  std::vector<GlyphRun> glyphRuns(PointF origin) const;

  // Where a text caret sits before the glyph with this index (counting every placed glyph, spaces included): the pen
  // x relative to the window origin, and the top and height of its line. An index past the last glyph is the end of
  // the last line. False when nothing is laid out.
  struct CaretPos {
    float x = 0, top = 0, height = 0;
  };
  bool caretAt(std::size_t glyphIndex, CaretPos& out) const;

 private:
  void drawImpl(Renderer& r, GlyphAtlas& atlas, PointF origin, float alpha, int firstLine, float shiftPx) const;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Markup helpers exposed for tests.
struct ParsedColor {
  bool ok = false;
  ColorF color;
};
ParsedColor parseHexColor(std::string_view s);  // "rrggbb", "#rrggbb", "rrggbbaa"

}  // namespace choco::gfx::text
