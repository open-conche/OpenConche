#pragma once

// BLayeredWindow (docs/re/greatcc-ui-library.md 9.6): a TLayeredWindow of three layers that shows layer 0 once its children
// exist. The Lua picks layers with SelectLayer(n) while it builds the children.

#include "ui/widgets/Widgets.h"

namespace choco::games::greatcc {

class BLayeredWindow : public ui::LayeredWindow {
 public:
  explicit BLayeredWindow(ui::WindowManager& wm) : LayeredWindow(wm, 3) {}
  std::string_view typeName() const override { return "BLayeredWindow"; }
  void postChildrenInit(const ui::Style& style) override {
    Window::postChildrenInit(style);
    setCurrentLayer(0);
  }
  std::string detail() const override { return " layer=" + std::to_string(currentLayer()); }
};

}  // namespace choco::games::greatcc
