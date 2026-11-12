#pragma once

// VSauce and VDrink (docs/re/greatcc-machines-and-chocolates.md 5.6): the machines that hand out a sauce bottle or a cup of a drink.
// Neither takes an order or a chocolate. A bottle stands in the machine from the start; taking it starts a cooldown during which a
// bar fills up, and when it ends the machine runs its refill sequence (the arm comes down, the spray fills the bottle, the arm
// goes back) and a new bottle is there for the next visit. The two classes are the same machine with other art, other enabling
// upgrades and another cooldown value, so one class serves both.

#include "games/greatcc/shop/VCarriable.h"
#include "games/greatcc/shop/VMachine.h"

namespace choco::games::greatcc {

class VSauceDrink : public VMachine {
 public:
  VSauceDrink(script::ScriptContext& ctx, bool drink);
  const char* kind() const override { return drink_ ? "VDrink" : "VSauce"; }
  bool isDrink() const { return drink_; }

  // Enabled by `kSauce1` (the sauce machine), `kSauce3` and not `kDrink1` (the machine named `machine_sauce_2`) or `kDrink1` (the
  // drink machine). While it is enabled (or in endless mode) the first Init clears the cooldown and puts a full bottle in; then
  // VMachine::Init. The machine is shown only when enabled.
  void init() override;
  // The cooldown counts down in game time (it is not stopped by a soft pause); its length follows the value when that changes;
  // the fill bar's frame follows it; at the end the tank shows its empty frame and the machine refills. The clink plays once while
  // the dispenser passes its 23rd frame. Then VMachine::Update.
  void update(bool softPaused) override;
  // The bottle goes to the avatar's hand (unlocked, a bottle in the machine, a hand free) and the cooldown starts.
  void onArrival(VPlayer& player) override;
  // The first step of the refill sequence: the dispenser and the top come down (930 ms) and the machine waits for the top.
  void activate() override;
  // The clink is forgotten (a running cooldown stays), the tank shows frame 12 or the fill bar by whether a cooldown is running, the top
  // is retracted.
  void reset() override;
  // Steps by `state`: 2 the dispenser goes, the spray fills (and the fill bar runs down) while the machine waits for the fill bar;
  // 3 the bottle shows and is unlocked and the top goes back up; then the machine is finished and Reset.
  bool onResume() override;

  // AttachItem: a bottle from the machine's `bottle` template (null and false when one is in already or there is no template).
  bool attachItem();
  // Refill: unless the machine is waiting, a new bottle is made, hidden, and the sequence starts.
  void refill();

  const std::shared_ptr<VCarriable>& bottle() const { return bottle_; }
  // The cooldown left in ms (0 = none running) and the length it was started with (value `sauce_cooldown` or `drink_cooldown`).
  int cooldownMs() const { return cooldown_; }
  float cooldownTotal() const { return total_; }
  bool clinkPlayed() const { return clink_; }

 private:
  const char* cooldownValue() const { return drink_ ? "drink_cooldown" : "sauce_cooldown"; }
  float cooldownTotalNow() const;
  bool drink_;
  std::shared_ptr<VCarriable> bottle_;  // +0x27c
  int cooldown_ = 0;                    // ms left
  float total_ = 0;
  bool clink_ = false;
};

}  // namespace choco::games::greatcc
