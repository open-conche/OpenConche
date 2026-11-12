#pragma once

// VCustomer (docs/re/greatcc-shop-simulation.md 5): a person at a counter. It arrives fading in at the counter's customer point with
// a menu bubble over its head; when the player comes to the counter it reads the menu for `orderDelay` ms and then shows what it
// wants as bubbles; the player serves it one item per visit; its patience drains from arrival on, and it leaves paying (a pile of
// money on the counter, collected by the player's next visit) or angry. The numbers are core/CustomerLogic's; this keeps the sprites.
// It is a VSuspendable (a VCharacter in the original, whose walk it never uses).

#include <list>
#include <map>
#include <memory>
#include <string>

#include "games/greatcc/core/CustomerLogic.h"
#include "games/greatcc/core/LevelData.h"
#include "games/greatcc/shop/ShopHost.h"
#include "games/greatcc/shop/ThoughtBubble.h"
#include "games/greatcc/shop/VCarriable.h"
#include "games/greatcc/shop/VSuspendable.h"

namespace choco::games::greatcc {

class CustomerVendor;
class VPlayer;
class VStation;

class VCustomer : public VSuspendable {
 public:
  // `vendor` is null for a prototype (the tables' customers); a customer made from one gets its own copies of the bubble, the menu,
  // the money and the orderables.
  VCustomer(script::ScriptContext& ctx, const CustomerVendor* vendor);
  ~VCustomer() override;
  static std::shared_ptr<VCustomer> create(script::ScriptContext& ctx, const CustomerVendor* vendor) {
    return std::make_shared<VCustomer>(ctx, vendor);
  }
  const char* kind() const override { return "VCustomer"; }
  void setHost(ShopHost* host) { host_ = host; }

  // `decay` (the ms to lose all patience when 1 or more, else the rate), `tip`, `orderDelay`, then the tables named `static_*` and
  // `idle_*` become the customer's static and idle sprites (children, leaves, kept by lower-cased key); then the VObject keys; the
  // draw depth is 0.
  void loadFromTable(const StyleChain& style) override;
  // The rates and delay, a copy of each static and idle sprite, then VObject::CopyTo.
  void copyTo(VSprite& dst) override;
  // The base asset, the idle sprites hidden and the static ones shown, the mood from the patience; no orders, nothing wanted.
  void init() override;
  // SetCustomerData: the level's data names the sprite (when it has a name) and may replace the tip and the decay rate.
  void setCustomerData(const std::shared_ptr<CustomerData>& data);
  const std::shared_ptr<CustomerData>& customerData() const { return data_; }

  // SetActive(station, patience): the customer is at the counter, fading in, with the menu bubble; state 1.
  void setActive(const std::shared_ptr<VStation>& station, float startPatience);
  // SetStation: remembers the counter and moves to its customer point (no walk).
  void setStation(const std::shared_ptr<VStation>& station);
  std::shared_ptr<VStation> station() const { return station_.lock(); }

  // Drain and mood, the fade end, the idle chatter, the end-of-wait; then VSuspendable::Update.
  void update(bool softPaused) override;
  // The menu was read: the orders are made and shown (state 3).
  bool onResume() override;
  // The player has come to the counter (what happens depends on the state, see the fact file 5.2).
  void onArrival(VPlayer& player);
  void onClick() override;
  void onMouseEnter() override;
  void onMouseLeave() override;

  // ---- orders ----
  // GenerateOrders: the level's scripted orders, else random ones.
  void generateOrders();
  // AddOrder: a chocolate from the vendor (nothing when it makes none), or a sauce or drink picture.
  void addOrder(const OrderInfo& info);
  // AttachThought / DetachThought: the bubbles show the orders; the first becomes the wanted one.
  void attachThought();
  void detachThought();
  // ServeCustomer: false (nothing changes) unless the item matches what is wanted.
  bool serveCustomer(const std::shared_ptr<VCarriable>& item);
  // ComplimentaryService(item): the conveyor's gift.
  bool complimentaryService(const std::shared_ptr<VCarriable>& item);
  bool hasMoreOrders() const { return !orders_.empty() || wanted_; }
  const std::list<std::shared_ptr<VCarriable>>& orders() const { return orders_; }
  const std::shared_ptr<VCarriable>& wanted() const { return wanted_; }

  // ---- state ----
  // UpdateMood, SetMood, Payout (a customer in state 4 or 5 leaves, with its money if it was served anything).
  void updateMood();
  void setMood(int mood);
  void payout();
  bool shouldDelete() const;
  int state() const { return state_; }
  int mood() const { return mood_; }
  float patience() const { return patience_; }
  void setPatience(float p) { patience_ = p; }
  float decayRate() const { return decay_; }
  float tip() const { return tip_; }
  float price() const { return price_; }
  int orderDelayMs() const { return orderDelay_; }
  // The state alone, for the tests.
  void setState(int s) { state_ = s; }
  const std::shared_ptr<VThoughtBubble>& bubble() const { return bubble_; }
  const std::shared_ptr<VObject>& menu() const { return menu_; }
  const std::shared_ptr<VObject>& money() const { return money_; }
  const std::map<std::string, std::shared_ptr<VObject>>& statics() const { return statics_; }
  const std::map<std::string, std::shared_ptr<VObject>>& idles() const { return idles_; }

  // The idle chatter (decorative): at most four run in the shop at once.
  bool playIdleAnim();
  void stopIdleAnim();
  void enableStatics(bool on);
  bool idleActive() const { return idleActive_; }
  int idleCooldownMs() const { return idleCooldown_; }
  static int idleAnimsRunning();
  static void resetIdleAnimsRunning();

 private:
  void payoutMoney(const std::shared_ptr<VStation>& station);
  void collectMoney(const std::shared_ptr<VStation>& station);
  ShopHost* host_ = nullptr;
  float patience_ = 100.0f;    // 0x290
  float decay_ = 0.001f;       // 0x294: per ms
  float tip_ = 100.0f;         // 0x298
  int orderDelay_ = 5000;      // 0x2a0
  float price_ = 0;            // 0x2a4
  int moneyStamp_ = 0;         // 0x2a8: the clock when the pile was made (endless: it decays), 0 for none
  int state_ = kCustomerPrototype;  // 0x2ac
  int mood_ = kMoodNone;       // 0x2b0
  std::shared_ptr<VThoughtBubble> bubble_;  // 0x2b4
  std::shared_ptr<VObject> menu_, money_;   // 0x2bc, 0x2c4
  std::shared_ptr<VCarriable> orderables_;  // 0x2cc
  std::map<std::string, std::shared_ptr<VObject>> idles_, statics_;  // 0x2d4, 0x2ec
  std::list<std::shared_ptr<VCarriable>> orders_;  // 0x30c
  std::shared_ptr<VCarriable> wanted_;             // 0x314
  std::shared_ptr<CustomerData> data_;             // 0x31c
  std::weak_ptr<VStation> station_;                // 0x324
  std::shared_ptr<SpriteEffect> fade_, moneyFade_;  // 0x32c, 0x334
  bool idleActive_ = false;                         // 0x226
  std::shared_ptr<VObject> curIdle_;                // 0x238
  int idleCooldown_ = 0;                            // 0x240
};

}  // namespace choco::games::greatcc
