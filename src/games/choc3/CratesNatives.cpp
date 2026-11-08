// PrepareGame and SelectPrize (docs/re/ledger-and-game-natives.md 10.1). They act on the live CratesGame window.
#include <optional>

#include "games/common/Crates.h"
#include "script/ScriptHost.h"
#include "games/choc3/Choc3Natives.h"

namespace choco::script {

namespace {

double nativePrepareGame(ScriptContext& ctx, double bet) {
  game::CratesGame* g = game::CratesGame::active();
  if (!g) {
    if (ctx.stubs.hit("PrepareGame")) ctx.log.write("[unimplemented] PrepareGame (no CratesGame window is open)");
    return 0;
  }
  const unsigned b = bet > 0 ? (bet >= 4294967295.0 ? 4294967295u : static_cast<unsigned>(bet)) : 0u;
  return g->prepare(b);
}

double nativeSelectPrize(ScriptContext& ctx, double index, std::optional<bool> final) {
  game::CratesGame* g = game::CratesGame::active();
  if (!g) {
    if (ctx.stubs.hit("SelectPrize")) ctx.log.write("[unimplemented] SelectPrize (no CratesGame window is open)");
    return 0;
  }
  return g->select(static_cast<int>(index), final.value_or(false));
}

}  // namespace

void registerCratesNatives(lua_State* L, ScriptContext& ctx) {
  ScriptContext* c = &ctx;
  ctx.wm.addWindowType("CratesGame", [c](ui::WindowManager& wm) { return std::make_unique<game::CratesGame>(wm, *c); });
  bind<nativePrepareGame>(L, ctx, "PrepareGame");
  bind<nativeSelectPrize>(L, ctx, "SelectPrize");
}

}  // namespace choco::script
