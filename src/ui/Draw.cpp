#include <algorithm>

#include "ui/DrawContext.h"
#include "ui/Window.h"

namespace choco::ui {

void setSubtreeAlpha(Window& w, float alpha, float rectScale) {
  // A Rectangle does not take fades by default (it draws at its own alpha); with a scale below 1 the fader sets it, as the original's
  // popup fader does (Rectangle alpha = a * 0.8).
  if (rectScale < 1 && w.typeName() == "Rectangle") w.setAlpha(alpha * rectScale);
  else if (w.takesFaderAlpha()) w.setAlpha(alpha);
  for (const auto& c : w.children()) setSubtreeAlpha(*c, alpha, rectScale);
}

void drawWindow(const Window& w, const DrawContext& parentCtx) {
  if (!w.shown()) return;
  const gfx::RectI& r = w.rect();
  const gfx::RectI screen{static_cast<int>(parentCtx.origin.x) + r.x, static_cast<int>(parentCtx.origin.y) + r.y, r.w, r.h};
  const gfx::RectI visible = gfx::RectI::intersect(parentCtx.clip, screen);
  if (visible.empty()) return;  // like the original, a window with an empty visible area skips its children too

  DrawContext ctx{parentCtx.renderer, {static_cast<float>(screen.x), static_cast<float>(screen.y)},
                  w.alpha(), visible};
  ctx.renderer.pushClip(screen);
  w.draw(ctx);
  if (w.drawsOutside()) {
    ctx.renderer.popClip();
    w.drawOutside(ctx);
    ctx.renderer.pushClip(screen);
  }
  for (auto it = w.children().rbegin(); it != w.children().rend(); ++it) drawWindow(**it, ctx);
  w.postDraw(ctx);
  ctx.renderer.popClip();
}

}  // namespace choco::ui
