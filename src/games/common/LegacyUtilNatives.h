#pragma once

// Natives that Chocolatier 2 (bsg*) and Chocolatier 1 (Util*) share with the same behaviour (docs/re/choc1-fit.md 1.5). Each game
// binds them under its own name.

#include <string>

#include "script/ScriptContext.h"

namespace choco::script {

// Local time as "%Y.%m.%d-%H.%M.%S" (bsgTimeStamp, UtilTimeStamp).
std::string legacyTimeStamp(ScriptContext&);

// The dev-only debug-save iterator (dev/devfile.lua) lists *.choco2 files of the `user:` directory and returns "" when exhausted
// (bsgNextSaveFileName, UtilNextSaveFile). This engine keeps no such files (saves live in the profile): always "".
std::string legacyNextSaveFile(ScriptContext&);

// SetCurrentGameMode: Chocolatier 3's stores only mode 0; here any mode is stored as the "gamemode" pref (Free Play calls it with
// 1). The original (Chocolatier 1 0041c190) accepts 0 or 1 and also tells its host the mode; that part is not modelled.
void legacySetCurrentGameMode(ScriptContext&, int mode);

}  // namespace choco::script
