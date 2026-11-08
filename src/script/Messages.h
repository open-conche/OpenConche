#pragma once

#include "script/LuaApi.h"
#include "ui/Message.h"

namespace choco::script {

// Message objects are userdata holding a ui::Message by value. Their metatable's __index exposes only `type` (number)
// and `name` (string); every other key is nil. No Lua identity is kept for posted messages.
void registerMessageType(lua_State* L);
void pushMessage(lua_State* L, const ui::Message& m);
// Null when the value is not a message.
ui::Message* toMessage(lua_State* L, int index);

}  // namespace choco::script
