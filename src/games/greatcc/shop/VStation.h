#pragma once

// VStation (docs/re/greatcc-shop-simulation.md 6.1, 6.2, docs/re/greatcc-machines-and-chocolates.md 10.2): a counter or a machine
// station of the shop: its art, its identity, its player points (the zone points the avatar stands at), customer point and money
// position. A click queues the avatar's walk to the nearest player point; the avatar's arrival there turns it to the point's
// facing. The machines and customers a station serves come with the slices that build them (the seams are named below).

#include <list>
#include <map>
#include <string>
#include <vector>

#include "games/greatcc/core/ZoneGraph.h"
#include "games/greatcc/shop/ShopHost.h"
#include "games/greatcc/shop/VMachine.h"
#include "games/greatcc/shop/VSuspendable.h"

namespace choco::games::greatcc {

class VCustomer;

// eStationType (locations/defines.lua)
enum StationType {
  kStationEmpty = 0, kStationCounter = 1, kStationMixer = 2, kStationIngredient = 3, kStationBarPress = 4, kStationTopping = 5,
  kStationSauce = 6, kStationInfuse = 7, kStationDrink = 8, kStationDisposal = 9, kStationDelivery = 10, kStationDisplay = 11,
  kStationConveyor = 12,
};
// eFacing
enum Facing { kFaceLeft = 0, kFaceRight = 1, kFaceFront = 2, kFaceBack = 3 };

class VStation : public VSuspendable {
 public:
  struct PlayerPoint {
    std::string zonePoint;
    int facing = kFaceFront;
  };

  explicit VStation(script::ScriptContext& ctx, int id = 0, int type = 0);
  // VStation::Create(id, type): disposal (9), display (11) and conveyor (12) stations are their own classes with their hover glow
  // (0.18, the disposal's offset along x only); a counter is a plain station with the same glow; every other type is a plain one.
  static std::shared_ptr<VStation> create(script::ScriptContext& ctx, int id, int type);
  const char* kind() const override { return "VStation"; }

  // The old machines and children go; `stationID` and `stationType` (any depth); each table of `machines` becomes a machine of its
  // own `stationType` (the station's by default), loaded, initialised, kept under its lower-cased key and added as a child (a
  // type with no machine class is logged once and skipped); `playerPoint` (a list of alternating zone point names and facings,
  // or one name); for counters `custPoint` (default customerNN) and `money`; then the VObject keys. The host must be set first:
  // the machines read their upgrades and the chocolate vendor through it.
  void loadFromTable(const StyleChain& style) override;
  // VObject::Init, then the tabletop shows its `upgrade2` state with the aesthetic upgrade kStation2, `upgrade1` with kStation1, else `default`, the machines are counted
  // (UpdateAvailable); a station without an enabled machine or a customer is not a mouse target.
  void init() override;
  // VStation::Update is VSprite::Update: a station's own wait (suspended while a machine works) is never resumed by the clock, only
  // by VSuspendable::Reset when the machine unlocks.
  void update(bool softPaused) override { VSprite::update(softPaused); }
  // UpdateAvailable: the enabled machines are counted; exactly one is the station's single machine. Every machine learns its
  // station.
  void updateAvailable();
  // VStation::OnMachineClick: the machine's name is queued and a click event goes to the station.
  void onMachineClick(const std::string& machineName);
  // The hover is passed to the single machine, so its glow follows.
  void onMouseEnter() override;
  void onMouseLeave() override;

  // Finds the player points (and the customer point of a counter) in the zone graph, after the table is read: the names that
  // are not points of the `avatar` zone are dropped. A single `playerPoint` name that is not one falls back to the station's own
  // name. The resolved points are kept in the order the graph made them (the original's set is ordered by allocation).
  void resolvePoints(const ZoneGraph& zones);
  // The shop this station belongs to (not owned).
  void setHost(ShopHost* host) { host_ = host; }
  ShopHost* host() const { return host_; }

  // VStation::OnClick: the player point nearest to the player (the first of equals) is appended to the player's move queue.
  // Nothing without a player or a point.
  void onClick() override;
  // VStation::OnArrival: unless the station is suspended (a machine of it is working) the single machine, or, with several
  // machines, each one whose name was queued by a click, gets the arrival, then the customer at the counter; the tutorial listeners
  // (slice 14) are called here too. Then the queued names are forgotten and, when the player's queue is empty, she turns
  // to the facing of the point.
  virtual void onArrival(VPlayer& player, const ZonePoint* point);
  // The resolved player points.
  const std::vector<ZonePoint*>& points() const { return points_; }
  bool hasPoint(const ZonePoint* p) const;
  // The facing stored for a point name (kFaceLeft, the map's default, for one that is not stored).
  int facingAt(const std::string& pointName) const;
  const ZonePoint* customerZonePoint() const { return custZone_; }

  // AttachCustomer: only a counter with nobody at it takes one (true); the station is then a mouse target.
  bool attachCustomer(const std::shared_ptr<VCustomer>& customer);
  // DetachCustomer: the customer's menu and money leave the station, the customer forgets it and the counter is a mouse target only
  // while it has machines (a hover that was on it ends). Returns the customer (null when there was none).
  std::shared_ptr<VCustomer> detachCustomer();
  std::shared_ptr<VCustomer> customer() const { return customer_.lock(); }

  int stationId() const { return id_; }
  int stationType() const { return type_; }
  const std::vector<PlayerPoint>& playerPoints() const { return playerPoints_; }
  const std::string& customerPoint() const { return custPoint_; }
  gfx::PointF money() const { return money_; }
  // The enabled machines (UpdateAvailable's count).
  int machineCount() const { return machineCount_; }
  const std::map<std::string, std::shared_ptr<VMachine>>& machines() const { return machines_; }
  const std::shared_ptr<VMachine>& singleMachine() const { return single_; }
  const std::list<std::string>& pendingClicks() const { return pendingClicks_; }

 private:
  int id_ = 0, type_ = 0;
  int machineCount_ = 0;
  std::map<std::string, std::shared_ptr<VMachine>> machines_;  // by lower-cased table key
  std::shared_ptr<VMachine> single_;
  std::list<std::string> pendingClicks_;
  ShopHost* host_ = nullptr;
  std::vector<PlayerPoint> playerPoints_;
  bool singleForm_ = false;  // `playerPoint` was one name, not a list
  std::vector<ZonePoint*> points_;
  std::map<std::string, int> facingByPoint_;
  const ZonePoint* custZone_ = nullptr;
  std::weak_ptr<VCustomer> customer_;  // +0x250
  std::string custPoint_;
  gfx::PointF money_;
};

}  // namespace choco::games::greatcc
