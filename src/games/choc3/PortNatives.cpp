// The port and character windows' registration and their Lua natives (docs/design/game-windows.md 3.6, 3.7, 3.9).
#include <functional>

#include "games/choc3/CharWindow.h"
#include "games/common/PortWindow.h"
#include "games/common/World.h"
#include "script/ScriptHost.h"
#include "games/choc3/Choc3Natives.h"

namespace choco::script {

namespace {

// Visits every live window of every modal (a covered port is still the port).
void forEachWindow(ui::WindowManager& wm, const std::function<void(ui::Window&)>& fn) {
  std::function<void(ui::Window&)> walk = [&](ui::Window& w) {
    fn(w);
    for (const auto& c : w.children()) walk(*c);
  };
  for (const auto& m : wm.modals()) walk(*m);
}

// PausePortAnimations(pause): the world's port window, found by id. No port: nothing happens.
void nativePausePortAnimations(ScriptContext& ctx, bool pause) {
  if (game::World* w = game::worldOf(ctx))
    if (game::PortWindow* port = w->port()) port->pauseAnimations(pause);
}

// SetMood(name, h): every live CharWindow with that name.
void nativeSetMood(ScriptContext& ctx, std::string name, double h) {
  forEachWindow(ctx.wm, [&](ui::Window& w) {
    if (auto* c = dynamic_cast<game::CharWindow*>(&w); c && c->characterName() == name) c->setMood(h);
  });
}

}  // namespace

void registerPortNatives(lua_State* L, ScriptContext& ctx) {
  ctx.wm.addWindowType("PortWindow", [&ctx](ui::WindowManager& wm) { return std::make_unique<game::PortWindow>(wm, ctx.rng, ctx.log, game::worldOf(ctx)); });
  ctx.wm.addWindowType("CharWindow", [](ui::WindowManager& wm) { return std::make_unique<game::CharWindow>(wm); });
  bind<nativePausePortAnimations>(L, ctx, "PausePortAnimations");
  bind<nativeSetMood>(L, ctx, "SetMood");
}

}  // namespace choco::script
