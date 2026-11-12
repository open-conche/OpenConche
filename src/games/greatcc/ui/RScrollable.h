#pragma once

// RScrollable (docs/re/greatcc-ui-library.md 6.1): a window whose content lives in an interior RWindow that slides inside it.
// After its children exist it moves every direct child (except a first slider and the names listed in `except`) into the
// interior, fits the interior to them and adds it. The slider (value 0..1) drives the interior's position.

#include <string>
#include <vector>

#include "games/greatcc/ui/RWindow.h"

namespace choco::games::greatcc {

class RScrollable : public RWindow {
 public:
  explicit RScrollable(ui::WindowManager& wm) : RWindow(wm) {}
  std::string_view typeName() const override { return "RScrollable"; }
  // Keys: vscroll (true; false scrolls sideways), except (a list of window names left in place), tick (pixels, 0).
  void postChildrenInit(const ui::Style& style) override;
  bool onMessage(const ui::Message& m) override;
  std::string detail() const override;

  // SetInteriorPosition: clamps to 0..1, moves the slider and the interior. An interior shorter than the view resets to 0.
  void setInteriorPosition(float f);
  // ScrollForward / ScrollBack: one `tick` of pixels along the scroll, as a fraction of the scrollable length. Nothing when the
  // interior is not longer than the view (the original divides by zero there).
  void scrollForward() { scrollBy(+1); }
  void scrollBack() { scrollBy(-1); }

  float position() const { return pos_; }
  int tick() const { return tick_; }
  bool vertical() const { return vertical_; }
  ui::Window* interior() const;
  ui::Window* slider() const;

 private:
  void scrollBy(int dir);
  ui::WindowId interior_ = 0, slider_ = 0;
  float pos_ = 0;
  int tick_ = 0;
  bool vertical_ = true;
  std::vector<std::string> except_;
};

}  // namespace choco::games::greatcc
