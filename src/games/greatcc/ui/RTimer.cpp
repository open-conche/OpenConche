#include "games/greatcc/ui/RTimer.h"

#include "ui/WindowManager.h"

namespace choco::games::greatcc {

void RTimer::init(const ui::Style& style) {
  TextWindow::init(style);
  timer_.incrementing = style.booleanOr("isincrementing", timer_.incrementing);
  timer_.durationMs = static_cast<int>(style.numberOr("starttime", timer_.durationMs));
  hide_ = style.booleanOr("hide", hide_);
  refreshText();
}

void RTimer::postChildrenInit(const ui::Style& style) {
  TextWindow::postChildrenInit(style);
  if (hide_) setShown(false);
  manager().startWindowAnimation(id(), 16);
}

int RTimer::clockNow() const { return static_cast<int>(manager().modalNow(*this)); }

bool RTimer::onAnimate() {
  if (timer_.running) {
    timer_.tick(clockNow());
    refreshText();
  }
  return true;
}

std::string RTimer::detail() const {
  return TextWindow::detail() + " timer=" + std::to_string(timer_.elapsedMs) + "/" + std::to_string(timer_.durationMs) +
         (timer_.running ? " running" : " stopped") + (timer_.incrementing ? " up" : " down");
}

}  // namespace choco::games::greatcc
