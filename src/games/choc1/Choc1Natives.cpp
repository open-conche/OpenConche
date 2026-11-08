#include "games/choc1/Choc1Natives.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "games/choc1/Choc1AudioNatives.h"
#include "games/choc1/Choc1Windows.h"
#include "games/choc1/Choc1World.h"
#include "games/choc1/ChoosePlayer.h"
#include "games/choc1/CreditsWindow.h"
#include "games/choc1/DiceGame.h"
#include "games/choc1/FactoryGame.h"
#include "games/choc1/Finale.h"
#include "games/choc1/OptionsDialog.h"
#include "games/choc1/HiscoreWindow.h"
#include "games/choc1/Logo.h"
#include "games/choc1/MapScreen.h"
#include "games/choc1/Rollover.h"
#include "games/choc1/FrameWindow.h"
#include "games/choc1/SimpleAnimation.h"
#include "games/common/DynamicWindow.h"
#include "games/common/LegacyUtilNatives.h"
#include "sdk/Users.h"
#include "script/ScriptHost.h"
#include "script/Stubs.h"

namespace choco::script {

namespace {

const StubRow kStubRows[] = {
#define STUB(name, ret, args) {name, ret, args},
#define ALIAS(name, target)
#define WINDOW_TYPE(name)
#define API_NAME(name)
#include "games/choc1/generated/Stubs_choc1.inc"
#undef STUB
#undef ALIAS
#undef WINDOW_TYPE
#undef API_NAME
};

const AliasRow kAliasRows[] = {
#define STUB(name, ret, args)
#define ALIAS(name, target) {name, target},
#define WINDOW_TYPE(name)
#define API_NAME(name)
#include "games/choc1/generated/Stubs_choc1.inc"
#undef STUB
#undef ALIAS
#undef WINDOW_TYPE
#undef API_NAME
};

const char* const kWindowTypes[] = {
#define STUB(name, ret, args)
#define ALIAS(name, target)
#define WINDOW_TYPE(name) name,
#define API_NAME(name)
#include "games/choc1/generated/Stubs_choc1.inc"
#undef STUB
#undef ALIAS
#undef WINDOW_TYPE
#undef API_NAME
};

const char* const kApiNames[] = {
#define STUB(name, ret, args)
#define ALIAS(name, target)
#define WINDOW_TYPE(name)
#define API_NAME(name) name,
#include "games/choc1/generated/Stubs_choc1.inc"
#undef STUB
#undef ALIAS
#undef WINDOW_TYPE
#undef API_NAME
};

// ---- hand-written natives ----

// UpdateDynamicWindow (exe 00409740): every DynamicWindow of that name in the whole tree (here the whole modal chain) is rebuilt.
void nativeUpdateDynamicWindow(ScriptContext& ctx, std::string name) { game::DynamicWindow::updateNamed(ctx.wm, name); }

// UpdatePlayerButtons(selected) (exe 00407180): lays out the live ChoosePlayer window. The exe registers it in the window's
// constructor and registers nil in its destructor; here it is always bound and does nothing (one log line) when no ChoosePlayer
// window exists.
void nativeUpdatePlayerButtons(ScriptContext& ctx, double selected) {
  if (auto* w = game::choc1::ChoosePlayerWindow::inChain(ctx.wm)) w->update(static_cast<int>(selected));
  else if (ctx.warned.insert("choc1-UpdatePlayerButtons-nowindow").second)
    ctx.log.write("[choc1] UpdatePlayerButtons: no ChoosePlayer window, ignored");
}

// AnimateTravel(route), PauseTravel(), ResumeTravel() (exe 00417da0, 00416210, 00416220): the exe binds them in the MapScreen
// constructor and registers nil in its destructor; here they are always bound, find the MapScreen in the modal chain and log
// one line when there is none.
game::choc1::MapScreen* mapScreenFor(ScriptContext& ctx, const char* what) {
  if (auto* m = game::choc1::MapScreen::inChain(ctx.wm)) return m;
  if (ctx.warned.insert(std::string("choc1-") + what + "-nowindow").second)
    ctx.log.write(std::string("[choc1] ") + what + ": no MapScreen window, ignored");
  return nullptr;
}
void nativeAnimateTravel(ScriptContext& ctx, TableArg route) {
  if (!route.valid()) return;
  if (auto* m = mapScreenFor(ctx, "AnimateTravel")) m->animateTravel(route.L, route.index);
}
void nativePauseTravel(ScriptContext& ctx) {
  if (auto* m = mapScreenFor(ctx, "PauseTravel")) m->pause();
}
void nativeResumeTravel(ScriptContext& ctx) {
  if (auto* m = mapScreenFor(ctx, "ResumeTravel")) m->resume();
}

// StartGame(), RestartGame(), GetProductCount(), TogglePause() (exe 0040a150, 0040e1c0, 00409fd0, 0040abb0): the exe binds them in
// the TFactoryGame constructor and registers nil in its destructor; here they are always bound, find the FactoryGame in the
// modal chain and log one line when there is none. (Chocolatier 3's StartGame is another game's; this one is bound after it.)
game::choc1::FactoryGame* factoryFor(ScriptContext& ctx, const char* what) {
  if (auto* g = game::choc1::FactoryGame::inChain(ctx.wm)) return g;
  if (ctx.warned.insert(std::string("choc1-") + what + "-nowindow").second)
    ctx.log.write(std::string("[choc1] ") + what + ": no FactoryGame window, ignored");
  return nullptr;
}
void nativeStartGame(ScriptContext& ctx) {
  if (auto* g = factoryFor(ctx, "StartGame")) g->startGame();
}
void nativeRestartGame(ScriptContext& ctx) {
  if (auto* g = factoryFor(ctx, "RestartGame")) g->restartGame();
}
double nativeGetProductCount(ScriptContext& ctx) {
  auto* g = factoryFor(ctx, "GetProductCount");
  return g ? g->productCount() : 0;
}
void nativeTogglePause(ScriptContext& ctx) {
  if (auto* g = factoryFor(ctx, "TogglePause")) g->togglePause();
}

// RollDice(bet), StopDie() (exe 00408a30, 004089e0): the exe binds them in the TDiceGame constructor and registers nil in its
// destructor; here they are always bound, find the DiceGame in the modal chain and log one line when there is none.
game::choc1::DiceGame* diceFor(ScriptContext& ctx, const char* what) {
  if (auto* d = game::choc1::DiceGame::inChain(ctx.wm)) return d;
  if (ctx.warned.insert(std::string("choc1-") + what + "-nowindow").second)
    ctx.log.write(std::string("[choc1] ") + what + ": no DiceGame window, ignored");
  return nullptr;
}
void nativeRollDice(ScriptContext& ctx, std::string bet) {
  if (auto* d = diceFor(ctx, "RollDice")) d->rollDice(std::move(bet));
}
void nativeStopDie(ScriptContext& ctx) {
  if (auto* d = diceFor(ctx, "StopDie")) d->stopDie();
}

// Fills the printf slots of `format` in order: %d and %0Nd (the only ones the format* strings use); "%%" is a literal '%'.
std::string fillSlots(const std::string& format, const std::vector<unsigned>& values) {
  std::string out;
  std::size_t next = 0;
  for (std::size_t i = 0; i < format.size(); ++i) {
    if (format[i] != '%') {
      out += format[i];
      continue;
    }
    ++i;
    if (i < format.size() && format[i] == '%') {
      out += '%';
      continue;
    }
    const bool zero = i < format.size() && format[i] == '0';
    std::size_t width = 0;
    while (i < format.size() && format[i] >= '0' && format[i] <= '9') width = width * 10 + static_cast<std::size_t>(format[i++] - '0');
    if (i >= format.size() || format[i] != 'd') break;  // not a slot we know: stop here
    std::string digits = std::to_string(next < values.size() ? values[next] : 0u);
    ++next;
    if (zero && digits.size() < width) digits.insert(0, width - digits.size(), '0');
    out += digits;
  }
  return out;
}

// The key's text, or `fallback` when the string table lacks it.
std::string formatOf(const assets::StringTable& strings, const char* key, const char* fallback) {
  if (!strings.has(key)) return fallback;
  const std::string text = strings.sdkLookup(key);
  return text == "#####" ? fallback : text;
}

double nativeUtilMathFloor(ScriptContext&, double x) { return std::trunc(x); }
void nativeNoop(ScriptContext&) {}
bool nativeFalse(ScriptContext&) { return false; }
bool nativeCheckCheatMode(ScriptContext& ctx) { return std::strtol(ctx.config.get("cheatmode").c_str(), nullptr, 10) != 0; }
std::string nativeUtilDollars(ScriptContext& ctx, double v) { return choc1Dollars(ctx.strings, v); }

int nativeUtilMathRandom(ScriptContext& ctx, double lo, double hi) {
  // The exe's UtilMathRandom (0041dbe0): truncTowardZero(lo + rand() * (hi - lo + 1.0) / 32767.0), `rand` the
  // shared CRT stream (one draw per call) that the factory and dice games also draw from. The original seeds it
  // with milliseconds since start at boot (UtilMathSeedRandom, srand); this engine keeps the unseeded stream
  // (seed 1) so the mini-games and the story tests stay reproducible - a documented deviation.
  game::choc1::Choc1World* world = game::choc1::choc1WorldOf(ctx);
  const int r = world ? world->crtRand.next() : 0;
  return static_cast<int>(lo + static_cast<double>(r) * (hi - lo + 1.0) / 32767.0);
}

// ---- users and saves (docs/re/choc1-fit.md section 3) ----

// GetCurrentUserMode (exe 0041c660): the string pref "mode" of the current user, "empire" when unset. Nothing in the Lua census
// or this engine writes that pref, so it is "empire" for every user (who stores it in the exe is unknown).
std::string nativeGetCurrentUserMode(ScriptContext& ctx) { return ctx.users ? ctx.users->pref("mode", "empire") : "empire"; }

// ChangeUserName(index, name) (exe 0041c700): sets the "name" pref of user `index`, i.e. renames it (zero-based).
void nativeChangeUserName(ScriptContext& ctx, int index, std::string name) {
  if (ctx.users && !ctx.users->rename(index, name))
    ctx.log.write("[user] ChangeUserName: rejected (index " + std::to_string(index) + " or the name)");
}

// The game's two saves are the pref save_<mode> of the current user, mode "empire" (Story) or "free" (sim.lua:114..285). On this
// engine they are the UserStore slots: "empire" is slot 0 (save.txt) and every other mode is slot 1 (save_free.txt).
int saveSlot(const std::string& mode) { return mode == "empire" ? 0 : 1; }

std::string nativeLoadGameString(ScriptContext& ctx, std::string mode) {
  if (!ctx.users) return {};
  std::string s = ctx.users->loadSave(saveSlot(mode));
  ctx.log.write("[save] load " + mode + ": " + (s.empty() ? "empty" : std::to_string(s.size()) + " bytes"));
  if (!s.empty()) ctx.gameStateTouched = true;
  return s;
}

// No refuseSave: the guard is for Chocolatier 3's Player shape and Chocolatier 1 never saves one.
void nativeSaveGameString(ScriptContext& ctx, std::string mode, std::string data) {
  if (!ctx.users || ctx.users->current() < 0) return;
  if (!ctx.users->saveSave(data, saveSlot(mode))) {
    ctx.log.write("[user] SaveGameString: could not write the save");
    return;
  }
  ctx.gameStateTouched = true;
  ctx.log.write("[save] saved " + mode + ": " + std::to_string(data.size()) + " bytes");
}

}  // namespace

std::string choc1Dollars(const assets::StringTable& strings, double amount) {
  std::int64_t n = 0;
  if (std::isfinite(amount)) {
    const double t = std::trunc(amount);
    n = t >= 9.2e18 ? INT64_MAX : t <= -9.2e18 ? INT64_MIN : static_cast<std::int64_t>(t);
  }
  const std::uint32_t u = static_cast<std::uint32_t>(static_cast<std::uint64_t>(n));
  const unsigned b = u / 1000000000u, m = (u % 1000000000u) / 1000000u, t = (u % 1000000u) / 1000u, o = u % 1000u;
  if (b != 0) return fillSlots(formatOf(strings, "formatBillions", "$%d,%03d,%03d,%03d"), {b, m, t, o});
  if (m != 0) return fillSlots(formatOf(strings, "formatMillions", "$%d,%03d,%03d"), {m, t, o});
  if (t != 0) return fillSlots(formatOf(strings, "formatThousands", "$%d,%03d"), {t, o});
  return fillSlots(formatOf(strings, "formatOnes", "$%d"), {u});
}

const char* const* choc1ApiNames(std::size_t& count) {
  count = sizeof(kApiNames) / sizeof(kApiNames[0]);
  return kApiNames;
}

void registerChoc1Natives(lua_State* L, ScriptContext& ctx) {
  registerStubRows(L, ctx, kStubRows, sizeof(kStubRows) / sizeof(kStubRows[0]));

  // Util*: the exact aliases are ALIAS rows below. UtilMathSeedRandom: our Rng is seeded from --seed, so there is nothing to
  // reseed. UtilTriggerAd / UtilTriggerCommunityOffer are the retail launcher's ad hooks (online, out of scope): nothing.
  bind<nativeUtilMathFloor>(L, ctx, "UtilMathFloor");
  bind<nativeNoop>(L, ctx, "UtilMathSeedRandom");
  bind<nativeNoop>(L, ctx, "UtilTriggerAd");
  bind<nativeNoop>(L, ctx, "UtilTriggerCommunityOffer");
  bind<legacyTimeStamp>(L, ctx, "UtilTimeStamp");
  bind<legacyNextSaveFile>(L, ctx, "UtilNextSaveFile");
  bind<nativeUtilMathRandom>(L, ctx, "UtilMathRandom");
  bind<nativeUtilDollars>(L, ctx, "UtilDollars");
  bind<nativeGetCurrentUserMode>(L, ctx, "GetCurrentUserMode");
  bind<nativeChangeUserName>(L, ctx, "ChangeUserName");
  bind<legacySetCurrentGameMode>(L, ctx, "SetCurrentGameMode");
  bind<nativeLoadGameString>(L, ctx, "LoadGameString");
  bind<nativeSaveGameString>(L, ctx, "SaveGameString");
  // The exe's UtilCheckCheatMode is IsEnabled("cheatmode") (choc1-natives.md 2), the same setting --cheatmode sets: the
  // main loop then sets gCheatsEnabled and gDevelopmentMode, the main menu grows its cheat buttons and the port shows
  // ui/cheatbar.lua. UtilCheckFirstPeek is the demo build's flag: false. UtilCheckCrossSell asks for xsell/xsell.xml:
  // cross-sell is out of scope, so false.
  bind<nativeCheckCheatMode>(L, ctx, "UtilCheckCheatMode");
  bind<nativeFalse>(L, ctx, "UtilCheckFirstPeek");
  bind<nativeFalse>(L, ctx, "UtilCheckCrossSell");
  registerChoc1AudioNatives(L, ctx);
  registerChoc1WindowNatives(L, ctx);
  bind<nativeUpdateDynamicWindow>(L, ctx, "UpdateDynamicWindow");
  bind<nativeUpdatePlayerButtons>(L, ctx, "UpdatePlayerButtons");
  bind<nativeAnimateTravel>(L, ctx, "AnimateTravel");
  bind<nativePauseTravel>(L, ctx, "PauseTravel");
  bind<nativeResumeTravel>(L, ctx, "ResumeTravel");
  bind<nativeStartGame>(L, ctx, "StartGame");
  bind<nativeRestartGame>(L, ctx, "RestartGame");
  bind<nativeGetProductCount>(L, ctx, "GetProductCount");
  bind<nativeTogglePause>(L, ctx, "TogglePause");
  bind<nativeRollDice>(L, ctx, "RollDice");
  bind<nativeStopDie>(L, ctx, "StopDie");
  registerAliasRows(L, ctx, kAliasRows, sizeof(kAliasRows) / sizeof(kAliasRows[0]));
}

void registerChoc1WindowTypes(lua_State* L, ScriptContext& ctx) {
  // SimpleAnimation is a real window; the rest of the table stays placeholders until its slice.
  if (addWindowType(L, ctx, "SimpleAnimation"))
    ctx.wm.addWindowType("SimpleAnimation",
                         [&ctx](ui::WindowManager& wm) { return std::make_unique<game::choc1::SimpleAnimation>(wm, ctx); });
  if (addWindowType(L, ctx, "Frame"))
    ctx.wm.addWindowType("Frame", [&ctx](ui::WindowManager& wm) { return std::make_unique<game::choc1::FrameWindow>(wm, ctx); });
  if (addWindowType(L, ctx, "DynamicWindow"))
    ctx.wm.addWindowType("DynamicWindow",
                         [&ctx](ui::WindowManager& wm) { return std::make_unique<game::DynamicWindow>(wm, ctx, "choc1"); });
  if (addWindowType(L, ctx, "ChoosePlayer"))
    ctx.wm.addWindowType("ChoosePlayer",
                         [&ctx](ui::WindowManager& wm) { return std::make_unique<game::choc1::ChoosePlayerWindow>(wm, ctx); });
  if (addWindowType(L, ctx, "MapScreen"))
    ctx.wm.addWindowType("MapScreen", [&ctx](ui::WindowManager& wm) { return std::make_unique<game::choc1::MapScreen>(wm, ctx); });
  // Chocolatier 1's TRollover (target, script, hot rect, instant popup): a factory registered over the engine's built-in
  // Rollover, which stays Chocolatier 2 and 3's (WindowManager::createWindow asks the factories first).
  if (addWindowType(L, ctx, "OptionsDialog"))
    ctx.wm.addWindowType("OptionsDialog", [&ctx](ui::WindowManager& wm) { return std::make_unique<game::choc1::OptionsDialog>(wm, ctx); });
  if (addWindowType(L, ctx, "CreditsWindow"))
    ctx.wm.addWindowType("CreditsWindow", [&ctx](ui::WindowManager& wm) { return std::make_unique<game::choc1::CreditsWindow>(wm, ctx); });
  if (addWindowType(L, ctx, "FactoryGame"))
    ctx.wm.addWindowType("FactoryGame", [&ctx](ui::WindowManager& wm) { return std::make_unique<game::choc1::FactoryGame>(wm, ctx); });
  // DiceGame comes after FactoryGame, the order its placeholder row held in the window-type list before this slice.
  if (addWindowType(L, ctx, "DiceGame"))
    ctx.wm.addWindowType("DiceGame", [&ctx](ui::WindowManager& wm) { return std::make_unique<game::choc1::DiceGame>(wm, ctx); });
  // TLogo, the company-sign composite the builder and the port sign show (ShowPlayerLogo/PlayerLogo of
  // ui/logoinfo.lua): registered after DiceGame, so the window-type list keeps the placeholder tail order
  // (the Finale factory follows it there).
  if (addWindowType(L, ctx, "Logo"))
    ctx.wm.addWindowType("Logo", [](ui::WindowManager& wm) { return std::make_unique<game::choc1::LogoWindow>(wm); });
  // TFinale, the ending screen's chocolate rain (docs/re/choc1-finale.md): ui/reward_finale.lua shows it when the
  // master medal is awarded. Registered right after Logo so the window-type list keeps its old order, and before
  // the tail loop so `Finale` no longer needs its placeholder row.
  if (addWindowType(L, ctx, "Finale"))
    ctx.wm.addWindowType("Finale",
                         [&ctx](ui::WindowManager& wm) { return std::make_unique<game::choc1::FinaleWindow>(wm, ctx); });
  // Chocolatier 1's THiscore on the shared offline screen: `formatScore` rate column and the gamemode label.
  // The factory replaces the shared one (choc3's registerHiscoreNatives ran first); the Lua global stays.
  ctx.wm.addWindowType("HiscoreWindow",
                       [&ctx](ui::WindowManager& wm) { return std::make_unique<game::choc1::HiscoreWindow>(wm, ctx); });
  if (!ctx.wm.hasWindowType("Rollover")) addWindowType(L, ctx, "Rollover");
  ctx.wm.addWindowType("Rollover", [&ctx](ui::WindowManager& wm) { return std::make_unique<game::choc1::RolloverWindow>(wm, ctx); });
  for (const char* name : kWindowTypes)
    if (!ctx.wm.hasWindowType(name)) addWindowType(L, ctx, name);
}

}  // namespace choco::script
