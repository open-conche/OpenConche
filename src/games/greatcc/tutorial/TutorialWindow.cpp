#include "games/greatcc/tutorial/TutorialWindow.h"

#include "games/greatcc/GreatCCWorld.h"
#include <cstdio>

#include "games/greatcc/tutorial/TutorialManager.h"
#include "games/greatcc/tutorial/TutorialSpider.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {
constexpr double kFadeMs = 500;
}

TutorialWindow::TutorialWindow(ui::WindowManager& wm, script::ScriptContext& ctx) : VSpriteWindow(wm, ctx) {
  setName("tutorial_window");
  setRect({0, 0, 800, 600});
}

TutorialWindow::~TutorialWindow() {
  // The tutorial outlives its window (the manager keeps it); it must not keep a link to this window's sprite manager.
  if (tutorial_ && tutorial_->manager() == &sprites()) sprites().removeObject(*tutorial_);
}

void TutorialWindow::setTutorial(const std::shared_ptr<Tutorial>& t) {
  tutorial_ = t;
  if (!t) return;
  // A window that is still fading out may hold it: it leaves that one first
  if (SpriteManager* old = t->manager(); old && old != &sprites()) old->removeObject(*t);
  sprites().addObject(t);
  t->cancelEffects();
  t->addEffect(std::make_shared<AlphaEffect>(true, 1.0f, kFadeMs, -1.0f, 0));
}

void TutorialWindow::close() {
  if (closed_) return;
  closed_ = true;
  manager().destroyWindow(*this);
}

bool TutorialWindow::onAnimate() {
  const std::shared_ptr<Tutorial> keep = tutorial_;
  VSpriteWindow::onAnimate();
  if (closed_) return false;
  if (tutorial_) {
    GreatCCWorld* world = worldOf(ctx_);
    if (!tutorial_->finished()) {
      if (tutorial_->isComplete()) {
        if (!fadeOut_) {
          fadeOut_ = std::make_shared<AlphaEffect>(false, 1.0f, kFadeMs, -1.0f, 0);
          tutorial_->cancelEffects();
          tutorial_->addEffect(fadeOut_);
        }
        if (world) world->tutorials().popTutorial(tutorial_);
        if (fadeOut_ && !fadeOut_->inProgress()) close();
      }
    } else {
      close();
      if (world) world->tutorials().popTutorial(tutorial_);
    }
  }
  return !closed_;
}

bool TutorialWindow::refuses(int x, int y) {
  GreatCCWorld* world = worldOf(ctx_);
  if (!world || !world->tutorials().tutorialInEffect() || !parent()) return false;
  const gfx::PointF origin = ui::WindowManager::screenOrigin(*this);
  const gfx::PointI screen{x + static_cast<int>(origin.x), y + static_cast<int>(origin.y)};
  return !world->tutorials().handleMouse(parent(), screen);
}

bool TutorialWindow::onMouseMove(int x, int y) { return VSpriteWindow::onMouseMove(x, y) || refuses(x, y); }

bool TutorialWindow::onMouseUp(int x, int y) { return VSpriteWindow::onMouseUp(x, y) || refuses(x, y); }

bool TutorialWindow::onMouseDown(int x, int y) {
  if (VSpriteWindow::onMouseDown(x, y)) return true;
  if (tutorial_)
    if (const auto step = tutorial_->currentStep(); step && step->clickAny()) {
      tutorial_->advanceStep();
      return true;
    }
  return refuses(x, y);
}

bool TutorialWindow::onMessage(const ui::Message& m) {
  GreatCCWorld* world = worldOf(ctx_);
  return world && world->tutorials().handleMessage(m);
}

std::string TutorialWindow::detail() const {
  std::string s = VSpriteWindow::detail();
  if (tutorial_) {
    s += " tutorial=" + tutorial_->tutorialName() + " step=" + std::to_string(tutorial_->stepNumber()) + "/" +
         std::to_string(tutorial_->numSteps());
    if (tutorial_->isComplete()) s += " done";
    if (const auto step = tutorial_->currentStep(); step && step->text())
      s += " lines=" + std::to_string(step->text()->drawnLineCount()) + "/" + std::to_string(step->text()->lineCount());
    if (const TutorialSpider* sp = tutorial_->spider(); sp && sp->dimEffect()) {
      char buf[48];
      std::snprintf(buf, sizeof buf, " dim=%.2f", static_cast<double>(sp->dimEffect()->intensity()));
      s += buf;
    }
  }
  return s;
}

}  // namespace choco::games::greatcc
