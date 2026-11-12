#include "games/greatcc/scene/TiledObject.h"

#include <algorithm>

#include "games/greatcc/scene/SpriteManager.h"
#include "gfx/Quad.h"

namespace choco::games::greatcc {

std::array<TiledPiece, 9> tiledPieces(int tw, int th, int w, int h, int x1, int x2, int y1, int y2) {
  const float TW = static_cast<float>(tw), TH = static_cast<float>(th);
  const float L = std::min(static_cast<float>(x1), TW), T = std::min(static_cast<float>(y1), TH);
  const float Rs = std::max(0.0f, TW - static_cast<float>(x2)), Bs = std::max(0.0f, TH - static_cast<float>(y2));
  const float mw = static_cast<float>(w - x1 - x2), mh = static_cast<float>(h - y1 - y2);
  const float sx = Rs != L ? mw / (Rs - L) : 1.0f, sy = Bs != T ? mh / (Bs - T) : 1.0f;
  const float cx = static_cast<float>(x1) + mw / 2, cy = static_cast<float>(y1) + mh / 2;
  const float W = static_cast<float>(w), H = static_cast<float>(h);
  const float hx1 = static_cast<float>(x1) / 2, hx2 = static_cast<float>(x2) / 2, hy1 = static_cast<float>(y1) / 2,
              hy2 = static_cast<float>(y2) / 2;
  auto piece = [](float x0, float y0, float xe, float ye, float px, float py, float psx, float psy) {
    TiledPiece p;
    p.src = {x0, y0, xe - x0, ye - y0};
    p.center = {px, py};
    p.scale = {psx, psy};
    p.drawn = p.src.w > 0 && p.src.h > 0;
    return p;
  };
  return {{
      piece(0, 0, L, T, hx1, hy1, 1, 1),
      piece(Rs, 0, TW, T, W - hx2, hy1, 1, 1),
      piece(0, Bs, L, TH, hx1, H - hy2, 1, 1),
      piece(Rs, Bs, TW, TH, W - hx2, H - hy2, 1, 1),
      piece(L, 0, Rs, T, cx, hy1, sx, 1),
      piece(L, Bs, Rs, TH, cx, H - hy2, sx, 1),
      piece(0, T, L, Bs, hx1, cy, 1, sy),
      piece(Rs, T, TW, Bs, W - hx2, cy, 1, sy),
      piece(L, T, Rs, Bs, cx, cy, sx, sy),
  }};
}

void TiledObject::loadFromTable(const StyleChain& style) {
  x1_ = style.intOr("x1", 0, 0);
  x2_ = style.intOr("x2", 0, 0);
  y1_ = style.intOr("y1", 0, 0);
  y2_ = style.intOr("y2", 0, 0);
  w_ = style.intOr("w", 0, 0);
  h_ = style.intOr("h", 0, 0);
  VObject::loadFromTable(style);
}

void TiledObject::init() {
  switchAsset("background", "default", "default");
  VObject::init();
}

namespace {
// The texture and the image-pixel rectangle the nine-slice cuts: the whole texture, or the current frame of a sheet.
bool imageOf(const VSprite& s, const gfx::Texture*& tex, gfx::RectF& base) {
  if (s.sheet()) {
    const gfx::SheetFrame* f = s.frameInfo();
    if (!f || !s.sheet()->image) return false;
    tex = s.sheet()->image.get();
    base = {static_cast<float>(f->src.x), static_cast<float>(f->src.y), static_cast<float>(f->src.w), static_cast<float>(f->src.h)};
    return true;
  }
  if (s.plainTexture()) {
    tex = s.plainTexture().get();
    base = {0, 0, static_cast<float>(tex->width()), static_cast<float>(tex->height())};
    return true;
  }
  return false;
}
}  // namespace

void TiledObject::drawImage(gfx::Renderer& r, const DrawSpec& spec) const {
  const Mat3& abs = spec.m;
  const float alpha = spec.alpha;
  const gfx::Texture* tex = nullptr;
  gfx::RectF base;
  if (w_ <= 0 || h_ <= 0 || !imageOf(*this, tex, base)) return;
  if (manager()) manager()->noteTextureUsed(*tex);
  const auto pieces = tiledPieces(static_cast<int>(base.w), static_cast<int>(base.h), w_, h_, x1_, x2_, y1_, y2_);
  static const int kIdx[6] = {0, 1, 2, 0, 2, 3};
  for (const TiledPiece& p : pieces) {
    if (!p.drawn) continue;
    const float dw = p.src.w * p.scale.x, dh = p.src.h * p.scale.y;
    auto quad = gfx::buildTexturedQuad({base.x + p.src.x, base.y + p.src.y, p.src.w, p.src.h},
                                       {p.center.x - dw / 2, p.center.y - dh / 2, dw, dh}, tex->width(), tex->height(),
                                       {tint().r, tint().g, tint().b, alpha * tint().a}, 0, {}, false, false);
    gfx::Vertex verts[4];
    for (int i = 0; i < 4; ++i) {
      verts[i] = quad[static_cast<std::size_t>(i)];
      const gfx::PointF q = abs.apply(verts[i].pos);
      verts[i].pos = q;
    }
    r.drawGeometry(tex, verts, kIdx, blendOf(spec.blend));
  }
}

void TiledObject::copyTo(VSprite& dst) {
  VObject::copyTo(dst);
  if (auto* d = dynamic_cast<TiledObject*>(&dst)) {
    d->x1_ = x1_;
    d->x2_ = x2_;
    d->y1_ = y1_;
    d->y2_ = y2_;
    d->setSize(w_, h_);
  }
}

bool TiledObject::imageHit(gfx::PointI p, const DrawSpec& spec, int) const {
  const gfx::RectF r = frameRect(spec);
  return r.w > 0 && r.h > 0 && static_cast<float>(p.x) >= r.x && static_cast<float>(p.x) < r.x + r.w &&
         static_cast<float>(p.y) >= r.y && static_cast<float>(p.y) < r.y + r.h;
}

gfx::RectF TiledObject::frameRect(const DrawSpec& spec) const {
  const Mat3& abs = spec.m;
  if (w_ <= 0 || h_ <= 0) return {};
  const gfx::PointF a = abs.apply({0, 0}), b = abs.apply({static_cast<float>(w_), static_cast<float>(h_)});
  return {std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
}

}  // namespace choco::games::greatcc
