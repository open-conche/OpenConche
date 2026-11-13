#include "games/greatcc/shop/VDisplay.h"

#include <cstdio>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/shop/VChocolate.h"
#include "games/greatcc/shop/VPlayer.h"
#include "games/greatcc/scene/SpriteManager.h"

namespace choco::games::greatcc {

// VDisplayListener: an item on the table that is clicked tells the table.
class VDisplay::Listener : public SpriteListener {
 public:
  explicit Listener(std::weak_ptr<VDisplay> display) : display_(std::move(display)) {}
  void onClick(VSprite& s) override {
    const auto item = std::dynamic_pointer_cast<VCarriable>(s.shared_from_this());
    if (auto d = display_.lock(); d && item) d->pickupCarriable(item);
  }

 private:
  std::weak_ptr<VDisplay> display_;
};

VDisplay::VDisplay(script::ScriptContext& ctx, int id) : VStation(ctx, id, kStationDisplay) {}

VDisplay::~VDisplay() {
  if (listener_) listener_->dead = true;
}

void VDisplay::loadFromTable(const StyleChain& style) {
  removeChildren();
  slots_.clear();
  slots_.resize(static_cast<std::size_t>(capacity_));
  count_ = 0;
  if (ShopHost* h = host())
    if (const auto vendor = h->chocolateVendor()) vendor->clearDisplayChocolates();
  VStation::loadFromTable(style);
}

void VDisplay::init() {
  ShopHost* h = host();
  const bool active = h && h->upgradeActive("kDisplay1", "station");
  if (active) switchAsset("base", "default", "default");
  setVisible(active);
  if (listener_) listener_->dead = true;
  listener_ = std::make_shared<Listener>(std::static_pointer_cast<VDisplay>(shared_from_this()));
  capacity_ = h && h->upgradeActive("kDisplay2", "station") ? 2 : 1;
  slots_.resize(static_cast<std::size_t>(capacity_));
}

void VDisplay::onClick() {
  if (!picked_ && slots_.size() == 1) picked_ = slots_[0];
  VStation::onClick();
}

void VDisplay::update(bool softPaused) {
  if (!softPaused) {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
      const std::shared_ptr<VCarriable> item = slots_[i];
      if (!item) continue;
      const auto choc = std::dynamic_pointer_cast<VChocolate>(item);
      if (choc && choc->hasDecayed()) detachCarriable(item);
    }
  }
  VStation::update(softPaused);
}

void VDisplay::onArrival(VPlayer& player, const ZonePoint* point) {
  const std::shared_ptr<VSprite> keep = shared_from_this();
  VStation::onArrival(player, point);
  if (picked_ && picked_->hands() <= player.numHandsFree()) {
    const std::shared_ptr<VCarriable> item = picked_;
    detachCarriable(item);
    player.attachObject(item);
  } else if (capacity_ > count_) {
    int hand = -1;
    if (const std::shared_ptr<VCarriable> carried = player.getObject(VPlayer::kHandPreferred, &hand)) {
      player.detachObject(hand);
      attachCarriable(carried);
      if (host() && host()->tutorialInEffect() && manager()) manager()->addExclusiveSprite(carried);
    }
  }
  picked_.reset();
}

void VDisplay::pickupCarriable(const std::shared_ptr<VCarriable>& item) {
  const std::shared_ptr<VSprite> keep = shared_from_this();
  picked_ = item;
  doEvent(SpriteEvent::Click, {});
}

void VDisplay::attachCarriable(const std::shared_ptr<VCarriable>& item) {
  if (!item || count_ >= capacity_) return;
  std::size_t slot = 0;
  while (slot < slots_.size() && slots_[slot]) ++slot;
  if (slot >= slots_.size()) return;
  slots_[slot] = item;
  addChild(item);
  ++count_;
  char anchor[32];
  std::snprintf(anchor, sizeof anchor, "chocolate%zu", slot + 1);
  item->attachToAnchor(anchor);
  item->setLeaf(false);
  item->setClickable(true);
  item->setLayer(static_cast<int>(slots_.size()));
  if (listener_) item->addListener(listener_);
  item->addEffect(std::make_shared<HoverGlow>(0.25f, true, true, true));
  if (ShopHost* h = host())
    if (const auto vendor = h->chocolateVendor())
      if (const auto choc = std::dynamic_pointer_cast<VChocolate>(item)) vendor->addDisplayChocolate(*choc);
  if (GreatCCWorld* w = worldOf(context())) w->gameStates().set("++DisplayUsed");
}

void VDisplay::detachCarriable(const std::shared_ptr<VCarriable>& item) {
  if (!item) return;
  item->setLeaf(true);
  item->setClickable(false);
  if (listener_) item->removeListener(listener_);
  item->cancelEffects();
  if (ShopHost* h = host())
    if (const auto vendor = h->chocolateVendor())
      if (const auto choc = std::dynamic_pointer_cast<VChocolate>(item)) vendor->removeDisplayChocolate(*choc);
  for (auto& slot : slots_)
    if (slot == item) {
      slot.reset();
      --count_;
      break;
    }
  removeChild(*item);
}

}  // namespace choco::games::greatcc
