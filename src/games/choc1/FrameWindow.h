#pragma once

// Chocolatier 1's Frame window (TFrame, exe vtable 004e800c;
// docs/re/choc1-natives.md section 3). Six hard-coded images (image/frameul ur
// ll lr h v), keys `x y w h color name fit` (`hollow` is read by the exe and
// never used by the Lua, so it is ignored here). The border is drawn INSIDE the
// window's rect, not around it as Chocolatier 2's is, so it is a different
// class.

#include <memory>
#include <string>
#include <vector>

#include "gfx/Texture.h"
#include "gfx/Types.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::game::choc1 {

struct FramePiece {
  enum class Image { UL, UR, LL, LR, H, V } image;
  gfx::RectI rect; // window coordinates, top-left origin
  bool operator==(const FramePiece &) const = default;
};

// The images' sizes; 0 x 0 means the image is missing (its pieces are left
// out).
struct FrameSizes {
  gfx::PointI ul, ur, ll, lr, h, v;
};

// FACT: (5, 5, W-10, H-10) in the four slots of the exe's fill call. INFERENCE
// (docs/re/choc1-fit.md question 6): the last two slots are right/bottom, as
// the decompilation reads, so the fill is (5, 5) to (W-10, H-10) and 5 px short
// of the border art on the right and bottom; the border covers the difference
// (frameh is 12 high, framev 12 wide) wherever the art exists.
gfx::RectI frameFillRect(int w, int h);

// The border pieces of a `w` x `h` window in the exe's draw order (FACT): UL,
// the top edge (from UL.w while x < w, tiles of H unscaled, the last may
// overrun), the left edge (from UL.h), the right edge (from UR.h,
// right-aligned), UR, LL (bottom-left), the bottom edge (from LL.w,
// bottom-aligned), LR (bottom-right). The window's clip cuts what overruns.
std::vector<FramePiece> layoutFrame(int w, int h, const FrameSizes &s);

class FrameWindow : public ui::Window {
public:
  FrameWindow(ui::WindowManager &wm, script::ScriptContext &ctx)
      : Window(wm), ctx_(ctx) {}
  std::string_view typeName() const override { return "Frame"; }
  void init(const ui::Style &style) override;
  void draw(ui::DrawContext &ctx) const override;
  std::string detail() const override;

  bool hollow() const { return hollow_; }
  // How many of the six images loaded.
  int imagesLoaded() const;

private:
  script::ScriptContext &ctx_;
  gfx::ColorF color_{1, 1, 1, 1};
  bool hollow_ = false;
  std::shared_ptr<gfx::Texture> tex_[6];
};

} // namespace choco::game::choc1
