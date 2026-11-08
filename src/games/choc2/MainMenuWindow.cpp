#include "games/choc2/MainMenuWindow.h"

#include <algorithm>
#include <cmath>

#include "gfx/Renderer.h"
#include "gfx/TextureCache.h"
#include "ui/DrawContext.h"
#include "ui/Message.h"
#include "ui/WindowManager.h"
#include "ui/widgets/Widgets.h"

namespace choco::game::choc2 {

// FACT (TMainMenu statics): pivot, angle limits, up angle, half-period.
const MainMenuWindow::Beam MainMenuWindow::kBeamTable[kBeams] = {
    {790, 560, 290, 355, 355, 5400},
    {800, 580, 285, 355, 350, 6100},
    {0, 445, 5, 85, 10, 5600},
    {0, 475, 5, 90, 5, 6900},
};

float MainMenuWindow::beamAngle(const Beam& b, double clockMs, double introT) {
  double tri = std::fmod(clockMs, 2 * b.halfPeriodMs) / b.halfPeriodMs;
  if (tri > 1) tri = 2 - tri;
  double a = b.minDeg + (b.maxDeg - b.minDeg) * tri;
  if (introT >= 1) a = b.upDeg;
  else if (introT > 0) a += (b.upDeg - a) * introT;
  return static_cast<float>(a);
}

float MainMenuWindow::blimpX(double clockMs, int w) {
  return static_cast<float>(std::fmod(clockMs, kBlimpPeriodMs) / kBlimpPeriodMs * (w + 500) - 250);
}

void MainMenuWindow::init(const ui::Style& style) {
  Window::init(style);
  if (gfx::TextureCache* cache = manager().textures()) {
    beam_ = cache->load("image/splash_searchlight");
    blimp_ = cache->load("image/splash_blimp");
    skyL_ = cache->load("image/splash_skyline_left");
    skyR_ = cache->load("image/splash_skyline_right");
  }
}

void MainMenuWindow::postDraw(ui::DrawContext& ctx) const {
  const int W = rect().w, H = rect().h;
  const gfx::ColorF white{1, 1, 1, 1};
  // The beams' phase is the free clock; the intro swing is the first state's own clock.
  const double clock = manager().modalNow(*this);
  const double introT = state_ == State::Idle ? 0.0 : state_ == State::FadeOut ? clockMs_ / kFadeOutMs : 1.0;
  if (beam_) {
    const float bw = static_cast<float>(beam_->width()), bh = static_cast<float>(beam_->height());
    for (const Beam& b : kBeamTable) {
      const float deg = beamAngle(b, clock, introT);
      // INFERENCE (beam placement): the unrotated beam stands on the pivot (its bottom-centre) and turns about it, so its
      // centre is half its length from the pivot along the beam: the binary's `pivot - R * (0, texH / 2)`.
      const gfx::RectF dst{ctx.origin.x + b.pivotX - bw / 2, ctx.origin.y + b.pivotY - bh, bw, bh};
      ctx.renderer.drawTexture(*beam_, {}, dst, white, gfx::Blend::Normal, kBeamClockwise ? deg : -deg, {bw / 2, bh});
    }
  }
  if (blimp_) {
    const float bw = static_cast<float>(blimp_->width()), bh = static_cast<float>(blimp_->height());
    const float cx = blimpX(clock, W), cy = H - 1.5f * bh;
    ctx.renderer.drawTexture(*blimp_, {}, {ctx.origin.x + cx - bw / 2, ctx.origin.y + cy - bh / 2, bw, bh}, white,
                             gfx::Blend::Normal);
  }
  if (skyL_) {
    const float w = static_cast<float>(skyL_->width()), h = static_cast<float>(skyL_->height());
    ctx.renderer.drawTexture(*skyL_, {}, {ctx.origin.x, ctx.origin.y + H - h, w, h}, white, gfx::Blend::Normal);
  }
  if (skyR_) {
    const float w = static_cast<float>(skyR_->width()), h = static_cast<float>(skyR_->height());
    ctx.renderer.drawTexture(*skyR_, {}, {ctx.origin.x + W - w, ctx.origin.y + H - h, w, h}, white, gfx::Blend::Normal);
  }
}

void MainMenuWindow::fadeParentChildren(float v) {
  Window* p = parent();
  if (!p) return fadeOwnChildren(v);
  for (const auto& c : p->children()) ui::setSubtreeAlpha(*c, v);
}

void MainMenuWindow::fadeOwnChildren(float v) {
  for (const auto& c : children()) ui::setSubtreeAlpha(*c, v);
}

bool MainMenuWindow::doIntro() {
  if (!dynamic_cast<ui::ButtonWindow*>(find("intro_button"))) return false;
  if (state_ != State::Idle) return true;
  state_ = State::FadeOut;
  clockMs_ = 0;
  // INFERENCE: the engine has no cursor control, so the original's hide is only logged (see the class comment).
  manager().log("[unimplemented] cursor hide (DoIntro hides the cursor in the original; the cursor is not changed)");
  manager().startWindowAnimation(id(), kTickMs, true);
  return true;
}

bool MainMenuWindow::onMessage(const ui::Message& m) {
  // The button was pressed (click or Enter): its command has run, so the intro is over whatever the command did.
  if (state_ != State::Idle && m.type == ui::kButtonPress && m.name == "intro_button") {
    state_ = State::Idle;
    manager().stopWindowAnimation(id());
    manager().log("[unimplemented] cursor restore (INFERRED: the intro ended early)");
  }
  return false;
}

void MainMenuWindow::endIntro() {
  state_ = State::Idle;
  manager().log("[unimplemented] cursor restore (INFERRED: the intro ended)");
  // The end of the intro is the button's own command, once. The command normally swaps the modal and so destroys this
  // window: nothing is touched afterwards.
  if (auto* button = dynamic_cast<ui::ButtonWindow*>(find("intro_button")); button && button->shown()) button->doCommand();
}

bool MainMenuWindow::onAnimate() {
  // The animation ticks on the modal's clock, which stops while a dialog is over the menu: count the ticks instead of
  // reading the platform clock, so the intro pauses and resumes with it.
  switch (state_) {
    case State::Idle:
      return false;
    case State::FadeOut: {
      clockMs_ += kTickMs;
      const float v = static_cast<float>(1.0 - clockMs_ / kFadeOutMs);
      if (v > 0) {
        fadeParentChildren(v);
        return true;
      }
      fadeParentChildren(0);
      state_ = State::FadeIn;
      clockMs_ = 0;
      if (auto* button = dynamic_cast<ui::ButtonWindow*>(find("intro_button"))) {
        button->setShown(true);
        button->resetState();
      }
      if (Window* text = find("intro_text")) text->setAlpha(0);
      return true;
    }
    case State::FadeIn: {
      clockMs_ += kTickMs;
      const float v = static_cast<float>(std::min(1.0, clockMs_ / kFadeInMs));
      fadeOwnChildren(v);
      if (v >= 1) {
        state_ = State::Hold;
        clockMs_ = 0;
      }
      return true;
    }
    case State::Hold:
      if (clockMs_ + kTickMs >= kHoldMs) {
        state_ = State::FadeEnd;
        clockMs_ = 0;
      } else {
        clockMs_ += kTickMs;
      }
      return true;
    case State::FadeEnd: {
      clockMs_ += kTickMs;
      const float v = static_cast<float>(1.0 - clockMs_ / kFadeEndMs);
      if (v > 0) {
        fadeOwnChildren(v);
        return true;
      }
      fadeOwnChildren(0);
      endIntro();
      return false;
    }
  }
  return false;
}

}  // namespace choco::game::choc2
