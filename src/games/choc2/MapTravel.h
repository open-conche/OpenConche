#pragma once

#include <memory>
#include <string>
#include <vector>

#include "games/choc2/TravelWeek.h"
#include "sdk/Sprite.h"
#include "gfx/Curve.h"
#include "gfx/Types.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"
#include "ui/WindowManager.h"
#include "games/common/SdkNames.h"

namespace choco::game::choc2 {

// MapTravel (Chocolatier 2, docs/re/choc2-port-and-ledger.md 5.1 and 5.3, docs/design/choc2-port-and-ledger.md 2.6): the
// window `ui/map.lua` creates beside the Map and that the travel natives talk to. Keys read: xOffset, yOffset (0), added to the
// map coordinates of a leg (the letterbox offsets; 0 at 800 x 600).
//
// The Lua drives the trip week by week (sim/travel.lua:78-101): InitiateTravel(leg) once per leg, then TravelOneWeek(type) per
// week, each followed by Pause(1900) and a wait until StillTravelling() is false. The window holds one root Sprite with the
// vehicle (image/icon_<type>) and the trail dots (image/traveldot) as children, and a WeekMover for the arithmetic.
//
// FACT (docs/re/choc2-port-binary.md 5): 40 ms tick, the icon is mirrored while the curve's tangent points right (art faces
// left), it is drawn only while a week runs, and a trail dot is dropped every 160 ms of leg time (WeekMover). The icons (single
// images) do not rotate. FACT (docs/re/choc2-map-binary.md 1.2): a week is WeekMover::kWeekMs = 2000 ms of a 40 ms tick count. The week is timed on the map modal's clock, so a dialog opened mid-week (the ledger's Pause or Recipes) holds the train and it goes on from where it stopped.
class MapTravel : public ui::Window {
 public:
  static constexpr double kTickMs = 40;
  static constexpr int kLayerDot = 1, kLayerVehicle = 10;

  MapTravel(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), ctx_(ctx) {}
  std::string_view typeName() const override { return "MapTravel"; }
  void init(const ui::Style& style) override;
  void postChildrenInit(const ui::Style& style) override;
  void draw(ui::DrawContext& ctx) const override;
  bool onAnimate() override;
  const Sprite* spriteRoot() const override { return root_.get(); }
  std::string detail() const override;

  // InitiateTravel(leg): a new leg of `weeks` weeks over the control points (3n+1 of them); the vehicle's icon is loaded from
  // `type`. Dots of an earlier leg stay on the map. A leg with fewer than 4 points is logged and ignored.
  bool initiateTravel(std::vector<gfx::PointF> points, int weeks, const std::string& type);
  // TravelOneWeek(type): reloads the icon if `type` differs and starts the next week. False (and a log line) with no leg.
  bool travelOneWeek(const std::string& type);
  // True while a week is animating.
  bool stillTravelling() const { return mover_.active(); }

  const WeekMover& mover() const { return mover_; }
  const std::string& vehicleType() const { return type_; }
  bool hasVehicleIcon() const { return vehicle_ && vehicle_->texture; }
  const Sprite* vehicle() const { return vehicle_; }
  int dotSprites() const;

  // The MapTravel the travel natives talk to: the first one in the modal chain, top modal first; null if none.
  static MapTravel* current(ui::WindowManager& wm);

 private:
  void loadIcon(const std::string& type);
  void place();
  void playTravelSounds(const std::string& type);

  script::ScriptContext& ctx_;
  std::unique_ptr<Sprite> root_;
  Sprite* vehicle_ = nullptr;  // owned by root_
  WeekMover mover_;
  std::string type_;
  std::string soundType_;  // the type TravelOneWeek last heard (TravelAudio::remember)
  float xOffset_ = 0, yOffset_ = 0;
  std::shared_ptr<gfx::Texture> dotTexture_;
  bool warnedIcon_ = false;
};

}  // namespace choco::game::choc2
