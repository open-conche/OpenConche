#include "games/greatcc/ui/RShrinkText.h"

#include <algorithm>

#include "ui/WindowManager.h"

namespace choco::games::greatcc {

int shrinkLineHeight(int maxSize, int winW, int winH, const std::function<gfx::RectI(int)>& measure, bool* hitZero) {
  if (hitZero) *hitZero = false;
  if (maxSize <= 0) return maxSize;
  int cur = maxSize;
  gfx::RectI b = measure(cur);
  while (winW <= b.w || winH <= b.h) {
    --cur;
    if (cur <= 0) {
      if (hitZero) *hitZero = true;
      return 0;
    }
    b = measure(cur);
  }
  return cur;
}

void RShrinkText::init(const ui::Style& style) {
  TextWindow::init(style);
  // `numbers("font", 2, -1)` reads [1] and [2]; a missing second entry stays -1.
  if (auto f = style.numbers("font", 2, -1.0); f && (*f)[1] >= 0) max_ = static_cast<int>((*f)[1]);
  cur_ = max_;
}

void RShrinkText::setLabel(std::string s) {
  if (max_ != 0) {
    cur_ = max_;
    setTextSize(static_cast<float>(max_));
  }
  TextWindow::setLabel(std::move(s));
  if (max_ <= 0) return;
  bool zero = false;
  cur_ = shrinkLineHeight(
      max_, rect().w, rect().h,
      [this](int lh) {
        setTextSize(static_cast<float>(lh));
        return measuredBounds();
      },
      &zero);
  if (zero) {
    manager().log("RShrinkText: Line Height Zero");
    setTextSize(1.0f);  // nothing sensible to draw at 0; the size stays reported as 0
  }
}

std::string RShrinkText::detail() const {
  return TextWindow::detail() + " shrink=" + std::to_string(cur_) + "/" + std::to_string(max_);
}

}  // namespace choco::games::greatcc
