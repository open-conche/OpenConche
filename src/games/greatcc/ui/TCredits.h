#pragma once

// TCredits (docs/re/greatcc-meta-flow.md 7): the scrolling credits of scripts/credits.lua, with the keys and the file format of
// Chocolatier 3's, so the engine's scroller does the work. After its children exist it fills the `credits_version` text of the
// screen with string `app_version` (argument: the `version` config value, empty here) and moves itself to the back of its
// siblings.

#include "script/ScriptContext.h"
#include "ui/widgets/CreditsWindow.h"

namespace choco::games::greatcc {

class TCredits : public ui::CreditsWindow {
 public:
  TCredits(ui::WindowManager& wm, script::ScriptContext& ctx) : CreditsWindow(wm), ctx_(ctx) {}
  std::string_view typeName() const override { return "TCredits"; }
  void postChildrenInit(const ui::Style& style) override;

 private:
  script::ScriptContext& ctx_;
};

}  // namespace choco::games::greatcc
