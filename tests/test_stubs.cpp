#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "ScriptHarness.h"
#include "games/choc3/SlotMachine.h"
#include "script/LuaStyle.h"
#include "ui/Style.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
using game::SlotMachine;

namespace {

// A SlotMachine in a modal, its callbacks read from the Lua table `gStyle` as the dialog script would give them.
struct SlotFixture : test::ScriptEnv {
  SlotFixture() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ledger") {
    game::registerStubNatives(lua.L(), ctx);
    REQUIRE(run("gMoney = 1000; gBet = 100; gPayout = 0; gOk = false; gOrder = ''\n"
                "function AllStopped() gOrder = gOrder .. 's'; gMoney = gMoney + Floor(gBet * gPayout + 0.9) end\n"
                "function PaidOut() gOrder = gOrder .. 'p'; gOk = true end\n"
                "function Pull() gMoney = gMoney - gBet; gOk = false; StartSlots(1, 2, 3, 4, gPayout) end\n"
                "gStyle = { onstopped = AllStopped, onpayout = PaidOut, options = { 'dollars', 'a', 'b', 'c' },\n"
                "  wheel1 = { x = 94, y = 156 }, wheel2 = { x = 155, y = 156 }, wheel3 = { x = 216, y = 156 }, wheel4 = { x = 277, y = 156 } }\n"
                "function Floor(x) return tonumber(string.format('%d', x)) end") == 0);
    modal = &wm.pushModal("m");
    modal->setRect({0, 0, 800, 600});
    auto owned = std::make_unique<SlotMachine>(wm, ctx);
    machine = static_cast<SlotMachine*>(&modal->adoptFront(std::move(owned)));
    lua_State* L = lua.L();
    lua_getglobal(L, "gStyle");
    const script::LuaStyle view(L, lua_gettop(L), &lua);
    machine->init(view);
    lua_pop(L, 1);
  }
  // Makes every wheel run `steps` steps at `speed` (a step every 200/speed ticks) instead of the random draw.
  void fix(int steps, int speed) {
    for (int i = 0; i < 4; ++i) {
      machine->wheel(i).stepsLeft = steps;
      machine->wheel(i).speed = speed;
    }
  }
  // One 50 ms animation tick (the first call is tick 1).
  void tick() { wm.tickWindowAnimations(50.0 * ++ticks); }
  ui::Window* modal = nullptr;
  SlotMachine* machine = nullptr;
  int ticks = 0;
};

}  // namespace

TEST_CASE("slots: winnings are Floor(bet * payout + 0.9)") {
  CHECK(SlotMachine::winnings(100, 0) == 0);
  CHECK(SlotMachine::winnings(100, 1) == 100);
  CHECK(SlotMachine::winnings(100, 2.5) == 250);
  CHECK(SlotMachine::winnings(100, 8) == 800);
  CHECK(SlotMachine::winnings(33, 2.5) == 83);  // 82.5 + 0.9 = 83.4
  CHECK(SlotMachine::winnings(3, 1) == 3);
}

TEST_CASE("slots: the wheels run, onstopped fires when the last wheel stops, then onpayout, and the net money change is winnings - bet") {
  for (double payout : {0.0, 1.0, 2.5, 8.0}) {
    SlotFixture fx;
    REQUIRE(fx.run("gPayout = " + std::to_string(payout) + "; Pull()") == 0);
    CHECK(fx.getString("gOrder") == "");  // deferred to the animation
    CHECK(fx.machine->spinning());
    fx.fix(20, 100);  // a step every second tick: stops on tick 40
    for (int k = 1; k < 40; ++k) fx.tick();
    CHECK(fx.getString("gOrder") == "");
    CHECK(fx.machine->spinning());
    fx.tick();
    CHECK(!fx.machine->spinning());
    if (payout > 0) {
      // The dollar sparkle runs first: gSparkleTime = payout*0.5 + 1 seconds of 50 ms ticks, then onpayout.
      CHECK(fx.getString("gOrder") == "s");
      CHECK(!fx.machine->sparkle().particles().active().empty());
      for (int k = 0; k < 200; ++k) fx.tick();
    }
    CHECK(fx.getString("gOrder") == "sp");  // and only once
    if (payout == 0) CHECK(fx.machine->sparkle().particles().active().empty());  // no payout, no sparkle
    CHECK(fx.log.contains("SoundEvent Stop_machine_spin"));
    CHECK(fx.log.contains("SoundEvent Stop_win_money"));
    CHECK(fx.getString("gOk") == "true");
    CHECK(fx.getNumber("gMoney") == doctest::Approx(static_cast<double>(1000 + SlotMachine::winnings(100, payout) - 100)));
    CHECK(fx.machine->spins() == 1);
    CHECK(fx.machine->wheels()[3] == 4);
  }
}

TEST_CASE("slots: onpayout arrives after exactly payout*0.5+1 seconds of 50 ms ticks counted from the tick the last wheel stops") {
  for (auto [payout, ticks] : {std::pair{1.0, 30}, std::pair{2.5, 45}, std::pair{8.0, 100}}) {
    SlotFixture fx;
    REQUIRE(fx.run("gPayout = " + std::to_string(payout) + "; Pull()") == 0);
    fx.fix(20, 100);
    for (int k = 1; k < 40; ++k) fx.tick();
    CHECK(fx.getString("gOrder") == "");
    fx.tick();  // tick 40: the last wheel stops, onstopped runs and the first sparkle update counts
    CHECK(fx.getString("gOrder") == "s");
    // gSparkleTime is a double decremented by 0.05, so for 2.25 and 5 the rounding residue may cost one more tick (as in
    // the Lua original); payout 1 lands exactly on 30.
    int paid = 0;
    for (int k = 2; k <= ticks + 1 && !paid; ++k) {
      fx.tick();
      if (fx.getString("gOrder") == "sp") paid = k;
      else CHECK(fx.getString("gOrder") == "s");
    }
    CHECK(paid >= ticks);
    CHECK(paid <= ticks + 1);
    if (payout == 1.0) CHECK(paid == 30);
  }
}

TEST_CASE("slots: a loss runs onpayout in the same tick as onstopped") {
  SlotFixture fx;
  REQUIRE(fx.run("gPayout = 0; Pull()") == 0);
  fx.fix(20, 100);
  for (int k = 1; k < 40; ++k) fx.tick();
  CHECK(fx.getString("gOrder") == "");
  fx.tick();
  CHECK(fx.getString("gOrder") == "sp");
  CHECK(fx.getString("gOk") == "true");
}

TEST_CASE("slots: StartSlots draws steps then speed per wheel in order from the game stream") {
  SlotFixture fx;
  script::Rng probe;
  probe.seed(77);
  fx.rng.seed(77);
  const int draws[4] = {1, 2, 3, 4};
  fx.machine->spin(draws, 0);
  for (int i = 0; i < 4; ++i) {
    const int steps = probe.range(20, 50), speed = probe.range(90, 110);
    CHECK(fx.machine->wheel(i).stepsLeft == steps);
    CHECK(fx.machine->wheel(i).speed == speed);
    CHECK(fx.machine->wheel(i).target == i);
    CHECK(fx.machine->wheel(i).stepsLeft >= 20);
    CHECK(fx.machine->wheel(i).stepsLeft <= 50);
    CHECK(fx.machine->wheel(i).speed >= 90);
    CHECK(fx.machine->wheel(i).speed <= 110);
  }
}

TEST_CASE("slots: a wheel steps every 200/speed ticks, drifts between steps and lands on the target at rest") {
  SlotFixture fx;
  REQUIRE(fx.run("Pull()") == 0);
  fx.fix(20, 100);
  const double base = 156;
  fx.tick();  // phase 100: no step yet, the strip is a half pitch down
  CHECK(fx.machine->wheel(0).stepsLeft == 20);
  CHECK(fx.machine->wheel(0).phase == 100);
  CHECK(static_cast<double>(fx.machine->wheel(0).stripY) == doctest::Approx(base + 100 * 60.0 / 200 * SlotMachine::kStripScale));
  fx.tick();  // phase 200: a step
  CHECK(fx.machine->wheel(0).stepsLeft == 19);
  CHECK(fx.machine->wheel(0).phase == 0);
  CHECK(static_cast<double>(fx.machine->wheel(0).stripY) == doctest::Approx(base));
  for (int k = 3; k <= 40; ++k) fx.tick();
  const auto& w = fx.machine->wheel(2);
  CHECK(w.stepsLeft == 0);
  CHECK(static_cast<double>(w.stripY) == doctest::Approx(base));  // snapped to rest
  CHECK(w.names[2] == fx.machine->options()[2]);                  // the target (option 3) is the centre symbol
  CHECK(fx.machine->wheel(0).names[2] == "dollars");
  CHECK(fx.machine->wheel(3).names[2] == "c");
  CHECK(fx.log.contains("SoundEvent spin_stop"));
}

TEST_CASE("slots: each step draws one new top symbol from the game stream") {
  SlotFixture fx;
  REQUIRE(fx.run("Pull()") == 0);
  fx.fix(20, 100);
  for (int i = 1; i < 4; ++i) fx.machine->wheel(i).stepsLeft = 0;  // only wheel 1 runs
  script::Rng probe;
  probe.seed(5);
  fx.rng.seed(5);
  fx.tick();
  fx.tick();  // the first step
  CHECK(fx.machine->wheel(0).names[0] == fx.machine->options()[static_cast<std::size_t>(probe.range(0, 3))]);
}

TEST_CASE("slots: the wheels stop in the order of their lengths; Stop_machine_spin and onstopped come on the last wheel's tick") {
  SlotFixture fx;
  REQUIRE(fx.run("gPayout = 0; Pull()") == 0);
  const int steps[4] = {30, 20, 25, 22};  // wheel 2 stops first, wheel 1 last
  for (int i = 0; i < 4; ++i) {
    fx.machine->wheel(i).stepsLeft = steps[i];
    fx.machine->wheel(i).speed = 100;
  }
  std::vector<int> order;
  int lastTick = 0;
  for (int k = 1; k <= 80; ++k) {
    int running[4];
    for (int i = 0; i < 4; ++i) running[i] = fx.machine->wheel(i).stepsLeft;
    fx.tick();
    for (int i = 0; i < 4; ++i)
      if (running[i] != 0 && fx.machine->wheel(i).stepsLeft == 0) order.push_back(i);
    if (!fx.machine->spinning()) {
      lastTick = k;
      break;
    }
    CHECK(!fx.log.contains("SoundEvent Stop_machine_spin"));
    CHECK(fx.getString("gOrder") == "");
  }
  CHECK(order == std::vector<int>{1, 3, 2, 0});
  CHECK(lastTick == 60);
  CHECK(fx.log.contains("SoundEvent Stop_machine_spin"));
  CHECK(fx.getString("gOrder") == "sp");
}

TEST_CASE("slots: StopSlots cuts the first spinning wheel to 3 steps, one wheel per call") {
  SlotFixture fx;
  REQUIRE(fx.run("Pull()") == 0);
  fx.fix(30, 100);
  fx.machine->wheel(0).stepsLeft = 2;  // already below 3: left alone, and it is the one the call looks at
  REQUIRE(fx.run("StopSlots()") == 0);
  CHECK(fx.machine->wheel(0).stepsLeft == 2);
  CHECK(fx.machine->wheel(1).stepsLeft == 30);
  fx.machine->wheel(0).stepsLeft = 0;
  REQUIRE(fx.run("StopSlots()") == 0);
  CHECK(fx.machine->wheel(1).stepsLeft == 3);
  CHECK(fx.machine->wheel(2).stepsLeft == 30);
  REQUIRE(fx.run("StopSlots()") == 0);  // the first spinning wheel is already at 3: nothing changes
  CHECK(fx.machine->wheel(1).stepsLeft == 3);
  CHECK(fx.machine->wheel(2).stepsLeft == 30);
}

TEST_CASE("slots: StopSlots and StartSlots without a window only log") {
  test::ScriptEnv env(std::string(CHOCO_TEST_ASSETS) + "/ledger");
  game::registerStubNatives(env.lua.L(), env.ctx);
  CHECK(env.run("StopSlots(); StartSlots(1, 2, 3, 4, 1)") == 0);
}

TEST_CASE("slots: a failing callback is logged and onpayout still runs") {
  SlotFixture fx;
  REQUIRE(fx.run("gStyle2 = { onstopped = function() error('boom') end, onpayout = PaidOut }") == 0);
  lua_State* L = fx.lua.L();
  lua_getglobal(L, "gStyle2");
  const script::LuaStyle view(L, lua_gettop(L), &fx.lua);
  fx.machine->init(view);
  lua_pop(L, 1);
  REQUIRE(fx.run("StartSlots(1, 1, 1, 1, 0)") == 0);
  fx.fix(20, 100);
  for (int k = 0; k < 40; ++k) fx.tick();
  CHECK(fx.getString("gOk") == "true");
  CHECK(fx.log.contains("[lua-error]"));
  CHECK(fx.log.contains("boom"));
}

TEST_CASE("placeholders: SignEditor and CurveEditor create, take children and close") {
  test::ScriptEnv env(std::string(CHOCO_TEST_ASSETS) + "/ledger");
  game::registerStubNatives(env.lua.L(), env.ctx);
  ui::Window& modal = env.wm.pushModal("m");
  modal.setRect({0, 0, 800, 600});
  for (const char* type : {"SignEditor", "CurveEditor"}) {
    ui::Window& w = modal.adoptFront(env.wm.createWindow(type));
    CHECK(w.typeName() == type);
    ui::MapStyle st;
    w.init(st);
    w.adoptFront(std::make_unique<ui::PlaceholderWindow>(env.wm, "Button"));
    env.wm.destroyWindow(w);
    env.wm.clearDestroyed();
  }
  // Their natives (SetBackground, SetAnimScale, ...) are the generated logged stubs, installed by ScriptHost::boot.
}
