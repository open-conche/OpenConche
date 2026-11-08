// Chocolatier 1 (docs/re/choc1-map.md): the MapScreen trip (MapTrip) and the AnimateTravel / PauseTravel /
// ResumeTravel natives.
#include <doctest/doctest.h>

#include <string>

#include "ScriptHarness.h"
#include "games/Registry.h"
#include "games/choc1/Choc1World.h"
#include "games/choc1/MapScreen.h"

using namespace choco;
namespace f1 = choco::game::choc1;
using Cmd = f1::MapTrip::Step::Command;

namespace {

f1::MapTrip::Leg leg(double weeks, const char* vehicle, std::vector<f1::PolylinePath::Point> pts) {
  return {f1::PolylinePath(f1::MapTrip::legDurationMs(weeks), false, std::move(pts)), vehicle};
}

}  // namespace

TEST_CASE("MapTrip: a leg lasts round(weeks * 2000) ms") {
  CHECK(f1::MapTrip::legDurationMs(1) == 2000);
  CHECK(f1::MapTrip::legDurationMs(3) == 6000);
  CHECK(f1::MapTrip::legDurationMs(0.5) == 1000);
  CHECK(f1::MapTrip::legDurationMs(1.0003) == 2001);  // rounded to nearest
}

TEST_CASE("MapTrip: no legs, no trip") {
  f1::MapTrip t;
  t.start("x", {}, 0);
  CHECK_FALSE(t.active());
  CHECK(t.update(5000).command == Cmd::None);
}

TEST_CASE("MapTrip: one tick per crossed week boundary, the last with the sting, arrival one update later") {
  f1::MapTrip t;
  t.start("merida", {leg(2, "boat", {{0, 0}, {400, 0}})}, 1000);
  // First update: the vehicle sound plays, no boundary yet.
  auto s = t.update(1062);
  CHECK(s.command == Cmd::None);
  CHECK(s.sound == "boat");
  CHECK(t.update(1124).sound.empty());  // the same vehicle again: silent
  // Across the first boundary (2000 ms): a tick.
  s = t.update(3000);
  CHECK(s.command == Cmd::Tick);
  CHECK(t.elapsedMs() == 2000);
  // The final boundary is also the end of the leg: the tick carries the sting and the trip stays active.
  s = t.update(5000);
  CHECK(s.command == Cmd::TickSting);
  CHECK(t.active());
  // The next update has no boundary: arrival, once.
  s = t.update(5062);
  CHECK(s.command == Cmd::Arrive);
  CHECK_FALSE(t.active());
  CHECK(t.update(5124).command == Cmd::None);
}

TEST_CASE("MapTrip: one update spanning several weeks queues one tick (a stall loses weeks)") {
  f1::MapTrip t;
  t.start("x", {leg(5, "boat", {{0, 0}, {100, 0}})}, 0);
  t.update(10);
  auto s = t.update(7010);  // crosses 2000, 4000 and 6000
  CHECK(s.command == Cmd::Tick);
  CHECK(t.elapsedMs() == 7010);
}

TEST_CASE("MapTrip: a dot every 200 ms of leg time at the vehicle position; none removed across legs") {
  f1::MapTrip t;
  t.start("x", {leg(1, "boat", {{0, 0}, {200, 0}}), leg(1, "train", {{200, 0}, {200, 100}})}, 0);
  t.update(450);
  REQUIRE(t.dots().size() == 2);  // at 200 and 400 ms
  CHECK(t.dots()[0].x == doctest::Approx(20));  // 200/2000 of 200 px
  CHECK(t.dots()[1].x == doctest::Approx(40));
  t.update(1000);
  CHECK(t.dots().size() == 5);
  // Across the leg boundary at 2000: leg 2 starts with the 200 ms overshoot; its first dot is at 200 of its own time.
  t.update(2200);
  CHECK(t.legIndex() == 1);
  CHECK(t.elapsedMs() == 200);
  CHECK(t.vehicle() == "train");
  REQUIRE(t.dots().size() == 11);  // 10 for leg 1 (200..2000), 1 for leg 2
  CHECK(t.dots()[10].y == doctest::Approx(10));
  CHECK(t.dots()[9].x == doctest::Approx(200));
}

TEST_CASE("MapTrip: leg advance keeps the residual; the sound changes with the vehicle; the week ticks line up across legs") {
  f1::MapTrip t;
  t.start("x", {leg(1, "boat", {{0, 0}, {10, 0}}), leg(2, "train", {{10, 0}, {20, 0}})}, 0);
  CHECK(t.update(100).sound == "boat");
  auto s = t.update(2500);  // leg 1 done at 2000 -> a tick; leg 2 at 500
  CHECK(s.command == Cmd::Tick);
  CHECK(s.sound == "train");
  CHECK(t.legIndex() == 1);
  CHECK(t.elapsedMs() == 500);
  s = t.update(4000);  // 2000 + 2000 total: the boundary at the 4000 ms of trip time
  CHECK(s.command == Cmd::Tick);
  s = t.update(4600);  // the end: 6000 total, leg 2 at 2000 ms of 4000... (4600 - 2000 = 2600 < 4000)
  CHECK(s.command == Cmd::None);
  s = t.update(6000);
  CHECK(s.command == Cmd::TickSting);
  CHECK(t.update(6062).command == Cmd::Arrive);
}

TEST_CASE("MapTrip: the music fade starts in the last second of the last leg only") {
  f1::MapTrip t;
  t.start("x", {leg(1, "boat", {{0, 0}, {10, 0}}), leg(1, "boat", {{10, 0}, {20, 0}})}, 0);
  CHECK_FALSE(t.update(1500).fade);   // the first leg's last second does not count
  CHECK_FALSE(t.update(2500).fade);   // leg 2 at 500 ms: 1500 left
  CHECK(t.update(3100).fade);         // leg 2 at 1100: 900 left
}

TEST_CASE("MapTrip: mirror follows the heading (moving right mirrors)") {
  f1::MapTrip t;
  t.start("x", {leg(1, "boat", {{0, 0}, {100, 0}})}, 0);
  t.update(100);
  CHECK(f1::SimpleAnimation::mirrored(true, true, t.heading()));
  f1::MapTrip u;
  u.start("x", {leg(1, "boat", {{100, 0}, {0, 0}})}, 0);
  u.update(100);
  CHECK_FALSE(f1::SimpleAnimation::mirrored(true, true, u.heading()));
}

TEST_CASE("MapScreen frames: (elapsed / 100) % frame count") {
  CHECK(f1::MapScreen::frameAt(0, 7) == 0);
  CHECK(f1::MapScreen::frameAt(250, 7) == 2);
  CHECK(f1::MapScreen::frameAt(700, 7) == 0);
  CHECK(f1::MapScreen::frameAt(699, 7) == 6);
}

namespace {

struct MapEnv : test::ScriptEnv {
  MapEnv() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/choc1_story"), host(ctx) {
    ctx.module = games::findModule("choc1");
    ctx.module->attach(ctx);
    booted = host.boot();
    wm.pushModal("map");
    wm.tickWindowAnimations(1000);
    REQUIRE(run("gCommandQueue = {} ; gLog = {} ; gSim = { TravelTick = function(self, s) table.insert(gLog, 'tick:' .. tostring(s)) end, "
                "ArriveAtPort = function(self, p) table.insert(gLog, 'arrive:' .. p) end }") == 0);
    REQUIRE(run("do local t = { typename = 'MapScreen', x = 0, y = 0, w = 800, h = 600 }; GenericCreate(t); PostCreate(t, 'MapScreen'); "
                "PopWindow() end") == 0);
  }
  f1::Choc1World& world() { return *f1::choc1WorldOf(ctx); }
  f1::MapScreen* screen() { return f1::MapScreen::inChain(wm); }
  // Advances the animation clock in 62 ms steps to `ms` after the trip's start (1000), then runs what the queue holds.
  void advance(double ms) {
    const double to = 1000 + ms;
    for (; now + 62 <= to; now += 62) wm.tickWindowAnimations(now + 62);
    drain();
  }
  void drain() {
    REQUIRE(run("local q = gCommandQueue; gCommandQueue = {}; for i = 1, table.getn(q) do q[i]() end") == 0);
  }
  std::string trace() {
    REQUIRE(run("gLogText = table.concat(gLog, ',')") == 0);
    return getString("gLogText");
  }
  script::ScriptHost host;
  bool booted = false;
  double now = 1000;
};

const char* kRoute =
    "route = { endName = 'merida', legs = { { time = 1, vehicle = 'train', path = { skip = true, {0,0}, {100,0} } },"
    " { time = 1, path = { {100,0}, {100,50} } } } }";

}  // namespace

TEST_CASE("AnimateTravel: runs a whole trip through the command queue and raises the Rollover gate") {
  MapEnv e;
  REQUIRE(e.booted);
  f1::MapScreen* m = e.screen();
  REQUIRE(m);
  REQUIRE(e.run(kRoute) == 0);
  REQUIRE(e.run("AnimateTravel(route)") == 0);
  CHECK(e.world().mapTrips == 1);
  CHECK(m->trip().active());
  CHECK(m->trip().legCount() == 2);
  CHECK(m->trip().legs()[0].vehicle == "train");
  CHECK(m->trip().legs()[1].vehicle == "boat");  // the default
  CHECK(m->trip().legs()[0].path.timeMs() == 2000);

  e.advance(2100);  // past the first week boundary
  CHECK(e.trace() == "tick:nil");
  CHECK(e.world().mapTrips == 1);
  e.advance(4040);  // the second boundary is also the end: the sting
  CHECK(e.trace() == "tick:nil,tick:merida");
  e.advance(4200);
  CHECK(e.trace() == "tick:nil,tick:merida,arrive:merida");
  CHECK_FALSE(m->trip().active());
  CHECK(e.world().mapTrips == 0);
}

TEST_CASE("AnimateTravel: a route without legs does nothing; the natives without a window log once") {
  MapEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("AnimateTravel({ endName = 'x' })") == 0);
  CHECK(e.world().mapTrips == 0);
  CHECK_FALSE(e.screen()->trip().active());
  REQUIRE(e.run("AnimateTravel({ endName = 'x', legs = {} })") == 0);
  CHECK(e.world().mapTrips == 0);
  e.wm.destroyWindow(*e.screen());
  e.wm.clearDestroyed();
  REQUIRE(e.run("AnimateTravel({}) PauseTravel() ResumeTravel()") == 0);
  CHECK(e.log.contains("[choc1] AnimateTravel: no MapScreen window"));
}

TEST_CASE("PauseTravel stops the trip clock, ResumeTravel does not count the pause") {
  MapEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run(kRoute) == 0);
  REQUIRE(e.run("AnimateTravel(route)") == 0);
  e.advance(1000);
  const long long before = e.screen()->trip().elapsedMs();
  CHECK(before >= 900);
  REQUIRE(e.run("PauseTravel()") == 0);
  for (double t = e.now; t <= e.now + 4000; t += 62) e.wm.tickWindowAnimations(t);  // the platform clock runs on
  CHECK(e.screen()->trip().elapsedMs() == before);
  e.now += 4000;
  e.wm.tickWindowAnimations(e.now);
  REQUIRE(e.run("ResumeTravel()") == 0);
  e.advance(e.now - 1000 + 124);
  const long long after = e.screen()->trip().elapsedMs();
  CHECK(after - before <= 124);
  CHECK(after > before);
}

TEST_CASE("MapScreen: a trip torn down mid-way lowers the Rollover gate") {
  MapEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run(kRoute) == 0);
  REQUIRE(e.run("AnimateTravel(route)") == 0);
  CHECK(e.world().mapTrips == 1);
  e.wm.destroyWindow(*e.screen());
  e.wm.clearDestroyed();
  CHECK(e.world().mapTrips == 0);
}
