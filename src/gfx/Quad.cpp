#include "gfx/Quad.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace choco::gfx {

std::array<Vertex, 4> buildTexturedQuad(RectF src, RectF dst, int texW, int texH, ColorF tint, float rotationDeg,
                                        PointF origin, bool hflip, bool vflip) {
  const float tw = static_cast<float>(texW), th = static_cast<float>(texH);
  const bool whole = src.w <= 0 || src.h <= 0 || (src.x == 0 && src.y == 0 && src.w == tw && src.h == th);
  if (src.w <= 0 || src.h <= 0) src = {0, 0, tw, th};
  // Some shipped sheets list a frame taller/wider than their image (zur_marketkeep's body is 264 rows in a 219-row
  // image). Sampling outside the texture wraps on the GPU, so clip src to the texture and shrink dst to match; the
  // missing part is simply not drawn. The cut edge moves with a mirrored axis, and the rotation origin is kept on the
  // same image point.
  {
    const float sx = dst.w / src.w, sy = dst.h / src.h;
    const float cl = std::max(0.0f, -src.x), ct = std::max(0.0f, -src.y);
    const float cr = std::max(0.0f, src.x + src.w - tw), cb = std::max(0.0f, src.y + src.h - th);
    if (cl > 0 || ct > 0 || cr > 0 || cb > 0) {
      if (cl + cr >= src.w || ct + cb >= src.h) {  // nothing of the frame lies inside the texture
        const Vertex none{{dst.x, dst.y}, {0, 0, 0, 0}, {0, 0}};
        return {none, none, none, none};
      }
      src = {src.x + cl, src.y + ct, src.w - cl - cr, src.h - ct - cb};
      const float dl = (hflip ? cr : cl) * sx, dr = (hflip ? cl : cr) * sx;
      const float dt = (vflip ? cb : ct) * sy, db = (vflip ? ct : cb) * sy;
      dst = {dst.x + dl, dst.y + dt, dst.w - dl - dr, dst.h - dt - db};
      origin = {origin.x - dl, origin.y - dt};
    }
  }
  float x0 = src.x, x1 = src.x + src.w, y0 = src.y, y1 = src.y + src.h;
  if (!whole) {
    if (src.w > 1) {
      x0 += 0.5f;
      x1 -= 0.5f;
    }
    if (src.h > 1) {
      y0 += 0.5f;
      y1 -= 0.5f;
    }
  }
  float u0 = x0 / tw, u1 = x1 / tw, v0 = y0 / th, v1 = y1 / th;
  if (hflip) std::swap(u0, u1);
  if (vflip) std::swap(v0, v1);

  PointF c[4] = {{dst.x, dst.y}, {dst.x + dst.w, dst.y}, {dst.x + dst.w, dst.y + dst.h}, {dst.x, dst.y + dst.h}};
  if (rotationDeg != 0.0f) {
    const float rad = rotationDeg * 3.14159265358979f / 180.0f;
    const float cs = std::cos(rad), sn = std::sin(rad);
    const float px = dst.x + origin.x, py = dst.y + origin.y;
    for (auto& p : c) {
      const float dx = p.x - px, dy = p.y - py;
      p = {px + cs * dx - sn * dy, py + sn * dx + cs * dy};
    }
  }
  return {{{c[0], tint, {u0, v0}}, {c[1], tint, {u1, v0}}, {c[2], tint, {u1, v1}}, {c[3], tint, {u0, v1}}}};
}

}  // namespace choco::gfx
