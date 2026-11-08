#include "games/choc2/HighScoresWindow.h"

#include <algorithm>
#include <cstdlib>
#include <optional>

#include "games/common/LedgerText.h"
#include "sdk/Users.h"
#include "script/Bind.h"
#include "script/LuaApi.h"
#include "script/ScriptHost.h"
#include "ui/Style.h"
#include "ui/WindowManager.h"
#include "ui/widgets/TextWidgets.h"

namespace choco::game::choc2 {

namespace {

HighScores* gActive = nullptr;

}  // namespace

HighScores::HighScores(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), ctx_(ctx) { gActive = this; }

HighScores::~HighScores() {
  if (gActive == this) gActive = nullptr;
}

HighScores* HighScores::active() { return gActive; }

std::string HighScores::scoreText(script::ScriptContext& ctx, long long score) {
  return ctx.strings.sdkLookup("formatScore", {script::dollars(ctx.lua.L(), static_cast<double>(score)), "", "", "", ""});
}

int HighScores::rowCount() const {
  if (!ctx_.users) return 0;
  return static_cast<int>(ctx_.users->hiscores().rows(std::to_string(mode_)).size());
}

void HighScores::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  for (int i = 0; i < kRows; ++i) {
    const std::string n = std::to_string(i + 1);
    Row& r = rows_[i];
    r.name = dynamic_cast<ui::TextWindow*>(find("name" + n));
    r.score = dynamic_cast<ui::TextWindow*>(find("score" + n));
    r.rank = dynamic_cast<ui::TextWindow*>(find(n));
    r.p1 = find("p1_" + n);
  }
  // The screen opens on the mode the current user is playing.
  if (ctx_.users) mode_ = std::clamp(std::atoi(ctx_.users->hiscoreMode().c_str()), 0, kModes - 1);
  updateText();
}

void HighScores::setShownByName(const char* name, bool shown) {
  if (Window* w = find(name)) w->setShown(shown);
}

void HighScores::viewLocal() {
  offset_ = 0;
  updateText();
}

void HighScores::stepMode(int dir) {
  mode_ = ((mode_ + dir) % kModes + kModes) % kModes;
  offset_ = 0;
  updateText();
}

void HighScores::scroll(int) {
  // The local view never scrolls: top ten, offset 0, both scroll buttons hidden (hiscore.md 2.4; paging is the global view's).
  updateText();
}

void HighScores::updateText() {
  static const std::vector<sdk::HiscoreRow> none;
  const std::vector<sdk::HiscoreRow>& list = ctx_.users ? ctx_.users->hiscores().rows(std::to_string(mode_)) : none;
  const int count = static_cast<int>(list.size());
  offset_ = 0;
  shown_ = std::clamp(count - offset_, 0, kRows);

  if (auto* t = dynamic_cast<ui::TextWindow*>(find("mode")))
    t->setLabel(ctx_.strings.sdkLookup(mode_ == 0 ? "story_mode" : "free_mode"));
  setShownByName("scrollup", false);
  setShownByName("scrolldown", false);

  for (int i = 0; i < kRows; ++i) {
    Row& r = rows_[i];
    const bool used = i < shown_;
    const sdk::HiscoreRow* row = used ? &list[static_cast<std::size_t>(offset_ + i)] : nullptr;
    if (r.name) {
      r.name->setLabel(used ? row->user : std::string());
      r.name->setShown(used);
    }
    if (r.score) {
      r.score->setLabel(used ? scoreText(ctx_, row->score) : std::string());
      r.score->setShown(used);
    }
    if (r.rank) {
      r.rank->setLabel(used ? std::to_string(row->rank) + "." : std::string());
      r.rank->setShown(used);
    }
    if (r.p1) r.p1->setShown(false);  // only online players carry the P1 icon
  }
}

std::string HighScores::detail() const {
  return " mode=" + std::to_string(mode_) + " offset=" + std::to_string(offset_) + " rows=" + std::to_string(shown_) + "/" +
         std::to_string(rowCount());
}

namespace {

// Each native acts on the open HighScores; without one it is logged once (the originals exist only while the window does).
template <class F>
void withWindow(script::ScriptContext& ctx, const char* name, F&& f) {
  if (HighScores* w = HighScores::active()) f(*w);
  else if (ctx.stubs.hit(name)) ctx.log.write(std::string("[unimplemented] ") + name + " (no HighScores window is open)");
}

// The argument the Lua passes to ViewLocal and ViewGlobal (always 0) is not used.
void nativeViewLocal(script::ScriptContext& ctx, std::optional<double>) {
  withWindow(ctx, "ViewLocal", [](HighScores& w) { w.viewLocal(); });
}

// There is no online list: the local one stays, and the call is logged once.
void nativeViewGlobal(script::ScriptContext& ctx, std::optional<double>) {
  if (ctx.stubs.hit("ViewGlobal")) ctx.log.write("[unimplemented] ViewGlobal (offline; the local list stays)");
}

void nativeNextMode(script::ScriptContext& ctx) {
  withWindow(ctx, "NextMode", [](HighScores& w) { w.stepMode(1); });
}

void nativePreviousMode(script::ScriptContext& ctx) {
  withWindow(ctx, "PreviousMode", [](HighScores& w) { w.stepMode(-1); });
}

void nativeNextCategory(script::ScriptContext& ctx) {
  if (ctx.stubs.hit("NextCategory")) ctx.log.write("[unimplemented] NextCategory (offline: the categories come from the server)");
}

void nativePreviousCategory(script::ScriptContext& ctx) {
  if (ctx.stubs.hit("PreviousCategory"))
    ctx.log.write("[unimplemented] PreviousCategory (offline: the categories come from the server)");
}

void nativeScrollUp(script::ScriptContext& ctx) {
  withWindow(ctx, "ScrollUp", [](HighScores& w) { w.scroll(-1); });
}

void nativeScrollDown(script::ScriptContext& ctx) {
  withWindow(ctx, "ScrollDown", [](HighScores& w) { w.scroll(1); });
}

}  // namespace

void registerHighScoresNatives(lua_State* L, script::ScriptContext& ctx) {
  script::ScriptContext* c = &ctx;
  ctx.wm.addWindowType("HighScores", [c](ui::WindowManager& wm) { return std::make_unique<HighScores>(wm, *c); });
  script::bind<nativeViewLocal>(L, ctx, "ViewLocal");
  script::bind<nativeViewGlobal>(L, ctx, "ViewGlobal");
  script::bind<nativeNextMode>(L, ctx, "NextMode");
  script::bind<nativePreviousMode>(L, ctx, "PreviousMode");
  script::bind<nativeNextCategory>(L, ctx, "NextCategory");
  script::bind<nativePreviousCategory>(L, ctx, "PreviousCategory");
  script::bind<nativeScrollUp>(L, ctx, "ScrollUp");
  script::bind<nativeScrollDown>(L, ctx, "ScrollDown");
}

}  // namespace choco::game::choc2
