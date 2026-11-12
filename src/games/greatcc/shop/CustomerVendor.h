#pragma once

// VCustomerVendor (docs/re/greatcc-shop-simulation.md 3.4, 5.1): the characters the level files name. The twelve posse characters
// are loaded once from the shop's `posse` table and each location's locals for each level; a customer a level asks for is a copy of
// the prototype of that name (the posse first, then the locals). The vendor also keeps the shared pieces every customer copies: the
// thought bubble, the menu, the pile of money and the orderables (sauce bottle and drink cup) of the location's `objects` table.

#include <map>
#include <memory>
#include <string>

#include "games/greatcc/shop/ShopHost.h"
#include "games/greatcc/shop/ThoughtBubble.h"
#include "games/greatcc/shop/VCarriable.h"
#include "ui/Style.h"

namespace choco::games::greatcc {

class VCustomer;

class CustomerVendor {
 public:
  explicit CustomerVendor(script::ScriptContext& ctx) : ctx_(ctx) {}

  // LoadPosseFromTable: every table of `posse` except thought_bubble, anim_menu and anim_money becomes a prototype under its own
  // `name`. Replaces the previous posse.
  void loadPosse(const ui::Style& posse);
  // LoadLocalsFromTable: the same for the location's `customers`. Replaces the previous locals.
  void loadLocals(const ui::Style& locals);
  void clearLocals() { locals_.clear(); }
  // LoadObjectsFromTable: the thought bubble, the menu, the money and the orderables from the location's `objects`.
  void loadObjects(const ui::Style& objects);

  // GetCustomer: a new customer (the shop is its host) copied from the prototype of that name, initialised; null for a name that
  // neither the posse nor the locals know.
  std::shared_ptr<VCustomer> getCustomer(const std::string& name, ShopHost* host) const;
  // GetRandomCustomer: a customer copied from one of the posse then the locals, picked uniformly (a draw only when there is a choice);
  // null when there is none.
  std::shared_ptr<VCustomer> getRandomCustomer(ShopHost* host) const;
  bool knows(const std::string& type) const { return posse_.count(type) != 0 || locals_.count(type) != 0; }
  std::size_t posseCount() const { return posse_.size(); }
  std::size_t localCount() const { return locals_.size(); }

  // GetThoughtBubble / GetMenu / GetMoney / GetOrderables: a fresh copy of each (null when the table had none).
  std::shared_ptr<VThoughtBubble> makeThoughtBubble() const;
  std::shared_ptr<VObject> makeMenu() const;
  std::shared_ptr<VObject> makeMoney() const;
  std::shared_ptr<VCarriable> makeOrderables() const;

 private:
  std::shared_ptr<VCustomer> makeCustomer(VCustomer& proto, ShopHost* host) const;

  script::ScriptContext& ctx_;
  std::map<std::string, std::shared_ptr<VCustomer>> posse_, locals_;
  std::shared_ptr<VThoughtBubble> bubbleProto_;
  std::shared_ptr<VObject> menuProto_, moneyProto_;
  std::shared_ptr<VCarriable> orderablesProto_;
};

}  // namespace choco::games::greatcc
