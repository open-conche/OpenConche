#include "ui/Transition.h"

#include <algorithm>
#include <cmath>

#include "ui/DrawContext.h"
#include "ui/WindowManager.h"
#include "ui/widgets/Widgets.h"

namespace choco::ui {

// ---- Transition ----

void applyTransition(const Transition& tr, Window& w, float t) {
  switch (tr.kind) {
    case Transition::Kind::FadeIn: setSubtreeAlpha(w, t * tr.alpha, tr.rectAlphaScale); break;
    case Transition::Kind::FadeOut: setSubtreeAlpha(w, (1 - t) * tr.alpha, tr.rectAlphaScale); break;
    case Transition::Kind::Swipe:
      // Integer truncation on each axis, as the original's float arithmetic.
      w.setPos(static_cast<int>(static_cast<float>(tr.startX) + static_cast<float>(tr.endX - tr.startX) * t),
               static_cast<int>(static_cast<float>(tr.startY) + static_cast<float>(tr.endY - tr.startY) * t));
      break;
    case Transition::Kind::Path: {
      const gfx::PointF p = tr.curve.at(t);
      w.setPos(static_cast<int>(p.x + 0.5), static_cast<int>(p.y + 0.5));
      break;
    }
    case Transition::Kind::ZoomIn:
      // TImage::SetTexture(image, texture, t): the window and the drawn image are the texture times t. Only the size
      // changes, so the image grows from the window's top-left corner. A window that is not an image is left alone.
      if (auto* image = dynamic_cast<BitmapWindow*>(&w)) image->setScale(t);
      break;
  }
}

void TransitionList::tick(WindowManager& wm, double nowMs) {
  std::vector<script::LuaRef> finished;
  std::vector<Transition> keep;
  keep.reserve(items_.size());
  for (Transition& tr : items_) {
    Window* w = wm.window(tr.target);
    if (!w) {
      // The target was destroyed (DestroyWindow, SwapToModal, a popped modal): the transition ends and its onend still
      // runs once, because FadeCloseWindow's onend is what closes the dialog.
      if (tr.onend) {
        wm.log("[transition] target window " + WindowManager::idString(tr.target) + " is gone; ending it and running onend");
        finished.push_back(std::move(*tr.onend));
      }
      continue;
    }
    w->setShown(true);  // every step re-shows the window, even if the script hid it meanwhile
    const double elapsed = nowMs - tr.startMs;
    const bool done = elapsed >= tr.durationMs;
    const double shown = tr.stepMs > 0 ? std::floor(elapsed / tr.stepMs) * tr.stepMs : elapsed;
    const float t = done ? 1.0f : static_cast<float>(shown / tr.durationMs);
    applyTransition(tr, *w, t);
    if (done) {
      if (tr.onend) finished.push_back(std::move(*tr.onend));
    } else {
      keep.push_back(std::move(tr));
    }
  }
  items_.swap(keep);
  // Now the onends, with the list in its final state so they can start transitions of their own.
  for (const script::LuaRef& fn : finished)
    if (ScriptSink* sink = wm.scriptSink()) sink->callProtected(fn);
}

}  // namespace choco::ui
