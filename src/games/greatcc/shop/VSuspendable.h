#pragma once

// VSuspendable (docs/re/greatcc-machines-and-chocolates.md 4.2): an object that can wait, for a time or for another sprite's
// animation, and is told when the wait is over. The machines chain their animations with it; characters derive from it too
// (a customer reading the menu is suspended). The wait runs on the sprite's clock, and a soft pause that ends moves the start
// of a running wait forward by the length of the pause, so waits do not run down while paused.

#include <memory>

#include "games/greatcc/scene/VObject.h"

namespace choco::games::greatcc {

class VSuspendable : public VObject {
 public:
  VSuspendable(script::ScriptContext& ctx, std::string name, std::string type) : VObject(ctx, std::move(name), std::move(type)) {}
  const char* kind() const override { return "VSuspendable"; }

  // Suspend(ms): waits that long from now.
  void suspend(double ms);
  // SuspendWhilePlaying: waits until the sprite has stopped playing (a null sprite changes nothing). The sprite is kept alive
  // for the wait.
  void suspendWhilePlaying(std::shared_ptr<VSprite> watched);
  // OnResume: the wait is over. True = finished (the suspension ends); false = another wait was set. The base ends it.
  virtual bool onResume() { return true; }
  // Reset: a suspended object is resumed first (OnResume is called, its result ignored), then the wait fields are cleared.
  virtual void reset();
  bool suspended() const { return suspended_; }

  // The soft-pause shift, the end of the wait (OnResume), then VSprite::Update.
  void update(bool softPaused) override;

 protected:
  void clearWait();
  // The flag alone, with no resume (VMachine::Reset clears it before the base Reset so that Reset does not resume).
  void clearSuspended() { suspended_ = false; }

 private:
  bool suspended_ = false;
  double start_ = 0, ms_ = 0;
  std::shared_ptr<VSprite> watched_;
};

}  // namespace choco::games::greatcc
