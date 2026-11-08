#include "games/choc2/Choc2BonusNatives.h"

#include <memory>

#include "games/common/Crates.h"

namespace choco::script {

void registerChoc2BonusNatives(lua_State*, ScriptContext& ctx) {
  ctx.wm.addWindowType("BonusGame", [&ctx](ui::WindowManager& wm) {
    return std::make_unique<game::CratesGame>(wm, ctx, game::CratesSpec::bonus());
  });
}

}  // namespace choco::script
