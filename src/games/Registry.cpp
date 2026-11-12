#include "games/Registry.h"

#include "assets/AssetFS.h"
#include "assets/Content.h"
#include "games/choc3/Choc3Module.h"
#ifdef CHOCO_GAME_CHOC2
#include "games/choc2/Choc2Module.h"
#endif
#ifdef CHOCO_GAME_CHOC1
#include "games/choc1/Choc1Module.h"
#endif
#ifdef CHOCO_GAME_GREATCC
#include "games/greatcc/GreatCCModule.h"
#endif

namespace choco::games {

const std::vector<const script::GameModule *> &modules() {
  static const std::vector<const script::GameModule *> list = {
      &choc3::module(),
#ifdef CHOCO_GAME_CHOC2
      &choc2::module(),
#endif
#ifdef CHOCO_GAME_CHOC1
      &choc1::module(),
#endif
#ifdef CHOCO_GAME_GREATCC
      &greatcc::module(),
#endif
  };
  return list;
}

const script::GameModule *findModule(const std::string &id) {
  for (const script::GameModule *m : modules())
    if (id == m->profile().id)
      return m;
  return nullptr;
}

const script::GameModule &detectGame(const assets::AssetFS &fs,
                                     const std::string &forcedId,
                                     std::string &why) {
  if (const script::GameModule *forced =
          forcedId.empty() ? nullptr : findModule(forcedId)) {
    why = std::string("--game ") + forced->profile().id;
    return *forced;
  }
  if (!fs.exists("::assets/settings.xml")) {
    // No settings.xml: a game whose tree claims itself by files alone wins. A settings-less SDK-3.x tree has no game
    // value to match (an SDK-3.4 tree has no settings.xml at all), while every Chocolatier
    // claim needs a settings game value, so this pass is inert for the shipped modules and choc3 stays the default.
    for (const script::GameModule *m : modules())
      if (m->claims(fs, "", why))
        return *m;
    why = "no settings.xml";
    return choc3::module();
  }
  std::string game;
  for (const auto &kv : assets::loadSettings(fs))
    if (kv.first == "game")
      game = kv.second;
  for (const script::GameModule *m : modules())
    if (m->claims(fs, game, why))
      return *m;
  why = "WARNING: settings.xml game='" + game +
        "' is not recognised, assuming Chocolatier 3 (use --game to override)";
  return choc3::module();
}

} // namespace choco::games
