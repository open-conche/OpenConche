// The sound natives (docs/re/greatcc-audio.md section 1, docs/design/greatcc.md 3.6): VSoundManager's globals and the two
// volume natives. The sprite script's playsound (slice 6) calls GreatCCSound::playSfx.
#include <optional>

#include "audio/Audio.h"
#include "audio/Prefs.h"
#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/natives/StateNatives.h"
#include "sdk/Users.h"
#include "ui/VolumeSliders.h"
#include "ui/WindowManager.h"
#include "ui/widgets/InputWidgets.h"

namespace choco::games::greatcc {

namespace {

using script::ScriptContext;

// The bool arguments are lua_toboolean style: a missing one is false.
void nativePlaySFX(ScriptContext& ctx, std::string file, std::optional<bool> loop) {
  if (GreatCCWorld* w = worldOf(ctx)) w->sound().playSfx(file, loop.value_or(false));
}
void nativePlayMusic(ScriptContext& ctx, std::string file, std::optional<bool> loop, std::optional<bool> fade) {
  if (GreatCCWorld* w = worldOf(ctx)) w->sound().playMusic(file, loop.value_or(false), fade.value_or(false));
}
void nativePlayVO(ScriptContext& ctx, std::string file, std::optional<bool> interrupt, std::optional<bool> skipIfBusy) {
  if (GreatCCWorld* w = worldOf(ctx)) w->sound().playVO(file, interrupt.value_or(false), skipIfBusy.value_or(false));
}
void nativePurgeVOQueue(ScriptContext& ctx) {
  if (GreatCCWorld* w = worldOf(ctx)) w->sound().purgeVOQueue();
}
void nativePurgeSounds(ScriptContext& ctx) {
  if (GreatCCWorld* w = worldOf(ctx)) w->sound().purgeSounds();
}
void nativePauseSounds(ScriptContext& ctx, std::optional<bool> pause, std::optional<double> group) {
  if (GreatCCWorld* w = worldOf(ctx)) w->sound().pauseSounds(pause.value_or(false), static_cast<int>(group.value_or(-1)));
}

// TSettings::ToggleSoundMute: flips the user's soundoff pref and sets the master only (0 when now muted, else 1). The group
// levels, including unsaved slider moves, are neither reset nor re-applied.
void nativeToggleSoundMute(ScriptContext& ctx) {
  if (!ctx.users) return;
  const bool muted = ctx.users->pref("soundoff") != "1";
  ctx.users->setPref("soundoff", muted ? "1" : "0");
  if (ctx.audio) ctx.audio->setLevels(ctx.audio->sfxLevel(), ctx.audio->musicLevel(), ctx.audio->envLevel(), muted);
}

// TOptions::SaveVolumes: the two sliders' values times 1000, truncated, become the user's soundlevel and musiclevel. The
// levels already applied while the sliders moved, so nothing is re-applied.
void nativeSaveVolumes(ScriptContext& ctx) {
  if (!ctx.users) return;
  for (const char* name : {"options_sfxlevel_slider", "options_musiclevel_slider"}) {
    const ui::VolumeSlider* row = ui::findVolumeSlider(name);
    if (auto* s = dynamic_cast<ui::SliderWindow*>(ctx.wm.findInChain(name)))
      if (row) ctx.users->setPref(row->pref, std::to_string(audio::perMilleFromLevel(s->value())));
  }
}

}  // namespace

void bindSoundNatives(lua_State* L, script::ScriptContext& ctx) {
  using detail::rebind;
  rebind<nativePlaySFX>(L, ctx, "PlaySFX");
  rebind<nativePlayMusic>(L, ctx, "PlayMusic");
  rebind<nativePlayVO>(L, ctx, "PlayVO");
  rebind<nativePurgeVOQueue>(L, ctx, "PurgeVOQueue");
  rebind<nativePurgeSounds>(L, ctx, "PurgeSounds");
  rebind<nativePauseSounds>(L, ctx, "PauseSounds");
  rebind<nativeToggleSoundMute>(L, ctx, "ToggleSoundMute");
  rebind<nativeSaveVolumes>(L, ctx, "SaveVolumes");
  // A TButton's click and rollover files (group 0) go through the same path rules as PlaySFX: the app installed the
  // engine's hook before boot, this one replaces it (a path without .ogg is silent). It finds the world per call.
  ctx.wm.setSoundHook([&ctx](const std::string& file, bool) {
    if (GreatCCWorld* w = worldOf(ctx)) w->sound().playUi(file);
  });
}

}  // namespace choco::games::greatcc
