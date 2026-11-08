#include "games/choc1/Choc1AudioNatives.h"

#include "audio/Audio.h"
#include "games/choc1/Choc1World.h"
#include "script/LuaStyle.h"
#include "script/ScriptHost.h"

namespace choco::script {

namespace {

struct SoundAlias {
  const char* name;
  const char* file;
};
// The sounds TChocoSound preloads (exe 00406340); the file follows the load order and the strings at 004e691c.
const SoundAlias kAliases[] = {
    {"open", "window_open"},       {"close", "window_close"}, {"charopen", "character_dlog_open"},
    {"charclose", "character_dlog_close"}, {"boat", "ship"},   {"train", "train"},
    {"airship", "airship"},        {"fanfare", "medal_fanfare"}, {"mcfanfare", "win_game_fanfare"},
};

bool haveAudio(ScriptContext& ctx, const char* what) {
  if (ctx.audio) return true;
  if (ctx.warned.insert(std::string("choc1-") + what + "-noaudio").second)
    ctx.log.write(std::string("[choc1] ") + what + ": no audio service, silent");
  return false;
}

void nativePlaySound(ScriptContext& ctx, std::string name) { choc1PlaySound(ctx, name); }

// The music natives and SetAmbient need the Chocolatier 1 world (the queue lives there).
game::choc1::Choc1World* worldFor(ScriptContext& ctx, const char* what) {
  if (!haveAudio(ctx, what)) return nullptr;
  auto* world = game::choc1::choc1WorldOf(ctx);
  if (!world && ctx.warned.insert(std::string("choc1-") + what + "-noworld").second)
    ctx.log.write(std::string("[choc1] ") + what + ": no Chocolatier 1 world, silent");
  return world;
}

void nativeStartMusic(ScriptContext& ctx, std::string file) {
  if (auto* world = worldFor(ctx, "StartMusic")) {
    game::choc1::AudioMusicSink sink(*ctx.audio);
    world->music.start(sink, file, world->nowMs());
  }
}

void nativeQueueMusic(ScriptContext& ctx, std::string file, double seconds) {
  if (auto* world = worldFor(ctx, "QueueMusic")) {
    game::choc1::AudioMusicSink sink(*ctx.audio);
    world->music.queue(sink, file, seconds, world->nowMs());
  }
}

// SetAmbient{name=, loop=} (exe 00404af0): an empty or missing name stops the ambient; the name already current changes
// nothing; otherwise the name is stored and, with a `loop`, audio/sfx/<loop>.ogg loops in the ambient group (an empty loop stops).
void nativeSetAmbient(ScriptContext& ctx, TableArg t) {
  auto* world = worldFor(ctx, "SetAmbient");
  if (!world) return;
  std::string name, loop;
  if (t.valid()) {
    LuaStyle style = LuaStyle::plainView(t.L, t.index, &ctx.lua);
    name = style.stringOr("name");
    loop = style.stringOr("loop");
  }
  if (name.empty()) {
    world->ambient.clear();
    ctx.audio->setAmbientFile("");
    return;
  }
  if (name == world->ambient) return;
  world->ambient = name;
  ctx.audio->setAmbientFile(loop.empty() ? std::string() : "audio/sfx/" + loop + ".ogg");
}

}  // namespace

std::string choc1SoundPath(const std::string& name) {
  if (name.empty()) return {};
  for (const SoundAlias& a : kAliases)
    if (name == a.name) return std::string("audio/sfx/") + a.file + ".ogg";
  return "audio/sfx/" + name + ".ogg";
}

void choc1PlaySound(ScriptContext& ctx, const std::string& name) {
  const std::string path = choc1SoundPath(name);
  if (path.empty() || !haveAudio(ctx, "PlaySound")) return;
  ctx.audio->playFile(path);
}

std::vector<audio::SoundCache::Request> choc1Prefetch() {
  std::vector<audio::SoundCache::Request> list;
  for (const SoundAlias& a : kAliases) list.push_back({std::string("audio/sfx/") + a.file + ".ogg", {}});
  for (const char* n : {"money_earned", "ui_click", "click_2"}) list.push_back({std::string("audio/sfx/") + n + ".ogg", {}});
  return list;
}

void registerChoc1AudioNatives(lua_State* L, ScriptContext& ctx) {
  bind<nativePlaySound>(L, ctx, "PlaySound");
  bind<nativeStartMusic>(L, ctx, "StartMusic");
  bind<nativeQueueMusic>(L, ctx, "QueueMusic");
  bind<nativeSetAmbient>(L, ctx, "SetAmbient");
}

}  // namespace choco::script
