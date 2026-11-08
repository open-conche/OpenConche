#include "games/choc1/FrameWindow.h"

#include <algorithm>

#include "gfx/TextureCache.h"
#include "ui/DrawContext.h"
#include "ui/Style.h"
#include "ui/WindowManager.h"

namespace choco::game::choc1 {

namespace {
using Image = FramePiece::Image;
constexpr const char *kNames[6] = {"image/frameul", "image/frameur",
                                   "image/framell", "image/framelr",
                                   "image/frameh",  "image/framev"};
constexpr int kInset = 5; // the exe's fill inset (field +0x78)

gfx::PointI sizeOf(const std::shared_ptr<gfx::Texture> &t) {
  return t ? gfx::PointI{t->width(), t->height()} : gfx::PointI{};
}
bool present(gfx::PointI p) { return p.x > 0 && p.y > 0; }
} // namespace

gfx::RectI frameFillRect(int w, int h) {
  return {kInset, kInset, std::max(0, w - 3 * kInset),
          std::max(0, h - 3 * kInset)};
}

std::vector<FramePiece> layoutFrame(int w, int h, const FrameSizes &s) {
  std::vector<FramePiece> out;
  auto add = [&](Image i, gfx::PointI size, int x, int y) {
    if (present(size))
      out.push_back({i, {x, y, size.x, size.y}});
  };
  add(Image::UL, s.ul, 0, 0);
  if (present(s.h))
    for (int x = s.ul.x; x < w; x += s.h.x)
      add(Image::H, s.h, x, 0);
  if (present(s.v)) {
    for (int y = s.ul.y; y < h; y += s.v.y)
      add(Image::V, s.v, 0, y);
    for (int y = s.ur.y; y < h; y += s.v.y)
      add(Image::V, s.v, w - s.v.x, y);
  }
  add(Image::UR, s.ur, w - s.ur.x, 0);
  add(Image::LL, s.ll, 0, h - s.ll.y);
  if (present(s.h))
    for (int x = s.ll.x; x < w; x += s.h.x)
      add(Image::H, s.h, x, h - s.h.y);
  add(Image::LR, s.lr, w - s.lr.x, h - s.lr.y);
  return out;
}

void FrameWindow::init(const ui::Style &style) {
  Window::init(style);
  if (auto v = style.numbers("color", 4, 1.0))
    color_ = {static_cast<float>((*v)[0]), static_cast<float>((*v)[1]),
              static_cast<float>((*v)[2]), static_cast<float>((*v)[3])};
  hollow_ = style.booleanOr("hollow", false);
  gfx::TextureCache *cache = manager().textures();
  for (int i = 0; i < 6; ++i) {
    if (cache)
      tex_[i] = cache->load(kNames[i]);
    if (!tex_[i] &&
        ctx_.warned.insert(std::string("choc1-frame-") + kNames[i]).second)
      ctx_.log.write(std::string("[choc1] Frame: ") + kNames[i] +
                     " not found, that part of the border is not drawn");
  }
}

int FrameWindow::imagesLoaded() const {
  int n = 0;
  for (const auto &t : tex_)
    n += t ? 1 : 0;
  return n;
}

void FrameWindow::draw(ui::DrawContext &ctx) const {
  const int w = rect().w, h = rect().h;
  if (!hollow_) {
    gfx::ColorF c = color_;
    c.a *= ctx.alpha;
    const gfx::RectI f = frameFillRect(w, h);
    ctx.renderer.fillRect({ctx.origin.x + static_cast<float>(f.x),
                           ctx.origin.y + static_cast<float>(f.y),
                           static_cast<float>(f.w), static_cast<float>(f.h)},
                          c, gfx::Blend::Normal);
  }
  const FrameSizes sizes{sizeOf(tex_[0]), sizeOf(tex_[1]), sizeOf(tex_[2]),
                         sizeOf(tex_[3]), sizeOf(tex_[4]), sizeOf(tex_[5])};
  for (const FramePiece &p : layoutFrame(w, h, sizes)) {
    const gfx::Texture &tex = *tex_[static_cast<int>(p.image)];
    ctx.renderer.drawTexture(
        tex, {},
        {ctx.origin.x + static_cast<float>(p.rect.x),
         ctx.origin.y + static_cast<float>(p.rect.y),
         static_cast<float>(p.rect.w), static_cast<float>(p.rect.h)},
        {1, 1, 1, ctx.alpha}, gfx::Blend::Normal, 0, {}, false, false);
  }
}

std::string FrameWindow::detail() const { return hollow_ ? " hollow" : ""; }

} // namespace choco::game::choc1
