#include "games/common/OptionsPanel.h"

#include "sdk/Users.h"
#include "ui/widgets/Widgets.h"

namespace choco::game {

bool OptionsPanel::fullscreenNow(script::ScriptContext &ctx) {
  if (ctx.isFullscreen)
    return ctx.isFullscreen();
  return ctx.users && ctx.users->globalPref("fullscreen") == "1";
}

bool OptionsPanel::mutedNow(script::ScriptContext &ctx) {
  return ctx.users && ctx.users->pref("soundoff") == "1";
}

void OptionsPanel::postChildrenInit(const ui::Style &style) {
  OptionsWindow::postChildrenInit(style); // the sliders, from the user's prefs
  if (auto *b = dynamic_cast<ui::ButtonWindow *>(find("fullscreen")))
    b->setOn(fullscreenNow(ctx_));
  if (auto *b = dynamic_cast<ui::ButtonWindow *>(find("mute")))
    b->setOn(mutedNow(ctx_));
}

} // namespace choco::game
