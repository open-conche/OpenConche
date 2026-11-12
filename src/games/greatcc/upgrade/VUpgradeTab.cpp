#include "games/greatcc/upgrade/VUpgradeTab.h"

#include <algorithm>
#include <cmath>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/ui/RDisableButton.h"
#include "ui/WindowManager.h"
#include "ui/Layout.h"
#include "ui/widgets/TextWidgets.h"
#include "ui/widgets/Widgets.h"

namespace choco::games::greatcc {

VUpgradeTab::VUpgradeTab(ui::WindowManager& wm, script::ScriptContext& ctx) : LayeredWindow(wm, 5), ctx_(ctx) {}

VUpgradeTab::~VUpgradeTab() { manager().removeMouseListener(id()); }

std::string VUpgradeTab::detail() const {
  return " upgrade=" + (upgrade_ ? upgrade_->name : std::string("-")) + " tab=" + std::to_string(state_) +
         (upgrade_ ? " state=" + std::to_string(upgrade_->state) : std::string()) + (active_ ? " preview" : "") +
         (inside_ ? " inside" : "");
}

const char* VUpgradeTab::moneyValue() const {
  const GreatCCWorld* w = worldOf(ctx_);
  return w && w->settings().mode != 0 ? "current_level_total" : "current_worth";
}

bool VUpgradeTab::canPurchase() const {
  GreatCCWorld* w = worldOf(ctx_);
  if (!upgrade_ || !upgrade_->isAvailable() || !w) return false;
  return static_cast<float>(w->values().getReal(moneyValue(), 0)) >= cost();
}

void VUpgradeTab::setUpgrade(const std::shared_ptr<Upgrade>& u) {
  upgrade_ = u;
  if (upgrade_) {
    setName("upgrade_tab_" + upgrade_->name);
    auto text = [&](const char* child, const std::string& s) {
      if (ui::Window* t = find(child); t && t->hasLabel()) t->setLabel(s);
    };
    text("txt_header", ctx_.strings.sdkLookup(upgrade_->attribute("header")));
    text("txt_content", ctx_.strings.sdkLookup(upgrade_->attribute("content")));
    if (auto* thumb = dynamic_cast<ui::BitmapWindow*>(find("img_thumbnail"))) thumb->setBitmap(upgrade_->attribute("image"), 1.0);
    const std::string price = "$" + std::to_string(static_cast<int>(cost()));
    text("txt_price", price);
    text("txt_price_shadow", price);
    if (ui::Window* b = find("btn_buy_upgrade"); dynamic_cast<RDisableButton*>(b)) buy_ = b->id();
    updateState();
  }
  ui::fitToChildren(*this);
}

void VUpgradeTab::updateState() {
  if (!upgrade_) return;
  if (!upgrade_->isAvailable()) {
    setState(upgrade_->isActive() ? kPurchased : kLocked);
    return;
  }
  const bool can = canPurchase();
  setState(inside_ && can ? kOver : kAvailable);
  if (auto* b = dynamic_cast<RDisableButton*>(manager().window(buy_))) b->toggleState(!can);
}

void VUpgradeTab::setState(int s) {
  if (state_ != s)
    if (ui::Window* b = manager().window(buy_)) b->setShown(!(s == kLocked || s == kPurchased));
  state_ = s;
  setCurrentLayer(s);
}

void VUpgradeTab::setActive(bool on) {
  GreatCCWorld* w = worldOf(ctx_);
  if (!w || !upgrade_) return;
  if (on) {
    if (!upgrade_->isAvailable() || active_) return;
    active_ = true;
    w->upgrades().enableTempUpgrade(upgrade_->name, upgrade_->category, spider_.get());
  } else {
    if (!active_) return;
    active_ = false;
    w->upgrades().disableTempUpgrades(upgrade_->name, upgrade_->category, spider_.get());
  }
}

bool VUpgradeTab::purchaseUpgrade() {
  GreatCCWorld* w = worldOf(ctx_);
  if (!w || !canPurchase()) return false;
  selected_ = false;
  setState(kPurchased);
  setActive(false);
  w->upgrades().enableUpgrade(upgrade_->name, upgrade_->category, true, nullptr);
  const float price = cost();
  w->values().subtractFromValue(moneyValue(), price, 0, true);
  const bool story = w->settings().mode == 0;
  if (story) {
    const float spent = price <= 0.0f ? 0.0f : price;
    w->levels().updateCurrentScore(w->levels().currentScore() - static_cast<int>(spent));
  }
  if (upgrade_->category == "gameplay") w->values().addToValue("num_char_upgrades", 1.0, 0, true);
  if (story) {
    GameStateStore& gs = w->gameStates();
    gs.set("begin-status", "UpgradeChosenStory");
    gs.set("add-value", std::to_string(w->levels().currentLevel()));
    gs.set("add-value", upgrade_->name);
    gs.set("end-status", "UpgradeChosenStory");
  }
  w->sound().playSfx("audio/sfx/purchase_upgrade_button.ogg", false);
  return true;
}

bool VUpgradeTab::mouseDispatch(int x, int y) {
  if (dispatching_) return false;
  // the window under the point, inside this one (the deepest shown one)
  ui::Window* w = this;
  int px = x, py = y;
  for (;;) {
    ui::Window* hit = nullptr;
    for (const auto& c : w->children()) {
      const gfx::RectI& r = c->rect();
      if (c->shown() && px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h) {
        hit = c.get();
        break;
      }
    }
    if (!hit) break;
    px -= hit->rect().x;
    py -= hit->rect().y;
    w = hit;
  }
  if (w == this) return false;
  dispatching_ = true;
  w->onMouseMove(px, py);
  dispatching_ = false;
  return true;
}

bool VUpgradeTab::onMouseMove(int x, int y) {
  // The tab listens to the mouse once the pointer has been on it, and leaves when it is out of the list's own rectangle.
  if (ui::Window* interior = parent(); interior && interior->parent()) {
    const ui::Window* list = interior->parent();
    const gfx::PointF here = ui::WindowManager::screenOrigin(*this), at = ui::WindowManager::screenOrigin(*list);
    const float lx = here.x + static_cast<float>(x) - at.x, ly = here.y + static_cast<float>(y) - at.y;
    if (lx < 0 || lx >= static_cast<float>(list->rect().w) || ly < 0 || ly >= static_cast<float>(list->rect().h)) {
      onMouseLeave();
      return false;
    }
  }
  if (!inside_) {
    manager().addMouseListener(id());
    if (canPurchase() && !selected_) {
      setState(kOver);
      setActive(true);
    }
    inside_ = true;
  }
  return mouseDispatch(x, y);
}

void VUpgradeTab::onMouseLeave() {
  manager().removeMouseListener(id());
  inside_ = false;
  if (!selected_ && canPurchase()) {
    setActive(false);
    setState(kAvailable);
  }
}

bool VUpgradeTab::onMessage(const ui::Message& m) {
  if (m.type == ui::kButtonPress && m.name == "btn_buy_upgrade") purchaseUpgrade();
  return false;
}

}  // namespace choco::games::greatcc
