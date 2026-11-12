#pragma once

// What the characters and stations ask of the shop window (ShopWindow::GetInstance and its getters in the original). The shop
// outlives its sprites, so they may keep a pointer to it; it is not the world (design 3.2).

#include <memory>
#include <string>
#include <vector>

namespace choco::games::greatcc {

class VChocolate;
class VChocolateVendor;
class VCustomer;
class VPlayer;
class VSprite;
class VStation;
struct OrderInfo;
struct ZonePoint;

class ShopHost {
 public:
  virtual ~ShopHost() = default;
  // ShopWindow::GetPlayer.
  virtual std::shared_ptr<VPlayer> player() const = 0;
  // station16 exists and is shown: the avatar then routes round the middle table through the zone graph.
  virtual bool hasCenterTable() const = 0;
  // The station whose player points include `point`, the first by name when several do (null when none).
  virtual std::shared_ptr<VStation> stationByZonePoint(const ZonePoint* point) const = 0;
  // A tutorial is running and not finished.
  virtual bool tutorialInEffect() const { return false; }
  // VUpgradeManager::IsActive(name, category) (the machines read their upgrades from here at Init and when they start).
  virtual bool upgradeActive(const std::string& /*name*/, const std::string& /*category*/) const { return false; }
  // ShopWindow::GetChocolateVendor (null before the location's chocolates are loaded).
  virtual std::shared_ptr<VChocolateVendor> chocolateVendor() const { return nullptr; }
  // ShopWindow::IsEndlessMode.
  virtual bool isEndless() const { return false; }
  // ShopWindow::AddCombo(type): the chain of the same kind of event (menu 1, service 2, payment 4) and its bonus.
  virtual void addCombo(int /*type*/) {}
  // ShopWindow::OrderComplete(price, position, customer): pays the sale and the tip of `customer` (none for a delivery), shows the
  // amount at (x, y), checks the goals, then counts a payment in the chain.
  virtual void orderComplete(float /*price*/, float /*x*/, float /*y*/, const VCustomer* /*customer*/) {}
  // The level clock's elapsed milliseconds (the endless run's orders depend on it).
  virtual int levelElapsedMs() const { return 0; }
  // ShopWindow::FindOrderWithLayer(c0, c1, layer, option): the one of the two chocolates a counter customer's wanted chocolate is
  // heading toward with that layer (the counters in order), else `c0` when there is one, else `c1`.
  virtual std::shared_ptr<VChocolate> findOrderWithLayer(const std::shared_ptr<VChocolate>& c0, const std::shared_ptr<VChocolate>& /*c1*/,
                                                         int /*layer*/, int /*option*/) const {
    return c0;
  }
  // ShopWindow::GetStationByName (lower case name, "station07") and GetStationByNumber (1..17); null when there is none.
  virtual std::shared_ptr<VStation> stationByName(const std::string& /*name*/) const { return nullptr; }
  virtual std::shared_ptr<VStation> stationByNumber(int /*n*/) const { return nullptr; }
  // ShopWindow::LevelIsComplete: the level was over, or (story) its time is up.
  virtual bool levelIsComplete() const { return false; }
  // ShopWindow::WaitOnZeppelin(wait): the level may not end while a delivery is under way.
  virtual void waitOnZeppelin(bool /*wait*/) {}
  // ShopWindow::AddObject: a sprite that belongs to the shop's own scene rather than to a station (the delivery's zeppelin).
  virtual void addSceneSprite(const std::shared_ptr<VSprite>& /*sprite*/) {}
  // The story level's zeppelin orders in file order (CSLevel+0x48); null in endless mode or before a level is loaded.
  virtual const std::vector<OrderInfo>* zeppelinOrders() const { return nullptr; }
};

}  // namespace choco::games::greatcc
