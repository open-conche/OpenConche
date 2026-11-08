#pragma once

#include <algorithm>
#include <cmath>

namespace choco::game {

// The ledger's counting-up money display, shared by Chocolatier 3's LedgerWindow, Chocolatier 2's Ledger and Chocolatier 1's money Text. `target` is the
// real amount, `display` is what the Text shows, and each step moves the display by `delta`, 1/20 of the gap when the target
// was set (one second of 50 ms steps). The step never overshoots.
struct MoneyCounter {
  double display = 0, target = 0, delta = 0;

  // The display jumps to the amount (a new game or a load): no count-up.
  void snap(double amount) { display = target = amount; }

  // A new target. Returns true when it differs from the old one (and so a count-up starts). The caller passes the amount
  // already rounded the way it wants (Chocolatier 3 passes it through float, as the original).
  bool setTarget(double amount) {
    if (amount == target) return false;
    target = amount;
    delta = static_cast<double>(static_cast<float>((amount - display) / 20.0));
    return true;
  }

  // Chocolatier 2's rule (docs/re/choc2-ledger-binary.md section 3): a fixed step of max(1, |gap| / 7) whole dollars, so the
  // count-up takes 7 or 8 ticks (100 ms each) whatever the gap. The amount is an unsigned 32-bit: negative becomes 0.
  bool setTargetChoc2(double amount) {
    amount = std::trunc(std::clamp(amount, 0.0, 4294967295.0));
    if (amount == target) return false;
    target = amount;
    const double gap = std::trunc(std::fabs(amount - display) / 7.0);
    delta = (amount < display ? -1 : 1) * std::max(1.0, gap);
    return true;
  }

  // Chocolatier 1's rule (docs/re/choc1-natives.md section 5): the display counts to the amount in `steps` ticks, a step of
  // whole dollars `max(1, |gap| / steps)` toward it (the division truncates), and never overshoots. The amount is a whole number.
  bool setTargetSteps(double amount, int steps) {
    amount = std::trunc(amount);
    if (amount == target) return false;
    target = amount;
    const double gap = std::trunc(std::fabs(amount - display) / static_cast<double>(std::max(1, steps)));
    delta = (amount < display ? -1 : 1) * std::max(1.0, gap);
    return true;
  }

  bool moving() const { return display != target; }

  // One 50 ms step. Returns true when the display changed.
  bool step() {
    if (display == target) return false;
    display += delta;
    if ((delta < 0 && display < target) || (delta > 0 && display > target) || delta == 0) display = target;
    return true;
  }
};

}  // namespace choco::game
