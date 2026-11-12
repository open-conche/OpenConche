#include "games/greatcc/ui/RDisableButton.h"

namespace choco::games::greatcc {

void RDisableButton::postChildrenInit(const ui::Style& style) {
  ButtonWindow::postChildrenInit(style);
  if (style.booleanOr("disabled", false)) toggleState(true);
}

void RDisableButton::toggleState(bool disabled) {
  disabled_ = disabled;
  if (disabled_) setCurrentLayer(kDisabledLayer);
  else ButtonWindow::refreshLayer();
}

void RDisableButton::refreshLayer() {
  if (disabled_) setCurrentLayer(kDisabledLayer);
  else ButtonWindow::refreshLayer();
}

// Disabled: the press is swallowed and the disabled look stays. Enabled: the Button's press, then layer 1 (the pressed look
// is layer 1 for every kind of button, here).
bool RDisableButton::onMouseDown(int x, int y) {
  if (disabled_) {
    setCurrentLayer(kDisabledLayer);
    return true;
  }
  ButtonWindow::onMouseDown(x, y);
  setCurrentLayer(1);
  return true;
}

// The Button's release (which presses it) only when enabled; afterwards a normal or hovered button shows layer 2 (3 when
// disabled). A button with the pointer off it keeps the layer of its state.
bool RDisableButton::onMouseUp(int x, int y) {
  if (!disabled_) ButtonWindow::onMouseUp(x, y);
  if (mouseState() == kNormal || mouseState() == kOver) {
    if (disabled_) setCurrentLayer(kDisabledLayer);
    else if (mouseState() == kOver) setCurrentLayer(2);
  }
  return true;
}

bool RDisableButton::onMouseMove(int x, int y) {
  const bool r = ButtonWindow::onMouseMove(x, y);
  if (disabled_) setCurrentLayer(kDisabledLayer);
  else if (mouseState() == kOver) setCurrentLayer(2);
  return r;
}

void RDisableButton::onMouseLeave() {
  ButtonWindow::onMouseLeave();
  if (disabled_) setCurrentLayer(kDisabledLayer);
}

bool RDisableButton::doCommand() {
  if (disabled_) return false;
  return ButtonWindow::doCommand();
}

std::string RDisableButton::detail() const { return ButtonWindow::detail() + (disabled_ ? " disabled" : ""); }

}  // namespace choco::games::greatcc
