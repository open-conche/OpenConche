#pragma once

#include <vector>

#include "gfx/Image.h"

namespace choco::game::choc2 {

// The Chocolatier 2 map's colour picture (TMap::UpdateColorMap, C2 0x23248; docs/re/choc2-map-binary.md 1.1). FACT: the
// texture is image/map_color's pixels and the visited ports' ports/map_mask_<port> are drawn into its ALPHA channel only.
// INFERENCE (a recorded decision): the original rewrites alpha only when the JPEG texture reports an alpha
// channel; the engine always does, so the visited regions show the colour picture and the rest is transparent. The blend of
// overlapping masks is UNKNOWN: this uses the maximum alpha (the union).
//
// `colour` is changed in place: its RGB stays, its alpha becomes 0 everywhere and then the maximum of the masks' alpha.
// A mask of another size is clipped to the overlap with the top-left corner aligned (the shipped masks are all 800 x 600).
void composeColorMask(gfx::ImageData& colour, const std::vector<const gfx::ImageData*>& masks);

}  // namespace choco::game::choc2
