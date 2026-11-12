#pragma once

// VSpriteWindow, VObjectWindow and VRegister (docs/re/greatcc-sprites-and-zones.md 4.1, docs/re/greatcc-shop-simulation.md 8).
// A sprite window owns a SpriteManager, draws it inside its rectangle and updates it once per drawn frame on its modal's
// clock. VObjectWindow builds objects from the style's `objects` table; VRegister is the odometer of the score and cash.

#include <memory>
#include <vector>

#include "games/greatcc/scene/Digit.h"
#include "games/greatcc/scene/SpriteManager.h"
#include "ui/Window.h"

namespace choco::games::greatcc {

class VSpriteWindow : public ui::Window {
 public:
  VSpriteWindow(ui::WindowManager& wm, script::ScriptContext& ctx);
  std::string_view typeName() const override { return "VSpriteWindow"; }
  // Hands the manager its window name and the modal clock, then starts the per-frame update.
  void postChildrenInit(const ui::Style& style) override;
  void draw(ui::DrawContext& ctx) const override;
  // OnTaskAnimate: the manager's clock := the modal's, then updateObjects. Keeps running.
  bool onAnimate() override;
  // Mouse events go to the manager; the result says whether it consumed the event (a miss falls through to the windows
  // behind). Window-local coordinates are the sprites' coordinates. Keys go to the manager's debug toggles (Ctrl+T, Ctrl+S) and are
  // never consumed.
  bool onMouseMove(int x, int y) override { return manager_->onMouseMove({x, y}); }
  bool onMouseDown(int x, int y) override { return manager_->onMouseDown({x, y}); }
  bool onMouseUp(int x, int y) override { return manager_->onMouseUp({x, y}); }
  bool onKeyDown(const ui::KeyEvent& ev) override {
    manager_->onKeyDown(ev.name, ev.ctrl);
    return false;
  }
  std::string detail() const override;

  SpriteManager& sprites() { return *manager_; }
  const SpriteManager& sprites() const { return *manager_; }

 protected:
  // See VSpriteWindow.cpp: keys reach the window that is its modal's default focus.
  void claimKeyFocus();
  script::ScriptContext& ctx_;

 private:
  std::unique_ptr<SpriteManager> manager_;
};

// The `objects` table of the style: each entry a VObject (loaded, initialised, added to the manager); an entry's `effects`
// list may hold `{ type = "move", x, y, time (300), accel (0), delay (0) }`, the only type recognised.
class VObjectWindow : public VSpriteWindow {
 public:
  using VSpriteWindow::VSpriteWindow;
  std::string_view typeName() const override { return "VObjectWindow"; }
  void postChildrenInit(const ui::Style& style) override;
  // The sprite window's, then `lines=drawn/total` of the wrapped text the objects hold (nothing when there is none).
  std::string detail() const override;
};

// The odometer. `numDigits` digits (VDigit, named digit_1, digit_10, ...) laid out right to left from the window's width, the
// `digit` table giving their font and size; `context` picks which managed value it shows.
class VRegister : public VSpriteWindow {
 public:
  using VSpriteWindow::VSpriteWindow;
  std::string_view typeName() const override { return "VRegister"; }
  void postChildrenInit(const ui::Style& style) override;
  // Recomputes the value; the digits are redrawn only when it changed.
  bool onAnimate() override;
  std::string detail() const override;

  // The value for the register's context: endless (mode != 0) shows current_level_total; story shows current_worth for
  // `endlevel`, current_worth + current_level_total for `prelevel`, and current_level_total for anything else.
  int valueNow() const;
  int shownValue() const { return value_; }
  const std::string& context() const { return context_; }
  const std::vector<std::shared_ptr<Digit>>& digits() const { return digits_; }
  // UpdateDigits: digit i is visible when i is 0 or value >= 10^i, and shows (value % 10^(i+1) - value % 10^i) / 10^i.
  void updateDigits();
  // Sets the value directly (tests, and the first draw).
  void setValue(int v) {
    value_ = v;
    updateDigits();
  }

 private:
  int numDigits_ = 1;
  int value_ = -1;
  std::string context_;
  std::vector<std::shared_ptr<Digit>> digits_;
};

}  // namespace choco::games::greatcc
