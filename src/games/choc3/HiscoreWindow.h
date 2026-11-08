#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "script/ScriptContext.h"
#include "sdk/HiscoreStore.h"
#include "ui/Window.h"

struct lua_State;

namespace choco::ui {
class TextWindow;
}

namespace choco::game {

// HiscoreWindow (THiscore, docs/re/ledger-and-game-natives.md 8.2): the high-score screen's container. The engine is
// offline, so it always behaves as local-only: a pure local list (up to 10 rows from the current user's mode table, no
// scrolling, no categories), both right panels hidden, no "Connecting to server" text. The Lua side reads the rows back
// with GetLabel(name<i> / score<i> / info<i>) (Reforged) or just shows the children (vanilla).
//
// Online requests cannot succeed: `view`, a category step and SubmissionDone enter the requesting state and the next
// 20 ms tick reports eError, from which `viewlocal` goes back to the local list.
class HiscoreWindow : public ui::Window {
 public:
  enum State { eLocalView = 0, eRequestingCategories = 1, eRequestingScores = 2, eSubmitting = 3, eGlobalView = 4, eError = 5 };
  static constexpr int kRows = 10;
  static constexpr int kTickMs = 20;
  static constexpr int kNameLineHeight = 20;

  HiscoreWindow(ui::WindowManager& wm, script::ScriptContext& ctx);
  ~HiscoreWindow() override;
  static HiscoreWindow* active();  // the newest live one, the target of UpdateText and SubmissionDone

  std::string_view typeName() const override { return "HiscoreWindow"; }
  // Binds name1..10, score1..10, info1..10, 1..10 (the rank Texts) and p1_1..10, starts the tick and runs
  // UpdateText(); UpdateButtons().
  void postChildrenInit(const ui::Style& style) override;
  bool onMessage(const ui::Message& m) override;
  bool onAnimate() override;
  std::string detail() const override;

  int state() const { return state_; }
  // UpdateText: the headers, the right panels, the scroll buttons and the ten rows. Virtual: Chocolatier 1's
  // THiscore adds a gamemode label to the shared screen.
  virtual void updateText();
  // SubmissionDone(qualified): state := eRequestingCategories, result recorded, text refreshed, UpdateButtons run.
  void submissionDone(bool qualified);
  // The score column this game writes. Virtual: Chocolatier 1 reads `formatScore` (the weekly rate,
  // money-formatted and capped at 999,999,999) instead of Chocolatier 3's `score_format`
  // (docs/re/choc1-logo-and-hiscore.md).
  virtual std::string scoreTextOf(const sdk::HiscoreRow& row) const { return scoreText(ctx_, row.score); }

  // ParseGameData: "R-WWWWWWWWW-MMMMMMMMMMMM" to "<rank_R>: <money> in <weeks> weeks" (score_info_format).
  static std::string parseGameData(script::ScriptContext& ctx, std::string_view a);
  // The score column: GetString("score_format", Dollars(score)).
  static std::string scoreText(script::ScriptContext& ctx, long long score);

 private:
  struct Row {
    ui::TextWindow *name = nullptr, *score = nullptr, *info = nullptr, *rank = nullptr;
    ui::Window* p1 = nullptr;
  };
  void runUpdateButtons();
  void beginRequest(State s);
  void setShownByName(const char* name, bool shown);
  static void fitName(ui::TextWindow& t);

 protected:
  script::ScriptContext& ctx_;  // the game subclass reads it (Chocolatier 1's formatScore and gamemode labels)
 private:
  Row rows_[kRows];
  State state_ = eLocalView;
  int submitState_ = 0;  // 0 none, 1 success, 2 did not qualify
  int shownRows_ = 0;
};

// Registers the HiscoreWindow type and GetState, ScoreAvailable, UpdateText, SubmissionDone and LaunchPrivacyPolicy.
void registerHiscoreNatives(lua_State* L, script::ScriptContext& ctx);

}  // namespace choco::game
