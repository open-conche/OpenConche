#include "games/choc1/Finale.h"

#include <cmath>
#include <cstdio>

#include "games/choc1/Choc1World.h"
#include "ui/DrawContext.h"
#include "ui/Style.h"
#include "ui/WindowManager.h"

namespace choco::game::choc1 {
namespace {

constexpr int kItemCount = static_cast<int>(sizeof(kFinaleItemNames) / sizeof(kFinaleItemNames[0]));

}  // namespace

void FinaleSim::spawn(Particle& p) const {
  // 00410e90, six rand() draws in this order (docs/re/choc1-finale.md 3.1).
  p.x = kSpawnX;
  p.y = kSpawnY;
  p.vx = spawnXSpeed(rand_());
  p.vy = spawnYSpeed(rand_());
  if (rand_() & 1) p.vx = -p.vx;  // the MSVC % 2 idiom; the [4] flip beside it is a no-op on the zeroed spin
  if (rand_() % 3 > 0) p.vy = -p.vy;
  p.spin = spawnSpin(rand_());
  p.angle = 0;
  p.texture = rand_() % kTextureCount;
}

bool FinaleSim::tick(Particle& p) const {
  // 00410f90 (docs/re/choc1-finale.md 3.2). The x87 adds run in double precision and store back to the float fields.
  p.angle += p.spin;
  p.x += p.vx;
  p.y += p.vy;
  p.vy = static_cast<float>(static_cast<double>(p.vy) + 0.08);
  if (p.y > kFloorY) {
    p.vy = static_cast<float>(static_cast<double>(p.vy) * -0.8);
    p.spin = -p.spin;
    p.y = static_cast<float>(kFloorY - (static_cast<double>(p.y) - kFloorY));
  }
  if ((p.vx < 0 && p.x < kRecycleLeftX) || (p.vx > 0 && p.x > kRecycleRightX)) {
    spawn(p);
    return true;
  }
  return false;
}

int FinaleSim::tickAll() {
  int respawned = 0;
  for (auto& p : particles_)
    if (tick(p)) ++respawned;
  respawns_ += respawned;
  ++ticks_;
  return respawned;
}

void FinaleWindow::init(const ui::Style& style) {
  Window::init(style);
  textures_.clear();
  if (manager().textures()) {
    char path[64];
    for (int i = 0; i < kItemCount; ++i) {
      std::snprintf(path, sizeof path, "item/%s", kFinaleItemNames[i]);
      if (auto t = manager().textures()->load(path)) textures_.push_back(std::move(t));
    }
  }
}

void FinaleWindow::catchUp(double nowMs) const {
  if (!started_) {
    started_ = true;
    startMs_ = nowMs;
  }
  const long long target = static_cast<long long>((nowMs - startMs_) / FinaleSim::kTickMs);
  while (sim_.ticks() < target) sim_.tickAll();  // one whole 10 ms per task tick; a stopped modal clock freezes the rain
}

void FinaleWindow::draw(ui::DrawContext& ctx) const {
  if (ctx.alpha <= 0) return;
  catchUp(manager().modalNow(*this));
  for (const FinaleSim::Particle& p : sim_.particles()) {
    if (p.texture < 0 || p.texture >= static_cast<int>(textures_.size())) continue;
    const gfx::Texture& t = *textures_[static_cast<std::size_t>(p.texture)];
    const float w = static_cast<float>(t.width()), h = static_cast<float>(t.height());
    // The picture draw of 004110e0, at the particle's position and natural size (slot-0x14, inference: CENTRED, as the
    // factory's falling product pictures use the same call around a float position).
    const gfx::RectF dst{ctx.origin.x + p.x - w / 2, ctx.origin.y + p.y - h / 2, w, h};
    ctx.renderer.drawTexture(t, {}, dst, {1, 1, 1, ctx.alpha}, gfx::Blend::Normal, 0, {}, false);
  }
}

std::string FinaleWindow::detail() const {
  char buf[96];
  const auto& p = sim_.particles().front();
  std::snprintf(buf, sizeof buf, " tick=%lld respawns=%d pos0=%.0f,%.0f", sim_.ticks(), sim_.respawns(), p.x, p.y);
  return buf;
}

}  // namespace choco::game::choc1
