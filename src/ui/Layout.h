#pragma once

#include "ui/Style.h"

namespace choco::ui {

class Window;

// The magic numbers of the position/size encodings (kCenter and kMax in the prelude).
constexpr double kCenter = 80000;
constexpr double kMax = 160000;

// One axis of TWindow::FixSize (docs/re/design-questions.md section 12). `pos` and `size` are the style values (x/w or
// y/h) as integers, `parentDim` the parent's width or height, `hasParent` false when the window has no parent (only
// step 1 runs). `alignCenter` and `alignFar` are the axis's align bits (kHAlignCenter/kHAlignRight or
// kVAlignCenter/kVAlignBottom). `noWrap` is "align was given", which stops a negative position wrapping from the far edge.
struct AxisIn {
  int pos = 0;
  int size = 0;
  int parentDim = 0;
  bool hasParent = true;
  bool alignCenter = false;
  bool alignFar = false;
  bool noWrap = false;
};
struct AxisOut {
  int pos = 0;
  int size = 0;
};
AxisOut resolveAxis(const AxisIn& in);

// TWindow::SizeAndPositionFromStyle: reads x, y, w, h and align and applies them to the window's rectangle. A window
// whose left/top is already non-zero is not moved, and one whose width/height is already non-zero keeps it, so a second
// call (from PostChildrenInit) changes nothing that the first one set.
void sizeAndPositionFromStyle(Window& w, const Style& style);

// TWindowScript::SetPosition: x and y resolve as FixSize does with no align bits and negatives wrapping; the size is kept.
void setPositionFromScript(Window& w, double x, double y);

// TWindow::FitToChildren: grows the window to the right and bottom edges of its direct children (hidden ones too),
// never shrinks it and moves nothing.
void fitToChildren(Window& w);

}  // namespace choco::ui
