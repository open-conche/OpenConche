#include "games/choc2/TravelWeek.h"

#include <algorithm>
#include <cmath>

namespace choco::game::choc2 {

void WeekMover::begin(gfx::Curve curve, int weeks) {
  curve_ = std::move(curve);
  weeks_ = std::max(1, weeks);
  done_ = 0;
  dots_ = 0;
  active_ = false;
  from_ = to_ = fraction_ = 0;
  legMs_ = 0;
}

bool WeekMover::startWeek(double nowMs) {
  if (!begun()) return false;
  if (active_) step(startMs_ + kWeekMs, nullptr);  // the Lua always waits, but a second call must not lose a week
  if (done_ >= weeks_) return false;
  from_ = static_cast<float>(done_) / weeks_;
  to_ = static_cast<float>(done_ + 1) / weeks_;
  fraction_ = from_;
  legMs_ = done_ * kWeekMs;
  startMs_ = nowMs;
  active_ = true;
  return true;
}

void WeekMover::dropDots(std::vector<gfx::PointF>* out) {
  if (curve_.length() <= 0) return;
  const double total = weeks_ * kWeekMs;
  // Dot k (1-based) is dropped when the leg clock reaches k * kDotMs, at that fraction of the curve.
  const int reached = static_cast<int>(std::floor(legMs_ / kDotMs));
  for (; dots_ < reached; ++dots_)
    if (out) out->push_back(curve_.at(static_cast<float>((dots_ + 1) * kDotMs / total)));
}

bool WeekMover::step(double nowMs, std::vector<gfx::PointF>* newDots) {
  if (!active_) return false;
  const double e = std::clamp((nowMs - startMs_) / kWeekMs, 0.0, 1.0);
  if (e >= 1.0) {
    fraction_ = to_;
    ++done_;
    legMs_ = done_ * kWeekMs;
    active_ = false;
  } else {
    fraction_ = from_ + static_cast<float>(e) * (to_ - from_);
    legMs_ = (done_ + e) * kWeekMs;
  }
  dropDots(newDots);
  return active_;
}

}  // namespace choco::game::choc2
