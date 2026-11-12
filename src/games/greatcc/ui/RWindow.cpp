#include "games/greatcc/ui/RWindow.h"

#include <cstdio>

#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {
constexpr int kNoEffect = 1000;
constexpr double kTickMs = 16;

bool hasModalAncestor(const ui::Window& w) {
  for (const ui::Window* p = &w; p; p = p->parent())
    if (p->isModal()) return true;
  return false;
}
}  // namespace

void RWindow::postChildrenInit(const ui::Style& style) {
  BitmapWindow::postChildrenInit(style);
  const double imageAlpha = style.numberOr("imagealpha", -1);
  if (imageAlpha >= 0) setAlpha(static_cast<float>(imageAlpha));
  if (style.booleanOr("disable", false)) setShown(false);
  if (auto c = style.numbers("bgcolor", 4, 1.0)) {
    const gfx::ColorF v{static_cast<float>((*c)[0]), static_cast<float>((*c)[1]), static_cast<float>((*c)[2]),
                        static_cast<float>((*c)[3])};
    if (!(v == bg_)) setBgColor(v);
  }
  const int effect = static_cast<int>(style.numberOr("effect", kNoEffect));
  if (effect != kNoEffect) manager().post({effect, style.stringOr("luastring"), 0});
  // The original re-parents a window that has no modal ancestor yet (X22 of the design: not built; ours are always made inside
  // a modal). With a modal ancestor it only starts the animation.
  if (hasModalAncestor(*this)) manager().startWindowAnimation(id(), kTickMs);
}

void RWindow::setBgColor(const gfx::ColorF& c) {
  bg_ = c;
  hasBg_ = true;
  bgW_ = rect().w;
  bgH_ = rect().h;
}

void RWindow::draw(ui::DrawContext& ctx) const {
  BitmapWindow::draw(ctx);
  if (hasBg_)
    ctx.renderer.fillRect({ctx.origin.x, ctx.origin.y, static_cast<float>(bgW_), static_cast<float>(bgH_)}, bg_,
                          gfx::Blend::Normal);
}

bool RWindow::onAnimate() {
  const double now = manager().modalNow(*this);
  if (!started_) {
    lastMs_ = now;
    started_ = true;
  }
  deltaMs_ = now - lastMs_;
  lastMs_ = now;
  accumulatedMs_ += deltaMs_;
  return true;
}

std::string RWindow::detail() const {
  std::string s = BitmapWindow::detail();
  if (hasBg_) {
    char buf[96];
    std::snprintf(buf, sizeof buf, " bgcolor=%.2f,%.2f,%.2f,%.2f", bg_.r, bg_.g, bg_.b, bg_.a);
    s += buf;
  }
  return s;
}

}  // namespace choco::games::greatcc
