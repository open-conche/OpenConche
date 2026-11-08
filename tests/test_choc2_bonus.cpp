// Chocolatier 2 BonusGame (docs/design/choc2-minigames.md 2.6): a CratesGame built from CratesSpec::bonus().
#include <doctest/doctest.h>

#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

#include "ScriptHarness.h"
#include "games/common/Crates.h"
#include "script/ScriptHost.h"
#include "games/choc3/Choc3Natives.h"
#include "games/choc2/Choc2BonusNatives.h"
#include "ui/Style.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
using game::CratesGame;
using game::CratesSpec;
using State = game::CratesGame::State;

namespace {

// A BonusGame over 25 plain children laid out as ui/gamble_bonus.lua does: 50x47 buttons, 60 px apart, 5 columns.
struct BonusFixture : test::ScriptEnv {
  BonusFixture() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/choc2_ledger") {
    registerCratesNatives(lua.L(), ctx);
    script::registerChoc2BonusNatives(lua.L(), ctx);
    modal = &wm.pushModal("m");
    modal->setRect({0, 0, 800, 600});
    auto w = wm.createWindow("BonusGame");
    REQUIRE(w);
    game = dynamic_cast<CratesGame*>(w.get());
    REQUIRE(game);
    modal->adoptFront(std::move(w));
    game->setRect({0, 0, 800, 600});
    for (int i = 1; i <= 25; ++i) {
      auto b = std::make_unique<ui::PlaceholderWindow>(wm, "Button");
      auto* raw = b.get();
      game->adoptFront(std::move(b));
      raw->setName("select_" + std::to_string(i));
      raw->setRect({((i - 1) % 5) * 60 + 260, ((i - 1) / 5) * 60 + 120, 50, 47});
    }
    ui::MapStyle empty;
    game->postChildrenInit(empty);
  }
  ui::Window* modal = nullptr;
  CratesGame* game = nullptr;
};

double sumOf(const std::vector<double>& v) { return std::accumulate(v.begin(), v.end(), 0.0); }
unsigned sumOf(const std::vector<unsigned>& v) { return std::accumulate(v.begin(), v.end(), 0u); }

}  // namespace

TEST_CASE("bonus: the spec maps the Chocolatier 2 values") {
  const CratesSpec& s = CratesSpec::bonus();
  CHECK(std::string(s.type) == "BonusGame");
  CHECK(s.count == 25);
  CHECK(s.scale == 0.5f);
  CHECK(s.factors.size() == 25);
  CHECK(std::string(s.closed) == "image/large_crate");
  CHECK(std::string(s.open) == "image/large_crate_open");
  CHECK(std::string(s.mask) == "image/large_crate_front");
  CHECK(std::string(s.whammy) == "image/whammy_devil");
  CHECK(std::string(s.soundDir) == "sfx/");
  CHECK(std::string(s.soundClick) == "crate_click");
  CHECK(std::string(s.soundMoney) == "crate_with_money");
  CHECK(std::string(s.soundNoMoney) == "crate_no_money");
}

TEST_CASE("bonus: the guessed factors sum to 6.16 with four whammies") {
  const auto& f = CratesSpec::bonus().factors;
  CHECK(sumOf(f) == doctest::Approx(6.16));
  CHECK(std::count(f.begin(), f.end(), 0.0) == 4);
  CHECK(std::count(f.begin(), f.end(), 0.435) == 4);
  CHECK(std::count(f.begin(), f.end(), 0.35) == 5);
  CHECK(std::count(f.begin(), f.end(), 0.25) == 5);
  CHECK(std::count(f.begin(), f.end(), 0.24) == 3);
}

TEST_CASE("bonus: values round as Chocolatier 3's do, and sum to 6.16 x the bet at a round bet") {
  const auto v = CratesGame::baseValues(10000, CratesSpec::bonus());
  REQUIRE(v.size() == 25);
  CHECK(v[0] == 4300);  // 4350 -> 4300
  CHECK(v[4] == 4000);
  CHECK(v[24] == 0);
  CHECK(sumOf(v) == 4300 * 4 + 4000 + 3500 * 5 + 2500 * 5 + 2400 * 3 + 1500 + 1000 + 500);
  const auto small = CratesGame::baseValues(100, CratesSpec::bonus());
  CHECK(small[0] == 100);  // floor of 100 for a non-empty crate
  CHECK(small[24] == 0);
  CHECK(sumOf(CratesGame::baseValues(1000000, CratesSpec::bonus())) == 6160000);
}

TEST_CASE("bonus: the 25-crate shuffle makes 25 picks in slot order") {
  std::vector<unsigned> v(25);
  std::iota(v.begin(), v.end(), 0u);
  int calls = 0;
  CratesGame::shuffle(v, [&] { return calls++; });  // pick k for slot k: every swap is with itself
  CHECK(calls == 25);
  CHECK(v[24] == 24);
  CHECK(v[3] == 3);
  calls = 0;
  CratesGame::shuffle(v, [&] { return 24 - calls++; });  // slot k swaps with 24 - k, in order
  CHECK(calls == 25);
  auto sorted = v;
  std::sort(sorted.begin(), sorted.end());
  std::vector<unsigned> id(25);
  std::iota(id.begin(), id.end(), 0u);
  CHECK(sorted == id);
}

TEST_CASE("bonus: PrepareGame draws range(0,24) 25 times and returns the sum") {
  BonusFixture f;
  f.rng.seed(9);
  script::Rng ref(9);
  std::vector<unsigned> expect = CratesGame::baseValues(1000000, CratesSpec::bonus());
  for (std::size_t k = 0; k < expect.size(); ++k) std::swap(expect[k], expect[static_cast<std::size_t>(ref.range(0, 24))]);
  REQUIRE(f.run("total = PrepareGame(1000000)") == 0);
  CHECK(f.getNumber("total") == 6160000);
  for (int i = 1; i <= 25; ++i) CHECK(f.game->crate(i).value == expect[static_cast<std::size_t>(i - 1)]);
  // The stream has advanced by exactly those 25 draws.
  CHECK(f.rng.range(0, 1000000) == ref.range(0, 1000000));
  int bombs = 0;
  for (int i = 1; i <= 25; ++i) bombs += f.game->crate(i).bomb;
  CHECK(bombs == 4);
}

TEST_CASE("bonus: SelectPrize opens any of the 25 crates through Lua") {
  BonusFixture f;
  f.rng.seed(3);
  REQUIRE(f.run("PrepareGame(1000000); sum = 0; zeros = 0; for i = 1, 25 do local v = SelectPrize(i, false); sum = sum + v; if v == 0 then zeros = zeros + 1 end end") == 0);
  CHECK(f.getNumber("sum") == 6160000);
  CHECK(f.getNumber("zeros") == 4);
  for (int i = 1; i <= 25; ++i) CHECK(f.game->crate(i).state == State::Opening);
  REQUIRE(f.run("a = SelectPrize(0, false); b = SelectPrize(26, false)") == 0);
  CHECK(f.getNumber("a") == 0);
  CHECK(f.getNumber("b") == 0);
  CHECK(f.game->typeName() == "BonusGame");
}

TEST_CASE("bonus: the layout comes from the 25 select_N children") {
  BonusFixture f;
  CHECK(f.game->crate(1).x == 285);  // 260 + 25
  CHECK(f.game->crate(5).x == 285 + 4 * 60);
  CHECK(f.game->crate(6).x == 285);
  CHECK(f.game->crate(25).y == doctest::Approx(143.5 + 4 * 60));
  CHECK(f.game->crate(25).rect.w == 90);
}

TEST_CASE("bonus: Chocolatier 3's spec is unchanged") {
  const CratesSpec& s = CratesSpec::chocolatier3();
  CHECK(std::string(s.type) == "CratesGame");
  CHECK(s.count == 21);
  CHECK(s.scale == 0.4f);
  CHECK(sumOf(s.factors) == doctest::Approx(5.125));
  CHECK(s.factors == std::vector<double>(CratesGame::prizeFactors().begin(), CratesGame::prizeFactors().end()));
  CHECK(std::string(s.closed) == "image/box_closed");
  CHECK(std::string(s.open) == "image/box_open");
  CHECK(std::string(s.mask) == "image/box_open_mask");
  CHECK(std::string(s.whammy) == "image/whammy_devil");
  CHECK(std::string(s.soundDir).empty());
  test::ScriptEnv env(std::string(CHOCO_TEST_ASSETS) + "/ledger");
  CratesGame g(env.wm, env.ctx);
  CHECK(&g.spec() == &CratesSpec::chocolatier3());
  CHECK(g.typeName() == "CratesGame");
  CHECK(g.count() == 21);
}
