// The game-level natives (docs/re/ledger-and-game-natives.md): the ledger, movies, and the offline score submission.
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

#include "games/choc3/HiscoreWindow.h"
#include "sdk/Users.h"
#include "games/common/World.h"
#include "script/ScriptHost.h"
#include "games/choc3/Choc3Natives.h"

namespace choco::script {

namespace {

// ---- the ledger ----
// All registered at boot. With no ledger they do nothing (the original niled them while there was none).

game::LedgerWindow* ledgerOf(ScriptContext& ctx) { game::World* w = game::worldOf(ctx);
  return w ? w->ledger.get() : nullptr;
}

// Raw so that the ledger's script runs on the calling thread.
int nativeGrabLedger(lua_State* L) {
  ScriptContext& ctx = contextOf(L);
  if (game::World* w = game::worldOf(ctx)) w->ledger.grab(L);
  return 0;
}

void nativeReleaseLedger(ScriptContext& ctx) {
  if (game::World* w = game::worldOf(ctx)) w->ledger.release();
}

void nativeRealUpdateLedger(ScriptContext& ctx, std::string mode, std::string data) {
  if (auto* l = ledgerOf(ctx)) l->update(mode, data);
}

void nativeSetLedgerQuestIndicator(ScriptContext& ctx, std::string mode) {
  if (auto* l = ledgerOf(ctx)) l->setQuestIndicator(mode);
}

void nativeAnimateProduction(ScriptContext& ctx) {
  if (auto* l = ledgerOf(ctx)) l->animateProduction();
}

// ---- movies: skipped, so DisplaySplash shows the still image ----

bool nativeStartMovie(ScriptContext& ctx, std::string path, std::optional<bool>, std::optional<bool>) {
  if (ctx.stubs.hit("StartMovie")) ctx.log.write("[unimplemented] StartMovie (movies are not played; '" + path + "' is skipped)");
  return false;
}

bool nativeIsMoviePlaying(ScriptContext&) { return false; }

void nativeStopMovie(ScriptContext&) {}

// ---- score submission: offline ----

// A Lua single-quoted string body.
std::string luaEscape(const std::string& s) {
  std::string out;
  for (unsigned char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '\'': out += "\\'"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      default:
        if (c < 32 || c == 127) {  // three digits, so a digit that follows is not read as part of the escape
          char esc[8];
          std::snprintf(esc, sizeof esc, "\\%03d", c);
          out += esc;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out;
}

// Lua source that sets the submit dialog's variables. No online accounts and no eligible-score lookup: both empty/zero.
std::string nativeGetLuaServerSubmitSetupVars(ScriptContext& ctx, bool medalsMode) {
  const std::string name = ctx.users ? ctx.users->pref("_anonusername", "") : std::string();
  std::string s = "gNameEdit='" + luaEscape(name) + "'; gAccountEdit=''; gPassEdit=''; gRemember=false; ";
  s += medalsMode ? "gMedalsMode=true;" : "gMedalsMode=false;gEligibleScore=0";
  return s;
}

// Nothing is sent. The dialog must not stay in submit mode, so SwitchModes(false) runs at once.
void nativeSubmitToServer(ScriptContext& ctx, std::string, std::string, std::string, std::optional<bool>, std::optional<bool>) {
  if (ctx.stubs.hit("SubmitToServer")) ctx.log.write("[unimplemented] SubmitToServer (offline; nothing is submitted)");
  if (ctx.host) ctx.host->runString("SwitchModes(false)");
}

// SubmitWindow (TServerSubmit): the base window keys only; its controls are found by name by the scripts.
class SubmitWindow : public ui::Window {
 public:
  explicit SubmitWindow(ui::WindowManager& wm) : Window(wm) {}
  std::string_view typeName() const override { return "SubmitWindow"; }
};

template <lua_CFunction Fn>
int guarded(lua_State* L) {
  try {
    return Fn(L);
  } catch (const std::exception& e) {
    detail::logNativeError(L, contextOf(L), nativeNameOf(L), e.what());
  } catch (...) {
    detail::logNativeError(L, contextOf(L), nativeNameOf(L), "unknown exception");
  }
  return 0;
}

}  // namespace

void registerGameNatives(lua_State* L, ScriptContext& ctx) {
  ScriptContext* c = &ctx;
  ctx.wm.addWindowType("DookieDropper", [c](ui::WindowManager& wm) { return std::make_unique<game::DookieDropper>(wm, *c); });
  ctx.wm.addWindowType("LedgerWindow", [c](ui::WindowManager& wm) { return std::make_unique<game::LedgerWindow>(wm, *c); });
  // Not in the generated type list, so its Lua constructor global is made here the same way (ui/hiscore_submit.lua uses it).
  ctx.wm.addWindowType("SubmitWindow", [](ui::WindowManager& wm) { return std::make_unique<SubmitWindow>(wm); });
  addWindowType(L, ctx, "SubmitWindow");

  game::registerHiscoreNatives(L, ctx);
  bindRaw(L, ctx, "GrabLedger", &guarded<nativeGrabLedger>);
  bind<nativeReleaseLedger>(L, ctx, "ReleaseLedger");
  bind<nativeRealUpdateLedger>(L, ctx, "RealUpdateLedger");
  bind<nativeSetLedgerQuestIndicator>(L, ctx, "SetLedgerQuestIndicator");
  bind<nativeAnimateProduction>(L, ctx, "AnimateProduction");
  bind<nativeStartMovie>(L, ctx, "StartMovie");
  bind<nativeIsMoviePlaying>(L, ctx, "IsMoviePlaying");
  bind<nativeStopMovie>(L, ctx, "StopMovie");
  bind<nativeSubmitToServer>(L, ctx, "SubmitToServer");
  bind<nativeGetLuaServerSubmitSetupVars>(L, ctx, "GetLuaServerSubmitSetupVars");
}

}  // namespace choco::script
