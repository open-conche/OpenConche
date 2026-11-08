#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "games/choc3/Particles.h"
#include "script/LuaState.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

struct lua_State;

namespace choco::game {

// One reel of the slot machine (docs/re/slot-machine.md 3, 5). A constant-speed scroll: every 50 ms tick adds `speed` to
// `phase`, a step at phase >= 200 shifts the four symbol names down and draws a new top symbol, and the last step snaps
// the strip to rest with the target in the middle (index 2).
struct SlotWheel {
  double x = 0, y = 0;     // the DSL position; the resting symbol (child 2) is centred on it
  int stepsLeft = 0;       // 0 = idle or stopped
  int speed = 0;           // phase units per tick, 200 = one symbol step
  int phase = 0;
  int target = 0;          // 0-based option index to land on
  std::string names[4];    // option names on the strip, index 0 on top
  double stripY = 0;       // the strip's current y (baseY + drift)
};

// The slot machine of ui/ui_slotmachine.lua (Lua type `SlotMachine`, docs/design/factories.md 3.7, docs/re/slot-machine.md).
// StartSlots(n1, n2, n3, n4, payout) starts four wheels (steps 20..50 and speed 90..110 drawn per wheel from the game's
// random stream); each 50 ms animation tick scrolls them. On the tick the last wheel stops, `Stop_machine_spin` is sent and
// `onstopped` runs (the Lua pays Floor(bet * payout + 0.9) and shows the result). A payout above 0 then runs the dollar
// sparkle (docs/re/particles.md 4) in 50 ms ticks until gSparkleTime is used up, and only then does `onpayout` run; no payout
// runs it in the same tick as `onstopped`. Both are read as functions from the style and run with a protected call through
// the script sink. The Lua draws the mask bitmap over the wheels, so the window does no clipping of its own.
class SlotMachine : public ui::Window {
 public:
  SlotMachine(ui::WindowManager& wm, script::ScriptContext& ctx);
  ~SlotMachine() override;
  std::string_view typeName() const override { return "SlotMachine"; }
  void init(const ui::Style& style) override;
  std::string detail() const override;

  // The newest live slot machine, the target of StartSlots and StopSlots. Null when none exists.
  static SlotMachine* active();

  // StartSlots: the 1-based option indices of the four wheels and the payout. Draws steps then speed per wheel in order and
  // starts the 50 ms animation.
  void spin(const int wheels[4], double payout);
  // StopSlots: the first wheel still spinning is cut to 3 steps if it had more.
  void stopOne();
  bool onAnimate() override;
  void draw(ui::DrawContext& ctx) const override;
  const SlotSparkle& sparkle() const { return sparkle_; }

  // The money a spin pays out before the bet is subtracted: Floor(bet * payout + 0.9). The net change to the player is this
  // minus the bet.
  static long long winnings(double bet, double payout);

  int spins() const { return spins_; }
  const int* wheels() const { return wheels_; }
  SlotWheel& wheel(int i) { return wheelState_[i]; }
  const SlotWheel& wheel(int i) const { return wheelState_[i]; }
  const std::vector<std::string>& options() const { return options_; }
  bool spinning() const { return !allStopped_; }

  static constexpr double kStripScale = 0.425;  // the strip sprite's uniform scale
  static constexpr double kSymbolPitch = 60;    // strip units between symbols (25.5 px on screen)

 private:
  static constexpr double kSpinMs = 50;
  static void sound(script::ScriptContext& ctx, const char* name);
  void call(const std::optional<script::LuaRef>& fn);
  void tickWheel(int i);
  const gfx::Texture* symbolTexture(const std::string& name) const;

  script::ScriptContext& ctx_;
  std::optional<script::LuaRef> onStopped_, onPayout_;
  std::vector<std::string> options_;
  SlotWheel wheelState_[4];
  double baseY_[4] = {0, 0, 0, 0};
  int wheels_[4] = {0, 0, 0, 0};
  double payout_ = 0;
  int spins_ = 0;
  bool allStopped_ = true;  // every wheel is at rest (also before the first spin)
  bool payoutDone_ = true;
  SlotSparkle sparkle_;
  mutable std::shared_ptr<gfx::Texture> dollars_;
  mutable bool dollarsTried_ = false;
  mutable std::map<std::string, std::shared_ptr<gfx::Texture>> symbols_;
};

// Registers the SlotMachine, SignEditor and CurveEditor window types and the StartSlots/StopSlots natives. SignEditor and
// CurveEditor are placeholders (dead feature and dev menu only: docs/re/coffee-factory-and-recipes.md 6, 7); their natives
// (SetBackground and the others) stay the generated logged stubs.
void registerStubNatives(lua_State* L, script::ScriptContext& ctx);

}  // namespace choco::game
