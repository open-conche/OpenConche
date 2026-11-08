#include <algorithm>
#include <iterator>
#include <string>
#include <tuple>

#include "script/LuaStyle.h"
#include "script/Messages.h"
#include "script/RunFile.h"
#include "script/ScriptHost.h"
#include "ui/DrawContext.h"
#include "ui/Layout.h"
#include "ui/widgets/Widgets.h"

namespace choco::script {

namespace {

using ui::Window;

void warnOnce(ScriptContext& ctx, const std::string& key, const std::string& text) {
  if (ctx.warned.insert(key).second) ctx.log.write(text);
}

// ---- creation ----

void nativeGenericCreate(ScriptContext& ctx, TableArg t) {
  if (!t.valid()) return;
  LuaStyle style(t.L, t.index, &ctx.lua);
  std::string type = style.stringOr("typename");
  std::unique_ptr<Window> w = ctx.wm.createWindow(type);
  // A placeholder named Window is the real plain Window type (the generic container), not a missing class.
  if (dynamic_cast<ui::PlaceholderWindow*>(w.get()) && w->typeName() != "Window" && ctx.wm.firstPlaceholderOf(type)) {
    ctx.log.write("[unimplemented-type] " + (type.empty() ? std::string("Window") : type));  // once per type
  }
  Window& made = ctx.wm.initWindow(std::move(w), style);
  lua_pushnumber(t.L, made.rect().w);
  lua_setglobal(t.L, "gDialogWidth");
  lua_pushnumber(t.L, made.rect().h);
  lua_setglobal(t.L, "gDialogHeight");
}

void nativePostCreate(ScriptContext& ctx, TableArg t, std::string) {
  if (!t.valid()) return;
  LuaStyle style(t.L, t.index, &ctx.lua);
  ctx.wm.creationTop()->postChildrenInit(style);
}

void nativePopWindow(ScriptContext& ctx) { ctx.wm.popCreation(); }

void nativeSetWindowName(ScriptContext& ctx, std::string name) { ctx.wm.creationTop()->setName(std::move(name)); }

// ---- modals ----

std::string nativePushModal(ScriptContext& ctx, std::string name) {
  return ui::WindowManager::idString(ctx.wm.pushModal(name).id());
}

void nativeDoPopModal(ScriptContext& ctx, std::string s) { ctx.wm.popModal(s); }

int nativeGetWindowDepth(ScriptContext& ctx) { return ctx.wm.modalDepth(); }

void nativeClearModalChildren(ScriptContext& ctx) { ctx.wm.clearModalChildren(); }

// ---- windows by name ----

bool nativeEnableWindow(ScriptContext& ctx, std::string name, bool enable) {
  Window* w = ctx.wm.findInChain(name);
  if (!w) return false;
  w->setShown(enable);
  if (auto* b = dynamic_cast<ui::ButtonWindow*>(w)) b->resetState();  // a button drops capture and its pressed look
  return true;
}

void nativeDestroyWindow(ScriptContext& ctx, std::string name) {
  if (Window* w = ctx.wm.creationTop()->find(name)) ctx.wm.destroyWindow(*w);
}

void nativeRealSetFocus(ScriptContext& ctx, std::string name) {
  Window* w = ctx.wm.creationTop()->find(name);
  ctx.wm.setFocus(w ? w->id() : 0);  // a missing name clears focus, as in the original
}

// ---- buttons ----

ui::ButtonWindow* buttonIn(ScriptContext& ctx, const std::string& name) {
  return dynamic_cast<ui::ButtonWindow*>(ctx.wm.findInTopModal(name));
}

void nativeSetCommand(ScriptContext& ctx, std::string name, LuaRef fn) {
  if (ui::ButtonWindow* b = buttonIn(ctx, name); b && fn.valid()) b->setCommand(std::move(fn));
}

bool nativeGetButtonToggleState(ScriptContext& ctx, std::string name) {
  ui::ButtonWindow* b = buttonIn(ctx, name);
  return b && b->on();
}

void nativeSetButtonToggleState(ScriptContext& ctx, std::string name, bool on) {
  if (ui::ButtonWindow* b = buttonIn(ctx, name)) b->setOn(on);
}

// An empty key is ignored; an empty button name removes the key.
void nativeRegisterHotkey(ScriptContext& ctx, std::string key, std::string button) {
  if (key.empty()) return;
  ctx.wm.topModal()->addHotkey(key, button);
}

// "&Save" -> ("Save", "S"): removes the first '&' and returns the character after it, or "" when there is none.
std::tuple<std::string, std::string> nativeStripKeyFromLabel(ScriptContext&, std::string label) {
  const auto at = label.find('&');
  if (at == std::string::npos) return {label, std::string()};
  label.erase(at, 1);
  return {label, at < label.size() ? std::string(1, label[at]) : std::string()};
}

std::string nativeGetLabel(ScriptContext& ctx, std::string name) {
  Window* w = ctx.wm.findInTopModal(name);
  if (!w) return name + "_is_not_found";
  if (!w->hasLabel()) return name + "_is_not_a_TText_window_or_TButton";
  return w->label();
}

void nativeSetLabel(ScriptContext& ctx, std::string name, std::string text) {
  Window* w = ctx.wm.findInTopModal(name);
  if (w && w->hasLabel()) w->setLabel(std::move(text));
}

// ---- layout and appearance ----

void nativeDoFitToChildren(ScriptContext& ctx) { ui::fitToChildren(*ctx.wm.creationTop()); }

void nativeSetPosition(ScriptContext& ctx, std::string name, double x, double y) {
  if (Window* w = ctx.wm.findInTopModal(name)) ui::setPositionFromScript(*w, x, y);
}

void nativeSetWindowPos(ScriptContext& ctx, std::string name, double x, double y) {
  if (Window* w = ctx.wm.findInChain(name)) w->setPos(static_cast<int>(x), static_cast<int>(y));
}

void nativeSetAlpha(ScriptContext& ctx, std::string name, double alpha0to255) {
  if (Window* w = ctx.wm.findInChain(name)) ui::setSubtreeAlpha(*w, static_cast<float>(alpha0to255 / 255.0));
}

// The whole tree is drawn every frame, so there is nothing to invalidate.
void nativeUpdateWindow(ScriptContext&) {}

bool nativeImageLoaded(ScriptContext& ctx, std::string name) {
  Window* w = ctx.wm.findInTopModal(name);
  return w && w->imageLoaded();
}

void nativeSetBitmap(ScriptContext& ctx, std::string name, std::string image, double scale) {
  Window* w = ctx.wm.findInTopModal(name);
  if (auto* b = dynamic_cast<ui::BitmapWindow*>(w)) b->setBitmap(image, scale);
}

// Only a layered window (a button) on top of the creation stack reacts; anything else is ignored, as in the original.
void nativeDoSelectLayer(ScriptContext& ctx, int layer) {
  if (auto* lw = dynamic_cast<ui::LayeredWindow*>(ctx.wm.creationTop())) lw->setCurrentLayer(layer);
}

// Transition{ "<style>", window=name, time=ms, alpha=a, onend=fn, startx.. endy, path } (DoTransition, 0x394ee). The
// window is looked up under the top modal; a missing one returns without touching onend. `center` and unknown styles
// release onend without calling it; every other style starts a transition whose onend runs once when it ends.
void nativeTransition(ScriptContext& ctx, TableArg t) {
  if (!t.valid()) return;
  lua_State* L = t.L;
  const int top = lua_gettop(L);
  auto field = [&](const char* key) {
    lua_pushstring(L, key);
    lua_rawget(L, t.index);
  };
  auto number = [&](const char* key, double def) {
    field(key);
    const double v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
  };
  std::string type, windowName;
  lua_rawgeti(L, t.index, 1);
  if (lua_type(L, -1) == LUA_TSTRING) type = lua_tostring(L, -1);
  field("window");
  if (lua_type(L, -1) == LUA_TSTRING) windowName = lua_tostring(L, -1);
  lua_settop(L, top);
  Window* w = ctx.wm.findInTopModal(windowName);
  if (!w) return;

  ui::Transition tr;
  tr.target = w->id();
  tr.startMs = ctx.clock.nowMs;
  const double time = std::clamp(number("time", 100), 0.0, 4294967295.0);
  tr.durationMs = std::floor(time);
  tr.alpha = static_cast<float>(number("alpha", 1));
  // The horizontal centre in the screen; y is kept. (Unsigned shift in the original; the same for windows that fit.)
  auto centre = [&] {
    const int screenW = ctx.wm.screen()->rect().w;
    w->setPos((screenW - w->rect().w) >> 1, w->rect().y);
  };

  bool start = true;
  if (type == "center_fadein") {
    w->setShown(false);
    ui::setSubtreeAlpha(*w, 0);
    tr.kind = ui::Transition::Kind::FadeIn;
    centre();
  } else if (type == "center") {
    centre();
    start = false;  // no transition object: onend is released unused
  } else if (type == "fadein") {
    tr.kind = ui::Transition::Kind::FadeIn;
    ui::setSubtreeAlpha(*w, 0);
  } else if (type == "fadeout") {
    tr.kind = ui::Transition::Kind::FadeOut;
  } else if (type == "swipe") {
    tr.kind = ui::Transition::Kind::Swipe;
    tr.startX = static_cast<int>(number("startx", w->rect().x));
    tr.startY = static_cast<int>(number("starty", w->rect().y));
    tr.endX = static_cast<int>(number("endx", w->rect().x));
    tr.endY = static_cast<int>(number("endy", w->rect().y));
  } else if (type == "path") {
    tr.kind = ui::Transition::Kind::Path;
    field("path");
    if (lua_istable(L, -1)) {
      const int pi = lua_gettop(L);
      for (int i = 1;; ++i) {
        lua_rawgeti(L, pi, i);
        if (lua_isnil(L, -1)) {
          lua_pop(L, 1);
          break;
        }
        if (lua_istable(L, -1)) {
          const int ci = lua_gettop(L);
          lua_rawgeti(L, ci, 1);
          lua_rawgeti(L, ci, 2);
          tr.curve.addControlPoint(lua_isnumber(L, -2) ? static_cast<float>(lua_tonumber(L, -2)) : 0.0f,
                                   lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.0f);
        }
        lua_settop(L, pi);
      }
    } else {
      start = false;  // no usable path table: nothing starts and onend is released
    }
    lua_settop(L, top);
  } else if (type == "zoomin") {
    tr.kind = ui::Transition::Kind::ZoomIn;
    // The original requires a TImage; for any other window the transition still times and calls onend.
    if (!dynamic_cast<ui::BitmapWindow*>(w))
      warnOnce(ctx, "transition-zoomin-nonimage", "[transition] zoomin on a window that is not a Bitmap: no zoom, onend still runs");
  } else {
    start = false;  // unknown style
    warnOnce(ctx, "transition-style:" + type, "[transition] unknown style '" + type + "': nothing happens and onend is not called");
  }
  if (!start) return;

  field("onend");
  if (lua_isfunction(L, -1)) tr.onend.emplace(ctx.lua, L, -1);
  lua_settop(L, top);
  ctx.wm.startTransition(std::move(tr));
}

// FillWindow(name, script): the named window anywhere in the modal chain loses all its children and the Lua file is run
// with that window as the one being built (FillWindowPtr: DestroyAllChildren, then TWindowManager::RunScript). Despite
// the original's parameter name the second argument is a script path, not an image. A missing window does nothing.
int nativeFillWindow(lua_State* L) {
  ScriptContext& ctx = contextOf(L);
  const char* name = lua_tostring(L, 1);
  const char* path = lua_tostring(L, 2);
  if (!name || !path) return 0;
  Window* w = ctx.wm.findInChain(name);
  if (!w) return 0;
  while (!w->children().empty()) ctx.wm.destroyWindow(*w->children().front());
  runFileInto(ctx, L, *w, path);
  return 0;
}

// ---- messages (raw: they handle userdata) ----

int nativeCreateNamedMessage(lua_State* L) {
  ui::Message m;
  m.type = lua_isnumber(L, 1) ? static_cast<int>(lua_tonumber(L, 1)) : 0;
  if (lua_type(L, 2) == LUA_TSTRING || lua_type(L, 2) == LUA_TNUMBER) m.name.assign(lua_tostring(L, 2), lua_strlen(L, 2));
  pushMessage(L, m);
  return 1;
}

// A close request addressed to the window on top of the creation stack (the top modal outside dialog building).
int nativeCreateCloseMessage(lua_State* L) {
  ScriptContext& ctx = contextOf(L);
  ui::Message m;
  m.type = ui::kCloseWindow;
  m.name = ui::WindowManager::idString(ctx.wm.creationTop()->id());
  pushMessage(L, m);
  return 1;
}

int postMessageFromLua(lua_State* L, bool toParent) {
  ScriptContext& ctx = contextOf(L);
  ui::Message* m = toMessage(L, 1);
  if (!m) {
    warnOnce(ctx, std::string(nativeNameOf(L)) + "#msg",
             std::string("[lua-arg] ") + nativeNameOf(L) + " arg 1: expected a message, got " + lua_typename(L, lua_type(L, 1)));
    return 0;
  }
  ui::Message copy = *m;
  if (toParent) {
    ui::Window* parent = ctx.wm.topModal()->parent();
    copy.target = parent ? parent->id() : 0;
  }
  ctx.wm.post(std::move(copy));
  return 0;
}

int nativePostMessage(lua_State* L) { return postMessageFromLua(L, false); }
int nativePostMessageToParent(lua_State* L) { return postMessageFromLua(L, true); }

// Raw natives must not let exceptions out (Lua is C).
template <lua_CFunction Fn>
int guarded(lua_State* L) {
  try {
    return Fn(L);
  } catch (const std::exception& e) {
    detail::logNativeError(L, contextOf(L), nativeNameOf(L), e.what());
  } catch (...) {
    detail::logNativeError(L, contextOf(L), nativeNameOf(L), "unknown exception");
  }
  return 0;
}

}  // namespace

void registerWindowNatives(lua_State* L, ScriptContext& ctx) {
  bind<nativeGenericCreate>(L, ctx, "GenericCreate");
  bind<nativePostCreate>(L, ctx, "PostCreate");
  bind<nativePopWindow>(L, ctx, "PopWindow");
  bind<nativeSetWindowName>(L, ctx, "SetWindowName");
  bind<nativePushModal>(L, ctx, "PushModal");
  bind<nativeDoPopModal>(L, ctx, "DoPopModal");
  bind<nativeGetWindowDepth>(L, ctx, "GetWindowDepth");
  bind<nativeClearModalChildren>(L, ctx, "ClearModalChildren");
  bind<nativeEnableWindow>(L, ctx, "EnableWindow");
  bind<nativeDestroyWindow>(L, ctx, "DestroyWindow");
  bind<nativeRealSetFocus>(L, ctx, "RealSetFocus");
  bind<nativeSetCommand>(L, ctx, "SetCommand");
  bind<nativeGetButtonToggleState>(L, ctx, "GetButtonToggleState");
  bind<nativeSetButtonToggleState>(L, ctx, "SetButtonToggleState");
  bind<nativeRegisterHotkey>(L, ctx, "_RegisterHotkey");
  bind<nativeStripKeyFromLabel>(L, ctx, "StripKeyFromLabel");
  bind<nativeDoFitToChildren>(L, ctx, "DoFitToChildren");
  bind<nativeSetPosition>(L, ctx, "SetPosition");
  bind<nativeSetWindowPos>(L, ctx, "SetWindowPos");
  bind<nativeSetAlpha>(L, ctx, "SetAlpha");
  bind<nativeUpdateWindow>(L, ctx, "UpdateWindow");
  bind<nativeImageLoaded>(L, ctx, "ImageLoaded");
  bind<nativeSetBitmap>(L, ctx, "_SetBitmap");
  bind<nativeDoSelectLayer>(L, ctx, "DoSelectLayer");
  bind<nativeTransition>(L, ctx, "Transition");
  bind<nativeGetLabel>(L, ctx, "GetLabel");
  bind<nativeSetLabel>(L, ctx, "SetLabel");
  bindRaw(L, ctx, "FillWindow", &guarded<nativeFillWindow>);
  bindRaw(L, ctx, "CreateNamedMessage", &guarded<nativeCreateNamedMessage>);
  bindRaw(L, ctx, "CreateCloseMessage", &guarded<nativeCreateCloseMessage>);
  bindRaw(L, ctx, "PostMessage", &guarded<nativePostMessage>);
  bindRaw(L, ctx, "PostMessageToParent", &guarded<nativePostMessageToParent>);
}

}  // namespace choco::script
