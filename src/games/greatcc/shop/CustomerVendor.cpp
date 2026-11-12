#include "games/greatcc/shop/CustomerVendor.h"

#include <vector>

#include "games/greatcc/core/EndlessLogic.h"
#include "games/greatcc/shop/VCustomer.h"

namespace choco::games::greatcc {

namespace {
void loadCharacters(script::ScriptContext& ctx, const ui::Style& group, bool skipObjects, std::map<std::string, std::shared_ptr<VCustomer>>& into) {
  into.clear();
  const StyleChain parent(group, nullptr);
  group.forEachEntry([&](const std::string& key, const ui::Style* t, const std::optional<std::string>&) {
    if (!t) return;
    if (skipObjects) {
      const std::string k = VObject::lower(key);
      if (k == "thought_bubble" || k == "anim_menu" || k == "anim_money") return;
    }
    const StyleChain chain(*t, &parent);
    auto proto = VCustomer::create(ctx, nullptr);
    proto->loadFromTable(chain);
    into[t->stringOr("name")] = std::move(proto);
  });
}
}  // namespace

void CustomerVendor::loadPosse(const ui::Style& posse) { loadCharacters(ctx_, posse, true, posse_); }

void CustomerVendor::loadLocals(const ui::Style& locals) { loadCharacters(ctx_, locals, false, locals_); }

void CustomerVendor::loadObjects(const ui::Style& objects) {
  const StyleChain group(objects, nullptr);
  bubbleProto_ = VThoughtBubble::create(ctx_);
  if (auto t = objects.table("thought_bubble")) {
    const StyleChain chain(*t, &group);
    bubbleProto_->loadFromTable(chain);
  }
  menuProto_ = VObject::create(ctx_, "", "");
  if (auto t = objects.table("anim_menu")) {
    const StyleChain chain(*t, &group);
    menuProto_->loadFromTable(chain);
  }
  moneyProto_ = VObject::create(ctx_, "", "");
  if (auto t = objects.table("anim_money")) {
    const StyleChain chain(*t, &group);
    moneyProto_->loadFromTable(chain);
  }
  orderablesProto_ = VCarriable::create(ctx_, 1);
  if (auto t = objects.table("orderables")) {
    const StyleChain chain(*t, &group);
    orderablesProto_->loadFromTable(chain);
  }
}

std::shared_ptr<VCustomer> CustomerVendor::getCustomer(const std::string& name, ShopHost* host) const {
  auto it = posse_.find(name);
  VCustomer* proto = it != posse_.end() && it->second ? it->second.get() : nullptr;
  if (!proto) {
    auto lit = locals_.find(name);
    if (lit != locals_.end() && lit->second) proto = lit->second.get();
  }
  if (!proto) return nullptr;
  return makeCustomer(*proto, host);
}

std::shared_ptr<VCustomer> CustomerVendor::makeCustomer(VCustomer& proto, ShopHost* host) const {
  auto customer = VCustomer::create(ctx_, this);
  customer->setHost(host);
  proto.copyTo(*customer);
  customer->init();
  return customer;
}

std::shared_ptr<VCustomer> CustomerVendor::getRandomCustomer(ShopHost* host) const {
  std::vector<VCustomer*> all;
  for (const auto& [name, c] : posse_)
    if (c) all.push_back(c.get());
  for (const auto& [name, c] : locals_)
    if (c) all.push_back(c.get());
  if (all.empty()) return nullptr;
  const unsigned draw = all.size() > 1 ? ctx_.rng.next32() : 0;
  return makeCustomer(*all[randomCustomerIndex(static_cast<unsigned>(all.size()), draw)], host);
}

std::shared_ptr<VThoughtBubble> CustomerVendor::makeThoughtBubble() const {
  if (!bubbleProto_) return nullptr;
  auto b = VThoughtBubble::create(ctx_);
  bubbleProto_->copyTo(*b);
  b->switchAsset("transparent", "default", "default");
  b->setLeaf(true);
  return b;
}

std::shared_ptr<VObject> CustomerVendor::makeMenu() const {
  if (!menuProto_) return nullptr;
  auto o = VObject::create(ctx_, "", "");
  menuProto_->copyTo(*o);
  o->switchAsset("menu", "default", "default");
  o->setLeaf(true);  // after the copy, which brings the prototype's flags
  return o;
}

std::shared_ptr<VObject> CustomerVendor::makeMoney() const {
  if (!moneyProto_) return nullptr;
  auto o = VObject::create(ctx_, "", "");
  moneyProto_->copyTo(*o);
  o->setLeaf(true);  // after the copy, which brings the prototype's flags: a click on the pile is a click on the counter
  o->switchAsset("money", "default", "default");
  return o;
}

std::shared_ptr<VCarriable> CustomerVendor::makeOrderables() const {
  if (!orderablesProto_) return nullptr;
  auto c = VCarriable::create(ctx_, 1);
  orderablesProto_->copyTo(*c);
  c->setLeaf(true);
  return c;
}

}  // namespace choco::games::greatcc
