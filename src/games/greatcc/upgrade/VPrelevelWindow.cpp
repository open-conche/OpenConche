#include "games/greatcc/upgrade/VPrelevelWindow.h"

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/shop/ShopWindow.h"
#include "games/greatcc/ui/RDisableButton.h"
#include "games/greatcc/upgrade/VUpgradeList.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {
int luaClose(lua_State* L) {
  script::ScriptContext& ctx = script::contextOf(L);
  if (auto* w = dynamic_cast<VPrelevelWindow*>(ctx.wm.window(ScopedNatives::ownerOf(L)))) w->close();
  return 0;
}
ShopWindow* shopOf(script::ScriptContext& ctx) {
  GreatCCWorld* w = worldOf(ctx);
  return w ? dynamic_cast<ShopWindow*>(ctx.wm.window(w->shopWindowId())) : nullptr;
}
}  // namespace

VPrelevelWindow::VPrelevelWindow(ui::WindowManager& wm, script::ScriptContext& ctx) : RBounceWindow(wm), ctx_(ctx), natives_(ctx) {
  natives_.bindOwned("Close", &luaClose, id());
}

VUpgradeList* VPrelevelWindow::list() const { return dynamic_cast<VUpgradeList*>(manager().window(list_)); }

std::string VPrelevelWindow::detail() const {
  const VUpgradeList* l = list();
  return RBounceWindow::detail() + " category=" + (category_.empty() ? std::string("-") : category_) +
         " tabs=" + std::to_string(l ? l->tabs().size() : 0);
}

void VPrelevelWindow::postChildrenInit(const ui::Style& style) {
  RBounceWindow::postChildrenInit(style);
  if (ui::Window* l = find("upgrade_list"); dynamic_cast<VUpgradeList*>(l)) list_ = l->id();
  initUpgrades();
  for (ui::Window* p = parent(); p; p = p->parent())
    if (auto* modal = dynamic_cast<ui::ModalWindow*>(p)) {
      modal->defaultFocus = id();
      break;
    }
  // StartWindowAnimation(10, true, true, true): the last argument is the catch-up flag. The original starts it only with a parent modal.
  bool inModal = false;
  for (ui::Window* p = parent(); p; p = p->parent()) inModal |= p->isModal();
  if (inModal) manager().startWindowAnimation(id(), 10, true);
}

void VPrelevelWindow::initUpgrades() {
  GreatCCWorld* w = worldOf(ctx_);
  if (w) {
    UpgradeManager& um = w->upgrades();
    VUpgradeList* l = list();
    bool listed = false;
    auto first = [&](const char* button, const char* category, ui::WindowId& slot) {
      auto* b = dynamic_cast<RDisableButton*>(find(button));
      slot = b ? b->id() : 0;
      if (!b) return;
      if (um.visibleOfType(category) < 1) {
        b->toggleState(true);
      } else if (!listed) {
        category_ = category;
        if (l) l->listUpgradeType(category);
        b->setOn(true);
        listed = true;
      }
    };
    first("toggle_upgrades_gameplay", "station", machines_);
    first("toggle_upgrades_aesthetic", "aesthetic", decoration_);
    first("toggle_upgrades_player", "gameplay", powerups_);
    w->tutorials().checkTutorial("tutorial_upgrades");
  }
}

void VPrelevelWindow::draw(ui::DrawContext& ctx) const {
  RBounceWindow::draw(ctx);
  if (ShopWindow* shop = shopOf(ctx_)) shop->renderPrelevel(ctx);
}

bool VPrelevelWindow::onAnimate() {
  if (ShopWindow* shop = shopOf(ctx_)) shop->sprites().updateObjects();
  GreatCCWorld* w = worldOf(ctx_);
  if (!ready_) {
    ready_ = true;
    if (w) w->tutorials().checkTutorial("tutorial_upgrades");
  }
  // The category that is switched on is disabled while its list is empty and enabled again when it is not.
  const VUpgradeList* l = list();
  const bool empty = !l || l->empty();
  for (ui::WindowId id : {machines_, decoration_, powerups_}) {
    auto* b = dynamic_cast<RDisableButton*>(manager().window(id));
    if (b && b->on() && b->disabled() != empty) {
      b->toggleState(empty);
      break;
    }
  }
  return RBounceWindow::onAnimate();
}

bool VPrelevelWindow::onMessage(const ui::Message& m) {
  GreatCCWorld* w = worldOf(ctx_);
  if (w && w->tutorials().tutorialInEffect()) w->tutorials().handleMessage(m);
  VUpgradeList* l = list();
  if (!ready_ || m.type != ui::kButtonPress || !l) return false;
  auto show = [&](const char* category) {
    category_ = category;
    l->listUpgradeType(category);
  };
  if (m.name == "toggle_upgrades_aesthetic") show("aesthetic");
  else if (m.name == "toggle_upgrades_gameplay") show("station");
  else if (m.name == "toggle_upgrades_player") show("gameplay");
  else if (m.name == "btn_buy") l->purchaseUpgrades();
  else if (m.name == "slider_start") l->scrollBack();
  else if (m.name == "slider_end") l->scrollForward();
  return false;
}

bool VPrelevelWindow::onKeyDown(const ui::KeyEvent& key) {
  VUpgradeList* l = list();
  if (!l) return false;
  if (key.name == "Down") l->scrollForward();
  else if (key.name == "Up") l->scrollBack();
  else return false;
  return true;
}

void VPrelevelWindow::close() {
  if (GreatCCWorld* w = worldOf(ctx_)) w->upgrades().disableTempUpgrades("", "", nullptr);
  if (ShopWindow* shop = shopOf(ctx_)) shop->upgradeComplete();
}

}  // namespace choco::games::greatcc
