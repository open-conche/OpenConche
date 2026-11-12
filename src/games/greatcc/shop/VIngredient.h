#pragma once

// VIngredient (docs/re/greatcc-machines-and-chocolates.md 5.2): the machine that adds a filling to a bucket. She brings a bare
// bucket (or the machine already holds one), picks one of up to three fillings, and the machine runs: the intro lowers the tanks, the
// filling shows on the barrel's icon, the pump runs, and the intro plays back; the bucket can be picked up when the pump has stopped.

#include "games/greatcc/shop/VMachine.h"

namespace choco::games::greatcc {

class VIngredient : public VMachine {
 public:
  explicit VIngredient(script::ScriptContext& ctx) : VMachine(ctx) {}
  const char* kind() const override { return "VIngredient"; }

  // The buttons follow `kIngredient<i>` (the quality mark is `kIngredientQ<i>`); every lit tank (`static_top1/2`, `anim_top1/2`)
  // shows the art of the highest enabled `upgrade<i>`; then VMachine::Init when any is on.
  void init() override;
  // The overlay icon is forgotten; then VMachine::Reset.
  void reset() override;
  // Needs: not suspended, a held chocolate that takes the filling layer (a bare bucket), a choice. Adds the filling, remembers the
  // barrel icon's `overlay` leaf and starts the machine.
  bool processChocolate() override;
  // The intro and the two tank animations play (`machine_noise.ogg`), the overlay is hidden, the machine waits for the intro.
  void activate() override;
  // Step 2: the overlay fades in over 1 s while the pump runs for 1 s; step 3: the intro plays back, the bucket is unlocked and the
  // machine waits for it; then Reset.
  bool onResume() override;
  void onArrival(VPlayer& player) override { feedArrival(player, 1); }

  VObject* overlay() const { return overlay_.get(); }

 private:
  std::shared_ptr<VObject> overlay_;  // +0x27c
};

}  // namespace choco::games::greatcc
