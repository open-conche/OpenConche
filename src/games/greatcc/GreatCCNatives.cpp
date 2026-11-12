#include "games/greatcc/GreatCCNatives.h"

#include <memory>

#include "games/greatcc/meta/VPlotWindow.h"
#include "games/greatcc/meta/VQuestWindow.h"
#include "games/greatcc/meta/VRewardNotify.h"
#include "games/greatcc/meta/VRewardsWindow.h"
#include "games/greatcc/shop/ShopWindow.h"
#include "games/greatcc/shop/VShopHud.h"
#include "games/greatcc/ui/BLayeredWindow.h"
#include "games/greatcc/ui/KDial.h"
#include "games/greatcc/ui/RActiveTextLayer.h"
#include "games/greatcc/ui/RTimer.h"
#include "games/greatcc/ui/KSplit9Image.h"
#include "games/greatcc/ui/RBounceWindow.h"
#include "games/greatcc/ui/RDisableButton.h"
#include "games/greatcc/ui/RScrollable.h"
#include "games/greatcc/ui/RShrinkText.h"
#include "games/greatcc/ui/RTextEditDisable.h"
#include "games/greatcc/scene/VSpriteWindow.h"
#include "games/greatcc/ui/RWindow.h"
#include "games/greatcc/ui/HiscoreWindow.h"
#include "games/greatcc/ui/TChoosePlayer.h"
#include "games/greatcc/ui/VChooseAchievements.h"
#include "games/greatcc/ui/TCredits.h"
#include "games/greatcc/ui/TMainMenu.h"
#include "games/greatcc/ui/TOptions.h"
#include "games/greatcc/ui/VHelpWindow.h"
#include "games/greatcc/ui/VTintWindow.h"
#include "games/greatcc/ui/VUpsellWindow.h"
#include "games/greatcc/ui/VWorldMap.h"
#include "games/greatcc/upgrade/VPrelevelWindow.h"
#include "games/greatcc/upgrade/VUpgradeList.h"
#include "games/greatcc/upgrade/VUpgradeTab.h"
#include "script/Loaders.h"
#include "script/ScriptHost.h"
#include "script/Stubs.h"
#include "ui/WindowManager.h"
#include "ui/widgets/InputWidgets.h"

namespace choco::script {

namespace {

const StubRow kStubRows[] = {
#define STUB(name, ret, args) {name, ret, args},
#define WINDOW_TYPE(name)
#define API_NAME(name)
#include "games/greatcc/generated/Stubs_greatcc.inc"
#undef STUB
#undef WINDOW_TYPE
#undef API_NAME
};

const char* const kWindowTypes[] = {
#define STUB(name, ret, args)
#define WINDOW_TYPE(name) name,
#define API_NAME(name)
#include "games/greatcc/generated/Stubs_greatcc.inc"
#undef STUB
#undef WINDOW_TYPE
#undef API_NAME
};

const char* const kApiNames[] = {
#define STUB(name, ret, args)
#define WINDOW_TYPE(name)
#define API_NAME(name) name,
#include "games/greatcc/generated/Stubs_greatcc.inc"
#undef STUB
#undef WINDOW_TYPE
#undef API_NAME
};

// RTransitionManager's constructor runs this text (docs/re/api-greatcc.tsv, DoTransition): Lua_DoTransition(type) yields the
// coroutine and later resumes it with a boolean; the loop waits for that boolean through Yield.
const char kDoTransition[] =
    "function DoTransition( type ) local ret = Lua_DoTransition( type ); "
    "while( ret ~= true and ret ~= false )do ret = Yield(); end return ret; end";

}  // namespace

const char* const* greatccApiNames(std::size_t& count) {
  count = sizeof(kApiNames) / sizeof(kApiNames[0]);
  return kApiNames;
}

void registerGreatccStubs(lua_State* L, ScriptContext& ctx) {
  registerStubRows(L, ctx, kStubRows, sizeof(kStubRows) / sizeof(kStubRows[0]));
}

void registerGreatccWindowTypes(lua_State* L, ScriptContext& ctx) {
  for (const char* type : kWindowTypes) addWindowType(L, ctx, type);
  // TSlider is the class of the built-in Slider, registered under a second name (docs/re/greatcc-boot-and-natives.md 3.1).
  // The window types of the finished slices replace their placeholders.
  ctx.wm.addWindowType("RWindow", [](ui::WindowManager& wm) { return std::make_unique<games::greatcc::RWindow>(wm); });
  ctx.wm.addWindowType("RBounceWindow",
                       [](ui::WindowManager& wm) { return std::make_unique<games::greatcc::RBounceWindow>(wm); });
  ctx.wm.addWindowType("VTintWindow", [](ui::WindowManager& wm) { return std::make_unique<games::greatcc::VTintWindow>(wm); });
  // Slice 3: the widget library and the menu screens. The ones that bind natives keep the context (it outlives the windows).
  using namespace games::greatcc;
  ScriptContext* c = &ctx;
  ctx.wm.addWindowType("RShrinkText", [](ui::WindowManager& wm) { return std::make_unique<RShrinkText>(wm); });
  ctx.wm.addWindowType("RDisableButton", [](ui::WindowManager& wm) { return std::make_unique<RDisableButton>(wm); });
  ctx.wm.addWindowType("RTextEditDisable", [](ui::WindowManager& wm) { return std::make_unique<RTextEditDisable>(wm); });
  ctx.wm.addWindowType("KSplit9Image", [](ui::WindowManager& wm) { return std::make_unique<KSplit9Image>(wm); });
  ctx.wm.addWindowType("BLayeredWindow", [](ui::WindowManager& wm) { return std::make_unique<BLayeredWindow>(wm); });
  ctx.wm.addWindowType("RScrollable", [](ui::WindowManager& wm) { return std::make_unique<RScrollable>(wm); });
  ctx.wm.addWindowType("VUpsellWindow", [](ui::WindowManager& wm) { return std::make_unique<VUpsellWindow>(wm); });
  ctx.wm.addWindowType("TMainMenu", [c](ui::WindowManager& wm) { return std::make_unique<TMainMenu>(wm, *c); });
  ctx.wm.addWindowType("TChoosePlayer", [c](ui::WindowManager& wm) { return std::make_unique<TChoosePlayer>(wm, *c); });
  ctx.wm.addWindowType("TOptions", [c](ui::WindowManager& wm) { return std::make_unique<TOptions>(wm, *c); });
  ctx.wm.addWindowType("TCredits", [c](ui::WindowManager& wm) { return std::make_unique<TCredits>(wm, *c); });
  ctx.wm.addWindowType("VHelpWindow", [c](ui::WindowManager& wm) { return std::make_unique<VHelpWindow>(wm, *c); });
  // Slice 7: the sprite windows.
  ctx.wm.addWindowType("VSpriteWindow", [c](ui::WindowManager& wm) { return std::make_unique<VSpriteWindow>(wm, *c); });
  ctx.wm.addWindowType("VObjectWindow", [c](ui::WindowManager& wm) { return std::make_unique<VObjectWindow>(wm, *c); });
  ctx.wm.addWindowType("VRegister", [c](ui::WindowManager& wm) { return std::make_unique<VRegister>(wm, *c); });
  // Slice 8: the world map.
  ctx.wm.addWindowType("VWorldMap", [c](ui::WindowManager& wm) { return std::make_unique<VWorldMap>(wm, *c); });
  // Slice 9: the shop screen and its HUD.
  ctx.wm.addWindowType("ShopWindow", [c](ui::WindowManager& wm) { return std::make_unique<ShopWindow>(wm, *c); });
  ctx.wm.addWindowType("VShopHud", [c](ui::WindowManager& wm) { return std::make_unique<VShopHud>(wm, *c); });
  ctx.wm.addWindowType("RTimer", [](ui::WindowManager& wm) { return std::make_unique<RTimer>(wm); });
  ctx.wm.addWindowType("KDial", [](ui::WindowManager& wm) { return std::make_unique<KDial>(wm); });
  ctx.wm.addWindowType("KSweepDial", [](ui::WindowManager& wm) { return std::make_unique<KSweepDial>(wm); });
  ctx.wm.addWindowType("RActiveTextLayer", [](ui::WindowManager& wm) { return std::make_unique<RActiveTextLayer>(wm); });
  // Slice 16: the upgrade shop.
  ctx.wm.addWindowType("VPrelevelWindow", [c](ui::WindowManager& wm) { return std::make_unique<VPrelevelWindow>(wm, *c); });
  ctx.wm.addWindowType("VUpgradeList", [c](ui::WindowManager& wm) { return std::make_unique<VUpgradeList>(wm, *c); });
  ctx.wm.addWindowType("VUpgradeTab", [c](ui::WindowManager& wm) { return std::make_unique<VUpgradeTab>(wm, *c); });
  // Slice 17: the diary, the quest panel and the achievement windows.
  ctx.wm.addWindowType("VPlotWindow", [c](ui::WindowManager& wm) { return std::make_unique<VPlotWindow>(wm, *c); });
  ctx.wm.addWindowType("VQuestWindow", [c](ui::WindowManager& wm) { return std::make_unique<VQuestWindow>(wm, *c); });
  ctx.wm.addWindowType("VRewardNotify", [c](ui::WindowManager& wm) { return std::make_unique<VRewardNotify>(wm, *c); });
  ctx.wm.addWindowType("VRewardsWindow", [c](ui::WindowManager& wm) { return std::make_unique<VRewardsWindow>(wm, *c); });
  // Slice 20: the achievement cheat window.
  ctx.wm.addWindowType("VChooseAchievements", [c](ui::WindowManager& wm) { return std::make_unique<VChooseAchievements>(wm, *c); });
  // Slice 19: the high-score screen.
  ctx.wm.addWindowType("HiscoreWindow", [c](ui::WindowManager& wm) { return std::make_unique<HiscoreWindow>(wm, *c); });
  ctx.wm.addWindowType("TSlider", [](ui::WindowManager& wm) { return std::make_unique<ui::SliderWindow>(wm); });
}

void registerGreatccGlobals(lua_State* L, ScriptContext& ctx) {
  if (loadBuffer(L, kDoTransition, "DoTransition") != 0 || lua_pcall(L, 0, 0, 0) != 0) {
    ctx.log.write(std::string("[engine-error] DoTransition: ") + lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

}  // namespace choco::script
