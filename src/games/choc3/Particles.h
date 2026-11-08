#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "gfx/Renderer.h"
#include "gfx/Texture.h"
#include "gfx/Types.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

struct lua_State;

namespace choco::game {

// The CPU particle system behind the Awards fireworks and the slot machine's payout sparkle (docs/re/particles.md). It
// replaces TLuaParticleSystem for the two effects the game ships: the Lua particle DSL is not loaded, and each effect
// is a set of constants named after the keys of its glitz script (firework.lua, slotpayout.lua). All values are floats
// as in the original.
struct ParticleParams {
  struct Range {
    float lo = 0, hi = 0;
  };
  struct Range2 {
    Range x, y;
  };
  int numParticles = 80;          // SetNumParticles
  Range2 pPosition;               // fRange per component
  Range pScale = {1, 1};          // the prototype scale is 1.0 when the script never sets it
  Range2 pGravity;
  Range2 pVelocity;
  Range pSpin = {0, 6.2831854f};  // fRange(0, 2*3.1415927)
  Range pSpinSpeed;
  float scaleShrinkPerSecond = 0;  // pScale -= k * dt
  gfx::ColorF colorStart{1, 1, 1, 1}, colorEnd{1, 1, 1, 0.5f};  // the fFade endpoints
  float lifeMs = 2000;             // expire when pAge > lifeMs, which is also the fFade duration
};

// glitz/firework.lua (vanilla and Reforged agree). The fade start is Color(1,1,1,.8), set by the script itself.
ParticleParams fireworkParams();
// glitz/slotpayout.lua, with the default colours (gSparkleColorStart/End are not registered by SlotMachine).
ParticleParams slotPayoutParams();

class ParticleSystem {
 public:
  struct Particle {
    float x = 0, y = 0;
    float vx = 0, vy = 0;
    float gx = 0, gy = 0;
    float scale = 1;
    float spin = 0, spinSpeed = 0;
    float ageMs = 0;
    gfx::ColorF color{1, 1, 1, 1};
  };

  ParticleSystem(ParticleParams params, script::Rng& rng) : p_(std::move(params)), rng_(&rng) {}

  // CreateParticles(x): acc += x, then while acc > 0: acc -= 1 and spawn one, and the whole call stops when the pool is
  // full. The decrement comes first, so a full pool eats one unit of the accumulator and the rest stays. Returns the
  // number spawned.
  int create(float x);
  // ProcessParticles(ms): the animation statements for every active particle in script order (position with last tick's
  // velocity, then velocity, fade, shrink, spin, age), then expiry at age > life. Emission is the caller's job and
  // happens before this, as the original's Update runs the script's Update(seconds) first.
  void process(int ms);
  // Draws every active particle in spawn order at `origin` + its position with normal blending. Nothing when empty or
  // when `tex` is null.
  void draw(gfx::Renderer& r, const gfx::Texture* tex, gfx::PointF origin, float windowAlpha = 1) const;

  const std::vector<Particle>& active() const { return active_; }
  float accumulator() const { return acc_; }
  const ParticleParams& params() const { return p_; }

  // fFade(age, c0, T, c1) with a single segment: c0 + (c1 - c0) * max(age, 0) / T.
  static gfx::ColorF fade(float ageMs, const gfx::ColorF& c0, float durationMs, const gfx::ColorF& c1);

 private:
  float sample(const ParticleParams::Range& r) const;
  ParticleParams p_;
  script::Rng* rng_;
  std::vector<Particle> active_;
  float acc_ = 0;
};

// ---- the firework state machine (TFirework) ----

// TFirework's per-game numbers. Chocolatier 3's are the defaults (docs/re/particles.md: firework<rand()%5+1>, the burst
// at x in [50,299], y in [100,299]). Chocolatier 2's TPort::TFirework differs (docs/re/choc2-port-binary.md 7, facts of
// the Mac binary): firework<rand()%6+1> (the sixth has no shipped file: a silent fail, as in the original), x in
// [100,699], y in [10,99].
struct FireworkStyle {
  int soundCount = 5;                 // firework<1..soundCount>
  int xMin = 50, xSpan = 250;         // x = xMin + rand() % xSpan
  int yMin = 100, ySpan = 200;       // y = yMin + rand() % ySpan
};

class Firework {
 public:
  enum class State { Waiting = 0, Fire = 1, Burst = 2 };
  static constexpr int kTickMs = 40;
  static constexpr int kBurstLimitMs = 1100;  // simulated while timer < this: 28 ticks of 40 ms
  // textureIndex 4 is the NULL entry of particleOptions: nothing is drawn (see Firework::draw). A negative index rolls it
  // after the timer, from the particle RNG stream (Rng::fx*), so effects never shift the game's own rolls.
  // `style` carries the per-game rolls (Chocolatier 3's defaults when not given).
  Firework(script::Rng& rng, int textureIndex, const FireworkStyle& style = FireworkStyle{});

  // One 40 ms task tick. Returns the sound event number (1..5) when the tick fired a burst, else 0.
  int update();
  State state() const { return state_; }
  int timer() const { return timer_; }
  int x() const { return x_; }
  int y() const { return y_; }
  int textureIndex() const { return texture_; }
  const ParticleSystem& particles() const { return ps_; }
  double sparkleTime() const { return sparkleTime_; }
  void draw(gfx::Renderer& r, const gfx::Texture* tex, gfx::PointF windowOrigin, float alpha) const;

  // particleOptions[0..3] as glitz texture names; index 4 is the NULL pointer in the binary.
  static const char* textureName(int index);

 private:
  script::Rng* rng_;
  FireworkStyle style_;
  State state_ = State::Waiting;
  int timer_;
  int x_ = 200, y_ = 100;
  int texture_;
  double sparkleTime_ = 0;  // the script's gSparkleTime
  ParticleSystem ps_;
};

// FireworksWindow (Lua type, no keys): StartFireworks creates 9 fireworks and starts the 40 ms animation, which updates
// them all; they are drawn after the children, so they appear over the dialog.
class FireworksWindow : public ui::Window {
 public:
  static constexpr int kCount = 9;
  FireworksWindow(ui::WindowManager& wm, script::ScriptContext& ctx);
  ~FireworksWindow() override;
  std::string_view typeName() const override { return "FireworksWindow"; }
  void postDraw(ui::DrawContext& ctx) const override;
  bool onAnimate() override;
  std::string detail() const override;

  void start();  // StartFireworks
  void stop();   // StopFireworks
  static FireworksWindow* active();
  const std::vector<std::unique_ptr<Firework>>& fireworks() const { return fireworks_; }

 private:
  std::shared_ptr<gfx::Texture> texture(int index) const;
  script::ScriptContext& ctx_;
  std::vector<std::unique_ptr<Firework>> fireworks_;
  mutable std::array<std::shared_ptr<gfx::Texture>, 4> textures_;
  mutable std::array<bool, 4> tried_{};
};

// The slot machine's payout sparkle: origin (185, 244) in the SlotMachine's coordinates, dollars_big at scale 1.
class SlotSparkle {
 public:
  static constexpr float kOriginX = 185, kOriginY = 244;
  static constexpr int kTickMs = 50;
  explicit SlotSparkle(script::Rng& rng) : ps_(slotPayoutParams(), rng) {}
  // StartSlots' completion: gSparkleTime = payout*0.5 + 1, gSparkleDensity = payout*10.
  void arm(double payout);
  // One 50 ms tick: the script's Update(0.05) then ProcessParticles(50). Returns true when gSparkleTime <= 0 (the phase
  // is over).
  bool tick();
  double sparkleTime() const { return time_; }
  const ParticleSystem& particles() const { return ps_; }
  void draw(gfx::Renderer& r, const gfx::Texture* tex, gfx::PointF windowOrigin, float alpha) const {
    ps_.draw(r, tex, {windowOrigin.x + kOriginX, windowOrigin.y + kOriginY}, alpha);
  }

 private:
  ParticleSystem ps_;
  double time_ = 0, density_ = 0;
};

void registerParticleNatives(lua_State* L, script::ScriptContext& ctx);

}  // namespace choco::game
