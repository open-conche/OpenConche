#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "sdk/Ticker.h"
#include "sdk/Sprite.h"
#include "gfx/Curve.h"
#include "script/LuaState.h"
#include "games/common/SdkNames.h"

namespace choco::game {

// The vehicle's texture from a fraction of the trip on: `type = { {0, "travel_air"}, {0.5, "travel_boat"} }`.
struct TravelMode {
  float fraction = 0;
  std::string name = "travel_air";
};

// An action to run at `timeMs` of unpaused travel time. The trip lasts as long as its latest event.
struct TravelEvent {
  float timeMs = 0;
  script::LuaRef action;
};

// Vehicle scale by y (MapWindow keys yFar, yNear, farScale, nearScale).
struct TravelScaling {
  float yFar = 0, yNear = 400, farScale = 1, nearScale = 1;
};

// What the 16-way (or n-way) heading and the position of a trip at a given fraction look like.
struct TravelPose {
  gfx::PointF pos;
  int frame = 0;
  float rotationDeg = 0;
  float scale = 1;
};

// The vehicle frame for a heading and the residual rotation (docs/re/map-window.md 7, step 6). `tangent` is the
// curve's unit direction; (0,0) means "not written" and reads as facing east, as in the original.
TravelPose travelPose(const gfx::Curve& curve, float t, int frameCount, const TravelScaling& s);

// The 30 ms travel task (TravelTask, docs/re/map-window.md 7): moves the vehicle along the curve on wall-clock time
// minus the time spent paused, drops a trail dot every 12 px of arc length, switches the vehicle's texture and fires
// the due events. It owns no sprites and runs no Lua itself; the owner supplies what to do through Hooks.
class Travel {
 public:
  static constexpr double kIntervalMs = 30;
  static constexpr float kDotSpacing = 12;

  struct Hooks {
    std::function<void(const std::string& mode)> setMode;  // loads the vehicle's sheet; a no-op in tests
    std::function<void(gfx::PointF at)> addDot;
    // Runs one event. False means the owner is gone or going: the loop stops on the spot.
    std::function<bool(const TravelEvent&)> fire;
  };
  enum class Result { Running, Finished, OwnerGone };

  // `vehicle` stays hidden until the first completed tick. The first mode applies at once, whatever its fraction.
  Travel(gfx::Curve curve, Sprite& vehicle, std::vector<TravelMode> modes, std::vector<TravelEvent> events,
         TravelScaling scaling, double now, const std::function<void(const std::string&)>& setMode);

  // PauseTravel: the ticker stops; the vehicle freezes. ResumeTravel: the clock re-baselines, so paused time is lost.
  void pause();
  void restart(double now);
  bool paused() const { return paused_; }

  // At most one step per call; true to run again later. Does nothing once finished.
  Result tick(double now, const Hooks& hooks);
  bool finished() const { return finished_; }

  float fraction() const { return t_; }
  double timeMs() const { return time_; }
  float durationMs() const { return duration_; }
  std::size_t eventsFired() const { return eventAt_; }
  std::size_t eventCount() const { return events_.size(); }
  int dots() const { return dots_; }
  const gfx::Curve& curve() const { return curve_; }
  const std::string& mode() const { return mode_; }

 private:
  gfx::Curve curve_;
  Sprite& vehicle_;
  std::vector<TravelMode> modes_;
  std::size_t modeAt_ = 0;
  std::vector<TravelEvent> events_;
  std::size_t eventAt_ = 0;
  TravelScaling scaling_;
  Ticker ticker_;
  double time_ = 0, lastClock_ = 0;
  float duration_ = 0;
  float t_ = 0;
  int dots_ = 0;
  bool paused_ = false, finished_ = false;
  std::string mode_;
};

}  // namespace choco::game
