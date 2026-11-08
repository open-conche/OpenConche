#pragma once

#include <memory>
#include <string>
#include <vector>

#include "gfx/Texture.h"
#include "gfx/Types.h"
#include "ui/Window.h"

namespace choco::game::choc2 {

// One image of a frame border, placed in WINDOW coordinates (negative = outside the rect on the left/top).
struct FramePiece {
  enum class Image { Corner, Top, Left, Ornament } image;
  gfx::RectI rect;
  bool hflip = false, vflip = false;
  // Edge tiles are cut to this window-coordinate rectangle (the far tile overruns); w == 0 means no clip.
  gfx::RectI clip{};
  bool operator==(const FramePiece&) const = default;
};

// The fill rectangle: exactly the window rect (docs/re/choc2-map-binary.md 2, B2; FACT).
gfx::RectI frameFillRect(int w, int h);

// Where the border images of a `w` x `h` window go, in window coordinates (docs/re/choc2-map-binary.md 2, from TFrame::Draw).
// FACT: the border lies outside the window (the frame's outer size is (w+2L, h+O+T), L = left image width, T = top image
// height, O = the ornament's height or T); edges are tiled unscaled from window 0 while the tile's start is <= w (or h) and are
// cut at window x = w (or y = h); the bottom/right edges are the top/left tiles flipped; the one corner image is mirrored to the
// four corners; the ornament is last. INFERENCE (the grown viewport's origin): the ornament's bottom touches the window's top
// edge (rect y = -ornament height) and it is centred in the outer width.
// A size of 0 x 0 means "no such image". Pieces come back in draw order: edges, corners, ornament.
std::vector<FramePiece> layoutFrame(int w, int h, gfx::PointI corner, gfx::PointI left, gfx::PointI top, gfx::PointI ornament);

// Frame (Chocolatier 2 and 1 `Frame`): `color` fills the inset rectangle unless `empty` is set, then the border images from the
// style (`corner`, `left`, `top`, `ornament`, usually inherited from the default style) are drawn per layoutFrame.
class FrameWindow : public ui::Window {
 public:
  explicit FrameWindow(ui::WindowManager& wm) : Window(wm) {}
  std::string_view typeName() const override { return "Frame"; }
  void init(const ui::Style& style) override;
  void draw(ui::DrawContext& ctx) const override;
  // INFERRED: the border is drawn OUTSIDE the rect, around it (the rect grown by the border thickness on every side),
  // so children at x=10 (chartalk's text) or ending at the frame's bottom edge (a `fit` frame, chooseplayer's buttons) sit inside
  // the border. Everything is therefore drawn outside the rect's clip.
  bool drawsOutside() const override { return corner_ || top_ || left_ || ornament_; }
  void drawOutside(ui::DrawContext& ctx) const override;
  std::string detail() const override;

  bool empty() const { return empty_; }

 private:
  void paint(ui::DrawContext& ctx) const;
  void drawPiece(ui::DrawContext& ctx, const FramePiece& p) const;
  std::vector<FramePiece> pieces() const;
  const gfx::Texture* texture(FramePiece::Image i) const;
  gfx::ColorF color_{1, 1, 1, 1};
  bool empty_ = false;
  float alpha_ = 1;  // the `alpha` key: multiplies the border textures (not the fill)
  std::string names_[4];
  std::shared_ptr<gfx::Texture> corner_, left_, top_, ornament_;
};

}  // namespace choco::game::choc2
