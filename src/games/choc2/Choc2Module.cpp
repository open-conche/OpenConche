#include "games/choc2/Choc2Module.h"

#include "assets/AssetFS.h"
#include "games/choc2/Session.h"
#include "games/choc3/Choc3Module.h"
#include "games/choc2/Choc2Natives.h"

namespace choco::games::choc2 {
namespace {

// Chocolatier 2 inherits every Chocolatier 3 native and adds its own rows on top.
class Choc2Module : public script::GameModule {
 public:
  const script::GameProfile& profile() const override {
    // Chocolatier 2 defines no AskQuit: its quit dialog is the global QuitGame that mainmenu.lua sets. The dialog asks "save
    // your progress and quit?" (confirm_quit) yet QuitGame only posts kQuitNow and never calls SaveGame, so the save must be
    // the quit handler's: the exit save runs gSim:SaveGame() (inferred; a real session showed no save on Pause > Quit, and
    // the last autosave, up to 4 minutes old, came back with a finished mission active again).
    static const script::GameProfile p{"choc2", "Chocolatier2", "QuitGame", "QuitGame()", "ui/yndialog.lua",
                                       "if gSim then gSim:SaveGame() end", "Chocolatier 2",
                                       /*versionFromTitle=*/true, /*pauseDialogOnFocusLoss=*/false,
                                       {/*fallThroughPoint=*/true, /*popupFader=*/true, /*cancelDefault=*/true,
                                        /*spriteHitAlpha=*/192, /*rollover=*/true},
                                       "gTravelActive", "gQuitActive", {"ui/portview.lua", "ui/mapview.lua"},
                                       /*volumeDefaults=*/{750, 750, 750},
                                       /*typingSound=*/{},
                                       /*bootCursor=*/"image/cursor"};  // the C2 Main sets the arrow after the window title (TTexture::GetSimple + SetCursor(1,1))
    return p;
  }
  bool claims(const assets::AssetFS& fs, const std::string& settingsGame, std::string& why) const override {
    if (settingsGame != "chocolatier" || !fs.exists("ui/uihelp.lua")) return false;
    why = "settings.xml game=chocolatier, ui/uihelp.lua";
    return true;
  }
  // The ambient beds (sfx/*_loop.ogg) that exist in the game's sfx folder.
  std::vector<audio::SoundCache::Request> prefetch() const override {
    std::vector<audio::SoundCache::Request> list;
    for (const char* n : {"chocolate_shop", "farm", "marketplace", "metropolitan", "other", "port", "remote", "saloon",
                          "seaside", "village"})
      list.push_back({std::string("sfx/") + n + "_loop.ogg", {}});
    return list;
  }
  void attach(script::ScriptContext& ctx) const override { ctx.gameState = std::make_unique<game::choc2::Choc2World>(ctx); }
  void registerStubs(lua_State* L, script::ScriptContext& ctx) const override { choc3::module().registerStubs(L, ctx); }
  void registerCoreExtras(lua_State* L, script::ScriptContext& ctx) const override { choc3::module().registerCoreExtras(L, ctx); }
  void registerContentExtras(lua_State* L, script::ScriptContext& ctx) const override {
    choc3::module().registerContentExtras(L, ctx);
  }
  void registerNatives(lua_State* L, script::ScriptContext& ctx) const override {
    choc3::module().registerNatives(L, ctx);
    script::registerChoc2Natives(L, ctx);
  }
  void registerAppWindows(ui::WindowManager& wm, script::ScriptContext& ctx, const script::AppWindowEnv& env) const override {
    choc3::module().registerAppWindows(wm, ctx, env);
  }
  void appendDiagnostics(script::ScriptContext& ctx, std::string& out) const override {
    choc3::module().appendDiagnostics(ctx, out);
  }
  void registerWindowTypes(lua_State* L, script::ScriptContext& ctx) const override {
    choc3::module().registerWindowTypes(L, ctx);
    script::registerChoc2WindowTypes(L, ctx);
  }
  void registerGlobals(lua_State* L, script::ScriptContext&) const override {
    script::registerChoc2Globals(L);  // ui/uiconstants.lua builds `ui` from these
  }
};

}  // namespace

const script::GameModule& module() {
  static const Choc2Module m;
  return m;
}

}  // namespace choco::games::choc2
