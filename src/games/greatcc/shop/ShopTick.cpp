#include "games/greatcc/shop/ShopTick.h"

namespace choco::games::greatcc {

TickResult runShopTick(ShopTickState& s, ShopTickEnv& env) {
  // 1. The start gate.
  if (s.startCountdownMs >= 1) {
    s.startCountdownMs -= env.windowElapsedMs();
    if (s.startCountdownMs < 1) s.startCountdownMs = 0;
  } else if (!s.waitingToStart || (!s.endless && (s.prelevelPending || s.plotPending))) {
    // The level is running (or still waits for the pre-level dialog and the diary): the lockdown follows the level's window.
    if (!s.waitingToStart && env.hasLevel() && !s.endless && !env.tutorialInEffect()) {
      const bool lock = env.hasTimer() && env.showTutorials() ? env.levelInLockDown(env.percentElapsed()) : false;
      env.setLockDown(lock);
    }
  } else {
    s.waitingToStart = false;
    env.startLevel();
  }
  // 2, 3.
  env.updateCustomers();
  env.updateHud();
  // 4. and 5. The pending dialogs and the end of the level.
  bool checkEnd = true;
  if (!s.endless) {
    if (s.prelevelPending) {
      s.prelevelPending = false;
      if (!env.hasLevel() || !env.levelNeedsPrelevel()) s.plotPending = true;
      else env.showPrelevel();
    }
    if (s.plotPending) {
      s.plotPending = false;
      if (env.hasLevel() && env.levelNeedsPlot()) env.launchPlotDialog();
    }
    if (!env.hasTimer()) checkEnd = false;
    else if (s.levelOverStamp == 0 && !s.waitingOnZeppelin &&
             ((env.timerDurationMs() <= env.timerElapsedMs() && env.activeCustomersEmpty()) ||
              (env.activeCustomersEmpty() && env.pendingCustomersEmpty())))
      s.levelOverStamp = env.timerElapsedMs();
  } else {
    env.updateEndlessMode();
    if (env.angryLimit() <= env.angryThisLevel() && env.hasTimer() && s.levelOverStamp == 0) s.levelOverStamp = env.timerElapsedMs();
  }
  // 6. Two seconds after the end was noticed the level ends and the task stops.
  if (checkEnd && s.levelOverStamp != 0 && env.hasTimer() &&
      static_cast<unsigned>(env.timerElapsedMs() - s.levelOverStamp) > static_cast<unsigned>(kEndLevelDelayMs)) {
    env.doEndLevel();
    return TickResult::Stopped;
  }
  return TickResult::Continue;
}

}  // namespace choco::games::greatcc
