#pragma once

// RTextEditDisable (docs/re/greatcc-ui-library.md 5.3): a TextEdit that keeps an RDisableButton (key `disable`, the name of a
// window in the top modal) enabled only while the name can be accepted. It judges every key before the edit applies, so it sees
// the text as it was.

#include <string>

#include "ui/widgets/InputWidgets.h"

namespace choco::games::greatcc {

// HandleKeyHit's rule: true when the button is to be disabled. `text` is the field's text before the key (it is trimmed
// here), `type` the key type (1 is an edit key such as Backspace), `ch` the character code and `handled` whether the edit
// accepted the key. The button stays enabled only when all of these hold: the text is not empty or the key was accepted; the
// text is longer than one character or the key is not type 1; the text is not empty or the key is not a space.
bool textEditDisablesButton(const std::string& text, int type, int ch, bool handled);

class RTextEditDisable : public ui::TextEditWindow {
 public:
  explicit RTextEditDisable(ui::WindowManager& wm) : TextEditWindow(wm) {}
  std::string_view typeName() const override { return "RTextEditDisable"; }
  // Keys, after the TextEdit's: disable (the button's name, default ""). Evaluates a Backspace the edit accepted once, for the
  // initial label: the button is enabled exactly when the trimmed label is longer than one character.
  void postChildrenInit(const ui::Style& style) override;
  std::string detail() const override;

  const std::string& buttonName() const { return button_; }

 private:
  void handleKeyHit(int type, int ch, bool handled);
  std::string button_;
};

}  // namespace choco::games::greatcc
