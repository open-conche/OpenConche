#include "games/choc2/Choc2LedgerNatives.h"

#include <algorithm>
#include <memory>
#include <string>

#include "games/common/DynamicWindow.h"
#include "games/choc2/FactoryStatus.h"
#include "games/choc2/LedgerWindow.h"
#include "games/choc2/Session.h"
#include "ui/widgets/TextWidgets.h"

namespace choco::script {

namespace {

using game::DynamicWindow;
using game::choc2::Ledger;

// With no ledger in the modal chain the ledger natives do nothing (a dialog or menu has none).
void nativeSetLedgerContents(ScriptContext& ctx, std::string tab) {
  if (!Ledger::isTab(tab)) {
    ctx.log.write("[choc2] SetLedgerContents: '" + tab + "' is not a ledger tab (messages, factories, inventory)");
    return;
  }
  // Recorded by each ledger as it shows the tab: with no ledger the native does nothing (TLedger's loop is empty).
  for (Ledger* l : Ledger::all(ctx.wm)) l->setContents(tab);
}

void nativeUpdateLedgerContents(ScriptContext& ctx, std::string tab) {
  for (Ledger* l : Ledger::all(ctx.wm)) l->updateContents(tab);
}

void nativeSetLedgerMode(ScriptContext& ctx, std::string mode) {
  for (Ledger* l : Ledger::all(ctx.wm)) l->setMode(mode);
}

void nativeResetLedger(ScriptContext& ctx, double money) {
  Ledger::resetShared(ctx);  // forgets the tab, so the next ledger opens on the factories
  for (Ledger* l : Ledger::all(ctx.wm)) l->reset(money);
}

void nativeUpdateBadge(ScriptContext& ctx, TableArg gSim) {
  if (!gSim.valid()) return;
  // The money is shared: every ledger shows the one count-up, which is stepped once per tick.
  for (Ledger* l : Ledger::all(ctx.wm)) l->updateBadge(gSim.L, gSim.index);
}

void nativeSetQuestHint(ScriptContext& ctx, std::string text) {
  for (Ledger* l : Ledger::all(ctx.wm)) l->setQuestHint(text);
}

// StartProductionAnim / StopProductionAnim are TFactoryStatus statics (docs/re/choc2-ledger-binary.md section 8), not port
// natives: one global flag that makes the Factories tab's pictures move.
void nativeStartProductionAnim(ScriptContext&) { game::choc2::FactoryStatus::setAnimating(true); }
void nativeStopProductionAnim(ScriptContext&) { game::choc2::FactoryStatus::setAnimating(false); }

void nativeUpdateDynamicContent(ScriptContext& ctx, std::string name) {
  DynamicWindow::updateNamed(ctx.wm, name);
  // A rebuilt badge has empty Texts (the Lua makes them blank); UpdateBadge is not called again, so refill them.
  if (name == "badge")
    for (Ledger* l : Ledger::all(ctx.wm)) l->refreshBadge();
}

ui::TextWindow* textNamed(ScriptContext& ctx, const std::string& name) {
  return dynamic_cast<ui::TextWindow*>(ctx.wm.findInChain(name));
}

// -1 when there is no such Text: the Lua adds 1 and tests > 0, so nothing scrolls.
double nativeBsgGetMaxScroll(ScriptContext& ctx, std::string name) {
  ui::TextWindow* t = textNamed(ctx, name);
  return t ? t->maxStartLine() : -1;
}

// The binary stores the Lua number unchanged as the 0-based first line drawn (docs/re/choc2-ledger-binary.md 12.1): the Lua's
// "1 = top" skips the empty line the messages' leading '#' leaves, and its "max + 1 = bottom" starts the last page one line late
// (k - 1 lines and a blank under them). So the draw must not clamp at maxStartLine(); a start line past the end draws nothing.
void nativeBsgSetStartLine(ScriptContext& ctx, std::string name, double line) {
  if (ui::TextWindow* t = textNamed(ctx, name)) {
    t->setStartLineOverrun(true);
    t->setStartLine(static_cast<int>(line));
  }
}

}  // namespace

void registerChoc2LedgerNatives(lua_State* L, ScriptContext& ctx) {
  bind<nativeSetLedgerContents>(L, ctx, "SetLedgerContents");
  bind<nativeUpdateLedgerContents>(L, ctx, "UpdateLedgerContents");
  bind<nativeSetLedgerMode>(L, ctx, "SetLedgerMode");
  bind<nativeResetLedger>(L, ctx, "ResetLedger");
  bind<nativeUpdateBadge>(L, ctx, "UpdateBadge");
  bind<nativeSetQuestHint>(L, ctx, "SetQuestHint");
  bind<nativeUpdateDynamicContent>(L, ctx, "UpdateDynamicContent");
  bind<nativeStartProductionAnim>(L, ctx, "StartProductionAnim");
  bind<nativeStopProductionAnim>(L, ctx, "StopProductionAnim");
  bind<nativeBsgGetMaxScroll>(L, ctx, "bsgGetMaxScroll");
  bind<nativeBsgSetStartLine>(L, ctx, "bsgSetStartLine");
  ctx.wm.addWindowType("DynamicWindow", [&ctx](ui::WindowManager& wm) { return std::make_unique<DynamicWindow>(wm, ctx, "choc2"); });
  ctx.wm.addWindowType("Ledger", [&ctx](ui::WindowManager& wm) { return std::make_unique<Ledger>(wm, ctx); });
}

}  // namespace choco::script
