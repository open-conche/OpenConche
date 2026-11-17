#include "script/Enhancements.h"

#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "script/Bind.h"
#include "script/Embedded.h"
#include "script/GameModule.h"
#include "script/Loaders.h"
#include "script/ScriptHost.h"
#include "ui/Message.h"
#include "ui/WindowManager.h"
#include "ui/widgets/InputWidgets.h"
#include "ui/widgets/Widgets.h"

namespace choco::script {

namespace {

// What the two hooks share: the context and the error texts already logged.
struct HookState {
  explicit HookState(ScriptContext& c) : ctx(c) {}
  ScriptContext& ctx;
  std::set<std::string> logged;
};

// An overlay error: logged once per message text, then the hook counts as "not handled".
void logHookError(HookState& s, lua_State* L) {
  const char* m = lua_tostring(L, -1);
  const std::string text = m ? m : "(error object is not a string)";
  if (s.logged.insert(text).second) s.ctx.log.write("[enhancements] " + text);
}

// Pushes Enhance.<field> and returns true when it is a function (and leaves it on the stack); else pops and returns false.
bool pushHandler(lua_State* L, const char* field) {
  lua_getglobal(L, "Enhance");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  lua_pushstring(L, field);
  lua_gettable(L, -2);
  lua_remove(L, -2);
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  return true;
}

bool wheelHook(HookState& s, const std::vector<ui::Window*>& under, int notches) {
  ScriptContext& ctx = s.ctx;
  lua_State* L = ctx.lua.L();
  const int top = lua_gettop(L);
  if (!pushHandler(L, "onWheel")) return false;
  lua_pushnumber(L, notches);
  lua_newtable(L);
  int n = 0;
  for (const ui::Window* w : under) {
    if (w->name().empty()) continue;
    lua_pushlstring(L, w->name().data(), w->name().size());
    lua_rawseti(L, -2, ++n);
  }
  luaL_setn(L, -1, n);
  std::string press;
  bool handled = false;
  if (lua_pcall(L, 2, 1, 0) != 0) {
    logHookError(s, L);
  } else if (lua_type(L, -1) == LUA_TSTRING) {
    press.assign(lua_tostring(L, -1), lua_strlen(L, -1));
    handled = true;
  } else {
    handled = lua_toboolean(L, -1) != 0;
  }
  lua_settop(L, top);
  // After the Lua call has returned: the game's own command runs and does its own clamping.
  // One press per notch, but only while it is still the same screen: stop once the button is gone, the top modal
  // changed or the press asked for the modal to close.
  const ui::WindowId modalId = press.empty() ? 0 : ctx.wm.topModal()->id();
  for (int i = std::abs(notches); modalId != 0 && i > 0; --i) {
    if (ctx.wm.topModal()->id() != modalId || ctx.wm.closePending(modalId)) break;
    if (!ctx.wm.pressButtonNamed(press)) break;
  }
  return handled;
}

LuaRef commandHook(HookState& s, const std::string& name, const LuaRef* original, bool closes) {
  ScriptContext& ctx = s.ctx;
  lua_State* L = ctx.lua.L();
  const int top = lua_gettop(L);
  LuaRef result;
  if (pushHandler(L, "beforeCommand")) {
    lua_pushlstring(L, name.data(), name.size());
    if (original) original->push(L);
    else lua_pushnil(L);
    lua_pushboolean(L, closes ? 1 : 0);
    if (lua_pcall(L, 3, 1, 0) != 0) logHookError(s, L);
    else if (lua_isfunction(L, -1)) result = LuaRef(ctx.lua, L, -1);
  }
  lua_settop(L, top);
  return result;
}

// Enhance.textLimit(name, length, ignore): the number of characters a typed-into field holds, or anything else for the
// original limit.
int textLimitHook(HookState& s, const std::string& name, int length, const std::string& ignore) {
  lua_State* L = s.ctx.lua.L();
  const int top = lua_gettop(L);
  int limit = 0;
  if (pushHandler(L, "textLimit")) {
    lua_pushlstring(L, name.data(), name.size());
    lua_pushnumber(L, length);
    lua_pushlstring(L, ignore.data(), ignore.size());
    if (lua_pcall(L, 3, 1, 0) != 0) logHookError(s, L);
    else if (lua_type(L, -1) == LUA_TNUMBER) limit = static_cast<int>(lua_tonumber(L, -1));
  }
  lua_settop(L, top);
  return limit;
}

// EnhanceClose(name): what a `close` button's press does.
int nativeEnhanceClose(lua_State* L) {
  ScriptContext& ctx = contextOf(L);
  const char* name = lua_tostring(L, 1);
  ui::ModalWindow* modal = ctx.wm.topModal();
  ctx.wm.post({ui::kCloseWindow, ui::WindowManager::idString(modal->id()), modal->id()});
  if (ctx.host) ctx.host->setGlobalString("gReturnValue", name ? name : "");
  return 0;
}

bool shownWindowIn(const ui::Window& w, const std::string& name) {
  if (!w.shown()) return false;
  if (w.name() == name) return true;
  for (const auto& c : w.children())
    if (shownWindowIn(*c, name)) return true;
  return false;
}

// EnhanceWindowExists(name): whether the top modal has a shown window of that name.
int nativeEnhanceWindowExists(lua_State* L) {
  ScriptContext& ctx = contextOf(L);
  const char* name = lua_tostring(L, 1);
  const bool found = name && *name && shownWindowIn(*ctx.wm.topModal(), name);
  lua_pushboolean(L, found ? 1 : 0);
  return 1;
}

// EnhanceSetText(name, text): sets the text of the named TextEdit in the top modal as typing would have left it (the
// change message is posted and its onkey runs). Returns whether such a field was found.
int nativeEnhanceSetText(lua_State* L) {
  ScriptContext& ctx = contextOf(L);
  const char* name = lua_tostring(L, 1);
  const char* text = lua_tostring(L, 2);
  auto* edit = name && text ? dynamic_cast<ui::TextEditWindow*>(ctx.wm.findInTopModal(name)) : nullptr;
  if (edit) edit->setTextAsTyped(text);
  lua_pushboolean(L, edit ? 1 : 0);
  return 1;
}

// The image of a button's idle layer (its child Bitmap `image0`), or null.
const std::string* idleGraphic(const ui::Window& button) {
  const auto* bmp = dynamic_cast<const ui::BitmapWindow*>(button.find("image0"));
  return bmp ? &bmp->image() : nullptr;
}

bool shownButtonWithGraphic(const ui::Window& w, const std::string& image) {
  if (!w.shown()) return false;
  if (dynamic_cast<const ui::ButtonWindow*>(&w)) {
    const std::string* g = idleGraphic(w);
    if (g && *g == image) return true;
  }
  for (const auto& c : w.children())
    if (shownButtonWithGraphic(*c, image)) return true;
  return false;
}

// EnhanceHasButtonGraphic(image): whether the top modal has a shown button whose idle graphic is `image`.
int nativeEnhanceHasButtonGraphic(lua_State* L) {
  ScriptContext& ctx = contextOf(L);
  const char* image = lua_tostring(L, 1);
  lua_pushboolean(L, image && shownButtonWithGraphic(*ctx.wm.topModal(), image) ? 1 : 0);
  return 1;
}

// EnhanceButtonGraphic(): the idle graphic of the button whose press the command hook is being consulted for (an
// unnamed button is told apart by it), or nil outside that call.
int nativeEnhanceButtonGraphic(lua_State* L) {
  ScriptContext& ctx = contextOf(L);
  const ui::ButtonWindow* b = ctx.wm.hookedButton();
  const std::string* g = b ? idleGraphic(*b) : nullptr;
  if (g) lua_pushlstring(L, g->data(), g->size());
  else lua_pushnil(L);
  return 1;
}

// Runs one overlay chunk; on failure returns the error text, else "".
std::string runOverlay(lua_State* L, std::string_view code, const char* chunkName) {
  if (loadBuffer(L, code, chunkName) != 0 || lua_pcall(L, 0, 0, 0) != 0) {
    const char* m = lua_tostring(L, -1);
    std::string text = std::string(chunkName) + ": " + (m ? m : "(error object is not a string)");
    lua_settop(L, 0);
    return text;
  }
  return "";
}

const char* const kNatives[] = {"EnhanceClose",          "EnhanceWindowExists", "EnhanceHasButtonGraphic",
                                "EnhanceButtonGraphic", "EnhanceSetText"};

// The state of the switch being off: no Enhance global, no Enhance* native.
void removeOverlayGlobals(lua_State* L) {
  for (const char* n : kNatives) {
    lua_pushnil(L);
    lua_setglobal(L, n);
  }
  lua_pushnil(L);
  lua_setglobal(L, "Enhance");
}

}  // namespace

void bootEnhancements(ScriptContext& ctx) {
  const std::string id = ctx.module ? ctx.module->profile().id : "";
  const std::string_view gameCode = embeddedEnhanceFor(id);
  if (ctx.config.get("enhancements") != "1" || gameCode.empty()) {
    ctx.log.write("[enhancements] off");
    return;
  }
  loadOverlay(ctx, id, embeddedEnhanceShared(), gameCode);
}

void loadOverlay(ScriptContext& ctx, const std::string& id, std::string_view shared, std::string_view gameCode) {
  lua_State* L = ctx.lua.L();
  bindRaw(L, ctx, "EnhanceClose", nativeEnhanceClose);
  bindRaw(L, ctx, "EnhanceWindowExists", nativeEnhanceWindowExists);
  bindRaw(L, ctx, "EnhanceHasButtonGraphic", nativeEnhanceHasButtonGraphic);
  bindRaw(L, ctx, "EnhanceButtonGraphic", nativeEnhanceButtonGraphic);
  bindRaw(L, ctx, "EnhanceSetText", nativeEnhanceSetText);
  const std::string gameChunk = "enhance_" + id + ".lua";
  std::string failure = runOverlay(L, shared, "enhance.lua");
  if (failure.empty()) failure = runOverlay(L, gameCode, gameChunk.c_str());
  if (!failure.empty()) {
    // A broken overlay never stops the game: the state is the switch's off state.
    ctx.log.write("[enhancements] " + failure);
    removeOverlayGlobals(L);
    ctx.log.write("[enhancements] off");
    return;
  }
  auto state = std::make_shared<HookState>(ctx);
  ctx.wm.setWheelHook([state](const std::vector<ui::Window*>& under, int notches) { return wheelHook(*state, under, notches); });
  ctx.wm.setCommandHook([state](const std::string& name, const LuaRef* original, bool closes) {
    return commandHook(*state, name, original, closes);
  });
  ctx.wm.setTextLimitHook([state](const std::string& name, int length, const std::string& ignore) {
    return textLimitHook(*state, name, length, ignore);
  });
  ctx.log.write("[enhancements] on (" + id + ")");
}

void shutdownEnhancements(ScriptContext& ctx) {
  ctx.wm.setWheelHook({});
  ctx.wm.setCommandHook({});
  ctx.wm.setTextLimitHook({});
}

}  // namespace choco::script
