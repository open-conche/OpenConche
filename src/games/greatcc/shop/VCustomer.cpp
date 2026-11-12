#include "games/greatcc/shop/VCustomer.h"

#include <algorithm>
#include <cmath>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/core/EndlessLogic.h"
#include "games/greatcc/shop/CustomerVendor.h"
#include "games/greatcc/shop/VChocolate.h"
#include "games/greatcc/shop/VPlayer.h"
#include "games/greatcc/shop/VStation.h"

namespace choco::games::greatcc {

namespace {
int gIdleAnimsRunning = 0;  // mAnimationsRunning: the idle animations playing in the shop, at most four

double valueReal(script::ScriptContext& ctx, const char* name, double def) {
  GreatCCWorld* w = worldOf(ctx);
  return w ? w->values().getReal(name, def) : def;
}
int valueInt(script::ScriptContext& ctx, const char* name, int def) {
  GreatCCWorld* w = worldOf(ctx);
  return w ? w->values().getInt(name, def) : def;
}
unsigned valueUint(script::ScriptContext& ctx, const char* name, unsigned def) {
  GreatCCWorld* w = worldOf(ctx);
  return w ? w->values().getUint(name, def) : def;
}
void playSfx(script::ScriptContext& ctx, const char* path) {
  if (GreatCCWorld* w = worldOf(ctx)) w->sound().playSfx(path, false);
}
}  // namespace

int VCustomer::idleAnimsRunning() { return gIdleAnimsRunning; }
void VCustomer::resetIdleAnimsRunning() { gIdleAnimsRunning = 0; }

VCustomer::VCustomer(script::ScriptContext& ctx, const CustomerVendor* vendor) : VSuspendable(ctx, "", "customer") {
  setClickable(true);
  if (vendor) {
    bubble_ = vendor->makeThoughtBubble();
    menu_ = vendor->makeMenu();
    money_ = vendor->makeMoney();
    orderables_ = vendor->makeOrderables();
  }
}

VCustomer::~VCustomer() { stopIdleAnim(); }

// ---- loading ----

void VCustomer::loadFromTable(const StyleChain& style) {
  const auto oldStatics = statics_;
  const auto oldIdles = idles_;
  statics_.clear();
  idles_.clear();
  // the three numbers look in the table and the one it was found in (the posse's or the location's)
  const double decay = style.numberOr("decay", 100000.0, 1);
  decay_ = decayRateFromTable(static_cast<float>(decay));
  tip_ = static_cast<float>(style.numberOr("tip", 100.0, 1));
  orderDelay_ = static_cast<int>(static_cast<unsigned>(style.numberOr("orderDelay", 5000.0, 1)));
  style.own().forEachEntry([&](const std::string& key, const ui::Style* t, const std::optional<std::string>&) {
    if (!t) return;
    const std::string k = lower(key);
    const bool isStatic = k.compare(0, 7, "static_") == 0;
    const bool isIdle = !isStatic && k.compare(0, 5, "idle_") == 0;
    if (!isStatic && !isIdle) return;
    auto& previous = isStatic ? oldStatics : oldIdles;
    auto& into = isStatic ? statics_ : idles_;
    std::shared_ptr<VObject> obj;
    if (auto it = previous.find(k); it != previous.end()) obj = it->second;
    if (!obj) {
      obj = VObject::create(context(), k, isStatic ? "customer_static" : "customer_idle");
      addChild(obj);
    }
    const StyleChain chain(*t, &style);
    obj->loadFromTable(chain);
    obj->setLeaf(true);
    into[k] = obj;
  });
  VObject::loadFromTable(style);
  setDepth(0);
}

void VCustomer::copyTo(VSprite& dst) {
  if (auto* d = dynamic_cast<VCustomer*>(&dst)) {
    d->decay_ = decay_;
    d->tip_ = tip_;
    d->orderDelay_ = orderDelay_;
    for (const auto& [key, obj] : statics_) {
      if (!obj) continue;
      auto c = VObject::create(context(), "", "");
      obj->copyTo(*c);
      d->statics_[key] = c;
      d->addChild(c);
    }
    for (const auto& [key, obj] : idles_) {
      if (!obj) continue;
      auto c = VObject::create(context(), "", "");
      obj->copyTo(*c);
      d->idles_[key] = c;
      d->addChild(c);
    }
  }
  VObject::copyTo(dst);
}

void VCustomer::init() {
  switchAsset("base", "default", "default");
  VObject::init();
  for (const auto& [key, obj] : idles_)
    if (obj) {
      obj->setVisible(false);
      obj->init();
    }
  for (const auto& [key, obj] : statics_)
    if (obj) {
      obj->setVisible(true);
      obj->init();
    }
  updateMood();
  orders_.clear();
  wanted_.reset();
}

void VCustomer::setCustomerData(const std::shared_ptr<CustomerData>& data) {
  data_ = data;
  if (!data_) return;
  if (!data_->name.empty()) setName(data_->name);
  if (0.0f <= data_->tip) tip_ = data_->tip;
  if (0.0f <= data_->decay) decay_ = data_->decay;
}

// ---- arrival ----

void VCustomer::setStation(const std::shared_ptr<VStation>& station) {
  station_ = station;
  if (station && station->customerZonePoint())
    setPosition({static_cast<float>(station->customerZonePoint()->x), static_cast<float>(station->customerZonePoint()->y)});
}

void VCustomer::setActive(const std::shared_ptr<VStation>& station, float startPatience) {
  if (!station) return;
  const std::shared_ptr<VCustomer> self = std::static_pointer_cast<VCustomer>(shared_from_this());
  init();
  idleCooldown_ = 0;
  state_ = kCustomerArrived;
  patience_ = startPatience;
  station->attachCustomer(self);
  setStation(station);
  addEffect(std::make_shared<AlphaEffect>(true, 1.0f, valueInt(context(), "customer_fade_time", 500), -1.0f, 0));
  if (bubble_ && menu_) {
    detachThought();
    addChild(bubble_);
    bubble_->attachToAnchor("bubble_single");
    menu_->setCurrentFrame(1);
    bubble_->addOrder(menu_, "menu", true);
    bubble_->setMood(mood_);
  }
  // the tutorial trigger: a tutorial that waits for this customer (by name) starts here
  if (GreatCCWorld* w = worldOf(context())) {
    const std::shared_ptr<VSprite> keep = shared_from_this();
    w->tutorials().checkTutorialsByTrigger("customer_arrival", name());
  }
}

// ---- the per-frame update ----

void VCustomer::update(bool softPaused) {
  const std::shared_ptr<VSprite> keep = shared_from_this();
  if (!softPaused) {
    if (state_ > 0 && state_ < 4) {
      patience_ = drainPatience(patience_, static_cast<unsigned>(getElapsed()), decay_,
                                static_cast<float>(valueReal(context(), "patience_decay_modifier", 1.0)),
                                static_cast<float>(valueReal(context(), "level_customer_decay", 1.0)));
      updateMood();
    }
  }
  if (fade_ && !fade_->inProgress()) {
    fade_.reset();
    setVisible(false);
  }
  if ((state_ == kCustomerArrived || state_ == kCustomerOrdering) && !idleActive_) {
    bool rearm = true;
    if (idleCooldown_ > 0) {
      idleCooldown_ -= static_cast<int>(static_cast<unsigned>(getElapsed()));
      if (idleCooldown_ < 1) {
        idleCooldown_ = 0;
        playIdleAnim();
      }
      if (idleCooldown_ > 0) rearm = false;
    }
    if (rearm) {
      const int var = valueInt(context(), "customer_idle_cooldown_var", 0);
      const int low = -var / 2, high = var / 2;
      unsigned draw = 0;
      if (low < high) draw = context().rng.next32() % static_cast<unsigned>(std::abs(high - low) + 1);
      idleCooldown_ = static_cast<int>(draw) + low + valueInt(context(), "customer_idle_cooldown_base", 0);
    }
  }
  if (idleActive_ && (!curIdle_ || !curIdle_->scriptIsPlaying(-1))) stopIdleAnim();
  // a pile of money decays in the endless run: it fades and the customer is done
  if (!(host_ && host_->isEndless()) || moneyStamp_ == 0 || state_ != kCustomerMoney) {
    moneyStamp_ = 0;
  } else if (endlessMoneyDecayed(static_cast<int>(clockNow()), moneyStamp_, valueUint(context(), "endless_money_decay", 0))) {
    moneyStamp_ = 0;
    if (money_) {
      money_->cancelEffects();
      moneyFade_ = std::make_shared<AlphaEffect>(false, 1.0f, valueInt(context(), "transition_time", 200), -1.0f, 0);
      money_->addEffect(moneyFade_);
    }
    state_ = kCustomerDone;
  }
  VSuspendable::update(softPaused);
}

bool VCustomer::onResume() {
  if (state_ == kCustomerReading) {
    generateOrders();
    state_ = kCustomerOrdering;
    attachThought();
    setMood(mood_);
    if (GreatCCWorld* w = worldOf(context())) {
      const std::shared_ptr<VSprite> keep = shared_from_this();
      w->tutorials().checkTutorialsByTrigger("customer_order", name());
    }
  }
  return true;
}

void VCustomer::onArrival(VPlayer& player) {
  const std::shared_ptr<VSprite> keep = shared_from_this();
  if (state_ == kCustomerOrdering) {
    // one item per visit: slot A first, then slot B
    bool served = false;
    if (const std::shared_ptr<VCarriable> item = player.getObject(VPlayer::kHandA)) {
      if (serveCustomer(item)) {
        player.detachObject(VPlayer::kHandA);
        served = true;
      }
    }
    if (!served) {
      if (const std::shared_ptr<VCarriable> item = player.getObject(VPlayer::kHandB)) {
        if (serveCustomer(item)) player.detachObject(VPlayer::kHandB);
      }
    }
    if (!hasMoreOrders()) {
      state_ = kCustomerServed;
      payout();
    }
    return;
  }
  if (state_ == kCustomerMoney) {
    collectMoney(station());
    return;
  }
  if (state_ != kCustomerArrived) return;
  state_ = kCustomerReading;
  setMood(mood_);
  if (bubble_) bubble_->nextBubble();
  playSfx(context(), "audio/sfx/menu.ogg");
  suspend(orderDelay_);
  if (host_) host_->addCombo(kComboMenu);
}

void VCustomer::collectMoney(const std::shared_ptr<VStation>& station) {
  float x = 0, y = 0;
  if (station) {
    x = station->money().x;
    y = station->money().y;
  }
  if (host_) host_->orderComplete(price_, x, y - 50.0f, this);
  if (money_) {
    money_->cancelEffects();
    moneyFade_ = std::make_shared<AlphaEffect>(false, 1.0f, valueInt(context(), "transition_time", 200), -1.0f, 0);
    money_->addEffect(moneyFade_);
  }
  playSfx(context(), "audio/sfx/score_board.ogg");
  state_ = kCustomerDone;
}

void VCustomer::onClick() {
  if (const std::shared_ptr<VStation> s = station()) s->doEvent(SpriteEvent::Click, {});
}
void VCustomer::onMouseEnter() {
  if (const std::shared_ptr<VStation> s = station()) s->doEvent(SpriteEvent::Enter, {});
}
void VCustomer::onMouseLeave() {
  if (const std::shared_ptr<VStation> s = station()) s->doEvent(SpriteEvent::Leave, {});
}

// ---- mood ----

void VCustomer::updateMood() {
  const float p = patience_;
  const int m = moodForPatience(p);
  if (m == kMoodNone) {
    if (0.0f < p) return;
    if (state_ < kCustomerServed) {
      state_ = kCustomerAngry;
      payout();
    }
    return;
  }
  if (mood_ != m) setMood(m);
}

void VCustomer::setMood(int mood) {
  mood_ = mood;
  if (bubble_) bubble_->setMood(mood);
  const char* name = "";
  switch (mood) {
    case 1: case 2: name = "happy"; break;
    case 3: name = "neutral"; break;
    case 4: name = "annoyed"; break;
    case 5: name = "angry"; break;
    default: break;
  }
  for (const auto& [key, obj] : statics_)
    if (obj) obj->switchState(state_ == kCustomerReading ? "menu" : name, "default");
}

void VCustomer::enableStatics(bool on) {
  for (const auto& [key, obj] : statics_)
    if (obj) obj->setVisible(on);
}

// ---- the idle chatter ----

bool VCustomer::playIdleAnim() {
  if (idleActive_ || gIdleAnimsRunning >= 4) return false;
  const auto it = idles_.find(mood_ > 3 ? "idle_angry" : "idle_happy");
  curIdle_ = it == idles_.end() ? nullptr : it->second;
  if (!curIdle_) return false;
  curIdle_->setVisible(true);
  curIdle_->play("", false, true, 0);
  enableStatics(false);
  idleActive_ = true;
  ++gIdleAnimsRunning;
  return true;
}

void VCustomer::stopIdleAnim() {
  if (idleActive_) {
    idleActive_ = false;
    for (const auto& [key, obj] : idles_)
      if (obj) {
        obj->stop(true);
        obj->setVisible(false);
      }
    enableStatics(true);
    gIdleAnimsRunning = std::max(0, gIdleAnimsRunning - 1);
  }
  curIdle_.reset();
}

// ---- orders ----

void VCustomer::addOrder(const OrderInfo& info) {
  if (info.type < 1) {
    // a chocolate (an order of an unknown type asks for one the vendor does not make: nothing is added)
    if (!host_) return;
    const std::shared_ptr<VChocolateVendor> vendor = host_->chocolateVendor();
    if (!vendor) return;
    if (const std::shared_ptr<VChocolate> choc = vendor->getChocolate(info, true)) orders_.push_back(choc);
    return;
  }
  auto item = VCarriable::create(context(), 1);
  if (orderables_) orderables_->copyTo(*item);
  item->init();
  if (info.type == kOrderSauce) {
    item->setType("sauce");
    item->switchAsset("sauce", "default", "default");
    item->setScale(0.8f, 0.8f);
  } else if (info.type == kOrderDrink) {
    item->setType("drink");
    item->switchAsset("drink", "default", "default");
  }
  orders_.push_back(item);
}

void VCustomer::generateOrders() {
  orders_.clear();
  if (data_ && !data_->orders.empty()) {
    for (const OrderInfo& o : data_->orders) addOrder(o);
    return;
  }
  if (!host_) return;
  const std::shared_ptr<VChocolateVendor> vendor = host_->chocolateVendor();
  if (!vendor) return;
  GreatCCWorld* w = worldOf(context());
  if (!w) return;
  int count = 1;
  if (!host_->isEndless()) {
    const int level = w->levels().currentLevel();
    const int minLevel = w->values().getInt("multi_order_min_level", 0);
    if (minLevel <= level % 10) {
      const float chance = w->values().getProduct("multi_order_chance", static_cast<float>(level % 10) - static_cast<float>(minLevel) + 1.0f, false);
      const float roll = static_cast<float>(context().rng.next32() % 100u);
      count = chance <= roll ? 1 : 2;
    }
  } else {
    const double diff = w->values().getReal("endless_diff_modifier", 0);
    const unsigned seconds = static_cast<unsigned>(host_->levelElapsedMs()) / 1000u;
    const float chance = w->values().getProduct("endless_multi_chance", static_cast<float>(seconds) / (static_cast<float>(diff) + 1.0f), true);
    const float roll = static_cast<float>(context().rng.next32()) * 2.3283064e-10f;
    count = chance <= roll ? 1 : 2;
  }
  bool unmakeable = false, firstUnmakeable = false;
  for (int i = 0; i < count; ++i) {
    const bool sauceOk = orderables_ && host_->upgradeActive("kSauce1", "station") && orderables_->hasAsset("sauce");
    const bool drinkOk = orderables_ && host_->upgradeActive("kDrink1", "station") && orderables_->hasAsset("drink");
    if ((sauceOk || drinkOk) && context().rng.next32() % 6u == 0) {
      // a sauce or a drink; with a drink on offer a coin says which (an odd draw is a sauce whatever is on offer)
      int type = kOrderSauce;
      if (drinkOk) type = (context().rng.next32() & 1u) != 0 ? kOrderSauce : kOrderDrink;
      OrderInfo info;
      info.type = type;
      addOrder(info);
      continue;
    }
    OrderInfo info;
    info.type = kOrderChocolate;
    const std::shared_ptr<VChocolate> choc = vendor->getChocolate(info, false);
    if (!choc) continue;
    unmakeable = unmakeable || !vendor->canCreate(*choc);
    if (unmakeable) {
      if (count == 1) count = 2;
      if (i == 0) firstUnmakeable = true;
    }
    orders_.push_back(choc);
  }
  if (firstUnmakeable && orders_.size() > 1) {
    const std::shared_ptr<VCarriable> first = orders_.front();
    orders_.pop_front();
    orders_.push_back(first);
  }
}

void VCustomer::detachThought() {
  if (!bubble_) return;
  removeChild(*bubble_);
  bubble_->clear();
}

void VCustomer::attachThought() {
  if (!hasMoreOrders() || !bubble_) return;
  const std::shared_ptr<VChocolateVendor> vendor = host_ ? host_->chocolateVendor() : nullptr;
  detachThought();
  addChild(bubble_);
  bubble_->attachToAnchor("bubble_single");
  for (const std::shared_ptr<VCarriable>& item : orders_) {
    const std::string& type = item->type();
    if (type == "chocolate") {
      const auto choc = std::dynamic_pointer_cast<VChocolate>(item);
      const bool makeable = vendor && choc && vendor->canCreate(*choc);
      bubble_->addOrder(item, "chocolate", makeable);
    } else if (type == "sauce") {
      bubble_->addOrder(item, "sauce", host_ && host_->upgradeActive("kSauce1", "station"));
    } else if (type == "drink") {
      bubble_->addOrder(item, "chocolate", host_ && host_->upgradeActive("kDrink1", "station"));
    }
  }
  if (orders_.empty()) {
    wanted_.reset();
  } else {
    wanted_ = orders_.front();
    orders_.pop_front();
  }
  bubble_->setMood(mood_);
}

bool VCustomer::serveCustomer(const std::shared_ptr<VCarriable>& item) {
  if (!item || !wanted_ || item->type() != wanted_->type()) return false;
  GreatCCWorld* w = worldOf(context());
  if (wanted_->type() == "chocolate") {
    const auto given = std::dynamic_pointer_cast<VChocolate>(item);
    const auto asked = std::dynamic_pointer_cast<VChocolate>(wanted_);
    if (!given || !asked || !given->compare(asked.get())) return false;
    price_ += given->cost();
  } else if (wanted_->type() == "sauce") {
    if (w) {
      w->values().addToValue("num_sauce_sold", 1.0, 0, true);
      price_ += static_cast<float>(w->values().getReal("sauce_price", 0));
    }
  } else if (item->type() == "drink") {
    if (w) price_ += static_cast<float>(w->values().getReal("drink_price", 0));
  } else {
    return false;
  }
  if (host_) host_->addCombo(kComboService);
  patience_ += static_cast<float>(valueReal(context(), "order_patience_bonus", 0));
  if (orders_.empty()) {
    wanted_.reset();
  } else {
    wanted_ = orders_.front();
    orders_.pop_front();
  }
  if (bubble_) bubble_->nextBubble();
  playSfx(context(), "audio/sfx/customer_leaves.ogg");
  if (w) w->gameStates().set("++OrderCompleted");
  return true;
}

bool VCustomer::complimentaryService(const std::shared_ptr<VCarriable>& item) {
  if (!item || state_ < 1 || state_ > 3) return false;
  patience_ += static_cast<float>(valueReal(context(), "complimentary_patience_bonus", 0));
  playSfx(context(), "audio/sfx/chomp.ogg");
  return true;
}

// ---- leaving ----

bool VCustomer::shouldDelete() const {
  return (!fade_ || !fade_->inProgress()) && (!moneyFade_ || !moneyFade_->inProgress()) && state_ == kCustomerDone;
}

void VCustomer::payoutMoney(const std::shared_ptr<VStation>& station) {
  GreatCCWorld* w = worldOf(context());
  (void)w;
  if (!money_ || !station) {
    return;
  }
  money_->removeChildren();
  money_->switchState("dollar", "default");
  const int extra = payoutDollarCount(price_);
  int delay = 100;
  for (int d = 1; d <= extra; ++d, delay += 100) {
    std::shared_ptr<VObject> leaf = money_->addAssetLeaf("money", "dollar", 0);
    if (!leaf) continue;
    leaf->setTint(money_->tint());
    float x = static_cast<float>(context().rng.next32() & 3u) + 2.0f;
    if ((context().rng.next32() & 1u) == 0) x = -x;
    leaf->setPosition({x, -static_cast<float>(d)});
    leaf->setFlip(((context().rng.next32() ^ 1u) & 1u) != 0, false);
    leaf->addEffect(std::make_shared<AlphaEffect>(true, 1.0f, 300, -1.0f, delay));
    leaf->play("", false, false, delay);
  }
  station->addChild(money_);
  money_->setPosition(station->money());
  money_->play("", false, false, 0);
}

void VCustomer::payout() {
  const std::shared_ptr<VSprite> keep = shared_from_this();
  const bool served = state_ == kCustomerServed && !hasMoreOrders();
  if (state_ != kCustomerServed && state_ != kCustomerAngry) return;
  GreatCCWorld* w = worldOf(context());
  if (price_ <= 0.0f) {
    state_ = kCustomerDone;
  } else {
    payoutMoney(station());
    state_ = kCustomerMoney;
    moneyStamp_ = static_cast<int>(clockNow());
  }
  if (!fade_) {
    fade_ = std::make_shared<AlphaEffect>(false, 1.0f, valueInt(context(), "customer_fade_time", 500), -1.0f, 0);
    addEffect(fade_);
  }
  playSfx(context(), "audio/sfx/customer_leaves.ogg");
  if (!w) return;
  const bool endless = host_ && host_->isEndless();
  if (served) {
    if (!endless) w->values().addToValue("num_served", 1.0, 0, true);
    w->values().addToValue("served_this_level", 1.0, 0, false);
  } else {
    w->values().addToValue("angry_this_level", 1.0, 0, false);
    if (endless) return;
    w->values().addToValue("num_angry", 1.0, 0, true);
    w->gameStates().set("++TotalCustomersLost");
  }
}

}  // namespace choco::games::greatcc
