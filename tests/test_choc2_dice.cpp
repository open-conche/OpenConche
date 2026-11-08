// Chocolatier 2 DiceGame (docs/design/choc2-minigames.md 2.5): the pure roll, then the window on a fake modal clock.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "ScriptHarness.h"
#include "games/choc2/DiceGame.h"
#include "games/choc2/Choc2DiceNatives.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
using game::choc2::DiceGame;
using game::choc2::DiceRoll;

namespace {

// The faces a fresh stream seeded like the harness's gives, n draws of range(1, 6).
std::vector<int> expectedFaces(int skip, int n) {
  script::Rng r(1);
  for (int i = 0; i < skip; ++i) r.range(1, 6);
  std::vector<int> v;
  for (int i = 0; i < n; ++i) v.push_back(r.range(1, 6));
  return v;
}

// Tick times at which the dice of a roll stop, in 62 ms ticks.
std::vector<int> stopTimes(DiceRoll& roll) {
  std::vector<int> t;
  for (int ms = 0; roll.rolling() && ms < 20000;) {
    ms += DiceRoll::kTickMs;
    for (int d : roll.tick()) {
      (void)d;
      t.push_back(ms);
    }
  }
  return t;
}

struct DiceEnv : test::ScriptEnv {
  DiceEnv() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/choc2_ledger") {
    script::registerChoc2DiceNatives(lua.L(), ctx);
    modal = &wm.pushModal("m");
    modal->setRect({0, 0, 800, 600});
    game = static_cast<DiceGame*>(&modal->adoptFront(std::make_unique<DiceGame>(wm, ctx)));
    game->setRect({0, 0, 800, 600});
    for (int i = 0; i < 8; ++i) {
      auto b = std::make_unique<ui::BitmapWindow>(wm);
      auto* raw = b.get();
      game->adoptFront(std::move(b));
      raw->setName("die_" + std::to_string(i));
      raw->setBitmap("image/die_unknown", 1.0);
    }
  }
  // Runs the modal clock to `ms` after the start, one platform frame of 16 ms at a time.
  void advance(double ms) {
    const double end = now + ms;
    while (now < end) {
      now = std::min(now + 16, end);
      wm.tickWindowAnimations(now);
    }
  }
  std::string die(int i) {
    for (const auto& c : game->children())
      if (c->name() == "die_" + std::to_string(i)) return c->describe();
    return "";
  }
  ui::Window* modal = nullptr;
  DiceGame* game = nullptr;
  double now = 0;
};

}  // namespace

TEST_CASE("dice roll: two dice stop at 2790 and 3534 ms, six at 2790 to 6510 ms") {
  script::Rng rng(1);
  DiceRoll a;
  a.start({0, 1}, rng);
  CHECK(stopTimes(a) == std::vector<int>{2790, 3534});
  DiceRoll b;
  b.start({2, 3, 4, 5, 6, 7}, rng);
  CHECK(stopTimes(b) == std::vector<int>{2790, 3534, 4278, 5022, 5766, 6510});
  CHECK_FALSE(b.rolling());
}

TEST_CASE("dice roll: the faces come from the game stream, 2 draws then 6, in list order, and only those") {
  script::Rng rng(1);
  DiceRoll r;
  r.start({0, 1}, rng);
  const auto first = expectedFaces(0, 2);
  CHECK(r.face[0] == first[0]);
  CHECK(r.face[1] == first[1]);
  r.start({2, 3, 4, 5, 6, 7}, rng);
  const auto second = expectedFaces(2, 6);
  for (int i = 0; i < 6; ++i) CHECK(r.face[2 + i] == second[i]);
  script::Rng ref(1);
  for (int i = 0; i < 8; ++i) ref.range(1, 6);
  CHECK(rng.range(1, 1000) == ref.range(1, 1000));  // the next draw is the ninth: nothing else consumed the stream
}

TEST_CASE("dice roll: StopOne makes the next die stop on the next tick, and totals count stopped dice only") {
  script::Rng rng(1);
  DiceRoll r;
  r.start({0, 1, 2, 3}, rng);
  r.stopOne();  // while still spinning
  CHECK(r.tick() == std::vector<int>{0});
  CHECK(r.playerTotal() == r.face[0]);
  CHECK(r.characterTotal() == 0);
  r.stopOne();
  CHECK(r.tick() == std::vector<int>{1});
  r.stopOne();
  r.stopOne();  // twice before a tick is still one die
  CHECK(r.tick() == std::vector<int>{2});
  CHECK(r.remaining() == 1);
  CHECK(r.tick().empty());
  r.stopOne();
  CHECK(r.tick() == std::vector<int>{3});
  CHECK_FALSE(r.rolling());
  r.stopOne();  // idle: nothing
  CHECK(r.tick().empty());
  CHECK(r.playerTotal() == r.face[0] + r.face[1] + r.face[2] + r.face[3]);
}

TEST_CASE("dice game: RollFirst then RollSecond on the modal clock count gDiceRollCount 0, 1, 2 then up to 8") {
  DiceEnv e;
  REQUIRE(e.run("gDiceRollCount = 0; RollFirst()") == 0);
  const auto f = expectedFaces(0, 8);
  CHECK(e.game->roll().face[0] == f[0]);
  CHECK(e.game->roll().face[1] == f[1]);
  CHECK(e.getNumber("gDiceRollCount") == 0);
  CHECK(e.die(0).find("image/die_unknown") == std::string::npos);  // spinning: a face already
  CHECK(e.die(2).find("image/die_unknown") != std::string::npos);  // not rolled yet
  e.advance(2700);
  CHECK(e.getNumber("gDiceRollCount") == 0);
  e.advance(100);  // 2800 ms: the first die stopped at 2790
  CHECK(e.getNumber("gDiceRollCount") == 1);
  CHECK(e.die(0).find("image/die" + std::to_string(f[0])) != std::string::npos);
  e.advance(740);  // 3540 ms
  CHECK(e.getNumber("gDiceRollCount") == 2);
  CHECK(e.game->playerTotal() == f[0] + f[1]);
  CHECK(e.game->characterTotal() == 0);
  CHECK(e.wm.windowAnimationCount() == 0);
  CHECK(e.die(2).find("image/die_unknown") != std::string::npos);

  REQUIRE(e.run("RollSecond(); RollFirst()") == 0);  // the second is ignored while the first call runs
  e.advance(2800);
  CHECK(e.getNumber("gDiceRollCount") == 3);
  e.advance(3800);  // 6600 ms
  CHECK(e.getNumber("gDiceRollCount") == 8);
  for (int i = 0; i < 8; ++i) CHECK(e.game->roll().face[i] == f[i]);  // all eight from the game stream, in die order
  {
    script::Rng ref(1);
    for (int i = 0; i < 8; ++i) ref.range(1, 6);
    CHECK(e.ctx.rng.range(1, 1000) == ref.range(1, 1000));  // nothing else drew from it
  }
  CHECK_FALSE(e.game->roll().rolling());
  int p = 0, c = 0;
  for (int i = 0; i < 4; ++i) p += e.game->roll().face[i];
  for (int i = 4; i < 8; ++i) c += e.game->roll().face[i];
  CHECK(e.game->playerTotal() == p);
  CHECK(e.game->characterTotal() == c);
  CHECK(e.getNumber("gDiceRollCount") == 8);
  for (int i = 0; i < 8; ++i) CHECK(e.die(i).find("image/die_unknown") == std::string::npos);
}

TEST_CASE("dice game: RollFirst during a roll, StopOne idle and the natives with no window are no-ops") {
  DiceEnv e;
  REQUIRE(e.run("gDiceRollCount = 0; StopOne(); RollFirst(); RollFirst()") == 0);
  CHECK(e.game->rolls() == 1);
  e.advance(500);
  REQUIRE(e.run("StopOne()") == 0);
  e.advance(100);  // the next die stops long before 2790 ms
  CHECK(e.getNumber("gDiceRollCount") == 1);
  e.advance(100);
  CHECK(e.getNumber("gDiceRollCount") == 1);
  e.wm.destroyWindow(*e.game);
  e.wm.clearDestroyed();
  e.game = nullptr;
  CHECK(DiceGame::active() == nullptr);
  REQUIRE(e.run("RollFirst(); RollSecond(); StopOne(); t = PlayerTotal() + CharacterTotal()") == 0);
  CHECK(e.getNumber("t") == 0);
  CHECK(e.log.contains("[native-warning] RollFirst: there is no DiceGame"));
}

TEST_CASE("dice game: a window destroyed mid-roll stops cleanly") {
  DiceEnv e;
  REQUIRE(e.run("gDiceRollCount = 0; RollFirst()") == 0);
  e.advance(3000);  // the first die stopped at 2790
  CHECK(e.getNumber("gDiceRollCount") == 1);
  e.wm.destroyWindow(*e.game);
  e.game = nullptr;
  CHECK(DiceGame::active() == nullptr);
  e.advance(200);  // before it is cleared away
  e.wm.clearDestroyed();
  e.advance(10000);
  CHECK(e.getNumber("gDiceRollCount") == 1);
  CHECK(e.wm.windowAnimationCount() == 0);
}

TEST_CASE("dice game: a nil gDiceRollCount counts from 0") {
  DiceEnv e;
  REQUIRE(e.run("gDiceRollCount = nil; RollFirst()") == 0);
  e.advance(2800);
  CHECK(e.getNumber("gDiceRollCount") == 1);
}
