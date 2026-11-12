#include "games/greatcc/shop/VMachine.h"

#include <cmath>
#include <cstdio>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/shop/VDelivery.h"
#include "games/greatcc/shop/VIngredient.h"
#include "games/greatcc/shop/VInfuse.h"
#include "games/greatcc/shop/VMixer.h"
#include "games/greatcc/shop/VPlayer.h"
#include "games/greatcc/shop/VPress.h"
#include "games/greatcc/shop/VTopping.h"
#include "games/greatcc/shop/VSauceDrink.h"
#include "games/greatcc/shop/VStation.h"

namespace choco::games::greatcc {

namespace {
unsigned delayFrom(float ms) {
  // the float is clamped at 0 and converted to an unsigned count
  if (ms <= 0.0f) return 0;
  return static_cast<unsigned>(ms);
}
bool startsWith(const std::string& s, const char* prefix) { return s.compare(0, std::char_traits<char>::length(prefix), prefix) == 0; }
}  // namespace

// ================================================================ VMachineButton

VMachineButton::VMachineButton(script::ScriptContext& ctx, std::weak_ptr<VMachine> machine) : VButton(ctx, 0, ""), machine_(std::move(machine)) {
  setType("machine button");
  setLayer(10);
}

std::shared_ptr<VMachineButton> VMachineButton::create(script::ScriptContext& ctx, std::weak_ptr<VMachine> machine) {
  auto b = std::make_shared<VMachineButton>(ctx, std::move(machine));
  b->addEffect(std::make_shared<HoverGlow>(0.05f, true, true, true));
  return b;
}

void VMachineButton::setUpgraded(bool upgraded) {
  removeChildren();
  switchAsset("button", "default", "default");
  upgraded_ = upgraded;
  if (upgraded) setTint({0.0f, 1.0f, 1.0f, 1.0f});
}

void VMachineButton::init() {
  const std::string iconName = attribute("icon");
  icon_ = addAssetLeaf("icon", iconName, 1);
  if (iconName == "dark" && icon_) icon_->setTint({0.5f, 0.5f, 1.0f, 1.0f});
  const std::string stripeName = attribute("stripe");
  if (!stripeName.empty()) {
    stripe_ = addAssetLeaf("stripe", stripeName, 1);
    // a button with a stripe shows it; the icon comes up when the pointer rests
    if (icon_) icon_->setVisible(false);
  }
}

void VMachineButton::onMouseEnter() {
  VButton::onMouseEnter();
  if (auto m = machine_.lock()) m->doEvent(SpriteEvent::Enter, {});
}

void VMachineButton::onMouseLeave() {
  VButton::onMouseLeave();
  if (auto m = machine_.lock()) m->doEvent(SpriteEvent::Leave, {});
  swapVisual(false);
}

void VMachineButton::onMouseHover(double ms) {
  if (ms < 501) return;  // 0x1f5
  swapVisual(true);
}

void VMachineButton::onClick() {
  const std::shared_ptr<VSprite> keep = shared_from_this();
  if (GreatCCWorld* w = worldOf(context())) w->sound().playSfx("audio/sfx/buttonclick.ogg", false);
  if (auto m = machine_.lock()) m->chooseOption(id());
}

void VMachineButton::update(bool softPaused) {
  // a finished fade-out hides its sprite
  if (iconFx_ && !iconFx_->fadeIn() && !iconFx_->inProgress()) {
    iconFx_.reset();
    if (icon_) icon_->setVisible(false);
  }
  if (stripeFx_ && !stripeFx_->fadeIn() && !stripeFx_->inProgress()) {
    stripeFx_.reset();
    if (stripe_) stripe_->setVisible(false);
  }
  VSprite::update(softPaused);
}

void VMachineButton::swapVisual(bool hovered) {
  if (swapped_ == hovered) return;
  swapped_ = hovered;
  if (!stripe_ || !icon_) return;
  if (iconFx_) icon_->removeEffect(iconFx_);
  if (stripeFx_) stripe_->removeEffect(stripeFx_);
  if (!swapped_) {
    // the pointer left: the stripe comes back and the icon goes
    stripeFx_ = std::make_shared<AlphaEffect>(true, 1.0f, 300, stripeFx_ ? stripeFx_->alpha() : 0.0f, 0);
    stripe_->setVisible(true);
    stripe_->addEffect(stripeFx_);
    iconFx_ = std::make_shared<AlphaEffect>(false, 1.0f, 300, iconFx_ ? iconFx_->alpha() : 1.0f, 0);
    icon_->addEffect(iconFx_);
  } else {
    iconFx_ = std::make_shared<AlphaEffect>(true, 1.0f, 300, iconFx_ ? iconFx_->alpha() : 0.0f, 0);
    icon_->setVisible(true);
    icon_->addEffect(iconFx_);
    stripeFx_ = std::make_shared<AlphaEffect>(false, 1.0f, 300, stripeFx_ ? stripeFx_->alpha() : 1.0f, 0);
    stripe_->addEffect(stripeFx_);
  }
}

// ================================================================ VMachine

VMachine::VMachine(script::ScriptContext& ctx) : VSuspendable(ctx, "", "") {
  setDepth(7);
  setClickable(true);
}

std::shared_ptr<VMachine> VMachine::create(script::ScriptContext& ctx, int stationType) {
  switch (stationType) {
    case kStationMixer: return std::make_shared<VMixer>(ctx);
    case kStationIngredient: return std::make_shared<VIngredient>(ctx);
    case kStationBarPress: return std::make_shared<VPress>(ctx);
    case kStationTopping: return std::make_shared<VTopping>(ctx);
    case kStationInfuse: return std::make_shared<VInfuse>(ctx);
    case kStationDelivery: return std::make_shared<VDelivery>(ctx);
    case kStationSauce: return std::make_shared<VSauceDrink>(ctx, false);
    case kStationDrink: return std::make_shared<VSauceDrink>(ctx, true);
    default: return nullptr;
  }
}

void VMachine::loadFromTable(const StyleChain& style) {
  detachChocolate(true);
  auto previous = std::move(buttons_);
  buttons_.clear();
  selectionCount_ = 0;
  if (auto selections = style.table("selections", 0)) {
    const StyleChain group(*selections, &style);
    selections->forEachEntry([&](const std::string& key, const ui::Style* t, const std::optional<std::string>&) {
      if (!t) return;
      ++selectionCount_;
      const std::string k = VObject::lower(key);
      std::shared_ptr<VMachineButton> button;
      if (auto it = previous.find(k); it != previous.end()) button = it->second;
      if (!button) {
        button = VMachineButton::create(context(), std::static_pointer_cast<VMachine>(shared_from_this()));
        addChild(button);
      }
      const StyleChain bc(*t, &group);
      button->loadFromTable(bc);
      button->init();
      button->setVisible(false);
      buttons_[k] = button;
    });
  }
  cancelEffects();
  auto glow = std::make_shared<HoverGlow>(0.15f, true, true, true);
  addEffect(glow);
  for (auto& [key, b] : buttons_)
    if (b) b->removeEffect(glow);
  VObject::loadFromTable(style);
}

void VMachine::init() {
  if (initialised_) return;
  initialised_ = true;
  VObject::init();
  switchAsset("base", "default", "default");
  reset();
  setDelay(delayFrom(frameDelay_), true);
}

void VMachine::update(bool softPaused) {
  if (chocolate_ && chocolate_->hasDecayed()) detachChocolate(false);
  VSuspendable::update(softPaused);
}

void VMachine::onClick() {
  if (auto st = station_.lock()) st->onMachineClick(name());
}

bool VMachine::onResume() {
  reset();
  return true;
}

void VMachine::reset() {
  state_ = 0;
  unlock();
  clearSuspended();
  VSuspendable::reset();
  for (const auto& [key, c] : childObjects()) {
    if (!c) continue;
    c->setLeaf(true);
    if (startsWith(key, "anim_")) {
      c->setVisible(false);
      c->stop(true);
    } else if (startsWith(key, "static_")) {
      c->setVisible(true);
    }
  }
  if (option_ > -1 && !processed_) processChocolate();
}

void VMachine::chooseOption(int id) {
  if (!locked_) option_ = id;
  doEvent(SpriteEvent::Click, {});
}

void VMachine::activate() {
  for (const auto& [key, c] : childObjects())
    if (c && startsWith(key, "static_")) c->setVisible(false);
  // the steam booster (a gameplay upgrade) runs the machine's frames ten percent faster
  const bool steam = stationUpgrade("kSteam1", "gameplay");
  forward_ = "DoAnim";
  reverse_ = "Reverse";
  setDelay(delayFrom(steam ? static_cast<float>(frameDelay_ * 0.9) : frameDelay_), true);
  if (auto st = station_.lock()) st->suspend(0);
  state_ = 1;
  locked_ = true;
  processed_ = true;
  option_ = -1;
}

void VMachine::unlock() {
  if (!locked_) return;
  locked_ = false;
  if (auto st = station_.lock()) st->reset();
}

void VMachine::onArrival(VPlayer& player) {
  if (chocolate_ && !locked_ && chocolate_->hands() <= player.numHandsFree()) {
    std::shared_ptr<VChocolate> out = detachChocolate(true);
    player.attachObject(out);
  }
}

bool VMachine::attachChocolate(const std::shared_ptr<VChocolate>& choc, int spriteLayer, bool playSound) {
  processed_ = false;
  if ((!chocolate_ || chocolate_ == choc) && choc) {
    chocolate_ = choc;
    choc->setLayer(spriteLayer);
    addChild(choc);
    choc->attachToAnchor("chocolate");
    if (playSound) playSfx("audio/sfx/pickup_drop.ogg");
    return true;
  }
  return false;
}

std::shared_ptr<VChocolate> VMachine::detachChocolate(bool playSound) {
  processed_ = false;
  if (!chocolate_) return nullptr;
  std::shared_ptr<VChocolate> out = chocolate_;
  removeChild(*out);
  chocolate_.reset();
  if (playSound) playSfx("audio/sfx/pickup_drop.ogg");
  return out;
}

void VMachine::setStation(const std::shared_ptr<VStation>& station) {
  station_ = station;
  setLeaf(false);
  setClickable(true);
}

void VMachine::playSfx(const char* file) const {
  if (GreatCCWorld* w = worldOf(context())) w->sound().playSfx(file, false);
}

int VMachine::getWhichHand(VPlayer& player, int layer) const {
  int h0 = -1, h1 = -1;
  const std::shared_ptr<VChocolate> c0 = player.getChocolate(VPlayer::kHandPreferred, &h0);
  const std::shared_ptr<VChocolate> c1 = player.getChocolate(h0 != 2 ? 2 : 1, &h1);
  const bool take0 = c0 && c0->canAddLayer(layer), take1 = c1 && c1->canAddLayer(layer);
  if (!take0 && !take1) return -1;
  if (take0 && !take1) return h0;
  if (!take0) return h1;
  if (!host_) return -1;
  const std::shared_ptr<VChocolate> pick = host_->findOrderWithLayer(c0, c1, layer, option_);
  if (pick == c0) return h0;
  if (pick == c1) return h1;
  return -1;
}

void VMachine::feedArrival(VPlayer& player, int layer) {
  if (locked_) return;
  if (processChocolate()) return;
  if (option_ < 0) {
    if (!chocolate_) {
      const int hand = getWhichHand(player, layer);
      // no fitting hand gives -1, which DetachObject takes as no item (and turns the preferred hand)
      const std::shared_ptr<VCarriable> item = player.detachObject(hand);
      if (item) attachChocolate(std::dynamic_pointer_cast<VChocolate>(item), 1, true);
    } else if (player.numHandsFree() > 0) {
      player.attachObject(detachChocolate(true));
    }
    option_ = -1;
    return;
  }
  const int hand = getWhichHand(player, layer);
  if (hand == -1) {
    option_ = -1;
    return;
  }
  const std::shared_ptr<VChocolate> output = detachChocolate(true);
  const std::shared_ptr<VCarriable> item = player.detachObject(hand);
  if (item) attachChocolate(std::dynamic_pointer_cast<VChocolate>(item), 0, true);
  player.attachObject(output);
  processChocolate();
}

void VMachine::showChild(const char* key, bool visible) const {
  if (VObject* c = childObject(key)) c->setVisible(visible);
}

void VMachine::playChild(const char* key, const std::string& fn, bool loop) const {
  if (VObject* c = childObject(key)) c->play(fn, loop, false, 0);
}

void VMachine::initSelections(const char* family, const char* qualityFamily, const char* tankPrefix) {
  enabled_ = false;
  if (!host_) return;
  for (int i = 1; i <= selectionCount_; ++i) {
    char name[40];
    std::snprintf(name, sizeof name, "%s%d", family, i);
    const bool active = stationUpgrade(name);
    if (tankPrefix) {
      std::snprintf(name, sizeof name, "%s%d", tankPrefix, i);
      showChild(name, active);
    }
    std::snprintf(name, sizeof name, "button%d", i);
    const auto it = buttons_.find(name);
    if (it == buttons_.end() || !it->second) continue;
    VMachineButton& button = *it->second;
    button.setVisible(active);
    bool quality = false;
    if (qualityFamily) {
      char q[40];
      std::snprintf(q, sizeof q, "%s%d", qualityFamily, i);
      quality = stationUpgrade(q);
    }
    button.setUpgraded(quality);
    if (active) {
      button.init();
      enabled_ = true;
    }
  }
}

void VMachine::finishInit() {
  if (enabled_ || (host_ && host_->isEndless())) VMachine::init();
  setVisible(enabled_);
}

}  // namespace choco::games::greatcc
