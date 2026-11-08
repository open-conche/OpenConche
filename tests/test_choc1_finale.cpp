// Chocolatier 1's Finale (docs/re/choc1-finale.md): FinaleSim's respawn (the six rand() draws, their exact order and the
// integer speed maths), the 10 ms tick (position, gravity, the 584 floor bounce, the -64/864 recycle), the boot's rand
// draw count, and the 48-name picture table it indexes.
#include <doctest/doctest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "games/choc1/Finale.h"

using namespace choco;
namespace f1 = choco::game::choc1;
using f1::FinaleSim;
using Particle = FinaleSim::Particle;

namespace {

// kFinaleItemNames holds the exe's 48 picture names (the table at 0x4e7e08).
constexpr int kItemCount = static_cast<int>(sizeof(f1::kFinaleItemNames) / sizeof(f1::kFinaleItemNames[0]));

// A sim over a scripted sequence (empty = every draw returns 1).
struct Fixture {
  explicit Fixture(std::vector<int> seq = {})
      : seq_(std::move(seq)),
        sim([this] {
          ++calls;
          return index < static_cast<int>(seq_.size()) ? seq_[static_cast<std::size_t>(index++)]
                                                        : (seq_.empty() ? 1 : seq_.back());
        }) {}
  int index = 0;  // how far into the scripted sequence the draws are
  int calls = 0;   // total draws (the empty-sequence stream still draws)
  const std::vector<int> seq_;
  FinaleSim sim;
};

}  // namespace

TEST_CASE("finale: the exe's constants and the spawn integer maths") {
  CHECK(FinaleSim::kParticleCount == 60);  // 0x3c, the constructor's and the tick's loop count
  CHECK(kItemCount == 48);                  // 0x30, the respawn's modulo and the picture table
  CHECK(FinaleSim::kTextureCount == 48);
  CHECK(FinaleSim::kTickMs == 10);  // Init's repeating task delay (FUN_00421980(this, 10, ...))
  CHECK(FinaleSim::kBootRandDraws == 360);
  CHECK(FinaleSim::kSpawnX == 400.0f);
  CHECK(FinaleSim::kSpawnY == 250.0f);
  CHECK(FinaleSim::kFloorY == 584.0f);
  CHECK(FinaleSim::kRecycleLeftX == -64.0f);
  CHECK(FinaleSim::kRecycleRightX == 864.0f);

  std::string names;
  for (int i = 0; i < kItemCount; ++i) names += std::string(" ") + f1::kFinaleItemNames[i];
  CHECK(names.find(" xdark_sq cashew_dark_sq almond_dark_sq hazelnut_milk_sq") == 0);  // the four singles
  CHECK(names.find(" exdark_blend_truffles") != std::string::npos);                    // the table's last row
  CHECK(names.find("maha_mint_raspberry_infusion") != std::string::npos);

  // The speeds are truncated integers of the exe's divisions (00410e90 steps 2-3).
  CHECK(FinaleSim::spawnXSpeed(0) == 5.0f);     // 800/((0+1500)/10) = 800/150
  CHECK(FinaleSim::spawnXSpeed(1999) == 2.0f);  // 800/349, the range floor
  CHECK(FinaleSim::spawnYSpeed(0) == 6.0f);     // 600/((0+1000)/10) = 600/100
  CHECK(FinaleSim::spawnYSpeed(1499) == 2.0f);  // (1499+1000)/10 = 249, 600/249 = 2
}

TEST_CASE("finale: a respawn takes six rand values in the exe's order") {
  // The seed-1 stream's first six values, drawn by 00410e90 in this exact order.
  Fixture f({41, 18467, 6334, 26500, 19169, 15724});
  Particle p{};
  f.sim.spawn(p);
  CHECK(f.index == 6);       // six draws per respawn, on the shared stream
  CHECK(p.x == 400.0f);      // no draw: the centre
  CHECK(p.y == 250.0f);
  CHECK(p.vx == 5.0f);   // 41: 800/((41+1500)/10) = 800/154
  CHECK(p.vy == -4.0f);  // 18467: 600/146 = 4, negated because 26500 % 3 == 1 (2 of 3 rise)
  CHECK(p.spin == 0.22973262f);  // 19169 * PI / 32767 * 0.125
  CHECK(p.angle == 0.0f);
  CHECK(p.texture == 28);        // 15724 % 48
}

TEST_CASE("finale: the parity draw can negate vx, the % 3 draw vy (both consume one rand)") {
  {
    Fixture f({100, 100, 1, 3, 0, 0});  // parity odd, % 3 == 0
    Particle p{};
    f.sim.spawn(p);
    CHECK(p.vx == -5.0f);  // parity 1 negates
    CHECK(p.vy == 5.0f);   // 100: 600/((100+1000)/10) = 600/110 = 5, kept positive
  }
  {
    Fixture f({100, 100, 2, 2, 0, 0});  // parity even, % 3 == 2
    Particle p{};
    f.sim.spawn(p);
    CHECK(p.vx == 5.0f);
    CHECK(p.vy == -5.0f);
  }
}

TEST_CASE("finale: the tick moves by the velocity, adds 0.08, and mirrors the 584 bounce") {
  Fixture f;
  Particle p{};
  p.vx = 2;
  p.vy = 1;
  p.x = 700;
  p.y = 583.5f;
  p.spin = 0.1f;
  p.angle = 0.5f;
  CHECK_FALSE(f.sim.tick(p));
  CHECK(p.angle == 0.6f);         // angle += spin (a field no draw reads: recorded, never shown)
  CHECK(p.x == 702.0f);
  CHECK(p.y == 583.5f);            // moved to 584.5, then mirrored: 584 - (584.5 - 584)
  CHECK(p.vy == -0.864f);          // (1 + 0.08) * -0.8, the x87 double add then the store
  CHECK(p.spin == -0.1f);          // the bounce flips the spin
}

TEST_CASE("finale: a rising particle tests no ceiling; gravity alone turns it") {
  Fixture f;
  Particle p{};
  p.vx = 0;
  p.vy = -3;
  p.x = 400;
  p.y = 100;
  CHECK_FALSE(f.sim.tick(p));
  CHECK(p.y == 97.0f);
  CHECK(p.vy == -2.92f);
}

TEST_CASE("finale: the recycle bounds are one-way (left-moving below -64, right-moving above 864)") {
  {
    Fixture f({41, 18467, 6334, 26500, 19169, 15724});
    Particle p{};
    p.vx = -2;
    p.vy = 1;
    p.x = -62.0f;  // after the move x = -64: the exe compares strictly, so this one survives
    p.y = 100;
    CHECK_FALSE(f.sim.tick(p));
    CHECK(p.x == -64.0f);
    CHECK(f.index == 0);
    p.x = -65.0f;
    p.vx = -2;
    CHECK(f.sim.tick(p));  // moved to -67, past the bound while moving left
    CHECK(f.index == 6);    // the respawn consumed six draws
    CHECK(p.x == 400.0f);
    CHECK(p.texture == 28);
  }
  {
    Fixture f({0, 0, 0, 0, 0, 0});  // the scripted sub-case never reaches the spawn divisions that 1s would
    Particle p{};
    p.vx = 3;  // right-moving: the left bound never takes it
    p.vy = 1;
    p.x = -65.0f;
    p.y = 100;
    CHECK_FALSE(f.sim.tick(p));
    p.x = 861.0f;
    CHECK_FALSE(f.sim.tick(p));  // after the move x = 864: not past yet
    CHECK(p.x == 864.0f);
    p.x = 862.0f;
    p.vx = 3;
    CHECK(f.sim.tick(p));  // moved to 865, past the bound while moving right
    CHECK(p.x == 400.0f);
  }
}

TEST_CASE("finale: the boot respawns all 60 particles once; tickAll counts ticks and the mass recycle") {
  Fixture f;  // every draw returns 1
  CHECK(f.calls == 0);    // the sim starts empty; the WINDOW's construction respawns everything (FinaleWindow)
  f.sim.spawnAll();
  CHECK(f.calls == 360);  // one respawn per particle, six draws each, all 60
  CHECK(f.sim.particles().size() == 60);
  CHECK(f.sim.ticks() == 0);
  CHECK(f.sim.respawns() == 0);
  const Particle& first = f.sim.particles().front();
  CHECK(first.x == 400.0f);
  CHECK(first.vx == -5.0f);  // spawnXSpeed(1) = 800/150 = 5, negated by the odd parity draw
  CHECK(first.vy == -6.0f);  // spawnYSpeed(1) = 6, negated by the 1 % 3 == 1 draw
  CHECK(first.spin == FinaleSim::spawnSpin(1));
  CHECK(first.texture == 1);  // 1 % 48

  // The all-identical particles fly left (vx = -5): x = 400 - 5*n leaves -64 at n = 93, together. The rising vy changes
  // nothing on the x axis (there is no ceiling test).
  for (int n = 0; n < 92; ++n) {
    f.sim.tickAll();
    CHECK(f.sim.respawns() == 0);
  }
  f.sim.tickAll();
  CHECK(f.sim.ticks() == 93);
  CHECK(f.sim.respawns() == 60);
}
