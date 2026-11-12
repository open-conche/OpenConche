#pragma once

// VConveyor (docs/re/greatcc-machines-and-chocolates.md 5.9): the belt above the counters. She puts a finished chocolate on it
// (anything that is not a bucket), it rides an arc past the counters, and each time it passes a counter its customer may take it as a
// free sample: the one waiting for the most patience, never a more patient one while a more urgent one stands further down the belt.
// An item nobody took falls into the bin at the end. A chocolate that has decayed is dropped from the belt.

#include <list>
#include <map>
#include <string>

#include "games/greatcc/shop/VStation.h"

namespace choco::games::greatcc {

class VConveyor : public VStation {
 public:
  // A chocolate riding the belt: its angle on the arc (radians, from the arc centre), the zone it last passed and the sprite.
  struct BeltItem {
    float theta = 0;
    std::string zone;
    std::shared_ptr<VObject> item;
  };
  // A named point of the belt (a counter, the bin, the drop): its distance from the arc centre and its angle.
  struct Zone {
    float radius = 0, angle = 0;
  };

  explicit VConveyor(script::ScriptContext& ctx, int id = 0);
  const char* kind() const override { return "VConveyor"; }

  // `arcCenter`, `arcStart` and `arcEnd` (points) give the arc's centre and its first and last angle and radius, `arcVelocity` the
  // speed, `stations` (names alternating with points) the zones; then VStation::LoadFromTable.
  void loadFromTable(const StyleChain& style) override;
  // Shown only when `kConveyor1` is on; then the `base` asset and VObject::Init (not VStation::Init).
  void init() override;
  // The belt's items move, then VStation::Update.
  void update(bool softPaused) override;
  // VStation::OnArrival, then the chocolate that is not a bucket (hand A first, else hand B) goes on the belt.
  void onArrival(VPlayer& player, const ZonePoint* point) override;

  // AddItemToBelt: at the start angle (plus `extra`), sprite layer 1, a child of the belt; the belt's own animation restarts and the
  // value `conveyor_max_this_level` follows the longest belt seen.
  void addItemToBelt(const std::shared_ptr<VObject>& item, float extra = 0);
  // UpdateBeltItems: each item in list order. One that has passed a new zone is offered to that counter's customer; an item that
  // is not taken moves on by `(velocity / radius)^2 * elapsed / arc` radians, held back so it keeps 0.08 rad from the one ahead; one
  // past the end is removed and the bin plays.
  void updateBeltItems();
  // GetStationZones: the nearest zone ahead of `theta` (empty when none) and the nearest behind.
  void getStationZones(float theta, std::string& ahead, std::string& behind) const;
  // CalculateRadius: the radius at `theta`, interpolated between the zones around it (the arc's own ends where there is none).
  float calculateRadius(float theta, const std::string& ahead, const std::string& behind) const;

  const std::list<BeltItem>& items() const { return items_; }
  const std::map<std::string, Zone>& zones() const { return zones_; }
  gfx::PointF center() const { return center_; }
  float startAngle() const { return startAngle_; }
  float endAngle() const { return endAngle_; }
  float startRadius() const { return startRadius_; }
  float endRadius() const { return endRadius_; }
  float velocity() const { return velocity_; }

 private:
  // The customer of the counter the item has just passed takes it (true: the item goes). `ahead` and `behind` are its zones.
  bool offerToCustomer(const BeltItem& it, const std::string& behind);
  void activateDisposal();
  gfx::PointF center_;                    // +0x2a0
  float startAngle_ = 0, endAngle_ = 0;   // +0x2a8, +0x2ac
  float startRadius_ = 0, endRadius_ = 0;  // +0x2b0, +0x2b4
  float arcLength_ = 0;                   // +0x2b8
  float velocity_ = 0;                    // +0x2bc
  std::map<std::string, Zone> zones_;     // +0x2c0
  std::list<BeltItem> items_;             // +0x2d8
  bool turnaround_ = false;               // +0x2e0 (never set: kConveyor2 is read by nothing)
};

}  // namespace choco::games::greatcc
