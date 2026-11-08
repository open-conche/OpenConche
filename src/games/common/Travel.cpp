#include "games/common/Travel.h"

#include <algorithm>
#include <cmath>

namespace choco::game {

namespace {
constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

double fractionalPart(double v) { return v - std::floor(v); }
}  // namespace

TravelPose travelPose(const gfx::Curve& curve, float t, int frameCount, const TravelScaling& s) {
  TravelPose out;
  gfx::PointF tangent{0, 0};
  out.pos = curve.at(t, &tangent);
  const int n = std::max(1, frameCount);
  const double step = 360.0 / n;
  double a = 180.0 - std::acos(std::clamp(static_cast<double>(tangent.y), -1.0, 1.0)) * kRadToDeg;
  if (tangent.x < 0) a = 360.0 - a;
  const double nearest = std::floor(a / step + 0.5);
  out.frame = static_cast<int>(std::fmod(nearest, n));
  if (s.yFar != s.yNear) {
    // Perspective scale replaces the matrix, which discards the residual rotation: the heading is quantised to frames.
    out.scale = (out.pos.y - s.yFar) / (s.yNear - s.yFar) * (s.nearScale - s.farScale) + s.farScale;
  } else {
    out.rotationDeg = static_cast<float>(a - nearest * step);
  }
  return out;
}

Travel::Travel(gfx::Curve curve, Sprite& vehicle, std::vector<TravelMode> modes, std::vector<TravelEvent> events,
               TravelScaling scaling, double now, const std::function<void(const std::string&)>& setMode)
    : curve_(std::move(curve)),
      vehicle_(vehicle),
      modes_(std::move(modes)),
      events_(std::move(events)),
      scaling_(scaling),
      lastClock_(now) {
  vehicle_.visible = false;
  for (const TravelEvent& e : events_) duration_ = std::max(duration_, e.timeMs);  // AddEvent
  mode_ = modes_.empty() ? std::string("travel_air") : modes_[0].name;
  modeAt_ = modes_.empty() ? 0 : 1;
  if (setMode) setMode(mode_);
  ticker_.start(now, kIntervalMs);
}

void Travel::pause() {
  ticker_.pause();
  paused_ = true;
}

void Travel::restart(double now) {
  lastClock_ = now;
  ticker_.start(now, kIntervalMs);
  paused_ = false;
}

Travel::Result Travel::tick(double now, const Hooks& hooks) {
  if (finished_) return Result::Finished;
  if (!ticker_.due(now)) return Result::Running;

  const double oldT = duration_ > 0 ? fractionalPart(time_ / duration_) : 0.0;
  time_ += now - lastClock_;
  lastClock_ = now;
  const double t = time_ < duration_ ? fractionalPart(time_ / duration_) : 1.0;
  t_ = static_cast<float>(t);

  // A trail dot for every 12 px of arc length reached.
  const float len = curve_.length();
  if (len > 0) {
    const int oldCount = std::max(0, static_cast<int>(std::floor(oldT * len)) / static_cast<int>(kDotSpacing));
    const int newCount = static_cast<int>(std::floor(t * len)) / static_cast<int>(kDotSpacing);
    for (int i = oldCount; i < newCount; ++i) {
      if (hooks.addDot) hooks.addDot(curve_.at(static_cast<float>(i) * kDotSpacing / len));
      ++dots_;
    }
  }

  // The vehicle's texture changes at the listed fractions.
  while (modeAt_ < modes_.size() && modes_[modeAt_].fraction <= t_) {
    mode_ = modes_[modeAt_++].name;
    if (hooks.setMode) hooks.setMode(mode_);
  }

  const int frames = vehicle_.sheet ? static_cast<int>(vehicle_.sheet->frames.size()) : 1;
  const TravelPose pose = travelPose(curve_, t_, frames, scaling_);
  vehicle_.pos = pose.pos;
  vehicle_.frame = pose.frame;
  vehicle_.rotationDeg = pose.rotationDeg;
  vehicle_.scale = pose.scale;

  // Due events in list order; the first one still in the future stops the scan, and the last tick fires the rest.
  while (eventAt_ < events_.size() && (events_[eventAt_].timeMs <= time_ || t >= 1.0)) {
    const TravelEvent& e = events_[eventAt_++];
    if (hooks.fire && !hooks.fire(e)) return Result::OwnerGone;
  }

  vehicle_.visible = true;
  if (t >= 1.0) {
    finished_ = true;
    return Result::Finished;
  }
  return Result::Running;
}

}  // namespace choco::game
