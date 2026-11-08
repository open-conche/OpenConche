#pragma once

// The list of game modules (docs/design/engine-and-games.md section 4). Each module is a function-local static.

#include <string>
#include <vector>

#include "script/GameModule.h"

namespace choco::assets {
class AssetFS;
}

namespace choco::games {

// Every module, in detection order: choc3, choc2, choc1.
const std::vector<const script::GameModule*>& modules();

// The module whose profile id is `id` ("choc3", "choc2", "choc1"), or null.
const script::GameModule* findModule(const std::string& id);

// Detects the game from the mounted assets: `forcedId` (non-empty, a valid id) wins; no settings.xml is choc3; otherwise the
// first module that claims settings.xml's <game> wins; if none does, choc3 with a warning. `why` receives the one-line
// reason ("settings.xml game=chocolatier, ui/uihelp.lua").
const script::GameModule& detectGame(const assets::AssetFS& fs, const std::string& forcedId, std::string& why);

}  // namespace choco::games
