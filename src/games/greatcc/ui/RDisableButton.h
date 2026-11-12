#pragma once

// RDisableButton (docs/re/greatcc-ui-library.md 5.1): a Button that can be switched off. Disabled, it shows layer 3 whatever the
// mouse does, runs nothing when pressed (neither its command nor the message and close of a press), and still swallows clicks.

#include "ui/widgets/Widgets.h"

namespace choco::games::greatcc {

class RDisableButton : public ui::ButtonWindow {
 public:
  static constexpr int kDisabledLayer = 3;

  explicit RDisableButton(ui::WindowManager& wm) : ButtonWindow(wm) {}
  std::string_view typeName() const override { return "RDisableButton"; }
  // Keys, after the Button's own (the `command` stays the Button's): disabled (false).
  void postChildrenInit(const ui::Style& style) override;
  bool onMouseDown(int x, int y) override;
  bool onMouseUp(int x, int y) override;
  bool onMouseMove(int x, int y) override;
  void onMouseLeave() override;
  // A disabled button does nothing at all: no command, no close, no message (RDisableButtonAction::DoAction).
  bool doCommand() override;
  std::string detail() const override;

  bool disabled() const { return disabled_; }
  // ToggleState(disabled): sets the flag and shows layer 3, or re-applies the layer of the current state.
  void toggleState(bool disabled);

 protected:
  void refreshLayer() override;

 private:
  bool disabled_ = false;
};

}  // namespace choco::games::greatcc
