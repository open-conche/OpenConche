#include <cstdlib>
#include <string>

#include "assets/Content.h"
#include "script/ScriptHost.h"

namespace choco::script {

namespace {

// ---- config (settings.xml keys) ----

std::string nativeGetConfig(ScriptContext& ctx, std::string key, std::optional<std::string> def) {
  return ctx.config.get(key, def.value_or(""));
}

// IsEnabled: GetConfig(key, "") converted with to_int, non-zero.
bool configEnabled(ScriptContext& ctx, const std::string& key) {
  return std::strtol(ctx.config.get(key).c_str(), nullptr, 10) != 0;
}

bool nativeIsEnabled(ScriptContext& ctx, std::string key) { return configEnabled(ctx, key); }

// CheckConfig allows a fixed set of keys (api-globals.md); anything else is false.
bool nativeCheckConfig(ScriptContext& ctx, std::string key) {
  if (key == "local") return configEnabled(ctx, "hiscorelocal");
  if (key == "anonymous") return configEnabled(ctx, "hiscoreanon");
  if (key == "firstpeek") return configEnabled(ctx, "firstpeek");
  if (key == "xsell") return ctx.fs.exists("xsell/xsell.lua");
  if (key == "dev") return configEnabled(ctx, "cheatmode");
  return false;
}

// ---- strings ----

void nativeBsgLoadStringFile(ScriptContext& ctx, std::string path) {
  assets::Warnings warnings;
  ctx.strings.loadFile(ctx.fs, path, assets::StringTable::Layer::Bsg, &warnings);
  for (const auto& w : warnings) ctx.log.write("[content] " + w);
}

bool nativeBsgStringExists(ScriptContext& ctx, std::string key) { return ctx.strings.hasBsg(key); }

std::string nativeBsgGetString(ScriptContext& ctx, std::string key) { return ctx.strings.lookup(key); }

// _GetString (TWindowScript::LookupString): '#' literal, else the SDK table with %1%..%5%.
std::string nativeGetString(ScriptContext& ctx, std::string id, std::string p1, std::string p2, std::string p3,
                            std::string p4, std::string p5) {
  return ctx.strings.sdkLookup(id, {p1, p2, p3, p4, p5});
}

}  // namespace

void registerContentNatives(lua_State* L, ScriptContext& ctx) {
  bind<nativeGetConfig>(L, ctx, "GetConfig");
  bind<nativeIsEnabled>(L, ctx, "IsEnabled");
  bind<nativeCheckConfig>(L, ctx, "CheckConfig");
  bind<nativeBsgLoadStringFile>(L, ctx, "bsgLoadStringFile");
  bind<nativeBsgStringExists>(L, ctx, "bsgStringExists");
  bind<nativeBsgGetString>(L, ctx, "bsgGetString");
  bind<nativeGetString>(L, ctx, "_GetString");
}

}  // namespace choco::script
