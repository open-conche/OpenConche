#pragma once

// RBounceWindow (docs/re/greatcc-ui-library.md 2.1): an RWindow that slides from its style position to `targetposx/y` along
// 1 - sin(pi t)/(pi t), on the modal clock. RBounceOff (Transitions.h) retargets it to leave the screen again.

#include "games/greatcc/ui/RWindow.h"

namespace choco::games::greatcc {

// BounceFunctionValue (0x6d256): a = (funcmin - funcmax) * t; function 0 (sincx) is sin(a)/a, function 1 (sincpix) is
// sin(a pi)/(a pi). With the defaults the window's position fraction is 1 minus this.
float bounceFunctionValue(int function, float funcMin, float funcMax, float t);

// The side of the screen a window entered from, by its start position (RBounceOff's constructor rules, applied in order, the
// later ones overriding the earlier; top is also the default).
enum class BounceSide { Top = 0, Right = 1, Bottom = 2, Left = 3 };
BounceSide bounceSide(int startX, int startY);

// The exit target of RBounceOff for `mode` 0..3 (kBounceBack, Through, Clock, Counter): entry = (X,Y), current target =
// (targetX, targetY).
struct BouncePoint {
  int x = 0, y = 0;
  bool operator==(const BouncePoint&) const = default;
};
BouncePoint bounceOffTarget(int mode, BounceSide side, int entryX, int entryY, int targetX, int targetY);

class RBounceWindow : public RWindow {
 public:
  explicit RBounceWindow(ui::WindowManager& wm) : RWindow(wm) {}
  std::string_view typeName() const override { return "RBounceWindow"; }
  // Keys: targetposx/y (default the window's own position), bouncelen (500), funcmin (0), funcmax (pi), bouncefunc ("sincx").
  void postChildrenInit(const ui::Style& style) override;
  bool onAnimate() override;
  std::string detail() const override;

  // SetTargetPos: nothing when the target is unchanged; otherwise the slide restarts from the current position.
  void setTargetPos(int x, int y);
  int targetX() const { return targetX_; }
  int targetY() const { return targetY_; }
  // Where the window entered from (its style position); fixed.
  int entryX() const { return entryX_; }
  int entryY() const { return entryY_; }
  int startX() const { return startX_; }
  int startY() const { return startY_; }
  int bounceLenMs() const { return durationMs_; }
  float progress() const { return t_; }
  bool moving() const { return moving_; }

 private:
  int entryX_ = 0, entryY_ = 0, startX_ = 0, startY_ = 0, targetX_ = 0, targetY_ = 0;
  int durationMs_ = 500;
  double startTime_ = 0;  // 0 = not taken yet
  float funcMin_ = 0.0f, funcMax_ = 3.1415927f;
  float t_ = 0;
  int function_ = 0;
  bool moving_ = false;
};

}  // namespace choco::games::greatcc
