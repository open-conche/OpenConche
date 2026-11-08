#pragma once

#include "script/Bind.h"

namespace choco::script {

// Chocolatier 2's LogoEditor window and its natives (docs/re/choc2-logo-and-trade.md 1.5): SetLogoText, GetLogoString,
// RandomizeLogo, SetTextPosition, SetGraphicPosition, SetTextColor, ToggleBold, ToggleItalic, ToggleOutline and
// Set{Left,Center,Right}Justify. They act on the LogoEditor of the top-most modal that has one. Called from
// registerChoc2Natives after the stub rows, so these replace the stubs of the same names.
void registerChoc2LogoNatives(lua_State* L, ScriptContext& ctx);

}  // namespace choco::script
