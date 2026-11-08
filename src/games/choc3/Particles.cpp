#include "games/choc3/Particles.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "gfx/TextureCache.h"
#include "script/AudioBridge.h"
#include "script/Bind.h"
#include "script/LuaApi.h"
#include "ui/WindowManager.h"

namespace choco::game {

namespace {

constexpr float kPi = 3.1415927f;
FireworksWindow* gActiveFireworks = nullptr;

// _FireworkColors (rgba, alpha 1). The C++ colour is unused: the script's fade overwrites it (docs/re/particles.md 3), so
// the table is only drawn from to keep the original's sequence of random numbers.
constexpr int kFireworkColorCount = 6;
constexpr int kParticleOptionCount = 5;  // rand() % 5 indexes particleOptions

}  // namespace

ParticleParams fireworkParams() {
  ParticleParams p;
  p.numParticles = 80;
  p.pPosition = {{-2, 2}, {-2, 2}};
  p.pScale = {0.2f, 0.75f};
  p.pGravity = {{0, 0}, {90, 110}};
  p.pVelocity = {{-90, 90}, {-50, 100}};
  p.pSpin = {0, 2 * kPi};
  p.pSpinSpeed = {60, 80};
  p.scaleShrinkPerSecond = 0.1f;
  p.colorStart = {1, 1, 1, 0.8f};
  p.colorEnd = {1, 1, 1, 0};
  p.lifeMs = 2000;
  return p;
}

ParticleParams slotPayoutParams() {
  ParticleParams p;
  p.numParticles = 80;
  p.pPosition = {{-5, 5}, {0, 0}};
  p.pScale = {1, 1};  // never initialised by the script: the prototype scale, a full 64x64 image
  p.pGravity = {{0, 0}, {300, 300}};
  p.pVelocity = {{-45, 45}, {100, 100}};
  p.pSpin = {0, 2 * kPi};
  p.pSpinSpeed = {-1, 1};
  p.scaleShrinkPerSecond = 0;
  p.colorStart = {1, 1, 1, 1};
  p.colorEnd = {1, 1, 1, 0.5f};
  p.lifeMs = 2000;
  return p;
}

// ---- ParticleSystem ----

float ParticleSystem::sample(const ParticleParams::Range& r) const {
  if (r.lo == r.hi) return r.lo;  // a constant, not an fRange
  return r.lo + (r.hi - r.lo) * static_cast<float>(rng_->fxUniform());
}

int ParticleSystem::create(float x) {
  acc_ += x;
  int made = 0;
  while (acc_ > 0.0f) {
    acc_ -= 1.0f;
    if (static_cast<int>(active_.size()) >= p_.numParticles) break;  // the pool is exhausted: silent, the rest stays
    Particle q;
    q.x = sample(p_.pPosition.x);
    q.y = sample(p_.pPosition.y);
    q.scale = sample(p_.pScale);
    q.gx = sample(p_.pGravity.x);
    q.gy = sample(p_.pGravity.y);
    q.vx = sample(p_.pVelocity.x);
    q.vy = sample(p_.pVelocity.y);
    q.ageMs = 0;
    q.spin = sample(p_.pSpin);
    q.spinSpeed = sample(p_.pSpinSpeed);
    q.color = p_.colorStart;
    active_.push_back(q);
    ++made;
  }
  return made;
}

gfx::ColorF ParticleSystem::fade(float ageMs, const gfx::ColorF& c0, float durationMs, const gfx::ColorF& c1) {
  const float t = std::max(ageMs, 0.0f) / durationMs;
  return {c0.r + (c1.r - c0.r) * t, c0.g + (c1.g - c0.g) * t, c0.b + (c1.b - c0.b) * t, c0.a + (c1.a - c0.a) * t};
}

void ParticleSystem::process(int ms) {
  const float dt = static_cast<float>(ms) / 1000.0f;  // fTimeScale
  for (Particle& q : active_) {
    q.x += q.vx * dt;  // explicit Euler: the position uses last tick's velocity
    q.y += q.vy * dt;
    q.vx += q.gx * dt;
    q.vy += q.gy * dt;
    q.color = fade(q.ageMs, p_.colorStart, p_.lifeMs, p_.colorEnd);
    q.scale -= p_.scaleShrinkPerSecond * dt;
    q.spin += q.spinSpeed * dt;
    q.ageMs += static_cast<float>(ms);
  }
  std::erase_if(active_, [&](const Particle& q) { return q.ageMs > p_.lifeMs; });  // fExpire(fGreater(pAge, life))
}

void ParticleSystem::draw(gfx::Renderer& r, const gfx::Texture* tex, gfx::PointF origin, float windowAlpha) const {
  if (!tex || active_.empty()) return;
  const float tw = static_cast<float>(tex->width()), th = static_cast<float>(tex->height());
  for (const Particle& q : active_) {
    const float w = tw * q.scale, h = th * q.scale;
    if (w <= 0 || h <= 0) continue;
    gfx::ColorF c = q.color;
    c.a *= windowAlpha;
    // The original rotates the corners by pUp = (cos spin, sin spin) from the identity (0,-1), which is a turn of
    // spin + 90 degrees, clockwise on the y-down screen.
    const float deg = (q.spin + kPi / 2) * (180.0f / kPi);
    r.drawTexture(*tex, {}, {origin.x + q.x - w / 2, origin.y + q.y - h / 2, w, h}, c, gfx::Blend::Normal, deg, {w / 2, h / 2});
  }
}

// ---- Firework ----

const char* Firework::textureName(int index) {
  static const char* const names[] = {"glitz/firework_blue", "glitz/firework_red", "glitz/firework_yellow", "glitz/firework_green"};
  return index >= 0 && index < 4 ? names[index] : nullptr;
}

Firework::Firework(script::Rng& rng, int textureIndex, const FireworkStyle& style)
    : rng_(&rng), style_(style), timer_(rng.fxRange(0, 999)), texture_(textureIndex), ps_(fireworkParams(), rng) {
  // As in the original constructor the timer is rolled first, then the texture (a negative index rolls it here).
  if (texture_ < 0) texture_ = rng.fxRange(0, kParticleOptionCount - 1);
}

int Firework::update() {
  int sound = 0;
  switch (state_) {
    case State::Waiting:
      if (timer_ <= kTickMs) {
        timer_ = 0;
        state_ = State::Fire;
      } else {
        timer_ -= kTickMs;
      }
      break;
    case State::Fire:
      (void)rng_->fxRange(0, kFireworkColorCount - 1);  // the colour index, unused (see above)
      sound = rng_->fxRange(0, style_.soundCount - 1) + 1;  // BSGSoundEvent("firework%d", rand() % soundCount + 1)
      timer_ = 0;
      state_ = State::Burst;
      x_ = rng_->fxRange(0, style_.xSpan - 1) + style_.xMin;
      y_ = rng_->fxRange(0, style_.ySpan - 1) + style_.yMin;
      sparkleTime_ = 0.1;  // re-arms the script's emission
      break;
    case State::Burst:
      if (timer_ < kBurstLimitMs) {
        timer_ += kTickMs;
        // The script's Update(seconds): if gSparkleTime > 0 then gSparkleTime -= s; CreateParticles(s * 800) end. Once per
        // tick, so 0.1 s gives 32, 32 and 16 of the 96 requested over three ticks.
        const double seconds = kTickMs / 1000.0;
        if (sparkleTime_ > 0) {
          sparkleTime_ -= seconds;
          ps_.create(static_cast<float>(seconds * 800));
        }
        ps_.process(kTickMs);
      } else {
        state_ = State::Waiting;
        timer_ = rng_->fxRange(0, 499) + 250;
      }
      break;
  }
  return sound;
}

void Firework::draw(gfx::Renderer& r, const gfx::Texture* tex, gfx::PointF windowOrigin, float alpha) const {
  if (state_ != State::Burst) return;
  ps_.draw(r, tex, {windowOrigin.x + static_cast<float>(x_), windowOrigin.y + static_cast<float>(y_)}, alpha);
}

// ---- FireworksWindow ----

FireworksWindow::FireworksWindow(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), ctx_(ctx) {
  gActiveFireworks = this;
}

FireworksWindow::~FireworksWindow() {
  if (gActiveFireworks == this) gActiveFireworks = nullptr;
}

FireworksWindow* FireworksWindow::active() { return gActiveFireworks; }

void FireworksWindow::start() {
  if (fireworks_.empty()) {
    for (int i = 0; i < kCount; ++i) {
      // The original takes particleOptions[rand() % 5]; entry 4 is a NULL pointer. Documented deviation
      // (docs/re/particles.md 7): a firework that rolls it keeps its state machine, sounds and random numbers but
      // draws nothing, instead of building a texture name from a null pointer.
      fireworks_.push_back(std::make_unique<Firework>(ctx_.rng, -1));
    }
  }
  manager().startWindowAnimation(id(), Firework::kTickMs, true);  // the original's flags are (40, true, true, false)
}

void FireworksWindow::stop() { manager().stopWindowAnimation(id()); }

bool FireworksWindow::onAnimate() {
  for (auto& f : fireworks_) {
    const int sound = f->update();
    if (sound) {
      const std::string name = "firework" + std::to_string(sound);
      script::soundEvent(ctx_, name);
    }
  }
  return true;
}

std::shared_ptr<gfx::Texture> FireworksWindow::texture(int index) const {
  if (index < 0 || index >= 4) return nullptr;
  if (!tried_[static_cast<std::size_t>(index)]) {
    tried_[static_cast<std::size_t>(index)] = true;
    gfx::TextureCache* cache = manager().textures();
    const std::string name = Firework::textureName(index);
    if (cache && !cache->resolve(name).path.empty()) textures_[static_cast<std::size_t>(index)] = cache->load(name);
  }
  return textures_[static_cast<std::size_t>(index)];
}

void FireworksWindow::postDraw(ui::DrawContext& ctx) const {
  for (const auto& f : fireworks_) {
    const auto tex = texture(f->textureIndex());
    f->draw(ctx.renderer, tex.get(), ctx.origin, ctx.alpha);
  }
}

std::string FireworksWindow::detail() const {
  int burst = 0, live = 0;
  for (const auto& f : fireworks_) {
    burst += f->state() == Firework::State::Burst;
    live += static_cast<int>(f->particles().active().size());
  }
  char buf[80];
  std::snprintf(buf, sizeof buf, " fireworks=%d bursting=%d particles=%d", static_cast<int>(fireworks_.size()), burst, live);
  return buf;
}

// ---- SlotSparkle ----

void SlotSparkle::arm(double payout) {
  time_ = payout * 0.5 + 1;
  density_ = payout * 10;
}

bool SlotSparkle::tick() {
  const double seconds = kTickMs / 1000.0;
  // The script's Update(seconds): if gSparkleTime >= 1 then CreateParticles(seconds * density); if gSparkleTime > 0 then
  // gSparkleTime -= seconds.
  if (time_ >= 1) ps_.create(static_cast<float>(seconds * density_));
  if (time_ > 0) time_ -= seconds;
  ps_.process(kTickMs);
  return time_ <= 0;
}

// ---- natives ----

namespace {

int nativeStartFireworks(lua_State* L) {
  script::ScriptContext& ctx = script::contextOf(L);
  if (FireworksWindow* w = FireworksWindow::active()) w->start();
  else if (ctx.stubs.hit("StartFireworks")) ctx.log.write("[unimplemented] StartFireworks (no FireworksWindow is open)");
  return 0;
}

int nativeStopFireworks(lua_State* L) {
  (void)L;
  if (FireworksWindow* w = FireworksWindow::active()) w->stop();
  return 0;
}

}  // namespace

void registerParticleNatives(lua_State* L, script::ScriptContext& ctx) {
  script::ScriptContext* c = &ctx;
  ctx.wm.addWindowType("FireworksWindow", [c](ui::WindowManager& wm) { return std::make_unique<FireworksWindow>(wm, *c); });
  script::bindRaw(L, ctx, "StartFireworks", &nativeStartFireworks);
  script::bindRaw(L, ctx, "StopFireworks", &nativeStopFireworks);
}

}  // namespace choco::game
