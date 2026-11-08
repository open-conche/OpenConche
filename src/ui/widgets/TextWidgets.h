#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "gfx/text/TextLayout.h"
#include "ui/Window.h"

namespace choco::ui {

// The label a Text window shows for the label given in its style (TText::Init): a leading '#' is literal with the '#'
// removed; a label of only digits and dots is literal; anything else is a string-table key resolved by `lookup`
// (which answers "#####" for a missing key). An empty label stays empty, and with no `lookup` the label is literal.
std::string resolveLabel(std::string_view label, const std::function<std::string(const std::string&)>& lookup);

// The text style keys of TText::Init: `font = { face, size, color }` (face "__DEFAULT" and size 12 when absent or 0,
// colour white), `flags` (default `defflags`, default 0) and `padding`. `fonts` supplies the face (may be null: the
// style then carries no font and layout uses its built-in fallback).
gfx::text::TextStyle readTextStyle(const Style& style, gfx::text::FontCache* fonts);

// Text (TText): a label laid out with gfx::text (markup, wrapping, alignment) inside the window's rectangle.
// Stores and returns its resolved label; SetLabel replaces it as given (SetText does no lookup).
class TextWindow : public Window {
 public:
  explicit TextWindow(WindowManager& wm) : Window(wm) {}
  std::string_view typeName() const override { return "Text"; }
  void init(const Style& style) override;
  void draw(DrawContext& ctx) const override;
  bool hasLabel() const override { return true; }
  bool takesFaderAlpha() const override { return true; }
  const std::string& label() const override { return label_; }
  void setLabel(std::string s) override;
  std::string detail() const override;

  const gfx::text::TextStyle& textStyle() const { return style_; }
  // The layout for the current label, style and size (built on first use, rebuilt when any of them change).
  const gfx::text::TextLayout& layout() const;
  // Replaces the text colour (the font table's colour at creation) and rebuilds the layout.
  void setColor(const gfx::ColorF& c) {
    style_.color = c;
    invalidate();
  }
  // Changes the line box height (the font size of the style) and rebuilds the layout.
  void setTextSize(float size) {
    style_.size = size;
    invalidate();
  }
  // The first visible line (0-based; 0 is the whole text from the top). Chocolatier 2's ledger scrolls its messages with it
  // (bsgSetStartLine). Draw skips the lines above it and places that line's top at the window's top edge.
  int startLine() const { return startLine_; }
  void setStartLine(int line) { startLine_ = line < 0 ? 0 : line; }
  // Chocolatier 2 (TTextGraphic::Draw): the start line is not clamped to maxStartLine() when drawing, so one past the last page's
  // first line draws a page that starts a line late (docs/re/choc2-ledger-binary.md 12.1). Off keeps the clamp.
  void setStartLineOverrun(bool on) { startLineOverrun_ = on; }
  // The line the draw really starts at.
  int drawnStartLine() const { return startLineOverrun_ ? startLine_ : std::min(startLine_, maxStartLine()); }
  // The largest useful start line: the laid-out lines less the whole lines that fit the box (0 when all fit).
  int maxStartLine() const;
  // The tight rectangle of the text at the window's current size (what `shrink` on a parent Rectangle measures).
  gfx::RectI textBounds() const { return measuredBounds(); }
  // True when the laid-out text fits the window's rectangle.
  bool textFits() const {
    const gfx::RectI b = measuredBounds();
    return b.w <= rect().w && b.h <= rect().h;
  }

 protected:
  // The tight rectangle of the text at the window's current size.
  gfx::RectI measuredBounds() const;
  void invalidate() { layout_.reset(); }
  // TextEdit builds with the string table off: its initial label is literal.
  bool literalLabel_ = false;
  std::string label_;
  gfx::text::TextStyle style_;
  mutable std::unique_ptr<gfx::text::TextLayout> layout_;
  mutable int layoutW_ = -1, layoutH_ = -1;
  int startLine_ = 0;
  bool startLineOverrun_ = false;
};

// TightText (TTightText): a Text that forces 800x600 at Init and shrinks to (text width + pad, text height) at
// PostChildrenInit, so a parent's FitToChildren afterwards sees the trimmed size.
class TightTextWindow : public TextWindow {
 public:
  explicit TightTextWindow(WindowManager& wm) : TextWindow(wm) {}
  std::string_view typeName() const override { return "TightText"; }
  void init(const Style& style) override;
  void postChildrenInit(const Style& style) override;
  int pad() const { return pad_; }

 private:
  int pad_ = 3;
};

// Registers Text and TightText with the manager.
void registerTextWidgets(WindowManager& wm);

}  // namespace choco::ui
