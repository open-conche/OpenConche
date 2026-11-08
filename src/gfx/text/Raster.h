#pragma once

#include <cstdint>
#include <vector>

#include "gfx/text/Font.h"

namespace choco::gfx::text {

// Closed polygons in device pixels, y down.
using Polygon = std::vector<PointF>;

// 8-bit coverage. (x, y) is the top-left corner relative to the glyph origin (pen position on the baseline).
struct Bitmap {
  int x = 0, y = 0, w = 0, h = 0;
  std::vector<std::uint8_t> alpha;  // w * h
  bool empty() const { return w <= 0 || h <= 0; }
  std::uint8_t at(int px, int py) const { return alpha[static_cast<std::size_t>(py) * static_cast<std::size_t>(w) + static_cast<std::size_t>(px)]; }
};

// Quadratics become exactly three line segments (t = 1/3, 2/3, 1), as in the original. Contours are scaled by
// (scaleX, scaleY), sheared when italic (x += y / -3 in font units, so tops lean right), then shifted by dx.
std::vector<Polygon> flatten(const std::vector<Contour>& contours, float scaleX, float scaleY, bool italic, float dx);

// Union of rectangles around every edge plus a square at every vertex, each `half` pixels from the edge. The original
// strokes outlines the same way (flat ends); the squares close the gaps at corners.
std::vector<Polygon> strokePolygons(const std::vector<Polygon>& outline, float half);

// Scanline fill, non-zero winding. Four sub-scanlines per pixel with exact horizontal coverage, so the result is a
// 4x vertical supersample with an analytic horizontal one. Returns an empty bitmap if there is nothing to draw or the
// result would exceed `maxDim` pixels either way.
Bitmap rasterize(const std::vector<Polygon>& polys, int maxDim = 1024);

// Pixel-wise maximum of two bitmaps (the union of two coverage layers).
Bitmap unite(const Bitmap& a, const Bitmap& b);

}  // namespace choco::gfx::text
