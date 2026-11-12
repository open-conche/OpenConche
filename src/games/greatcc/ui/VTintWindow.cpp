#include "games/greatcc/ui/VTintWindow.h"

#include <algorithm>
#include <cstdio>

#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {
bool hasModalAncestor(const ui::Window& w) {
  for (const ui::Window* p = &w; p; p = p->parent())
    if (p->isModal()) return true;
  return false;
}
}  // namespace

void VTintWindow::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  color_ = {0, 0, 0, 1};
  if (auto c = style.numbers("color", 4, 1.0))
    color_ = {static_cast<float>((*c)[0]), static_cast<float>((*c)[1]), static_cast<float>((*c)[2]), static_cast<float>((*c)[3])};
  fade_ = style.booleanOr("fade", false);
  time_ = style.numberOr("time", 500);
}

float VTintWindow::alphaAt(double now) const {
  if (!fade_) return color_.a;
  const double span = time_;
  const float t = span > 0 ? static_cast<float>(std::min(1.0, (now - start_) / span)) : 1.0f;
  return color_.a * (fadingIn_ ? t : 1.0f - t);
}

void VTintWindow::draw(ui::DrawContext& ctx) const {
  gfx::ColorF c = color_;
  if (fade_) {
    if (!hasModalAncestor(*this)) return;  // the ramp needs a modal clock
    const double now = manager().modalNow(*this);
    if (!startTaken_) {
      start_ = now;
      startTaken_ = true;
    }
    c.a = alphaAt(now);
  }
  ctx.renderer.fillRect({ctx.origin.x, ctx.origin.y, static_cast<float>(rect().w), static_cast<float>(rect().h)}, c,
                        gfx::Blend::Normal);
}

void VTintWindow::tintOff() {
  if (!fade_) return;
  fadingIn_ = !fadingIn_;
  startTaken_ = false;
}

std::string VTintWindow::detail() const {
  char buf[128];
  std::snprintf(buf, sizeof buf, " color=%.2f,%.2f,%.2f,%.2f fade=%d time=%g %s", color_.r, color_.g, color_.b, color_.a,
                fade_ ? 1 : 0, time_, fadingIn_ ? "in" : "out");
  return buf;
}

}  // namespace choco::games::greatcc
