#pragma once

// The natives over the settings, user, value, level and achievement state (docs/design/greatcc.md 3.1, slice 4). Each
// binds over the logged stub (and, for the user natives, over the engine's SDK version) of the same name. They resolve the
// world per call and answer the documented default when the context has none.

#include "script/Bind.h"

namespace choco::games::greatcc {

void bindSettingsNatives(lua_State* L, script::ScriptContext& ctx);     // TSettings
void bindValueNatives(lua_State* L, script::ScriptContext& ctx);        // VValueManager
void bindLevelNatives(lua_State* L, script::ScriptContext& ctx);        // VLevelManager
void bindAchievementNatives(lua_State* L, script::ScriptContext& ctx);  // VAchievementManager
void bindGameStateNatives(lua_State* L, script::ScriptContext& ctx);    // TGameState
void bindSoundNatives(lua_State* L, script::ScriptContext& ctx);        // VSoundManager, the volume natives, button sounds

namespace detail {
// A typed native (script/Bind.h) that replaces whatever is bound under `name`.
template <auto Fn>
void rebind(lua_State* L, script::ScriptContext& ctx, const char* name) {
  script::rebindRaw(L, ctx, name, &script::detail::trampoline<Fn>);
}
}  // namespace detail

}  // namespace choco::games::greatcc
