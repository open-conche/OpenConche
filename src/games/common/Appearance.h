#pragma once

#include <string>
#include <vector>

#include "games/common/Textures.h"
#include "sdk/Sprite.h"
#include "gfx/Types.h"
#include "ui/Style.h"
#include "games/common/SdkNames.h"

namespace choco::game {

// The shared form of LoadAppearance and BuildItemAppearance (docs/re/chocolate-factory.md 3.4, coffee-factory-and-recipes
// 3): how a product is pictured by the factory games and the ledger.
struct AppearanceLayer {
  std::string image;  // the texture name, e.g. "items/b01_big" or "custom/bar/layer1_01"
  gfx::ColorF tint{1, 1, 1, 1};
};

// `t` is a product table, an `appearance` table or a list. A sub-table `appearance` is read as a list; else a `code` (or
// `name`) gives the single layer "items/<code>_big" in white; else `t` itself is the list. In a list a string entry is
// the layer "custom/<s>" in white and a table entry {name, r, g, b} the layer "custom/<name>" tinted (r, g, b, 1), with
// missing numbers 0.
std::vector<AppearanceLayer> loadAppearance(const ui::Style& t);

// Empties `s`'s children, sets its scale, and for each layer in order adds one child (layer numbers count up from 0): the
// layer tinted, then `<image>_highlight` untinted when that texture exists. Layers whose texture is missing add nothing.
void buildItemAppearance(Sprite& s, const std::vector<AppearanceLayer>& layers, float scale, Textures& textures);

}  // namespace choco::game
