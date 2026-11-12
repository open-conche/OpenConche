#pragma once

// VChooseAchievements (docs/re/greatcc-debug-tools.md 2.3): the window of scripts/cheats_achievements.lua, reached from the cheats dialog's
// "Choose Achievements" under cheatmode. A scroller holds a toggle per achievement (two columns, in category and name order; the label
// is the raw achievement id); Acquire All, Release All and Test Notify work through the button-press messages of the buttons named
// btn_acquire, btn_release and btn_notify, and each toggle acquires or releases its own achievement. Nothing here shows the end-level
// "new achievement" list.

#include <map>
#include <memory>
#include <string>

#include "games/greatcc/scene/VButton.h"
#include "games/greatcc/scene/VSpriteWindow.h"

namespace choco::games::greatcc {

class VChooseAchievements : public VSpriteWindow {
 public:
  VChooseAchievements(ui::WindowManager& wm, script::ScriptContext& ctx) : VSpriteWindow(wm, ctx) {}
  std::string_view typeName() const override { return "VChooseAchievements"; }
  // The toggles go into the scroller's `sprite_window`: toggle i is `btn_achieve_<name>` at x = (i % 2) * ((W - 40) / 2) + 20,
  // y = (i / 2) * 25 + 20 (W = this window's width), selected when acquired; the inner window is then W wide and (count / 2) * 25 + 30
  // tall and the scroller's interior is fitted to it. Nothing happens without that inner window.
  void postChildrenInit(const ui::Style& style) override;
  bool onMessage(const ui::Message& m) override;
  std::string detail() const override;

  const std::map<std::string, std::shared_ptr<VButton>>& toggles() const { return toggles_; }
  // The toggle of `btn_achieve_<name>`, or null.
  VButton* toggle(const std::string& buttonName) const;

 private:
  std::map<std::string, std::pair<std::string, std::string>> achievements_;  // button name -> (achievement name, category)
  std::map<std::string, std::shared_ptr<VButton>> toggles_;
};

}  // namespace choco::games::greatcc
