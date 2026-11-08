#include "games/choc3/HiscoreWindow.h"

#include <cmath>
#include <cstdlib>
#include <limits>

#include "sdk/Users.h"
#include "script/Bind.h"
#include "script/Formats.h"
#include "script/LuaApi.h"
#include "script/ScriptHost.h"
#include "ui/ScriptSink.h"
#include "ui/Style.h"
#include "ui/WindowManager.h"
#include "ui/widgets/TextWidgets.h"
#include "games/common/SdkNames.h"

namespace choco::game {

namespace {

HiscoreWindow* gActive = nullptr;
// The offline request failure text (docs/re/hiscore.md 3: the likely string of the original; it has no string-table key).
constexpr const char* kOfflineError = "Unable to connect to server. Please try again later.";

std::string dollars(script::ScriptContext& ctx, double v) { return script::dollars(ctx.lua.L(), v); }

}  // namespace

HiscoreWindow::HiscoreWindow(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), ctx_(ctx) { gActive = this; }

HiscoreWindow::~HiscoreWindow() {
  if (gActive == this) gActive = nullptr;
}

HiscoreWindow* HiscoreWindow::active() { return gActive; }

std::string HiscoreWindow::scoreText(script::ScriptContext& ctx, long long score) {
  return ctx.strings.sdkLookup("score_format", {dollars(ctx, static_cast<double>(score)), "", "", "", ""});
}

std::string HiscoreWindow::parseGameData(script::ScriptContext& ctx, std::string_view a) {
  const std::string s(a);
  const std::string rank = ctx.strings.sdkLookup(std::string("rank_") + (s.empty() ? '1' : s[0]));
  double weeks = 0, money = 0;
  const std::size_t d1 = s.find('-');
  const std::size_t d2 = d1 == std::string::npos ? d1 : s.find('-', d1 + 1);
  if (d2 != std::string::npos) {
    weeks = std::strtod(s.substr(d1 + 1, d2 - d1 - 1).c_str(), nullptr);
    money = std::strtod(s.c_str() + d2 + 1, nullptr);
  }
  if (weeks >= 1e9) weeks = -std::numeric_limits<double>::infinity();
  if (!(money > 0)) money = 0;
  // The week count is a plain thousands-separated number: Dollars with its money sign taken off again.
  std::string weeksText = dollars(ctx, weeks);
  const script::MoneyFormat m = script::moneyFormat(ctx.lua.L());
  if (const std::size_t at = weeksText.find(m.sign); at != std::string::npos && !m.sign.empty()) weeksText.erase(at, m.sign.size());
  return ctx.strings.sdkLookup("score_info_format", {rank, dollars(ctx, money), weeksText, "", ""});
}

void HiscoreWindow::fitName(ui::TextWindow& t) {
  // The line height starts at 20 and drops by one until the text fits the window (never below 2).
  int size = kNameLineHeight;
  t.setTextSize(static_cast<float>(size));
  while (size > 2 && !t.textFits()) t.setTextSize(static_cast<float>(--size));
}

void HiscoreWindow::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  for (int i = 0; i < kRows; ++i) {
    const std::string n = std::to_string(i + 1);
    Row& r = rows_[i];
    r.name = dynamic_cast<ui::TextWindow*>(find("name" + n));
    r.score = dynamic_cast<ui::TextWindow*>(find("score" + n));
    r.info = dynamic_cast<ui::TextWindow*>(find("info" + n));
    r.rank = dynamic_cast<ui::TextWindow*>(find(n));
    r.p1 = find("p1_" + n);
  }
  manager().startWindowAnimation(id(), kTickMs);
  updateText();
  runUpdateButtons();
}

void HiscoreWindow::setShownByName(const char* name, bool shown) {
  if (Window* w = find(name)) w->setShown(shown);
}

void HiscoreWindow::updateText() {
  const bool localOnly = std::strtol(ctx_.config.get("hiscorelocal").c_str(), nullptr, 10) != 0;
  // HideAllText: the named widgets start hidden and the rules below show what applies (hiscore.md 2.4, 2.5).
  for (const char* n : {"eligible", "eligibleasterisk", "server", "error", "info", "category", "local", "global", "topplayers",
                        "congratulations", "dnq", "yourrank", "yourrankglobalinfo", "rankvalue", "scrollup", "scrolldown"})
    setShownByName(n, false);
  // Headers. Local-only: the original shows `topplayers` (absent from the shipped screens) and no `local` header; this
  // engine still shows "local" then, so the screen has a title.
  const bool topPlayers = find("topplayers") != nullptr;
  if (state_ != eGlobalView) {
    setShownByName("local", !localOnly || !topPlayers);
    setShownByName("topplayers", localOnly);
  } else {
    setShownByName("global", !localOnly);
  }
  // Right panels.
  if (localOnly) {
    setShownByName("rightpanel", false);
    setShownByName("rightpanelsmall", false);
  } else {
    setShownByName("rightpanel", state_ == eLocalView);
    setShownByName("rightpanelsmall", state_ != eLocalView);
    if (state_ == eLocalView) {
      setShownByName("info", true);
      if (ctx_.users && ctx_.users->scoreAvailable()) setShownByName("eligible", true);
    }
    if (state_ == eRequestingCategories || state_ == eRequestingScores || state_ == eSubmitting) setShownByName("server", true);
    if (state_ == eError) {
      if (auto* e = dynamic_cast<ui::TextWindow*>(find("error"))) e->setLabel(kOfflineError);
      setShownByName("error", true);
    }
  }

  // The local list shows in every state but the global view.
  const std::vector<HiscoreRow>* list = nullptr;
  static const std::vector<HiscoreRow> none;
  list = &none;
  if (state_ != eGlobalView && ctx_.users) list = &ctx_.users->hiscores().rows(ctx_.users->hiscoreMode());
  shownRows_ = static_cast<int>(std::min<std::size_t>(list->size(), kRows));
  for (int i = 0; i < kRows; ++i) {
    Row& r = rows_[i];
    const bool used = i < shownRows_;
    const HiscoreRow* row = used ? &(*list)[i] : nullptr;
    if (r.name) {
      r.name->setLabel(used ? row->user : std::string());
      fitName(*r.name);
      r.name->setShown(used);
    }
    if (r.score) {
      r.score->setLabel(used ? scoreTextOf(*row) : std::string());
      r.score->setShown(used);
    }
    if (r.rank) {
      r.rank->setLabel(used ? std::to_string(row->rank) + "." : std::string());
      r.rank->setShown(used);
    }
    if (r.info) {
      r.info->setLabel(used ? parseGameData(ctx_, row->a) : std::string());
      r.info->setShown(used);
    }
    if (r.p1) r.p1->setShown(false);  // only online rows carry the P1 icon
  }
}

void HiscoreWindow::runUpdateButtons() {
  if (ui::ScriptSink* sink = manager().scriptSink()) sink->runString("if UpdateButtons then UpdateButtons() end");
}

void HiscoreWindow::beginRequest(State s) {
  state_ = s;
  manager().startWindowAnimation(id(), kTickMs);
  updateText();
  runUpdateButtons();
}

void HiscoreWindow::submissionDone(bool qualified) {
  submitState_ = qualified ? 1 : 2;
  beginRequest(eRequestingCategories);
}

bool HiscoreWindow::onAnimate() {
  if (state_ == eRequestingCategories || state_ == eRequestingScores) {
    // Offline: the request fails.
    state_ = eError;
    updateText();
    runUpdateButtons();
  }
  return false;
}

bool HiscoreWindow::onMessage(const ui::Message& m) {
  if (m.type != ui::kButtonPress) return false;
  const std::string& n = m.name;
  if (n == "viewlocal") {
    state_ = eLocalView;
    updateText();
    runUpdateButtons();
    return true;
  }
  if (n == "view") {
    submitState_ = 0;
    beginRequest(eRequestingCategories);
    return true;
  }
  if (n == "categoryleft" || n == "categoryright") {
    beginRequest(eRequestingScores);
    return true;
  }
  if (n == "scrollup" || n == "scrolldown") {
    updateText();  // the local list never scrolls: the offset stays 0
    return true;
  }
  return false;
}

std::string HiscoreWindow::detail() const {
  return " state=" + std::to_string(state_) + " rows=" + std::to_string(shownRows_);
}

namespace {

int nativeGetState(script::ScriptContext&) { return HiscoreWindow::active() ? HiscoreWindow::active()->state() : 0; }

bool nativeScoreAvailable(script::ScriptContext& ctx) { return ctx.users && ctx.users->scoreAvailable(); }

void nativeUpdateText(script::ScriptContext& ctx) {
  if (HiscoreWindow* w = HiscoreWindow::active()) w->updateText();
  else if (ctx.stubs.hit("UpdateText")) ctx.log.write("[unimplemented] UpdateText (no HiscoreWindow is open)");
}

void nativeSubmissionDone(script::ScriptContext& ctx, bool qualified) {
  if (HiscoreWindow* w = HiscoreWindow::active()) w->submissionDone(qualified);
  else if (ctx.stubs.hit("SubmissionDone")) ctx.log.write("[unimplemented] SubmissionDone (no HiscoreWindow is open)");
}

void nativeLaunchPrivacyPolicy(script::ScriptContext& ctx) {
  if (ctx.stubs.hit("LaunchPrivacyPolicy")) ctx.log.write("[unimplemented] LaunchPrivacyPolicy (offline; no browser is opened)");
}

}  // namespace

void registerHiscoreNatives(lua_State* L, script::ScriptContext& ctx) {
  script::ScriptContext* c = &ctx;
  ctx.wm.addWindowType("HiscoreWindow", [c](ui::WindowManager& wm) { return std::make_unique<HiscoreWindow>(wm, *c); });
  script::bind<nativeGetState>(L, ctx, "GetState");
  script::bind<nativeScoreAvailable>(L, ctx, "ScoreAvailable");
  script::bind<nativeUpdateText>(L, ctx, "UpdateText");
  script::bind<nativeSubmissionDone>(L, ctx, "SubmissionDone");
  script::bind<nativeLaunchPrivacyPolicy>(L, ctx, "LaunchPrivacyPolicy");
}

}  // namespace choco::game
