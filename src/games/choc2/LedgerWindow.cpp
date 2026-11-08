#include "games/choc2/LedgerWindow.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "games/common/LedgerText.h"
#include "games/common/DynamicWindow.h"
#include "games/choc2/Session.h"
#include "ui/WindowManager.h"
#include "ui/widgets/Widgets.h"

namespace choco::game::choc2 {

namespace {

using ui::Window;

// A button that is hidden also drops capture and its pressed look (EnableWindow's rule).
void setShownAndReset(Window* w, bool shown) {
  if (!w) return;
  w->setShown(shown);
  if (auto* b = dynamic_cast<ui::ButtonWindow*>(w)) b->resetState();
}

Ledger* findLedger(Window& w) {
  if (auto* l = dynamic_cast<Ledger*>(&w)) return l;
  for (const auto& c : w.children())
    if (Ledger* l = findLedger(*c)) return l;
  return nullptr;
}

double numberFromTable(lua_State* L, int table, const char* key, double def) {
  const int top = lua_gettop(L);
  lua_pushstring(L, key);
  lua_gettable(L, table < 0 ? table - 1 : table);
  const double v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def;
  lua_settop(L, top);
  return v;
}

}  // namespace

Ledger* Ledger::current(ui::WindowManager& wm) {
  const auto& modals = wm.modals();
  for (auto it = modals.rbegin(); it != modals.rend(); ++it)
    if (Ledger* l = findLedger(**it)) return l;
  return nullptr;
}

std::vector<Ledger*> Ledger::all(ui::WindowManager& wm) {
  std::vector<Ledger*> out;
  const auto& modals = wm.modals();
  for (auto it = modals.rbegin(); it != modals.rend(); ++it)
    if (Ledger* l = findLedger(**it)) out.push_back(l);
  return out;
}

void Ledger::init(const ui::Style& style) {
  Window::init(style);
  showQuestY_ = static_cast<int>(style.numberOr("showQuestY", 0));
  hideQuestY_ = static_cast<int>(style.numberOr("hideQuestY", 53));
  if (auto data = style.table("data")) {
    // The first ledger starts at the build-time amount. One built over another continues the shared count (the original's
    // Init calls UpdateBadge, which only sets a new target).
    const double money = std::max(0.0, std::trunc(data->numberOr("money", 0)));
    bool others = false;
    for (Ledger* l : all(manager())) others = others || l != this;
    if (others) shared().money.setTargetChoc2(money);
    else shared().money.snap(money);
    rank_ = data->numberOr("rank", 0);
    weeks_ = data->numberOr("weeks", 0);
    days_ = data->numberOr("days", 0);
    score_ = data->numberOr("score", 0);
  }
}

void Ledger::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  // The tab remembered last, else the factories (ResetLedger clears it after every load). The bodies were made with
  // init=false, so showing one builds it.
  const Session* session = sessionOf(ctx_);
  showTab(session && Ledger::isTab(session->ledgerTab) ? session->ledgerTab : kTabs[1]);
  // The bar starts where the shared Y says; ResetLedger leaves it unset, so it starts hidden and slides in if there is a hint.
  // The hint: a ledger built with a quest label starts the shared hint; one built over another shows the shared one.
  Window* qt = find("quest_text");
  bool others = false;
  for (Ledger* l : all(manager())) others = others || l != this;
  if (qt && !qt->label().empty()) shared().questHint = qt->label();
  else if (others) ledgerSetText(qt, shared().questHint);
  else shared().questHint.clear();
  if (questY() == Session::kQuestUnset) questY() = hideQuestY_;
  placeQuest();
  writeBadgeTexts();
  manager().startWindowAnimation(id(), kTickMs);
}

bool Ledger::onAnimate() {
  // The shared state is stepped once per tick, whichever ledger's task runs first; a second task in the same manager tick (two
  // clocks running at once) does nothing, and a ledger that is destroyed leaves the others' tasks to carry on.
  Session& s = shared();
  const double now = manager().nowMs();
  if (s.lastTickMs != now) {
    s.lastTickMs = now;
    const int before = questY();
    stepQuest();
    const bool moved = s.money.step();
    if (moved || questY() != before) syncViews();
  }
  return true;
}

void Ledger::syncViews() {
  bool self = false;
  for (Ledger* l : all(manager())) {
    self = self || l == this;
    l->placeQuest();
    l->writeMoney();
  }
  if (!self) {
    placeQuest();
    writeMoney();
  }
}

ui::Window* Ledger::sibling(const std::string& name) {
  for (Window* w = this; w; w = w->parent())
    if (dynamic_cast<ui::ModalWindow*>(w)) return w->find(name);
  return manager().findInChain(name);
}

void Ledger::placeQuest() {
  if (Window* quest = find("quest")) quest->setPos(quest->rect().x, questY());
}

// One 100 ms step of the quest bar: (hide - show) / 2 px towards hide with no hint, towards show with one. The state is
// shared; the caller moves every ledger's bar to it.
void Ledger::stepQuest() {
  const bool hint = !shared().questHint.empty();
  const int step = std::max(0, hideQuestY_ - showQuestY_) >> 1;
  int& y = questY();
  if (!hint && y != hideQuestY_) {
    y = step > 0 && y < hideQuestY_ ? std::min(y + step, hideQuestY_) : hideQuestY_;
  } else if (hint && y != showQuestY_) {
    y = step > 0 && y > showQuestY_ ? std::max(y - step, showQuestY_) : showQuestY_;
  }
}

std::string Ledger::detail() const {
  return " money=" + std::to_string(static_cast<long long>(counter().display)) + " target=" +
         std::to_string(static_cast<long long>(counter().target)) + " tab=\"" + shownTab() + "\"";
}

std::string Ledger::shownTab() const {
  for (const char* t : kTabs)
    if (const Window* w = Window::find(t); w && w->shown()) return t;
  return {};
}

void Ledger::showTab(const std::string& tab) {
  // Recorded here, so a ledger that opens on the default also remembers it (TLedger::SetContents stores currentKey).
  if (Session* s = sessionOf(ctx_)) s->ledgerTab = tab;
  for (const char* t : kTabs)
    if (Window* w = find(t)) w->setShown(tab == t);
  // A hidden body keeps nothing: its buttons (scroll_up/scroll_down) would otherwise be found by name before the shown body's.
  for (const char* t : kTabs)
    if (tab != t)
      if (auto* h = dynamic_cast<DynamicWindow*>(find(t))) h->clear();
  if (auto* d = dynamic_cast<DynamicWindow*>(find(tab))) d->rebuild();
  if (auto* b = dynamic_cast<ui::ButtonWindow*>(find("btn_" + tab))) b->setOn(true);
}

void Ledger::setContents(const std::string& tab) {
  if (!Ledger::isTab(tab)) {
    ctx_.log.write("[choc2] SetLedgerContents: '" + tab + "' is not a ledger tab (messages, factories, inventory)");
    return;
  }
  showTab(tab);
}

void Ledger::updateContents(const std::string& tab) {
  if (!Ledger::isTab(tab)) {
    ctx_.log.write("[choc2] UpdateLedgerContents: '" + tab + "' is not a ledger tab (messages, factories, inventory)");
    return;
  }
  Window* w = find(tab);
  if (auto* d = dynamic_cast<DynamicWindow*>(w); d && d->shown()) d->rebuild();
}

void Ledger::setMode(const std::string& mode) {
  if (mode != "port" && mode != "map") {
    ctx_.log.write("[choc2] SetLedgerMode: '" + mode + "' is not a mode (port, map)");
    return;
  }
  // They are siblings of the ledger (the button group before it), so they are found through the modal chain.
  // Each ledger sets the buttons of its own modal, so a covered ledger's are right when it is uncovered.
  const bool port = mode == "port";
  shared().mode = mode;
  for (const char* n : {"to_map", "to_map_label"}) setShownAndReset(sibling(n), port);
  for (const char* n : {"to_port", "to_port_label"}) setShownAndReset(sibling(n), !port);
}

void Ledger::reset(double money) {
  shared().money.snap(std::max(0.0, std::trunc(money)));
  shared().questHint.clear();
  writeMoney();
  ledgerSetText(find("quest_text"), {});
}

void Ledger::resetShared(script::ScriptContext& ctx) {
  if (Session* s = sessionOf(ctx)) {
    s->ledgerTab.clear();
    s->questY = Session::kQuestUnset;
  }
}

void Ledger::writeMoney() {
  // From a billion the digits shrink: the binary prefixes the text with an unclosed font tag.
  std::string text = script::dollars(ctx_.lua.L(), counter().display);
  if (counter().display > 999999999) text = " <font size='25'>" + text;
  ledgerSetText(find("money"), std::move(text));
}

// The badge's four Texts, written from the stored fields; only when a badge exists.
void Ledger::writeBadgeTexts() {
  if (!find("badge")) return;
  writeMoney();
  lua_State* L = ctx_.lua.L();
  const int top = lua_gettop(L);
  lua_getglobal(L, "bsgDate");
  if (lua_isfunction(L, -1)) {
    lua_pushnumber(L, weeks_);
    lua_pushnumber(L, days_);
    if (lua_pcall(L, 2, 1, 0) == 0 && lua_type(L, -1) == LUA_TSTRING && lua_strlen(L, -1) > 0)
      ledgerSetText(find("date"), lua_tostring(L, -1));
  }
  lua_settop(L, top);
  const std::string rank = ctx_.strings.sdkLookup("rank_" + std::to_string(static_cast<long long>(rank_)));
  if (rank != "#####") ledgerSetText(find("rank"), rank);
  ledgerSetText(find("score"), scoreText(rank_, score_));
}

std::string Ledger::scoreText(double rank, double score) const {
  if (rank == 0) return {};
  return ctx_.strings.sdkLookup("formatScore", {script::dollars(ctx_.lua.L(), std::trunc(score))});
}

void Ledger::updateBadge(lua_State* L, int index) {
  if (!lua_istable(L, index)) return;
  // Unsigned in the binary: a negative amount is 0. Missing keys keep the stored values.
  // Shared: the first ledger sets the target, the others find it already set.
  shared().money.setTargetChoc2(numberFromTable(L, index, "money", counter().target));
  rank_ = numberFromTable(L, index, "rank", rank_);
  weeks_ = numberFromTable(L, index, "weeks", weeks_);
  days_ = numberFromTable(L, index, "days", days_);
  score_ = numberFromTable(L, index, "score", score_);
  writeBadgeTexts();
}

void Ledger::setQuestHint(const std::string& hint) {
  shared().questHint = hint;
  ledgerSetText(find("quest_text"), hint);
}

}  // namespace choco::game::choc2
