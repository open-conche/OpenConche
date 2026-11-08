#pragma once

#include <map>
#include <string>
#include <vector>

#include "app/Args.h"

namespace choco::platform {
class Window;
}
namespace choco::gfx {
class Renderer;
class TextureUploader;
}

namespace choco::app {

// One labelled choice of the finder window (docs/design/game-finder.md). The order is the card order,
// top to bottom; the ids are what --pick-game takes and what finder.txt stores.
struct FinderVariant {
  const char* id;         // "choc1" | "choc2" | "choc3" | "choc3-reforged"
  const char* moduleId;   // the registry module that must claim the tree
  const char* title;      // the error messages' name of the game
  const char* label;      // the plate label (built-in font: no parentheses, hence "Chocolatier 3 - Reforged")
  bool reforged;          // choc3: the tree must (true) or must not (false) hold community/account.lua
};
extern const FinderVariant kFinderVariants[];
extern const int kFinderVariantCount;
const FinderVariant* findFinderVariant(const std::string& id);

// The engine's game library (OpenConche/games.cfg, docs/design/game-finder.md section 3): every game the finder
// has validated plus the one to boot. Dotted keys per variant (dir = the folder the picker returned, assets = the
// tree resolved from it) and one `last` key with the variant to boot on the next launch.
struct FinderGameEntry {
  std::string dir;     // what the OS folder dialog returned
  std::string assets;  // the assets tree resolved from dir
};
struct GameLibrary {
  std::string last;  // the variant to boot ("" = none picked yet)
  std::map<std::string, FinderGameEntry> entries;
  std::string path;  // the config file ("" until loadGameLibrary ran)
};

// games.cfg, key = value lines; an unparsable or missing file loads as an empty library (the finder is shown).
// A ChocolatierEngine/finder.txt from before the rename is migrated into games.cfg when the new file is absent.
bool loadGameLibrary(GameLibrary& out, std::string& error);
bool saveGameLibrary(const GameLibrary& library, std::string& error);

// The candidate assets roots a user's picked folder may hold, best first (design section 2). Only directories that
// exist are returned; `picked` itself comes last, so an install root beats an assets folder picked directly.
std::vector<std::string> resolveAssetCandidates(const std::string& picked);

// Mounts `assetsDir` as a throwaway AssetFS and asks the registry's own detectGame what it is. Returns "" when the
// tree is exactly `variant` (module id, the choc3 community marker, and the module's mainloop script present);
// otherwise a one-line reason for the message box / the [finder] log line.
std::string validateAssetsForVariant(const FinderVariant& variant, const std::string& assetsDir);

// The finder without the screen: --finder --pick-game <variant> --pick-dir <folder> (design section 4). Validates
// the candidates in order and logs the outcome (diag lines, including "[finder] variant %s, assets %s"). Returns
// 0 on success (o.assetsDir and o.game are then set, and the boot continues), 3 when the folder is missing,
// unreadable or holds nothing playable for the variant. (An unknown variant is a usage error, exit 2, in App.)
int finderPick(Options& o, const FinderVariant& variant, const std::string& picked);

// Registers the game held in `folder` (any folder resolveAssetCandidates understands) in `library` and saves it:
// the first variant that validates over the candidates names the game. `last` is untouched. This is what the
// finder's ADD GAME button and --add-game share.
enum class AddGameStatus { Added, AlreadyListed, NotFound, SaveFailed };
struct AddGameResult {
  AddGameStatus status = AddGameStatus::NotFound;
  std::string id;      // the variant (Added, AlreadyListed, SaveFailed)
  std::string assets;  // its assets folder
  std::string error;   // SaveFailed: why
};
AddGameResult addGameFromFolder(GameLibrary& library, const std::string& folder);

// Boots a variant straight from its library entry, re-resolving `dir` and falling back to the saved `assets`
// (a moved install). True and o filled when the entry is playable, false (with `why`) when the game must be
// re-picked. The unit-testable heart of both savedFinderChoice and the switcher's click-to-boot button.
bool bootFromLibrary(const FinderVariant& variant, const GameLibrary& library, Options& o, std::string& why);

// The saved choice readied for boot: the library's `last` entry, via bootFromLibrary. Returns false (with the
// reason logged) when there is no valid saved choice.
bool savedFinderChoice(Options& o);

// Running inside a booted game (the in-game switcher, Ctrl+Shift+G): the finder draws on the existing window and
// renderer instead of making its own, so the game's window keeps its place and the game's renderer keeps its
// flush hook (a second SdlRenderer would steal it). Empty = the standalone first-launch screen.
struct FinderHost {
  platform::Window* window = nullptr;
  gfx::Renderer* renderer = nullptr;
  gfx::TextureUploader* uploader = nullptr;
};

// The finder window's answer: a chosen game (o.assetsDir and o.game set, every other option untouched), the
// screen closed without a choice, the Exit button, or Failed: the screen could not run at all (an automated run,
// no SDL, no window) - `why` says so, and the App exits 1 rather than treating it as a quiet Exit.
enum class FinderOutcome { Picked, Cancelled, Exit, Failed };

// The finder window (design section 2): PAINTED entirely at runtime - no embedded PNG, no committed or
// generated data file (the binary ships alone; the launcher appears before any game asset exists) - a solid
// backdrop, one CARD per INSTALLED game (only registered games, no placeholders: the name the card's primary
// element, the configured directory small and dim beneath it - elided to the box, the full path in the
// footer while hovered - a PLAY button, and a small X that unregisters the game: a games.cfg erase, never a
// disk change), the one global ADD GAME button under the list (the OS folder picker plus the EXISTING
// detection: the first variant that validates over the picked folder's candidates names the game - the user
// never has to know which game they are picking; adding registers it and does NOT launch, `last` untouched),
// and the information-only SUPPORTED GAMES line. Every word is drawn live in the engine's built-in font
// (sizes and colours only), and the controls are the finder's own rects, drawn, hovered and hit-tested by
// the same numbers. A card's Play click boots the game through bootFromLibrary when its entry validates
// (and `last` remembers it for the automatic launch); a stale entry falls back to that game's re-locate
// folder dialog. The contextual footer explains the launch behaviour ("Select a game to get started..." /
// "[game] will launch automatically next time...", re-validated), and doubles as the path tooltip. The
// Exit button (top right) says "Exit" (standalone: the process ends; closing the window does the same) or
// "Back to game" (the in-game switcher: it just closes). Closing without a choice is Exit/Cancelled: the
// standalone launcher ends the process on either, in game the switcher just closes. The --pick path
// (finderPick) never opens this window.
FinderOutcome runGameFinder(Options& o, std::string& why, const FinderHost& host = {});

}  // namespace choco::app
