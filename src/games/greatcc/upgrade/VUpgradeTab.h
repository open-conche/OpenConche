#pragma once

// VUpgradeTab (docs/re/greatcc-meta-flow.md 5.3): one offer of the upgrade shop, a layered window of five looks (available, over,
// selected, locked, purchased) built by scripts/upgrade.lua. It shows the upgrade's header, text, picture and price, keeps its look
// from the upgrade's state and the player's cash, previews the upgrade in the scene while the pointer is on it, and buys it when
// its price button is pressed.

#include <memory>
#include <string>

#include "games/greatcc/core/UpgradeManager.h"
#include "script/ScriptContext.h"
#include "ui/widgets/Widgets.h"

namespace choco::games::greatcc {

class RDisableButton;

class VUpgradeTab : public ui::LayeredWindow {
 public:
  enum State { kAvailable = 0, kOver = 1, kSelected = 2, kLocked = 3, kPurchased = 4 };

  VUpgradeTab(ui::WindowManager& wm, script::ScriptContext& ctx);
  ~VUpgradeTab() override;
  std::string_view typeName() const override { return "VUpgradeTab"; }
  std::string detail() const override;

  // SetUpgrade: the window is named upgrade_tab_<name>; txt_header and txt_content show the strings of the upgrade's `header` and
  // `content`, img_thumbnail its `image`, the price texts "$<cost>"; the buy button is found; the look is brought up to date; the
  // window is fitted to its children.
  void setUpgrade(const std::shared_ptr<Upgrade>& u);
  void setSelectionSpider(std::shared_ptr<UpgradeSpider> s) { spider_ = std::move(s); }
  const std::shared_ptr<Upgrade>& upgrade() const { return upgrade_; }
  int tabState() const { return state_; }
  bool active() const { return active_; }
  bool pointerInside() const { return inside_; }
  float cost() const { return upgrade_ ? upgrade_->cost : 0.0f; }
  // The upgrade is available and the cash (current_worth, in an endless run current_level_total) covers its cost.
  bool canPurchase() const;
  // UpdateState: a locked or bought upgrade shows the locked or purchased look; an available one shows `over` while the pointer is on
  // it and it is affordable, else `available`; the buy button is disabled while it cannot be bought.
  void updateState();
  // SetState: the buy button is hidden for the locked and purchased looks, then the layer.
  void setState(int s);
  // SetActive(true) previews the upgrade (a temporary upgrade, with the list's glow); SetActive(false) ends the preview.
  void setActive(bool on);
  // PurchaseUpgrade: when it can be bought, the purchased look, the upgrade is owned and saved, the cash and the location's score go
  // down by the cost, a character upgrade is counted, the click sounds. False when it cannot.
  bool purchaseUpgrade();

  bool onMouseDown(int, int) override { return false; }
  bool onMouseMove(int x, int y) override;
  void onMouseLeave() override;
  bool onMessage(const ui::Message& m) override;

 private:
  // MouseDispatch: the move goes on to the window under the pointer inside this one; true when there is one.
  bool mouseDispatch(int x, int y);
  const char* moneyValue() const;

  script::ScriptContext& ctx_;
  std::shared_ptr<Upgrade> upgrade_;
  std::shared_ptr<UpgradeSpider> spider_;
  ui::WindowId buy_ = 0;
  int state_ = -1;
  bool inside_ = false;     // 0x48: the pointer is in (the tab listens for the mouse)
  bool selected_ = false;   // 0x49: never set by the shipped flow
  bool active_ = false;     // 0x4a: the preview is on
  bool dispatching_ = false;  // 0x5c
};

}  // namespace choco::games::greatcc
