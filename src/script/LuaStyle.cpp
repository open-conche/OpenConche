#include "script/LuaStyle.h"

namespace choco::script {

namespace {

// Protected t[k]: a metamethod that raises must not longjmp through C++ frames. Stack: t, k -> value.
int protectedGet(lua_State* L) {
  lua_gettable(L, 1);
  return 1;
}

// Pushes t[key] and returns true when it is not nil; pushes nothing otherwise. `t` is an absolute index.
bool getField(lua_State* L, int t, const char* key) {
  lua_pushcfunction(L, protectedGet);
  lua_pushvalue(L, t);
  lua_pushstring(L, key);
  if (lua_pcall(L, 2, 1, 0) != 0) {
    lua_pop(L, 1);
    return false;
  }
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  return true;
}

constexpr int kMaxParentDepth = 64;  // AppendStyle can make a style its own ancestor

}  // namespace

LuaStyle::LuaStyle(lua_State* L, int tableIndex, LuaState* state)
    : L_(L), table_(tableIndex < 0 && tableIndex > LUA_REGISTRYINDEX ? lua_gettop(L) + tableIndex + 1 : tableIndex), state_(state) {}

LuaStyle::LuaStyle(lua_State* L, int tableIndex, LuaState* state, bool plain, bool owns)
    : L_(L), table_(tableIndex), state_(state), plain_(plain), owns_(owns) {}

LuaStyle::~LuaStyle() {
  if (owns_) lua_settop(L_, table_ - 1);
}

LuaRef LuaStyle::tableRef() const { return state_ ? LuaRef(*state_, L_, table_) : LuaRef(); }

bool LuaStyle::lookup(const char* key) const {
  if (getField(L_, table_, key)) return true;
  if (plain_) return false;
  lua_getglobal(L_, "gCurrentDefaultStyle");
  for (int depth = 0; depth < kMaxParentDepth && lua_istable(L_, -1); ++depth) {
    const int style = lua_gettop(L_);
    if (getField(L_, style, key)) {
      lua_remove(L_, style);  // leave only the value
      return true;
    }
    if (!getField(L_, style, "parent")) break;
    lua_remove(L_, style);
  }
  lua_settop(L_, lua_gettop(L_) - 1);  // the style (or non-table) left on top
  return false;
}

std::optional<double> LuaStyle::number(const char* key) const {
  if (!lookup(key)) return std::nullopt;
  std::optional<double> out;
  if (lua_isnumber(L_, -1)) out = lua_tonumber(L_, -1);
  lua_pop(L_, 1);
  return out;
}

std::optional<std::string> LuaStyle::string(const char* key) const {
  if (!lookup(key)) return std::nullopt;
  std::optional<std::string> out;
  if (lua_type(L_, -1) == LUA_TSTRING || lua_type(L_, -1) == LUA_TNUMBER) {
    const char* s = lua_tostring(L_, -1);
    out = std::string(s, lua_strlen(L_, -1));
  }
  lua_pop(L_, 1);
  return out;
}

std::optional<bool> LuaStyle::boolean(const char* key) const {
  if (!lookup(key)) return std::nullopt;
  bool out = false;  // strings and other types read as false (PopBool)
  if (lua_type(L_, -1) == LUA_TBOOLEAN) out = lua_toboolean(L_, -1) != 0;
  else if (lua_type(L_, -1) == LUA_TNUMBER) out = lua_tonumber(L_, -1) != 0;
  lua_pop(L_, 1);
  return out;
}

std::optional<std::vector<double>> LuaStyle::numbers(const char* key, int count, double def) const {
  if (!lookup(key)) return std::nullopt;
  if (!lua_istable(L_, -1)) {
    lua_pop(L_, 1);
    return std::nullopt;
  }
  const int t = lua_gettop(L_);
  std::vector<double> out(static_cast<std::size_t>(count), def);
  for (int i = 0; i < count; ++i) {
    lua_pushnumber(L_, i + 1);
    lua_rawget(L_, t);  // raw: a metamethod must not run (and raise) inside a native
    if (lua_type(L_, -1) == LUA_TNUMBER) out[static_cast<std::size_t>(i)] = lua_tonumber(L_, -1);
    lua_pop(L_, 1);
  }
  lua_pop(L_, 1);
  return out;
}

std::optional<std::vector<std::string>> LuaStyle::strings(const char* key, int count) const {
  if (!lookup(key)) return std::nullopt;
  if (!lua_istable(L_, -1)) {
    lua_pop(L_, 1);
    return std::nullopt;
  }
  const int t = lua_gettop(L_);
  std::vector<std::string> out(static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    lua_pushnumber(L_, i + 1);
    lua_rawget(L_, t);
    if (lua_type(L_, -1) == LUA_TSTRING) out[static_cast<std::size_t>(i)].assign(lua_tostring(L_, -1), lua_strlen(L_, -1));
    lua_pop(L_, 1);
  }
  lua_pop(L_, 1);
  return out;
}

std::optional<LuaRef> LuaStyle::function(const char* key) const {
  if (!lookup(key)) return std::nullopt;
  std::optional<LuaRef> out;
  if (state_ && lua_isfunction(L_, -1)) out.emplace(*state_, L_, -1);
  lua_pop(L_, 1);
  return out;
}


// Pushes key[index] (raw) of a table-valued key and returns true when it is not nil; pushes nothing otherwise.
bool LuaStyle::lookupIndexed(const char* key, int index) const {
  if (!lookup(key)) return false;
  if (!lua_istable(L_, -1)) {
    lua_pop(L_, 1);
    return false;
  }
  lua_pushnumber(L_, index);
  lua_rawget(L_, -2);
  lua_remove(L_, -2);
  if (lua_isnil(L_, -1)) {
    lua_pop(L_, 1);
    return false;
  }
  return true;
}

std::optional<std::string> LuaStyle::tableString(const char* key, int index) const {
  if (!lookupIndexed(key, index)) return std::nullopt;
  std::optional<std::string> out;
  if (lua_type(L_, -1) == LUA_TSTRING || lua_type(L_, -1) == LUA_TNUMBER) out = std::string(lua_tostring(L_, -1), lua_strlen(L_, -1));
  lua_pop(L_, 1);
  return out;
}

std::optional<std::vector<double>> LuaStyle::tableNumbers(const char* key, int index, int count, double def) const {
  if (!lookupIndexed(key, index)) return std::nullopt;
  if (!lua_istable(L_, -1)) {
    lua_pop(L_, 1);
    return std::nullopt;
  }
  const int t = lua_gettop(L_);
  std::vector<double> out(static_cast<std::size_t>(count), def);
  for (int i = 0; i < count; ++i) {
    lua_pushnumber(L_, i + 1);
    lua_rawget(L_, t);
    if (lua_type(L_, -1) == LUA_TNUMBER) out[static_cast<std::size_t>(i)] = lua_tonumber(L_, -1);
    lua_pop(L_, 1);
  }
  lua_pop(L_, 1);
  return out;
}

// ---- nested views ----

namespace {

// Pushes t[i] (gettable, so metatables apply) and returns true when it is not nil.
bool getIndex(lua_State* L, int t, int i) {
  lua_pushcfunction(L, protectedGet);
  lua_pushvalue(L, t);
  lua_pushnumber(L, i);
  if (lua_pcall(L, 2, 1, 0) != 0 || lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  return true;
}

}  // namespace

std::unique_ptr<const ui::Style> LuaStyle::table(const char* key) const {
  if (!lookup(key)) return nullptr;
  if (!lua_istable(L_, -1)) {
    lua_pop(L_, 1);
    return nullptr;
  }
  return std::unique_ptr<const ui::Style>(new LuaStyle(L_, lua_gettop(L_), state_, true, true));
}

int LuaStyle::size() const { return luaL_getn(L_, table_); }

std::unique_ptr<const ui::Style> LuaStyle::element(int i) const {
  if (!getIndex(L_, table_, i)) return nullptr;
  if (!lua_istable(L_, -1)) {
    lua_pop(L_, 1);
    return nullptr;
  }
  return std::unique_ptr<const ui::Style>(new LuaStyle(L_, lua_gettop(L_), state_, true, true));
}

std::optional<double> LuaStyle::numberAt(int i) const {
  if (!getIndex(L_, table_, i)) return std::nullopt;
  std::optional<double> out;
  if (lua_isnumber(L_, -1)) out = lua_tonumber(L_, -1);
  lua_pop(L_, 1);
  return out;
}

std::optional<std::string> LuaStyle::stringAt(int i) const {
  if (!getIndex(L_, table_, i)) return std::nullopt;
  std::optional<std::string> out;
  if (lua_type(L_, -1) == LUA_TSTRING || lua_type(L_, -1) == LUA_TNUMBER) out = std::string(lua_tostring(L_, -1), lua_strlen(L_, -1));
  lua_pop(L_, 1);
  return out;
}

void LuaStyle::forEachTable(const std::function<void(const ui::Style&)>& fn) const {
  // lua_next on the raw table: the array part first, then the hash, as pairs() does.
  lua_pushnil(L_);
  while (lua_next(L_, table_) != 0) {
    if (lua_istable(L_, -1)) {
      const LuaStyle view(L_, lua_gettop(L_), state_, true, false);
      fn(view);
    }
    lua_pop(L_, 1);  // the value; the key stays for lua_next
  }
}

void LuaStyle::forEachNumber(const std::function<void(const std::string&, double)>& fn) const {
  lua_pushnil(L_);
  while (lua_next(L_, table_) != 0) {
    // Only a string key is read as one: lua_tostring on a number key would convert it in place and confuse lua_next.
    if (lua_type(L_, -2) == LUA_TSTRING && lua_type(L_, -1) == LUA_TNUMBER) fn(lua_tostring(L_, -2), lua_tonumber(L_, -1));
    lua_pop(L_, 1);
  }
}

void LuaStyle::forEachEntry(const EntryFn& fn) const {
  lua_pushnil(L_);
  while (lua_next(L_, table_) != 0) {
    // Only a string key is read as one (lua_tostring on a number key would convert it in place and confuse lua_next).
    if (lua_type(L_, -2) == LUA_TSTRING) {
      const std::string key = lua_tostring(L_, -2);
      if (lua_istable(L_, -1)) {
        const LuaStyle view(L_, lua_gettop(L_), state_, true, false);
        fn(key, &view, std::nullopt);
      } else if (lua_type(L_, -1) == LUA_TSTRING || lua_type(L_, -1) == LUA_TNUMBER) {
        // A number value is converted on a copy, so the stack's value stays a number.
        lua_pushvalue(L_, -1);
        const std::string text(lua_tostring(L_, -1), lua_strlen(L_, -1));
        lua_pop(L_, 1);
        fn(key, nullptr, text);
      } else {
        fn(key, nullptr, std::nullopt);
      }
    }
    lua_pop(L_, 1);
  }
}

}  // namespace choco::script
