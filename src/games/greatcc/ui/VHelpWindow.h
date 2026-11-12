#pragma once

// VHelpWindow (docs/re/greatcc-meta-flow.md 7): the help pages, ten layers of a layered window. SetPage(n) (bound by the
// constructor) clamps n to the pages, shows that layer, sets txt_title (found in the top modal) to string help_title_<n>,
// txt_page to "<n+1>/<pages>", disables btn_next on the last page and btn_back on the first. A btn_back or btn_next press
// steps one page.

#include "games/greatcc/natives/ScopedNatives.h"
#include "script/ScriptContext.h"
#include "ui/widgets/Widgets.h"

namespace choco::games::greatcc {

class VHelpWindow : public ui::LayeredWindow {
 public:
  static constexpr int kPages = 10;
  VHelpWindow(ui::WindowManager& wm, script::ScriptContext& ctx);
  std::string_view typeName() const override { return "VHelpWindow"; }
  void postChildrenInit(const ui::Style& style) override;
  bool onMessage(const ui::Message& m) override;
  std::string detail() const override;

  void setPage(int n);
  int page() const { return page_; }

 private:
  script::ScriptContext& ctx_;
  ScopedNatives natives_;
  int page_ = 0;
  ui::WindowId title_ = 0, counter_ = 0, next_ = 0, back_ = 0;
};

}  // namespace choco::games::greatcc
