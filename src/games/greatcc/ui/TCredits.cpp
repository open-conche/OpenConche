#include "games/greatcc/ui/TCredits.h"

#include "ui/WindowManager.h"

namespace choco::games::greatcc {

void TCredits::postChildrenInit(const ui::Style& style) {
  CreditsWindow::postChildrenInit(style);
  // The version text is a sibling in the screen's tree: look from the top modal.
  ui::Window* modal = manager().topModal();
  if (ui::Window* v = modal ? modal->find("credits_version") : nullptr; v && v->hasLabel())
    v->setLabel(ctx_.strings.sdkLookup("app_version", {ctx_.config.get("version")}));
  sendToBack();  // SetWindowDepth(0): the scroller draws behind the frame art, which hides its overflow
}

}  // namespace choco::games::greatcc
