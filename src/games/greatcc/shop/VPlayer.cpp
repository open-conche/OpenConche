#include "games/greatcc/shop/VPlayer.h"

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/shop/VChocolate.h"
#include "games/greatcc/shop/VStation.h"

namespace choco::games::greatcc {

VPlayer::VPlayer(script::ScriptContext& ctx, std::shared_ptr<ZoneGraph> zones) : VCharacter(ctx, std::move(zones)) {
  setType("player_character");
  setMouse(false);  // the avatar is not clickable
}

void VPlayer::loadFromTable(const StyleChain& style) {
  detachObject(kHandA);
  detachObject(kHandB);
  baseY_ = static_cast<float>(style.numberOr("baseY", 0.0, 0));
  rangeY_ = static_cast<float>(style.numberOr("rangeY", 1.0, 0));
  scaleRange_ = static_cast<float>(style.numberOr("scaleRange", 0.0, 0));
  speed_ = static_cast<float>(valueReal("walkspeed", 5.0));
  VCharacter::loadFromTable(style);
  setDelay(30, true);
}

// ---- update ----

void VPlayer::update(bool softPaused) {
  const std::shared_ptr<VSprite> keep = weak_from_this().lock();
  if (turnAround_ > 0) {
    turnAround_ -= elapsedMs();
    // half a second after she stops she faces front
    if (turnAround_ < 1 && !isBusy()) setDirectionFacing(2);
  }
  if (slotA_ && dropCheck_ && dropCheck_(*slotA_)) detachObject(kHandA);
  if (slotB_ && dropCheck_ && dropCheck_(*slotB_)) detachObject(kHandB);
  VCharacter::update(softPaused);
}

// ---- the walk ----

void VPlayer::travelTo(ZonePoint* p) {
  if (!zones_) return;
  finalDest_ = p;
  if (host_ && host_->hasCenterTable()) {
    zones_->addPath(node_, p);
    setDestination(zones_->nextPoint());
  } else {
    setDestination(p);
  }
}

void VPlayer::atDestination() {
  // the station's arrival may reload the level or take the avatar out of the scene; this call finishes on a live object
  const std::shared_ptr<VSprite> keep = weak_from_this().lock();
  if (host_) {
    if (const std::shared_ptr<VStation> station = host_->stationByZonePoint(finalDest_)) station->onArrival(*this, finalDest_);
  }
  // an avatar that left the scene (a new level replaced it) does not walk on to the next queued point
  if (!manager()) return;
  VCharacter::atDestination();
}

void VPlayer::updatePosition() {
  speed_ = static_cast<float>(valueReal("walkspeed", 5.0));
  VCharacter::updatePosition();
  scale_ = (position().y - baseY_) / rangeY_ * scaleRange_ + 1.0f;
  setScale(scale_, scale_);
  positionCarriables();
}

// ---- facing and the body parts ----

void VPlayer::setFacing(int f) {
  if (facing_ != f) {
    facing_ = f;
    std::string asset = "walkfront";
    bool mirror = false;
    if (f == 1) asset = "walkright";
    else if (f == 0) asset = "walkright", mirror = true;
    else if (f == 3) asset = "walkback", mirror = true;
    switchAsset(asset, "default", "default");
    setFlip(mirror, false);
    // the arm that holds slot A is the right one, unless the figure is mirrored
    const bool carryLeft = (slotB_ && !mirror) || (slotA_ && mirror);
    const bool carryRight = (slotA_ && !mirror) || (slotB_ && mirror);
    for (const auto& [key, child] : childObjects()) {
      if (!child || isIdleName(key)) continue;
      if (key == "left_arm" || key == "left_arm_upper") child->switchAsset(asset, carryLeft ? "carry" : "default", "default");
      else if (key == "right_arm" || key == "right_arm_upper") child->switchAsset(asset, carryRight ? "carry" : "default", "default");
      else child->switchAsset(asset, "default", "default");
    }
    positionCarriables();
  }
  if (!moving_) {
    stop(true);
  } else {
    play("", true, false, 0);
    for (const auto& [key, child] : childObjects())
      if (child && !isIdleName(key)) child->play("", true, false, 0);
  }
  turnAround_ = facing_ == 2 ? 0 : valueInt("player_turnaround_cooldown", 1000);
  if (idle_) stopCurrentIdle();
}

// While she idles only the idle child shows: the body parts hide, and come back with the walk.
void VPlayer::playRandomIdleAnim() {
  if (numHandsFree() < 2) {
    setDirectionFacing(2);
    return;
  }
  VCharacter::playRandomIdleAnim();
  if (!idle_) return;
  for (const auto& [key, child] : childObjects())
    if (child && !isIdleName(key)) child->setVisible(false);
}

void VPlayer::stopCurrentIdle() {
  VCharacter::stopCurrentIdle();
  for (const auto& [key, child] : childObjects())
    if (child && !isIdleName(key)) child->setVisible(true);
}

// ---- hands ----

bool VPlayer::attachObject(const std::shared_ptr<VCarriable>& item) {
  if (!item || item->hands() > numHandsFree()) return false;
  if (item->hands() == 2) {
    slotB_ = item;
    slotA_ = slotB_;
    item->attachToAnchor("bucket");
  } else if (!slotA_) {
    slotA_ = item;
  } else if (!slotB_) {
    slotB_ = item;
  }
  const int f = facing_;
  facing_ = -1;
  setFacing(f);
  return true;
}

std::shared_ptr<VCarriable> VPlayer::getObject(int hand, int* outHand) {
  if (hand == kHandPreferred) {
    // the preferred hand first; an empty one turns the preference and the ask is made again
    std::shared_ptr<VCarriable> item = getObject(preferred_, outHand);
    return item ? item : getObject(preferred_, outHand);
  }
  if ((hand == kHandA || hand == kHandA2) && slotA_) {
    if (outHand) *outHand = 2;
    return slotA_;
  }
  // the hand is unsigned in the original, so -1 lands here too: the preference turns and nothing is taken
  if (static_cast<unsigned>(hand) > 1 || !slotB_) {
    preferred_ = preferred_ == 2 ? 1 : 2;
    return nullptr;
  }
  if (outHand) *outHand = 1;
  return slotB_;
}

std::shared_ptr<VChocolate> VPlayer::getChocolate(int hand, int* outHand) {
  return std::dynamic_pointer_cast<VChocolate>(getObject(hand, outHand));
}

std::shared_ptr<VCarriable> VPlayer::detachObject(int hand) {
  auto leaveArms = [&](VCarriable& item) {
    for (const char* arm : {"left_arm", "right_arm"})
      if (VObject* a = childObject(arm)) a->removeChild(item);
    item.setScale(1, 1);
  };
  auto refreshFacing = [&] {
    const int f = facing_;
    facing_ = -1;
    setFacing(f);
  };
  if (hand == kHandPreferred) {
    // asking for a hand that holds nothing turns the preference, so the second ask tries the other hand
    std::shared_ptr<VCarriable> item = detachObject(preferred_);
    return item ? item : detachObject(preferred_);
  }
  if ((hand == kHandA || hand == kHandA2) && slotA_) {
    preferred_ = 1;
    std::shared_ptr<VCarriable> item = slotA_;
    leaveArms(*item);
    if (slotB_ == slotA_) slotB_.reset();
    slotA_.reset();
    refreshFacing();
    return item;
  }
  // the hand is unsigned in the original, so -1 lands here too: the preference turns and nothing is taken
  if (static_cast<unsigned>(hand) > 1 || !slotB_) {
    preferred_ = preferred_ == 2 ? 1 : 2;
    return nullptr;
  }
  preferred_ = 2;
  std::shared_ptr<VCarriable> item = slotB_;
  leaveArms(*item);
  slotB_.reset();
  refreshFacing();
  return item;
}

void VPlayer::positionCarriables() {
  const gfx::PointF pos = position();
  // undoes the avatar's perspective scale, so a held thing keeps its size
  const float s = (1.0f - (scale_ - 1.0f)) - (((baseY_ + rangeY_) - pos.y) / rangeY_) * scaleRange_;
  if (slotA_ && slotA_ == slotB_) return;  // a two-hand item is placed once, by whoever attached it
  const bool mirrored = facing_ == 0 || facing_ == 3;
  auto place = [&](const std::shared_ptr<VCarriable>& item, const char* firstArm, const char* firstAnchor, const char* otherArm,
                   const char* otherAnchor) {
    // the first choice when the figure is mirrored, the other arm when it is not; the player itself when an arm is missing
    VSprite* holder = this;
    std::string anchor = otherAnchor;
    VObject* arm = nullptr;
    if (mirrored) {
      arm = childObject(firstArm);
      if (arm) anchor = firstAnchor;
    }
    if (!arm) arm = childObject(otherArm);
    if (arm) holder = arm;
    if (VSprite* parent = item->parent()) parent->removeChild(*item);
    holder->addChild(item);
    item->attachToAnchor(anchor);
    item->setScale(s, s);
  };
  if (slotA_) place(slotA_, "left_arm", "lefthand", "right_arm", "righthand");
  if (slotB_) place(slotB_, "right_arm", "righthand", "left_arm", "lefthand");
}

}  // namespace choco::games::greatcc
