// Chocolatier 1 (docs/re/choc1-factory.md): the factory mini-game's rules (FactorySim) on a fixed RNG.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "games/choc1/FactorySim.h"

namespace f1 = choco::game::choc1;
using Sim = f1::FactorySim;
using State = Sim::State;
using Kind = Sim::Event::Kind;

namespace {

Sim::Config config(int trays, std::vector<int> quantities, double startspeed = 1.0) {
  Sim::Config c;
  c.trayCount = trays;
  c.quantities = std::move(quantities);
  c.startspeed = startspeed;
  return c;
}

// A sim over the CRT generator from seed 1 (the exe's default state), counting the calls.
struct Fixture {
  explicit Fixture(Sim::Config c) : rand(1), sim(std::move(c), [this] { ++calls; return rand.next(); }) {}
  f1::CrtRand rand;
  int calls = 0;
  Sim sim;
  std::vector<Sim::Event> events;
  std::vector<int> stamps;  // the elapsed ms of the tick that produced each event
  void tick(int n = 1) {
    for (int i = 0; i < n; ++i) {
      sim.tick();
      for (const auto& e : sim.takeEvents()) {
        events.push_back(e);
        stamps.push_back(sim.elapsedMs());
      }
    }
  }
  // The elapsed ms of the first event of a kind, or -1.
  int firstAt(Kind k) const {
    for (std::size_t i = 0; i < events.size(); ++i)
      if (events[i].kind == k) return stamps[i];
    return -1;
  }
  int count(Kind k) const { return static_cast<int>(std::count_if(events.begin(), events.end(), [k](const Sim::Event& e) { return e.kind == k; })); }
  // Starts the game and runs the countdown out: state 2, elapsed 0.
  void begin() {
    sim.start();
    while (sim.state() == State::Countdown) tick();
  }
  // Ticks until the gun holds an ingredient (at most `limit` ticks).
  bool untilLoaded(int limit = 4000) {
    for (int i = 0; i < limit && sim.loaded() < 0; ++i) tick();
    return sim.loaded() >= 0;
  }
  // Fires at (x, y) and ticks until no shot is in flight.
  void fireAt(int x, int y, int limit = 200) {
    REQUIRE(sim.click(x, y));
    for (int i = 0; i < limit && !sim.shots().empty(); ++i) tick();
  }
};

// A point opposite the (only) tray: a shot there leaves the playfield without touching it.
std::pair<int, int> missAim(const Sim& sim) {
  const Sim::Point p = sim.trayScreen(0, sim.angle());
  return {static_cast<int>(std::lround(600 - p.x)), static_cast<int>(std::lround(600 - p.y))};
}

// A rand() that returns 0 forever: the block shuffle then swaps every entry with its block's first.
int zero() { return 0; }

}  // namespace

TEST_CASE("CrtRand: the MSVC rand() stream from seed 1") {
  f1::CrtRand r;
  CHECK(r.next() == 41);
  CHECK(r.next() == 18467);
  CHECK(r.next() == 6334);
  CHECK(r.next() == 26500);
  CHECK(r.next() == 19169);
}

TEST_CASE("FactorySim: the ingredient queue is len = (60000/rate/U + 1) * U long, one rand() per entry, shuffled in blocks of U") {
  // xdarkbars (a square): 8 trays, sugar 1 + cacao 3 per tray: U = 32, len = (171 / 32 + 1) * 32 = 192.
  Fixture f(config(8, {1, 3}));
  CHECK(f.sim.total() == 4);
  REQUIRE(f.sim.queue().size() == 192);
  CHECK(f.calls == 192);
  for (int block = 0; block < 6; ++block) {
    const auto b = f.sim.queue().begin() + block * 32;
    CHECK(std::count(b, b + 32, 0) == 8);   // one sugar per tray
    CHECK(std::count(b, b + 32, 1) == 24);  // three cacao per tray
  }
  // The shuffle actually mixes: not the template order any more.
  bool ordered = true;
  for (std::size_t i = 0; i < 192; ++i) ordered = ordered && f.sim.queue()[i] == f.sim.slotTemplate()[i % 4];
  CHECK_FALSE(ordered);
  // The same stream gives the same queue; a restart draws len more numbers.
  Fixture g(config(8, {1, 3}));
  CHECK(g.sim.queue() == f.sim.queue());
  f.sim.restart();
  CHECK(f.calls == 384);
  // Other shapes: 7 bars trays of 2 -> U 14, len 182; truffles 6 trays of 6 -> U 36, len 180.
  CHECK(Fixture(config(7, {1, 1})).sim.queue().size() == 182);
  CHECK(Fixture(config(6, {2, 2, 2})).sim.queue().size() == 180);
}

TEST_CASE("FactorySim: the countdown ticks at 410 and 1410 ms and the bell ends it at 2400") {
  Fixture f(config(7, {1, 1}));
  CHECK(f.sim.state() == State::Idle);
  f.sim.start();
  CHECK(f.sim.state() == State::Countdown);
  std::vector<int> tickAt, digits;
  int bellAt = -1;
  for (int i = 0; i < 300 && f.sim.state() == State::Countdown; ++i) {
    f.sim.tick();
    for (const auto& e : f.sim.takeEvents()) {
      if (e.kind == Kind::CountdownTick) tickAt.push_back(f.sim.elapsedMs());
      if (e.kind == Kind::StartBell) bellAt = i * 10 + 10;
    }
    if (f.sim.state() == State::Countdown && (i == 0 || i == 40 || i == 140)) digits.push_back(f.sim.countdownDigit());
  }
  CHECK(tickAt == std::vector<int>{410, 1410});
  CHECK(digits == std::vector<int>{3, 2, 1});  // at 10, 410 and 1410 ms
  CHECK(bellAt == 2400);
  CHECK(f.sim.state() == State::Running);
  CHECK(f.sim.elapsedMs() == 0);
  CHECK_FALSE(f.sim.countdownShown());
}

TEST_CASE("FactorySim: spawns every 350 ms of unblocked time, the belt takes 1142 ms and a loaded gun stops it") {
  Fixture f(config(7, {1, 1}));
  std::size_t queued = f.sim.queue().size();
  f.begin();  // the bell tick is tick 1 of the run
  std::vector<int> spawnTicks;
  int loadedAt = -1;
  // The bell tick runs with dt = 0 (0x410cd9): the spawn clock stays 0, the first ingredient spawns at 0 px, the queue lost one.
  CHECK(f.sim.spawnClock() == 0);
  REQUIRE(f.sim.belt().size() == 1);
  CHECK(f.sim.belt().front().s == 0);
  CHECK(f.sim.queue().size() == queued - 1);
  spawnTicks.push_back(1);
  --queued;
  for (int t = 2; t <= 200; ++t) {
    f.tick();
    if (f.sim.queue().size() < queued) {
      spawnTicks.push_back(t);
      queued = f.sim.queue().size();
    }
    if (loadedAt < 0 && f.sim.loaded() >= 0) loadedAt = t;
  }
  CHECK(spawnTicks == std::vector<int>{1, 36, 71, 106});
  CHECK(loadedAt >= 114);
  CHECK(loadedAt <= 116);  // 571 px at 0.5 px/ms, the node starting 5 px along
  // The gun holds the next ingredient: the belt and the spawn clock stand still (no fifth spawn, 1400 ms never reached).
  CHECK(f.sim.queue().size() == queued);
  CHECK(f.sim.spawnClock() < 1400);
  const int clock = f.sim.spawnClock();
  f.tick(100);
  CHECK(f.sim.spawnClock() == clock);
  // Fire: the belt moves again.
  const int before = static_cast<int>(f.sim.belt().size());
  REQUIRE(f.sim.click(0, 0));
  f.tick(5);
  CHECK(f.sim.spawnClock() > clock);
  CHECK(static_cast<int>(f.sim.belt().size()) >= before - 1);
}

TEST_CASE("FactorySim: the wheel turns counter-clockwise on screen, one revolution per 30 s at speed 1") {
  Fixture f(config(7, {1, 1}));
  f.sim.start();
  CHECK(f.sim.trays()[0].cx() == 525);
  CHECK(f.sim.trays()[0].cy() == 300);
  f.tick(10);
  CHECK(f.sim.angle() == doctest::Approx(-10 * 2 * 3.14159265358979 / 3000));
  CHECK(f.sim.trays()[0].cy() < 300);  // tray 0, on the right, moves up
  f.tick(2991);  // (the bell tick at 2400 ms does not turn the wheel: dt = 0)
  CHECK(f.sim.angle() == doctest::Approx(-2 * 3.14159265358979).epsilon(1e-6));
}

TEST_CASE("FactorySim: a shot aimed at a tray centre hits it 13 ticks later (distance^2 <= 5881) and fills the matching slot") {
  Fixture f(config(1, {1, 1}, 0.0));  // one tray, the wheel stands still: it sits at (525, 300)
  f.begin();
  REQUIRE(f.untilLoaded());
  const int ing = f.sim.loaded();
  f.events.clear();
  REQUIRE(f.sim.click(525, 300));
  CHECK(f.sim.aimX() == doctest::Approx(1.0f));
  CHECK(f.count(Kind::Fire) == 0);  // the events are taken by the caller's tick/take
  f.events = f.sim.takeEvents();
  CHECK(f.count(Kind::Fire) == 1);
  CHECK(f.sim.loaded() == -1);
  f.events.clear();
  for (int n = 1; n <= 12; ++n) {
    f.tick();
    REQUIRE(f.sim.shots().size() == 1);  // x = 300 + 12 n: 81 px from the centre after 12 ticks: out of reach
  }
  f.tick();                              // 13: x = 456, 69 px away
  CHECK(f.sim.shots().empty());
  const auto& tray = f.sim.trays()[0];
  CHECK(tray.filled == 1);
  int filledSlots = 0;
  for (int s : tray.slots) {
    if (s != Sim::kEmpty) {
      ++filledSlots;
      CHECK(s == ing);
    }
  }
  CHECK(filledSlots == 1);
  CHECK(f.count(Kind::Success) == 1);
  CHECK(f.events.front().kind == Kind::Success);
  CHECK(f.events.front().arg == 1);  // ingredient_success_01
  CHECK(Sim::soundName(f.events.front()) == "ingredient_success_01");
  CHECK(f.sim.misses() == 0);
}

TEST_CASE("FactorySim: a shot that passes the tray centre within the hit radius hits, one that passes wider does not") {
  // The aim is normalised and the shot's integer position truncates its fractions every tick, so the offsets are generous:
  // 40 px of aim offset at the tray's distance passes about 38 px from the centre, 120 px about 95 px (radius 76.7).
  for (const int dy : {40, 120}) {
    Fixture f(config(1, {1, 1}, 0.0));
    f.begin();
    REQUIRE(f.untilLoaded());
    f.fireAt(525, 300 + dy, 200);
    if (dy == 40) CHECK(f.sim.trays()[0].filled == 1);
    else CHECK(f.sim.trays()[0].filled == 0);
    if (dy != 40) CHECK(f.sim.misses() == 1);
  }
}

TEST_CASE("FactorySim: a repeated ingredient passes through its full slot and wastes itself at the edge") {
  Fixture f(config(1, {1, 1}, 0.0));
  // rand() == 0: every block of two is swapped to [cacao, sugar], so the belt delivers sugar, cacao, sugar, cacao ...
  f.sim = Sim(config(1, {1, 1}, 0.0), zero);
  f.begin();
  REQUIRE(f.untilLoaded());
  const int a = f.sim.loaded();
  f.fireAt(525, 300);  // into the tray: fills its `a` slot
  CHECK(f.sim.trays()[0].filled == 1);
  REQUIRE(f.untilLoaded());
  const int b = f.sim.loaded();
  CHECK(b != a);
  f.fireAt(0, 0, 400);  // up and to the left: leaves the playfield
  CHECK(f.sim.misses() == 1);
  CHECK(f.sim.trays()[0].filled == 1);
  REQUIRE(f.untilLoaded());
  CHECK(f.sim.loaded() == a);  // the slot for `a` is already filled
  f.fireAt(525, 300, 400);     // straight through the tray, on to the right edge
  CHECK(f.sim.trays()[0].filled == 1);
  CHECK(f.sim.misses() == 2);
  // ingredient_failure_01 and _02, in order.
  std::vector<int> fails;
  for (const auto& e : f.events)
    if (e.kind == Kind::Failure) fails.push_back(e.arg);
  CHECK(fails == std::vector<int>{1, 2});
  CHECK(Sim::soundName({Kind::Failure, 2}) == "ingredient_failure_02");
}

TEST_CASE("FactorySim: a completed tray multiplies the speed by speedup (capped), a miss halves its excess over startspeed") {
  Sim::Config c = config(1, {1, 1});
  c.speedup = 1.5;
  c.maxspeed = 1.6;
  Fixture f(c);
  f.begin();
  CHECK(f.sim.speed() == doctest::Approx(1.0));
  // A miss at start speed changes nothing: (1 - 1) * 0.5 + 1.
  REQUIRE(f.untilLoaded());
  f.fireAt(missAim(f.sim).first, missAim(f.sim).second, 400);
  CHECK(f.sim.speed() == doctest::Approx(1.0));
  // Fill the tray with the harness's lead aim: the speed becomes min(1 * 1.5, 1.6) = 1.5.
  for (int i = 0; i < 60 && f.sim.trays()[0].filled < 1; ++i) {
    REQUIRE(f.untilLoaded());
    if (const auto p = f.sim.bestAim()) f.fireAt(static_cast<int>(std::lround(p->x)), static_cast<int>(std::lround(p->y)));
    else f.fireAt(missAim(f.sim).first, missAim(f.sim).second, 400);
  }
  for (int i = 0; i < 60 && f.count(Kind::Powerup) + f.count(Kind::TemplateComplete) == 0; ++i) {
    REQUIRE(f.untilLoaded());
    if (const auto p = f.sim.bestAim()) f.fireAt(static_cast<int>(std::lround(p->x)), static_cast<int>(std::lround(p->y)));
    else f.fireAt(missAim(f.sim).first, missAim(f.sim).second, 400);
  }
  REQUIRE(f.count(Kind::TemplateComplete) == 1);
  CHECK(f.sim.speed() == doctest::Approx(1.5));
  // A miss now: (1.5 - 1) * 0.5 + 1 = 1.25.
  REQUIRE(f.untilLoaded());
  f.fireAt(missAim(f.sim).first, missAim(f.sim).second, 400);
  CHECK(f.sim.speed() == doctest::Approx(1.25));
}

TEST_CASE("FactorySim: filling the last tray slot plays template_complete, and one tray full means every tray full: the power-up") {
  Fixture f(config(1, {1, 1}, 0.0));  // one tray: its completion is also 'all trays full'
  f.begin();
  for (int i = 0; i < 2; ++i) {
    REQUIRE(f.untilLoaded());
    f.fireAt(525, 300);
  }
  // Not both ingredients? The belt alternates only by the shuffle: keep firing until the tray is done (at most a few more).
  for (int i = 0; i < 10 && f.count(Kind::TemplateComplete) == 0; ++i) {
    REQUIRE(f.untilLoaded());
    f.fireAt(525, 300);
    if (f.sim.trays()[0].filled == 0) break;
  }
  CHECK(f.count(Kind::TemplateComplete) == 1);
  CHECK(f.count(Kind::Powerup) == 1);
  CHECK(f.count(Kind::Snatch) == 1);
  // The power-up collected the tray at once: it is empty again and a product rides the conveyor.
  CHECK(f.sim.trays()[0].filled == 0);
  CHECK(f.sim.products().size() >= 1);
  const auto it = std::find_if(f.events.begin(), f.events.end(), [](const Sim::Event& e) { return e.kind == Kind::TemplateComplete; });
  REQUIRE(it != f.events.end());
  CHECK(Sim::soundName(*it) == "template_complete");
  CHECK(Sim::soundName({Kind::Powerup}) == "powerup");
}

TEST_CASE("FactorySim: a full tray crossing y = 252 upward is collected and counted after 2000 ms on the output conveyor") {
  Fixture f(config(7, {1, 1}));
  f.begin();
  // Complete tray 0 by hand-placing its slots through real shots is long; fill it through the harness aim, whichever tray
  // the loaded ingredient is needed by, until some tray is full.
  int guard = 0;
  while (f.count(Kind::Snatch) == 0 && guard++ < 400) {
    REQUIRE(f.untilLoaded());
    if (const auto p = f.sim.bestAim()) f.fireAt(static_cast<int>(std::lround(p->x)), static_cast<int>(std::lround(p->y)));
    else f.fireAt(0, 0, 400);  // (a 7-tray wheel: some tray may be in the way, a rare hit is as good)
    if (f.sim.state() != State::Running) break;
  }
  REQUIRE(f.count(Kind::Snatch) >= 1);
  CHECK(f.sim.products().size() >= 1);
  CHECK(f.sim.products().front() >= 300.0);
  CHECK(f.sim.count() == 0);
  const int collectedAt = f.firstAt(Kind::Snatch);
  for (int i = 0; i < 400 && f.count(Kind::TemplateSuccess) == 0; ++i) f.tick();
  const int arrivedAt = f.firstAt(Kind::TemplateSuccess);
  REQUIRE(arrivedAt > 0);
  CHECK(arrivedAt - collectedAt >= 1980);  // 216 px at 0.108 px/ms: 200 ticks, the collecting tick being the first
  CHECK(arrivedAt - collectedAt <= 2010);
  CHECK(f.sim.count() >= 1);
}

TEST_CASE("FactorySim: the eighth miss ends the run aborted, with production_stopped") {
  Fixture f(config(1, {1, 1}, 0.0));
  f.begin();
  for (int i = 0; i < 8; ++i) {
    REQUIRE(f.untilLoaded());
    CHECK(f.sim.state() == State::Running);
    f.fireAt(0, 0, 400);
    CHECK(f.sim.misses() == i + 1);
  }
  f.tick();  // tick step 1.5: misses == 8
  CHECK(f.sim.state() == State::Over);
  CHECK(f.sim.verdict() == "production_stopped");
  CHECK(f.count(Kind::Ended) == 1);
  const auto it = std::find_if(f.events.begin(), f.events.end(), [](const Sim::Event& e) { return e.kind == Kind::Ended; });
  CHECK(it->arg == 1);  // aborted: the music stops too
  CHECK(Sim::soundName(*it) == "factory_shut_down");
  // The failure sounds run 01..08.
  std::vector<int> fails;
  for (const auto& e : f.events)
    if (e.kind == Kind::Failure) fails.push_back(e.arg);
  CHECK(fails == std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8});
}

TEST_CASE("FactorySim: 60 s of play ends at 60010 ms: state 4 (speed >= 5), and with nothing in flight state 5 in the same tick") {
  Fixture f(config(7, {1, 1}));
  f.begin();
  for (int i = 0; i < 7000 && f.sim.state() == State::Running; ++i) f.tick();
  CHECK(f.sim.elapsedMs() == 60010);
  CHECK(f.firstAt(Kind::WarningBell) == 52010);  // 7.99 s left, once
  CHECK(f.count(Kind::WarningBell) == 1);
  CHECK(f.firstAt(Kind::ProductionOver) == 60010);
  CHECK(Sim::soundName({Kind::ProductionOver}) == "mini_game_production_over");
  // Tick step 1.6 sets state 4 and step 6 of the same tick ends it: no shot, no product, no full tray.
  CHECK(f.sim.state() == State::Over);
  CHECK(f.firstAt(Kind::Ended) == 60010);
  CHECK(f.sim.speed() == doctest::Approx(5.0));  // 1.6: speed < 5 becomes 5
  CHECK(f.sim.verdict() == "production_low");
  // 2.9: decay = speed / 300 a tick: the speed reaches 0 after 300 ticks and the wheel stops.
  const double a0 = f.sim.angle();
  f.tick(150);
  CHECK(f.sim.speed() == doctest::Approx(2.5).epsilon(0.02));
  CHECK(f.sim.angle() < a0);
  f.tick(200);
  CHECK(f.sim.speed() == 0.0);
  const double a1 = f.sim.angle();
  f.tick(10);
  CHECK(f.sim.angle() == a1);
}

TEST_CASE("FactorySim: state 4 lasts while a shot is in flight, then the run is over") {
  Fixture f(config(1, {1, 1}, 0.0));
  f.begin();
  while (f.sim.elapsedMs() < 59950) f.tick();
  REQUIRE(f.sim.loaded() >= 0);
  const int ing = f.sim.loaded();
  REQUIRE(f.sim.click(525, 300));  // 13 ticks of flight: 59960 .. 60090
  while (f.sim.elapsedMs() < 60010) f.tick();
  CHECK(f.sim.state() == State::Finishing);
  CHECK(f.sim.speed() >= 5.0 - 1e-9);
  CHECK(f.sim.shots().size() == 1);
  CHECK_FALSE(f.sim.click(525, 300));  // no firing in state 4
  f.tick(6);  // 60070: still in flight
  CHECK(f.sim.state() == State::Finishing);
  f.tick(1);  // 60080: the shot lands in the hit test and step 6 of the same tick ends the run
  CHECK(f.sim.state() == State::Over);
  CHECK(f.sim.trays()[0].filled == 1);
  CHECK(f.sim.trays()[0].slots[static_cast<std::size_t>(ing)] == ing);
  CHECK(f.sim.verdict() == "production_low");
}

TEST_CASE("FactorySim: TogglePause stops everything, saves the state, resumes it, and state 5 ignores it") {
  Fixture f(config(7, {1, 1}));
  f.begin();
  f.tick(100);
  const int elapsed = f.sim.elapsedMs();
  const double angle = f.sim.angle();
  f.events.clear();
  f.sim.togglePause();
  for (const auto& e : f.sim.takeEvents()) f.events.push_back(e);
  CHECK(f.sim.state() == State::Paused);
  CHECK(f.sim.savedState() == State::Running);
  REQUIRE(f.count(Kind::Paused) == 1);
  CHECK(f.events.front().arg == 1);
  f.tick(50);
  CHECK(f.sim.elapsedMs() == elapsed);
  CHECK(f.sim.angle() == angle);
  CHECK_FALSE(f.sim.click(300, 400));  // no firing while paused
  f.sim.togglePause();
  CHECK(f.sim.state() == State::Running);
  CHECK(f.sim.takeEvents().front().arg == 0);
  f.tick();
  CHECK(f.sim.elapsedMs() == elapsed + 10);
  // Pausing from the countdown and from the start dialog (state 0) resumes into the same state.
  Fixture g(config(7, {1, 1}));
  g.sim.togglePause();
  CHECK(g.sim.state() == State::Paused);
  g.sim.togglePause();
  CHECK(g.sim.state() == State::Idle);
  g.sim.start();
  g.tick(5);
  g.sim.togglePause();
  g.sim.togglePause();
  CHECK(g.sim.state() == State::Countdown);
  // State 5 ignores it.
  Fixture h(config(1, {1, 1}, 0.0));
  h.begin();
  for (int i = 0; i < 8; ++i) {
    REQUIRE(h.untilLoaded());
    h.fireAt(0, 0, 400);
  }
  h.tick();
  REQUIRE(h.sim.state() == State::Over);
  h.sim.takeEvents();
  h.sim.togglePause();
  CHECK(h.sim.state() == State::Over);
  CHECK(h.sim.takeEvents().empty());
}

TEST_CASE("FactorySim: the first waste pauses the game once and asks for the character's warning") {
  Sim::Config c = config(1, {1, 1}, 0.0);
  c.firstMiss = true;
  Fixture f(c);
  f.begin();
  REQUIRE(f.untilLoaded());
  REQUIRE(f.sim.click(0, 0));
  f.tick(60);
  CHECK(f.sim.misses() == 1);
  CHECK(f.sim.state() == State::Paused);
  CHECK(f.count(Kind::FirstMiss) == 1);
  CHECK_FALSE(f.sim.firstMissPending());
  f.sim.togglePause();
  REQUIRE(f.untilLoaded());
  f.fireAt(0, 0, 400);
  CHECK(f.sim.misses() == 2);
  CHECK(f.sim.state() == State::Running);  // no second warning
  CHECK(f.count(Kind::FirstMiss) == 1);
}

TEST_CASE("FactorySim: the verdict follows count / floor(171 / total)") {
  // total 4: 42 trays at most; 0.11 -> med at 5 (0.119); 0.19 -> high at 8 (0.1905); awesome from 0.55 -> 24 (0.571).
  CHECK(Sim::verdictKey(0, 4, 350, false) == "production_low");
  CHECK(Sim::verdictKey(4, 4, 350, false) == "production_low");   // 0.095
  CHECK(Sim::verdictKey(5, 4, 350, false) == "production_med");   // 0.119
  CHECK(Sim::verdictKey(7, 4, 350, false) == "production_med");   // 0.167
  CHECK(Sim::verdictKey(8, 4, 350, false) == "production_high");  // 0.190
  CHECK(Sim::verdictKey(23, 4, 350, false) == "production_high"); // 0.548
  CHECK(Sim::verdictKey(24, 4, 350, false) == "production_awesome");
  // total 2: 85 trays at most; awesome from 0.65 -> 56 (0.659), 55 is 0.647.
  CHECK(Sim::verdictKey(55, 2, 350, false) == "production_high");
  CHECK(Sim::verdictKey(56, 2, 350, false) == "production_awesome");
  // total 3 also uses 0.65: 57 trays at most, 37 -> 0.649, 38 -> 0.667.
  CHECK(Sim::verdictKey(37, 3, 350, false) == "production_high");
  CHECK(Sim::verdictKey(38, 3, 350, false) == "production_awesome");
  // total 6: 28 trays; 0.55 -> 16 (0.571), 15 is 0.536.
  CHECK(Sim::verdictKey(15, 6, 350, false) == "production_high");
  CHECK(Sim::verdictKey(16, 6, 350, false) == "production_awesome");
  CHECK(Sim::verdictKey(99, 4, 350, true) == "production_stopped");
}

TEST_CASE("FactorySim: the timer text is m:ss of the whole seconds left of 60") {
  Fixture f(config(7, {1, 1}));
  CHECK(f.sim.timerText() == "1:00");
  f.begin();
  CHECK(f.sim.timerText() == "1:00");  // elapsed 0 after the bell
  f.tick();
  CHECK(f.sim.timerText() == "0:59");  // 59990 ms left, rounded down
  f.tick(99);
  CHECK(f.sim.timerText() == "0:59");  // elapsed 1000: 59 s
  f.tick(1);
  CHECK(f.sim.timerText() == "0:58");
}

TEST_CASE("FactorySim: the harness aim leads the tray and finds nothing when no tray needs the loaded ingredient") {
  Fixture f(config(7, {1, 1}));
  CHECK_FALSE(f.sim.bestAim().has_value());  // not running
  f.begin();
  CHECK_FALSE(f.sim.bestAim().has_value());  // gun empty
  REQUIRE(f.untilLoaded());
  const auto p = f.sim.bestAim();
  REQUIRE(p.has_value());
  f.fireAt(static_cast<int>(std::lround(p->x)), static_cast<int>(std::lround(p->y)));
  CHECK(f.sim.misses() == 0);
  int filled = 0;
  for (const auto& t : f.sim.trays()) filled += t.filled;
  CHECK(filled == 1);
}

TEST_CASE("FactorySim: the ingredient spawns pop the BACK of the queue") {
  Fixture f(config(8, {1, 3}));
  const std::vector<int> before = f.sim.queue();
  f.begin();  // the bell tick spawns the first node
  REQUIRE(f.sim.belt().size() == 1);
  CHECK(f.sim.belt().front().ingredient == before.back());
  CHECK(f.sim.queue() == std::vector<int>(before.begin(), before.end() - 1));
  f.tick(40);  // the second spawn (tick 36 after the bell)
  REQUIRE(f.sim.belt().size() == 2);
  CHECK(f.sim.belt().back().ingredient == before[before.size() - 2]);
}

TEST_CASE("FactorySim: golden: the first queue entries of the 8-tray {1,3} template from CRT seed 1") {
  Fixture f(config(8, {1, 3}));
  const auto& q = f.sim.queue();
  REQUIRE(q.size() == 192);
  const std::vector<int> head(q.begin(), q.begin() + 10);
  CHECK(head == std::vector<int>{1, 1, 1, 0, 0, 0, 0, 1, 1, 1});
}

TEST_CASE("FactorySim: a full tray is collected on the tick its box top goes from >= 252 to < 252, never on a downward crossing") {
  // (One tray cannot show it: a full single tray is the power-up and is collected at once. Seven trays and the harness aim.)
  Fixture f(config(7, {1, 1}));
  f.begin();
  int collections = 0, guard = 0;
  std::vector<int> prevTop(7);
  std::vector<bool> prevFull(7);
  const auto tick = [&] {
    for (int i = 0; i < 7; ++i) {
      prevTop[static_cast<std::size_t>(i)] = f.sim.trays()[static_cast<std::size_t>(i)].top;
      prevFull[static_cast<std::size_t>(i)] = f.sim.trays()[static_cast<std::size_t>(i)].filled == f.sim.total();
    }
    const int snatches = f.count(Kind::Snatch);
    f.tick();
    for (int i = 0; i < 7; ++i) {
      const auto& t = f.sim.trays()[static_cast<std::size_t>(i)];
      const bool crossedUp = prevTop[static_cast<std::size_t>(i)] >= 252 && t.top < 252;
      const bool collected = prevFull[static_cast<std::size_t>(i)] && t.filled == 0;
      // Only a full tray ever leaves full; it leaves exactly when it crosses upward.
      if (prevFull[static_cast<std::size_t>(i)]) CHECK(collected == crossedUp);
      if (collected) ++collections;
    }
    (void)snatches;
    CHECK(f.sim.armPosition() <= 7.0);  // the arm position is clamped to 7 (the exe ends it at f >= 8)
  };
  while (collections < 2 && guard++ < 20000) {
    if (f.sim.loaded() >= 0) {
      if (const auto p = f.sim.bestAim()) {
        if (f.sim.click(static_cast<int>(std::lround(p->x)), static_cast<int>(std::lround(p->y)))) continue;
      }
    }
    tick();
    if (f.sim.state() != State::Running) break;
  }
  CHECK(collections >= 1);
}

TEST_CASE("FactorySim: the countdown's bell tick has dt = 0: the wheel does not turn on it") {
  Fixture f(config(7, {1, 1}));
  f.sim.start();
  while (f.sim.elapsedMs() < 2390 || f.sim.state() != State::Countdown) f.tick();
  const double a = f.sim.angle();
  f.tick();  // elapsed 2400: the bell
  CHECK(f.sim.state() == State::Running);
  CHECK(f.sim.angle() == a);
  f.tick();
  CHECK(f.sim.angle() < a);
}

TEST_CASE("FactorySim: the gun sprite starts at 3 pi / 2 (the exe's constructor), not at 0") {
  Fixture f(config(1, {1, 1}, 1.0));
  CHECK(f.sim.gunAngle() == doctest::Approx(4.71238898038469));
  f.sim.start();
  CHECK(f.sim.gunAngle() == doctest::Approx(4.71238898038469));
}
