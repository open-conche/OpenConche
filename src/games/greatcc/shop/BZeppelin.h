#pragma once

// BZeppelin (docs/re/greatcc-meta-flow.md 4.6): the airship of the delivery station. When a box is being packed it flies in along a
// cubic Bezier curve, hovers at the top right until the box is ready, picks it up with its own animation and flies away. It is the
// scene's own sprite (the shop adds it), not part of the station.

#include <memory>

#include "games/greatcc/scene/VObject.h"

namespace choco::games::greatcc {

class VMachine;

class BZeppelin : public VObject {
 public:
  // The state at +0x284.
  enum State { kIdle = 0, kWaiting = 5, kGrabbing = 6 };

  explicit BZeppelin(script::ScriptContext& ctx, std::string name = "zeppelin");
  ~BZeppelin() override;
  static std::shared_ptr<BZeppelin> create(script::ScriptContext& ctx, std::string name = "zeppelin") {
    return std::make_shared<BZeppelin>(ctx, std::move(name));
  }
  const char* kind() const override { return "BZeppelin"; }

  // The empty airship: hidden, idle, the image the one without the box, a fresh approach curve; a running engine sound ends.
  void init() override;
  // The airship appears left of the screen and flies in over 4.5 s (the engine loop sounds); nothing when it is flying in already.
  void pickup();
  // The box is ready: the airship takes it as soon as it has arrived. The machine is told when the pickup is under way.
  void readyForPickup(const std::shared_ptr<VMachine>& machine);
  // The sprite's update, then the flight: arrival, waiting, the grab (the machine is reset once the pickup animation is past its
  // 8th frame, every update until it ends), and the flight away.
  void update(bool softPaused) override;

  int state() const { return state_; }
  bool approaching() const { return approaching_; }
  bool leaving() const { return leaving_; }
  bool ready() const { return ready_; }
  gfx::PointF flightPosition() const { return pos_; }
  VSprite* body() const { return body_.get(); }
  // The control points of the curve in use (start, first, second, end).
  gfx::PointF p0() const { return p0_; }
  gfx::PointF p1() const { return p1_; }
  gfx::PointF p2() const { return p2_; }
  gfx::PointF p3() const { return p3_; }
  int durationMs() const { return duration_; }

 private:
  float rand(float lo, float hi) const;
  void waitForChocolate();
  void grabBox();
  void leave();
  // UpdatePosition: the curve at the time since the start; returns that fraction (it may pass 1).
  float updatePosition();

  std::shared_ptr<VSprite> body_;  // +0x218
  std::shared_ptr<const gfx::SpriteSheet> pickupSheet_, withBox_, noBox_;
  gfx::PointF p0_, p3_, pos_, p1_, p2_;  // +0x238, +0x240, +0x248, +0x250, +0x258
  int duration_ = 4500;                  // +0x268: the one in use
  int leaveMs_ = 4500, approachMs_ = 4500;  // +0x26c, +0x270
  double start_ = 0;                     // +0x264
  bool approaching_ = false, leaving_ = false, ready_ = false;  // +0x260, +0x261, +0x262
  int state_ = kIdle;
  std::weak_ptr<VMachine> machine_;      // +0x27c
  int voice_ = 0;                        // +0x210
};

}  // namespace choco::games::greatcc
