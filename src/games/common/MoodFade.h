#pragma once

namespace choco::game {

// The cross-fade between two mood faces (docs/re/port-window.md 2.5): 200 ms on 50 ms window-animation ticks. `shown` is the
// face at full alpha, `target` fades in over it with timer / kFadeMs. Shared by CharWindow and choc2::Character.
struct MoodFade {
  static constexpr int kFadeMs = 200;
  static constexpr int kTickMs = 50;

  int shown = 0, target = 0, timer = 0;

  // Aims at `t`. True when the shown face differs, so the caller starts its animation (the timer restarts).
  bool setTarget(int t) {
    target = t;
    if (t == shown) return false;
    timer = 0;
    return true;
  }
  // No fade: the target is shown at once.
  void snap() {
    shown = target;
    timer = 0;
  }
  // One tick. True while the fade goes on; at the end the target becomes the shown face.
  bool tick() {
    timer += kTickMs;
    if (timer >= kFadeMs) {
      timer = kFadeMs;
      shown = target;
      return false;
    }
    return true;
  }
  float targetAlpha() const { return static_cast<float>(timer) / kFadeMs; }
};

}  // namespace choco::game
