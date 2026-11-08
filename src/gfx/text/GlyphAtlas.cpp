#include "gfx/text/GlyphAtlas.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace choco::gfx::text {

struct GlyphAtlas::Page {
  struct Shelf {
    int y, h, x;
  };
  std::vector<std::uint8_t> rgba;
  std::vector<Shelf> shelves;
  std::shared_ptr<Texture> texture;
  bool dirty = true;
  bool whole = true;  // the GPU copy must be created (or re-created) from scratch rather than patched
  int dx0 = kPageSize, dy0 = kPageSize, dx1 = 0, dy1 = 0;  // changed texels since the last flush
  void touch(int x, int y, int w, int h) {
    dirty = true;
    dx0 = std::min(dx0, x);
    dy0 = std::min(dy0, y);
    dx1 = std::max(dx1, x + w);
    dy1 = std::max(dy1, y + h);
  }
  Page() : rgba(static_cast<std::size_t>(kPageSize) * kPageSize * 4, 0) {}
};

Bitmap rasterizeGlyph(const Font& font, int glyph, const RasterParams& p) {
  auto polys = flatten(font.outline(glyph), p.scale, p.scale * p.yRatio * font.glyphScaleY(glyph), p.italic,
                       static_cast<float>(p.subX) / 4.0f);
  Bitmap fill = rasterize(polys);
  if (p.stroke > 0) return unite(fill, rasterize(strokePolygons(polys, p.stroke)));
  return fill;
}

GlyphAtlas::GlyphAtlas(TextureUploader* uploader) : uploader_(uploader) {}
GlyphAtlas::~GlyphAtlas() = default;

void GlyphAtlas::beginDraw() {
  if (overflowed_) wipe();
}

void GlyphAtlas::setOutputScale(float scale) {
  if (scale == outputScale_) return;
  outputScale_ = scale;
  if (!entries_.empty() || overflowed_) wipe();
}

void GlyphAtlas::wipe() {
  overflowed_ = false;
  entries_.clear();
  if (pages_.size() > static_cast<std::size_t>(kMaxPages)) pages_.resize(kMaxPages);
  for (auto& pg : pages_) {
    std::fill(pg->rgba.begin(), pg->rgba.end(), 0);
    pg->shelves.clear();
    pg->dirty = true;
    pg->whole = true;
  }
}

GlyphAtlas::Entry GlyphAtlas::place(const Bitmap& bm) {
  Entry e;
  e.x = bm.x;
  e.y = bm.y;
  e.w = bm.w;
  e.h = bm.h;
  const int needW = bm.w + 1, needH = bm.h + 1;  // one transparent texel of gutter right and below
  if (needW > kPageSize || needH > kPageSize) return Entry{};
  for (std::size_t pi = 0;; ++pi) {
    if (pi == pages_.size()) {
      pages_.push_back(std::make_unique<Page>());
      Page& np = *pages_.back();
      std::shared_ptr<Texture> t(new Texture());
      t->width_ = t->pixelWidth_ = t->height_ = t->pixelHeight_ = kPageSize;
      t->path_ = "glyph-atlas";
      t->uploader_ = uploader_;
      np.texture = std::move(t);
      if (pages_.size() > static_cast<std::size_t>(kMaxPages)) overflowed_ = true;
    }
    Page& pg = *pages_[pi];
    int px = -1, py = -1;
    for (auto& s : pg.shelves) {
      if (s.h >= needH && s.h <= needH + needH / 2 + 2 && s.x + needW <= kPageSize) {
        px = s.x;
        py = s.y;
        s.x += needW;
        break;
      }
    }
    if (px < 0) {
      const int y = pg.shelves.empty() ? 0 : pg.shelves.back().y + pg.shelves.back().h;
      if (y + needH <= kPageSize) {
        pg.shelves.push_back({y, needH, needW});
        px = 0;
        py = y;
      }
    }
    if (px < 0) continue;
    for (int y = 0; y < bm.h; ++y) {
      std::uint8_t* d = pg.rgba.data() + (static_cast<std::size_t>(py + y) * kPageSize + static_cast<std::size_t>(px)) * 4;
      for (int x = 0; x < bm.w; ++x) {
        d[x * 4] = d[x * 4 + 1] = d[x * 4 + 2] = 255;
        d[x * 4 + 3] = bm.at(x, y);
      }
    }
    pg.touch(px, py, bm.w, bm.h);
    e.texture = pg.texture.get();
    e.u0 = static_cast<float>(px) / kPageSize;
    e.v0 = static_cast<float>(py) / kPageSize;
    e.u1 = static_cast<float>(px + bm.w) / kPageSize;
    e.v1 = static_cast<float>(py + bm.h) / kPageSize;
    return e;
  }
}

GlyphAtlas::Entry GlyphAtlas::get(const Font& font, int glyph, const RasterParams& p) {
  std::uint32_t scaleBits;
  std::memcpy(&scaleBits, &p.scale, sizeof scaleBits);
  const Key key{font.id(), glyph, scaleBits, p.subX, p.italic ? 1 : 0, static_cast<int>(std::lround(p.stroke * 16.0f)),
                 static_cast<int>(std::lround(p.yRatio * 4096.0f))};
  if (auto it = entries_.find(key); it != entries_.end()) return it->second;
  ++rasterisations_;
  Bitmap bm = rasterizeGlyph(font, glyph, p);
  Entry e = bm.empty() ? Entry{} : place(bm);
  entries_[key] = e;
  return e;
}

void GlyphAtlas::flush() {
  for (auto& pg : pages_) {
    if (!pg->dirty) continue;
    pg->dirty = false;
    if (!uploader_) continue;
    const int x = pg->dx0, y = pg->dy0, w = pg->dx1 - pg->dx0, h = pg->dy1 - pg->dy0;
    pg->dx0 = pg->dy0 = kPageSize;
    pg->dx1 = pg->dy1 = 0;
    // Only the texels a new glyph touched go to the GPU; the page texture lives on.
    if (!pg->whole && pg->texture->handle_ && w > 0 && h > 0 &&
        uploader_->update(pg->texture->handle_, x, y, w, h,
                          pg->rgba.data() + (static_cast<std::size_t>(y) * kPageSize + static_cast<std::size_t>(x)) * 4,
                          kPageSize * 4)) {
      ++uploads_;
      ++partialUploads_;
      continue;
    }
    void* fresh = uploader_->upload(pg->rgba.data(), kPageSize, kPageSize);
    if (!fresh) continue;
    if (pg->texture->handle_) uploader_->release(pg->texture->handle_);
    pg->texture->handle_ = fresh;
    pg->whole = false;
    ++uploads_;
  }
}

void GlyphAtlas::reuploadAll() {
  for (auto& pg : pages_) {
    pg->dirty = true;
    pg->whole = true;
  }
  flush();
}

}  // namespace choco::gfx::text
