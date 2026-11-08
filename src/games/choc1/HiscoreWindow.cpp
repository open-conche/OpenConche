// Chocolatier 1's HiscoreWindow: the two C1 deviations of the shared offline high-score screen. The score text of
// the update at 00416150: the score is capped at 999,999,999 (`if (999999999 < score) score = 999999999`), passed
// through the money formats of FUN_0041e0e0 (the same tables UtilDollars reads: formatOnes/formatThousands/...,
// so the placeholder of the row constructors, "#$99,999,999") and put into the string `formatScore`, which the
// installed table holds as "%1%/week". The gamemode label comes from the exe reading the windows `gamemode` /
// `gamemodeleft` / `gamemoderight` (strings.tsv 004e840c..) and is not fed by the Lua: it shows `empire_mode`
// ("Story Mode") for the story table, `free_mode` ("Free Play Mode") for free play.

#include "games/choc1/HiscoreWindow.h"

#include <algorithm>

#include "games/choc1/Choc1Natives.h"
#include "sdk/Users.h"
#include "ui/widgets/TextWidgets.h"

namespace choco::game::choc1 {

std::string HiscoreWindow::scoreTextOf(const sdk::HiscoreRow& row) const {
  const long long capped = std::min<long long>(row.score, 999999999);
  return ctx_.strings.sdkLookup("formatScore",
                                {script::choc1Dollars(ctx_.strings, static_cast<double>(capped)), "", "", "", ""});
}

void HiscoreWindow::updateText() {
  game::HiscoreWindow::updateText();
  if (auto* gamemode = dynamic_cast<ui::TextWindow*>(find("gamemode"))) {
    const bool free = ctx_.users && ctx_.users->hiscoreMode() != "0";  // pref "gamemode": 0 is story
    gamemode->setLabel(ctx_.strings.sdkLookup(free ? "free_mode" : "empire_mode", {}));
  }
}

}  // namespace choco::game::choc1
