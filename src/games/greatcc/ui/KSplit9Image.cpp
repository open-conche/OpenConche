#include "games/greatcc/ui/KSplit9Image.h"

#include "gfx/TextureCache.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

std::array<Split9Piece, 9> split9Pieces(int tw, int th, int w, int h, int l, int r, int t, int b) {
  auto rect = [](int x0, int y0, int x1, int y1) { return gfx::RectI{x0, y0, x1 - x0, y1 - y0}; };
  return {{
      {rect(0, 0, l, t), rect(0, 0, l, t)},
      {rect(l, 0, tw - r, t), rect(l, 0, w - r, t)},
      {rect(tw - r, 0, tw, t), rect(w - r, 0, w, t)},
      {rect(0, t, l, th - b), rect(0, t, l, h - b)},
      {rect(l, t, tw - r, th - b), rect(l, t, w - r, h - b)},
      {rect(tw - r, t, tw, th - b), rect(w - r, t, w, h - b)},
      {rect(0, th - b, l, th), rect(0, h - b, l, h)},
      {rect(l, th - b, tw - r, th), rect(l, h - b, w - r, h)},
      {rect(tw - r, th - b, tw, th), rect(w - r, h - b, w, h)},
  }};
}

void KSplit9Image::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  x1_ = static_cast<int>(style.numberOr("x1", 0));
  x2_ = static_cast<int>(style.numberOr("x2", 0));
  y1_ = static_cast<int>(style.numberOr("y1", 0));
  y2_ = static_cast<int>(style.numberOr("y2", 0));
  pad_ = static_cast<int>(style.numberOr("padding", 0));
  setSize(static_cast<int>(style.numberOr("w", 0)), static_cast<int>(style.numberOr("h", 0)));
  image_ = style.stringOr("image");
  texture_.reset();
  if (!image_.empty())
    if (gfx::TextureCache* cache = manager().textures()) texture_ = cache->load(image_);
  builtW_ = rect().w;
  builtH_ = rect().h;
  if (texture_) pieces_ = split9Pieces(texture_->width(), texture_->height(), builtW_, builtH_, x1_, x2_, y1_, y2_);
}

void KSplit9Image::draw(ui::DrawContext& ctx) const {
  if (!texture_) return;
  for (const Split9Piece& p : pieces_) {
    if (p.src.w <= 0 || p.src.h <= 0 || p.dst.w <= 0 || p.dst.h <= 0) continue;
    ctx.renderer.drawTexture(
        *texture_, {static_cast<float>(p.src.x), static_cast<float>(p.src.y), static_cast<float>(p.src.w), static_cast<float>(p.src.h)},
        {ctx.origin.x + p.dst.x, ctx.origin.y + p.dst.y, static_cast<float>(p.dst.w), static_cast<float>(p.dst.h)},
        {1, 1, 1, 1}, gfx::Blend::Normal);
  }
}

std::string KSplit9Image::detail() const {
  return " image=\"" + image_ + "\"" + (texture_ ? "" : " (not loaded)") + " insets=" + std::to_string(x1_) + "," +
         std::to_string(x2_) + "," + std::to_string(y1_) + "," + std::to_string(y2_);
}

}  // namespace choco::games::greatcc
