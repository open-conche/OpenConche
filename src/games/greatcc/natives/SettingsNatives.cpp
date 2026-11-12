#include <cmath>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/natives/StateNatives.h"
#include "games/greatcc/ui/Transitions.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {

using script::ScriptContext;

// ---- mode and difficulty (docs/re/greatcc-boot-and-natives.md 2.2) ----

void nativeSetCurrentGameMode(ScriptContext& ctx, int mode) {
  if (GreatCCWorld* w = worldOf(ctx)) w->setGameMode(ctx, mode);
}
double nativeGetCurrentGameMode(ScriptContext& ctx) {
  const GreatCCWorld* w = worldOf(ctx);
  return w ? w->settings().mode : 0;
}
void nativeSetCurrentDifficulty(ScriptContext& ctx, int diff) {
  GreatCCWorld* w = worldOf(ctx);
  if (!w) return;
  const int before = w->settings().mode;
  if (!w->settings().setDifficulty(diff)) return;
  if (w->settings().mode != before) w->persistGameMode(ctx);  // the mode pref follows the difficulty
}
double nativeGetCurrentDifficulty(ScriptContext& ctx) {
  const GreatCCWorld* w = worldOf(ctx);
  return w ? w->settings().difficulty : 0;
}

// ---- users: the world flushes and reloads around the store's change ----

void nativeCreateNewUser(ScriptContext& ctx, std::string name) {
  if (GreatCCWorld* w = worldOf(ctx)) w->createUser(ctx, name);
}
void nativeSetCurrentUser(ScriptContext& ctx, int index) {
  if (GreatCCWorld* w = worldOf(ctx)) w->setCurrentUser(ctx, index);
}
void nativeChangeCurrentUserName(ScriptContext& ctx, std::string name) {
  if (GreatCCWorld* w = worldOf(ctx)) w->renameCurrentUser(ctx, name);
}
void nativeDeleteUser(ScriptContext& ctx, int index) {
  if (GreatCCWorld* w = worldOf(ctx)) w->deleteUser(ctx, index);
}

// ---- flags ----

// TSettings::InsideGame: the flag, and the pause dialog at once when the game is entered while the application is in the
// background.
void nativeInsideGame(ScriptContext& ctx, bool inside) {
  GreatCCWorld* w = worldOf(ctx);
  if (!w) return;
  w->settings().insideGame = inside;
  if (inside && !w->foreground()) doLuaString(ctx, "DoModal( 'scripts/pause.lua' );");
}
bool nativeAreInsideGame(ScriptContext& ctx) {
  const GreatCCWorld* w = worldOf(ctx);
  return w && w->settings().insideGame;
}
bool nativeIsGameLoaded(ScriptContext& ctx) {
  const GreatCCWorld* w = worldOf(ctx);
  return w && w->settings().gameLoaded;
}
void nativeSetQuitting(ScriptContext& ctx, bool quitting) {
  if (GreatCCWorld* w = worldOf(ctx)) w->settings().quitting = quitting;
}
bool nativeAreQuitting(ScriptContext& ctx) {
  const GreatCCWorld* w = worldOf(ctx);
  return w && w->settings().quitting;
}

// TSettings::QuitToMainMenu: every modal that has a parent is popped, the top one first: all but the Screen. That includes the
// modal DoMainWindow pushed (the screens swap into it), so DoMainWindow returns and Main pushes a fresh main menu. Then the sound
// manager forgets its voices.
void nativeQuitToMainMenu(ScriptContext& ctx) {
  for (int guard = 0; ctx.wm.modalDepth() > 1 && guard < 64; ++guard)
    ctx.wm.popModal(ui::WindowManager::idString(ctx.wm.topModal()->id()));
  if (GreatCCWorld* w = worldOf(ctx)) w->sound().purgeSounds();
}

// ---- small ones ----

// RIntBlock "highest_level": the same number as the saved value of that name.
double nativeGetLastLevelCompleted(ScriptContext& ctx) {
  GreatCCWorld* w = worldOf(ctx);
  return w ? w->levels().highestComplete() : 0;
}
// RIntBlock "score": nothing in the game writes it, so it is always 0.
double nativeGetScore(ScriptContext&) { return 0; }

// TPlatform::Rand() % n + 1. The original divides by zero for n = 0; here that gives 1 (design 3.5).
double nativeRandom(ScriptContext& ctx, double n) {
  const long long limit = static_cast<long long>(n < 0 ? 0 : n);
  if (limit <= 0) {
    ctx.log.write("[greatcc] random(0): returning 1");
    return 1;
  }
  return static_cast<double>(ctx.rng.next32() % static_cast<unsigned long long>(limit) + 1);
}
// (int)(a - b * floor(a / b)).
double nativeRem(ScriptContext&, double a, double b) {
  const double r = a - b * std::floor(a / b);
  return std::isfinite(r) ? static_cast<double>(static_cast<int>(r)) : 0;
}

}  // namespace

void bindSettingsNatives(lua_State* L, script::ScriptContext& ctx) {
  using detail::rebind;
  rebind<nativeSetCurrentGameMode>(L, ctx, "SetCurrentGameMode");
  rebind<nativeGetCurrentGameMode>(L, ctx, "GetCurrentGameMode");
  rebind<nativeSetCurrentDifficulty>(L, ctx, "SetCurrentDifficulty");
  rebind<nativeGetCurrentDifficulty>(L, ctx, "GetCurrentDifficulty");
  rebind<nativeCreateNewUser>(L, ctx, "CreateNewUser");
  rebind<nativeSetCurrentUser>(L, ctx, "SetCurrentUser");
  rebind<nativeDeleteUser>(L, ctx, "DeleteUser");
  rebind<nativeChangeCurrentUserName>(L, ctx, "ChangeCurrentUserName");
  rebind<nativeInsideGame>(L, ctx, "InsideGame");
  rebind<nativeAreInsideGame>(L, ctx, "AreInsideGame");
  rebind<nativeIsGameLoaded>(L, ctx, "IsGameLoaded");
  rebind<nativeSetQuitting>(L, ctx, "SetQuitting");
  rebind<nativeAreQuitting>(L, ctx, "AreQuitting");
  rebind<nativeQuitToMainMenu>(L, ctx, "QuitToMainMenu");
  rebind<nativeGetLastLevelCompleted>(L, ctx, "GetLastLevelCompleted");
  rebind<nativeGetScore>(L, ctx, "GetScore");
  rebind<nativeRandom>(L, ctx, "random");
  rebind<nativeRem>(L, ctx, "rem");
}

}  // namespace choco::games::greatcc
