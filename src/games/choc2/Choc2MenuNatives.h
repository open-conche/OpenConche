#pragma once

#include "script/Bind.h"

namespace choco::script {

// Chocolatier 2's menu windows and their natives: Options (games/choc2/OptionsWindow.h), HighScores with ViewLocal,
// ViewGlobal, NextMode, PreviousMode, NextCategory, PreviousCategory, ScrollUp and ScrollDown
// (games/choc2/HighScoresWindow.h), Credits (games/choc2/CreditsWindow.h), and the submit dialog's GetSubmitInfo,
// SubmitScore and SubmitMedals (offline: nothing is sent). SaveVolumes, ToggleFullScreen and ToggleSoundMute are
// Chocolatier 3's (UserNatives.cpp), which already serve the Chocolatier 2 Options screen. Called from
// registerChoc2Natives after the stub rows, so these replace the placeholder types and stubs of the same names.
void registerChoc2MenuNatives(lua_State* L, ScriptContext& ctx);

}  // namespace choco::script
