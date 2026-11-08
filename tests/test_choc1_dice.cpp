// Chocolatier 1 dice (docs/re/choc-dice-game.md section 1): the DiceSim rules on fixed RNGs, then the window on
// a fake modal clock with the natives bound.
#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <vector>

#include "games/choc1/DiceGame.h"
#include "games/Registry.h"
#include "ScriptHarness.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
namespace f1 = choco::game::choc1;
using f1::DiceSim;
using Outcome = DiceSim::Outcome;

namespace {

// A sim over a fixed stream: either the MSVC rand() from seed 1 (the exe never calls srand) or a scripted sequence.
struct Fixture {
  Fixture() : sim([this] {
    ++calls;
    return scripted ? step() : rand.next();
  }) {
    sim.setDiceCount(4);
  }
  int step() {
    if (static_cast<std::size_t>(index) < seq.size()) return seq[static_cast<std::size_t>(index++)];
    return seq.empty() ? 0 : seq.back();
  }
  f1::CrtRand rand;
  int calls = 0;
  DiceSim sim;
  bool scripted = false;
  std::vector<int> seq;
  int index = 0;
};

}  // namespace

TEST_CASE("dice: remaining is N through the 2000 ms spin, then one die per 750 ms, first stop 2790, last tick 5022") {
  CHECK(DiceSim::remainingOf(4, 0) == 4);
  CHECK(DiceSim::remainingOf(4, 62) == 4);
  CHECK(DiceSim::remainingOf(4, 2000) == 4);  // the exe cuts at 2000 < elapsed, not <=
  CHECK(DiceSim::remainingOf(4, 2046) == 4);  // tick 33: 46 < 750, so every die still spins
  CHECK(DiceSim::remainingOf(4, 2749) == 4);
  CHECK(DiceSim::remainingOf(4, 2750) == 3);
  CHECK(DiceSim::remainingOf(4, 2790) == 3);  // tick 45: die4 stops
  CHECK(DiceSim::remainingOf(4, 3534) == 2);  // tick 57: die3
  CHECK(DiceSim::remainingOf(4, 4278) == 1);  // tick 69: die2
  CHECK(DiceSim::remainingOf(4, 5000) == 0);  // die1's deadline, first reached by tick 81 (5022)
  CHECK(DiceSim::remainingOf(4, 5022) == 0);
  CHECK(DiceSim::remainingOf(4, 9000) == 0);
  CHECK(DiceSim::remainingOf(2, 5022) == 0);
  CHECK(DiceSim::remainingOf(6, 6510) == 0);

  Fixture f;
  f.sim.rollDice("500");
  int ticks = 0;
  std::optional<Outcome> outcome;
  while (!outcome && ticks < 200) outcome = f.sim.tick().outcome, ++ticks;
  REQUIRE(ticks == 81);
  CHECK(f.sim.elapsed() == 5022);
  CHECK(outcome == Outcome::Win);  // the seed-1 finals are 5,5,4,4: the player's 10 against the character's 8
  CHECK(f.calls == 4 + 81 * 4);   // the final draws, then a draw per die per tick (the stopped dice's draws are discarded)
}

TEST_CASE("dice: the faces: 4 draws in RollDice in die order, then 4 per tick, the highest-numbered die stopping first") {
  Fixture f;
  // The first four rand() of seed 1 are 41, 18467, 6334, 26500: rand()%6 gives 5, 1, 4, 2 (player 5+1, character 4+2).
  f.sim.rollDice("500");
  CHECK(f.calls == 4);
  CHECK(f.sim.bet() == "500");
  CHECK(f.sim.elapsed() == 0);
  const int final0 = f.sim.finalFace(0), final1 = f.sim.finalFace(1), final2 = f.sim.finalFace(2), final3 = f.sim.finalFace(3);
  CHECK(std::vector<int>{final0, final1, final2, final3} == std::vector<int>{5, 5, 4, 4});  // 41, 18467, 6334, 26500 (mod 6)
  CHECK(f.sim.playerTotal() == 10);
  CHECK(f.sim.characterTotal() == 8);

  DiceSim::Faces t = f.sim.tick();  // the first tick: four fresh draws, in die order, one per die
  CHECK(f.calls == 8);
  CHECK(t.remaining == 4);
  f1::CrtRand ref(1);
  for (int i = 0; i < 4; ++i) ref.next();  // the four final draws
  for (int i = 0; i < 4; ++i) CHECK(t.face[static_cast<std::size_t>(i)] == ref.next() % 6);

  // At tick 45 (2790) die4 shows its final face while die1..die3 still spin; die4's draw still happened.
  while (f.sim.elapsed() < 2790 - 62) f.sim.tick();
  const int before = f.calls;
  t = f.sim.tick();
  CHECK(f.sim.elapsed() == 2790);
  CHECK(t.remaining == 3);
  CHECK(f.calls == before + 4);  // one draw per die, the stopped die's included (made and discarded)
  CHECK(t.face[3] == final3);
  CHECK((t.face[0] != final0 || t.face[1] != final1 || t.face[2] != final2));  // the rest still spins
  CHECK(f.sim.rolling());
}

TEST_CASE("dice: the outcomes: who wins, which message, which sound and which AdjustMoney statement") {
  // The player rolls dice 0 and 1, the character 2 and 3 (A against B); the scripted values are the raw rand() draws.
  auto outcome = [](std::vector<int> finals) {
    Fixture f;
    f.scripted = true;
    f.seq = std::move(finals);
    f.sim.rollDice("500");
    std::optional<Outcome> o;
    int ticks = 0;
    while (!o && ticks < 200) o = f.sim.tick().outcome, ++ticks;
    REQUIRE(ticks == 81);
    return o;
  };
  REQUIRE(outcome({5, 5, 0, 0, 3}) == Outcome::Win);  // 10 against 0
  CHECK(DiceSim::messageOf(Outcome::Win) == std::string("win_message"));
  CHECK(DiceSim::soundOf(Outcome::Win) == std::string("dice_game_win"));
  CHECK(DiceSim::moneyOf(Outcome::Win, "500") == "gSim:AdjustMoney(500,'dice_logwin')");
  REQUIRE(outcome({0, 0, 5, 5, 3}) == Outcome::Lose);  // 0 against 10
  CHECK(DiceSim::messageOf(Outcome::Lose) == std::string("lose_message"));
  CHECK(DiceSim::soundOf(Outcome::Lose) == std::string("dice_game_lose"));
  CHECK(DiceSim::moneyOf(Outcome::Lose, "500") == "gSim:AdjustMoney(-500,'dice_logloss')");
  REQUIRE(outcome({2, 2, 2, 2, 3}) == Outcome::Tie);  // 4 against 4
  CHECK(DiceSim::messageOf(Outcome::Tie) == std::string("draw_message"));
  CHECK(DiceSim::soundOf(Outcome::Tie) == std::string("dice_game_tie"));
  CHECK(DiceSim::moneyOf(Outcome::Tie, "500").empty());
}

TEST_CASE("dice: StopDie jumps elapsed 2688/3438/4188/4938 so the next tick stops exactly one die") {
  Fixture f;
  f.sim.rollDice("100");
  CHECK(f.sim.elapsed() == 0);
  const int final3 = f.sim.finalFace(3);
  // Pressed before the first tick: remaining is 4, so it redraws (4 draws) and jumps to 2688. No task runs, so the
  // shipped Lua cannot reach this (the roll button covers the stop button), but the native does it all the same.
  DiceSim::Faces s = f.sim.stopDie();
  CHECK(s.remaining == 4);
  CHECK(f.calls == 8);
  CHECK(f.sim.elapsed() == 2688);
  DiceSim::Faces t = f.sim.tick();
  CHECK(t.remaining == 3);
  CHECK(t.face[3] == final3);  // die4 stopped on the very next tick
  f.sim.stopDie();
  CHECK(f.sim.elapsed() == 3438);
  f.sim.tick();
  f.sim.stopDie();
  CHECK(f.sim.elapsed() == 4188);
  f.sim.tick();
  f.sim.stopDie();
  CHECK(f.sim.elapsed() == 4938);
  f.sim.tick();
  CHECK(f.sim.elapsed() == 5000);
  CHECK(f.sim.remaining() == 0);
  // The whole contest took 4 draws in RollDice, 4 per StopDie press and 4 per tick.
  CHECK(f.calls == 4 + 4 * 8);

  // Once every die stopped StopDie does nothing: no draw, no elapsed change.
  const int idle = f.calls;
  CHECK(f.sim.stopDie().remaining == 0);
  CHECK(f.calls == idle);
  CHECK(f.sim.elapsed() == 5000);
  CHECK_FALSE(f.sim.rolling());
}

TEST_CASE("dice: StopDie before any roll only redraws and jumps") {
  Fixture f;
  f.scripted = true;
  f.seq = {1, 2, 3, 4, 5, 0};
  const int before = f.calls;
  f.sim.stopDie();
  CHECK(f.calls == before + 4);
  CHECK(f.sim.elapsed() == 2688);
  CHECK(f.sim.rolling());
}

namespace {

// The dialog body the shipped Lua builds (sim/character.lua's PlayDice): the stop and leave buttons, the six texts and
// the four die Bitmaps, under a DiceGame the count finds.
struct DiceEnv : test::ScriptEnv {
  DiceEnv() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/choc1_story"), host(ctx) {
    ctx.module = games::findModule("choc1");
    ctx.module->attach(ctx);
    booted = host.boot();
    wm.pushModal("m").setRect({0, 0, 800, 600});
    std::string body;
    std::string text;
    for (const char* name : {"stop", "leave"}) {
      body += std::string("c = { typename = 'Button', name = '") + name + "', x = 10, y = 10, w = 60, h = 20 }; GenericCreate(c); PopWindow(); ";
    }
    for (const char* name : {"intro", "win_message", "lose_message", "draw_message", "stop_label", "leave_label"}) {
      body += std::string("c = { typename = 'Text', name = '") + name +
             "', label = '#x', x = 0, y = 0, w = 50, h = 20 }; GenericCreate(c); PostCreate(c, 'Text'); PopWindow(); ";
    }
    for (int i = 4; i >= 1; --i)  // the top row (die4, die3) is created before the bottom (die2, die1), as in the Lua
      // die3 is built with different body art: Init re-images every counted die to picture slot 0 whatever it was
      body += "c = { typename = 'Bitmap', name = 'die" + std::to_string(i) + "', image = '" +
              std::string(i == 3 ? "image/die5" : "image/die1") +
              "', x = 0, y = 0, w = 50, h = 50 }; GenericCreate(c); PopWindow(); ";
    body += "PostCreate(t, 'DiceGame'); PopWindow()";
    REQUIRE_MESSAGE(run("c = nil; do local t = { typename = 'DiceGame', name = 'dice', x = 0, y = 0, w = 800, h = 600 }; "
                   "GenericCreate(t); " + body + " end") == 0, lastError);
    game = f1::DiceGame::inChain(wm);
    REQUIRE(game);
    // The bet's money statement lands in gMoney, so the test sees the exact statement the outcome ran.
    REQUIRE(run("gMoney = 0; gReason = nil; gSim = { AdjustMoney = function(self, a, r) gMoney = gMoney + a; gReason = r end }") == 0);
  }
  // Runs the modal clock in 16 ms frames to `ms` after the start (1000).
  void advance(double ms) {
    const double to = now + ms;
    while (now < to) {
      now = std::min(now + 16, to);
      wm.tickWindowAnimations(now);
    }
  }
  bool shown(const char* name) {
    const ui::Window* w = game->find(name);
    return w && w->shown();
  }
  std::string dieImage(int i) {
    if (auto* b = dynamic_cast<ui::BitmapWindow*>(game->find("die" + std::to_string(i)))) return b->describe();
    return "";
  }
  f1::DiceGame* game = nullptr;
  script::ScriptHost host;
  bool booted = false;
  double now = 1000;
};

}  // namespace

TEST_CASE("dice game: the window hides the four texts at init and counts the die Bitmaps") {
  DiceEnv e;
  REQUIRE(e.booted);
  CHECK(e.game->sim().diceCount() == 4);
  CHECK_FALSE(e.shown("win_message"));
  CHECK_FALSE(e.shown("lose_message"));
  CHECK_FALSE(e.shown("draw_message"));
  CHECK_FALSE(e.shown("stop_label"));
  CHECK(e.shown("intro"));  // the intro question stays
  CHECK(e.shown("stop"));   // 1.3 does not disable the stop button (the roll button covers it)
  CHECK(e.shown("leave"));
  CHECK(e.dieImage(1).find("image/die1") != std::string::npos);
  CHECK(e.dieImage(3).find("image/die5") == std::string::npos);  // Init re-images each counted die to picture slot 0
  CHECK(e.dieImage(3).find("image/die1") != std::string::npos);
}

TEST_CASE("dice game: RollDice animates the four dice on the modal clock and pays through gSim:AdjustMoney") {
  DiceEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("RollDice('500')") == 0);
  CHECK(e.game->sim().rolling());
  CHECK(e.wm.windowAnimationCount() == 1);
  e.advance(5100);  // past the full run (5022)
  const Outcome o = e.game->outcome();
  CHECK_FALSE(e.game->sim().rolling());
  CHECK(e.game->sim().elapsed() == 5022);
  CHECK(e.wm.windowAnimationCount() == 0);
  const int final0 = e.game->sim().finalFace(0), final1 = e.game->sim().finalFace(1), final2 = e.game->sim().finalFace(2),
            final3 = e.game->sim().finalFace(3);
  CHECK((o == Outcome::Win || o == Outcome::Lose || o == Outcome::Tie));
  // The final faces are on the dice: image/die<face+1> (the redraw's FACT: face v is picture slot v of die1..die6).
  CHECK(e.dieImage(1).find("image/die" + std::to_string(final0 + 1)) != std::string::npos);
  CHECK(e.dieImage(2).find("image/die" + std::to_string(final1 + 1)) != std::string::npos);
  CHECK(e.dieImage(3).find("image/die" + std::to_string(final2 + 1)) != std::string::npos);
  CHECK(e.dieImage(4).find("image/die" + std::to_string(final3 + 1)) != std::string::npos);
  // stop and stop_label were disabled, intro hidden, leave left for the exit, exactly one outcome text shown.
  CHECK_FALSE(e.shown("stop"));
  CHECK_FALSE(e.shown("stop_label"));
  CHECK_FALSE(e.shown("intro"));
  CHECK(e.shown("leave"));
  CHECK(e.shown("leave_label"));
  CHECK(e.shown("win_message") + e.shown("lose_message") + e.shown("draw_message") == 1);
  CHECK(e.shown(DiceSim::messageOf(o)));
  CHECK(e.getString("gReason") == (o == Outcome::Win ? "dice_logwin" : o == Outcome::Lose ? "dice_logloss" : "<nil>"));
  if (o == Outcome::Win) CHECK(e.getNumber("gMoney") == 500);
  if (o == Outcome::Lose) CHECK(e.getNumber("gMoney") == -500);
  if (o == Outcome::Tie) CHECK(e.getNumber("gMoney") == 0);
  // The sound plays through PlaySound; the unit env has no audio service, so the headless test asserts it.
}

TEST_CASE("dice game: StopDie mid-roll stops one die per press; with no DiceGame the natives log once each") {
  DiceEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("RollDice('100')") == 0);
  e.advance(300);
  REQUIRE(e.run("StopDie()") == 0);
  e.advance(100);  // 400 ms in: long before the 2750 ms deadline one die stopped
  CHECK(e.game->sim().remaining() == 3);
  e.advance(5500);
  CHECK_FALSE(e.game->sim().rolling());
  CHECK(e.game->outcome() != Outcome::None);
  const double paid = e.getNumber("gMoney");  // the contest paid (or charged) exactly the bet
  if (e.game->outcome() == Outcome::Win) CHECK(paid == 100);
  if (e.game->outcome() == Outcome::Lose) CHECK(paid == -100);
  if (e.game->outcome() == Outcome::Tie) CHECK(paid == 0);
  e.wm.destroyWindow(*e.game);
  e.wm.clearDestroyed();
  REQUIRE(e.run("StopDie(); RollDice('5')") == 0);
  CHECK(e.log.contains("RollDice: no DiceGame window, ignored"));
  CHECK(e.log.contains("StopDie: no DiceGame window, ignored"));
  CHECK(e.getNumber("gMoney") == paid);  // the destroyed window's tick ran nothing
}
