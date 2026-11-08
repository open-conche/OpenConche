#include "games/choc3/Choc3Module.h"

#include <cstddef>

#include "assets/AssetFS.h"
#include "games/choc3/AudioEvents.h"
#include "games/choc3/PlayerSave.h"
#include "games/common/MapWindow.h"
#include "games/common/World.h"
#include "script/ScriptContext.h"
#include "script/Bind.h"
#include "script/ScriptHost.h"
#include "games/choc3/Choc3Natives.h"
#include "script/Stubs.h"
#include "games/choc3/Choc3Api.h"

namespace choco::games::choc3 {
namespace {

class Choc3Module : public script::GameModule {
 public:
  const script::GameProfile& profile() const override {
    static const script::GameProfile p{
        "choc3", "Chocolatier3", "AskQuit", "AskQuit()", "", "if Player then Player:SaveGame() end", "Chocolatier",
        /*versionFromTitle=*/false, /*pauseDialogOnFocusLoss=*/true, {},
        "gTravelActive", "gQuitActive", {"ui/portview.lua", "ui/mapview.lua"},
        /*volumeDefaults=*/{750, 650, 500},
        /*typingSound=*/{"sfx/sfx_typing_", 4, ".ogg?group=1000"},
        // ChocoThreeMain (0001001c) loads cursor/cursor through Content::LoadCursor and passes it to
        // TPlatform::SetCursor(1,1,0) right before BSGSoundInit: the skinned arrow from boot, like Chocolatier 1 and 2.
        /*bootCursor=*/"cursor/cursor"};
    return p;
  }
  bool claims(const assets::AssetFS&, const std::string& settingsGame, std::string& why) const override {
    if (settingsGame != "chocolatier3") return false;
    why = "settings.xml game=chocolatier3";
    return true;
  }
  // Chocolatier 3 has two incompatible content sets: the community (Reforged) tree extends vanilla's ingredient code
  // tables (100 against 60 codes), so a player made with one crashes the shipped Lua under the other - sim/recipe.lua:23
  // dies on the first ingredient code the loaded table does not know (a custom product designed in the community tree).
  // The community tree keeps the historical "Chocolatier3" default (its existing players stay found); the shipped
  // content gets a leaf of its own, so the two never share a player store again. --userdir still overrides both.
  // The marker is the community store's first file (the vanilla trees have no community/ at all).
  std::string defaultUserLeaf(const assets::AssetFS& fs) const override {
    return fs.exists("community/account.lua") ? std::string("Chocolatier3") : std::string("Chocolatier3-Vanilla");
  }
  std::unique_ptr<const audio::EventTable> makeAudioEvents(const assets::AssetFS& fs) const override {
    return choc3::makeAudioEvents(fs);
  }
  void attach(script::ScriptContext& ctx) const override { ctx.gameState = std::make_unique<game::World>(ctx); }
  void registerStubs(lua_State* L, script::ScriptContext& ctx) const override { script::registerStubs(L, ctx); }
  void registerCoreExtras(lua_State* L, script::ScriptContext& ctx) const override { script::registerCalendarNatives(L, ctx); }
  void registerContentExtras(lua_State* L, script::ScriptContext& ctx) const override {
    script::registerContentLoaderNatives(L, ctx);
  }
  bool refuseSave(std::string_view data, std::string& why) const override {
    if (!game::isUninitialisedPlayerSave(data)) return false;
    why = "Player was never initialised";
    return true;
  }
  void registerNatives(lua_State* L, script::ScriptContext& ctx) const override {
    script::registerPortNatives(L, ctx);
    script::registerGameNatives(L, ctx);
    script::registerMapNatives(L, ctx);
    script::registerCratesNatives(L, ctx);
    script::registerFactoryNatives(L, ctx);
  }
  void registerAppWindows(ui::WindowManager& wm, script::ScriptContext& ctx, const script::AppWindowEnv& env) const override {
    game::registerMapWindow(wm, {env.sheets, env.rng, env.log, game::worldOf(ctx)});
  }
  void appendDiagnostics(script::ScriptContext& ctx, std::string& out) const override {
    game::appendChocolatierDiagnostics(ctx, out);
  }
  void registerWindowTypes(lua_State* L, script::ScriptContext& ctx) const override {
    std::size_t n = 0;
    const char* const* types = script::stubWindowTypes(n);
    for (std::size_t i = 0; i < n; ++i) script::addWindowType(L, ctx, types[i]);
  }
};

}  // namespace

const script::GameModule& module() {
  static const Choc3Module m;
  return m;
}

}  // namespace choco::games::choc3
