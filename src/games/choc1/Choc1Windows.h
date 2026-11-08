#pragma once

// Chocolatier 1's window-tree natives (docs/re/choc1-natives.md section 2): the exe's `Util*` helpers walk the whole tree
// below the screen root, so here they look through the whole modal chain (a dialog over a view leaves the view's windows
// reachable). SimpleAnimation is in games/choc1/SimpleAnimation.h.

#include <functional>
#include <string>

#include "script/Bind.h"

namespace choco::ui {
class Window;
class WindowManager;
}

namespace choco::script {

// Calls `fn` for every window named `name` in every modal of the chain, the top modal first and each tree in pre-order.
void forEachNamedInChain(ui::WindowManager& wm, const std::string& name, const std::function<void(ui::Window&)>& fn);

// UtilEnableWindow, UtilDisableWindow, UtilSetButtonOn, UtilSetLedgerTab, UtilSetGlobalString, UtilSetTextColor,
// UtilPositionWindow, UtilPulseImage, SetUpdateRate, SetMoneyDisplayTarget, SetMoneyDisplay.
void registerChoc1WindowNatives(lua_State* L, ScriptContext& ctx);

}  // namespace choco::script
