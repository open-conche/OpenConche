#include "games/choc2/FrameWindow.h"

#include <algorithm>

#include "gfx/TextureCache.h"
#include "ui/DrawContext.h"
#include "ui/Style.h"
#include "ui/WindowManager.h"

namespace choco::game::choc2 {

gfx::RectI frameFillRect(int w, int h) { return {0, 0, std::max(0, w), std::max(0, h)}; }

std::vector<FramePiece> layoutFrame(int w, int h, gfx::PointI corner, gfx::PointI left, gfx::PointI top, gfx::PointI ornament) {
  using Image = FramePiece::Image;
  std::vector<FramePiece> out;
  const int L = left.x > 0 ? left.x : corner.x;
  if (top.x > 0 && top.y > 0) {
    const gfx::RectI topClip{0, -top.y, w, top.y}, bottomClip{0, h, w, top.y};
    for (int x = 0; x <= w; x += top.x) out.push_back({Image::Top, {x, -top.y, top.x, top.y}, false, false, topClip});
    for (int x = 0; x <= w; x += top.x) out.push_back({Image::Top, {x, h, top.x, top.y}, false, true, bottomClip});
  }
  if (left.x > 0 && left.y > 0) {
    const gfx::RectI leftClip{-left.x, 0, left.x, h}, rightClip{w, 0, left.x, h};
    for (int y = 0; y <= h; y += left.y) out.push_back({Image::Left, {-left.x, y, left.x, left.y}, false, false, leftClip});
    for (int y = 0; y <= h; y += left.y) out.push_back({Image::Left, {w, y, left.x, left.y}, true, false, rightClip});
  }
  if (corner.x > 0 && corner.y > 0) {
    out.push_back({Image::Corner, {-L, -corner.y, corner.x, corner.y}, false, false});
    out.push_back({Image::Corner, {w + L - corner.x, -corner.y, corner.x, corner.y}, true, false});
    out.push_back({Image::Corner, {-L, h, corner.x, corner.y}, false, true});
    out.push_back({Image::Corner, {w + L - corner.x, h, corner.x, corner.y}, true, true});
  }
  if (ornament.x > 0 && ornament.y > 0)
    out.push_back({Image::Ornament, {-L + (w + 2 * L - ornament.x) / 2, -ornament.y, ornament.x, ornament.y}, false, false});
  return out;
}

void FrameWindow::init(const ui::Style& style) {
  Window::init(style);
  if (auto v = style.numbers("color", 4, 1.0))
    color_ = {static_cast<float>((*v)[0]), static_cast<float>((*v)[1]), static_cast<float>((*v)[2]), static_cast<float>((*v)[3])};
  empty_ = style.booleanOr("empty", false);
  alpha_ = static_cast<float>(style.numberOr("alpha", 1.0));
  const char* keys[4] = {"corner", "top", "left", "ornament"};
  std::shared_ptr<gfx::Texture>* slots[4] = {&corner_, &top_, &left_, &ornament_};
  for (int i = 0; i < 4; ++i) {
    names_[i] = style.stringOr(keys[i]);
    if (!names_[i].empty())
      if (gfx::TextureCache* cache = manager().textures()) *slots[i] = cache->load(names_[i]);
  }
}

const gfx::Texture* FrameWindow::texture(FramePiece::Image i) const {
  switch (i) {
    case FramePiece::Image::Corner: return corner_.get();
    case FramePiece::Image::Top: return top_.get();
    case FramePiece::Image::Left: return left_.get();
    case FramePiece::Image::Ornament: return ornament_.get();
  }
  return nullptr;
}

namespace {
gfx::PointI sizeOf(const std::shared_ptr<gfx::Texture>& t) { return t ? gfx::PointI{t->width(), t->height()} : gfx::PointI{}; }
}  // namespace

// The border is laid out around the rect, in window coordinates.
std::vector<FramePiece> FrameWindow::pieces() const {
  return layoutFrame(rect().w, rect().h, sizeOf(corner_), sizeOf(left_), sizeOf(top_), sizeOf(ornament_));
}

void FrameWindow::drawPiece(ui::DrawContext& ctx, const FramePiece& p) const {
  const gfx::Texture* tex = texture(p.image);
  if (!tex) return;
  const bool clipped = p.clip.w > 0;
  if (clipped)
    ctx.renderer.pushClip({static_cast<int>(ctx.origin.x) + p.clip.x, static_cast<int>(ctx.origin.y) + p.clip.y, p.clip.w, p.clip.h});
  const gfx::RectF dst{ctx.origin.x + static_cast<float>(p.rect.x), ctx.origin.y + static_cast<float>(p.rect.y),
                       static_cast<float>(p.rect.w), static_cast<float>(p.rect.h)};
  ctx.renderer.drawTexture(*tex, {}, dst, {1, 1, 1, ctx.alpha * alpha_}, gfx::Blend::Normal, 0, {}, p.hflip, p.vflip);
  if (clipped) ctx.renderer.popClip();
}

void FrameWindow::paint(ui::DrawContext& ctx) const {
  if (!empty_) {
    gfx::ColorF c = color_;
    c.a *= ctx.alpha;
    const gfx::RectI f = frameFillRect(rect().w, rect().h);
    ctx.renderer.fillRect({ctx.origin.x + static_cast<float>(f.x), ctx.origin.y + static_cast<float>(f.y), static_cast<float>(f.w),
                           static_cast<float>(f.h)},
                          c, gfx::Blend::Normal);
  }
  for (const FramePiece& p : pieces()) drawPiece(ctx, p);
}

// With border images everything is drawn in drawOutside (the border lies outside the rect's clip); without any, the fill is
// drawn here, inset in the window's own rect.
void FrameWindow::draw(ui::DrawContext& ctx) const {
  if (!drawsOutside()) paint(ctx);
}

void FrameWindow::drawOutside(ui::DrawContext& ctx) const { paint(ctx); }

std::string FrameWindow::detail() const {
  std::string s;
  if (empty_) s += " empty";
  for (const std::string& n : names_)
    if (!n.empty()) s += " " + n;
  return s;
}

}  // namespace choco::game::choc2
