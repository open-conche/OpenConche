#pragma once

// VPlotWindow (docs/re/greatcc-meta-flow.md 2.3): the diary. It is the `plot_window` of scripts/plotwindow.lua, a sprite window
// inside the sliding dialog; made, it takes the plot manager's active pages and shows them one after the other, each until it is
// complete, then closes the dialog (a bounce back, then the modal pops and the level goes on).

#include <memory>
#include <string>
#include <vector>

#include "games/greatcc/meta/PlotItem.h"
#include "games/greatcc/scene/VSpriteWindow.h"

namespace choco::games::greatcc {

class VPlotWindow : public VSpriteWindow {
 public:
  VPlotWindow(ui::WindowManager& wm, script::ScriptContext& ctx) : VSpriteWindow(wm, ctx) {}
  ~VPlotWindow() override;
  std::string_view typeName() const override { return "VPlotWindow"; }
  // The sprite window's, then StartPlotSequence on the world's plot manager.
  void postChildrenInit(const ui::Style& style) override;
  // OnTaskAnimate: a complete page is followed by the next, or by the end of the diary; then the sprites.
  bool onAnimate() override;
  std::string detail() const override;

  // StartPlotSequence: the diary-time state is raised, a listener is made, the active pages are copied and the first starts; an
  // empty list closes at once.
  void startPlotSequence();
  // StartNextItem: the page shown so far is reset and leaves; the next name is taken from the list; false when there is none (or its
  // item does not exist).
  bool startNextItem();
  // Close (once): the dialog bounces back and the modal pops on a coroutine of its own; the game continues.
  void close();
  bool closed() const { return closed_; }
  const std::shared_ptr<PlotItem>& current() const { return current_; }

 private:
  std::shared_ptr<PlotItem> current_;
  std::shared_ptr<PlotListener> listener_;
  std::vector<std::string> pending_;
  std::vector<std::string> shown_;
  bool closed_ = false;
};

}  // namespace choco::games::greatcc
