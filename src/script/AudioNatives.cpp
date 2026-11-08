#include "script/AudioBridge.h"

#include "audio/Audio.h"
#include "audio/Prefs.h"
#include "sdk/Users.h"
#include "script/ScriptHost.h"

namespace choco::script {

void soundEvent(ScriptContext& ctx, const std::string& name) {
  if (ctx.audio) {
    ctx.audio->event(name);
    return;
  }
  if (ctx.stubs.hit("SoundEvent." + name)) ctx.log.write("[unimplemented] SoundEvent " + name + " (no audio service)");
}

void applyAudioPrefs(ScriptContext& ctx) {
  if (!ctx.audio) return;
  audio::Levels l;
  if (ctx.users) l = audio::levelsFromPrefs([&](const char* key) { return ctx.users->pref(key, ""); });
  ctx.audio->setLevels(l.sfx, l.music, l.env, l.muted);
}

namespace {

// Never errors and returns nothing; an unknown name is silent.
void nativeSoundEvent(ScriptContext& ctx, std::string name) { soundEvent(ctx, name); }

}  // namespace

void registerAudioNatives(lua_State* L, ScriptContext& ctx) { bind<nativeSoundEvent>(L, ctx, "SoundEvent"); }

}  // namespace choco::script
