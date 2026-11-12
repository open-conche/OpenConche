#include "games/greatcc/shop/VDelivery.h"

#include <cstdlib>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/shop/VPlayer.h"

namespace choco::games::greatcc {

VDelivery::VDelivery(script::ScriptContext& ctx) : VMachine(ctx) {}

void VDelivery::loadFromTable(const StyleChain& style) {
  if (!zeppelin_) zeppelin_ = BZeppelin::create(context());
  if (host_) host_->addSceneSprite(zeppelin_);
  if (!bubble_) {
    bubble_ = VThoughtBubble::create(context());
    if (auto t = style.table("bubble", 0)) {
      const StyleChain bc(*t, &style);
      bubble_->loadFromTable(bc);
    }
    addChild(bubble_);
  }
  VMachine::loadFromTable(style);
}

void VDelivery::init() {
  enabled_ = stationUpgrade("kDelivery1");
  if (enabled_ || (host_ && host_->isEndless())) {
    if (!initialised_) {
      orders_.clear();
      wanted_.reset();
      if (fade_) {
        fade_->destroy();
        fade_.reset();
      }
      if (zeppelin_) zeppelin_->init();
      if (bubble_) bubble_->init();
      if (!(host_ && host_->isEndless()) && host_) {
        if (const std::vector<OrderInfo>* level = host_->zeppelinOrders())
          for (const OrderInfo& o : *level) orders_.push_back(o);
      }
    }
    VMachine::init();
  }
  setVisible(enabled_);
}

void VDelivery::reset() {
  VMachine::reset();
  setCurrentFrame(1);
  if (bubble_ && (!fade_ || !fade_->inProgress())) {
    bubble_->setVisible(false);
    bubble_->clear();
  }
  wanted_.reset();
  patience_ = 100.0f;
  setMood(1);
  deliveryState_ = kWaitingForOrder;
  GreatCCWorld* w = worldOf(context());
  if (!w) return;
  const unsigned variance = static_cast<unsigned>(w->values().getInt("delivery_variance", 2));
  const unsigned draw = context().rng.next32();
  const int delta = variance == 0 ? 0 : static_cast<int>(-static_cast<int>(variance) / 2 + static_cast<int>(draw % variance));
  timer_ = static_cast<int>(w->values().getSum("delivery_interval", static_cast<float>(delta), true));
}

void VDelivery::setMood(int mood) {
  mood_ = mood;
  if (bubble_) bubble_->setMood(mood);
}

void VDelivery::generateOrder() {
  const std::shared_ptr<VChocolateVendor> vendor = host_ ? host_->chocolateVendor() : nullptr;
  if (wanted_ || !vendor) return;
  if (orders_.empty()) {
    OrderInfo random;
    random.type = kOrderChocolate;
    wanted_ = vendor->getChocolate(random, true);
  } else {
    wanted_ = vendor->getChocolate(orders_.front(), false);
    orders_.pop_front();
  }
}

void VDelivery::attachThought(bool fadeIn) {
  if (!bubble_) return;
  float start = fadeIn ? 0.0f : 1.0f;
  if (fade_ && fade_->inProgress()) start = fade_->alpha();
  fade_ = std::make_shared<AlphaEffect>(fadeIn, 1.0f, 300, start, 0);
  if (fadeIn) {
    bubble_->setVisible(true);
    bubble_->clear();
    if (wanted_) {
      wanted_->setFlip(true, false);
      bubble_->addOrder(wanted_, "chocolate", true);
    }
    if (GreatCCWorld* w = worldOf(context())) w->tutorials().checkTutorial("tutorial_zepplin");
  }
  bubble_->cancelEffects();
  bubble_->addEffect(fade_);
}

void VDelivery::update(bool softPaused) {
  if (!enabled_) {
    VMachine::update(softPaused);
    return;
  }
  const std::shared_ptr<VSprite> keep = shared_from_this();
  if (state_ == 1 && chocolate_ && chocolate_->visible()) {
    if (const VObject* back = child("anim_boxing_back"); back && back->currentFrame() > 0x39) chocolate_->setVisible(false);
  }
  if (!softPaused && deliveryState_ == kWaitingForOrder && timer_ > 0) {
    timer_ -= static_cast<int>(getElapsed());
    if (timer_ < 1) {
      timer_ = 0;
      if (!(host_ && host_->levelIsComplete())) generateOrder();
      if (!(host_ && host_->levelIsComplete()) && wanted_ && bubble_) {
        attachThought(true);
        setMood(1);
        deliveryState_ = kOrderShowing;
      } else {
        reset();
      }
    }
  }
  if (fade_ && !fade_->inProgress()) {
    if (!fade_->fadeIn() && bubble_) {
      bubble_->setVisible(false);
      bubble_->clear();
    }
    fade_.reset();
  }
  if (deliveryState_ == kOrderShowing) {
    float p = patience_;
    if (p > 0.0f) {
      const float next = p - static_cast<float>(static_cast<unsigned>(getElapsed())) * 0.001f;
      p = next > 0.0f ? next : 0.0f;
      patience_ = p;
    }
    // the mood follows the patience (> 80 is 1, then 2 to 5 by the steps of 20); with none left the order lapses
    int mood = 1;
    if (p <= 80.0f) mood = 2;
    if (p <= 60.0f) mood = 3;
    if (p <= 40.0f) mood = 4;
    if (p <= 20.0f) mood = 5;
    if (p <= 0.0f) {
      attachThought(false);
      reset();
    } else if (mood_ != mood) {
      setMood(mood);
    }
  }
  if (host_) host_->waitOnZeppelin((zeppelin_ && zeppelin_->state() != 0) || deliveryState_ != kWaitingForOrder);
  VMachine::update(softPaused);
}

void VDelivery::onArrival(VPlayer& player) {
  if (suspended() || chocolate_ || deliveryState_ != kOrderShowing || !wanted_) return;
  std::shared_ptr<VChocolate> taken;
  const std::shared_ptr<VChocolate> a = player.getChocolate(VPlayer::kHandA);
  if (a && wanted_->compare(a.get())) {
    player.detachObject(VPlayer::kHandA);
    taken = a;
  } else {
    const std::shared_ptr<VChocolate> b = player.getChocolate(VPlayer::kHandB);
    if (b && wanted_->compare(b.get())) {
      player.detachObject(VPlayer::kHandB);
      taken = b;
    }
  }
  if (!taken) return;
  GreatCCWorld* w = worldOf(context());
  if (w) w->values().addToValue("zeppelin_served_this_level", 1.0, 0, false);
  if (taken->barType() == kShapeBar || taken->barType() == kShapeNovelty) taken->setScale(0.8f, 0.8f);
  attachChocolate(taken, 1, true);
  deliveryState_ = kBoxing;
  attachThought(false);
  activate();
  if (host_) host_->orderComplete(taken->cost(), 622.0f, 246.0f, nullptr);
}

void VDelivery::activate() {
  VMachine::activate();
  showChild("anim_boxing_front", true);
  playChild("anim_boxing_front", "DoFront");
  showChild("anim_boxing_back", true);
  playChild("anim_boxing_back", "DoAnim");
  suspendWhilePlaying(childShared("anim_boxing_back"));
  if (zeppelin_) zeppelin_->pickup();
  playSfx("audio/sfx/box_station.ogg");
}

bool VDelivery::onResume() {
  if (state_ > 0) {
    processed_ = true;
    setCurrentFrame(2);
    showChild("anim_boxing_front", false);
    showChild("anim_boxing_back", false);
    detachChocolate(true);
    if (!zeppelin_) {
      reset();
    } else {
      zeppelin_->readyForPickup(std::static_pointer_cast<VMachine>(shared_from_this()));
      deliveryState_ = kBoxed;
      if (GreatCCWorld* w = worldOf(context())) w->values().addToValue("num_deliveries", 1.0, 0, true);
    }
  }
  return true;
}

}  // namespace choco::games::greatcc
