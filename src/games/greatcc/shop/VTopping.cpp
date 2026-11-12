#include "games/greatcc/shop/VTopping.h"

#include "games/greatcc/shop/VPlayer.h"

namespace choco::games::greatcc {

void VTopping::init() {
  initSelections("kTopping", "kToppingQ", nullptr);
  finishInit();
}

void VTopping::reset() {
  if (VObject* particles = child("anim_particles")) particles->cancelEffects();
  layer_.reset();
  VMachine::reset();
}

bool VTopping::processChocolate() {
  if (!chocolate_ || !chocolate_->canAddLayer(kLayerTopping) || option_ < 0) return false;
  layer_ = chocolate_->addLayer(chocolate_->barType(), kLayerTopping, option_);
  activate();
  return true;
}

void VTopping::activate() {
  VMachine::activate();
  showChild("anim_funnel", true);
  playChild("anim_funnel", forward_);
  showChild("anim_particles", true);
  playChild("anim_particles", "DoParticles");
  if (VObject* particles = child("anim_particles")) particles->setAlpha(1.0f);
  suspendWhilePlaying(childShared("anim_particles"));
  playSfx("audio/sfx/topping_machine.ogg");
  playSfx("audio/sfx/topping.ogg");
  if (layer_) layer_->addEffect(std::make_shared<AlphaEffect>(true, 1.0f, 810, -1.0f, 0));
}

bool VTopping::onResume() {
  if (state_ > 0) {
    if (state_ == 1) {
      state_ = 2;
      if (VObject* particles = child("anim_particles")) particles->addEffect(std::make_shared<AlphaEffect>(false, 1.0f, 300, -1.0f, 0));
      suspend(300);
      return false;
    }
    processed_ = true;
    reset();
  }
  return true;
}

}  // namespace choco::games::greatcc
