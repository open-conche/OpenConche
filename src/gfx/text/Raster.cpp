#include "gfx/text/Raster.h"

#include <algorithm>
#include <cmath>

namespace choco::gfx::text {

std::vector<Polygon> flatten(const std::vector<Contour>& contours, float sx, float sy, bool italic, float dx) {
  std::vector<Polygon> out;
  auto map = [&](PointF p) {
    const float x = italic ? p.x - p.y / 3.0f : p.x;
    return PointF{x * sx + dx, p.y * sy};
  };
  for (const Contour& c : contours) {
    Polygon poly;
    PointF cur = c.start;
    poly.push_back(map(cur));
    for (const Segment& s : c.segs) {
      if (s.quad) {
        // point = A t^2 + B t + P0 with A = P0 - 2 P1 + P2 and B = 2 (P1 - P0)
        const PointF a{cur.x - 2 * s.c.x + s.p.x, cur.y - 2 * s.c.y + s.p.y};
        const PointF b{2 * (s.c.x - cur.x), 2 * (s.c.y - cur.y)};
        for (int i = 1; i <= 3; ++i) {
          const float t = static_cast<float>(i) / 3.0f;
          poly.push_back(map({a.x * t * t + b.x * t + cur.x, a.y * t * t + b.y * t + cur.y}));
        }
      } else {
        poly.push_back(map(s.p));
      }
      cur = s.p;
    }
    if (poly.size() >= 3) out.push_back(std::move(poly));
  }
  return out;
}

std::vector<Polygon> strokePolygons(const std::vector<Polygon>& outline, float half) {
  std::vector<Polygon> out;
  if (half <= 0) return out;
  for (const Polygon& poly : outline) {
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
      const PointF a = poly[i], b = poly[(i + 1) % n];
      out.push_back({{a.x - half, a.y - half}, {a.x + half, a.y - half}, {a.x + half, a.y + half}, {a.x - half, a.y + half}});
      const float dx = b.x - a.x, dy = b.y - a.y;
      const float len = std::sqrt(dx * dx + dy * dy);
      if (len < 1e-6f) continue;
      const float nx = -dy / len * half, ny = dx / len * half;
      Polygon quad{{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}};
      // Every piece must wind the same way, or overlaps would cancel under the non-zero rule.
      const float area = (quad[1].x - quad[0].x) * (quad[2].y - quad[0].y) - (quad[2].x - quad[0].x) * (quad[1].y - quad[0].y);
      if (area < 0) std::reverse(quad.begin(), quad.end());
      out.push_back(std::move(quad));
    }
  }
  // Squares above are axis-aligned and wind the same way (clockwise on screen, positive area).
  return out;
}

namespace {

struct Edge {
  float x0, y0, x1, y1;  // y0 < y1
  int dir;
};

}  // namespace

Bitmap rasterize(const std::vector<Polygon>& polys, int maxDim) {
  Bitmap bm;
  float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
  std::vector<Edge> edges;
  for (const Polygon& poly : polys) {
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
      const PointF a = poly[i], b = poly[(i + 1) % n];
      minX = std::min({minX, a.x, b.x});
      maxX = std::max({maxX, a.x, b.x});
      minY = std::min({minY, a.y, b.y});
      maxY = std::max({maxY, a.y, b.y});
      if (a.y == b.y) continue;
      if (a.y < b.y) edges.push_back({a.x, a.y, b.x, b.y, 1});
      else edges.push_back({b.x, b.y, a.x, a.y, -1});
    }
  }
  if (edges.empty()) return bm;
  const int x0 = static_cast<int>(std::floor(minX)), y0 = static_cast<int>(std::floor(minY));
  const int x1 = static_cast<int>(std::ceil(maxX)), y1 = static_cast<int>(std::ceil(maxY));
  if (x1 - x0 > maxDim || y1 - y0 > maxDim || x1 <= x0 || y1 <= y0) return bm;
  bm.x = x0;
  bm.y = y0;
  bm.w = x1 - x0;
  bm.h = y1 - y0;
  bm.alpha.assign(static_cast<std::size_t>(bm.w) * static_cast<std::size_t>(bm.h), 0);

  std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) { return a.y0 < b.y0; });
  std::vector<float> acc(static_cast<std::size_t>(bm.w));
  std::vector<std::pair<float, int>> xs;
  std::vector<const Edge*> active;
  std::size_t next = 0;
  for (int row = 0; row < bm.h; ++row) {
    std::fill(acc.begin(), acc.end(), 0.0f);
    bool any = false;
    for (int sub = 0; sub < 4; ++sub) {
      const float y = static_cast<float>(y0 + row) + (static_cast<float>(sub) + 0.5f) / 4.0f;
      while (next < edges.size() && edges[next].y0 <= y) active.push_back(&edges[next++]);
      xs.clear();
      for (std::size_t i = 0; i < active.size();) {
        const Edge& e = *active[i];
        if (e.y1 <= y) {
          active[i] = active.back();
          active.pop_back();
          continue;
        }
        xs.push_back({e.x0 + (y - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0), e.dir});
        ++i;
      }
      if (xs.size() < 2) continue;
      std::sort(xs.begin(), xs.end());
      int wind = 0;
      for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
        wind += xs[i].second;
        if (wind == 0) continue;
        const float a = std::clamp(xs[i].first - static_cast<float>(x0), 0.0f, static_cast<float>(bm.w));
        const float b = std::clamp(xs[i + 1].first - static_cast<float>(x0), 0.0f, static_cast<float>(bm.w));
        if (b <= a) continue;
        any = true;
        const int ia = static_cast<int>(a), ib = std::min(static_cast<int>(b), bm.w - 1);
        if (ia == ib) {
          acc[static_cast<std::size_t>(ia)] += (b - a) * 0.25f;
        } else {
          acc[static_cast<std::size_t>(ia)] += (static_cast<float>(ia + 1) - a) * 0.25f;
          for (int k = ia + 1; k < ib; ++k) acc[static_cast<std::size_t>(k)] += 0.25f;
          acc[static_cast<std::size_t>(ib)] += (b - static_cast<float>(ib)) * 0.25f;
        }
      }
    }
    if (!any) continue;
    std::uint8_t* dst = bm.alpha.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(bm.w);
    for (int k = 0; k < bm.w; ++k) {
      dst[k] = static_cast<std::uint8_t>(std::lround(std::min(1.0f, acc[static_cast<std::size_t>(k)]) * 255.0f));
    }
  }
  return bm;
}

Bitmap unite(const Bitmap& a, const Bitmap& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  Bitmap r;
  r.x = std::min(a.x, b.x);
  r.y = std::min(a.y, b.y);
  r.w = std::max(a.x + a.w, b.x + b.w) - r.x;
  r.h = std::max(a.y + a.h, b.y + b.h) - r.y;
  r.alpha.assign(static_cast<std::size_t>(r.w) * static_cast<std::size_t>(r.h), 0);
  for (const Bitmap* s : {&a, &b}) {
    for (int y = 0; y < s->h; ++y) {
      for (int x = 0; x < s->w; ++x) {
        std::uint8_t& d = r.alpha[static_cast<std::size_t>(y + s->y - r.y) * static_cast<std::size_t>(r.w) + static_cast<std::size_t>(x + s->x - r.x)];
        d = std::max(d, s->at(x, y));
      }
    }
  }
  return r;
}

}  // namespace choco::gfx::text
