#include "gfx/Curve.h"

#include <cmath>

namespace choco::gfx {

namespace {

float dist(PointF a, PointF b) { return std::hypot(b.x - a.x, b.y - a.y); }
PointF mid(PointF a, PointF b) { return {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}; }
constexpr int kMaxDepth = 24;  // a guard against NaN or enormous coordinates, never reached with real routes

}  // namespace

Curve Curve::fromPoints(std::vector<PointF> points) {
  Curve c;
  c.cp_ = std::move(points);
  return c;
}

void Curve::subdivide(float t0, float t1, PointF p0, PointF p1, PointF p2, PointF p3, int depth) const {
  const float chord = dist(p0, p3);
  const float polygon = dist(p0, p1) + dist(p1, p2) + dist(p2, p3);
  if (polygon - chord <= kAcceptableError || depth >= kMaxDepth) {
    pieces_.push_back({t0, t1, polygon, p0, p3});
    return;
  }
  const PointF p01 = mid(p0, p1), p12 = mid(p1, p2), p23 = mid(p2, p3);
  const PointF p012 = mid(p01, p12), p123 = mid(p12, p23);
  const PointF m = mid(p012, p123);
  const float tm = (t0 + t1) * 0.5f;
  subdivide(t0, tm, p0, p01, p012, m, depth + 1);
  subdivide(tm, t1, m, p123, p23, p3, depth + 1);
}

void Curve::build() const {
  built_ = true;
  pieces_.clear();
  length_ = 0;
  if (cp_.size() < 4) return;
  for (std::size_t i = 0; i + 3 < cp_.size(); i += 3) subdivide(0, 1, cp_[i], cp_[i + 1], cp_[i + 2], cp_[i + 3], 0);
  for (const Piece& p : pieces_) length_ += p.length;
}

float Curve::length() const {
  if (!built_) build();
  return length_;
}

PointF Curve::at(float t, PointF* tangent) const {
  if (cp_.empty()) return {};
  if (t >= 1) return cp_[((cp_.size() - 1) / 3) * 3];
  if (t <= 0) return cp_.front();
  if (!built_) build();
  if (pieces_.empty()) return cp_.front();
  float d = t * length_;
  for (const Piece& p : pieces_) {
    if (d <= p.length) {
      const float f = p.length > 0 ? d / p.length : 0.0f;
      const PointF dir{p.end.x - p.start.x, p.end.y - p.start.y};
      if (tangent) {
        const float n = std::hypot(dir.x, dir.y);
        *tangent = n > 0 ? PointF{dir.x / n, dir.y / n} : PointF{};
      }
      return {p.start.x + dir.x * f, p.start.y + dir.y * f};
    }
    d -= p.length;
  }
  // Rounding left a sliver (or t was NaN): the end of the last piece.
  if (tangent) {
    const Piece& p = pieces_.back();
    const PointF dir{p.end.x - p.start.x, p.end.y - p.start.y};
    const float n = std::hypot(dir.x, dir.y);
    *tangent = n > 0 ? PointF{dir.x / n, dir.y / n} : PointF{};
  }
  return pieces_.back().end;
}

}  // namespace choco::gfx
