#include "games/choc2/Choc2DiceNatives.h"

#include <memory>
#include <string>

#include "games/choc2/DiceGame.h"

namespace choco::script {

namespace {

using game::choc2::DiceGame;

// With no DiceGame a native does nothing but say so once: the Lua only calls them from the game's own dialog.
DiceGame* dice(ScriptContext& ctx, const char* native) {
  DiceGame* g = DiceGame::active();
  if (!g && ctx.warned.insert(std::string("dice-no-game-") + native).second)
    ctx.log.write(std::string("[native-warning] ") + native + ": there is no DiceGame");
  return g;
}

void nativeRollFirst(ScriptContext& ctx) {
  if (DiceGame* g = dice(ctx, "RollFirst")) g->rollFirst();
}
void nativeRollSecond(ScriptContext& ctx) {
  if (DiceGame* g = dice(ctx, "RollSecond")) g->rollSecond();
}
void nativeStopOne(ScriptContext& ctx) {
  if (DiceGame* g = dice(ctx, "StopOne")) g->stopOne();
}
double nativePlayerTotal(ScriptContext& ctx) {
  DiceGame* g = dice(ctx, "PlayerTotal");
  return g ? g->playerTotal() : 0;
}
double nativeCharacterTotal(ScriptContext& ctx) {
  DiceGame* g = dice(ctx, "CharacterTotal");
  return g ? g->characterTotal() : 0;
}

}  // namespace

void registerChoc2DiceNatives(lua_State* L, ScriptContext& ctx) {
  bind<nativeRollFirst>(L, ctx, "RollFirst");
  bind<nativeRollSecond>(L, ctx, "RollSecond");
  bind<nativeStopOne>(L, ctx, "StopOne");
  bind<nativePlayerTotal>(L, ctx, "PlayerTotal");
  bind<nativeCharacterTotal>(L, ctx, "CharacterTotal");
  ctx.wm.addWindowType("DiceGame", [&ctx](ui::WindowManager& wm) { return std::make_unique<game::choc2::DiceGame>(wm, ctx); });
}

}  // namespace choco::script
