#pragma once

#include <memory>
#include <string>
#include <vector>

#include "games/common/MoneyCounter.h"
#include "games/choc2/Session.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::game::choc2 {

// Chocolatier 2's Ledger (docs/re/choc2-port-and-ledger.md section 2, design docs/design/choc2-port-and-ledger.md 2.3). The
// native packs no drawing: its children are Lua-built (the pictures, the three DynamicWindow tab bodies with init=false, the
// radio buttons, the badge DynamicWindow and the `quest` bar). This window keeps the money counter and finds the children by
// name on each use, because a DynamicWindow rebuild replaces them. Keys read: `data` (the money at build time), `showQuestY` and
// `hideQuestY`. Not a singleton: every port and map modal builds its own, and the tab the player chose lives in Session.
//
// Every native effect here is an INFERENCE from the call sites (the Chocolatier 2 exe is packed); see the notes on each method.
class Ledger : public ui::Window {
 public:
  Ledger(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), ctx_(ctx) {
    if (!sessionOf(ctx)) ownSession_ = std::make_unique<Session>();
  }
  std::string_view typeName() const override { return "Ledger"; }
  void init(const ui::Style& style) override;
  void postChildrenInit(const ui::Style& style) override;
  bool onAnimate() override;
  std::string detail() const override;

  static constexpr const char* kTabs[3] = {"messages", "factories", "inventory"};
  static bool isTab(const std::string& s) { return s == kTabs[0] || s == kTabs[1] || s == kTabs[2]; }
  // TRedrawTask's rate 10 (docs/re/choc2-ledger-binary.md section 3): 100 ms per money and quest-bar step.
  static constexpr double kTickMs = 100;

  // The first Ledger in the modal chain, from the top modal down; null when there is none.
  static Ledger* current(ui::WindowManager& wm);
  // Every ledger in the modal chain, topmost first. A dialog over the port view (the market, a shop) has a ledger of its own
  // and the port view's is still there underneath: a state change must reach both, or the lower one is stale when the
  // dialog closes.
  static std::vector<Ledger*> all(ui::WindowManager& wm);

  // SetLedgerContents: shows only the tab's body and rebuilds it, and turns its radio button on. A name that is not a tab is
  // logged and ignored.
  void setContents(const std::string& tab);
  // UpdateLedgerContents: rebuilds the tab's body when it is the one shown; a hidden body is rebuilt when it is next shown.
  void updateContents(const std::string& tab);
  // SetLedgerMode: "port" shows to_map (and its label) and hides to_port; "map" does the reverse. The native calls it on every
  // ledger in the modal chain.
  void setMode(const std::string& mode);
  // ResetLedger(money): the shared counter jumps to the amount and the quest line is cleared (the bar then slides down); this
  // view is rewritten. Idempotent, so the native calls it on every ledger. The remembered tab and bar Y are reset by
  // resetShared, once per call.
  void reset(double money);
  static void resetShared(script::ScriptContext& ctx);
  // UpdateBadge(gSim): the table is at `index` of `L`. Targets t.money (fixed step max(1, gap / 7)) and writes the money,
  // date, rank and score Texts. It does not rebuild the badge: that is UpdateDynamicContent("badge").
  void updateBadge(lua_State* L, int index);
  // Writes the badge Texts again from the stored fields (after the badge DynamicWindow was rebuilt).
  void refreshBadge() { writeBadgeTexts(); }
  // SetQuestHint: the quest line. The bar slides to showQuestY (non-empty) or hideQuestY, 26 px per 100 ms tick.
  void setQuestHint(const std::string& hint);

  // The shared money counter (Session::money): every ledger shows it.
  const MoneyCounter& counter() const { return shared().money; }
  // The tab whose body is shown ("" when none is).
  std::string shownTab() const;
  int showQuestY() const { return showQuestY_; }
  int hideQuestY() const { return hideQuestY_; }

  // The score line for a rank and a weekly score: "" at rank 0, else the formatScore string with the whole-dollar score.
  std::string scoreText(double rank, double score) const;

 private:
  void showTab(const std::string& tab);
  void writeMoney();
  void writeBadgeTexts();
  Session& shared() const { return sessionOf(ctx_) ? *sessionOf(ctx_) : *ownSession_; }
  int& questY() { return shared().questY; }
  // The window holding the ledger's sibling buttons (to_map, to_port): the modal this ledger is in.
  ui::Window* sibling(const std::string& name);
  // Moves every ledger's view (this one included) to the shared money and quest Y.
  void syncViews();
  void placeQuest();
  void stepQuest();

  script::ScriptContext& ctx_;
  int showQuestY_ = 0, hideQuestY_ = 53;
  std::unique_ptr<Session> ownSession_;  // only when there is no Session (a ledger built outside the game)
  double rank_ = 0, weeks_ = 0, days_ = 0, score_ = 0;  // the last UpdateBadge table (unsigned in the binary)
};

}  // namespace choco::game::choc2
