#pragma once

// VQuestWindow (docs/re/greatcc-meta-flow.md 3.3): the quest panel in the top-left corner of the level screen. It shows the active
// quests of the level, one line each: the title on the left and the status on the right, coloured by the live status (the style's
// `colors`: in progress, complete, failed). It is shown only while a quest is active, its background panel is moved so that it ends
// just under the text, and while shown it rewrites its text every 300 ms.

#include <string>
#include <vector>

#include "gfx/Types.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::games::greatcc {

class VQuestWindow : public ui::Window {
 public:
  VQuestWindow(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), ctx_(ctx) {}
  std::string_view typeName() const override { return "VQuestWindow"; }
  // The children it fills (`txt_quest_names`, `txt_quest_status`, `background`) and the style keys `marginY`, `outlinecolor`,
  // `outlinesize` and `colors` (one colour per status).
  void postChildrenInit(const ui::Style& style) override;
  // OnTaskAnimate: the text again while a quest is shown; the animation ends when none is.
  bool onAnimate() override { return shown_ ? (updateText(), true) : false; }
  std::string detail() const override;

  // UpdateText: one coloured line per active quest in both texts; true when there is one.
  bool updateText();
  // LevelInit (the level loads): the text, the panel shown or hidden, the background placed, the 300 ms update started.
  void levelInit();
  bool questsShown() const { return shown_; }

 private:
  script::ScriptContext& ctx_;
  ui::WindowId names_ = 0, status_ = 0, background_ = 0;
  std::vector<gfx::ColorF> colors_;
  gfx::ColorF outlineColor_{0, 0, 0, 1};
  int outlineSize_ = 0, marginY_ = 0;
  bool shown_ = false;
  int lines_ = 0;
};

}  // namespace choco::games::greatcc
