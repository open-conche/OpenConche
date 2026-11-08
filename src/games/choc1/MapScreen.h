#pragma once

// Chocolatier 1's MapScreen window (TMapScreen, exe vtable 004e867c; docs/re/choc1-map.md sections 2 and 3) and the natives
// AnimateTravel, PauseTravel and ResumeTravel. The window owns a whole trip: a polyline and a duration per leg, one vehicle
// sprite, the trail dots, and the two Lua command strings (gSim:TravelTick(), gSim:ArriveAtPort()). MapTrip is the plain logic
// (no window, no Lua, a caller-supplied clock) so the update steps are unit-tested; MapScreen wires it to the window system.

#include <memory>
#include <string>
#include <vector>

#include "games/choc1/SimpleAnimation.h"
#include "gfx/SpriteSheet.h"
#include "gfx/Texture.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::game::choc1 {

class MapTrip {
 public:
  static constexpr int kWeekMs = 2000;   // one sim week of trip time
  static constexpr int kDotMs = 200;     // a trail dot every 200 ms of leg time
  static constexpr int kFadeLeftMs = 1000;
  struct Leg {
    PolylinePath path;   // its time() is the leg duration (weeks * 2000, rounded)
    std::string vehicle;
  };
  struct Dot {
    float x = 0, y = 0;
  };
  // What one update asks the owner to do (step numbers: docs/re/choc1-map.md 3.3).
  struct Step {
    enum class Command { None, Tick, TickSting, Arrive };
    Command command = Command::None;
    bool fade = false;       // step 5: start the 1000 ms music fade
    std::string sound;       // step 7: PlaySound(<vehicle>), empty = none
  };

  // The duration of a leg of `weeks` weeks (3.2 step 4): round(weeks * 2000).
  static int legDurationMs(double weeks);

  // 3.2 steps 1 and 5-6. An empty leg list leaves the trip inactive.
  void start(std::string endName, std::vector<Leg> legs, long long nowMs);
  // 3.3. Does nothing while inactive.
  Step update(long long nowMs);
  // ResumeTravel: the paused interval is not counted.
  void resetClock(long long nowMs) { lastMs_ = nowMs; }

  bool active() const { return active_; }
  bool started() const { return !legs_.empty(); }
  const std::string& endName() const { return end_; }
  std::size_t legIndex() const { return idx_; }
  std::size_t legCount() const { return legs_.size(); }
  long long elapsedMs() const { return elapsed_; }
  const std::vector<Dot>& dots() const { return dots_; }
  const std::vector<Leg>& legs() const { return legs_; }
  Dot pos() const { return pos_; }
  double heading() const { return heading_; }
  const std::string& vehicle() const;

 private:
  void sampleDots(const Leg& leg);
  std::vector<Leg> legs_;
  std::string end_;
  std::size_t idx_ = 0;
  long long elapsed_ = 0, lastMs_ = 0, trail_ = 0;
  std::string lastSound_;
  std::vector<Dot> dots_;
  Dot pos_;
  double heading_ = 0;
  bool active_ = false;
};

class MapScreen : public ui::Window {
 public:
  static constexpr double kUpdateMs = 62;  // the window's own timer, started in PostInit (3.2 / section 2)
  MapScreen(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), ctx_(ctx) {}
  ~MapScreen() override;
  std::string_view typeName() const override { return "MapScreen"; }
  void init(const ui::Style& style) override;
  void postChildrenInit(const ui::Style& style) override;
  void draw(ui::DrawContext& ctx) const override;
  bool onAnimate() override;
  std::string detail() const override;

  // The natives. `route` is the Lua table at (L, index).
  void animateTravel(lua_State* L, int index);
  void pause();
  void resume();
  const MapTrip& trip() const { return trip_; }

  // The MapScreen in the modal chain, or null.
  static MapScreen* inChain(ui::WindowManager& wm);

  // The sheet frame shown `elapsedMs` into a leg: (elapsed / 100) % frames.
  static int frameAt(long long elapsedMs, int frames) { return frames > 0 ? static_cast<int>((elapsedMs / 100) % frames) : 0; }

 private:
  void releaseGate();
  void runCommand(const std::string& code);
  script::ScriptContext& ctx_;
  MapTrip trip_;
  bool gate_ = false;  // this window holds one count of Choc1World::mapTrips
  std::shared_ptr<const gfx::SpriteSheet> boat_, train_, airship_;
  std::shared_ptr<gfx::Texture> dot_;
};

}  // namespace choco::game::choc1
