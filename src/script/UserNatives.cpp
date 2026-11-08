#include <string>

#include "audio/Audio.h"
#include "audio/Prefs.h"
#include "sdk/Users.h"
#include "script/AudioBridge.h"
#include "script/GameModule.h"
#include "script/ScriptHost.h"
#include "ui/VolumeSliders.h"
#include "ui/WindowManager.h"
#include "ui/widgets/InputWidgets.h"

namespace choco::script {

namespace {

sdk::UserStore& store(ScriptContext& ctx) { return *ctx.users; }

// ---- users (TSettings): 0-based indices, -1 for none ----

double nativeGetNumUsers(ScriptContext& ctx) { return store(ctx).count(); }
double nativeGetCurrentUser(ScriptContext& ctx) { return store(ctx).current(); }
std::string nativeGetCurrentUserName(ScriptContext& ctx) { return store(ctx).currentName(); }
std::string nativeGetUserName(ScriptContext& ctx, int index) { return store(ctx).name(index); }
bool nativeIsNameInUse(ScriptContext& ctx, std::string name) { return store(ctx).nameInUse(name); }

void nativeCreateNewUser(ScriptContext& ctx, std::string name) {
  if (store(ctx).create(name) < 0) ctx.log.write("[user] CreateNewUser: rejected the name (empty, too long, bad UTF-8 or control characters)");
  applyAudioPrefs(ctx);  // a new user starts from the previous user's sound prefs
}

// Ignored for an index >= the user count (the original compares unsigned; -1 is allowed).
void nativeSetCurrentUser(ScriptContext& ctx, int index) {
  store(ctx).setCurrent(index);
  applyAudioPrefs(ctx);  // the user's sound levels and mute apply when they are selected
}

// The current user, or user 0 when none is selected (the original defaults the index to 0).
void nativeChangeCurrentUserName(ScriptContext& ctx, std::string name) {
  sdk::UserStore& s = store(ctx);
  const int index = s.current() >= 0 ? s.current() : 0;
  if (!s.rename(index, name)) ctx.log.write("[user] ChangeCurrentUserName: nothing renamed");
}

// An out-of-range index deletes nothing but still runs SetCurrentUser(index - 1), as the original does.
void nativeDeleteUser(ScriptContext& ctx, int index) {
  if (!store(ctx).remove(index)) store(ctx).setCurrent(index - 1);
  applyAudioPrefs(ctx);
}

// ---- save string (per user) ----

std::string nativeLoadGameString(ScriptContext& ctx) {
  std::string s = store(ctx).loadSave();
  if (!s.empty()) ctx.gameStateTouched = true;
  return s;
}

void nativeSaveGameString(ScriptContext& ctx, std::string data) {
  if (store(ctx).current() < 0) return;
  std::string why;
  if (ctx.module && ctx.module->refuseSave(data, why)) {
    ctx.log.write("[save] refused: " + why);
    return;
  }
  if (!store(ctx).saveSave(data)) ctx.log.write("[user] SaveGameString: could not write the save");
  else ctx.gameStateTouched = true;
}

// ---- user files ----

// Names that would overwrite the store's own bookkeeping are refused.
bool reservedUserFile(const std::string& name) {
  // ':' would make a drive or root name on Windows, a leading slash an absolute path.
  if (name.find(':') != std::string::npos || (!name.empty() && (name[0] == '/' || name[0] == '\\'))) return true;
  const std::string key = assets::AssetFS::normalize("user:" + name);
  if (key.empty()) return true;
  const std::string rest = key.substr(5);
  if (rest == "users.txt" || rest == "prefs.txt" || rest == "hiscore.dat" || rest.compare(0, 9, "profiles/") == 0 || rest == "profiles") return true;
  return rest.size() >= 4 && rest.compare(rest.size() - 4, 4, ".tmp") == 0;
}

// The original returns nothing; here a failed write returns false (Reforged's SaveGame checks `ok == false`).
bool nativeWriteToFile(ScriptContext& ctx, std::string name, std::string data) {
  if (reservedUserFile(name)) {
    ctx.log.write("[user] WriteToFile: refused '" + name + "'");
    return false;
  }
  const std::string rel = assets::AssetFS::normalize("user:" + name).substr(5);
  if (rel.empty() || !store(ctx).writeFile(rel, data)) {
    ctx.log.write("[user] WriteToFile: could not write '" + name + "'");
    return false;
  }
  return true;
}

// A missing file gives "".
std::string nativeReadFromFile(ScriptContext& ctx, std::string name) {
  if (reservedUserFile(name)) return {};
  return ctx.fs.readText("user:" + name).value_or(std::string());
}

std::string nativeNextSaveFileName(ScriptContext& ctx) { return store(ctx).nextSaveName(); }

// ---- scores and game mode ----

// LogScore(score, a, b): HiscoreStore rules (score truncated, 0 skipped, one row per user); a = game data, b = medals XML.
void nativeLogScore(ScriptContext& ctx, double score, std::string a, std::string b) { store(ctx).logScore(score, a, b); }

// Only mode 0 (the native Story slot) does anything in the original; the engine keeps the mode as a pref.
void nativeSetCurrentGameMode(ScriptContext& ctx, int mode) {
  if (mode == 0) store(ctx).setPref("gamemode", "0");
}

// Flips the current user's soundoff pref and mutes or unmutes the mixer.
void nativeToggleSoundMute(ScriptContext& ctx) {
  sdk::UserStore& s = store(ctx);
  const bool muted = s.pref("soundoff") != "1";
  s.setPref("soundoff", muted ? "1" : "0");
  // BSGSoundMute(true) only zeroes the master, so unsaved slider levels stay while muted; BSGSoundMute(false) restores the
  // master and re-applies the STORED prefs (ledger-and-game-natives.md 8, audio.md), which drops unsaved slider moves.
  if (muted && ctx.audio) ctx.audio->setLevels(ctx.audio->sfxLevel(), ctx.audio->musicLevel(), ctx.audio->envLevel(), true);
  else applyAudioPrefs(ctx);
}

// The Options toggle. The original fades, switches the display and stores the pref; the window stays 800x600 logical.
// With no platform hook (tests) only the stored "fullscreen" pref flips. False makes Options show "screenswitchfailed".
bool nativeToggleFullScreen(ScriptContext& ctx) {
  if (ctx.toggleFullscreen) return ctx.toggleFullscreen();
  if (!ctx.users) return false;
  sdk::UserStore& s = store(ctx);
  s.setGlobalPref("fullscreen", s.globalPref("fullscreen") == "1" ? "0" : "1");
  return true;
}

// TOptions::SaveVolumes: each of the three option sliders that exists stores its value (times 1000, truncated) as the
// current user's matching preference. A missing slider is skipped.
void nativeSaveVolumes(ScriptContext& ctx) {
  for (const ui::VolumeSlider& row : ui::kVolumeSliders)
    if (auto* s = dynamic_cast<ui::SliderWindow*>(ctx.wm.findInChain(row.name)))
      store(ctx).setPref(row.pref, std::to_string(audio::perMilleFromLevel(s->value())));
  applyAudioPrefs(ctx);
}

}  // namespace

void registerUserNatives(lua_State* L, ScriptContext& ctx) {
  if (!ctx.users) {
    ctx.users = std::make_shared<sdk::UserStore>(ctx.fs.userRoot());
    if (ctx.module) {
      const VolumeDefaults& v = ctx.module->profile().volumeDefaults;
      ctx.users->setVolumeDefaults(v.sfx, v.music, v.ambient);
    }
  }
  bind<nativeGetNumUsers>(L, ctx, "GetNumUsers");
  bind<nativeGetCurrentUser>(L, ctx, "GetCurrentUser");
  bind<nativeGetCurrentUserName>(L, ctx, "GetCurrentUserName");
  bind<nativeGetUserName>(L, ctx, "GetUserName");
  bind<nativeIsNameInUse>(L, ctx, "IsNameInUse");
  bind<nativeCreateNewUser>(L, ctx, "CreateNewUser");
  bind<nativeSetCurrentUser>(L, ctx, "SetCurrentUser");
  bind<nativeChangeCurrentUserName>(L, ctx, "ChangeCurrentUserName");
  bind<nativeDeleteUser>(L, ctx, "DeleteUser");
  bind<nativeLoadGameString>(L, ctx, "LoadGameString");
  bind<nativeSaveGameString>(L, ctx, "SaveGameString");
  bind<nativeWriteToFile>(L, ctx, "WriteToFile");
  bind<nativeReadFromFile>(L, ctx, "ReadFromFile");
  bind<nativeNextSaveFileName>(L, ctx, "NextSaveFileName");
  bind<nativeLogScore>(L, ctx, "LogScore");
  bind<nativeSetCurrentGameMode>(L, ctx, "SetCurrentGameMode");
  bind<nativeToggleSoundMute>(L, ctx, "ToggleSoundMute");
  bind<nativeSaveVolumes>(L, ctx, "SaveVolumes");
  bind<nativeToggleFullScreen>(L, ctx, "ToggleFullScreen");
}

}  // namespace choco::script
