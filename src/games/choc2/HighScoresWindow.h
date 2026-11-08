#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::ui {
class TextWindow;
}

namespace choco::game::choc2 {

// HighScores (Chocolatier 2): the window of ui/hiscore_local.lua, offline and local only (the `hiscorelocal` policy,
// docs/re/hiscore.md section 3 option B). The Lua builds the controls and the ten rows by name and calls natives for its
// buttons, so unlike Chocolatier 3's HiscoreWindow nothing here reads button messages: ViewLocal, ViewGlobal, NextMode,
// PreviousMode, NextCategory, PreviousCategory, ScrollUp and ScrollDown act on the newest live window.
//
// Rows: `name<i>`, `score<i>`, the rank Text `<i>` and the icon `p1_<i>` for i = 1..10 (the Lua's ScoreList). The native
// fills them from the current user's score table for the viewed game mode (HiscoreStore: best first, one row per user,
// at most 500), and hides the unused ones; the P1 icon marks online players and stays hidden. `mode` shows the viewed
// mode's name; Story Mode (0) and Free Play Mode (1) are the two tables, and NextMode / PreviousMode step through them
// (INFERENCE: the Lua has the two buttons and a `mode` Text, nothing says what they list). The local view shows the top ten
// only: offset 0 and both scroll buttons hidden, ScrollUp and ScrollDown do nothing (docs/re/hiscore.md 2.4; paging exists
// only in the online view).
// Online views do not exist: ViewGlobal and the category natives log once and leave the local list on screen.
class HighScores : public ui::Window {
 public:
  static constexpr int kRows = 10;
  static constexpr int kModes = 2;

  HighScores(ui::WindowManager& wm, script::ScriptContext& ctx);
  ~HighScores() override;
  static HighScores* active();  // the newest live one, the target of the natives

  std::string_view typeName() const override { return "HighScores"; }
  void postChildrenInit(const ui::Style& style) override;
  std::string detail() const override;

  // ViewLocal: back to the first page of the local list of the viewed mode.
  void viewLocal();
  // NextMode / PreviousMode: the viewed mode steps by one (wrapping) and the list returns to the first page.
  void stepMode(int dir);
  // ScrollUp / ScrollDown: no effect on the local list (the buttons are hidden); refreshes the view.
  void scroll(int dir);
  // The headers, the rows and the scroll buttons for the current view.
  void updateText();

  int viewMode() const { return mode_; }
  int offset() const { return offset_; }
  int shownRows() const { return shown_; }
  int rowCount() const;  // the length of the viewed mode's table

  // The score column: the formatScore string ("%1%/week") with the whole-dollar score.
  static std::string scoreText(script::ScriptContext& ctx, long long score);

 private:
  struct Row {
    ui::TextWindow *name = nullptr, *score = nullptr, *rank = nullptr;
    ui::Window* p1 = nullptr;
  };
  void setShownByName(const char* name, bool shown);

  script::ScriptContext& ctx_;
  Row rows_[kRows];
  int mode_ = 0;
  int offset_ = 0;
  int shown_ = 0;
};

// The natives and the window type: registered from registerChoc2MenuNatives.
void registerHighScoresNatives(lua_State* L, script::ScriptContext& ctx);

}  // namespace choco::game::choc2
