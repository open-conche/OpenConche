#pragma once

#include <vector>

#include "gfx/Curve.h"
#include "gfx/Types.h"

namespace choco::game::choc2 {

// The week mover of Chocolatier 2's travel (docs/design/choc2-port-and-ledger.md 2.6), pure so it can be tested without a
// window. A leg of `weeks` weeks is one Bezier curve; the Lua calls TravelOneWeek once per week, and each call moves the
// vehicle from week/weeks to (week+1)/weeks of the curve, uniformly in the curve parameter, over kWeekMs. FACT (docs/re/
// choc2-port-binary.md 5, TMapTravel::OnTaskAnimate): a trail dot is dropped each time the leg's clock passes a multiple of
// kDotMs (160 ms, the first at 160), at the vehicle's position then. The clock runs over the whole leg (weeks * kWeekMs).
class WeekMover {
 public:
  static constexpr double kWeekMs = 2000;
  static constexpr double kDotMs = 160;

  // Starts a leg at week 0. At least one week; fewer than 4 control points make a curve with no length (the vehicle sits on
  // its first point).
  void begin(gfx::Curve curve, int weeks);
  bool begun() const { return weeks_ > 0; }

  // Starts the next week's animation at `nowMs`. A week still running is finished first. False when no leg was begun or all its
  // weeks are done.
  bool startWeek(double nowMs);
  // Moves to `nowMs` and appends the trail dots reached to `newDots`. True while the week is still animating.
  bool step(double nowMs, std::vector<gfx::PointF>* newDots = nullptr);

  bool active() const { return active_; }
  int weeks() const { return weeks_; }
  // Weeks whose animation has finished.
  int weeksDone() const { return done_; }
  // 0..1 along the whole leg.
  float fraction() const { return fraction_; }
  // Milliseconds of leg time travelled so far (weeks done * kWeekMs plus the running week's elapsed time).
  double legMs() const { return legMs_; }
  gfx::PointF position() const { return curve_.at(fraction_); }
  int dots() const { return dots_; }
  const gfx::Curve& curve() const { return curve_; }

 private:
  void dropDots(std::vector<gfx::PointF>* out);

  gfx::Curve curve_;
  int weeks_ = 0, done_ = 0, dots_ = 0;
  bool active_ = false;
  double startMs_ = 0, legMs_ = 0;
  float from_ = 0, to_ = 0, fraction_ = 0;
};

}  // namespace choco::game::choc2
