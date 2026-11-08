#include "script/Messages.h"

#include <cstring>
#include <new>

namespace choco::script {

namespace {

constexpr const char* kMessageMeta = "choco.Message";

void getMessageMeta(lua_State* L) {  // 5.0 has no luaL_getmetatable
  lua_pushstring(L, kMessageMeta);
  lua_rawget(L, LUA_REGISTRYINDEX);
}

int messageIndex(lua_State* L) {
  auto* m = static_cast<ui::Message*>(lua_touserdata(L, 1));
  const char* key = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : nullptr;
  if (m && key && std::strcmp(key, "type") == 0) {
    lua_pushnumber(L, m->type);
  } else if (m && key && std::strcmp(key, "name") == 0) {
    lua_pushlstring(L, m->name.data(), m->name.size());
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int messageGc(lua_State* L) {
  auto* m = static_cast<ui::Message*>(lua_touserdata(L, 1));
  if (m) m->~Message();
  return 0;
}

}  // namespace

void registerMessageType(lua_State* L) {
  luaL_newmetatable(L, kMessageMeta);
  lua_pushstring(L, "__index");
  lua_pushcfunction(L, messageIndex);
  lua_rawset(L, -3);
  lua_pushstring(L, "__gc");
  lua_pushcfunction(L, messageGc);
  lua_rawset(L, -3);
  lua_pop(L, 1);
}

void pushMessage(lua_State* L, const ui::Message& m) {
  void* mem = lua_newuserdata(L, sizeof(ui::Message));
  new (mem) ui::Message(m);
  getMessageMeta(L);
  lua_setmetatable(L, -2);
}

ui::Message* toMessage(lua_State* L, int index) {
  if (lua_type(L, index) != LUA_TUSERDATA || !lua_getmetatable(L, index)) return nullptr;
  getMessageMeta(L);
  bool same = lua_rawequal(L, -1, -2) != 0;
  lua_pop(L, 2);
  return same ? static_cast<ui::Message*>(lua_touserdata(L, index)) : nullptr;
}

}  // namespace choco::script
