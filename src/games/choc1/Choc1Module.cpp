#include "games/choc1/Choc1Module.h"

#include "assets/AssetFS.h"
#include "games/choc1/Choc1AudioNatives.h"
#include "games/choc1/FactoryGame.h"
#include "games/choc1/Choc1Natives.h"
#include "games/choc1/Choc1World.h"
#include "games/choc3/Choc3Module.h"

namespace choco::games::choc1 {
namespace {

// Chocolatier 1 inherits Chocolatier 3's natives and window classes (HiscoreWindow, CreditsWindow, XSellWindow, OptionsWindow,
// MapWindow ... until it gets its own) and binds its own rows after them (docs/re/choc1-fit.md).
class Choc1Module : public script::GameModule {
 public:
  const script::GameProfile& profile() const override {
    // docs/re/choc1-fit.md section 7. mainmenu.lua sets QuitGame (a yes/no dialog that posts kQuitNow), like Chocolatier 2; the
    // exe's own close handler runs it and its exit runs `if bsutil then bsutil.SaveGame() end`. Neither travelGlobal nor
    // quitGlobal exists in its Lua (empty = none). The window quirks are docs/re/choc1-rollover-and-quirks.md section 9:
    // the C2-style fall-through skew, no popup fader (the Rollover popup is instant), Enter has no Cancel to press, no sprite hit
    // test, and the Rollover keys of the TRollover class.
    static const script::GameProfile p{"choc1", "Chocolatier1", "QuitGame", "QuitGame()", "ui/yndialog.lua",
                                       "if bsutil then bsutil.SaveGame() end", "Chocolatier", false, false,
                                       /*quirks=*/{/*fallThroughPoint=*/true, /*popupFader=*/false, /*cancelDefault=*/false,
                                                 /*spriteHitAlpha=*/0, /*rollover=*/true,
                                                 /*keepLastOverflowChar=*/true},
                                       "", "", {"ui/portview.lua", "ui/map.lua"},
                                       /*volumeDefaults=*/{750, 750, 750},
                                       // Inference: Chocolatier 1 ships audio/sfx/typing_01..05.ogg; the class that plays
                                       // them and how it picks are not found yet, so this reuses the LCG pick modulo 5.
                                       /*typingSound=*/{"audio/sfx/typing_", 5, ".ogg"},
                                       /*bootCursor=*/"cursor/cursor"};  // the main loop sets the arrow right after the window title
    return p;
  }
  bool claims(const assets::AssetFS& fs, const std::string& settingsGame, std::string& why) const override {
    if (settingsGame != "chocolatier" || !fs.exists("data/ports.lua")) return false;
    why = "settings.xml game=chocolatier, data/ports.lua";
    return true;
  }
  // The Chocolatier 3 World (its port, map and ledger natives) plus the music queue, money counter and pulsers.
  void attach(script::ScriptContext& ctx) const override { ctx.gameState = std::make_unique<game::choc1::Choc1World>(ctx); }
  // The nine PlaySound aliases and the sounds the menu and the money counter use.
  std::vector<audio::SoundCache::Request> prefetch() const override { return script::choc1Prefetch(); }
  void registerStubs(lua_State* L, script::ScriptContext& ctx) const override { choc3::module().registerStubs(L, ctx); }
  void registerCoreExtras(lua_State* L, script::ScriptContext& ctx) const override { choc3::module().registerCoreExtras(L, ctx); }
  void registerContentExtras(lua_State* L, script::ScriptContext& ctx) const override {
    choc3::module().registerContentExtras(L, ctx);
  }
  void registerNatives(lua_State* L, script::ScriptContext& ctx) const override {
    choc3::module().registerNatives(L, ctx);
    script::registerChoc1Natives(L, ctx);
  }
  void registerAppWindows(ui::WindowManager& wm, script::ScriptContext& ctx, const script::AppWindowEnv& env) const override {
    choc3::module().registerAppWindows(wm, ctx, env);
  }
  void appendDiagnostics(script::ScriptContext& ctx, std::string& out) const override {
    choc3::module().appendDiagnostics(ctx, out);
  }
  void registerWindowTypes(lua_State* L, script::ScriptContext& ctx) const override {
    choc3::module().registerWindowTypes(L, ctx);
    script::registerChoc1WindowTypes(L, ctx);
  }
  // The main loop's key-27 check (choc1-natives.md section 8): a running factory mini-game is looked up in the modal
  // chain (the top modal named `minigame`) and paused whatever has the focus; the helper skips the start dialog and an
  // already-paused game.
  void onEscapeKey(script::ScriptContext& ctx) const override {
    if (auto* g = game::choc1::FactoryGame::inChain(ctx.wm)) g->escapePause();
  }
  // registerGlobals: none. Chocolatier 3 sets none either, and the six screen globals are Chocolatier 2's (it hard-codes 800x600).
};

}  // namespace

const script::GameModule& module() {
  static const Choc1Module m;
  return m;
}

}  // namespace choco::games::choc1
