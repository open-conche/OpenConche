#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gfx/Texture.h"
#include "gfx/Types.h"

namespace choco::gfx {

struct SheetFrame {
  RectI src;  // the frame's rectangle inside the sheet image
  PointI reg; // registration point: the frame-local pixel that lands on the
              // sprite's position
  // Named anchor points the sheet carries per frame (.anm only:
  // TAnimatedTexture::GetAnchorPoint names such as "chair1", "hand1",
  // "sit_offset", in frame pixels; y is negative upwards). Empty for XML
  // sheets.
  struct Anchor {
    std::string name;
    PointI pos;
  };
  std::vector<Anchor> anchors;
  // .anm version 2 only. `alpha` multiplies the draw alpha of this frame (the file's per-frame "scale" word is an opacity,
  // not a size). `xform`, when present, is a row-vector 3x3 (x0 x1 0 / x2 x3 0 / x4 x5 1) applied to the frame quad,
  // relative to the registration point, before the sprite's own transform. Bounding boxes and hit tests ignore both.
  float alpha = 1;
  bool hasXform = false;
  float xform[6] = {1, 0, 0, 1, 0, 0};
};

// What the sequence XML says, before any image is loaded
// (docs/re/port-window.md 2.3).
struct SheetData {
  std::string image; // <img name=...>, relative to the XML's folder
  std::string mask;  // <img mask=...>, likewise; empty if none
  PointI reg{0, 0}; // the sheet-level registration point (the .anm IMGN header;
                    // 0,0 for XML sheets), frame-relative
  std::vector<SheetFrame> frames;
  std::string script; // the sheet's Lua text; the generated default when the
                      // XML has none
  std::vector<std::string> warnings; // attributes present that are read but not
                                     // supported (xform, anchor, ...)
};

// A parsed sprite sheet: one image, its frames, and the script that animates
// it.
class SpriteSheet {
public:
  // Parses the XML text. False with `error` set when there is no sequence, img
  // or framelist node or no frame.
  static bool parse(std::string_view xml, SheetData &out, std::string &error);
  // The script of a sheet that has none: plays frames 1..n once at 60 ms each.
  static std::string defaultScript(std::size_t frameCount);

  std::shared_ptr<Texture> image;
  std::vector<SheetFrame> frames;
  std::string script;
  std::string path; // the XML's resolved asset path, for logs
  PointI reg{0, 0}; // the sheet-level registration point (.anm IMGN header; 0,0
                    // for XML sheets)
};

} // namespace choco::gfx
