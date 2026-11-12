#pragma once

// VTutorialSpider and VTutorialListener (docs/re/greatcc-meta-flow.md 1.5): what dims the level and what lets a step's sprites be
// clicked, and the listener a step's sprites carry to tell the tutorial of a click, an arrival or a resume.

#include <map>
#include <memory>
#include <set>
#include <string>

#include "games/greatcc/scene/Effects.h"
#include "games/greatcc/scene/VSprite.h"

namespace choco::games::greatcc {

class Tutorial;

// VTutorialListener: a sprite listener that holds the tutorial weakly. It acts when the sprite's name is a trigger of the kind in
// the tutorial's current step.
class TutorialListener : public SpriteListener {
 public:
  explicit TutorialListener(std::weak_ptr<Tutorial> tutorial) : tutorial_(std::move(tutorial)) {}
  void onClick(VSprite& sprite) override;
  // A player arrived at the sprite (a station): kArrival advances.
  void onArrival(VSprite& sprite);
  // A suspendable's wait is over: `stillSuspended` is its flag after the call; only a finished wait (false) with a kResume trigger
  // advances.
  void onResume(bool stillSuspended, VSprite& sprite);

 private:
  std::weak_ptr<Tutorial> tutorial_;
};

// VSpriteListener::GetCast<VTutorialListener> over the sprite's active listeners, then the call.
void tellTutorialsOfArrival(VSprite& sprite);
void tellTutorialsOfResume(bool stillSuspended, VSprite& sprite);

class SpriteManager;

class TutorialSpider {
 public:
  explicit TutorialSpider(Tutorial& tutorial) : tutorial_(tutorial) {}

  // Activate: from step 0 with nothing remembered, the old listener dead and a new one.
  void activate();
  // Reset: the effects end (now, or by fading), the listener is dropped, the level's exclusive set is emptied and its soft pause
  // ends.
  void reset(bool cancelNow);
  // SetStep(n): the previous step's sprites let go of the listener, the exclusive set is rebuilt from the step's action objects
  // and click-able triggers, the dim is made (or cancelled when the step does not dim) and every top-level sprite of the level
  // is processed.
  void setStep(int step);
  // Process: only the sprites that some step so far exempted are touched (the rest keep the dim from the global effect): the ones
  // this step exempts lose it (fading when they had it a step ago), the ones that were exempt a step ago and are not now get their
  // own tint that fades in and stays.
  void process(VSprite& sprite);

  int step() const { return step_; }
  const std::shared_ptr<TutorialListener>& listener() const { return listener_; }
  const std::shared_ptr<TintEffect>& dimEffect() const { return dim_; }
  // The sprites of a step that got the listener (for tests).
  std::size_t listenedCount(int step) const;
  std::size_t exemptCount(int step) const;

 private:
  void addExclusive(int step, const std::shared_ptr<VSprite>& sprite, bool listen);
  void cancelEffects(bool now);
  void clearExceptions();
  SpriteManager* levelSprites() const;

  Tutorial& tutorial_;
  int step_ = 0;
  std::shared_ptr<TutorialListener> listener_;
  std::map<int, std::set<std::weak_ptr<VSprite>, std::owner_less<std::weak_ptr<VSprite>>>> seen_, listened_;
  std::map<int, std::set<std::string>> exempt_;
  std::map<int, std::map<std::string, std::shared_ptr<TintEffect>>> tints_;
  std::shared_ptr<TintEffect> dim_;
};

}  // namespace choco::games::greatcc
