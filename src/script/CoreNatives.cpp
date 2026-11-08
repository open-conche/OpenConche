#include "script/ScriptHost.h"

#include <cctype>
#include <cstdint>

#include <cmath>

#include "script/Formats.h"
#include "script/Loaders.h"

namespace choco::script {

namespace {

std::uint32_t nativeGetTimer(ScriptContext& ctx) {
  return static_cast<std::uint32_t>(ctx.clock.nowMs < 0 ? 0 : ctx.clock.nowMs);
}

int nativeRandRange(ScriptContext& ctx, int lo, int hi) { return ctx.rng.range(lo, hi); }

double nativeFloor(ScriptContext&, double v) { return std::floor(v); }

double nativePow(ScriptContext&, double x, double y) { return std::pow(x, y); }

double nativeMod(ScriptContext& ctx, double a, double b) {
  bool zero = false;
  double r = modNative(a, b, &zero);
  if (zero && ctx.warned.insert("Mod#0").second) ctx.log.write("[lua-arg] Mod: divisor is 0 (the original faults); returning 0");
  return r;
}

std::string nativeDollars(ScriptContext& ctx, double v) { return dollars(ctx.lua.L(), v); }

void nativeAddWindowType(ScriptContext& ctx, std::string name) { addWindowType(ctx.lua.L(), ctx, name); }

}  // namespace

bool addWindowType(lua_State* L, ScriptContext& ctx, const std::string& name) {
  bool ok = !name.empty() && !std::isdigit(static_cast<unsigned char>(name[0]));
  for (char c : name) ok = ok && (std::isalnum(static_cast<unsigned char>(c)) || c == '_');
  if (!ok) {
    ctx.log.write("[native-error] AddWindowType: invalid type name '" + name + "'");
    return false;
  }
  // The original's exact template (window-system design section 3).
  std::string code = "function " + name + "( table ) Creating( '" + name + "', table ); table.typename='" + name +
                     "'; return function() DoWindow( table ) end end;";
  int top = lua_gettop(L);
  if (loadBuffer(L, code, "AddWindowType") != 0 || lua_pcall(L, 0, 0, 0) != 0) {
    ctx.log.write(std::string("[native-error] AddWindowType ") + name + ": " + lua_tostring(L, -1));
    lua_settop(L, top);
    return false;
  }
  ctx.wm.addWindowType(name);
  return true;
}

void registerCoreNatives(lua_State* L, ScriptContext& ctx) {
  bind<nativeGetTimer>(L, ctx, "GetTimer");
  bind<nativeGetTimer>(L, ctx, "CurrentTime");
  bind<nativeRandRange>(L, ctx, "RandRange");
  bind<nativeAddWindowType>(L, ctx, "AddWindowType");
  bind<nativeFloor>(L, ctx, "Floor");
  bind<nativePow>(L, ctx, "Pow");
  bind<nativeMod>(L, ctx, "Mod");
  bind<nativeDollars>(L, ctx, "Dollars");
}

}  // namespace choco::script
