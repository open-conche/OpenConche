#pragma once

#include "games/common/OptionsPanel.h"

namespace choco::game::choc1 {

// OptionsDialog (TOptions, vtable 004e8844, PostInit 004186b0, message handler
// 00418c60, SaveVolumes 00418a10): Chocolatier 1's ui/options.lua container. It
// is the shared game::OptionsPanel under the Lua type name `OptionsDialog`; the
// checks that make that reuse sound are in docs/re/choc1-options-credits.md
// (same child names, same prefs, same live slider handling, defaults 750 from
// the profile's volume defaults).
class OptionsDialog : public OptionsPanel {
public:
  OptionsDialog(ui::WindowManager &wm, script::ScriptContext &ctx)
      : OptionsPanel(wm, ctx, "OptionsDialog") {}
};

} // namespace choco::game::choc1
