#pragma once

#include "gfx/Renderer.h"
#include "gfx/Types.h"

namespace choco::ui {

class Window;

// What a window draws with: the renderer, the screen position of the window's top-left corner, the window's own alpha
// (nothing is inherited), and the clip rectangle in force (also pushed on the renderer; kept here so the walk can
// stop at windows that are entirely outside it).
struct DrawContext {
  gfx::Renderer& renderer;
  gfx::PointF origin;  // logical screen position of the window being drawn
  float alpha = 1;     // the alpha of the window being drawn
  gfx::RectI clip{0, 0, 1 << 20, 1 << 20};
};

// Draws `w` and its subtree (window-system design section 6): nothing when it is hidden or its rectangle does not
// meet the clip; otherwise the clip is narrowed to the window's rectangle, `w.draw` runs, then the children from the
// last to the first (so the first child ends up on top), then `w.postDraw`. `parentCtx.origin` is the parent's
// screen origin. Each window draws with its own alpha.
void drawWindow(const Window& w, const DrawContext& parentCtx);

// What TWindowFader::Process does: sets `alpha` on `w` and every descendant of a type that takes it (Text, Bitmap,
// Character, BSGWindow; not Rectangle), and BitmapTint draws ignoring it.
// With rectScale < 1 a Window of type Rectangle gets alpha * rectScale too, whatever its own alpha was (Chocolatier 2's
// TPopupWindow::TFader: a * 0.8).
void setSubtreeAlpha(Window& w, float alpha, float rectScale = 1);

}  // namespace choco::ui
