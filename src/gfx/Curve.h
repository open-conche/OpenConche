#pragma once

#include <cstddef>
#include <vector>

#include "gfx/Types.h"

namespace choco::gfx {

// A chain of cubic Beziers over 3n+1 control points (segment k uses points 3k..3k+3), sampled by arc length as the
// original's TCurve does (docs/re/map-window.md 6.2). Each segment is split recursively with de Casteljau until a
// piece's control polygon is within 0.2 of its chord; a piece is then treated as a straight line of the polygon's
// length, so the speed along the curve is uniform in that length.
class Curve {
 public:
  static constexpr float kAcceptableError = 0.2f;

  Curve() = default;
  static Curve fromPoints(std::vector<PointF> points);

  void addControlPoint(float x, float y) {
    cp_.push_back({x, y});
    pieces_.clear();
    built_ = false;
  }
  std::size_t controlPoints() const { return cp_.size(); }
  const std::vector<PointF>& points() const { return cp_; }

  // 0 for fewer than 4 control points. Subdivides on first use.
  float length() const;
  // t <= 0: the first control point. t >= 1: the point at index ((n-1)/3)*3. In both cases `tangent` is untouched.
  // Otherwise the point on the piece reached after t * length() of travel, and the normalised direction of that piece.
  // An empty curve gives (0,0).
  PointF at(float t, PointF* tangent = nullptr) const;

 private:
  struct Piece {
    float tStart = 0, tEnd = 0;
    float length = 0;  // control polygon length
    PointF start, end;
  };
  void build() const;
  void subdivide(float t0, float t1, PointF p0, PointF p1, PointF p2, PointF p3, int depth) const;

  std::vector<PointF> cp_;
  mutable bool built_ = false;
  mutable std::vector<Piece> pieces_;
  mutable float length_ = 0;
};

}  // namespace choco::gfx
