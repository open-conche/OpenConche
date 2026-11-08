#pragma once

#include <functional>
#include <string>
#include <vector>

#include "games/common/PortDef.h"
#include "games/choc2/LogoSpec.h"
#include "ui/Style.h"
#include "games/common/SdkNames.h"

namespace choco::game::choc2 {

// A sprite table passed to Chocolatier 2's bsgAddSprite, turned into what PortWindow builds from
// (docs/design/choc2-port-and-ledger.md 2.1, docs/re/choc2-port-and-ledger.md 1.3). A pure function: no window, no Lua state.
struct AddedSprite {
  enum class Kind { Sprite, Building, Skipped } kind = Kind::Skipped;
  BuildingDef building;
  SpriteDef sprite;
  // `.LOGO`: the player's logo, set on a Building whose picture the caller renders (Port::addSprite) and puts in
  // building.texture. building.scale is the table's `scale`.
  bool isLogo = false;
  LogoSpec logo;
  // Log lines for guesses and skips; `noteKey` is what the caller de-duplicates them on (the image).
  std::vector<std::string> notes;
  std::string noteKey;
};

// The width in pixels of an image (a texture, or the first frame of an `.xml` sheet); 0 when unknown.
using TextureInfo = std::function<int(const std::string& image)>;

// FACT (docs/re/choc2-port-binary.md 1.2): a path sprite without `time` runs for 10000 ms.
constexpr float kDefaultPathMs = 10000;

// `order` is the call index: the draw order, so a later sprite is above an earlier one (and wins a hit test). A table with
// `command` or `contents` is a building. `.LOGO` (the player logo) is a building with `isLogo` set and its LogoSpec read from
// the `logo` table; the caller renders it. A `.LOGO` without a `logo` table is skipped with a note.
AddedSprite fromAddSprite(const ui::Style& t, int order, const TextureInfo& textureInfo);

}  // namespace choco::game::choc2
