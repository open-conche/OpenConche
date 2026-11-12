#include "games/greatcc/ui/HiscoreWindow.h"

#include <cstdlib>

#include "games/greatcc/GreatCCWorld.h"
#include "sdk/Users.h"
#include "ui/ScriptSink.h"
#include "ui/WindowManager.h"
#include "ui/widgets/TextWidgets.h"

namespace choco::games::greatcc {

namespace {

HiscoreWindow* ownerWindow(lua_State* L) {
  script::ScriptContext& ctx = script::contextOf(L);
  return dynamic_cast<HiscoreWindow*>(ctx.wm.window(ScopedNatives::ownerOf(L)));
}
int luaGetState(lua_State* L) {
  auto* w = ownerWindow(L);
  lua_pushnumber(L, w ? w->state() : 0);
  return 1;
}
int luaScoreAvailable(lua_State* L) {
  script::ScriptContext& ctx = script::contextOf(L);
  const GreatCCWorld* world = worldOf(ctx);
  lua_pushboolean(L, ctx.users && world && ctx.users->scoreAvailable(world->settings().hiscoreMode()));
  return 1;
}
int luaUpdateText(lua_State* L) {
  if (auto* w = ownerWindow(L)) w->updateText();
  return 0;
}
// THiscore::LaunchPrivacyPolicy opens a playfirst.com page: the page is gone and no browser is opened.
int luaLaunchPrivacyPolicy(lua_State* L) {
  script::contextOf(L).log.write("[greatcc] LaunchPrivacyPolicy: no browser is opened (the page is gone)");
  return 0;
}

}  // namespace

HiscoreWindow::HiscoreWindow(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), ctx_(ctx), natives_(ctx) {
  natives_.bindOwned("GetState", &luaGetState, id());
  natives_.bindOwned("ScoreAvailable", &luaScoreAvailable, id());
  natives_.bindOwned("UpdateText", &luaUpdateText, id());
  natives_.bindOwned("LaunchPrivacyPolicy", &luaLaunchPrivacyPolicy, id());
}

bool HiscoreWindow::localOnly() const { return std::strtol(ctx_.config.get("hiscorelocal").c_str(), nullptr, 10) != 0; }

void HiscoreWindow::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  for (int i = 0; i < kRows; ++i) {
    const std::string n = std::to_string(i + 1);
    Row& r = rows_[i];
    r.name = dynamic_cast<ui::TextWindow*>(find("name" + n));
    r.score = dynamic_cast<ui::TextWindow*>(find("score" + n));
    r.rank = dynamic_cast<ui::TextWindow*>(find(n));
    r.p1 = find("p1_" + n);
  }
  // Local-only: the table's panel moves 130 px to the right (0x82), into the middle of the screen where the right panels were.
  if (localOnly())
    if (ui::Window* left = find("leftpanel")) left->setPos(left->rect().x + 0x82, left->rect().y);
  gameMode_ = dynamic_cast<ui::TextWindow*>(find("gamemode"));
  updateText();
  runUpdateButtons();
}

void HiscoreWindow::setShownByName(const char* name, bool shown) {
  if (ui::Window* w = find(name)) w->setShown(shown);
}

void HiscoreWindow::hideAll() {
  for (Row& r : rows_)
    for (ui::Window* w : {static_cast<ui::Window*>(r.name), static_cast<ui::Window*>(r.score), static_cast<ui::Window*>(r.rank), r.p1})
      if (w) w->setShown(false);
  for (const char* n : {"eligible", "eligibleasterisk", "server", "error", "info", "category", "local", "global", "topplayers",
                        "congratulations", "dnq", "yourrank", "yourrankglobalinfo", "rankvalue", "scrollup", "scrolldown"})
    setShownByName(n, false);
}

void HiscoreWindow::updateText() {
  hideAll();
  const GreatCCWorld* world = worldOf(ctx_);
  const int mode = world ? world->settings().mode : 0;
  if (gameMode_ && mode >= 0 && mode <= 3)
    gameMode_->setLabel("<outline color='000000' size='1'>" + ctx_.strings.sdkLookup("scores_gamemode" + std::to_string(mode + 1)) +
                        "</outline>");
  else if (gameMode_)
    gameMode_->setLabel("");
  // Local-only: neither right panel. (Online, the big one shows in the local view and the small one otherwise.)
  const bool local = localOnly();
  setShownByName("rightpanel", !local && state_ == 0);
  setShownByName("rightpanelsmall", !local && state_ != 0);
  updateScoresTable();
  if (state_ == 0) {
    // The eligibility texts and the info text show for a user with a row, but local-only hides them again at the end.
    if (!local) {
      setShownByName("info", true);
      if (ctx_.users && world && ctx_.users->scoreAvailable(world->settings().hiscoreMode())) {
        setShownByName("eligible", true);
        setShownByName("eligibleasterisk", true);
      }
    }
  }
}

void HiscoreWindow::updateScoresTable() {
  // UpdateScoresTable, local branch: "local" shows unless local-only, "topplayers" only when local-only, no scrolling.
  const bool local = localOnly();
  setShownByName("local", !local);
  setShownByName("topplayers", local);
  setShownByName("scrollup", false);
  setShownByName("scrolldown", false);
  const GreatCCWorld* world = worldOf(ctx_);
  static const std::vector<sdk::HiscoreRow> none;
  const std::vector<sdk::HiscoreRow>& list =
      ctx_.users && world ? ctx_.users->hiscores().rows(world->settings().hiscoreMode()) : none;
  shown_ = static_cast<int>(std::min<std::size_t>(list.size(), kRows));
  for (int i = 0; i < shown_; ++i) {
    const sdk::HiscoreRow& row = list[static_cast<std::size_t>(i)];
    Row& r = rows_[i];
    if (r.name) {
      r.name->setLabel(row.user);  // RShrinkText: the line height shrinks to fit the box
      r.name->setShown(true);
    }
    if (r.score) {
      r.score->setLabel(std::to_string(row.score));
      r.score->setShown(true);
    }
    if (r.rank) {
      r.rank->setLabel(std::to_string(row.rank) + ".");
      r.rank->setShown(true);
    }
    // The p1 icon marks a local row that is also on the online table: never shown here (local-only, and `hiscoreanon` is on).
  }
}

void HiscoreWindow::runUpdateButtons() {
  if (ui::ScriptSink* sink = manager().scriptSink()) sink->runString("UpdateButtons();");
}

void HiscoreWindow::changeMode(int delta) {
  GreatCCWorld* world = worldOf(ctx_);
  if (!world) return;
  int n = world->settings().mode + delta;
  if (n > 3) n = 0;
  if (n < 0) n = 3;
  world->setGameMode(ctx_, n);
  updateText();
  runUpdateButtons();
}

bool HiscoreWindow::onMessage(const ui::Message& m) {
  if (m.type != ui::kButtonPress) return false;
  const std::string& n = m.name;
  if (n == "gamemodeleft" || n == "gamemoderight") {
    if (state_ != 0 && state_ != 4) return false;
    changeMode(n == "gamemodeleft" ? -1 : 1);
    return true;
  }
  if (n == "viewlocal") {
    state_ = 0;
    updateText();
    runUpdateButtons();
    return false;
  }
  if (n == "scrollup" || n == "scrolldown") {
    updateText();  // the local table never scrolls
    return false;
  }
  if (n == "view" || n == "categoryleft" || n == "categoryright" || n == "submit") {
    // The online tables and the submission need the playfirst.com server: nothing happens (the buttons are disabled by UpdateButtons).
    ctx_.log.write("[greatcc] high scores: '" + n + "' needs the online service, which does not exist here");
    return false;
  }
  return false;
}

std::string HiscoreWindow::detail() const { return " state=" + std::to_string(state_) + " rows=" + std::to_string(shown_); }

}  // namespace choco::games::greatcc
