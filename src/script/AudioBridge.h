#pragma once

#include <string>

#include "script/ScriptContext.h"

namespace choco::script {

// The one entry point every call site uses for SoundEvent(name) (docs/design/audio.md s.3). Plays through ctx.audio; with
// none (unit tests) it keeps the old logged stub: the "SoundEvent.<name>" counter and a one-line "[unimplemented]" note.
void soundEvent(ScriptContext& ctx, const std::string& name);

// Reads the current user's soundlevel, musiclevel, ambientlevel and soundoff prefs (defaults when unset or no user) and
// applies them to ctx.audio. No audio, no effect.
void applyAudioPrefs(ScriptContext& ctx);

// The SoundEvent native.
void registerAudioNatives(lua_State* L, ScriptContext& ctx);

}  // namespace choco::script
