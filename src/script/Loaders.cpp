#include "script/Loaders.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace choco::script {

int loadBuffer(lua_State* L, std::string_view code, const char* chunkName) {
  std::string name = std::string("@") + chunkName;
  // luaL_loadfile skips a UTF-8 byte order mark and a first line starting with '#'; do the same.
  if (code.size() >= 3 && code.compare(0, 3, "\xEF\xBB\xBF") == 0) code.remove_prefix(3);
  std::string patched;
  if (!code.empty() && code[0] == '#') {
    auto nl = code.find('\n');
    patched = nl == std::string_view::npos ? std::string() : std::string(code.substr(nl));  // keep the line count
    code = patched;
  }
  return luaL_loadbuffer(L, code.data(), code.size(), name.c_str());
}

int loadChunk(lua_State* L, ScriptContext& ctx, std::string_view name) {
  auto bytes = ctx.fs.readFile(name);
  if (!bytes) {
    // ReadFile does dofile('assets/' .. name): a leading "assets/" names the asset root.
    std::string key = assets::AssetFS::normalize(name);
    if (key.compare(0, 7, "assets/") == 0) bytes = ctx.fs.readFile(key.substr(7));
  }
  if (!bytes) {
    std::string msg = "cannot open " + std::string(name);
    lua_pushlstring(L, msg.data(), msg.size());
    return LUA_ERRFILE;
  }
  return loadBuffer(L, std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()),
                    std::string(name).c_str());
}

namespace {

// The helpers below build C++ objects, so they return before any Lua call that can raise, and they are never inlined
// into a function that raises: a Lua error is a longjmp, and MSVC's longjmp unwinds C++ frames by their exception
// state, which /EHsc leaves stale across an extern "C" call. A destructor then runs twice and the raise crashes.
#if defined(_MSC_VER)
#define CHOCO_NOINLINE __declspec(noinline)
#else
#define CHOCO_NOINLINE __attribute__((noinline))
#endif

CHOCO_NOINLINE int loadNamed(lua_State* L, int argIndex) {
  size_t len = 0;
  const char* name = luaL_checklstring(L, argIndex, &len);
  return loadChunk(L, contextOf(L), std::string_view(name, len));
}

int luaDofile(lua_State* L) {
  int n = lua_gettop(L);
  int status = loadNamed(L, 1);
  if (status != 0) return lua_error(L);
  contextOf(L).wm.noteScriptLoad(lua_tostring(L, 1));
  lua_call(L, 0, LUA_MULTRET);
  return lua_gettop(L) - n;
}

int luaLoadfile(lua_State* L) {
  int status = loadNamed(L, 1);
  if (status == 0) return 1;
  lua_pushnil(L);
  lua_insert(L, -2);
  return 2;
}

// ---- require: a port of luaB_require from Lua 5.0.2 ----
constexpr const char* kReqTab = "_LOADED";
constexpr const char* kPathGlobal = "LUA_PATH";
constexpr const char* kPathDefault = "?;?.lua";

const char* getPath(lua_State* L) {
  lua_getglobal(L, kPathGlobal);
  const char* path = lua_tostring(L, -1);
  lua_pop(L, 1);  // the string stays alive in the global
  return path ? path : kPathDefault;
}

const char* pushNextPath(lua_State* L, const char* path) {
  if (*path == '\0') return nullptr;
  if (*path == ';') path++;
  const char* l = std::strchr(path, ';');
  if (l == nullptr) l = path + std::strlen(path);
  lua_pushlstring(L, path, static_cast<size_t>(l - path));
  return l;
}

void pushComposeName(lua_State* L, const char* name) {
  const char* path = lua_tostring(L, -1);
  const char* wild;
  int n = 1;
  while ((wild = std::strchr(path, '?')) != nullptr) {
    luaL_checkstack(L, 3, "too many marks in a path component");
    lua_pushlstring(L, path, static_cast<size_t>(wild - path));
    lua_pushstring(L, name);
    path = wild + 1;
    n += 2;
  }
  lua_pushstring(L, path);
  lua_concat(L, n);
}

// Finds and loads the package; leaves the loader (or an error message) on the stack. No C++ objects live across it.
int requireLoad(lua_State* L, const char* path, const char* name) {
  int status = LUA_ERRFILE;
  while (status == LUA_ERRFILE) {
    lua_settop(L, 3);
    if ((path = pushNextPath(L, path)) == nullptr) break;
    pushComposeName(L, name);
    status = loadNamed(L, lua_gettop(L));  // the composed name is on top
  }
  return status;
}

// The search with the `.lua` retry. Deliberate deviation from 5.0.2 (docs/re/sweep-findings.md section 1): the
// original has no `.lua` suffix logic, but Reforged's catalogue History panel requires bare names. If every template
// missed and the name has no `.lua`, retry with it appended. `_LOADED` and `_REQUIREDNAME` keep the original name;
// this only turns errors into successes. Its own frame (see CHOCO_NOINLINE): luaRequire raises.
CHOCO_NOINLINE int requireLoadAny(lua_State* L, const char* path) {
  int status = requireLoad(L, path, lua_tostring(L, 1));
  if (status == LUA_ERRFILE) {
    const std::string name = lua_tostring(L, 1);
    if (name.size() < 4 || name.compare(name.size() - 4, 4, ".lua") != 0) {
      const std::string alt = name + ".lua";
      status = requireLoad(L, path, alt.c_str());
      if (status == 0) contextOf(L).log.write("[script] require '" + name + "' resolved as '" + alt + "' (.lua fallback)");
    }
  }
  return status;
}

// Holds no C++ object: it raises.
int luaRequire(lua_State* L) {
  int status = LUA_ERRFILE;
  luaL_checkstring(L, 1);
  lua_settop(L, 1);
  lua_getglobal(L, kReqTab);
  if (!lua_istable(L, 2)) return luaL_error(L, "`%s' is not a table", kReqTab);
  const char* path = getPath(L);
  lua_pushvalue(L, 1);
  lua_rawget(L, 2);
  if (lua_toboolean(L, -1)) return 1;  // already loaded
  status = requireLoadAny(L, path);
  switch (status) {
    case 0: {
      lua_getglobal(L, "_REQUIREDNAME");
      lua_insert(L, -2);
      lua_pushvalue(L, 1);
      lua_setglobal(L, "_REQUIREDNAME");
      lua_call(L, 0, 1);
      lua_insert(L, -2);
      lua_setglobal(L, "_REQUIREDNAME");
      if (lua_isnil(L, -1)) {
        lua_pushboolean(L, 1);
        lua_replace(L, -2);
      }
      lua_pushvalue(L, 1);
      lua_pushvalue(L, -2);
      lua_rawset(L, 2);
      return 1;
    }
    case LUA_ERRFILE:
      return luaL_error(L, "could not load package `%s' from path `%s'", lua_tostring(L, 1), getPath(L));
    default:
      return luaL_error(L, "error loading package `%s' (%s)", lua_tostring(L, 1), lua_tostring(L, -1));
  }
}

// ---- logging ----
std::string describe(lua_State* L, int idx) {
  switch (lua_type(L, idx)) {
    case LUA_TSTRING:
    case LUA_TNUMBER: return std::string(lua_tostring(L, idx), lua_strlen(L, idx));
    case LUA_TNIL: return "nil";
    case LUA_TBOOLEAN: return lua_toboolean(L, idx) ? "true" : "false";
    default: return std::string("<") + lua_typename(L, lua_type(L, idx)) + ">";
  }
}

int luaDebugOut(lua_State* L) {
  ScriptContext& ctx = contextOf(L);
  std::string line = "[debug]";
  int n = lua_gettop(L);
  for (int i = 1; i <= n; ++i) {
    line += ' ';
    line += describe(L, i);
  }
  ctx.log.write(line);
  return 0;
}

int luaAlert(lua_State* L) {
  ScriptContext& ctx = contextOf(L);
  std::string msg = lua_gettop(L) >= 1 ? describe(L, 1) : std::string();
  while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.pop_back();
  ctx.log.write("[lua-error] " + msg);
  return 0;
}

}  // namespace

void registerLoaders(lua_State* L, ScriptContext& ctx) {
  bindRaw(L, ctx, "dofile", luaDofile);
  bindRaw(L, ctx, "loadfile", luaLoadfile);
  bindRaw(L, ctx, "require", luaRequire);
}

void registerLogging(lua_State* L, ScriptContext& ctx) {
  bindRaw(L, ctx, "_ALERT", luaAlert);
  bindRaw(L, ctx, "DebugOut", luaDebugOut);
}

}  // namespace choco::script
