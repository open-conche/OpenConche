#include <doctest/doctest.h>

#include <cmath>
#include <string>

#include "ScriptHarness.h"
#include "games/choc3/Particles.h"

using namespace choco;
using game::Firework;
using game::FireworksWindow;
using game::ParticleParams;
using game::ParticleSystem;
using game::SlotSparkle;

namespace {

// A system with every random range collapsed to a constant, so a test can follow one particle exactly.
ParticleParams fixedParams() {
  ParticleParams p;
  p.numParticles = 3;
  p.pPosition = {{0, 0}, {0, 0}};
  p.pScale = {1, 1};
  p.pGravity = {{0, 0}, {100, 100}};
  p.pVelocity = {{10, 10}, {0, 0}};
  p.pSpin = {1, 1};
  p.pSpinSpeed = {2, 2};
  p.scaleShrinkPerSecond = 0.5f;
  p.colorStart = {1, 1, 1, 1};
  p.colorEnd = {1, 1, 1, 0};
  p.lifeMs = 2000;
  return p;
}

}  // namespace

TEST_CASE("particles: the spawn accumulator carries fractions and can overshoot by one") {
  script::Rng rng(1);
  ParticleSystem ps(fixedParams(), rng);
  CHECK(ps.create(0.4f) == 1);  // any positive remainder spawns one and goes negative
  CHECK(ps.accumulator() == doctest::Approx(-0.6f));
  CHECK(ps.create(0.3f) == 0);
  CHECK(ps.accumulator() == doctest::Approx(-0.3f));
  CHECK(ps.create(0.3f) == 0);  // 0.0 is not > 0
  CHECK(ps.create(0.5f) == 1);
  CHECK(ps.accumulator() == doctest::Approx(-0.5f));
  CHECK(ps.active().size() == 2);
}

TEST_CASE("particles: a full pool stops the call, eating one unit, and the rest stays in the accumulator") {
  script::Rng rng(1);
  ParticleSystem ps(fixedParams(), rng);  // pool of 3
  CHECK(ps.create(5) == 3);
  CHECK(ps.active().size() == 3);
  CHECK(ps.accumulator() == doctest::Approx(1.0f));  // 5 - 3 spawned - 1 for the failed attempt
  CHECK(ps.create(0) == 0);                          // the leftover 1.0 > 0 hits the full pool again
  CHECK(ps.accumulator() == doctest::Approx(0.0f));
  CHECK(ps.active().size() == 3);
}

TEST_CASE("particles: fFade interpolates linearly and clamps negative ages") {
  const gfx::ColorF a{1, 1, 1, 0.8f}, b{1, 1, 1, 0};
  CHECK(ParticleSystem::fade(0, a, 2000, b).a == doctest::Approx(0.8f));
  CHECK(ParticleSystem::fade(1000, a, 2000, b).a == doctest::Approx(0.4f));
  CHECK(ParticleSystem::fade(2000, a, 2000, b).a == doctest::Approx(0.0f));
  CHECK(ParticleSystem::fade(-500, a, 2000, b).a == doctest::Approx(0.8f));
  const gfx::ColorF c{0, 0.5f, 1, 1}, d{1, 0.5f, 0, 0};
  const gfx::ColorF m = ParticleSystem::fade(500, c, 1000, d);
  CHECK(m.r == doctest::Approx(0.5f));
  CHECK(m.b == doctest::Approx(0.5f));
}

TEST_CASE("particles: the update order is position, velocity, fade, shrink, spin, age") {
  script::Rng rng(1);
  ParticleSystem ps(fixedParams(), rng);
  REQUIRE(ps.create(1) == 1);
  ps.process(1000);
  const auto& q = ps.active()[0];
  CHECK(q.x == doctest::Approx(10));  // moved by the velocity it spawned with
  CHECK(q.y == doctest::Approx(0));   // not by the gravity of the same tick
  CHECK(q.vy == doctest::Approx(100));
  CHECK(q.color.a == doctest::Approx(1.0f));  // the fade saw age 0
  CHECK(q.scale == doctest::Approx(0.5f));
  CHECK(q.spin == doctest::Approx(3));
  CHECK(q.ageMs == doctest::Approx(1000));
}

TEST_CASE("particles: the second tick moves by last tick's velocity") {
  script::Rng rng(1);
  ParticleSystem ps(fixedParams(), rng);
  ps.create(1);
  ps.process(1000);
  ps.process(500);
  const auto& q = ps.active()[0];
  CHECK(q.y == doctest::Approx(50));  // 100 px/s * 0.5 s
  CHECK(q.color.a == doctest::Approx(0.5f));  // age was 1000 of 2000 when the fade ran
  CHECK(q.vy == doctest::Approx(150));
}

TEST_CASE("particles: a particle expires when its age passes the lifetime, not when it reaches it") {
  script::Rng rng(1);
  ParticleSystem ps(fixedParams(), rng);
  ps.create(1);
  for (int i = 0; i < 50; ++i) ps.process(40);  // age 2000: not yet > 2000
  CHECK(ps.active().size() == 1);
  ps.process(40);
  CHECK(ps.active().empty());
}

TEST_CASE("particles: the shipped parameter sets match the glitz scripts") {
  const ParticleParams f = game::fireworkParams();
  CHECK(f.numParticles == 80);
  CHECK(f.pScale.lo == doctest::Approx(0.2f));
  CHECK(f.pScale.hi == doctest::Approx(0.75f));
  CHECK(f.pGravity.y.lo == 90);
  CHECK(f.pGravity.y.hi == 110);
  CHECK(f.pVelocity.x.lo == -90);
  CHECK(f.pVelocity.y.hi == 100);
  CHECK(f.pSpinSpeed.lo == 60);
  CHECK(f.pSpinSpeed.hi == 80);
  CHECK(f.scaleShrinkPerSecond == doctest::Approx(0.1f));
  CHECK(f.colorStart.a == doctest::Approx(0.8f));
  const ParticleParams s = game::slotPayoutParams();
  CHECK(s.pVelocity.x.lo == -45);
  CHECK(s.pVelocity.y.lo == 100);
  CHECK(s.pGravity.y.lo == 300);
  CHECK(s.pGravity.x.lo == 0);
  CHECK(s.colorStart.a == 1.0f);
  CHECK(s.colorEnd.a == doctest::Approx(0.5f));
  CHECK(s.pScale.lo == 1.0f);
  CHECK(s.scaleShrinkPerSecond == 0);
}

TEST_CASE("fireworks: Chocolatier 2's TPort::TFirework rolls its game's numbers") {
  // docs/re/choc2-port-binary.md 7 (facts of the Mac binary): firework<rand()%6+1>, the burst at x in [100,699],
  // y in [10,99]; the state machine's timing is shared with Chocolatier 3's.
  const game::FireworkStyle c2{6, 100, 600, 10, 90};
  for (unsigned seed = 1; seed <= 40; ++seed) {
    script::Rng rng(seed);
    Firework f(rng, -1, c2);  // the texture rolls from the particleOptions stream, as TPort's ctor does
    REQUIRE(f.textureIndex() >= 0);
    REQUIRE(f.textureIndex() <= 4);
    while (f.state() == Firework::State::Waiting) f.update();
    const int sound = f.update();
    CHECK(sound >= 1);
    CHECK(sound <= 6);
    CHECK(f.x() >= 100);
    CHECK(f.x() <= 699);
    CHECK(f.y() >= 10);
    CHECK(f.y() <= 99);
  }
}

TEST_CASE("fireworks: one firework's state machine timing") {
  for (unsigned seed = 1; seed <= 20; ++seed) {
    script::Rng rng(seed);
    Firework f(rng, 0);
    const int t0 = f.timer();
    REQUIRE(t0 >= 0);
    REQUIRE(t0 < 1000);
    // Waiting: timer <= 40 fires on the next tick, otherwise it counts down by 40.
    int ticks = 0;
    while (f.state() == Firework::State::Waiting) {
      f.update();
      ++ticks;
      REQUIRE(ticks < 100);
    }
    CHECK(ticks == (t0 <= 40 ? 1 : (t0 + 39) / 40));
    CHECK(f.state() == Firework::State::Fire);
    CHECK(f.timer() == 0);
    // The fire tick picks the burst position and the sound, and re-arms the emission.
    const int sound = f.update();
    CHECK(sound >= 1);
    CHECK(sound <= 5);
    CHECK(f.state() == Firework::State::Burst);
    CHECK(f.x() >= 50);
    CHECK(f.x() <= 299);
    CHECK(f.y() >= 100);
    CHECK(f.y() <= 299);
    CHECK(f.sparkleTime() == doctest::Approx(0.1));
    // The burst: 28 simulated ticks of 40 ms, then one tick to go back to waiting.
    int simulated = 0;
    while (f.state() == Firework::State::Burst && f.timer() < Firework::kBurstLimitMs) {
      f.update();
      ++simulated;
      // One CreateParticles(32) per tick while gSparkleTime > 0 (0.1, .06, .02): 32, 64, then 80 of the 96 asked for.
      if (simulated == 1) CHECK(f.particles().active().size() == 32);
      if (simulated == 2) CHECK(f.particles().active().size() == 64);
      if (simulated == 3) {
        CHECK(f.particles().active().size() == 80);
        CHECK(f.sparkleTime() <= 0);
      }
      REQUIRE(simulated < 100);
    }
    CHECK(simulated == 28);
    CHECK(f.state() == Firework::State::Burst);
    CHECK(f.timer() == 1120);
    for (const auto& q : f.particles().active()) CHECK(q.ageMs <= 1120);
    f.update();
    CHECK(f.state() == Firework::State::Waiting);
    CHECK(f.timer() >= 250);
    CHECK(f.timer() <= 749);
  }
}

TEST_CASE("fireworks: a re-burst keeps the old sparks, so fewer free slots") {
  script::Rng rng(3);
  Firework f(rng, 1);
  int bursts = 0;
  for (int i = 0; i < 400 && bursts < 2; ++i) {
    const bool fired = f.update() != 0;
    bursts += fired;
  }
  REQUIRE(bursts == 2);
  // The frozen sparks of the first burst are still in the list when the second begins.
  CHECK(f.particles().active().size() == 80);
}

TEST_CASE("fireworks: the window creates 9 fireworks on StartFireworks and ticks them every 40 ms") {
  test::ScriptEnv env(std::string(CHOCO_TEST_ASSETS) + "/ledger");
  game::registerParticleNatives(env.lua.L(), env.ctx);
  ui::Window& modal = env.wm.pushModal("m");
  modal.setRect({0, 0, 800, 600});
  auto& w = static_cast<FireworksWindow&>(modal.adoptFront(env.wm.createWindow("FireworksWindow")));
  CHECK(w.typeName() == "FireworksWindow");
  CHECK(w.fireworks().empty());
  REQUIRE(env.run("StartFireworks()") == 0);
  REQUIRE(w.fireworks().size() == 9);
  std::array<int, 9> timers{};
  for (int i = 0; i < 9; ++i) timers[static_cast<std::size_t>(i)] = w.fireworks()[static_cast<std::size_t>(i)]->timer();
  env.wm.tickWindowAnimations(40);
  for (int i = 0; i < 9; ++i) {
    const int t0 = timers[static_cast<std::size_t>(i)];
    const Firework& f = *w.fireworks()[static_cast<std::size_t>(i)];
    if (t0 <= 40) CHECK(f.state() == Firework::State::Fire);
    else CHECK(f.timer() == t0 - 40);
  }
  // Starting again only restarts the animation.
  const Firework* first = w.fireworks()[0].get();
  REQUIRE(env.run("StartFireworks()") == 0);
  CHECK(w.fireworks().size() == 9);
  CHECK(w.fireworks()[0].get() == first);
  // StopFireworks ends the ticking.
  REQUIRE(env.run("StopFireworks()") == 0);
  const int before = w.fireworks()[1]->timer();
  const auto stateBefore = w.fireworks()[1]->state();
  env.wm.tickWindowAnimations(5000);
  CHECK(w.fireworks()[1]->timer() == before);
  CHECK(w.fireworks()[1]->state() == stateBefore);
}

TEST_CASE("slot sparkle: gSparkleTime, the spawn gate and the end of the phase") {
  script::Rng rng(1);
  SlotSparkle s(rng);
  s.arm(1);
  CHECK(s.sparkleTime() == doctest::Approx(1.5));
  size_t count = 0;
  int ticks = 0;
  bool done = false;
  while (!done) {
    const bool gated = s.sparkleTime() >= 1;
    const size_t before = s.particles().active().size();
    done = s.tick();
    ++ticks;
    if (!gated) CHECK(s.particles().active().size() == before);  // no spawning below one second left
    count = s.particles().active().size();
    REQUIRE(ticks < 1000);
  }
  CHECK(ticks == 30);
  CHECK(count > 0);
  CHECK(count <= 80);
  s.arm(8);
  CHECK(s.sparkleTime() == doctest::Approx(5.0));
  for (int i = 0; i < 30; ++i) s.tick();
  CHECK(s.particles().active().size() <= 80);
  // Payout 0 spawns nothing: gSparkleTime is 1, which passes the gate once, but the density is 0.
  script::Rng rng2(1);
  SlotSparkle z(rng2);
  z.arm(0);
  CHECK(z.sparkleTime() == doctest::Approx(1.0));
  z.tick();
  CHECK(z.particles().active().empty());
}
