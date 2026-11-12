#pragma once

// VPress (docs/re/greatcc-machines-and-chocolates.md 5.3): the machine that shapes a bucket into a bar, a truffle or a novelty. The
// player puts a bucket in (a swap with whatever finished bar the press still holds), picks the shape, and the press runs: the
// barrel's fill wipes away, the door opens and the barrel drops, the weight comes down and stamps (its `unlock()` callback makes
// the bar available again), and the counters of bars and truffles are kept.

#include "games/greatcc/shop/VMachine.h"

namespace choco::games::greatcc {

class VPress : public VMachine {
 public:
  explicit VPress(script::ScriptContext& ctx);
  const char* kind() const override { return "VPress"; }

  // The buttons follow `kPress<i>` (no quality marks); then VMachine::Init when any is on. `unlock` is added to the scripts of
  // `anim_largeweight` and `drop` to those of `anim_door`, enabled or not.
  void init() override;
  // The fill name goes back to `DoAnim_0`, the shape to -1, the temporary fill layer leaves, the base frame is 3; then
  // VMachine::Reset.
  void reset() override;
  // Needs: not suspended, a held chocolate that takes the shape layer, a choice. Copies the bucket's look into a temporary fill
  // layer (anchor `bucket`), adds the shape layer to the chocolate (hidden until the stamp), starts the machine and counts
  // `chocolates_made_this_level`.
  bool processChocolate() override;
  // The fill icon wipes away over 300 ms, the `static_` door, weight and wheel stay shown, the fill animation `DoAnim_<mix>` plays,
  // and the machine waits 300 ms.
  void activate() override;
  // Step 2: the statics go, the door, weight and wheel play (the wait is on the weight). Then the counters `num_truffles` or
  // `num_bars`, Reset, and whether the machine is idle again.
  bool onResume() override;
  // VMachine::Update, then, in step 2, the base frame is 4 while the weight is at its frame 5 and 3 otherwise.
  void update(bool softPaused) override;
  // A bucket in hand is swapped with the press's output (or put in an empty press) and processed; otherwise the choice is cleared
  // and the output picked up.
  void onArrival(VPlayer& player) override;

  // The `unlock()` of the weight's animation: VMachine::Unlock, the bar shows, the fill hides, the stamp sounds.
  void unlockStamp();
  // The `drop()` of the door's animation: the fill layer falls 46 px and wipes away over 200 ms, the trap door sounds.
  void dropBarrel();

  int shape() const { return shape_; }
  const std::string& fillAnimation() const { return fillAnim_; }
  VChocolateLayer* fillLayer() const { return fillLayer_.get(); }

 private:
  void addCallbacks(const char* childKey, const char* globalName, void (VPress::*fn)());
  int shape_ = -1;                       // +0x27c
  std::string fillAnim_ = "DoAnim_0";    // +0x280
  std::shared_ptr<VChocolateLayer> fillLayer_;  // +0x284
  std::shared_ptr<VObject> fillIcon_;    // +0x28c
};

}  // namespace choco::games::greatcc
