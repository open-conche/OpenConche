#include "games/common/DynamicWindow.h"

#include <functional>
#include <set>
#include <vector>

#include "script/RunFile.h"
#include "ui/Layout.h"
#include "ui/WindowManager.h"

namespace choco::game {

void DynamicWindow::init(const ui::Style& style) {
  Window::init(style);
  contents_ = style.stringOr("contents");
  init_ = style.booleanOr("init", true);
  script_ = style.stringOr("script");
  onText_ = style.booleanOr("ontext", false);
  if (onText_) {
    manager().addMessageListener(id(), ui::kTextEditChanged, [this](const ui::Message&) { onTextChanged(); });
  } else if (!script_.empty() && ctx_.warned.insert("dynamicwindow-script-without-ontext").second) {
    ctx_.log.write("[" + tag_ + "] DynamicWindow: `script` without `ontext` never runs");
  }
}

void DynamicWindow::onTextChanged() {
  if (isDestroyed() || !onText_) return;
  if (scriptRunning_ || building_) {  // not reachable from a script's own edits (they post, and the post is handled later); a guard
    ++textIgnored_;
    return;
  }
  ++textRuns_;
  if (script_.empty()) {
    rebuild();
    return;
  }
  ui::ScriptSink* sink = manager().scriptSink();
  if (!sink) return;
  scriptRunning_ = true;
  sink->runString(script_);
  scriptRunning_ = false;
}

void DynamicWindow::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  if (init_) rebuild();
}

void DynamicWindow::clear() {
  if (building_) return;
  while (!children().empty()) manager().destroyWindow(*children().front());
}

bool DynamicWindow::rebuild() {
  if (isDestroyed() || contents_.empty()) return false;
  if (building_) {
    ctx_.log.write("[" + tag_ + "] DynamicWindow '" + name() + "': rebuild ignored, its script is still running");
    return false;
  }
  building_ = true;
  ui::WindowManager& wm = manager();
  while (!children().empty()) wm.destroyWindow(*children().front());
  script::runFileInto(ctx_, ctx_.lua.L(), *this, contents_);
  ui::fitToChildren(*this);  // grows only: a window made without w and h (the ledger badge) must not clip its children to 0 x 0
  building_ = false;
  ++builds_;
  return true;
}

std::string DynamicWindow::detail() const {
  return " contents=\"" + contents_ + "\" builds=" + std::to_string(builds_) +
         (onText_ ? " ontext runs=" + std::to_string(textRuns_) : std::string());
}

int DynamicWindow::updateNamed(ui::WindowManager& wm, const std::string& name) {
  std::vector<ui::WindowId> hits;
  std::set<ui::WindowId> seen;
  std::function<void(ui::Window&)> walk = [&](ui::Window& w) {
    if (!seen.insert(w.id()).second) return;
    if (w.name() == name && dynamic_cast<DynamicWindow*>(&w)) hits.push_back(w.id());
    for (const auto& c : w.children()) walk(*c);
  };
  for (const auto& m : wm.modals()) walk(*m);
  int n = 0;
  for (ui::WindowId id : hits)
    if (auto* d = dynamic_cast<DynamicWindow*>(wm.window(id)); d && d->rebuild()) ++n;
  return n;
}

}  // namespace choco::game
