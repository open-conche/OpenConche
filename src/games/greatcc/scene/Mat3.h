#pragma once

// The sprite scene's 3x3 matrix (docs/re/greatcc-sprites-and-zones.md 1.5, 2.3): row vectors, v' = v * M, the translation in
// the third row, like every TMat3 of the original. `a * b` applies `a` first, then `b`.

#include <cmath>

#include "gfx/Types.h"

namespace choco::games::greatcc {

struct Mat3 {
  float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

  static Mat3 translation(float x, float y) {
    Mat3 r;
    r.m[2][0] = x;
    r.m[2][1] = y;
    return r;
  }
  static Mat3 scaling(float sx, float sy) {
    Mat3 r;
    r.m[0][0] = sx;
    r.m[1][1] = sy;
    return r;
  }
  // Rotation by `angle` radians about the point (cx, cy): translate it to the origin, rotate, translate back. In screen
  // coordinates (y down) a positive angle turns clockwise.
  static Mat3 rotationAbout(float cx, float cy, float angle) {
    Mat3 rot;
    const float c = std::cos(angle), s = std::sin(angle);
    rot.m[0][0] = c;
    rot.m[0][1] = s;
    rot.m[1][0] = -s;
    rot.m[1][1] = c;
    return translation(-cx, -cy) * rot * translation(cx, cy);
  }
  // The frame's xform words (x0 x1 0 / x2 x3 0 / x4 x5 1).
  static Mat3 fromWords(const float w[6]) {
    Mat3 r;
    r.m[0][0] = w[0];
    r.m[0][1] = w[1];
    r.m[1][0] = w[2];
    r.m[1][1] = w[3];
    r.m[2][0] = w[4];
    r.m[2][1] = w[5];
    return r;
  }

  Mat3 operator*(const Mat3& b) const {
    Mat3 r;
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) r.m[i][j] = m[i][0] * b.m[0][j] + m[i][1] * b.m[1][j] + m[i][2] * b.m[2][j];
    return r;
  }
  gfx::PointF apply(gfx::PointF p) const {
    return {p.x * m[0][0] + p.y * m[1][0] + m[2][0], p.x * m[0][1] + p.y * m[1][1] + m[2][1]};
  }
  // The point that `apply` maps to `p`; false for a singular matrix.
  bool applyInverse(gfx::PointF p, gfx::PointF& out) const {
    const float det = m[0][0] * m[1][1] - m[0][1] * m[1][0];
    if (std::fabs(det) < 1e-12f) return false;
    const float dx = p.x - m[2][0], dy = p.y - m[2][1];
    out = {(dx * m[1][1] - dy * m[1][0]) / det, (dy * m[0][0] - dx * m[0][1]) / det};
    return true;
  }
  bool operator==(const Mat3&) const = default;
};

}  // namespace choco::games::greatcc
