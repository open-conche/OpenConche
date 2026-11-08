#pragma once

// Chocolatier 1's money counter (SetUpdateRate / SetMoneyDisplayTarget / SetMoneyDisplay; docs/re/choc1-natives.md section 5).
// A repeating task every 1000/hz ms (integer division) steps the shown amount toward the target by max(1, gap / (3 * hz))
// whole dollars, so a count-up takes about 3 s. Pure logic: Choc1World ticks it and writes the Texts named "money".

#include <algorithm>
#include <cmath>
#include <string>

#include "games/common/MoneyCounter.h"

namespace choco::game::choc1 {

class Choc1Money {
 public:
  // SetUpdateRate(hz): 0 (or less) stops the task. The next step is one period after `nowMs`.
  void setRate(double hz, double nowMs) {
    hz_ = hz > 0 ? static_cast<int>(hz) : 0;
    periodMs_ = hz_ > 0 ? 1000 / hz_ : 0;
    if (periodMs_ <= 0) hz_ = 0;
    nextMs_ = nowMs + periodMs_;
  }
  int rate() const { return hz_; }
  int periodMs() const { return periodMs_; }
  bool enabled() const { return periodMs_ > 0; }

  // SetMoneyDisplay(n): the shown amount and the target jump to n.
  void snap(double amount) { counter_.snap(amount); }

  // SetMoneyDisplayTarget(n). Returns true when the money_earned sound plays: n is above the old target and the task runs.
  // Without a running task nothing would ever step the counter, so it snaps (inference).
  bool setTarget(double amount) {
    if (!enabled()) {
      counter_.snap(std::trunc(amount));
      return false;
    }
    const bool earned = amount > counter_.target;
    counter_.setTargetSteps(amount, 3 * hz_);
    return earned;
  }

  // Runs the steps due at `nowMs` (at most `kMaxCatchUp`). Returns whether the shown amount changed.
  bool tick(double nowMs) {
    if (!enabled() || nowMs < nextMs_) return false;
    bool changed = false;
    for (int i = 0; i < kMaxCatchUp && nowMs >= nextMs_; ++i) {
      nextMs_ += periodMs_;
      changed = counter_.step() || changed;
    }
    if (nowMs >= nextMs_) nextMs_ = nowMs + periodMs_;  // far behind: drop the rest
    return changed;
  }

  double shown() const { return counter_.display; }
  double target() const { return counter_.target; }
  const MoneyCounter& counter() const { return counter_; }

 private:
  static constexpr int kMaxCatchUp = 200;
  MoneyCounter counter_;
  int hz_ = 0, periodMs_ = 0;
  double nextMs_ = 0;
};

// UtilPulseImage(name, count) (TImagePulser, docs/re/choc1-natives.md section 5): a task every 100 ms sets the grey of the
// BitmapTint `name` from a ten-entry cycle; `count` ticks, then the tint goes back to 1.0. A Rollover popup runs its own
// TImagePulser on the same kind of window (`popup`: stops when the popup closes, never touches the UtilPulseImage one of the
// same name), so `popup` keeps the two apart.
struct Choc1Pulse {
  static constexpr double kPeriodMs = 100;
  static constexpr double kCycle[10] = {1.0, 0.9, 0.8, 0.7, 0.6, 0.6, 0.7, 0.8, 0.9, 1.0};
  std::string name;
  bool popup = false;  // a Rollover popup's pulser, not UtilPulseImage's
  int count = 0;
  int index = 0;
  double nextMs = 0;
};

}  // namespace choco::game::choc1
