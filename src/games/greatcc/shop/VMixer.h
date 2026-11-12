#pragma once

// VMixer (docs/re/greatcc-machines-and-chocolates.md 5.1): the machine that makes a bucket of dark, milk or white chocolate. It takes
// nothing in; a visit with a choice and an empty machine fills a fresh bucket, the arm, the tube and the spray run, and the
// bucket can be picked up at the fourth step while the machine winds down.

#include "games/greatcc/shop/VMachine.h"

namespace choco::games::greatcc {

class VMixer : public VMachine {
 public:
  explicit VMixer(script::ScriptContext& ctx);
  const char* kind() const override { return "VMixer"; }

  // The buttons, tanks and quality marks follow `kMixer<i>` and `kMixerQ<i>`; then VMachine::Init when any is on.
  void init() override;
  // A held, processed bucket is handed over (when unlocked and a hand is free); then, when unlocked and idle, the choice is made.
  void onArrival(VPlayer& player) override;
  void reset() override;
  // A choice and an empty machine: the vendor's empty chocolate of that mix (decay on) is attached at sprite layer 2 and the
  // machine starts.
  bool processChocolate() override;
  void activate() override;
  // The steps (state after the step): 2 the spray sound and the tube down; 3 the spray runs and the bucket's icon wipes in for
  // 800 ms; 4 Unlock and the tube back up; 5 the arm back; then Reset.
  bool onResume() override;

  VObject* bucketIcon() const { return icon_.get(); }

 private:
  std::shared_ptr<VObject> icon_;  // the held bucket's icon (+0x27c)
};

}  // namespace choco::games::greatcc
