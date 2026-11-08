#pragma once

struct lua_State;

namespace choco::script {

struct ScriptContext;

// Date and DayOfYear (bound through GameModule::registerCoreExtras).
void registerCalendarNatives(lua_State* L, ScriptContext& ctx);
// LoadIngredients, LoadProducts, LoadPortFileList and LoadQuestFileList (bound through GameModule::registerContentExtras).
void registerContentLoaderNatives(lua_State* L, ScriptContext& ctx);
// The PortWindow and CharWindow types, PausePortAnimations and SetMood (no-ops while no such window exists).
void registerPortNatives(lua_State* L, ScriptContext& ctx);
// The ledger, movies and score submission natives, and the game's window types (the World comes from the module's attach).
void registerGameNatives(lua_State* L, ScriptContext& ctx);

// InitiateTravel, PauseTravel and ResumeTravel. They resolve the MapWindow when called and do nothing without one.
void registerMapNatives(lua_State* L, ScriptContext& ctx);
void registerCratesNatives(lua_State* L, ScriptContext& ctx);
void registerFactoryNatives(lua_State* L, ScriptContext& ctx);

}  // namespace choco::script
