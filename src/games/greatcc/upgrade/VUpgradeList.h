#pragma once

// VUpgradeList and VUpgradeSelect (docs/re/greatcc-meta-flow.md 5.2): the scrolling list of the upgrade shop. It lists the offers of
// one category as VUpgradeTabs (built from scripts/upgrade.lua), best first (the look of each, then the cost), drops the ones that
// were bought and closes the gap they leave by sliding the ones below up, ten pixels a frame.

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "games/greatcc/core/UpgradeManager.h"
#include "games/greatcc/scene/Effects.h"
#include "games/greatcc/ui/RScrollable.h"
#include "script/ScriptContext.h"

namespace choco::games::greatcc {

class VUpgradeTab;

// VUpgradeSelect (a VUpgradeSpider): the glow an offer puts on the scene sprites its upgrade is bound to while the upgrade is previewed.
// It is one glow shared by every sprite (peak 1, 500 ms a half wave, endless); `recursive` is the flag of the bound object. The glow's
// own clock is the platform's, so it pulses while the shop's clock stands still behind the dialog.
class UpgradeSelect : public UpgradeSpider {
 public:
  explicit UpgradeSelect(script::ScriptContext& ctx);
  void process(const Upgrade& upgrade, const BoundObject& object) override;
  const std::shared_ptr<GlowEffect>& glow() const { return glow_; }

 private:
  script::ScriptContext& ctx_;
  std::shared_ptr<GlowEffect> glow_;
};

class VUpgradeList : public RScrollable {
 public:
  VUpgradeList(ui::WindowManager& wm, script::ScriptContext& ctx);
  std::string_view typeName() const override { return "VUpgradeList"; }
  // The scroller, then a 20 ms animation.
  void postChildrenInit(const ui::Style& style) override;
  bool onAnimate() override;
  std::string detail() const override;

  // ListUpgradeType: for another category than the one shown, the list is cleared, the previews end, a tab is made for each
  // upgrade of the category that is neither owned nor hidden, the scroll goes back to the top and the tabs are sorted. True when the
  // list has tabs.
  bool listUpgradeType(const std::string& category);
  // PurchaseUpgrades: every tab is asked to buy.
  void purchaseUpgrades();
  void clearList();
  // SortTabs: by look, then cost (stable); bought tabs go; the rest are stacked from the top.
  void sortTabs();
  // The list has no tab (the flag the prelevel window reads at +0x120).
  bool empty() const { return tabs_.empty(); }
  const std::string& category() const { return category_; }
  // The tabs in list order.
  std::vector<VUpgradeTab*> tabs() const;
  const std::shared_ptr<UpgradeSelect>& select() const { return select_; }

 private:
  VUpgradeTab* tab(ui::WindowId id) const;
  void removeTab(ui::WindowId id);

  script::ScriptContext& ctx_;
  std::string category_;
  std::list<ui::WindowId> tabs_;
  std::shared_ptr<UpgradeSelect> select_;
};

}  // namespace choco::games::greatcc
