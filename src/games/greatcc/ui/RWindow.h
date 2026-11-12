#pragma once

// RWindow (docs/re/greatcc-ui-library.md 1): the game's "bitmap with extras", a Bitmap that also keeps a background colour
// quad drawn over the image, an `imagealpha`, a `disable` flag, an `effect` message, and a 16 ms animation that
// accumulates the modal clock's time for its subclasses (RBounceWindow).

#include "gfx/Types.h"
#include "ui/widgets/Widgets.h"

namespace choco::games::greatcc {

class RWindow : public ui::BitmapWindow {
 public:
  explicit RWindow(ui::WindowManager& wm) : BitmapWindow(wm) {}
  std::string_view typeName() const override { return "RWindow"; }
  // Keys, after the Bitmap's own: imagealpha (>= 0 sets the opacity), disable (hides), bgcolor, effect + luastring (posts a
  // message of type `effect` named `luastring` unless effect is 1000).
  void postChildrenInit(const ui::Style& style) override;
  void draw(ui::DrawContext& ctx) const override;
  // RWindow::OnTaskAnimate: `last`, `delta` and `accumulated` follow the owning modal's clock; always keeps running.
  bool onAnimate() override;
  std::string detail() const override;

  // SetBgColor: stores the colour and builds the quad at the window's size now (a later resize does not change it).
  void setBgColor(const gfx::ColorF& c);
  bool hasBg() const { return hasBg_; }
  const gfx::ColorF& bgColor() const { return bg_; }

  double lastMs() const { return lastMs_; }
  double deltaMs() const { return deltaMs_; }
  double accumulatedMs() const { return accumulatedMs_; }

 private:
  bool started_ = false;
  double lastMs_ = 0, deltaMs_ = 0, accumulatedMs_ = 0;
  gfx::ColorF bg_{-1, -1, -1, -1};
  bool hasBg_ = false;
  int bgW_ = 0, bgH_ = 0;
};

}  // namespace choco::games::greatcc
