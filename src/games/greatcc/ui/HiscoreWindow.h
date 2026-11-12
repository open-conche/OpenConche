#pragma once

// HiscoreWindow (THiscore, docs/re/greatcc-meta-flow.md 7.1): the container of scripts/hiscore.lua. Its constructor binds GetState,
// ScoreAvailable, UpdateText and LaunchPrivacyPolicy; after its children exist it binds the ten rows (name, score, rank, p1 icon),
// moves the left panel, binds the `gamemode` text and runs UpdateText and UpdateButtons. This engine has no online tables, so the
// config's `hiscorelocal` is always on (the screen shows the local table of the current game mode and nothing else) and the
// online states never begin: the window stays in state 0.

#include <string>

#include "games/greatcc/natives/ScopedNatives.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::ui {
class TextWindow;
}

namespace choco::games::greatcc {

class HiscoreWindow : public ui::Window {
 public:
  static constexpr int kRows = 10;
  HiscoreWindow(ui::WindowManager& wm, script::ScriptContext& ctx);
  std::string_view typeName() const override { return "HiscoreWindow"; }
  void postChildrenInit(const ui::Style& style) override;
  bool onMessage(const ui::Message& m) override;
  std::string detail() const override;

  int state() const { return state_; }
  // UpdateText: every row and named text hidden, the game mode's heading, the right panels, then the local table's rows.
  void updateText();
  // The current mode's rows shown so far.
  int shownRows() const { return shown_; }

 private:
  struct Row {
    ui::TextWindow *name = nullptr, *score = nullptr, *rank = nullptr;
    ui::Window* p1 = nullptr;
  };
  bool localOnly() const;
  void hideAll();
  void updateScoresTable();
  void runUpdateButtons();
  void setShownByName(const char* name, bool shown);
  void changeMode(int delta);

  script::ScriptContext& ctx_;
  ScopedNatives natives_;
  Row rows_[kRows];
  ui::TextWindow* gameMode_ = nullptr;
  int state_ = 0;  // 0 local view (the only one reached offline)
  int shown_ = 0;
};

}  // namespace choco::games::greatcc
