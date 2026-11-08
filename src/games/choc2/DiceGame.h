#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::game::choc2 {

// The timing and the faces of one roll of Chocolatier 2's dice game, free of windows. Chocolatier 1's TDiceGame mechanism
// (docs/re/choc-dice-game.md 1.5, 1.6) generalised to a list of dice (docs/design/choc2-minigames.md 2.5): a 62 ms tick; every
// die of the roll spins while elapsed <= 2000 ms; then `remaining = n - (elapsed - 2000) / 750` and the dice stop in list order.
// So a roll of two dice ends at 3534 ms (stops at 2790 and 3534) and one of six at 6510 ms (2790, 3534, 4278, 5022, 5766, 6510).
// INFERENCE: the packed Chocolatier 2 executable was not decoded; only the Lua's use of the natives is fact.
struct DiceRoll {
  static constexpr int kTickMs = 62, kSpinMs = 2000, kStepMs = 750, kStopBias = 1938;
  static constexpr int kDice = 8;

  std::array<int, kDice> face{};      // the final face of the dice of the latest roll, 1..6 (0 = never rolled)
  std::array<bool, kDice> stopped{};  // the die has stopped, so its face counts
  std::vector<int> dice;              // the latest roll's dice in stop order
  int elapsed = 0;

  // Dice of the roll that have not stopped.
  int remaining() const;
  bool rolling() const { return remaining() > 0; }
  // Draws the final faces from the game stream, range(1, 6), in list order, and restarts the clock.
  void start(std::vector<int> d, script::Rng& rng);
  // One 62 ms tick; the dice that stopped on it.
  std::vector<int> tick();
  // C1's StopDie: the next die stops on the next tick. Nothing when no die is rolling.
  void stopOne();
  // The sums of the stopped faces among dice 0-3 and 4-7.
  int playerTotal() const { return total(0, 4); }
  int characterTotal() const { return total(4, 8); }

 private:
  int total(int from, int to) const;
};

// DiceGame{x y w h fit} (ui/gamble_dice.lua), the container of the eight Bitmap dice die_0..die_3 (the player's) and
// die_4..die_7 (the opponent's). RollFirst rolls dice 0 and 1, RollSecond dice 2 to 7; each is ignored while a roll runs. The
// roll is a 62 ms window animation with catch-up on the modal's clock. Each stopped die shows image/die<v> and adds 1 to the
// global gDiceRollCount (nil counts as 0), which the Lua polls with Pause(50); spinning dice show faces from a cosmetic
// generator of the window's own, never the game stream. The children are found by name each time. No native sounds.
class DiceGame : public ui::Window {
 public:
  DiceGame(ui::WindowManager& wm, script::ScriptContext& ctx);
  ~DiceGame() override;
  std::string_view typeName() const override { return "DiceGame"; }
  bool onAnimate() override;
  std::string detail() const override;

  // The newest live dice game, the target of the natives; null if none.
  static DiceGame* active();

  void rollFirst() { startRoll({0, 1}); }
  void rollSecond() { startRoll({2, 3, 4, 5, 6, 7}); }
  void stopOne() { roll_.stopOne(); }
  int playerTotal() const { return roll_.playerTotal(); }
  int characterTotal() const { return roll_.characterTotal(); }
  const DiceRoll& roll() const { return roll_; }
  int rolls() const { return rolls_; }

 private:
  void startRoll(std::vector<int> dice);
  void show(int die, int face);  // the die's Bitmap shows image/die<face> (die_unknown for 0)
  int spinFace();
  void bumpRollCount();

  script::ScriptContext& ctx_;
  DiceRoll roll_;
  int rolls_ = 0;
  std::uint32_t spin_ = 0;  // the cosmetic generator's state
};

}  // namespace choco::game::choc2
