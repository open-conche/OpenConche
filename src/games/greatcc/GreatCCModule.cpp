#include "games/greatcc/GreatCCModule.h"

#include "assets/AssetFS.h"
#include "games/greatcc/GreatCCNatives.h"
#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/natives/StateNatives.h"
#include "games/greatcc/natives/TransitionNatives.h"
#include "games/greatcc/shop/ShopWindow.h"
#include "sdk/Users.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {
namespace {

// The Great Chocolate Chase (SDK 4.0, docs/design/greatcc.md). It inherits nothing from Chocolatier 3: its Lua sees the SDK
// natives the engine core binds for every game, its own rows and nothing of Chocolatier's.
class GreatCCModule : public script::GameModule {
 public:
  const script::GameProfile& profile() const override {
    // Section 3.3: the quit dialog is scripts/quitverify.lua. Its loaded chunk goes on the command queue, so WaitForMessage
    // calls it as Lua on Main's coroutine and DisplayDialog can yield (through dofile it could not: a C call would be in
    // between). The original runs it unless the quitting flag is set; here a second close request while it is up quits. No exit save: progress
    // is committed as it changes. No window quirks. Strings are always the English table; the title is its `app_title`.
    static const script::GameProfile p{
        "greatcc", "GreatChocolateChase", "AreQuitting",
        "if AreQuitting() then PostMessage(CreateNamedMessage(kQuitNow, \"quit\")) "
        "else table.insert(gCommandQueue, assert(loadfile(\"scripts/quitverify.lua\"))) end",
        "scripts/quitverify.lua", "", "The Great Chocolate Chase",
        /*versionFromTitle=*/false, /*pauseDialogOnFocusLoss=*/false, {}, "", "", {},
        /*volumeDefaults=*/{750, 750, 750},
        /*typingSound=*/{},
        /*bootCursor=*/"cursor/chocolate_cursor_2",
        /*mainloop=*/"scripts/mainloop.lua",
        /*stringsPath=*/"strings/en/strings.xml",
        /*titleString=*/"app_title",
        /*bootCursorHotX=*/0, /*bootCursorHotY=*/0,
        /*squaredGain=*/true};
    return p;
  }
  // settings.xml game=chocolateshop (both the Windows install and the Mac bundle ship it); a tree without settings.xml is
  // recognised by the three files its boot needs.
  bool claims(const assets::AssetFS& fs, const std::string& settingsGame, std::string& why) const override {
    if (settingsGame == "chocolateshop") {
      why = "settings.xml game=chocolateshop";
      return true;
    }
    if (settingsGame.empty() && fs.exists("scripts/mainloop.lua") && fs.exists("locations/defines.lua") &&
        fs.exists("values.xml")) {
      why = "scripts/mainloop.lua, locations/defines.lua, values.xml";
      return true;
    }
    return false;
  }
  void attach(script::ScriptContext& ctx) const override { ctx.gameState = std::make_unique<GreatCCWorld>(ctx); }
  void registerStubs(lua_State* L, script::ScriptContext& ctx) const override { script::registerGreatccStubs(L, ctx); }
  // The binding order is the original's (docs/re/greatcc-boot-and-natives.md 2.1): TSettings, values, levels, achievements,
  // then transitions. The world then reads its data files and the current user's state (LoadStateForCurrentUser at boot).
  void registerNatives(lua_State* L, script::ScriptContext& ctx) const override {
    bindSettingsNatives(L, ctx);
    bindValueNatives(L, ctx);
    bindLevelNatives(L, ctx);
    bindAchievementNatives(L, ctx);
    bindTransitionNatives(L, ctx);
    bindSoundNatives(L, ctx);
    bindGameStateNatives(L, ctx);
    if (GreatCCWorld* w = worldOf(ctx)) w->init(ctx);
  }
  // The main loop pauses a running game on Escape (docs/re/greatcc-boot-and-natives.md 1.2); the key then goes on as usual.
  void onEscapeKey(script::ScriptContext& ctx) const override {
    if (GreatCCWorld* w = worldOf(ctx)) w->pauseInsideGame();
  }
  // The F10 / `diag` lines (AGENTS.md "Diagnosing a hang"): the session's mode and progress, the shop window's tick state and the
  // transition queue, so a stuck screen shows what the game thinks it is doing.
  void appendDiagnostics(script::ScriptContext& ctx, std::string& out) const override {
    GreatCCWorld* w = worldOf(ctx);
    if (!w) return;
    const Settings& st = w->settings();
    out += "greatcc: user=" + (ctx.users && ctx.users->count() > 0 ? ctx.users->currentName() : std::string("(none)")) +
           " mode=" + std::to_string(st.mode) + " difficulty=" + std::to_string(st.difficulty) + " insideGame=" +
           std::to_string(st.insideGame ? 1 : 0) + " gameLoaded=" + std::to_string(st.gameLoaded ? 1 : 0) + " foreground=" +
           std::to_string(w->foreground() ? 1 : 0) + " cheatmode=" + std::to_string(cheatModeOn(ctx) ? 1 : 0) + " fixbugs=" +
           std::to_string(st.fixBugs ? 1 : 0) + "\n";
    out += "greatcc: level=" + std::to_string(w->levels().currentLevel()) + " highest=" + std::to_string(w->levels().highestComplete()) +
           " total=" + std::to_string(w->values().getInt("current_level_total", 0)) + " worth=" +
           std::to_string(w->values().getInt("current_worth", 0)) + " angry=" + std::to_string(w->values().getInt("angry_this_level", 0)) +
           " transitions=" + (w->transitions().active() ? "active" : "idle") + "\n";
    if (auto* shop = dynamic_cast<ShopWindow*>(ctx.wm.window(w->shopWindowId()))) {
      const ShopTickState& t = shop->tickState();
      out += "greatcc: shop id=" + std::to_string(shop->id()) + " endless=" + std::to_string(shop->endless() ? 1 : 0) +
             " customers active=" + std::to_string(shop->activeCustomers()) + " pending=" + std::to_string(shop->pendingCustomers()) +
             " levelOverStamp=" + std::to_string(t.levelOverStamp) + " tutorialInEffect=" +
             std::to_string(w->tutorials().tutorialInEffect() ? 1 : 0) + shop->detail() + "\n";
    } else {
      out += "greatcc: no shop window\n";
    }
  }
  void registerWindowTypes(lua_State* L, script::ScriptContext& ctx) const override {
    script::registerGreatccWindowTypes(L, ctx);
  }
  void registerGlobals(lua_State* L, script::ScriptContext& ctx) const override { script::registerGreatccGlobals(L, ctx); }
};

}  // namespace

const script::GameModule& module() {
  static const GreatCCModule m;
  return m;
}

}  // namespace choco::games::greatcc
