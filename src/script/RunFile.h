#pragma once

#include <string>

#include "script/LuaApi.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::script {

// Runs a Lua file with `target` as the window being built: the window goes on the creation stack, the file runs through
// dofile (so the asset root, the "assets/" prefix and the script-load notes apply), and the creation stack and the Lua
// stack are restored afterwards, also when the file raised an error (logged through the host when there is one). FillWindow,
// the factory screens, the Chocolatier 3 ledger and Chocolatier 2's DynamicWindow all fill a window this way. The caller
// removes the old children first when the window is being refilled.
void runFileInto(ScriptContext& ctx, lua_State* L, ui::Window& target, const std::string& path);

}  // namespace choco::script
