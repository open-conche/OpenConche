#pragma once

#include <array>

#include "gfx/Types.h"

namespace choco::gfx {

// The four vertices (top-left, top-right, bottom-right, bottom-left of dst before rotation) of a textured quad, for
// the index list {0,1,2, 0,2,3}.
//
//  - src is in texels of a texW x texH texture; w <= 0 or h <= 0 means the whole texture.
//  - When src is a proper sub-rectangle, its UVs are inset by half a texel on every side so bilinear filtering on a
//    GPU never reads the neighbouring texels (atlas bleed). The whole texture is not inset. A side of one texel or
//    less is not inset.
//  - hflip/vflip mirror the image inside dst, then the corners rotate clockwise by rotationDeg about dst.xy + origin.
std::array<Vertex, 4> buildTexturedQuad(RectF src, RectF dst, int texW, int texH, ColorF tint, float rotationDeg,
                                        PointF origin, bool hflip, bool vflip);

}  // namespace choco::gfx
