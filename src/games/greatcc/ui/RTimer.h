#pragma once

// RTimer (docs/re/greatcc-ui-library.md 6.3): a Text that counts the level's time on its modal's clock and shows it as m:ss.
// The shop's HUD owns what it means (VShopHud): it resets the timer for each level, sets the limit, stops and resumes it.

#include "games/greatcc/core/LevelTimer.h"
#include "ui/widgets/TextWidgets.h"

namespace choco::games::greatcc {

class RTimer : public ui::TextWindow {
 public:
  explicit RTimer(ui::WindowManager& wm) : TextWindow(wm) {}
  std::string_view typeName() const override { return "RTimer"; }
  // Keys, after the Text's: isincrementing (default true), starttime (the limit in ms, default 0), hide (default false); then
  // the first text is set.
  void init(const ui::Style& style) override;
  // A window that is `hide`n is made invisible; the 16 ms animation starts.
  void postChildrenInit(const ui::Style& style) override;
  // OnTaskAnimate: while running, the elapsed time follows the modal's clock and the text is refreshed. Always keeps running.
  bool onAnimate() override;
  std::string detail() const override;

  LevelTimer& timer() { return timer_; }
  const LevelTimer& timer() const { return timer_; }
  // The clock reading the timer measures from (the modal clock in whole milliseconds).
  int clockNow() const;
  // RTimer::SetText.
  void refreshText() { setLabel(timer_.text()); }
  // RTimer::Reset.
  void reset() { timer_.reset(); }
  // The shop's start and VShopHud's resume: running, measuring from the clock's reading now.
  void resumeFromNow() { timer_.resume(clockNow()); }

 private:
  LevelTimer timer_;
  bool hide_ = false;
};

}  // namespace choco::games::greatcc
