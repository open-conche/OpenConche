#pragma once

#include <string>

#include "script/Bind.h"

namespace choco::script {

// Chocolatier 2's PlaySound(name) and LoadSound(name)
// (docs/re/choc2-logo-and-trade.md section 2.7).
//
// Name resolution. Chocolatier 1's PlaySound loads `audio/sfx/<name>.ogg` and
// plays it once (docs/re/choc1-natives.md section 6); Chocolatier 2 keeps its
// sounds in `sfx/` at the root of its asset tree and passes bare names ("buy",
// "haggle_price_up", "ui_click"), plus one literal path ("sfx/lab_select.ogg").
// So: a name with a '/' is a path used as given (".ogg" appended when it has no
// extension); a bare name is `sfx/<name>.ogg`. A miss is silent (the sound
// cache logs it once).
std::string choc2SoundPath(const std::string& name);

// PlaySound: one-shot on the direct group (the Audio service's playFile, which
// also counts it for the dump). LoadSound: decodes the file into the sound
// cache so the first PlaySound does not; makes no sound. Without an audio
// service PlaySound only logs once.
//
// Music and ambience (docs/re/choc2-audio.md): SetMusicState(state[, file]),
// SetAmbient(path), PauseMusic() and ResumeMusic() forward to the Audio
// service (setAmbientFile, setMusicPaused); SetMusicState goes to the
// session's Choc2Music (the state machine, ticked through afterAudio).
void registerChoc2AudioNatives(lua_State* L, ScriptContext& ctx);

}  // namespace choco::script
