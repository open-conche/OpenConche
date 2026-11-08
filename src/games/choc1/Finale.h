#pragma once

// Chocolatier 1's Finale window (TFinale, exe vtable 004e7f24; the constructor 00411200, Init 00411090, the Draw 004110e0 and
// the 10 ms tick task 004110b0; docs/re/choc1-finale.md): the chocolate rain of the ending screen. The shipped Lua shows it
// through Simulator.AwardMedal(gSim, last) -> ui/reward_finale.lua -> Finale{0,0,800,600}, over the mainmenu Bitmap and under
// the medal, the texts and the close button.
//
// Sixty particles rain item art over the whole 800x600 window: each respawns at the centre (400,250) with a random velocity
// and a picture from the exe's 48-name table. One respawn takes six rand() draws from the shared CRT stream; the tick
// moves by the velocity, adds 0.08 gravity, bounces off the 584 floor at -0.8 and recycles the particle past -64/864. The tick
// runs at the 10 ms repeating task Init registers (FUN_00421980(this, 10, ...), the same task kind the factory's 62 ms one
// uses). Facts and inferences: docs/re/choc1-finale.md sections 2 and 3.

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "games/choc1/Choc1World.h"
#include "gfx/Texture.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::game::choc1 {

// The exe's picture-name table (0x4e7e08..0x4e7ec4): item/<name>, drawn at that order's index per particle.
const char* const kFinaleItemNames[] = {
    "xdark_sq",           "cashew_dark_sq",     "almond_dark_sq",        "hazelnut_milk_sq",
    "cherry_squares",      "cinnamon_squares",   "caramel_squares",       "hazelnut_squares",
    "coffee_squares",      "mint_squares",      "almond_squares",        "cherry_van_squares",
    "cinnamon_van_squares", "milk_van_squares",  "hazelnut_van_squares",  "almond_van_squares",
    "milk_choc_infusion",  "vanilla_almond_infusion", "coconut_infusion", "cherry_infusion",
    "lemon_infusion",      "coffee_infusion",    "cinnamon_infusion",     "raspberry_infusion",
    "mint_infusion",       "maha_cherry_infusion", "quito_coffee_infusion", "colo_raspberry_infusion",
    "colo_coffee_almond_infusion", "maha_mint_raspberry_infusion", "sula_macadamia_infusion", "cacao_blend_infusion",
    "milk_truffles",      "mint_choco_truffles", "dark_truffles",         "almond_choco_truffles",
    "caramel_truffles",   "coconut_lemon_truffles", "coffee_cinnamon_truffles", "hazelnut_milk_truffles",
    "maha_cinnamon_cherry_truffles", "sula_lemon_macadamia_truffles", "colo_coffee_caramel_truffles",
    "trin_almond_vanilla_truffles",
    "blend_mint_coffee_truffles", "blend_raspberry_coconut_truffles", "milk_blend_truffles", "exdark_blend_truffles"};

class FinaleSim {
 public:
  using RandFn = std::function<int()>;  // the shared CRT rand() stream (Choc1World::crtRand)

  static constexpr int kParticleCount = 60;     // 0x3c (the constructor's and the tick's fixed loop count)
  static constexpr int kTextureCount = 48;      // 0x30 (the picture table and the respawn's modulo)
  static constexpr float kSpawnX = 400.0f, kSpawnY = 250.0f;
  static constexpr float kGravity = 0.08f;
  static constexpr float kFloorY = 584.0f;       // the bounce plane, 16 px above a 600-high window's bottom
  static constexpr float kRecycleLeftX = -64.0f, kRecycleRightX = 864.0f;
  static constexpr double kTickMs = 10;          // Init's repeating task delay (FUN_00421980(this, 10, ...))
  static constexpr int kBootRandDraws = kParticleCount * 6;  // every particle respawns once in the constructor

  // The exe's 7-float particle: vx [0], vy [1], x [2], y [3], spin [4], angle [5], the picture index [6].
  struct Particle {
    float vx = 0, vy = 0;
    float x = kSpawnX, y = kSpawnY;
    float spin = 0, angle = 0;
    int texture = 0;
  };

  explicit FinaleSim(RandFn rand) : rand_(std::move(rand)) {}

  // The exe's constructor respawns all 60 particles (00411200); the window does the same at its construction.
  void spawnAll() {
    for (auto& p : particles_) spawn(p);
  }

  // Respawn (00410e90): six rand() draws in this exact order (the shared stream's allocation matters as much as the
  // values): x/y speeds, the vx parity flip, the vy 2-of-3 flip, the spin, the picture index.
  void spawn(Particle& p) const;
  // One 10 ms tick of one particle (00410f90). Returns true when it respawned.
  bool tick(Particle& p) const;
  // One 10 ms tick for all 60 particles (004110b0's loop), the task body.
  int tickAll();

  const std::array<Particle, kParticleCount>& particles() const { return particles_; }
  long long ticks() const { return ticks_; }
  int respawns() const { return respawns_; }

  // The exe's literal respawns, re-read for the tests: the x/y speeds of an integer-drawn r, and the spin (the x87 runs
  // the PI scaling in double precision, then stores the float).
  static float spawnXSpeed(int r) { return static_cast<float>(800 / ((r % 2000 + 1500) / 10)); }
  static float spawnYSpeed(int r) { return static_cast<float>(600 / ((r % 1500 + 1000) / 10)); }
  static float spawnSpin(int r) { return static_cast<float>(r * 3.1415927 / 32767.0 * 0.125); }

 private:
  RandFn rand_;
  std::array<Particle, kParticleCount> particles_{};
  long long ticks_ = 0;
  int respawns_ = 0;
};

class FinaleWindow : public ui::Window {
 public:
  FinaleWindow(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), ctx_(ctx), sim_(randFnOf(*this)) { sim_.spawnAll(); }

  std::string_view typeName() const override { return "Finale"; }
  void init(const ui::Style& style) override;
  void draw(ui::DrawContext& ctx) const override;
  std::string detail() const override;

 private:
  // The shared CRT stream, exactly as the exe's single rand() (seed 1: docs/re/choc1-finale.md 3.1).
  static std::function<int()> randFnOf(FinaleWindow& w) { return [&w] { return w.randNext(); }; }
  int randNext() const {
    if (auto* world = choc1WorldOf(ctx_)) return world->crtRand.next();
    return 1;  // a bare context (no game world): never reached in a real session, kept deterministic
  }
  void catchUp(double nowMs) const;

  script::ScriptContext& ctx_;
  mutable FinaleSim sim_;         // the 10 ms ticks run as a catch-up inside draw()
  std::vector<std::shared_ptr<gfx::Texture>> textures_;  // in kFinaleItemNames order
  mutable bool started_ = false;   // the modal clock read at the first draw, so no time passes before the window shows
  mutable double startMs_ = 0;
};

}  // namespace choco::game::choc1
