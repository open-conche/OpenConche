#pragma once

#include <string_view>

#include "gfx/SpriteSheet.h"

namespace choco::gfx {

// Parses a Diner Dash 2 `.anm` sheet (the `ANIM` container
// TAnimatedTexture::ParseFromANM reads, i386 0x5c70a) into SheetData: the same
// data the sequence XML produces, plus the frame anchors and the sheet-level
// registration point the .anm format carries.
//
// Layout (all little-endian; every string is u32 length + bytes):
//   "ANIM" u32(version=1) u32(count, unused) str str          - build paths
//   "IMGN" 8.u32: regX regY w h xform[4] (float bits, the sentinel -10000000.0
//   in every shipped file) then str str
//           - the image name (a PNG next to the .anm) and the mask name (empty
//           in every shipped file)
//   "FRMS" u32(nframes), then per frame: 12.u32 regX regY srcX srcY w h
//   xform[6] (sentinels) str(name) u32(nanchors)
//           and nanchors x (u32 x, u32 y, str name)
//   "SCRP" str - the Lua animation script; a missing SCRP gets the default
//   script, as SetDefaultScript does
// The xform floats are never real in the shipped files (1903 frames checked);
// they are skipped, logged in warnings.
class AnmSheet {
public:
  static bool parse(std::string_view bytes, SheetData &out, std::string &error);
};

} // namespace choco::gfx
