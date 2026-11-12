#include "games/greatcc/shop/VInfuse.h"

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/scene/AnimScriptNatives.h"
#include "games/greatcc/shop/VPlayer.h"

namespace choco::games::greatcc {

void VInfuse::addCallbacks(const char* childKey, const char* globalName, void (VInfuse::*fn)()) {
  std::shared_ptr<VObject> target = childShared(childKey);
  if (!target) return;
  // see VPress::addCallbacks: the script holds the machine weakly
  std::weak_ptr<VSprite> self = shared_from_this();
  std::string name = globalName;
  target->setScriptSetup([self, name, fn](sdk::SpriteScript& script) {
    addMachineCallback(script, name, [self, fn] {
      if (auto s = self.lock())
        if (auto* machine = dynamic_cast<VInfuse*>(s.get())) (machine->*fn)();
    });
  });
}

void VInfuse::init() {
  initSelections("kInfusion", "kInfusionQ", nullptr);
  finishInit();
  addCallbacks("anim_top_working", "unlock", &VInfuse::unlockBar);
  addCallbacks("anim_door", "drop", &VInfuse::dropBarrel);
}

void VInfuse::reset() {
  if (fillLayer_) removeChild(*fillLayer_);
  fillLayer_.reset();
  fillIcon_.reset();
  setCurrentFrame(1);
  VMachine::reset();
}

bool VInfuse::processChocolate() {
  if (suspended() || !chocolate_ || !chocolate_->canAddLayer(kLayerInfusion) || option_ < 0) return false;
  fillLayer_ = VChocolateLayer::create(context(), 0, -1, -1);
  if (const auto& bucket = chocolate_->layerAt(kLayerBucket)) bucket->copyTo(*fillLayer_);
  addChild(fillLayer_);
  fillLayer_->setLayer(2);
  fillLayer_->attachToAnchor("bucket");
  fillIcon_ = fillLayer_->childShared("icon");
  chocolate_->addLayer(kShapeInfusion, kLayerInfusion, option_);
  chocolate_->setVisible(false);
  removeChild(*chocolate_);
  if (VObject* top = child("anim_top_working")) top->addChild(chocolate_);
  chocolate_->attachToAnchor("chocolate");
  activate();
  if (GreatCCWorld* w = worldOf(context())) w->values().addToValue("chocolates_made_this_level", 1.0, 0, false);
  return true;
}

void VInfuse::activate() {
  VMachine::activate();
  showChild("static_base", true);
  showChild("static_top", true);
  if (fillIcon_) fillIcon_->addEffect(std::make_shared<WipeEffect>(0, false, 300));
  suspend(300);
  playSfx("audio/sfx/infusion_machine.ogg");
}

bool VInfuse::onResume() {
  if (state_ < 1) return true;
  if (state_ == 1) {
    state_ = 2;
    setCurrentFrame(0);
    showChild("static_top", false);
    showChild("static_base", false);
    showChild("anim_top_working", true);
    playChild("anim_top_working", forward_);
    showChild("anim_base", true);
    playChild("anim_base", forward_);
    showChild("anim_door", true);
    playChild("anim_door", "DoDoor");
    suspendWhilePlaying(childShared("anim_base"));
    return false;
  }
  if (processed_ && chocolate_) {
    if (VObject* top = child("anim_top_working")) top->removeChild(*chocolate_);
    addChild(chocolate_);
    chocolate_->attachToAnchor("chocolate");
  }
  if (GreatCCWorld* w = worldOf(context())) w->values().addToValue("num_infusions", 1.0, 0, true);
  reset();
  return !suspended();
}

void VInfuse::unlockBar() {
  VMachine::unlock();
  if (chocolate_) chocolate_->setVisible(true);
}

void VInfuse::dropBarrel() {
  if (fillLayer_) {
    fillLayer_->addEffect(std::make_shared<SpriteMove>(gfx::PointF{fillLayer_->position().x, fillLayer_->position().y + 46.0f}, 200, 0.0f, 0));
    fillLayer_->addEffect(std::make_shared<WipeEffect>(1, false, 200));
  }
  playSfx("audio/sfx/barrel_trap_door.ogg");
}

void VInfuse::onArrival(VPlayer& player) {
  if (!locked_) {
    if (processChocolate()) return;
    int hand = -1;
    std::shared_ptr<VChocolate> carried = player.getChocolate(VPlayer::kHandPreferred, &hand);
    if (!carried || !carried->canAddLayer(kLayerInfusion)) carried = player.getChocolate(hand != 2 ? 2 : 1, &hand);
    if ((!chocolate_ || processed_) && carried && carried->canAddLayer(kLayerInfusion)) {
      const std::shared_ptr<VChocolate> output = detachChocolate(true);
      player.detachObject(hand);
      attachChocolate(carried, 2, true);
      carried->attachToAnchor("bucket");
      player.attachObject(output);
      processChocolate();
      return;
    }
  }
  option_ = -1;
  VMachine::onArrival(player);
}

}  // namespace choco::games::greatcc
