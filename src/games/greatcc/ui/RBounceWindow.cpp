#include "games/greatcc/ui/RBounceWindow.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {
constexpr float kPi = 3.1415927f;
}

float bounceFunctionValue(int function, float funcMin, float funcMax, float t) {
  const float a = (funcMin - funcMax) * t;
  if (function == 1) return std::sin(a * kPi) / (a * kPi);
  return std::sin(a) / a;
}

BounceSide bounceSide(int x, int y) {
  // The original's four tests in its order, later ones overriding earlier ones; x and y are signed where no abs is written.
  int side = (y < std::abs(x) && x < 0) ? 3 : 0;
  if (std::abs(x) < y) side = 2;
  if (x < std::abs(y) && y < 0) side = 0;
  if (std::abs(y) < x) side = 1;
  return static_cast<BounceSide>(side);
}

BouncePoint bounceOffTarget(int mode, BounceSide side, int x, int y, int tx, int ty) {
  const bool horizontal = side == BounceSide::Left || side == BounceSide::Right;  // entered from the left or right
  switch (mode) {
    case 1: return horizontal ? BouncePoint{-x, y} : BouncePoint{x, -y};
    case 2: return horizontal ? BouncePoint{tx, x} : BouncePoint{-y, ty};
    case 3: return horizontal ? BouncePoint{tx, -x} : BouncePoint{y, ty};
    default: return {x, y};
  }
}

void RBounceWindow::postChildrenInit(const ui::Style& style) {
  RWindow::postChildrenInit(style);
  entryX_ = startX_ = rect().x;
  entryY_ = startY_ = rect().y;
  targetX_ = static_cast<int>(style.numberOr("targetposx", rect().x));
  targetY_ = static_cast<int>(style.numberOr("targetposy", rect().y));
  durationMs_ = static_cast<int>(style.numberOr("bouncelen", 500));
  funcMin_ = static_cast<float>(style.numberOr("funcmin", 0.0));
  funcMax_ = static_cast<float>(style.numberOr("funcmax", kPi));
  std::string fn = style.stringOr("bouncefunc", "sincx");
  for (char& c : fn) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  function_ = fn == "sincpix" ? 1 : 0;
  // Initialize(target) = SetTargetPos(target), which is a no-op because the target was just stored, then the 16 ms animation.
  manager().startWindowAnimation(id(), 16);
}

void RBounceWindow::setTargetPos(int x, int y) {
  if (x == targetX_ && y == targetY_) return;
  targetX_ = x;
  targetY_ = y;
  startX_ = rect().x;  // OnSetTarget: slide from where the window is now
  startY_ = rect().y;
  startTime_ = 0;
  t_ = 0;
  moving_ = true;
}

bool RBounceWindow::onAnimate() {
  RWindow::onAnimate();
  if (t_ < 1.0f) {
    if (startTime_ == 0 && lastMs() != 0) startTime_ = lastMs();
    t_ = durationMs_ > 0 ? static_cast<float>((lastMs() - startTime_) / durationMs_) : 1.0f;
  }
  if (t_ >= 1.0f || t_ <= 0.0f) {
    if (t_ == 0.0f) setPos(startX_, startY_);
    else setPos(targetX_, targetY_);
    if (t_ >= 1.0f) moving_ = false;  // OnReachedTarget
  } else {
    const float v = bounceFunctionValue(function_, funcMin_, funcMax_, t_);
    setPos(targetX_ - static_cast<int>(static_cast<float>(targetX_ - startX_) * v),
           targetY_ - static_cast<int>(static_cast<float>(targetY_ - startY_) * v));
  }
  return true;
}

std::string RBounceWindow::detail() const {
  char buf[128];
  std::snprintf(buf, sizeof buf, " target=%d,%d len=%dms t=%.2f", targetX_, targetY_, durationMs_, t_);
  return RWindow::detail() + buf;
}

}  // namespace choco::games::greatcc
