#pragma once

// VTintWindow (docs/re/greatcc-ui-library.md 7): the dim layer behind a dialog. Not fading it is a flat colour fill; fading
// it ramps the alpha on the parent modal's clock, and TintOff() turns the ramp around (only when `fade` is set).

#include "gfx/Types.h"
#include "ui/Window.h"

namespace choco::games::greatcc {

class VTintWindow : public ui::Window {
 public:
  explicit VTintWindow(ui::WindowManager& wm) : Window(wm) {}
  std::string_view typeName() const override { return "VTintWindow"; }
  // Keys: color (default 0,0,0,1), fade (false), time (ms, 500).
  void postChildrenInit(const ui::Style& style) override;
  void draw(ui::DrawContext& ctx) const override;
  std::string detail() const override;

  // TintOff: with `fade`, flips the ramp's direction and restarts it on the next draw.
  void tintOff();
  // The ramp time in ms, 0 when negative.
  double timeMs() const { return time_ > 0 ? time_ : 0; }
  bool fade() const { return fade_; }
  bool fadingIn() const { return fadingIn_; }
  // The alpha the next draw would use at modal-clock time `now` (a test hook; does not start the ramp).
  float alphaAt(double now) const;

 private:
  gfx::ColorF color_{0, 0, 0, 1};
  bool fade_ = false;
  bool fadingIn_ = true;
  double time_ = 500;
  mutable double start_ = 0;       // 0 = not started
  mutable bool startTaken_ = false;
};

}  // namespace choco::games::greatcc
