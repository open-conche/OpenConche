#pragma once

#include <algorithm>

namespace choco::gfx {

struct ColorF {
  float r = 1, g = 1, b = 1, a = 1;
  bool operator==(const ColorF&) const = default;
};

struct PointF {
  float x = 0, y = 0;
  bool operator==(const PointF&) const = default;
};

struct PointI {
  int x = 0, y = 0;
  bool operator==(const PointI&) const = default;
};

struct RectF {
  float x = 0, y = 0, w = 0, h = 0;
  bool operator==(const RectF&) const = default;
};

struct RectI {
  int x = 0, y = 0, w = 0, h = 0;
  bool operator==(const RectI&) const = default;
  bool empty() const { return w <= 0 || h <= 0; }

  // Overlap of two rectangles; an empty result is normalised to {x, y, 0, 0} so equal empties compare equal.
  static RectI intersect(const RectI& a, const RectI& b) {
    int x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
    int x1 = std::min(a.x + a.w, b.x + b.w), y1 = std::min(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0) return {x0, y0, 0, 0};
    return {x0, y0, x1 - x0, y1 - y0};
  }
};

// Texture coordinates are normalised (0..1) over the texture.
struct Vertex {
  PointF pos;  // logical coordinates
  ColorF color;
  PointF uv;
};

enum class Blend { Normal, Additive, Multiply, Subtract, Opaque };

}  // namespace choco::gfx
