#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "games/common/Travel.h"
#include "sdk/Sprite.h"
#include "gfx/Curve.h"
#include "ui/Window.h"
#include "ui/WindowManager.h"
#include "games/common/SdkNames.h"

namespace choco::gfx {
class SheetCache;
}
namespace choco::script {
class Rng;
class Log;
}

namespace choco::game {

class World;

// ---- Pure parsing (unit-tested without a window) ----

// What a port contributes to the map (docs/re/map-window.md 4): its name, marker position and optional popup position.
struct PortInfo {
  std::string name;
  float mapx = 0, mapy = 0;
  std::string align;
  std::optional<int> popupX, popupY;
};

// The player fields the map reads: the port the player stands in and Player.portsAvailable by port name.
struct PlayerInfo {
  std::string portName;
  std::map<std::string, std::string> available;
};

// Every port table in `allPorts` (pairs order); entries without a name are skipped. Null gives none.
std::vector<PortInfo> parsePorts(const ui::Style* allPorts);
// portsAvailable has no key iteration in a Style, so it is read for the ports given.
PlayerInfo parsePlayer(const ui::Style* player, const std::vector<PortInfo>& ports);

// The marker for a Player.portsAvailable state. `hidden` has none; every unknown state is the locked marker.
struct MarkerLook {
  std::string image;     // image/port_visited, or an XML sheet name
  bool sheet = false;    // loaded through the SheetCache
  bool animated = false; // plays "DoAnim"
};
std::optional<MarkerLook> markerLook(std::string_view state);

// What InitiateTravel's table says.
struct TravelRequest {
  gfx::Curve curve;
  std::vector<TravelMode> modes;
  std::vector<TravelEvent> events;
};
// `path` is a list of {x, y} points (non-table entries are skipped); without one the curve runs from `from` to
// `dest.mapx/mapy` as {from, from, dest, dest}. `type` is a list of {fraction, mode}; `events` a list of {time, action}.
TravelRequest parseTravelRequest(const ui::Style& t, gfx::PointF from);
// The straight default route {A, A, B, B}.
std::vector<gfx::PointF> straightRoute(gfx::PointF a, gfx::PointF b);
// A via route: the first leg's points followed by the second leg's from index 2 on (the shared waypoint once).
std::vector<gfx::PointF> mergeRoutePoints(const std::vector<gfx::PointF>& first, const std::vector<gfx::PointF>& second);

// What a MapWindow needs from the app besides the window manager. Both may be null in tests (no sheets, fixed clouds).
struct MapEnv {
  gfx::SheetCache* sheets = nullptr;
  script::Rng* rng = nullptr;
  script::Log* log = nullptr;  // for the marker scripts; without it markers stay on their first frame
  World* world = nullptr;      // told about this map when it is built
};

// The world map (MapWindow, docs/re/map-window.md): the background, a marker, label and rollover per visible port, the
// "you are here" ring, drifting clouds, and the vehicle, trail and travel task of a trip in progress.
class MapWindow : public ui::Window {
 public:
  static constexpr double kCloudStepMs = 30;
  static constexpr int kRolloverHalf = 17;

  MapWindow(ui::WindowManager& wm, MapEnv env);
  std::string_view typeName() const override { return "MapWindow"; }
  void init(const ui::Style& style) override;
  void postChildrenInit(const ui::Style& style) override;
  void draw(ui::DrawContext& ctx) const override;
  bool onAnimate() override;
  const Sprite* spriteRoot() const override { return root_.get(); }
  std::string detail() const override;

  // Builds markers, labels, rollovers and the ring from the data (postChildrenInit reads it from Lua).
  void collectPortData(const PlayerInfo& player, const std::vector<PortInfo>& ports);

  // InitiateTravel: ignored (false) once a travel exists. `now` is the script clock.
  bool initiateTravel(TravelRequest request, double now);
  void pauseTravel();
  void resumeTravel(double now);
  // WindowToggleForeground: loss stops the cloud animation; gain restarts it (no catch-up) and re-bases a travel that is not
  // paused, so the flight does not jump over the time spent away. A paused travel stays paused until the dialog's OK.
  void setForeground(bool fg, double now);
  // One frame from World::tick with the script clock: the marker scripts, then the travel task.
  void frame(double now);
  // One frame of the travel task. Events go to the script sink through runInScript.
  void tickTravel(double now);
  ~MapWindow() override;
  Travel* travel() { return travel_.get(); }
  const Travel* travel() const { return travel_.get(); }

  Sprite* currentPort() const { return current_; }
  Sprite* markerOf(std::string_view port) const;
  int ringCount() const;
  double cloudX() const;  // for tests: the cloud strip's x
  const std::string& background() const { return background_; }

 private:
  struct Marker {
    std::string name;
    Sprite* sprite = nullptr;
  };
  void updateClouds();
  void setVehicleMode(const std::string& mode);
  std::shared_ptr<gfx::Texture> texture(std::string_view name) const;

  MapEnv env_;
  // Keys of MW 2.
  std::string background_;
  double cloudY_ = 360, cloudTime_ = 20000, cloudCounter_ = 0;
  TravelScaling scaling_;

  std::unique_ptr<Sprite> root_;
  Sprite* clouds_ = nullptr;
  Sprite* current_ = nullptr;
  Sprite* vehicle_ = nullptr;
  std::vector<Marker> markers_;
  // After root_: the travel refers to sprites inside the tree, so it dies first.
  std::unique_ptr<Travel> travel_;
  bool arrivedLogged_ = false;  // the "[travel] arrived" line was written
};

// Registers the MapWindow type with the manager.
void registerMapWindow(ui::WindowManager& wm, MapEnv env);

}  // namespace choco::game
