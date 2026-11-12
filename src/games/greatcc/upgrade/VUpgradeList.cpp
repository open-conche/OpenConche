#include "games/greatcc/upgrade/VUpgradeList.h"

#include <algorithm>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/scene/VObject.h"
#include "games/greatcc/shop/ShopWindow.h"
#include "games/greatcc/ui/RWindow.h"
#include "games/greatcc/upgrade/VUpgradeTab.h"
#include "script/RunFile.h"
#include "ui/Layout.h"
#include "ui/WindowManager.h"
#include "ui/widgets/InputWidgets.h"

namespace choco::games::greatcc {

namespace {

// A glow that reads the platform clock instead of the scene's (its own TClock in the original).
class SelectGlow : public GlowEffect {
 public:
  SelectGlow(script::ScriptContext& ctx) : GlowEffect(1.0f, 500, -1.0f, 0.0f), ctx_(ctx) {}
  bool onUpdate(double, bool softPaused) override { return GlowEffect::onUpdate(ctx_.wm.nowMs(), softPaused); }

 private:
  script::ScriptContext& ctx_;
};

}  // namespace

UpgradeSelect::UpgradeSelect(script::ScriptContext& ctx) : ctx_(ctx), glow_(std::make_shared<SelectGlow>(ctx)) {}

void UpgradeSelect::process(const Upgrade& upgrade, const BoundObject& object) {
  GreatCCWorld* w = worldOf(ctx_);
  auto* shop = w ? dynamic_cast<ShopWindow*>(ctx_.wm.window(w->shopWindowId())) : nullptr;
  VSprite* sprite = shop ? shop->sprites().findSpriteByName(object.name) : nullptr;
  if (!sprite) return;
  glow_->setPropagate(object.recursive);
  if (upgrade.isActive()) sprite->addEffect(glow_);
  else sprite->removeEffect(glow_);
}

VUpgradeList::VUpgradeList(ui::WindowManager& wm, script::ScriptContext& ctx)
    : RScrollable(wm), ctx_(ctx), select_(std::make_shared<UpgradeSelect>(ctx)) {}

void VUpgradeList::postChildrenInit(const ui::Style& style) {
  RScrollable::postChildrenInit(style);
  manager().startWindowAnimation(id(), 20);
}

std::string VUpgradeList::detail() const {
  return RScrollable::detail() + " category=" + (category_.empty() ? std::string("-") : category_) + " tabs=" + std::to_string(tabs_.size());
}

VUpgradeTab* VUpgradeList::tab(ui::WindowId id) const { return dynamic_cast<VUpgradeTab*>(manager().window(id)); }

std::vector<VUpgradeTab*> VUpgradeList::tabs() const {
  std::vector<VUpgradeTab*> out;
  for (ui::WindowId id : tabs_)
    if (VUpgradeTab* t = tab(id)) out.push_back(t);
  return out;
}

void VUpgradeList::removeTab(ui::WindowId id) {
  ui::Window* t = manager().window(id);
  if (t && t->parent()) manager().destroyWindow(*t);
}

void VUpgradeList::clearList() {
  for (ui::WindowId id : tabs_) removeTab(id);
  tabs_.clear();
}

bool VUpgradeList::listUpgradeType(const std::string& category) {
  if (category != category_) {
    clearList();
    category_ = category;
    if (GreatCCWorld* w = worldOf(ctx_)) {
      UpgradeManager& um = w->upgrades();
      um.disableTempUpgrades("", "", nullptr);
      ui::Window* in = interior();
      for (const auto& [name, u] : um.typeMap(category)) {
        if (!u || u->isActive() || u->hidden || !in) continue;
        // scripts/upgrade.lua is run into a window of its own; its upgrade_tab is taken out of it
        auto holder = std::make_unique<RWindow>(manager());
        script::runFileInto(ctx_, ctx_.lua.L(), *holder, "scripts/upgrade.lua");
        auto* t = dynamic_cast<VUpgradeTab*>(holder->find("upgrade_tab"));
        if (!t) {
          ctx_.log.write("[greatcc] scripts/upgrade.lua made no upgrade_tab");
          continue;
        }
        std::unique_ptr<ui::Window> owned = t->parent()->orphan(*t);
        t->setUpgrade(u);
        t->setSelectionSpider(select_);
        tabs_.push_back(t->id());
        in->adoptFront(std::move(owned));
      }
    }
    setInteriorPosition(1.0f);  // the slider to 1.0 and back to 0.0: the scroll goes to the top
    setInteriorPosition(0.0f);
    sortTabs();
  }
  return !tabs_.empty();
}

void VUpgradeList::purchaseUpgrades() {
  for (VUpgradeTab* t : tabs()) t->purchaseUpgrade();
}

void VUpgradeList::sortTabs() {
  std::vector<ui::WindowId> order(tabs_.begin(), tabs_.end());
  std::stable_sort(order.begin(), order.end(), [this](ui::WindowId a, ui::WindowId b) {
    const VUpgradeTab *ta = tab(a), *tb = tab(b);
    if (!ta || !tb) return ta != nullptr && tb == nullptr;
    if (ta->tabState() == tb->tabState()) return ta->cost() < tb->cost();
    return ta->tabState() < tb->tabState();
  });
  tabs_.assign(order.begin(), order.end());
  int y = 0;
  for (auto it = tabs_.begin(); it != tabs_.end();) {
    VUpgradeTab* t = tab(*it);
    if (!t) {
      it = tabs_.erase(it);
    } else if (t->tabState() == VUpgradeTab::kPurchased) {
      removeTab(*it);
      it = tabs_.erase(it);
    } else {
      t->setPos(0, y);
      y += t->rect().h;
      ++it;
    }
  }
  if (ui::Window* in = interior()) {
    in->setSize(0, 0);
    ui::fitToChildren(*in);
  }
  auto* sl = dynamic_cast<ui::SliderWindow*>(slider());
  setInteriorPosition(sl ? sl->value() : 0.0f);
}

bool VUpgradeList::onAnimate() {
  int y = 0;
  bool moved = false;
  for (auto it = tabs_.begin(); it != tabs_.end();) {
    VUpgradeTab* t = tab(*it);
    if (!t) {
      it = tabs_.erase(it);
      continue;
    }
    if (t->tabState() == VUpgradeTab::kPurchased) {
      removeTab(*it);
      it = tabs_.erase(it);
      continue;
    }
    t->updateState();
    if (y < t->rect().y) {
      const int d = t->rect().y - y;
      t->setPos(t->rect().x, t->rect().y + (d < 11 ? -d : -10));
      moved = true;
    }
    y += t->rect().h;
    ++it;
  }
  if (moved) {
    if (ui::Window* in = interior()) {
      in->setSize(0, 0);
      ui::fitToChildren(*in);
    }
    auto* sl = dynamic_cast<ui::SliderWindow*>(slider());
    setInteriorPosition(sl ? sl->value() : 0.0f);
  }
  return true;
}

}  // namespace choco::games::greatcc
