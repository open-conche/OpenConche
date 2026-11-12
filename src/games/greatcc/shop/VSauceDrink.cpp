#include "games/greatcc/shop/VSauceDrink.h"

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/shop/VPlayer.h"

namespace choco::games::greatcc {

namespace {
constexpr double kDefaultCooldownMs = 5000;
void frameOf(VObject* o, int f) {
  if (o) o->setCurrentFrame(f);
}
}  // namespace

VSauceDrink::VSauceDrink(script::ScriptContext& ctx, bool drink) : VMachine(ctx), drink_(drink) {
  total_ = cooldownTotalNow();
  addEffect(std::make_shared<HoverGlow>(0.15f, true, true, true));  // VSauce::Create and VDrink::Create
}

float VSauceDrink::cooldownTotalNow() const {
  GreatCCWorld* w = worldOf(context());
  return static_cast<float>(w ? w->values().getReal(cooldownValue(), kDefaultCooldownMs) : kDefaultCooldownMs);
}

void VSauceDrink::init() {
  if (drink_) {
    enabled_ = stationUpgrade("kDrink1");
  } else if (name() == "machine_sauce_2") {
    const bool sauce3 = stationUpgrade("kSauce3");
    enabled_ = sauce3 && !stationUpgrade("kDrink1");
  } else {
    enabled_ = stationUpgrade("kSauce1");
  }
  if (enabled_ || (host_ && host_->isEndless())) {
    if (!initialised_) {
      cooldown_ = 0;
      attachItem();
      if (bottle_) bottle_->setVisible(true);
    }
    VMachine::init();
  }
  setVisible(enabled_);
}

bool VSauceDrink::attachItem() {
  if (bottle_) return false;
  VObject* tmpl = child("bottle");
  if (!tmpl) return false;
  bottle_ = VCarriable::create(context(), 1);
  tmpl->copyTo(*bottle_);
  bottle_->init();
  addChild(bottle_);
  return true;
}

void VSauceDrink::refill() {
  if (suspended()) return;
  if (!attachItem()) return;
  if (bottle_) bottle_->setVisible(false);
  activate();
}

void VSauceDrink::onArrival(VPlayer& player) {
  if (locked_ || !bottle_) return;
  const bool room = drink_ ? bottle_->hands() <= player.numHandsFree() : player.numHandsFree() > 0;
  if (!room) return;
  const std::shared_ptr<VCarriable> taken = bottle_;
  removeChild(*taken);
  player.attachObject(taken);
  bottle_.reset();
  GreatCCWorld* w = worldOf(context());
  cooldown_ = w ? w->values().getInt(cooldownValue(), static_cast<int>(kDefaultCooldownMs)) : static_cast<int>(kDefaultCooldownMs);
  total_ = cooldownTotalNow();
  showChild("static_fill", false);
  showChild("anim_fill", true);
}

void VSauceDrink::update(bool softPaused) {
  if (cooldown_ > 0) {
    cooldown_ -= static_cast<int>(getElapsed());
    if (cooldown_ < 1) {
      cooldown_ = 0;
      frameOf(child("static_fill"), 13);
      showChild("static_fill", true);
      showChild("anim_fill", false);
      refill();
    } else {
      // an upgrade bought during the cooldown changes its length: what is left is scaled by the change
      const float now = cooldownTotalNow();
      if (now != total_) {
        cooldown_ = static_cast<int>(static_cast<float>(cooldown_) * (now / total_));
        total_ = now;
      }
      if (VObject* fill = child("anim_fill")) {
        const std::size_t frames = fill->sheet() ? fill->sheet()->frames.size() : 0;
        fill->setCurrentFrame(static_cast<int>(static_cast<float>(frames) * (1.0 - static_cast<double>(cooldown_) / static_cast<double>(total_))));
      }
    }
  }
  if (!clink_ && state_ == 1) {
    if (const VObject* dispenser = child("anim_dispenser"); dispenser && dispenser->currentFrame() > 22) {
      clink_ = true;
      playSfx("audio/sfx/sauce_clink.ogg");
    }
  }
  VMachine::update(softPaused);
}

void VSauceDrink::activate() {
  VMachine::activate();
  frameOf(child("static_fill"), 13);
  showChild("static_fill", true);
  showChild("anim_dispenser", true);
  playChild("anim_dispenser", forward_);
  showChild("anim_top", true);
  playChild("anim_top", forward_);
  suspendWhilePlaying(childShared("anim_top"));
  playSfx("audio/sfx/sauce_arm_extend.ogg");
}

void VSauceDrink::reset() {
  VMachine::reset();
  clink_ = false;
  frameOf(child("static_fill"), 12);
  showChild("static_fill", cooldown_ == 0);
  showChild("anim_fill", cooldown_ != 0);
  if (cooldown_ == 0) frameOf(child("anim_fill"), 0);
  frameOf(child("static_top"), drink_ ? 28 : 1);
}

bool VSauceDrink::onResume() {
  if (state_ < 1) return true;
  if (state_ == 1) {
    state_ = 2;
    showChild("anim_dispenser", false);
    showChild("static_dispenser", true);
    showChild("static_fill", false);
    showChild("anim_fill", true);
    playChild("anim_fill", reverse_);
    showChild("anim_top", false);
    showChild("static_top", true);
    frameOf(child("static_top"), drink_ ? 29 : 2);
    showChild("anim_spray", true);
    playChild("anim_spray", drink_ ? "DoDrinkFill" : "DoSauceFill");
    if (drink_) playChild("anim_fill_steam", "DoDrinkSteam", true);
    suspendWhilePlaying(childShared("anim_fill"));
    playSfx("audio/sfx/sauce_spray.ogg");
    return false;
  }
  if (state_ == 2) {
    state_ = 3;
    showChild("static_fill", true);
    frameOf(child("static_fill"), 12);
    showChild("anim_fill", false);
    showChild("anim_spray", false);
    showChild("static_top", false);
    showChild("anim_top", true);
    playChild("anim_top", reverse_);
    if (bottle_) bottle_->setVisible(true);
    unlock();
    suspendWhilePlaying(childShared("anim_top"));
    playSfx("audio/sfx/sauce_arm_retract.ogg");
    return false;
  }
  processed_ = true;
  reset();
  return true;
}

}  // namespace choco::games::greatcc
