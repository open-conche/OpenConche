#pragma once

// Typed binding of C++ functions as Lua natives (script-host design section 2).
//
// Rules for natives, because Lua 5.0 is built as C and its errors longjmp past C++ destructors:
//  1. Never call lua_error or luaL_check*.
//  2. Call back into Lua only through lua_pcall.
//  3. This helper catches every C++ exception at the boundary and returns no values.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "script/LuaApi.h"
#include "script/ScriptContext.h"

namespace choco::script {

// A view of a table argument's stack slot, valid only during the native call.
struct TableArg {
  lua_State* L = nullptr;
  int index = 0;
  bool valid() const { return L != nullptr; }
};

// {r,g,b[,a]} floats; a defaults to 1.
struct ColorArg {
  float r = 1, g = 1, b = 1, a = 1;
};

// Registers `fn` as the global `name` with the context and name as upvalues (for raw natives that yield).
void bindRaw(lua_State* L, ScriptContext& ctx, const char* name, lua_CFunction fn);
// bindRaw that does not duplicate the native-name list entry when `name` is already bound by another module.
void rebindRaw(lua_State* L, ScriptContext& ctx, const char* name, lua_CFunction fn);
ScriptContext& contextOf(lua_State* L);  // from inside a registered native
const char* nativeNameOf(lua_State* L);

namespace detail {

struct CallCtx {
  lua_State* L;
  ScriptContext* ctx;
  const char* name;
};

// "[lua-arg] Name arg N: expected X, got Y (file:line)", once per (native, arg, Lua type).
void warnArg(CallCtx& c, int idx, const char* expected);
void logNativeError(lua_State* L, ScriptContext& ctx, const char* name, const char* what);

template <class T, class = void>
struct Conv;

template <class T>
T clampTo(double d) {
  if (std::isnan(d)) return T{};
  constexpr double lo = static_cast<double>(std::numeric_limits<T>::lowest());
  constexpr double hi = static_cast<double>(std::numeric_limits<T>::max());
  if (d <= lo) return std::numeric_limits<T>::lowest();
  if (d >= hi) return std::numeric_limits<T>::max();
  return static_cast<T>(d);  // truncates toward zero
}

template <class T>
struct Conv<T, std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>>> {
  static T get(CallCtx& c, int idx) {
    if (lua_isnumber(c.L, idx)) {
      double d = lua_tonumber(c.L, idx);
      if constexpr (std::is_floating_point_v<T>) return static_cast<T>(d);
      else return clampTo<T>(d);
    }
    warnArg(c, idx, "number");
    return T{};
  }
};

template <>
struct Conv<bool> {
  static bool get(CallCtx& c, int idx) {
    switch (lua_type(c.L, idx)) {
      case LUA_TBOOLEAN: return lua_toboolean(c.L, idx) != 0;
      case LUA_TNUMBER: return lua_tonumber(c.L, idx) != 0;
      case LUA_TNIL:
      case LUA_TNONE: return false;
      default: warnArg(c, idx, "boolean"); return false;
    }
  }
};

template <>
struct Conv<std::string> {
  static std::string get(CallCtx& c, int idx) {
    int t = lua_type(c.L, idx);
    if (t == LUA_TSTRING || t == LUA_TNUMBER) {
      const char* s = lua_tostring(c.L, idx);
      return std::string(s, lua_strlen(c.L, idx));
    }
    if (t != LUA_TNIL && t != LUA_TNONE) warnArg(c, idx, "string");
    return {};
  }
};

template <class T>
struct Conv<std::optional<T>> {
  static std::optional<T> get(CallCtx& c, int idx) {
    if (lua_isnoneornil(c.L, idx)) return std::nullopt;
    return Conv<T>::get(c, idx);
  }
};

template <>
struct Conv<TableArg> {
  static TableArg get(CallCtx& c, int idx) {
    if (lua_istable(c.L, idx)) return {c.L, idx};
    warnArg(c, idx, "table");
    return {};
  }
};

template <>
struct Conv<LuaRef> {
  static LuaRef get(CallCtx& c, int idx) {
    if (lua_isfunction(c.L, idx) || lua_istable(c.L, idx)) return LuaRef(c.ctx->lua, c.L, idx);
    warnArg(c, idx, "function or table");
    return {};
  }
};

template <>
struct Conv<ColorArg> {
  static ColorArg get(CallCtx& c, int idx) {
    if (!lua_istable(c.L, idx)) {
      warnArg(c, idx, "table");
      return {};
    }
    auto comp = [&](int i, float def) {
      lua_rawgeti(c.L, idx, i);
      float v = lua_isnumber(c.L, -1) ? static_cast<float>(lua_tonumber(c.L, -1)) : def;
      lua_pop(c.L, 1);
      return v;
    };
    ColorArg col;
    col.r = comp(1, 0);
    col.g = comp(2, 0);
    col.b = comp(3, 0);
    col.a = comp(4, 1);
    return col;
  }
};

// ---- return values ----
template <class T>
struct IsTuple : std::false_type {};
template <class... T>
struct IsTuple<std::tuple<T...>> : std::true_type {};
template <class T>
struct IsOptional : std::false_type {};
template <class T>
struct IsOptional<std::optional<T>> : std::true_type {};

template <class T>
int pushResult(lua_State* L, const T& v) {
  if constexpr (std::is_same_v<T, bool>) {
    lua_pushboolean(L, v);
    return 1;
  } else if constexpr (std::is_arithmetic_v<T>) {
    lua_pushnumber(L, static_cast<lua_Number>(v));
    return 1;
  } else if constexpr (std::is_convertible_v<T, std::string_view>) {
    std::string_view sv = v;
    lua_pushlstring(L, sv.data(), sv.size());
    return 1;
  } else if constexpr (IsOptional<T>::value) {
    if (v) return pushResult(L, *v);
    lua_pushnil(L);
    return 1;
  } else if constexpr (IsTuple<T>::value) {
    constexpr int n = static_cast<int>(std::tuple_size_v<T>);
    lua_checkstack(L, n);
    std::apply([&](const auto&... e) { (pushResult(L, e), ...); }, v);
    return n;
  } else {
    static_assert(sizeof(T) == 0, "unsupported native return type");
  }
}

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4702)  // "unreachable code": the return after a native that never returns (a test native that throws)
#endif
template <auto Fn, class R, class... A, std::size_t... I>
int run(lua_State* L, ScriptContext& ctx, const char* name, R (*)(ScriptContext&, A...), std::index_sequence<I...>) {
  [[maybe_unused]] CallCtx cc{L, &ctx, name};  // unused when the native takes no arguments
  [[maybe_unused]] std::tuple<std::decay_t<A>...> args{Conv<std::decay_t<A>>::get(cc, static_cast<int>(I) + 1)...};
  if constexpr (std::is_void_v<R>) {
    Fn(ctx, static_cast<std::tuple_element_t<I, std::tuple<A...>>&&>(std::get<I>(args))...);
    return 0;
  } else {
    R r = Fn(ctx, static_cast<std::tuple_element_t<I, std::tuple<A...>>&&>(std::get<I>(args))...);
    return pushResult(L, r);
  }
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

template <auto Fn, class R, class... A>
int dispatch(lua_State* L, ScriptContext& ctx, const char* name, R (*f)(ScriptContext&, A...)) {
  return run<Fn>(L, ctx, name, f, std::index_sequence_for<A...>{});
}

template <auto Fn>
int trampoline(lua_State* L) {
  ScriptContext& ctx = contextOf(L);
  const char* name = nativeNameOf(L);
  try {
    return dispatch<Fn>(L, ctx, name, Fn);
  } catch (const std::exception& e) {
    logNativeError(L, ctx, name, e.what());
  } catch (...) {
    logNativeError(L, ctx, name, "unknown exception");
  }
  return 0;
}

}  // namespace detail

// Fn: R(ScriptContext&, Args...). Registers the global `name`.
template <auto Fn>
void bind(lua_State* L, ScriptContext& ctx, const char* name) {
  bindRaw(L, ctx, name, &detail::trampoline<Fn>);
}

}  // namespace choco::script
