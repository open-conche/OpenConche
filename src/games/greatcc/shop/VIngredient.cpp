#include "games/greatcc/shop/VIngredient.h"

#include <cstdio>

#include "games/greatcc/shop/VPlayer.h"

namespace choco::games::greatcc {

void VIngredient::init() {
  initSelections("kIngredient", "kIngredientQ", nullptr);
  for (int i = 1; i <= selectionCount_; ++i) {
    char name[40];
    std::snprintf(name, sizeof name, "kIngredient%d", i);
    if (!stationUpgrade(name)) continue;
    std::snprintf(name, sizeof name, "upgrade%d", i);
    for (const char* tank : {"static_top1", "static_top2", "anim_top1", "anim_top2"})
      if (VObject* o = child(tank)) o->switchAsset("anim", name, "default");
  }
  finishInit();
}

void VIngredient::reset() {
  overlay_.reset();
  VMachine::reset();
}

bool VIngredient::processChocolate() {
  if (suspended() || !chocolate_ || !chocolate_->canAddLayer(kLayerIngredient) || option_ < 0) return false;
  chocolate_->addIngredient(option_);
  const auto& bucket = chocolate_->layerAt(kLayerBucket);
  std::shared_ptr<VObject> icon = bucket ? bucket->childShared("icon") : nullptr;
  overlay_ = icon ? icon->childShared("overlay") : nullptr;
  activate();
  return true;
}

void VIngredient::activate() {
  VMachine::activate();
  for (const char* key : {"anim_intro", "anim_top1", "anim_top2"}) {
    showChild(key, true);
    playChild(key, forward_);
  }
  if (overlay_) overlay_->setVisible(false);
  suspendWhilePlaying(childShared("anim_intro"));
  playSfx("audio/sfx/machine_noise.ogg");
}

bool VIngredient::onResume() {
  if (state_ < 1) return true;
  if (state_ == 1) {
    state_ = 2;
    if (overlay_) {
      overlay_->setVisible(true);
      overlay_->addEffect(std::make_shared<AlphaEffect>(true, 1.0f, 1000, -1.0f, 0));
    }
    showChild("anim_intro", false);
    showChild("anim_pump", true);
    playChild("anim_pump", forward_, true);
    playSfx("audio/sfx/glug_machine.ogg");
    suspend(1000);
    return false;
  }
  if (state_ == 2) {
    state_ = 3;
    showChild("anim_intro", true);
    showChild("anim_pump", false);
    for (const char* key : {"anim_intro", "anim_top1", "anim_top2"}) playChild(key, reverse_);
    suspendWhilePlaying(childShared("anim_intro"));
    unlock();
    return false;
  }
  reset();
  return !suspended();
}

}  // namespace choco::games::greatcc
