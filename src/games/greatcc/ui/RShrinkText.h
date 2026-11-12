#pragma once

// RShrinkText (docs/re/greatcc-ui-library.md 4): a Text whose SetLabel starts again from the font table's size and shrinks the
// line height one pixel at a time until the text's bounds are strictly smaller than the window in both axes.

#include <functional>

#include "gfx/Types.h"
#include "ui/widgets/TextWidgets.h"

namespace choco::games::greatcc {

// The shrink loop on its own. `measure(lineHeight)` gives the text bounds at that line height. Starting at `maxSize`, while
// `winW <= bounds.w || winH <= bounds.h` (a text that exactly fills the window does not fit) the size drops by one; reaching 0
// stops the loop (`hitZero` is set). Returns the final size. A `maxSize` of 0 or less is returned as it is: nothing shrinks.
int shrinkLineHeight(int maxSize, int winW, int winH, const std::function<gfx::RectI(int)>& measure, bool* hitZero = nullptr);

class RShrinkText : public ui::TextWindow {
 public:
  explicit RShrinkText(ui::WindowManager& wm) : TextWindow(wm) {}
  std::string_view typeName() const override { return "RShrinkText"; }
  // The font table's second entry (an integer) is the largest size; a font table of one entry leaves it 0 (no shrinking).
  // The label of the style is laid out as given, at that size, without the loop (the original builds the text graphic
  // directly; only SetText shrinks).
  void init(const ui::Style& style) override;
  void setLabel(std::string s) override;
  std::string detail() const override;

  int maxSize() const { return max_; }
  int currentSize() const { return cur_; }

 private:
  int max_ = 0, cur_ = 0;
};

}  // namespace choco::games::greatcc
