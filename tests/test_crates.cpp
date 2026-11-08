#include <doctest/doctest.h>

#include <algorithm>
#include <numeric>
#include <string>

#include "ScriptHarness.h"
#include "games/common/Crates.h"
#include "script/ScriptHost.h"
#include "games/choc3/Choc3Natives.h"
#include "ui/Layout.h"
#include "ui/Style.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
using game::CratesGame;
using State = game::CratesGame::State;

namespace {

// A crates window over 21 plain children laid out as ui/ui_crates.lua does (50x47 buttons, 60/55 px apart).
struct CratesFixture : test::ScriptEnv {
  CratesFixture() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ledger") {
    registerCratesNatives(lua.L(), ctx);
    modal = &wm.pushModal("m");
    modal->setRect({0, 0, 800, 600});
    auto owned = std::make_unique<CratesGame>(wm, ctx);
    game = static_cast<CratesGame*>(&modal->adoptFront(std::move(owned)));
    game->setRect({0, 0, 800, 600});
    for (int i = 1; i <= 21; ++i) {
      auto b = std::make_unique<ui::PlaceholderWindow>(wm, "Button");
      auto* raw = b.get();
      game->adoptFront(std::move(b));
      raw->setName("select_" + std::to_string(i));
      raw->setRect({((i - 1) % 7) * 60 + 226, ((i - 1) / 7) * 55 + 95, 50, 47});
    }
    ui::MapStyle empty;
    game->postChildrenInit(empty);
  }
  ui::Window* modal = nullptr;
  CratesGame* game = nullptr;
};

unsigned sumOf(const std::array<unsigned, 21>& v) { return std::accumulate(v.begin(), v.end(), 0u); }

}  // namespace

TEST_CASE("crates: the factor table sums to 5.125 and has three empty crates") {
  const auto& f = CratesGame::prizeFactors();
  CHECK(std::accumulate(f.begin(), f.end(), 0.0) == doctest::Approx(5.125));
  CHECK(std::count(f.begin(), f.end(), 0.0) == 3);
  CHECK(f[0] == 0.435);
  CHECK(f[20] == 0.0);
}

TEST_CASE("crates: values round down to hundreds, with a floor of 100 for non-empty crates") {
  const auto v = CratesGame::baseValues(10000);
  CHECK(v[0] == 4300);  // 4350 -> 4300
  CHECK(v[3] == 4000);
  CHECK(v[4] == 3500);
  CHECK(v[12] == 2400);
  CHECK(v[16] == 1000);
  CHECK(v[17] == 500);
  CHECK(v[18] == 0);
  CHECK(sumOf(v) == 4300 * 3 + 4000 + 3500 * 4 + 2500 * 4 + 2400 * 3 + 1500 + 1000 + 500);
  const auto small = CratesGame::baseValues(100);  // 43.5 + 0.5 -> 44 -> 0 -> floor 100; empties stay 0
  CHECK(small[0] == 100);
  CHECK(small[17] == 100);
  CHECK(small[20] == 0);
  const auto exact = CratesGame::baseValues(1000000);
  CHECK(sumOf(exact) == 5125000);  // all multiples of 100 at this bet: exactly 5.125 x the bet
  CHECK(std::count(exact.begin(), exact.end(), 0u) == 3);
}

TEST_CASE("crates: the single-pass shuffle swaps each slot with a picked one") {
  std::array<unsigned, 21> v;
  std::iota(v.begin(), v.end(), 0u);
  int calls = 0;
  CratesGame::shuffle(v, [&] { ++calls; return 0; });  // always slot 0
  CHECK(calls == 21);
  // Slot 0 swaps with itself, then every k swaps with 0: a rotation.
  CHECK(v[0] == 20);
  CHECK(v[1] == 0);
  CHECK(v[20] == 19);
  // A fixed pick sequence is reproducible, and it is a permutation.
  auto run = [](unsigned seed) {
    script::Rng rng(seed);
    std::array<unsigned, 21> a = CratesGame::baseValues(5000);
    CratesGame::shuffle(a, [&] { return rng.range(0, 20); });
    return a;
  };
  CHECK(run(7) == run(7));
  CHECK(run(7) != run(8));
  auto sorted = run(7), base = CratesGame::baseValues(5000);
  std::sort(sorted.begin(), sorted.end());
  std::sort(base.begin(), base.end());
  CHECK(sorted == base);
}

TEST_CASE("crates: the animation curves") {
  CHECK(CratesGame::valueScale(0) == doctest::Approx(1.0));
  CHECK(CratesGame::valueScale(400) == doctest::Approx(1.5));
  CHECK(CratesGame::valueScale(600) == doctest::Approx(1.25));
  CHECK(CratesGame::valueScale(800) == doctest::Approx(1.0));
  CHECK(CratesGame::valueScale(5000) == 1.0);
  CHECK(CratesGame::whammyScale(0, false) == doctest::Approx(0.5));
  CHECK(CratesGame::whammyScale(500, false) == doctest::Approx(1.0));
  CHECK(CratesGame::whammyScale(750, false) == doctest::Approx(0.75));
  CHECK(CratesGame::whammyScale(1000, false) == doctest::Approx(0.5));
  CHECK(CratesGame::whammyScale(300, true) == 0.25);
}

TEST_CASE("crates: the layout comes from the select_N children") {
  CratesFixture f;
  const auto& c1 = f.game->crate(1);
  CHECK(c1.x == 251);  // 226 + 25
  CHECK(c1.y == doctest::Approx(118.5));
  CHECK(c1.rect == gfx::RectI{206, 95, 90, 47});  // 20 px wider on each side
  CHECK(f.game->crate(21).x == 251 + 6 * 60);
  CHECK(f.game->crate(21).y == doctest::Approx(118.5 + 110));
  CHECK(f.game->crate(8).y == doctest::Approx(118.5 + 55));
}

TEST_CASE("crates: PrepareGame and SelectPrize through Lua") {
  CratesFixture f;
  f.rng.seed(3);
  REQUIRE(f.run("total = PrepareGame(1000000)") == 0);
  CHECK(f.getNumber("total") == 5125000);
  CHECK(f.game->prepared());
  // Every crate is waiting; select all 21 (1-based) and add the values up.
  REQUIRE(f.run("sum = 0; zeros = 0; for i = 1, 21 do local v = SelectPrize(i, false); sum = sum + v; if v == 0 then zeros = zeros + 1 end end") == 0);
  CHECK(f.getNumber("sum") == 5125000);
  CHECK(f.getNumber("zeros") == 3);
  for (int i = 1; i <= 21; ++i) CHECK(f.game->crate(i).state == State::Opening);
  // A second pick of an open crate returns the same value and changes nothing.
  const unsigned v5 = f.game->crate(5).value;
  f.game->onAnimate();
  REQUIRE(f.run("again = SelectPrize(5, true)") == 0);
  CHECK(f.getNumber("again") == v5);
  CHECK(f.game->crate(5).state == State::Opening);
  CHECK(f.game->crate(5).time == 50);
  // Out of range indexes are 0.
  REQUIRE(f.run("a = SelectPrize(0, false); b = SelectPrize(22, false)") == 0);
  CHECK(f.getNumber("a") == 0);
  CHECK(f.getNumber("b") == 0);
}

TEST_CASE("crates: PrepareGame is deterministic for a seed and varies with it") {
  CratesFixture f;
  auto layout = [&](unsigned seed) {
    f.rng.seed(seed);
    f.game->prepare(2000);
    std::string s;
    for (int i = 1; i <= 21; ++i) s += std::to_string(f.game->crate(i).value) + ",";
    return s;
  };
  CHECK(layout(11) == layout(11));
  CHECK(layout(11) != layout(12));
}

TEST_CASE("crates: opening states and the 50 ms tick") {
  CratesFixture f;
  f.rng.seed(5);
  f.game->prepare(10000);
  int bomb = 0, money = 0, bomb2 = 0;
  for (int i = 1; i <= 21; ++i) {
    if (f.game->crate(i).value == 0) (bomb ? bomb2 : bomb) = i;
    else if (!money) money = i;
  }
  REQUIRE(bomb);
  REQUIRE(bomb2);
  REQUIRE(money);
  CHECK(f.game->onAnimate());  // waiting crates do not advance
  CHECK(f.game->crate(money).time == 0);
  CHECK(f.game->select(money, false) == f.game->crate(money).value);
  CHECK(f.game->crate(money).state == State::Opening);
  CHECK(f.game->select(bomb, false) == 0);
  CHECK(f.game->crate(bomb).state == State::Opening);  // a bomb opened by the player keeps animating
  CHECK(f.game->select(bomb2, true) == 0);
  CHECK(f.game->crate(bomb2).state == State::Blank);   // revealed: blank, and it never advances
  for (int i = 0; i < 4; ++i) CHECK(f.game->onAnimate());
  CHECK(f.game->crate(money).time == 200);
  CHECK(f.game->crate(bomb).time == 200);
  CHECK(f.game->crate(bomb2).time == 0);
  CHECK(f.game->crate(money).final == false);
  // Re-preparing only rewrites the values: states and times stay (they reset when the window is built).
  const State before = f.game->crate(money).state;
  f.game->prepare(10000);
  CHECK(f.game->crate(money).state == before);
  CHECK(f.game->crate(money).time == 200);
  CHECK(f.game->crate(bomb2).state == State::Blank);
}

TEST_CASE("crates: the natives are logged stubs without a window") {
  test::ScriptEnv env(std::string(CHOCO_TEST_ASSETS) + "/ledger");
  registerCratesNatives(env.lua.L(), env.ctx);
  REQUIRE(env.run("t = PrepareGame(100); s = SelectPrize(1, false)") == 0);
  CHECK(env.getNumber("t") == 0);
  CHECK(env.getNumber("s") == 0);
  CHECK(env.stubs.count("PrepareGame") == 1);
}
