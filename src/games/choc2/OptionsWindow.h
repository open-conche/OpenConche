#pragma once

#include "games/common/OptionsPanel.h"

namespace choco::game::choc2 {

// Options (Chocolatier 2): the container of ui/options.lua. It is the shared
// game::OptionsPanel (docs/re/choc1-natives.md section 9: Chocolatier 1's
// TOptions is the same class) built under the Lua type name `Options`.
class Options : public OptionsPanel {
public:
  Options(ui::WindowManager &wm, script::ScriptContext &ctx)
      : OptionsPanel(wm, ctx, "Options") {}
};

} // namespace choco::game::choc2
