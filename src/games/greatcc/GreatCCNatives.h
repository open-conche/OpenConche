#pragma once

// The Great Chocolate Chase's script surface (docs/design/greatcc.md 3.3): the rows of docs/re/api-greatcc.tsv through
// generated/Stubs_greatcc.inc, TSlider, and the Lua text the transition manager defines. Nothing here is inherited from
// Chocolatier 3.

#include <cstddef>

#include "script/Bind.h"

namespace choco::script {

// Every function name of the table (the generated API_NAME rows): after boot each is a Lua function of the main state.
const char* const* greatccApiNames(std::size_t& count);

// Boot step 1: a logged stub for every function row; real natives and the prelude bind over them later.
void registerGreatccStubs(lua_State* L, ScriptContext& ctx);
// Boot step 6: the window types of the table (placeholders until a class exists) and TSlider, a second name of Slider.
void registerGreatccWindowTypes(lua_State* L, ScriptContext& ctx);
// Boot step 9: DoTransition, the Lua text RTransitionManager's constructor runs (docs/re/greatcc-ui-library.md 3.1).
void registerGreatccGlobals(lua_State* L, ScriptContext& ctx);

}  // namespace choco::script
