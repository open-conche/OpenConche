#include "games/greatcc/shop/VPress.h"

#include <cstdio>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/scene/AnimScriptNatives.h"
#include "games/greatcc/shop/VPlayer.h"

namespace choco::games::greatcc {

VPress::VPress(script::ScriptContext& ctx) : VMachine(ctx) {}

void VPress::addCallbacks(const char* childKey, const char* globalName, void (VPress::*fn)()) {
  std::shared_ptr<VObject> target = childShared(childKey);
  if (!target) return;
  // The callback runs inside the child's own script, which the machine owns; it holds the machine weakly so a machine that has
  // left the scene (a reloaded level) is left alone.
  std::weak_ptr<VSprite> self = shared_from_this();
  std::string name = globalName;
  target->setScriptSetup([self, name, fn](sdk::SpriteScript& script) {
    addMachineCallback(script, name, [self, fn] {
      if (auto s = self.lock())
        if (auto* press = dynamic_cast<VPress*>(s.get())) (press->*fn)();
    });
  });
}

void VPress::init() {
  initSelections("kPress", nullptr, nullptr);
  finishInit();
  addCallbacks("anim_largeweight", "unlock", &VPress::unlockStamp);
  addCallbacks("anim_door", "drop", &VPress::dropBarrel);
}

void VPress::reset() {
  fillAnim_ = "DoAnim_0";
  shape_ = -1;
  if (fillLayer_) removeChild(*fillLayer_);
  fillLayer_.reset();
  fillIcon_.reset();
  setCurrentFrame(3);
  VMachine::reset();
}

bool VPress::processChocolate() {
  if (suspended() || !chocolate_ || !chocolate_->canAddLayer(kLayerChocolate) || option_ < 0) return false;
  shape_ = option_;
  fillLayer_ = VChocolateLayer::create(context(), 0, -1, -1);
  fillLayer_->setLayer(1);
  // the barrel as it was, copied from the bucket (this also takes the bucket's own sprite layer)
  chocolate_->layerAt(kLayerBucket)->copyTo(*fillLayer_);
  addChild(fillLayer_);
  fillLayer_->attachToAnchor("bucket");
  fillIcon_ = fillLayer_->childShared("icon");
  char name[24];
  std::snprintf(name, sizeof name, "DoAnim_%d", chocolate_->mixOption());
  fillAnim_ = name;
  // the shape layer goes on now; the bar stays hidden until the weight has stamped
  chocolate_->addLayer(shape_, kLayerChocolate, chocolate_->mixOption());
  chocolate_->setVisible(false);
  chocolate_->attachToAnchor("chocolate");
  activate();
  if (GreatCCWorld* w = worldOf(context())) w->values().addToValue("chocolates_made_this_level", 1.0, 0, false);
  return true;
}

void VPress::activate() {
  VMachine::activate();
  showChild("static_door", true);
  showChild("static_largeweight", true);
  showChild("static_wheel", true);
  if (fillIcon_) fillIcon_->addEffect(std::make_shared<WipeEffect>(0, false, 300));
  showChild("anim_fill", true);
  playChild("anim_fill", fillAnim_);
  suspend(300);
}

void VPress::unlockStamp() {
  VMachine::unlock();
  if (chocolate_) chocolate_->setVisible(true);
  showChild("anim_fill", false);
  playSfx("audio/sfx/stamp.ogg");
}

void VPress::dropBarrel() {
  if (fillLayer_) {
    fillLayer_->addEffect(std::make_shared<SpriteMove>(gfx::PointF{fillLayer_->position().x, fillLayer_->position().y + 46.0f}, 200, 0.0f, 0));
    fillLayer_->addEffect(std::make_shared<WipeEffect>(1, false, 200));
  }
  playSfx("audio/sfx/barrel_trap_door.ogg");
}

bool VPress::onResume() {
  if (state_ < 1) return true;
  if (state_ == 1) {
    state_ = 2;
    showChild("static_largeweight", false);
    showChild("static_wheel", false);
    showChild("static_door", false);
    showChild("anim_door", true);
    playChild("anim_door", "DoDoor");
    showChild("anim_largeweight", true);
    playChild("anim_largeweight", forward_);
    showChild("anim_wheel", true);
    playChild("anim_wheel", forward_);
    suspendWhilePlaying(childShared("anim_largeweight"));
    return false;
  }
  GreatCCWorld* w = worldOf(context());
  if (shape_ == kShapeTruffle) {
    if (w) w->values().addToValue("num_truffles", 1.0, 0, true);
  } else if (shape_ == kShapeBar) {
    if (w) w->values().addToValue("num_bars", 1.0, 0, true);
  }
  reset();
  return !suspended();
}

void VPress::update(bool softPaused) {
  VMachine::update(softPaused);
  if (state_ == 2) {
    const VObject* weight = child("anim_largeweight");
    setCurrentFrame(weight && weight->currentFrame() == 5 ? 4 : 3);
  }
}

void VPress::onArrival(VPlayer& player) {
  if (!locked_) {
    if (processChocolate()) return;
    int hand = -1;
    std::shared_ptr<VChocolate> carried = player.getChocolate(VPlayer::kHandPreferred, &hand);
    if (!carried || !carried->canAddLayer(kLayerChocolate)) carried = player.getChocolate(hand != 2 ? 2 : 1, &hand);
    if ((!chocolate_ || processed_) && carried && carried->canAddLayer(kLayerChocolate)) {
      // swap: the press's finished bar is taken out first, the bucket leaves her hand and goes in, the bar takes the free hand
      std::shared_ptr<VChocolate> output = detachChocolate(true);
      player.detachObject(hand);
      attachChocolate(carried, 1, true);
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
