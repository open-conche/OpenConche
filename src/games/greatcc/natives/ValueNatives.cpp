#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/natives/StateNatives.h"

namespace choco::games::greatcc {

namespace {

using script::ScriptContext;

// GetManagedInt(name, default): floor of the value; a missing name is created with the default as its base.
double nativeGetManagedInt(ScriptContext& ctx, std::string name, double def) {
  GreatCCWorld* w = worldOf(ctx);
  return w ? w->values().getInt(name, static_cast<int>(def)) : static_cast<int>(def);
}
double nativeGetManagedUint(ScriptContext& ctx, std::string name, double def) {
  GreatCCWorld* w = worldOf(ctx);
  return w ? w->values().getUint(name, def <= 0 ? 0u : static_cast<std::uint32_t>(def)) : def;
}
double nativeGetManagedReal(ScriptContext& ctx, std::string name, double def) {
  GreatCCWorld* w = worldOf(ctx);
  return w ? w->values().getReal(name, def) : def;
}
void nativeSetManagedValue(ScriptContext& ctx, std::string name, double value, bool save) {
  if (GreatCCWorld* w = worldOf(ctx)) w->values().setValue(name, value, save);
}

}  // namespace

void bindValueNatives(lua_State* L, script::ScriptContext& ctx) {
  using detail::rebind;
  rebind<nativeGetManagedInt>(L, ctx, "GetManagedInt");
  rebind<nativeGetManagedUint>(L, ctx, "GetManagedUint");
  rebind<nativeGetManagedReal>(L, ctx, "GetManagedReal");
  rebind<nativeSetManagedValue>(L, ctx, "SetManagedValue");
}

}  // namespace choco::games::greatcc
