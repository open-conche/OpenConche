#pragma once

// VTutorialWindow (docs/re/greatcc-meta-flow.md 1.6): a sprite window over the whole top modal that shows the running tutorial and
// filters the clicks that go on to the windows behind it. It holds the tutorial in its own sprite manager (the dim and the lock act on
// the level's); it fades the tutorial in when it starts and out when it is done, and closes after that.

#include <memory>

#include "games/greatcc/scene/VSpriteWindow.h"
#include "games/greatcc/tutorial/Tutorial.h"

namespace choco::games::greatcc {

class TutorialWindow : public VSpriteWindow {
 public:
  TutorialWindow(ui::WindowManager& wm, script::ScriptContext& ctx);
  ~TutorialWindow() override;
  std::string_view typeName() const override { return "VTutorialWindow"; }

  // SetTutorial: the tutorial becomes one of this window's sprites, with a 500 ms fade in.
  void setTutorial(const std::shared_ptr<Tutorial>& t);
  const std::shared_ptr<Tutorial>& tutorial() const { return tutorial_; }
  // UpdateObjects: the sprites, then the tutorial's end: a finished one that never ran closes the window at once; a completed one
  // fades out (500 ms), is popped from the manager and closes the window when the fade is done.
  bool onAnimate() override;
  // The mouse: the window's own sprites first (the panel, the buttons), then the Tutorials rules decide whether the click goes on.
  bool onMouseMove(int x, int y) override;
  bool onMouseUp(int x, int y) override;
  // A click on a step that advances on any click advances it and is taken.
  bool onMouseDown(int x, int y) override;
  // Button messages go to the manager (the skip button; the click triggers).
  bool onMessage(const ui::Message& m) override;
  bool closed() const { return closed_; }
  std::string detail() const override;

 private:
  // VTutorialWindow::HandleMouse: true when the click must not go on (a tutorial is in effect and its rules refuse the point).
  bool refuses(int x, int y);
  // Close: leaves the parent and is destroyed once.
  void close();

  std::shared_ptr<Tutorial> tutorial_;
  std::shared_ptr<SpriteEffect> fadeOut_;
  bool closed_ = false;
};

}  // namespace choco::games::greatcc
