#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "games/choc2/LogoSpec.h"
#include "gfx/Image.h"
#include "gfx/TextureCache.h"
#include "gfx/Types.h"
#include "gfx/text/TextLayout.h"

namespace choco::game::choc2 {

// Draws a LogoSpec into a 350x200 canvas on the CPU (docs/re/choc2-logo-and-trade.md 1.6): the background, the graphic, then the
// text with a one pixel drop shadow (Chocolatier 1's TLogo draw order, docs/re/choc1-natives.md section 8). The result is
// RGBA pixels, so the editor's preview and the port sign share it and the texture can be re-uploaded after a device reset.

// What the renderer reads from outside. `image` returns the decoded pixels of an asset name (a background, a graphic) or null
// when it is missing; the pointer stays valid for the call. `fonts` resolves "fonts/<name>.mvec".
struct LogoAssets {
  std::function<const gfx::ImageData*(const std::string& name)> image;
  gfx::text::FontCache* fonts = nullptr;
};

// Decoded background and graphic pictures by asset name, loaded through the texture cache's name resolution (the editor keeps
// one while it is open, the port sign makes one per build). A missing picture is remembered and logged once by the cache.
class LogoImages {
 public:
  explicit LogoImages(gfx::TextureCache* textures) : textures_(textures) {}
  const gfx::ImageData* get(const std::string& name);
  LogoAssets assets(gfx::text::FontCache* fonts);

 private:
  gfx::TextureCache* textures_;
  std::map<std::string, std::unique_ptr<gfx::ImageData>> images_;  // null: missing
};

// Where the graphic lands on the canvas. INFERRED (a recorded decision): the drawn size is the picture's size times `ls`
// (a stock graphic is 200x200, an item picture 64x64); p = lx / 1000 places its left edge at p x (350 - width), the same for
// y; it turns about its centre by `lr` / 1000 of a turn and is drawn with alpha `la` / 1000. The slots the Chocolatier 2
// layout buttons use (p = 0, 0.5, 1) are Chocolatier 1's fixed x = 0, 75, 150 for a 200 wide graphic.
struct GraphicPlacement {
  float x = 0, y = 0, w = 0, h = 0;  // the unrotated rectangle's top-left and size
  float rotationDeg = 0;             // clockwise
  float alpha = 1;
};
GraphicPlacement graphicPlacement(const LogoSpec& spec, int sourceW, int sourceH);

// The text: laid out with the text engine into a 330 wide box (Chocolatier 1's textW), then its tight block placed by the same
// free-space rule: left = tx / 1000 x (350 - block width), top = ty / 1000 x (200 - block height). `to` justifies the lines
// within the block (INFERRED). Bold, italic and outline are the engine's markup (`<b>`, `<i>`, `<outline size=2>`; the outline
// is black, a guess), the colour is the HSV triple at alpha `ta`.
struct TextBlock {
  gfx::text::TextLayout layout;
  gfx::PointF origin;  // the layout window's top-left on the canvas
  gfx::RectI bounds;   // the tight block on the canvas
};
TextBlock layoutLogoText(const LogoSpec& spec, gfx::text::FontCache* fonts);
gfx::ColorF logoTextColor(const LogoSpec& spec);

constexpr int kLogoTextBoxW = 330;
constexpr int kLogoOutlinePx = 2;

gfx::ImageData renderLogo(const LogoSpec& spec, const LogoAssets& assets);

// The fonts of the installed text system (null before one is installed).
gfx::text::FontCache* logoFonts();

}  // namespace choco::game::choc2
