#pragma once

#include <array>
#include <string>
#include <string_view>

#include "games/common/factory/Chocolate.h"

namespace choco::game::choc2 {

// Chocolatier 2's `FactoryGame` (docs/design/choc2-minigames.md 2.1 and 2.2): Chocolatier 3's ChocolateFactory fed by a key
// adapter. Chocolatier 2's exe is packed, so everything native below is an INFERENCE (each is a named constant or a spec field,
// logged as a decision for review); what the Lua passes and expects is fact.

// The eight miss points: the cell centres of the 4 x 2 panel of `image/machine_stack.png` (250 x 600, drawn at x = 565, y = 0;
// MEASURED from the PNG: cell columns 51..81, 90..120, 129..158, 168..198 and rows 120..150, 159..189 in the image).
const std::array<gfx::PointF, 8>& factoryMissPoints();

// The style Chocolatier 2's script builds (`traypath`, `trayEnd`, `ingredientpath`, `productpath`, `traycount`,
// `ingredientrate`, `bonuscount`, `maxspeed`, `colorcount`, `traycycle`, `conveyortime`, `flytime`, `producttime`, `speedup`,
// `starttime`, `countdown`, `bonusflytime`, `warn`, `product`, `type`) as a ChocolateFactory spec. A missing key keeps
// Chocolatier 3's default. Pure: no window needed.
ChocolateSpec factorySpec(const ui::Style& style);

// The Chocolatier 2 sound file (no folder, no extension: PlaySound's argument) for one of Chocolatier 3's event names, or ""
// for an event that plays nothing here (the intro, outro and game-over events: the music is the Lua's).
std::string factorySound(std::string_view c3Event);

// `performance` for MinigameEnd (FACT, docs/re/choc2-factory-binary.md 5): trunc(100 * production / ideal) with
// ideal = floor(floor(startMs / rateMs) / recipeSize), plus floor(startMs / rateMs) when colours are on; 0 when the ideal is 0;
// -1 after the 8-miss abort.
double factoryPerformance(unsigned production, bool aborted, double startMs, double rateMs, int recipeSize, bool colors);

// Lua type `FactoryGame`. Lifecycle: built idle (`started_` false); ResumeMinigame starts it, then pauses and resumes it;
// PauseMinigame pauses; ResetMinigame is back to idle. The Lua callbacks MinigamePause, MinigameWarn, MinigameColor and
// MinigameEnd(performance) are looked up by global name at every call, and nothing is called while the game is idle.
class FactoryGame : public ChocolateFactory {
 public:
  FactoryGame(ui::WindowManager& wm, script::ScriptContext& ctx);
  std::string_view typeName() const override { return "FactoryGame"; }
  void init(const ui::Style& style) override;
  ChocolateSpec readSpec(const ui::Style& style) const override { return factorySpec(style); }
  // The sprite tree is drawn before the children (the info dialog is one of them), and nothing is drawn after them (the "Nx" popups are TextSprite children of the trays).
  void draw(ui::DrawContext& ctx) const override;
  void postDraw(ui::DrawContext& ctx) const override;
  std::string detail() const override;

  // The newest live FactoryGame, or null.
  static FactoryGame* active();

  // The natives' targets. `L` is the calling Lua state.
  void resume(lua_State* L);  // ResumeMinigame
  void pause(lua_State* L);   // PauseMinigame
  void reset();               // ResetMinigame
  unsigned production() const { return score(); }
  bool started() const { return started_; }
  // The value MinigameEnd gets for the game as it stands.
  double performance() const;

 protected:
  void sound(std::string_view name) const override;
  void notifyPaused(lua_State* L, const std::string& key) override;
  void showGameOver() override;
  void onSecondMatch() override;
  const char* firstMissKey() const override { return "factory_waste"; }

 private:
  // Calls the Lua global `name` (with `arg` when given) on `L`; errors are logged as [lua-error]. A missing global is
  // logged once.
  void callGlobal(lua_State* L, const char* name, const double* arg = nullptr) const;
  bool aborted() const { return !park_.empty() && misses_ >= park_.size(); }

  bool started_ = false;
  bool colorWarn_ = false, colors_ = false;
  int recipeSize_ = 0;
  double rateMs_ = 250;
};

}  // namespace choco::game::choc2
