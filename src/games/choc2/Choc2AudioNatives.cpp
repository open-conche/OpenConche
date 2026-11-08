#include "games/choc2/Choc2AudioNatives.h"

#include <optional>

#include "audio/Audio.h"
#include "games/choc2/Session.h"
#include "script/ScriptHost.h"

namespace choco::script {

std::string choc2SoundPath(const std::string& name) {
  if (name.empty()) return {};
  const bool hasSlash = name.find('/') != std::string::npos;
  const std::size_t dot = name.find_last_of('.');
  const bool hasExt = dot != std::string::npos && (!hasSlash || dot > name.find_last_of('/'));
  if (hasSlash) return hasExt ? name : name + ".ogg";
  return "sfx/" + name + (hasExt ? "" : ".ogg");
}

namespace {

void nativePlaySound(ScriptContext& ctx, std::string name) {
  const std::string path = choc2SoundPath(name);
  if (path.empty()) return;
  if (!ctx.audio) {
    if (ctx.warned.insert("choc2-PlaySound-noaudio").second) ctx.log.write("[choc2] PlaySound: no audio service, silent");
    return;
  }
  ctx.audio->playFile(path);
}

void nativeLoadSound(ScriptContext& ctx, std::string name) {
  const std::string path = choc2SoundPath(name);
  if (path.empty() || !ctx.audio) return;
  ctx.audio->cache().pcm(path);
}

// Music and ambience (docs/re/choc2-audio.md). Without an audio service the calls only log once.
bool haveAudio(ScriptContext& ctx, const char* what) {
  if (ctx.audio) return true;
  if (ctx.warned.insert(std::string("choc2-") + what + "-noaudio").second) ctx.log.write(std::string("[choc2] ") + what + ": no audio service, silent");
  return false;
}

void nativeSetMusicState(ScriptContext& ctx, std::string state, std::optional<std::string> file) {
  if (!haveAudio(ctx, "SetMusicState")) return;
  auto* world = dynamic_cast<game::choc2::Choc2World*>(ctx.gameState.get());
  if (!world) {
    if (ctx.warned.insert("choc2-SetMusicState-noworld").second) ctx.log.write("[choc2] SetMusicState: no Chocolatier 2 world, silent");
    return;
  }
  world->music.setState(*ctx.audio, state, file.value_or(""));
}

void nativeSetAmbient(ScriptContext& ctx, std::string path) {
  if (haveAudio(ctx, "SetAmbient")) ctx.audio->setAmbientFile(path);
}

void nativePauseMusic(ScriptContext& ctx) {
  if (haveAudio(ctx, "PauseMusic")) ctx.audio->setMusicPaused(true);
}

void nativeResumeMusic(ScriptContext& ctx) {
  if (haveAudio(ctx, "ResumeMusic")) ctx.audio->setMusicPaused(false);
}

}  // namespace

void registerChoc2AudioNatives(lua_State* L, ScriptContext& ctx) {
  bind<nativePlaySound>(L, ctx, "PlaySound");
  bind<nativeLoadSound>(L, ctx, "LoadSound");
  bind<nativeSetMusicState>(L, ctx, "SetMusicState");
  bind<nativeSetAmbient>(L, ctx, "SetAmbient");
  bind<nativePauseMusic>(L, ctx, "PauseMusic");
  bind<nativeResumeMusic>(L, ctx, "ResumeMusic");
}

}  // namespace choco::script
