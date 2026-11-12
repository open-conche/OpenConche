#include "games/greatcc/shop/VSuspendable.h"

#include "games/greatcc/tutorial/TutorialSpider.h"

namespace choco::games::greatcc {

void VSuspendable::suspend(double ms) {
  start_ = clockNow();
  suspended_ = true;
  ms_ = ms;
}

void VSuspendable::suspendWhilePlaying(std::shared_ptr<VSprite> watched) {
  if (!watched) return;
  suspended_ = true;
  watched_ = std::move(watched);
}

void VSuspendable::clearWait() {
  start_ = 0;
  ms_ = 0;
  watched_.reset();
}

void VSuspendable::reset() {
  if (suspended_) {
    suspended_ = false;
    const std::shared_ptr<VSprite> keep = shared_from_this();
    onResume();
    // the tutorial listeners are told the wait is over (the flag is clear), then the pending listener changes take effect
    tellTutorialsOfResume(suspended_, *this);
    cleanUpListeners();
  }
  clearWait();
}

void VSuspendable::update(bool softPaused) {
  // The pause that just ended started at softPauseStart; the wait began before it, so the pause is taken out of the wait.
  if (!softPaused && softPauseStart() != 0 && start_ < softPauseStart()) {
    const double lost = softPauseStart() == start_ ? 0 : softPauseStart() - start_;
    start_ = clockNow() - lost;
  }
  if (suspended_) {
    const bool watchedDone = watched_ && !watched_->scriptIsPlaying(-1);
    const bool timeUp = ms_ != 0 && ms_ <= clockNow() - start_;
    if (watchedDone || timeUp) {
      // the wait fields go first: OnResume may set a new wait
      std::shared_ptr<VSprite> keep = std::move(watched_);
      watched_.reset();
      ms_ = 0;
      const std::shared_ptr<VSprite> self = shared_from_this();  // a listener may take the sprite out of the scene
      if (onResume()) suspended_ = false;
      tellTutorialsOfResume(suspended_, *this);
    }
  }
  VObject::update(softPaused);
}

}  // namespace choco::games::greatcc
