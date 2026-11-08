#pragma once

// Chocolatier 1's audio natives (docs/re/choc1-natives.md section 6): StartMusic, QueueMusic, SetAmbient{name, loop} and
// PlaySound. The queue state machine is games/choc1/Choc1Music, ticked by the Choc1World.

#include <string>
#include <vector>

#include "audio/SoundCache.h"
#include "script/Bind.h"

namespace choco::script {

// The file PlaySound(name) plays: the nine preloaded aliases (open close charopen charclose boat train airship fanfare
// mcfanfare) map to their files under audio/sfx/, any other name is audio/sfx/<name>.ogg. "" gives "".
std::string choc1SoundPath(const std::string& name);

// PlaySound's work, for the other natives (the money counter's money_earned). No audio service: nothing.
void choc1PlaySound(ScriptContext& ctx, const std::string& name);

// The sounds the app decodes ahead in windowed runs: the nine aliases plus money_earned, ui_click and click_2.
std::vector<audio::SoundCache::Request> choc1Prefetch();

void registerChoc1AudioNatives(lua_State* L, ScriptContext& ctx);

}  // namespace choco::script
