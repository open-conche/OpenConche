#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

#include "gfx/Texture.h"
#include "gfx/TextureCache.h"
#include "gfx/text/Font.h"
#include "gfx/text/Raster.h"

namespace choco::gfx::text {

// What to rasterise. The glyph origin is the pen position on the baseline; `subX` (0..3) shifts it right by that
// many quarter pixels, the grid the original rasterises on (docs/re/fonts-and-text.md section 2).
struct RasterParams {
  float scale = 1;     // device pixels per font unit horizontally
  float yRatio = 1;    // vertical scale relative to the horizontal one (output pixels need not be square)
  int subX = 0;        // quarter pixels
  bool italic = false;
  float stroke = 0;    // > 0: glyph widened by this many device pixels on every side (outline layer and bold)
};

// Renders one glyph to coverage: the fill, or fill united with its stroke.
Bitmap rasterizeGlyph(const Font& font, int glyph, const RasterParams& p);

// Glyph coverage cached in shelf-packed atlas textures. Pixels are white with the coverage in alpha, so a vertex colour
// tints them and its alpha fades them. Pages are created on demand and uploaded through the TextureUploader.
class GlyphAtlas {
 public:
  static constexpr int kPageSize = 1024;
  static constexpr int kMaxPages = 4;  // beyond this the atlas is emptied at the next beginDraw()

  explicit GlyphAtlas(TextureUploader* uploader);
  ~GlyphAtlas();
  GlyphAtlas(const GlyphAtlas&) = delete;
  GlyphAtlas& operator=(const GlyphAtlas&) = delete;

  struct Entry {
    const Texture* texture = nullptr;  // null: nothing to draw (blank glyph)
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    int x = 0, y = 0, w = 0, h = 0;  // bitmap box relative to the glyph origin, device pixels
  };

  // Call at the start of a draw: empties the atlas if it overflowed during the previous one.
  void beginDraw();
  // The output scale glyphs are rasterised at (the renderer's letterbox scale). When it changes, every entry of the
  // old scale is dead weight, so the atlas is emptied at once: call between frames, with nothing batched. The same
  // scale again does nothing, and an empty atlas just remembers it.
  void setOutputScale(float scale);
  // Rasterises on a miss. Never fails: a glyph too large for a page comes back blank.
  Entry get(const Font& font, int glyph, const RasterParams& p);
  // Uploads pages that changed since the last call. Call before the draw that uses the entries.
  void flush();
  // Re-creates every GPU texture from the CPU copies after a device reset.
  void reuploadAll();

  std::size_t pageCount() const { return pages_.size(); }
  std::size_t glyphCount() const { return entries_.size(); }
  std::size_t rasterisations() const { return rasterisations_; }
  std::size_t uploads() const { return uploads_; }                 // every flush that sent pixels to the GPU
  std::size_t partialUploads() const { return partialUploads_; }  // of those, patches of a sub-rectangle

 private:
  struct Page;
  using Key = std::tuple<std::uint64_t, int, std::uint32_t, int, int, int, int>;
  Entry place(const Bitmap& bm);
  void wipe();  // drops every entry and zeroes the pages (at most kMaxPages are kept)

  TextureUploader* uploader_;
  std::vector<std::unique_ptr<Page>> pages_;
  std::map<Key, Entry> entries_;
  bool overflowed_ = false;
  float outputScale_ = 0;
  std::size_t rasterisations_ = 0, uploads_ = 0, partialUploads_ = 0;
};

}  // namespace choco::gfx::text
