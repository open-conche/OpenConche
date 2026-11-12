#pragma once

// The numbers of RTimer (docs/re/greatcc-ui-library.md 6.3): the elapsed and total milliseconds of a level, the running flag
// and the clock stamp it measures from. The window around it (ui/RTimer) feeds it the modal clock.

#include <algorithm>
#include <cstdio>
#include <string>

namespace choco::games::greatcc {

struct LevelTimer {
  int durationMs = 0;  // the limit (story) or 0
  int elapsedMs = 0;
  int lastMs = 0;      // the clock reading of the previous tick; 0 = not taken yet
  bool incrementing = true;  // endless: shows the elapsed time
  bool running = true;

  // RTimer::Reset: the duration, elapsed time and stamp are cleared and the timer runs. `incrementing` stays.
  void reset() {
    durationMs = 0;
    elapsedMs = 0;
    lastMs = 0;
    running = true;
  }
  // RTimer::OnTaskAnimate at clock reading `nowMs`: while running, the first call takes the stamp, then the elapsed time grows
  // by the difference to the previous reading. A negative duration is clamped to 0.
  void tick(int nowMs) {
    if (!running) return;
    if (lastMs == 0) lastMs = nowMs;
    elapsedMs += nowMs - lastMs;
    lastMs = nowMs;
    if (durationMs < 0) durationMs = 0;
  }
  // The shop's start of the level and VShopHud's resume: running again, measuring from now.
  void resume(int nowMs) {
    running = true;
    lastMs = nowMs;
  }
  // The value RTimer::SetText shows: the remaining time (never negative) or, when incrementing, the elapsed time.
  int shownMs() const { return incrementing ? elapsedMs : std::max(0, durationMs - elapsedMs); }
  // "%d:%.2d" of minutes and seconds, both truncated.
  std::string text() const {
    const unsigned v = static_cast<unsigned>(shownMs());
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%.2d", static_cast<int>(v / 60000), static_cast<int>(v / 1000 - (v / 60000) * 60));
    return buf;
  }
  // PercentElapsed: 0 when the duration is not positive, else min(100, elapsed / duration * 100) in float arithmetic.
  float percentElapsed() const {
    if (durationMs <= 0) return 0;
    const float p = (static_cast<float>(elapsedMs) / static_cast<float>(durationMs)) * 100.0f;
    return p <= 100.0f ? p : 100.0f;
  }
};

}  // namespace choco::games::greatcc
