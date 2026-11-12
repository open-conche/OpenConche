#include "games/greatcc/tutorial/TutorialSpider.h"

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/scene/SpriteManager.h"
#include "games/greatcc/tutorial/Tutorial.h"
#include "games/greatcc/tutorial/TutorialManager.h"

namespace choco::games::greatcc {

namespace {
constexpr float kDimIntensity = 0.5f;  // VTintEffect(0.5, 300, black)
constexpr double kDimMs = 300;
const gfx::ColorF kBlack{0, 0, 0, 0};
}  // namespace

// ---- the listener ----

void TutorialListener::onClick(VSprite& sprite) {
  if (const std::shared_ptr<Tutorial> t = tutorial_.lock())
    if (t->trigger(sprite.name()) == kClick) t->advanceStep();
}

void TutorialListener::onArrival(VSprite& sprite) {
  if (const std::shared_ptr<Tutorial> t = tutorial_.lock())
    if (t->trigger(sprite.name()) == kArrival) t->advanceStep();
}

void TutorialListener::onResume(bool stillSuspended, VSprite& sprite) {
  if (stillSuspended) return;
  if (const std::shared_ptr<Tutorial> t = tutorial_.lock())
    if (t->trigger(sprite.name()) == kResume) t->advanceStep();
}

void tellTutorialsOfArrival(VSprite& sprite) {
  const std::shared_ptr<VSprite> keep = sprite.shared_from_this();
  for (const auto& l : sprite.activeListeners())
    if (auto* t = dynamic_cast<TutorialListener*>(l.get())) t->onArrival(sprite);
}

void tellTutorialsOfResume(bool stillSuspended, VSprite& sprite) {
  const std::shared_ptr<VSprite> keep = sprite.shared_from_this();
  for (const auto& l : sprite.activeListeners())
    if (auto* t = dynamic_cast<TutorialListener*>(l.get())) t->onResume(stillSuspended, sprite);
}

// ---- the spider ----

SpriteManager* TutorialSpider::levelSprites() const {
  GreatCCWorld* w = worldOf(tutorial_.context());
  return w ? w->tutorials().levelSprites() : nullptr;
}

std::size_t TutorialSpider::listenedCount(int step) const {
  const auto it = listened_.find(step);
  return it == listened_.end() ? 0 : it->second.size();
}

std::size_t TutorialSpider::exemptCount(int step) const {
  const auto it = exempt_.find(step);
  return it == exempt_.end() ? 0 : it->second.size();
}

void TutorialSpider::clearExceptions() {
  seen_.clear();
  exempt_.clear();
  listened_.clear();
}

void TutorialSpider::activate() {
  step_ = 0;
  clearExceptions();
  tints_.clear();
  if (listener_) listener_->dead = true;
  listener_ = std::make_shared<TutorialListener>(std::static_pointer_cast<Tutorial>(tutorial_.shared_from_this()));
}

void TutorialSpider::cancelEffects(bool now) {
  // each sprite's own tint, then the global dim: destroyed at once, or stopped so that they fade out
  const auto all = std::move(tints_);
  tints_.clear();
  for (const auto& [step, byName] : all)
    for (const auto& [name, e] : byName)
      if (e) {
        if (now) e->destroy();
        else e->stop();
      }
  if (dim_) {
    if (now) dim_->destroy();
    else dim_->stop();
  }
  dim_.reset();
}

void TutorialSpider::reset(bool cancelNow) {
  step_ = 0;
  cancelEffects(cancelNow);
  if (listener_) listener_->dead = true;
  listener_.reset();
  if (SpriteManager* level = levelSprites()) {
    level->clearExclusiveSprites();
    level->softPause(false);
  }
  clearExceptions();
}

void TutorialSpider::addExclusive(int step, const std::shared_ptr<VSprite>& sprite, bool listen) {
  if (!sprite) return;
  seen_[step].insert(sprite);
  exempt_[step].insert(sprite->name());
  if (listen && listener_) {
    listened_[step].insert(sprite);
    sprite->addListener(listener_);
  }
}

void TutorialSpider::setStep(int step) {
  step_ = step;
  SpriteManager* level = levelSprites();
  if (!level) return;
  // the previous step's sprites let go of the listener
  if (const auto it = listened_.find(step_ - 1); it != listened_.end()) {
    const auto previous = it->second;
    for (const auto& weak : previous)
      if (const std::shared_ptr<VSprite> s = weak.lock())
        if (listener_) s->removeListener(listener_);
  }
  level->clearExclusiveSprites();
  if (const std::shared_ptr<TutorialStep> current = tutorial_.currentStep()) {
    exempt_[step_] = current->exemptNames();
    if (tutorial_.skipButton()) tutorial_.skipButton()->setVisible(current->skip());
    for (const std::string& name : current->lookupNames()) {
      VSprite* found = level->findSpriteByName(name);
      if (!found) continue;
      const std::shared_ptr<VSprite> sprite = found->shared_from_this();
      addExclusive(step_, sprite, current->trigger(name) != kNoTrigger);
      if (current->actionNames().count(name)) level->addExclusiveSprite(sprite);
    }
    if (!current->dim()) {
      cancelEffects(false);
    } else if (!dim_) {
      dim_ = std::make_shared<TintEffect>(kDimIntensity, kDimMs, kBlack, 0.0f);
      level->addGlobalEffect(dim_);
    }
  }
  level->forEachSprite([this](VSprite& s) {
    process(s);
    return true;
  }, false);
  tints_[step_ - 1].clear();
}

void TutorialSpider::process(VSprite& sprite) {
  bool seenAny = false, exemptNow = false, exemptBefore = step_ == 1;
  for (const auto& [key, names] : exempt_) {
    if (!names.count(sprite.name())) continue;
    seenAny = true;
    if (key == step_) exemptNow = true;
    else if (key == step_ - 1) exemptBefore = true;
  }
  if (!seenAny || !dim_) return;
  const std::shared_ptr<VSprite> keep = sprite.shared_from_this();
  std::shared_ptr<TintEffect> mine;  // the sprite's own tint from the previous step, if it has one
  if (const auto prev = tints_.find(step_ - 1); prev != tints_.end())
    if (const auto it = prev->second.find(sprite.name()); it != prev->second.end()) mine = it->second;
  if (exemptNow) {
    sprite.removeEffect(dim_);
    if (!mine) {
      if (!exemptBefore) {
        // it had the dim a step ago: its own copy starts from the dim's level and fades out
        auto fade = std::make_shared<TintEffect>(kDimIntensity, kDimMs, kBlack, dim_->intensity());
        sprite.addEffect(fade);
        fade->stop();
      }
    } else {
      mine->stop();
    }
    tints_[step_][sprite.name()] = mine;
    return;
  }
  if (exemptBefore) {
    // exempt a step ago, not now: it fades into the dim with its own tint, which stays until the tutorial ends
    sprite.removeEffect(dim_);
    auto tint = std::make_shared<TintEffect>(kDimIntensity, kDimMs, kBlack, mine ? mine->intensity() : 0.0f);
    if (mine) mine->destroy();
    tints_[step_][sprite.name()] = tint;
    sprite.addEffect(tint);
    return;
  }
  tints_[step_][sprite.name()] = mine;
}

}  // namespace choco::games::greatcc
