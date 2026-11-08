#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "games/common/sprite/PathSprite.h"
#include "gfx/Texture.h"
#include "gfx/Types.h"
#include "script/LuaState.h"
#include "ui/Style.h"

namespace choco::game {

// What PortWindow reads from the Lua `Port` table (docs/re/port-window.md 1.3), copied into plain structs with the
// original's defaults. No random rolls happen here; the window makes them while it builds the scene.
struct BuildingDef {
  std::string name;
  float x = 0, y = 0;
  int layer = 0;
  std::optional<float> labelX, labelY;
  // Chocolatier 2 (docs/design/choc2-port-and-ledger.md 2.1): the picture's full texture name (empty: Chocolatier 3's
  // `ports/<port>/<name>`), the hover popup's Lua source, and the click closure. All empty in Chocolatier 3's data.
  std::string image;
  std::string contents;
  script::LuaRef command;
  // Chocolatier 2's player logo (`.LOGO`): a picture made in memory instead of loaded by name, drawn at `scale`.
  std::shared_ptr<gfx::Texture> texture;
  float scale = 1;
};

struct SpriteDef {
  double frequency = 100;  // percent chance the sprite exists
  std::vector<std::string> images, masks;
  std::string image, mask;
  int layer = 0;
  bool hasPath = false;
  std::vector<gfx::PointF> path;
  float time = 0;
  PathSprite::Mode mode = PathSprite::Mode::Loop;
  float x = 0, y = 0;
  float scale = 1;  // static sprites: 1; path sprites: -1 (no override)
  std::optional<float> yFar, yNear, scaleFar, scaleNear;
  float speedFar = 1, speedNear = 1;
  bool random = true;
  float htile = 0;
  // Chocolatier 2: the sprite parks at the end of its run for `hold` ms (a guess, see PathSprite::pathPhase), and a sheet on a
  // path sprite is a walk cycle played by its own script instead of heading frames.
  float hold = 0;
  bool headingFrames = true;
  // Chocolatier 2's path arithmetic (PathSprite::choc2Timing), its `hflip` key, and its perspective rule (an explicit nearscale or
  // farscale wins over `scale`, which is only their default).
  bool choc2 = false;
  bool hflip = false;
  bool faceLeft = false;  // the art faces left: mirror it while it moves right (Chocolatier 2)
  gfx::ColorF tint{1, 1, 1, 1};
  std::vector<gfx::ColorF> tints;
};

struct PortDef {
  std::string name;
  std::vector<BuildingDef> buildings;
  std::vector<SpriteDef> sprites;
  float yFar = 0, yNear = 800, scaleFar = 1, scaleNear = 1;
};

// Reads a Port table. Unknown motion strings mean a loop; missing arrays mean none.
PortDef parsePort(const ui::Style& port);

}  // namespace choco::game
