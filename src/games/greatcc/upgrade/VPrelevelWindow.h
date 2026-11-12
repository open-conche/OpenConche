#pragma once

// VPrelevelWindow (docs/re/greatcc-meta-flow.md 5.1): the upgrade shop between levels, a bouncing window of scripts/prelevel.lua. It
// shows the shop's scene at half size in its frame, offers the upgrades of three categories through the VUpgradeList, starts the
// upgrade tutorial, and ends with the Lua `Close()` (the preview upgrades go and the shop is told the upgrades are complete).

#include "games/greatcc/natives/ScopedNatives.h"
#include "games/greatcc/ui/RBounceWindow.h"
#include "script/ScriptContext.h"

namespace choco::games::greatcc {

class VUpgradeList;

class VPrelevelWindow : public RBounceWindow {
 public:
  VPrelevelWindow(ui::WindowManager& wm, script::ScriptContext& ctx);
  std::string_view typeName() const override { return "VPrelevelWindow"; }
  // The bounce, the list, InitUpgrades, a 10 ms animation, this window as the modal's default focus.
  void postChildrenInit(const ui::Style& style) override;
  // The window's image, then the scene at half size in the frame on the right.
  void draw(ui::DrawContext& ctx) const override;
  // The shop's sprites are updated; the first frame starts the tutorial; the selected category's button is disabled while its list
  // is empty; then the bounce.
  bool onAnimate() override;
  // The message goes to a running tutorial first. A category button lists that category; slider_start and slider_end scroll.
  bool onMessage(const ui::Message& m) override;
  // Down and Up scroll the list.
  bool onKeyDown(const ui::KeyEvent& key) override;
  std::string detail() const override;

  // InitUpgrades: a category button is disabled when nothing is left to buy in it; the first with something (Machines, Decoration,
  // Power-ups in that order) is listed and switched on; then the upgrade tutorial is asked for.
  void initUpgrades();
  // Close (the Lua global): the previews end and the shop completes the upgrades (ShopWindow::UpgradeComplete).
  void close();

  VUpgradeList* list() const;
  const std::string& category() const { return category_; }
  bool ready() const { return ready_; }

 private:
  script::ScriptContext& ctx_;
  ScopedNatives natives_;
  ui::WindowId list_ = 0, machines_ = 0, decoration_ = 0, powerups_ = 0;
  std::string category_;
  bool ready_ = false;
};

}  // namespace choco::games::greatcc
