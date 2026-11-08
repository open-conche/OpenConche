// StartGame, PauseGame, ResumeGame and GetScore (docs/re/chocolate-factory.md 12.2) and the factory window types. The
// natives act on the newest live factory window and do nothing without one (GetScore gives 0). Everything that runs Lua for
// the factories lives here: filling the sibling `information` window with ui/game_pause.lua or ui/chocolate_gameover.lua.
#include <exception>
#include <string>

#include "games/choc3/Particles.h"
#include "games/choc3/SlotMachine.h"
#include "games/common/factory/Chocolate.h"
#include "games/choc3/factory/Coffee.h"
#include "games/choc3/factory/RecipeDesigner.h"
#include "script/LuaApi.h"
#include "script/Bind.h"
#include "script/RunFile.h"
#include "script/ScriptHost.h"
#include "games/choc3/Choc3Natives.h"
#include "ui/WindowManager.h"
#include "ui/widgets/Widgets.h"

namespace choco::script {

namespace {

// FillWindow's way of loading a script into a window: the window loses its children (destroyed at the next
// clearDestroyed, so the button whose command is running is safe), then the file runs with the window as the one being
// built. `pauseText` is stored in the global gPauseText first (game_pause.lua reads it and clears it).
game::FactoryWindow::FillFn makeFill(ScriptContext* ctx) {
  return [ctx](lua_State* L, ui::Window& target, const std::string& file, const std::string* pauseText) {
    ui::WindowManager& wm = ctx->wm;
    while (!target.children().empty()) wm.destroyWindow(*target.children().front());
    if (pauseText) {
      lua_pushlstring(L, pauseText->data(), pauseText->size());
      lua_setglobal(L, "gPauseText");
    }
    runFileInto(*ctx, L, target, file);
  };
}

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

game::FactoryWindow* activeOrLog(lua_State* L) {
  ScriptContext& ctx = contextOf(L);
  game::FactoryWindow* f = game::FactoryWindow::active();
  if (!f && ctx.stubs.hit(nativeNameOf(L))) ctx.log.write(std::string("[unimplemented] ") + nativeNameOf(L) + " (no factory window is open)");
  return f;
}

int nativeStartGame(lua_State* L) {
  if (auto* f = activeOrLog(L)) f->startGame(L);
  return 0;
}

int nativePauseGame(lua_State* L) {
  if (auto* f = activeOrLog(L)) {
    const char* key = lua_type(L, 1) == LUA_TSTRING ? lua_tostring(L, 1) : "";
    f->pauseGame(L, key);
  }
  return 0;
}

int nativeResumeGame(lua_State* L) {
  if (auto* f = activeOrLog(L)) f->resumeGame(L);
  return 0;
}

int nativeGetScore(lua_State* L) {
  auto* f = activeOrLog(L);
  lua_pushnumber(L, f ? static_cast<lua_Number>(f->score()) : 0);
  return 1;
}

// ---- the kitchen's design mode: RecipeDesigner's natives (docs/re/ledger-and-game-natives.md 10.3, 10.5) ----

game::RecipeDesigner* designerOrLog(ScriptContext& ctx, const char* native) {
  game::RecipeDesigner* d = game::RecipeDesigner::active();
  if (!d && ctx.stubs.hit(native)) ctx.log.write(std::string("[unimplemented] ") + native + " (no RecipeDesigner window is open)");
  return d;
}

void nativeRandomDesign(ScriptContext& ctx) {
  if (auto* d = designerOrLog(ctx, "RandomDesign")) d->randomDesign();
}

void nativeSetRecipeType(ScriptContext& ctx, std::string category) {
  if (auto* d = designerOrLog(ctx, "SetRecipeType")) d->setRecipeType(category);
}

int nativeSetLayer(ScriptContext& ctx, int layer, int index) {
  auto* d = designerOrLog(ctx, "SetLayer");
  return d ? d->setLayer(layer, index) : 0;
}

std::string nativeGetLayerImage(ScriptContext& ctx, int layer) {
  auto* d = designerOrLog(ctx, "GetLayerImage");
  return d ? d->layerImage(layer) : std::string();
}

// {r,g,b,a} from the table's [1]..[4], a missing or non-number field taking `def`.
gfx::ColorF colorFromTable(TableArg t, float def) {
  float v[4];
  for (int i = 0; i < 4; ++i) {
    lua_rawgeti(t.L, t.index, i + 1);
    v[i] = lua_isnumber(t.L, -1) ? static_cast<float>(lua_tonumber(t.L, -1)) : def;
    lua_pop(t.L, 1);
  }
  return {v[0], v[1], v[2], v[3]};
}

// BTSetTint: the named BitmapTint under the top modal; each missing component is 1.0.
void nativeBTSetTint(ScriptContext& ctx, std::string name, TableArg color) {
  if (!color.valid()) return;
  if (auto* w = dynamic_cast<ui::BitmapTintWindow*>(ctx.wm.findInTopModal(name))) w->setTint(colorFromTable(color, 1.0f));
}

// SetRectangleColor: the named Rectangle under the top modal; each missing component is 0.0 (so no alpha means invisible).
void nativeSetRectangleColor(ScriptContext& ctx, std::string name, TableArg color) {
  if (!color.valid()) return;
  if (auto* w = dynamic_cast<ui::RectangleWindow*>(ctx.wm.findInTopModal(name))) w->setColor(colorFromTable(color, 0.0f));
}

}  // namespace

void registerFactoryNatives(lua_State* L, ScriptContext& ctx) {
  ScriptContext* c = &ctx;
  ctx.wm.addWindowType("ChocolateFactory", [c](ui::WindowManager& wm) {
    return std::make_unique<game::ChocolateFactory>(wm, *c, makeFill(c));
  });
  ctx.wm.addWindowType("CoffeeFactory", [c](ui::WindowManager& wm) {
    return std::make_unique<game::CoffeeFactory>(wm, *c, makeFill(c));
  });
  ctx.wm.addWindowType("RecipeDesigner", [c](ui::WindowManager& wm) { return std::make_unique<game::RecipeDesigner>(wm, *c); });
  bind<nativeRandomDesign>(L, ctx, "RandomDesign");
  bind<nativeSetRecipeType>(L, ctx, "SetRecipeType");
  bind<nativeSetLayer>(L, ctx, "SetLayer");
  bind<nativeGetLayerImage>(L, ctx, "GetLayerImage");
  bind<nativeBTSetTint>(L, ctx, "BTSetTint");
  bind<nativeSetRectangleColor>(L, ctx, "SetRectangleColor");
  bindRaw(L, ctx, "StartGame", &guarded<nativeStartGame>);
  bindRaw(L, ctx, "PauseGame", &guarded<nativePauseGame>);
  bindRaw(L, ctx, "ResumeGame", &guarded<nativeResumeGame>);
  bindRaw(L, ctx, "GetScore", &guarded<nativeGetScore>);
  game::registerStubNatives(L, ctx);
  game::registerParticleNatives(L, ctx);
}

}  // namespace choco::script
