#include "games/choc2/Choc2LogoNatives.h"

#include <memory>
#include <optional>
#include <string>

#include "games/choc2/LogoEditor.h"

namespace choco::script {

namespace {

using game::choc2::LogoEditor;
using game::choc2::LogoSpec;

// With no LogoEditor open a native does nothing but say so once: the Lua only calls them from the editor's own buttons.
LogoEditor* editor(ScriptContext& ctx, const char* native) {
  LogoEditor* e = LogoEditor::current(ctx.wm);
  if (!e && ctx.warned.insert(std::string("logo-no-editor-") + native).second)
    ctx.log.write(std::string("[native-warning] ") + native + ": there is no LogoEditor");
  return e;
}

void nativeSetLogoText(ScriptContext& ctx, std::string text) {
  if (LogoEditor* e = editor(ctx, "SetLogoText")) e->setText(text);
}

// nil with no editor: every caller guards it.
std::optional<std::string> nativeGetLogoString(ScriptContext& ctx) {
  if (LogoEditor* e = editor(ctx, "GetLogoString")) return e->luaString();
  return std::nullopt;
}

void nativeRandomizeLogo(ScriptContext& ctx) {
  if (LogoEditor* e = editor(ctx, "RandomizeLogo")) e->randomize(ctx.rng);
}

void nativeSetTextPosition(ScriptContext& ctx, double x, double y) {
  if (LogoEditor* e = editor(ctx, "SetTextPosition")) e->setTextPosition(x, y);
}

void nativeSetGraphicPosition(ScriptContext& ctx, double x, double y) {
  if (LogoEditor* e = editor(ctx, "SetGraphicPosition")) e->setGraphicPosition(x, y);
}

// The fourth argument (alpha, always 255) is not used.
void nativeSetTextColor(ScriptContext& ctx, double r, double g, double b, std::optional<double> a) {
  (void)a;
  if (LogoEditor* e = editor(ctx, "SetTextColor")) e->setTextColor(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b));
}

void nativeToggleBold(ScriptContext& ctx) {
  if (LogoEditor* e = editor(ctx, "ToggleBold")) e->toggleBold();
}
void nativeToggleItalic(ScriptContext& ctx) {
  if (LogoEditor* e = editor(ctx, "ToggleItalic")) e->toggleItalic();
}
void nativeToggleOutline(ScriptContext& ctx) {
  if (LogoEditor* e = editor(ctx, "ToggleOutline")) e->toggleOutline();
}
void nativeSetLeftJustify(ScriptContext& ctx) {
  if (LogoEditor* e = editor(ctx, "SetLeftJustify")) e->setJustify(LogoSpec::kLeft);
}
void nativeSetCenterJustify(ScriptContext& ctx) {
  if (LogoEditor* e = editor(ctx, "SetCenterJustify")) e->setJustify(LogoSpec::kCenter);
}
void nativeSetRightJustify(ScriptContext& ctx) {
  if (LogoEditor* e = editor(ctx, "SetRightJustify")) e->setJustify(LogoSpec::kRight);
}

}  // namespace

void registerChoc2LogoNatives(lua_State* L, ScriptContext& ctx) {
  bind<nativeSetLogoText>(L, ctx, "SetLogoText");
  bind<nativeGetLogoString>(L, ctx, "GetLogoString");
  bind<nativeRandomizeLogo>(L, ctx, "RandomizeLogo");
  bind<nativeSetTextPosition>(L, ctx, "SetTextPosition");
  bind<nativeSetGraphicPosition>(L, ctx, "SetGraphicPosition");
  bind<nativeSetTextColor>(L, ctx, "SetTextColor");
  bind<nativeToggleBold>(L, ctx, "ToggleBold");
  bind<nativeToggleItalic>(L, ctx, "ToggleItalic");
  bind<nativeToggleOutline>(L, ctx, "ToggleOutline");
  bind<nativeSetLeftJustify>(L, ctx, "SetLeftJustify");
  bind<nativeSetCenterJustify>(L, ctx, "SetCenterJustify");
  bind<nativeSetRightJustify>(L, ctx, "SetRightJustify");
  ctx.wm.addWindowType("LogoEditor", [](ui::WindowManager& wm) { return std::make_unique<game::choc2::LogoEditor>(wm); });
}

}  // namespace choco::script
