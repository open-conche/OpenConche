#pragma once

// KSplit9Image (docs/re/greatcc-ui-library.md 8): a nine-slice frame. The insets x1, x2, y1, y2 (left, right, top, bottom) cut
// the texture into nine pieces; each is stretched to fill the matching part of the window. The pieces are fixed when the window
// is built from the size it has then: a later resize is not reflected.

#include <array>
#include <memory>

#include "gfx/Texture.h"
#include "gfx/Types.h"
#include "ui/Window.h"

namespace choco::games::greatcc {

struct Split9Piece {
  gfx::RectI src;  // texture pixels
  gfx::RectI dst;  // window pixels
  bool operator==(const Split9Piece&) const = default;
};

// The nine pieces in the order top-left, top, top-right, left, centre, right, bottom-left, bottom, bottom-right for a texture
// of tw x th, a window of w x h and the insets l (x1), r (x2), t (y1), b (y2). No clamping: insets that overlap give
// negative-sized middle pieces, as in the original (nothing shipped does).
std::array<Split9Piece, 9> split9Pieces(int tw, int th, int w, int h, int l, int r, int t, int b);

class KSplit9Image : public ui::Window {
 public:
  explicit KSplit9Image(ui::WindowManager& wm) : Window(wm) {}
  std::string_view typeName() const override { return "KSplit9Image"; }
  // Keys: image, x1, x2, y1, y2 (integers, 0), padding (0), and the raw numbers w and h, which set the size as they stand
  // (kMax and kCenter are not understood here).
  void postChildrenInit(const ui::Style& style) override;
  void draw(ui::DrawContext& ctx) const override;
  std::string detail() const override;

  const std::array<Split9Piece, 9>& pieces() const { return pieces_; }
  bool imageLoaded() const override { return texture_ != nullptr; }
  // GetInnerRect: (x1 - pad, y1 - pad, (W - x2) + pad, (H - y2) + pad) as left, top, right, bottom of the size at build time.
  gfx::RectI innerRect() const { return {x1_ - pad_, y1_ - pad_, builtW_ - x2_ + pad_, builtH_ - y2_ + pad_}; }

 private:
  std::string image_;
  std::shared_ptr<gfx::Texture> texture_;
  int x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0, pad_ = 0, builtW_ = 0, builtH_ = 0;
  std::array<Split9Piece, 9> pieces_{};
};

}  // namespace choco::games::greatcc
