#pragma once

// VInfuse (docs/re/greatcc-machines-and-chocolates.md 5.4): the machine that turns a bare bucket into an infusion bar. The bucket
// goes in (a swap with the finished bar), the player picks an infusion, the fill wipes away, the top lifts the bar (its `unlock()`
// callback makes the bar available) and the door drops the barrel.

#include "games/greatcc/shop/VMachine.h"

namespace choco::games::greatcc {

class VInfuse : public VMachine {
 public:
  explicit VInfuse(script::ScriptContext& ctx) : VMachine(ctx) {}
  const char* kind() const override { return "VInfuse"; }

  // The buttons follow `kInfusion<i>` (quality mark `kInfusionQ<i>`); then VMachine::Init when any is on. `unlock` is added to the
  // scripts of `anim_top_working` and `drop` to those of `anim_door`.
  void init() override;
  // The temporary fill layer leaves, the layer and icon are forgotten and the base frame is 1; then
  // VMachine::Reset.
  void reset() override;
  // Needs: not suspended, a held chocolate that takes an infusion (a bucket without a filling), a choice. Copies the bucket's look
  // into a temporary fill layer, adds the infusion layer (which removes the bucket), hides the bar and moves it under
  // `anim_top_working`, starts the machine and counts `chocolates_made_this_level`.
  bool processChocolate() override;
  // The `static_base` and `static_top` stay shown, the fill icon wipes away over 300 ms, the machine waits 300 ms.
  void activate() override;
  // Step 2: the base frame 0, the statics go, the top, the base and the door play (the wait is on `anim_base`). Then the bar goes back
  // under the machine, `num_infusions` counts, Reset.
  bool onResume() override;
  // A bucket in hand is swapped with the machine's output and processed; otherwise the choice is cleared and the output picked up.
  void onArrival(VPlayer& player) override;

  // The `unlock()` of the top's animation: VMachine::Unlock and the bar shows.
  void unlockBar();
  // The `drop()` of the door's animation: the fill falls 46 px and wipes away over 200 ms, the trap door sounds.
  void dropBarrel();

  VChocolateLayer* fillLayer() const { return fillLayer_.get(); }

 private:
  void addCallbacks(const char* childKey, const char* globalName, void (VInfuse::*fn)());
  std::shared_ptr<VChocolateLayer> fillLayer_;  // +0x27c
  std::shared_ptr<VObject> fillIcon_;           // +0x284
};

}  // namespace choco::games::greatcc
