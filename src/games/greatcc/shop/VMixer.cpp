#include "games/greatcc/shop/VMixer.h"

#include <cstdio>

#include "games/greatcc/shop/VPlayer.h"

namespace choco::games::greatcc {

namespace {
std::string optionState(int n) {
  char buf[24];
  std::snprintf(buf, sizeof buf, "option_%d", n);
  return buf;
}
// dark chocolate's tube and spray are drawn at half brightness
void tintForMix(VObject& o, int mix) {
  if (mix == 0) o.setTint({0.5f, 0.5f, 0.5f, 1.0f});
  else o.setTint({1.0f, 1.0f, 1.0f, 1.0f});
}
}  // namespace

VMixer::VMixer(script::ScriptContext& ctx) : VMachine(ctx) { frameDelay_ = 20.0f; }

void VMixer::init() {
  initSelections("kMixer", "kMixerQ", "tank");
  finishInit();
}

void VMixer::onArrival(VPlayer& player) {
  if (chocolate_ && processed_) VMachine::onArrival(player);
  if (!locked_ && !suspended()) processChocolate();
}

void VMixer::reset() {
  icon_.reset();
  VMachine::reset();
}

bool VMixer::processChocolate() {
  if (option_ > -1 && !chocolate_) {
    std::shared_ptr<VChocolateVendor> vendor = host_ ? host_->chocolateVendor() : nullptr;
    if (vendor) {
      std::shared_ptr<VChocolate> choc = vendor->getEmptyChocolate(option_, true);
      if (attachChocolate(choc, 2, true)) {
        choc->attachToAnchor("chocolate");
        const auto& bucket = choc->layerAt(kLayerBucket);
        icon_ = bucket ? bucket->childShared("icon") : nullptr;
        activate();
        return true;
      }
    }
  }
  return false;
}

void VMixer::activate() {
  // the tube and the spray take the mix of the choice (it is still set here); dark ones are dimmed
  if (VObject* tube = child("anim_tube")) {
    tube->switchState(optionState(option_), "default");
    tintForMix(*tube, option_);
  }
  if (VObject* spray = child("anim_spray")) {
    spray->switchState(optionState(option_), "default");
    tintForMix(*spray, option_);
  }
  VMachine::activate();
  if (VObject* arm = child("anim_arm")) {
    arm->setVisible(true);
    arm->play(forward_, false, false, 0);
    playSfx("audio/sfx/robot_arm.ogg");
    suspendWhilePlaying(std::static_pointer_cast<VSprite>(childShared("anim_arm")));
  } else {
    suspend(1);
  }
  if (icon_) {
    icon_->setVisible(false);
    // (the bucket's icon is dimmed for dark chocolate when `option` is 0, which Activate has just reset to -1)
    if (option_ == 0) icon_->setTint({0.5f, 0.5f, 0.5f, 1.0f});
  }
}

bool VMixer::onResume() {
  if (state_ < 1) return false;
  switch (state_) {
    case 1: {
      state_ = 2;
      playSfx("audio/sfx/spray.ogg");
      if (VObject* tube = child("anim_tube")) {
        tube->setVisible(true);
        tube->play("DoAnim", false, false, 0);
        suspendWhilePlaying(childShared("anim_tube"));
        return false;
      }
      suspend(1);
      return false;
    }
    case 2: {
      state_ = 3;
      if (VObject* spray = child("anim_spray")) {
        spray->setVisible(true);
        spray->play("DoAnim", true, false, 0);
      }
      if (icon_) {
        icon_->setVisible(true);
        icon_->addEffect(std::make_shared<WipeEffect>(1, true, 800));
      }
      suspend(800);
      return false;
    }
    case 3: {
      state_ = 4;
      unlock();
      if (VObject* spray = child("anim_spray")) {
        spray->stop(true);
        spray->setVisible(false);
      }
      if (VObject* tube = child("anim_tube")) {
        tube->play("Reverse", false, false, 0);
        suspendWhilePlaying(childShared("anim_tube"));
        return false;
      }
      suspend(1);
      return false;
    }
    case 4: {
      state_ = 5;
      playSfx("audio/sfx/robot_arm_2.ogg");
      showChild("anim_tube", false);
      if (VObject* arm = child("anim_arm")) {
        arm->play(reverse_, false, false, 0);
        suspendWhilePlaying(childShared("anim_arm"));
      }
      return false;
    }
    default:
      reset();
      return false;
  }
}

}  // namespace choco::games::greatcc
