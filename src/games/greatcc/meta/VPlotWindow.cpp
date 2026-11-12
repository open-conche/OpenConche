#include "games/greatcc/meta/VPlotWindow.h"

#include <algorithm>
#include <cstdio>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/meta/PlotManager.h"
#include "games/greatcc/scene/TextSprite.h"
#include "games/greatcc/ui/Transitions.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {
}  // namespace

VPlotWindow::~VPlotWindow() {
  // The page lives in the plot manager, which outlives this window: it must not keep a link to this window's sprite manager.
  if (current_ && current_->manager() == &sprites()) sprites().removeObject(*current_);
}

void VPlotWindow::postChildrenInit(const ui::Style& style) {
  VSpriteWindow::postChildrenInit(style);
  startPlotSequence();
}

void VPlotWindow::startPlotSequence() {
  GreatCCWorld* world = worldOf(ctx_);
  if (world) {
    GameStateStore& gs = world->gameStates();
    gs.set("DiaryTime_" + std::to_string(gs.playCounter()), "1");
    listener_ = std::make_shared<PlotListener>(ctx_);
    pending_ = world->plots().activeItems();
    if (startNextItem()) return;
  }
  close();
}

bool VPlotWindow::startNextItem() {
  GreatCCWorld* world = worldOf(ctx_);
  if (current_) current_->reset();
  if (!world || pending_.empty()) return false;
  const std::string name = pending_.front();
  pending_.erase(pending_.begin());
  const std::shared_ptr<PlotItem> item = world->plots().item(name);
  if (!item) return false;
  if (current_ && current_->manager() == &sprites()) sprites().removeObject(*current_);
  listener_->setPlotItem(name);
  item->setListener(listener_);
  sprites().addObject(item);
  item->advance();
  current_ = item;
  shown_.push_back(name);
  return true;
}

bool VPlotWindow::onAnimate() {
  if (current_ && current_->isComplete() && !closed_ && !startNextItem()) close();
  return VSpriteWindow::onAnimate();
}

void VPlotWindow::close() {
  if (closed_) return;
  closed_ = true;
  // The original runs this on a coroutine of its own. Here a transition can only be waited for on Main's coroutine (the queued text
  // runs there), so Main waits for the bounce itself.
  char text[160];
  std::snprintf(text, sizeof text, "DoTransition( %i ); PopModal( 'plot_modal' ); InsideGame( true );", static_cast<int>(kBounceBack));
  doLuaString(ctx_, text);
  if (GreatCCWorld* world = worldOf(ctx_)) {
    GameStateStore& gs = world->gameStates();
    gs.set("DiaryTime_" + std::to_string(gs.playCounter()), "0");
  }
}

std::string VPlotWindow::detail() const {
  std::string s = VSpriteWindow::detail();
  if (current_) s += " page=" + current_->name() + (current_->isComplete() ? " done" : "");
  s += " shown=" + std::to_string(shown_.size()) + " left=" + std::to_string(pending_.size()) + (closed_ ? " closed" : "");
  if (current_)
    if (const TextLineCount n = countTextLines(*current_); n.total > 0) s += " lines=" + std::to_string(n.drawn) + "/" + std::to_string(n.total);
  return s;
}

}  // namespace choco::games::greatcc
