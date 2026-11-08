#include <string>

#include "script/ScriptHost.h"
#include "ui/Window.h"
#include "ui/widgets/Widgets.h"

namespace choco::script {

namespace {

// SetLayeredLabel (RegisterBSGLuaHelpers): the named layered window (a button) under the top modal. For each layer 0..3 it
// makes that layer current, sets the text of the child label<N> if there is one, and finally restores the layer it
// started on. A missing window, or one that is not layered, does nothing.
void nativeSetLayeredLabel(ScriptContext& ctx, std::string name, std::string text) {
  auto* w = dynamic_cast<ui::LayeredWindow*>(ctx.wm.findInTopModal(name));
  if (!w) return;
  const int original = w->currentLayer();
  for (int layer = 0; layer < 4; ++layer) {
    w->setCurrentLayer(layer);
    ui::Window* child = w->find("label" + std::to_string(layer));
    if (child && child != w && child->hasLabel()) child->setLabel(text);
  }
  w->setCurrentLayer(original);
}

}  // namespace

void registerTextNatives(lua_State* L, ScriptContext& ctx) {
  bind<nativeSetLayeredLabel>(L, ctx, "SetLayeredLabel");
  // Text labels that are not '#' literals are keys of the SDK string table, resolved with empty arguments
  // (TText::Init calls TStringTable::GetString(key, "", "", "", "", "")): %1%..%5% become "" and a missing nested key
  // makes the whole string "#####".
  ctx.wm.setTextFileReader([&ctx](const std::string& name) { return ctx.fs.readText(name); });
  ctx.wm.setLabelLookup([&ctx](const std::string& key) { return ctx.strings.sdkLookup(key, {}); });
}

}  // namespace choco::script
