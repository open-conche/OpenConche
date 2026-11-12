#pragma once

// VUpsellWindow (docs/re/greatcc-meta-flow.md 7): the marketing screen of scripts/upsell.lua, shown only from the hidden debug
// button of the main menu. It is inert: a picture window. Its natives SetupUpsell and LaunchGameLink stay the logged stubs (the
// links are dead web addresses and the engine never opens a browser).

#include "games/greatcc/ui/RWindow.h"

namespace choco::games::greatcc {

class VUpsellWindow : public RWindow {
 public:
  explicit VUpsellWindow(ui::WindowManager& wm) : RWindow(wm) {}
  std::string_view typeName() const override { return "VUpsellWindow"; }
};

}  // namespace choco::games::greatcc
