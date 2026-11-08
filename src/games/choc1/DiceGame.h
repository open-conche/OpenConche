#pragma once

// Chocolatier 1's DiceGame window (the exe's TDiceGame, vtable 004e6de4; docs/re/choc-dice-game.md section 1) and its natives
// RollDice(bet) and StopDie. The rules live in DiceSim, a plain core without a window or a clock; the window wires it to the
// 62 ms modal-clock animation, the die Bitmap children, the outcome texts and buttons, and the sounds.

#include <array>
#include <functional>
#include <optional>
#include <string>

#include "games/choc1/FactorySim.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::game::choc1 {

// The rules of one roll of the dice game (docs/re/choc-dice-game.md 1.5, 1.6; all facts from the Ghidra decompile): a 62 ms
// tick; every die spins while elapsed <= 2000 ms; then remaining = N - (elapsed - 2000)/750, so the highest-numbered die
// stops first (die4, then die3, die2, die1; N = 4 on the shipped Lua) and a full run ends at tick 81 (5022 ms).
class DiceSim {
 public:
  using RandFn = std::function<int()>;  // the shared CRT rand() stream (Choc1World::crtRand)

  static constexpr int kTickMs = 62, kSpinMs = 2000, kStepMs = 750, kStopBias = 1938;  // 0x792 = 1938
  static constexpr int kMaxDice = 4;  // the final-face array would run into the picture slots at +0x70 (1.2)

  enum class Outcome { None = 0, Win = 1, Lose = 2, Tie = 3 };

  // One visit of the exe's redraw (00408910): the face to show per die, plus (on the tick that stops the last die) the
  // contest's result.
  struct Faces {
    std::array<int, kMaxDice> face{};  // what die i (0-based) shows after this visit: a fresh draw while spinning, else final[i]
    int remaining = 0;                 // the remaining count this visit drew at
    std::optional<Outcome> outcome;   // set only when this visit ended the run (remaining hit 0)
  };

  explicit DiceSim(RandFn rand) : rand_(std::move(rand)) {}

  // 1.3 Init: the die count found among the window's children; the final faces are zeroed.
  void setDiceCount(int n) { n_ = n; final_ = {}; }
  int diceCount() const { return n_; }
  int elapsed() const { return elapsed_; }
  const std::string& bet() const { return bet_; }
  int finalFace(int i) const { return final_[static_cast<std::size_t>(i)]; }
  // remaining as the exe computes it: N while elapsed <= 2000, else N - (elapsed-2000)/750 floored at 0.
  int remaining() const { return remainingOf(n_, elapsed_); }
  bool rolling() const { return remaining() > 0; }
  // The player rolls dice 0 and 1, the character 2 and 3 (1.4).
  int playerTotal() const { return final_[0] + final_[1]; }
  int characterTotal() const { return final_[2] + final_[3]; }

  // RollDice (00408a30): N final draws (rand()%6 each, in order), the wager text, elapsed = 0. The dice are not redrawn
  // here; the first tick at +62 ms is the first redraw.
  void rollDice(std::string bet);

  // One tick (00408b40): elapsed += 62, then the redraw at the new remaining - a draw per die, always, in order 0..N-1
  // (the draw of a stopped die is discarded); then, when remaining hit 0, the contest ends on this same visit.
  Faces tick();

  // StopDie (004089e0): remaining as the tick computes it; nothing when it is 0. Otherwise one redraw (N draws) and
  // elapsed = (N - (remaining-1))*750 + 1938, so the next tick stops exactly one die. Pressed before RollDice (elapsed 0)
  // it still redraws and jumps (the roll button covers the stop button until the roll starts, so the shipped Lua cannot).
  Faces stopDie();

  // The children the outcome shows, the sound it plays (the file name under audio/sfx, no extension) and the Lua
  // statement it runs after the sound ("": a tie pays nothing).
  static const char* messageOf(Outcome o);
  static const char* soundOf(Outcome o);
  static std::string moneyOf(Outcome o, const std::string& bet);

  static int remainingOf(int n, int elapsedMs);

 private:
  // 00408910: one draw per die (discarded for the stopped ones), the shown face picked as the tick describes.
  Faces redraw(int remaining);

  RandFn rand_;
  int n_ = 0, elapsed_ = 0;
  std::string bet_;
  std::array<int, kMaxDice> final_{};
};

// DiceGame{x y w h; children die1..dieN, intro, win_message, lose_message, draw_message, stop, stop_label, leave,
// leave_label} (the shipped Lua's dialog body, sim/character.lua's PlayDice). RollDice rolls from the shared CRT rand()
// stream and starts a repeating 62 ms window animation on the modal's clock; each tick redraws the four die Bitmaps
// (image/die<face+1>, the picture-slot order of 1.1); the tick that stops the last die stops the roll sound, hides stop
// and stop_label, hides intro, shows leave with leave_label and exactly one of the three outcome texts, plays the
// outcome sound, and runs gSim:AdjustMoney(<bet>,'dice_logwin') / gSim:AdjustMoney(-<bet>,'dice_logloss') (never a tie).
class DiceGame : public ui::Window {
 public:
  DiceGame(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), ctx_(ctx), sim_([this] { return dieDraw(); }) {}
  std::string_view typeName() const override { return "DiceGame"; }
  void postChildrenInit(const ui::Style& style) override;
  bool onAnimate() override;
  std::string detail() const override;

  // The natives (1.5, 1.6).
  void rollDice(std::string bet);
  void stopDie();

  const DiceSim& sim() const { return sim_; }
  // The outcome of the last finished contest (None while it runs or before the first roll).
  DiceSim::Outcome outcome() const { return outcome_; }

  // The DiceGame in the modal chain (top first), or null.
  static DiceGame* inChain(ui::WindowManager& wm);

 private:
  void applyFaces(const DiceSim::Faces& f);
  void showChild(const char* child, bool shown);
  void playSound(const char* name);
  void stopRollSound();
  void runMoney(const std::string& statement);
  int dieDraw();  // the next value of the shared CRT rand() stream

  script::ScriptContext& ctx_;
  DiceSim sim_;
  CrtRand localRand_;  // used when there is no Chocolatier 1 world (a test)
  std::array<int, DiceSim::kMaxDice> shown_{};
  DiceSim::Outcome outcome_ = DiceSim::Outcome::None;
};

}  // namespace choco::game::choc1
