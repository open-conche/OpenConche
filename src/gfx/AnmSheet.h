#pragma once

#include <string_view>

#include "gfx/SpriteSheet.h"

namespace choco::gfx {

// Parses an `.anm` sheet (the `ANIM` container TAnimatedTexture::ParseFromANM
// reads, i386 0x5c70a) into SheetData: the same
// data the sequence XML produces, plus the frame anchors and the sheet-level
// registration point the .anm format carries.
//
// Two layouts, told apart by the header's second u32: 2 is version 2, anything
// else is the version 1 layout of Diner Dash 2.
//
// Version 1 layout (all little-endian; every string is u32 length + bytes):
//   "ANIM" u32(1) u32(format, 1 here) str str          - build paths
//   "IMGN" 8.u32: regX regY w h xform[4] (float bits, the sentinel -10000000.0
//   in every shipped file) then str str
//           - the image name (a PNG next to the .anm) and the mask name (empty
//           in every shipped file)
//   "FRMS" u32(nframes), then per frame: 12.u32 regX regY srcX srcY w h
//   xform[6] (sentinels) str(name) u32(nanchors)
//           and nanchors x (u32 x, u32 y, str name)
//   "SCRP" str - the Lua animation script; a missing SCRP gets the default
//   script, as SetDefaultScript does
//
// Version 2 differs in three places: the second header u32 is 2; IMGN has only
// 4 u32 before its strings, and they are never read (no sheet-level
// registration); each frame has 13 numeric words, regX regY srcX srcY w h then
// `scale` (an opacity multiplier, stored as SheetFrame::alpha) and xform[6]
// (a row-vector 3x3, SheetFrame::xform; word 0 = -10000000.0 means none).
//
// The version 1 xform floats are never real in the shipped files (1903 frames checked);
// they are skipped, logged in warnings.
class AnmSheet {
public:
  static bool parse(std::string_view bytes, SheetData &out, std::string &error);
};

} // namespace choco::gfx
