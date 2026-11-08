#include "games/choc2/Choc2Natives.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "sdk/Users.h"
#include "games/common/LegacyUtilNatives.h"
#include "games/common/World.h"
#include "games/choc2/Character.h"
#include "games/choc2/FrameWindow.h"
#include "games/choc2/MainMenuWindow.h"
#include "games/choc2/Map.h"
#include "games/choc2/MapTravel.h"
#include "games/choc2/Port.h"
#include "games/choc2/SpriteWindow.h"
#include "script/Formats.h"
#include "games/choc2/Choc2AudioNatives.h"
#include "games/choc2/Choc2FactoryNatives.h"
#include "games/choc2/Choc2BonusNatives.h"
#include "games/choc2/Choc2DiceNatives.h"
#include "games/choc2/Choc2LedgerNatives.h"
#include "games/choc2/Choc2LogoNatives.h"
#include "games/choc2/Choc2MenuNatives.h"
#include "script/LuaStyle.h"
#include "script/ScriptHost.h"
#include "script/Stubs.h"

namespace choco::script {

namespace {

const StubRow kStubRows[] = {
#define STUB(name, ret, args) {name, ret, args},
#define ALIAS(name, target)
#define WINDOW_TYPE(name)
#define GLOBAL_NUMBER(name, value)
#define API_NAME(name)
#include "games/choc2/generated/Stubs_choc2.inc"
#undef STUB
#undef ALIAS
#undef WINDOW_TYPE
#undef GLOBAL_NUMBER
#undef API_NAME
};

const AliasRow kAliasRows[] = {
#define STUB(name, ret, args)
#define ALIAS(name, target) {name, target},
#define WINDOW_TYPE(name)
#define GLOBAL_NUMBER(name, value)
#define API_NAME(name)
#include "games/choc2/generated/Stubs_choc2.inc"
#undef STUB
#undef ALIAS
#undef WINDOW_TYPE
#undef GLOBAL_NUMBER
#undef API_NAME
};

const char* const kWindowTypes[] = {
#define STUB(name, ret, args)
#define ALIAS(name, target)
#define WINDOW_TYPE(name) name,
#define GLOBAL_NUMBER(name, value)
#define API_NAME(name)
#include "games/choc2/generated/Stubs_choc2.inc"
#undef STUB
#undef ALIAS
#undef WINDOW_TYPE
#undef GLOBAL_NUMBER
#undef API_NAME
};

const char* const kApiNames[] = {
#define STUB(name, ret, args)
#define ALIAS(name, target)
#define WINDOW_TYPE(name)
#define GLOBAL_NUMBER(name, value)
#define API_NAME(name) name,
#include "games/choc2/generated/Stubs_choc2.inc"
#undef STUB
#undef ALIAS
#undef WINDOW_TYPE
#undef GLOBAL_NUMBER
#undef API_NAME
};

struct GlobalRow {
  const char* name;
  double value;
};

const GlobalRow kGlobals[] = {
#define STUB(name, ret, args)
#define ALIAS(name, target)
#define WINDOW_TYPE(name)
#define GLOBAL_NUMBER(name, value) {name, value},
#define API_NAME(name)
#include "games/choc2/generated/Stubs_choc2.inc"
#undef STUB
#undef ALIAS
#undef WINDOW_TYPE
#undef GLOBAL_NUMBER
#undef API_NAME
};

// ---- hand-written natives ----

// bsgRandomSeed: our Rng is seeded from --seed, so there is nothing to reseed (design 2.2): deterministic on purpose.
// bsgLuaGarbageCollect: the host's own collector runs. The offer, ad and browser calls are online features, which are out
// of scope: they do nothing.
void nativeNoop(ScriptContext&) {}
void nativeNoopString(ScriptContext&, std::string) {}

// SetUserName(index, name): renames a user (zero-based, like GetUserName).
void nativeSetUserName(ScriptContext& ctx, int index, std::string name) {
  if (ctx.users && !ctx.users->rename(index, name))
    ctx.log.write("[user] SetUserName: rejected (index " + std::to_string(index) + " or the name)");
}

// bsgDollars: UtilDollars truncates toward zero where Dollars rounds (docs/re/choc1-natives.md section 2); the text is built
// like Dollars (sign and separator globals). TODO for a later slice: the original builds it from the string-table keys
// formatOnes/Thousands/Millions/Billions and wraps negatives to 32-bit unsigned.
std::string nativeBsgDollars(ScriptContext& ctx, double v) {
  lua_State* L = ctx.lua.L();
  auto global = [&](const char* name, const char* def) {
    lua_getglobal(L, name);
    std::string s = lua_isstring(L, -1) ? lua_tostring(L, -1) : def;
    lua_pop(L, 1);
    return s;
  };
  return formatDollars(std::trunc(v), global("_MONEY_SIGN", "$"), global("_NUMERIC_SEPARATOR", ","));
}

unsigned dateArgument(double v) {
  if (!(v > 0)) return 0;
  return v >= 4294967295.0 ? 4294967295u : static_cast<unsigned>(v);
}

}  // namespace

std::string choc2Date(const assets::StringTable& strings, double weeks, double days, bool shortForm) {
  const unsigned w = dateArgument(weeks), d = dateArgument(days);
  const unsigned inYear = w % 52;
  const unsigned month = inYear * 12 / 52;  // 0..11
  unsigned day = (inYear - month * 52 / 12) * 7 + d;
  day = day == 0 ? 1 : std::min(day, 28u);
  const unsigned year = w / 52 + 1925;
  const std::string name = strings.sdkLookup("month_" + std::to_string(month + 1));
  if (shortForm) return std::to_string(day) + " " + name.substr(0, 3) + " " + std::to_string(year);
  // formatDate is "%1% %%d, %%d": the month name, then two printf slots for the day and the year.
  std::string text = strings.sdkLookup("formatDate", {name});
  for (const std::string& value : {std::to_string(day), std::to_string(year)}) {
    const std::size_t at = text.find("%d");
    if (at == std::string::npos) break;
    text.replace(at, 2, value);
  }
  return text;
}

namespace {

std::string nativeBsgDate(ScriptContext& ctx, double weeks, double days) { return choc2Date(ctx.strings, weeks, days, false); }
std::string nativeBsgShortDate(ScriptContext& ctx, double weeks, double days) { return choc2Date(ctx.strings, weeks, days, true); }

// GetCurrentGameMode: the mode SetCurrentGameMode stored (0 until a user exists or one was set).
double nativeGetCurrentGameMode(ScriptContext& ctx) {
  if (!ctx.users) return 0;
  try {
    return std::stod(ctx.users->pref("gamemode", "0"));
  } catch (...) {
    return 0;
  }
}

// bsgLoadGameString / bsgSaveGameString: Chocolatier 2's Simulator:LoadGame switches on the game mode (sim.lua:348 to 385): it
// sets the mode, calls bsgLoadGameString() and reads the result as that mode's save. So the native keeps one save per user
// PER MODE. Mode 0 (Story) is the same save.txt that Chocolatier 3 uses; any other mode (Free Play is 1) is save_free.txt, so a
// Free Play game cannot overwrite the Story one. (Chocolatier 3's LoadGameString has a single slot.) The mode is the "gamemode"
// pref that SetCurrentGameMode stores.
int saveSlot(ScriptContext& ctx) { return static_cast<int>(nativeGetCurrentGameMode(ctx)) == 0 ? 0 : 1; }

// A one-line summary of a save string for the log: the active quest and the week, to chase "a finished mission is active again".
std::string saveSummary(const std::string& data) {
  auto field = [&](const char* key) -> std::string {
    const std::string k = std::string(",") + key + "=";
    size_t i = data.find(k);
    if (i == std::string::npos) return "-";
    i += k.size();
    size_t e = data.find(',', i);
    return data.substr(i, e == std::string::npos ? std::string::npos : e - i);
  };
  return "quest=" + field("quest") + " weeks=" + field("weeks") + " questWeek=" + field("questWeek") + " bytes=" +
         std::to_string(data.size());
}

std::string nativeBsgLoadGameString(ScriptContext& ctx) {
  if (!ctx.users) return {};
  std::string s = ctx.users->loadSave(saveSlot(ctx));
  ctx.log.write("[save] load slot " + std::to_string(saveSlot(ctx)) + ": " + (s.empty() ? "empty" : saveSummary(s)));
  if (!s.empty()) ctx.gameStateTouched = true;
  return s;
}

void nativeBsgSaveGameString(ScriptContext& ctx, std::string data) {
  if (!ctx.users || ctx.users->current() < 0) return;
  if (!ctx.users->saveSave(data, saveSlot(ctx))) ctx.log.write("[user] bsgSaveGameString: could not write the save");
  else {
    ctx.gameStateTouched = true;
    ctx.log.write("[save] saved slot " + std::to_string(saveSlot(ctx)) + ": " + saveSummary(data));
  }
}

// The first MainMenu in the modal chain, front to back.
game::choc2::MainMenuWindow* findMainMenu(ui::Window* w) {
  if (!w) return nullptr;
  if (auto* m = dynamic_cast<game::choc2::MainMenuWindow*>(w)) return m;
  for (const auto& c : w->children())
    if (auto* m = findMainMenu(c.get())) return m;
  return nullptr;
}

// DoIntro: the MainMenu window's intro (games/choc2/MainMenuWindow.h). With no MainMenu in the top modal it is logged, not
// silent.
void nativeDoIntro(ScriptContext& ctx) {
  auto* m = findMainMenu(ctx.wm.topModal());
  if (!m || !m->doIntro()) ctx.log.write("[native-warning] DoIntro: no MainMenu with an intro_button in the top modal");
}

// bsgAddSprite(t): one sprite or building of the Port being built (games/choc2/Port.h). The table is read alone, without the
// window default style.
void nativeBsgAddSprite(ScriptContext& ctx, TableArg t) {
  game::choc2::Port* port = game::choc2::Port::underConstruction(ctx.wm);
  if (!port) {
    if (ctx.warned.insert("bsgAddSprite-outside").second) ctx.log.write("[native-warning] bsgAddSprite outside a Port");
    return;
  }
  const LuaStyle view = LuaStyle::plainView(t.L, t.index, &ctx.lua);
  port->addSprite(view);
}

// bsgSin(degrees), bsgCos(degrees): the recipe book (ui/recipebook_recipe.lua) passes `i * 360 / recipeSize` and uses
// `centre + radius * sin` for x and `centre - radius * cos` for y, so slot 0 is at the top: degrees (inferred from that).
double nativeBsgSin(ScriptContext&, double degrees) { return std::sin(degrees * 3.14159265358979323846 / 180.0); }
double nativeBsgCos(ScriptContext&, double degrees) { return std::cos(degrees * 3.14159265358979323846 / 180.0); }

// SetCharacterMood(name, mood): every live Character with that name, in every modal.
void nativeSetCharacterMood(ScriptContext& ctx, std::string name, double mood) {
  std::function<void(ui::Window&)> walk = [&](ui::Window& w) {
    if (auto* c = dynamic_cast<game::choc2::Character*>(&w); c && c->characterName() == name) c->setMood(mood);
    for (const auto& child : w.children()) walk(*child);
  };
  for (const auto& m : ctx.wm.modals()) walk(*m);
}


// InitiateTravel(leg): a route leg `{weeks, type, {x,y}, {x,y}, ...}` (sim/travel.lua:78). Chocolatier 3's native of the same
// name takes a table of path, type and events instead, so this one replaces it. With no MapTravel it logs and does nothing.
void nativeInitiateTravel(ScriptContext& ctx, TableArg leg) {
  if (!leg.valid()) return;
  auto* travel = game::choc2::MapTravel::current(ctx.wm);
  if (!travel) {
    ctx.log.write("[native-warning] InitiateTravel: there is no MapTravel");
    return;
  }
  const LuaStyle view = LuaStyle::plainView(leg.L, leg.index, &ctx.lua);
  std::vector<gfx::PointF> points;
  for (int i = 1, n = view.size(); i <= n; ++i)
    if (auto e = view.element(i))
      points.push_back({static_cast<float>(e->numberAt(1).value_or(0)), static_cast<float>(e->numberAt(2).value_or(0))});
  travel->initiateTravel(std::move(points), static_cast<int>(view.numberOr("weeks", 1)), view.stringOr("type"));
}

// TravelOneWeek(type): one week of the leg begun by InitiateTravel.
void nativeTravelOneWeek(ScriptContext& ctx, std::string type) {
  if (auto* travel = game::choc2::MapTravel::current(ctx.wm)) {
    travel->travelOneWeek(type);
  } else if (ctx.warned.insert("TravelOneWeek-no-map").second) {
    ctx.log.write("[native-warning] TravelOneWeek: there is no MapTravel");
  }
}

// StillTravelling(): true while a week animates. False with no MapTravel, so the Lua's wait loop cannot hang if the map died.
bool nativeStillTravelling(ScriptContext& ctx) {
  auto* travel = game::choc2::MapTravel::current(ctx.wm);
  return travel && travel->stillTravelling();
}

// StartFireworks(): the port's five TFireworks (docs/re/choc2-port-binary.md 7), at the layout slot this call sits in.
// Replaces Chocolatier 3's FireworksWindow binding (rebindRaw keeps the boot-trace native list stable); with no Port
// the request is logged once as before.
int nativeStartFireworks(lua_State* L) {
  ScriptContext& ctx = contextOf(L);
  if (game::choc2::Port* port = game::choc2::Port::current(ctx.wm)) {
    port->startFireworks();
    return 0;
  }
  if (ctx.stubs.hit("StartFireworks")) ctx.log.write("[unimplemented] StartFireworks (no Port is open)");
  return 0;
}

// StopPulsing(): hides the port's building labels and ends its intro pulse (docs/re/choc2-port-binary.md 1.5). No Port, no effect.
void nativeStopPulsing(ScriptContext& ctx) {
  if (game::choc2::Port* port = game::choc2::Port::current(ctx.wm)) port->stopPulsing();
}
// ResetColorMap(): the next Map made rebuilds the visited-ports colour picture (TMap::ResetColorMap).
void nativeResetColorMap(ScriptContext&) { game::choc2::Map::resetColorMap(); }

}  // namespace

const char* const* choc2ApiNames(std::size_t& count) {
  count = sizeof(kApiNames) / sizeof(kApiNames[0]);
  return kApiNames;
}

void registerChoc2Natives(lua_State* L, ScriptContext& ctx) {
  registerStubRows(L, ctx, kStubRows, sizeof(kStubRows) / sizeof(kStubRows[0]));

  bind<nativeNoop>(L, ctx, "bsgRandomSeed");
  bind<nativeNoop>(L, ctx, "bsgLuaGarbageCollect");
  bind<nativeNoop>(L, ctx, "bsgTriggerOffer");
  bind<nativeNoop>(L, ctx, "bsgTriggerAd");
  bind<nativeNoopString>(L, ctx, "OpenBrowser");
  bind<nativeSetUserName>(L, ctx, "SetUserName");
  bind<nativeGetCurrentGameMode>(L, ctx, "GetCurrentGameMode");
  bind<legacySetCurrentGameMode>(L, ctx, "SetCurrentGameMode");
  bind<nativeBsgDollars>(L, ctx, "bsgDollars");
  bind<nativeBsgDate>(L, ctx, "bsgDate");
  bind<nativeBsgShortDate>(L, ctx, "bsgShortDate");
  bind<legacyTimeStamp>(L, ctx, "bsgTimeStamp");
  bind<nativeDoIntro>(L, ctx, "DoIntro");
  bind<nativeBsgLoadGameString>(L, ctx, "bsgLoadGameString");
  bind<nativeBsgSaveGameString>(L, ctx, "bsgSaveGameString");
  bind<legacyNextSaveFile>(L, ctx, "bsgNextSaveFileName");

  registerChoc2LedgerNatives(L, ctx);  // DynamicWindow, Ledger and their natives
  registerChoc2LogoNatives(L, ctx);    // LogoEditor and its natives
  registerChoc2AudioNatives(L, ctx);  // PlaySound, LoadSound (the trade slice)
  registerChoc2FactoryNatives(L, ctx);  // FactoryGame, FactoryStatus and their natives (docs/design/choc2-minigames.md)
  registerChoc2DiceNatives(L, ctx);   // DiceGame and its natives
  registerChoc2BonusNatives(L, ctx);  // BonusGame (a CratesGame variant)
  bind<nativeBsgAddSprite>(L, ctx, "bsgAddSprite");
  bind<nativeBsgSin>(L, ctx, "bsgSin");
  bind<nativeBsgCos>(L, ctx, "bsgCos");
  bind<nativeSetCharacterMood>(L, ctx, "SetCharacterMood");
  bind<nativeStopPulsing>(L, ctx, "StopPulsing");
  rebindRaw(L, ctx, "StartFireworks", &nativeStartFireworks);  // replaces Chocolatier 3's FireworksWindow binding
  bind<nativeResetColorMap>(L, ctx, "ResetColorMap");
  bind<nativeInitiateTravel>(L, ctx, "InitiateTravel");
  bind<nativeTravelOneWeek>(L, ctx, "TravelOneWeek");
  bind<nativeStillTravelling>(L, ctx, "StillTravelling");

  registerAliasRows(L, ctx, kAliasRows, sizeof(kAliasRows) / sizeof(kAliasRows[0]));

  // The window classes that exist. Frame is drawn from its style keys by an inference (games/choc2/FrameWindow.h); Options,
  // HighScores and Credits are registered below by registerChoc2MenuNatives.
  ctx.wm.addWindowType("Frame", [](ui::WindowManager& wm) { return std::make_unique<game::choc2::FrameWindow>(wm); });
  ctx.wm.addWindowType("Port", [&ctx](ui::WindowManager& wm) {
    return std::make_unique<game::choc2::Port>(wm, ctx.rng, ctx.log, game::worldOf(ctx));
  });
  ctx.wm.addWindowType("Character", [](ui::WindowManager& wm) { return std::make_unique<game::choc2::Character>(wm); });
  ctx.wm.addWindowType("Map", [](ui::WindowManager& wm) { return std::make_unique<game::choc2::Map>(wm); });
  ctx.wm.addWindowType("MapTravel", [&ctx](ui::WindowManager& wm) { return std::make_unique<game::choc2::MapTravel>(wm, ctx); });
  ctx.wm.addWindowType("Sprite", [&ctx](ui::WindowManager& wm) { return std::make_unique<game::choc2::SpriteWindow>(wm, ctx); });
  ctx.wm.addWindowType("AnimationWindow", [](ui::WindowManager& wm) { return std::make_unique<game::choc2::AnimationWindow>(wm); });
  ctx.wm.addWindowType("MainMenu", [](ui::WindowManager& wm) { return std::make_unique<game::choc2::MainMenuWindow>(wm); });
  registerChoc2MenuNatives(L, ctx);  // Options, HighScores, Credits and their natives
}

void registerChoc2Globals(lua_State* L) {
  for (const GlobalRow& g : kGlobals) {
    lua_pushnumber(L, g.value);
    lua_setglobal(L, g.name);
  }
}

void registerChoc2WindowTypes(lua_State* L, ScriptContext& ctx) {
  for (const char* name : kWindowTypes) addWindowType(L, ctx, name);
  ctx.wm.setSoundNameResolver(choc2SoundPath);  // Rollover `rolloverSound` names are PlaySound names
}

}  // namespace choco::script
