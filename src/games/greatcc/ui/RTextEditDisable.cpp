#include "games/greatcc/ui/RTextEditDisable.h"

#include "games/greatcc/ui/RDisableButton.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {
std::string trimmed(const std::string& s) {
  const char* ws = " \t\r\n";
  const std::size_t a = s.find_first_not_of(ws);
  if (a == std::string::npos) return {};
  return s.substr(a, s.find_last_not_of(ws) - a + 1);
}
}  // namespace

bool textEditDisablesButton(const std::string& text, int type, int ch, bool handled) {
  const std::string t = trimmed(text);
  const bool enabled = (!t.empty() || handled) && (t.size() > 1 || type != 1) && (!t.empty() || ch != ' ');
  return !enabled;
}

void RTextEditDisable::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  button_ = style.stringOr("disable");
  setPreEditHook([this](int type, int ch, bool accepted) { handleKeyHit(type, ch, accepted); });
  handleKeyHit(1, 8, true);
}

void RTextEditDisable::handleKeyHit(int type, int ch, bool handled) {
  ui::Window* modal = manager().topModal();
  auto* button = modal ? dynamic_cast<RDisableButton*>(modal->find(button_)) : nullptr;
  if (button) button->toggleState(textEditDisablesButton(text(), type, ch, handled));
}

std::string RTextEditDisable::detail() const { return TextEditWindow::detail() + " disable=\"" + button_ + "\""; }

}  // namespace choco::games::greatcc
