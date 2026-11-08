#pragma once

#include "sdk/Sprite.h"
#include "gfx/Curve.h"
#include "games/common/SdkNames.h"

namespace choco::game {

// A sprite that follows a curve (TPathSprite, docs/re/port-window.md 1.9): cars, boats and clouds. update(dt) advances
// the path time, places the sprite on the curve, scales it by its y for perspective and, for a sheet with several
// frames, picks the frame from the heading. The window calls update(50) on every path sprite each 50 ms.
class PathSprite : public Sprite {
 public:
  enum class Mode { OneWay = 0, Pause = 1, Loop = 2, Bounce = 3 };

  explicit PathSprite(int layer = 0, std::string debugName = {}) : Sprite(layer, std::move(debugName)) {}

  void setCurve(gfx::Curve curve, float durationMs, Mode mode) {
    curve_ = std::move(curve);
    duration_ = durationMs;
    mode_ = mode;
  }
  // Perspective: scale runs linearly from scaleFar at yFar to scaleNear at yNear, unclamped. An equal yFar and yNear
  // gives the far scale (the original divides by zero).
  void setScaling(float yFar, float yNear, float scaleFar, float scaleNear);
  // Speed runs linearly from speedFar to speedNear in the same way, by the sprite's current y.
  void setSpeed(float speedFar, float speedNear) {
    speedFar_ = speedFar;
    speedNear_ = speedNear;
  }

  void update(float dtMs);

  // Chocolatier 2's `hold` (ms): the sprite parks at the end of its run (and, bouncing, at both ends) before it goes on.
  void setHold(float ms) { hold_ = ms; }
  float hold() const { return hold_; }
  // false: the sheet is a walk cycle played by its own script, not a set of heading frames.
  bool headingFrames = true;
  // Set when the art faces left (Chocolatier 2 ports): the sprite is mirrored while it moves to the right, and a
  // bouncing path's return leg counts as the opposite direction. Left false, the sprite is never mirrored.
  bool artFacesLeft = false;

  // The curve parameter in [0,1] for a path time, for Loop and Bounce. With hold = 0 this is exactly the formula without a
  // hold. Loop: the period is d + h, the sprite runs for d and then waits at the end for h. Bounce: the period is 2(d + h):
  // forward, park at the end, back, park at the start. A duration of 0 stays at the start.
  static float pathPhase(float timeMs, float durationMs, float holdMs, Mode mode);

  // Chocolatier 2's path arithmetic (docs/re/choc2-port-binary.md 1.4, TPort::Draw); set only for Chocolatier 2 ports.
  // `time` is the length of one run, and `bounce` is a round trip inside it (out in time/2, back in time/2). `hold` is a wait at the
  // START of each cycle (period time + hold). The perspective scale is clamped at 0 below.
  bool choc2Timing = false;
  // Chocolatier 2's `hflip` key: always mirrored, whatever the heading.
  bool forceHflip = false;
  // The curve parameter for Chocolatier 2 timing.
  static float choc2Phase(float timeMs, float durationMs, float holdMs, bool bounce);

  // The colour overlay (a child this sprite owns); it follows the sprite's frame.
  Sprite* mask = nullptr;

  float time() const { return time_; }
  float duration() const { return duration_; }
  Mode mode() const { return mode_; }
  float pathScale() const { return scale_; }
  const gfx::Curve& curve() const { return curve_; }

  // The frame for a heading (a normalised tangent) among `frames` frames; frame 0 faces up the screen and the frames go
  // clockwise. A zero tangent gives 90 degrees.
  // Chocolatier 2's mirror rule (TPort::Draw, TMapTravel::OnTaskAnimate): art faces left, so the picture is mirrored when the
  // tangent points right. angle = acos(t.y), negated when t.x > 0; mirrored iff angle < 0 or angle >= pi. A vertical tangent is
  // not mirrored going down and mirrored going up.
  static bool mirrorForTangent(gfx::PointF tangent);

  static int frameForHeading(gfx::PointF tangent, int frames);

 private:
  float perspective(float y, float far, float near) const;

  gfx::Curve curve_;
  float time_ = 0, duration_ = 0;
  Mode mode_ = Mode::OneWay;
  bool scaled_ = false;
  float yFar_ = 0, yNear_ = 0, scaleFar_ = 1, scaleNear_ = 1;
  float scale_ = 1;
  float speedFar_ = 1, speedNear_ = 1;
  float hold_ = 0;
  float prevPhase_ = 0;
  bool reversing_ = false;  // a bouncing sprite on its way back
};

}  // namespace choco::game
