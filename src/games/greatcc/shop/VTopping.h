#pragma once

// VTopping (docs/re/greatcc-machines-and-chocolates.md 5.5): the machine that puts a topping on a shaped chocolate. The player
// brings a bar, truffle, novelty or infusion bar and picks one of up to three toppings; the funnel runs, the particles fall, the
// topping layer fades in, and 900 ms later the chocolate can be picked up.

#include "games/greatcc/shop/VMachine.h"

namespace choco::games::greatcc {

class VTopping : public VMachine {
 public:
  explicit VTopping(script::ScriptContext& ctx) : VMachine(ctx) {}
  const char* kind() const override { return "VTopping"; }

  // The buttons follow `kTopping<i>` (quality mark `kToppingQ<i>`); then VMachine::Init when any is on.
  void init() override;
  // The particles' effects go and the topping layer is forgotten; then VMachine::Reset.
  void reset() override;
  // Needs a held chocolate that takes a topping and a choice (not the machine's own wait). Adds the layer of the chocolate's shape and
  // starts the machine.
  bool processChocolate() override;
  // The funnel plays, the particles fall (`DoParticles`, alpha 1), two sounds, the topping fades in over 810 ms, and the machine waits
  // for the particles.
  void activate() override;
  // Step 2: the particles fade out over 300 ms and the machine waits 300 ms; then it is done: Reset.
  bool onResume() override;
  void onArrival(VPlayer& player) override { feedArrival(player, 4); }

  VObject* toppingLayer() const { return layer_.get(); }

 private:
  std::shared_ptr<VChocolateLayer> layer_;  // +0x27c
};

}  // namespace choco::games::greatcc
