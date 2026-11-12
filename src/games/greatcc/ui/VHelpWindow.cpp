#include "games/greatcc/ui/VHelpWindow.h"

#include <algorithm>

#include "games/greatcc/ui/RDisableButton.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {
int luaSetPage(lua_State* L) {
  script::ScriptContext& ctx = script::contextOf(L);
  const int n = static_cast<int>(luaL_checknumber(L, 1));
  if (auto* w = dynamic_cast<VHelpWindow*>(ctx.wm.window(ScopedNatives::ownerOf(L)))) w->setPage(n);
  return 0;
}
}  // namespace

VHelpWindow::VHelpWindow(ui::WindowManager& wm, script::ScriptContext& ctx)
    : LayeredWindow(wm, kPages), ctx_(ctx), natives_(ctx) {
  natives_.bindOwned("SetPage", &luaSetPage, id());
}

void VHelpWindow::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  if (ui::Window* w = find("txt_page")) counter_ = w->id();
  if (ui::Window* w = find("btn_next"); dynamic_cast<RDisableButton*>(w)) next_ = w->id();
  if (ui::Window* w = find("btn_back"); dynamic_cast<RDisableButton*>(w)) back_ = w->id();
}

void VHelpWindow::setPage(int n) {
  page_ = std::clamp(n, 0, kPages - 1);
  setCurrentLayer(page_);
  if (!title_) {
    for (ui::Window* p = parent(); p; p = p->parent())
      if (p->isModal()) {
        if (ui::Window* t = p->find("txt_title")) title_ = t->id();
        break;
      }
  }
  if (ui::Window* t = manager().window(title_); t && t->hasLabel())
    t->setLabel(ctx_.strings.sdkLookup("help_title_" + std::to_string(page_)));
  if (ui::Window* c = manager().window(counter_); c && c->hasLabel())
    c->setLabel(std::to_string(page_ + 1) + "/" + std::to_string(kPages));
  if (auto* b = dynamic_cast<RDisableButton*>(manager().window(next_))) b->toggleState(page_ == kPages - 1);
  if (auto* b = dynamic_cast<RDisableButton*>(manager().window(back_))) b->toggleState(page_ == 0);
}

bool VHelpWindow::onMessage(const ui::Message& m) {
  if (m.type == ui::kButtonPress) {
    if (m.name == "btn_back") setPage(page_ - 1);
    else if (m.name == "btn_next") setPage(page_ + 1);
  }
  return false;
}

std::string VHelpWindow::detail() const { return " page=" + std::to_string(page_ + 1) + "/" + std::to_string(kPages); }

}  // namespace choco::games::greatcc
