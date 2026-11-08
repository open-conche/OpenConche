#include "games/common/sprite/PathSprite.h"

#include <algorithm>
#include <cmath>

namespace choco::game {

float PathSprite::perspective(float y, float far, float near) const {
  const float span = yNear_ - yFar_;
  const float f = span != 0 ? (y - yFar_) / span : 0;
  return far + f * (near - far);
}

void PathSprite::setScaling(float yFar, float yNear, float scaleFar, float scaleNear) {
  scaled_ = true;
  yFar_ = yFar;
  yNear_ = yNear;
  scaleFar_ = scaleFar;
  scaleNear_ = scaleNear;
  scale_ = perspective(pos.y, scaleFar_, scaleNear_);
}

int PathSprite::frameForHeading(gfx::PointF t, int frames) {
  if (frames <= 0) return 0;
  constexpr float kPi = 3.14159265358979f;
  float angle = 180.0f - std::acos(std::clamp(t.y, -1.0f, 1.0f)) * 180.0f / kPi;
  if (t.x < 0) angle = 360.0f - angle;
  const unsigned f = static_cast<unsigned>(angle / (360.0f / static_cast<float>(frames)) + 0.5f);
  return static_cast<int>(f % static_cast<unsigned>(frames));
}

bool PathSprite::mirrorForTangent(gfx::PointF t) {
  constexpr float kPi = 3.14159265358979f;
  float angle = std::acos(std::clamp(t.y, -1.0f, 1.0f));
  if (t.x > 0) angle = -angle;
  return angle < 0 || angle >= kPi - 1e-5f;  // acos(-1) is pi within float rounding
}

float PathSprite::pathPhase(float t, float d, float h, Mode mode) {
  if (d <= 0) return 0;
  if (h <= 0) {
    if (mode == Mode::Bounce) {
      const float m = std::fmod(t / d, 2.0f);
      return m >= 1.0f ? 2.0f - m : m;
    }
    return std::fmod(t / d, 1.0f);
  }
  if (mode == Mode::Bounce) {
    const float p = std::fmod(t, 2 * (d + h));
    if (p < d) return p / d;
    if (p < d + h) return 1.0f;
    if (p < 2 * d + h) return 1.0f - (p - d - h) / d;
    return 0.0f;
  }
  return std::min(std::fmod(t, d + h) / d, 1.0f);
}

float PathSprite::choc2Phase(float t, float d, float h, bool bounce) {
  if (d <= 0) return 0;
  t = std::fmod(t, d + h);
  t = t < h ? 0.0f : t - h;
  if (!bounce) return t / d;
  if (t >= d / 2) t = d - t;
  return 2 * t / d;
}

void PathSprite::update(float dt) {
  if (removed) return;
  // 1. Time, with the speed taken from the y of the previous update.
  if (speedFar_ == speedNear_) time_ += dt;
  else time_ += perspective(pos.y, speedFar_, speedNear_) * dt;

  // 2. The curve parameter. A duration of 0 (no `time` key) stays at the start.
  gfx::PointF point, tangent;
  const bool finite = (mode_ == Mode::OneWay || mode_ == Mode::Pause);
  if (finite && time_ >= duration_) {
    point = curve_.at(1.0f);
    if (mode_ == Mode::OneWay) removed = true;
  } else {
    const float m = choc2Timing ? choc2Phase(time_, duration_, hold_, mode_ == Mode::Bounce) : pathPhase(time_, duration_, hold_, mode_);
    point = curve_.at(m, &tangent);
    if (mode_ == Mode::Bounce) {
      if (m < prevPhase_) reversing_ = true;
      else if (m > prevPhase_) reversing_ = false;
      prevPhase_ = m;
    }
  }
  // Facing: parked or degenerate stretches keep the last facing.
  if (const float dx = reversing_ ? -tangent.x : tangent.x; std::fabs(dx) > 1e-3f) {
    hflip = artFacesLeft && dx > 0;
    if (mask) mask->hflip = hflip;
  }

  if (forceHflip) {
    hflip = true;
    if (mask) mask->hflip = true;
  }

  // 3. Perspective scale from the new y.
  scale_ = scaled_ ? perspective(point.y, scaleFar_, scaleNear_) : 1.0f;
  if (choc2Timing) scale_ = std::max(0.0f, scale_);

  // 4. The frame from the heading.
  const int frames = sheet ? static_cast<int>(sheet->frames.size()) : 1;
  if (frames > 1 && headingFrames) {
    frame = frameForHeading(tangent, frames);
    if (mask) mask->frame = frame;
    // The horizontal tile copies are the same picture: they show the same frame.
    for (const auto& c : children())
      if (c->debugName == "tile") c->frame = frame;
  }
  for (const auto& c : children())
    if (c->debugName == "tile") c->hflip = hflip;

  // 5. Place it.
  scale = scale_;
  pos = point;
}

}  // namespace choco::game
