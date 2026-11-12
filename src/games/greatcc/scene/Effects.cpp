#include "games/greatcc/scene/Effects.h"

#include <algorithm>
#include <cmath>

namespace choco::games::greatcc {

namespace {
int gNextEffectId = 1;
}

SpriteEffect::SpriteEffect() : id_(gNextEffectId++) {}

bool SpriteEffect::onUpdate(double now, bool) {
  stamped_ = true;
  last_ = now;
  return true;
}

// ---- alpha ----

AlphaEffect::AlphaEffect(bool fadeIn, float max, double durationMs, float startAlpha, double delayMs)
    : fadeIn_(fadeIn),
      max_(max),
      alpha_(startAlpha < 0 ? (fadeIn ? 0.0f : 1.0f) : startAlpha),
      duration_(durationMs),
      delay_(delayMs) {}

void AlphaEffect::addedToSprite(VSprite& s) {
  SpriteEffect::addedToSprite(s);
  s.setAlpha(alpha_);
}

bool AlphaEffect::onUpdate(double now, bool softPaused) {
  double dt = elapsed(now);
  if (delay_ > 0) {
    delay_ -= dt;
    dt = 0;
  } else if (inProgress_) {
    const float step = duration_ > 0 ? static_cast<float>(max_ * dt / duration_) : max_;
    if (fadeIn_) {
      alpha_ = std::min(max_, alpha_ + step);
      inProgress_ = alpha_ < max_;
    } else {
      alpha_ = std::max(0.0f, alpha_ - step);
      inProgress_ = alpha_ > 0;
    }
  }
  return SpriteEffect::onUpdate(now, softPaused);
}

// ---- tint ----

bool TintEffect::onUpdate(double now, bool softPaused) {
  const double dt = elapsed(now);
  const float step = duration_ > 0 ? static_cast<float>(std::fabs(target_) * dt / duration_) : std::fabs(target_);
  if (!inProgress_) {
    intensity_ -= step;
    if (intensity_ <= 0) {
      intensity_ = 0;
      destroyed_ = true;
    }
  } else if (intensity_ < target_) {
    intensity_ = std::min(target_, intensity_ + step);
  } else if (intensity_ > target_) {
    intensity_ = std::max(target_, intensity_ - step);
  }
  return SpriteEffect::onUpdate(now, softPaused);
}

void TintEffect::onPreRender(VSprite& s, DrawEnv&) {
  // The first of nested tints saves the sprite's tint; the nested ones blend on top of the current one.
  if (nest_++ == 0) saved_ = s.tint();
  const gfx::ColorF cur = s.tint();
  const float t = intensity_;
  s.setTint({cur.r + (color_.r - cur.r) * t, cur.g + (color_.g - cur.g) * t, cur.b + (color_.b - cur.b) * t, 1});
}

void TintEffect::onPostRender(VSprite& s, DrawEnv&) {
  if (nest_ > 0 && --nest_ == 0) s.setTint(saved_);
}

// ---- glow ----

GlowEffect::GlowEffect(float peak, double periodMs, float cycles, float accel)
    : peak_(peak), period_(periodMs), cycles_(cycles), accel_(accel) {
  setPropagate(true);
}

bool GlowEffect::onUpdate(double now, bool softPaused) {
  const double dt = elapsed(now);
  if (inProgress_) {
    time_ += dt + 0.5 * accel_ * dt * dt;
    if (period_ > 0) {
      const double half = time_ / period_;  // each period is one half wave: it rises or falls
      if (cycles_ > 0 && half > cycles_) {
        inProgress_ = false;  // done: from here it falls
      } else {
        const double whole = std::floor(half);
        const double frac = half - whole;
        const bool rising = std::fmod(whole, 2.0) == 0.0;
        intensity_ = static_cast<float>(peak_ * (rising ? frac : 1 - frac));
      }
    }
  } else {
    intensity_ -= period_ > 0 ? static_cast<float>(peak_ * dt / period_) : peak_;
    if (intensity_ <= 0) {
      intensity_ = 0;
      destroyed_ = true;
    }
  }
  return SpriteEffect::onUpdate(now, softPaused);
}

void GlowEffect::onPostRender(VSprite& s, DrawEnv& env) {
  if (intensity_ <= 0) return;
  DrawSpec spec = env.abs;  // a fresh spec with alpha = intensity, composed under the sprite's own: the alphas multiply
  spec.alpha *= intensity_;
  spec.blend = 2;
  s.drawImage(env.r, spec);
}

// ---- hover glow ----

struct HoverGlow::Hover : SpriteListener {
  explicit Hover(HoverGlow& g) : glow(&g) {}
  void onMouseEnter(VSprite&) override {
    if (glow) glow->setHovered(true);
  }
  void onMouseLeave(VSprite&) override {
    if (glow) glow->setHovered(false);
  }
  HoverGlow* glow;
};

HoverGlow::HoverGlow(float max, bool propagate, bool offsetX, bool offsetY)
    : max_(max), offsetX_(offsetX), offsetY_(offsetY) {
  setPropagate(propagate);
}

void HoverGlow::addedToSprite(VSprite& s) {
  SpriteEffect::addedToSprite(s);
  if (!listener_) listener_ = std::make_shared<Hover>(*this);
  s.addListener(listener_);
}

void HoverGlow::removedFromSprite(VSprite& s) {
  SpriteEffect::removedFromSprite(s);
  if (listener_) s.removeListener(listener_);
  if (refs() <= 0 && listener_) {
    static_cast<Hover&>(*listener_).glow = nullptr;
    listener_->dead = true;
  }
}

bool HoverGlow::onUpdate(double now, bool softPaused) {
  const double dt = elapsed(now);
  const float step = static_cast<float>(dt * max_ / 100.0);
  if (hovered_) intensity_ = std::min(max_, intensity_ + step);
  else intensity_ = std::max(0.0f, intensity_ - step);
  if (!inProgress_ && intensity_ <= 0) destroyed_ = true;
  return SpriteEffect::onUpdate(now, softPaused);
}

void HoverGlow::onPostRender(VSprite& s, DrawEnv& env) {
  if (intensity_ <= 0) return;
  // Always four additive passes; the offset flags only decide whether each is shifted by half a pixel (in the sprite's own
  // space, before its matrix).
  for (float ix : {-1.0f, 1.0f})
    for (float iy : {-1.0f, 1.0f}) {
      DrawSpec spec = env.abs;
      spec.alpha *= intensity_;
      spec.blend = 2;
      spec.m = Mat3::translation(offsetX_ ? ix * 0.5f : 0.0f, offsetY_ ? iy * 0.5f : 0.0f) * env.abs.m;
      s.drawImage(env.r, spec);
    }
}

// ---- wipe ----

bool WipeEffect::onUpdate(double now, bool softPaused) {
  const double dt = elapsed(now);
  progress_ = std::min(1.0f, progress_ + (duration_ > 0 ? static_cast<float>(dt / duration_) : 1.0f));
  return SpriteEffect::onUpdate(now, softPaused);
}

gfx::RectF WipeEffect::clipOf(const gfx::RectF& r) const {
  const float p = progress_;
  if (!reveal_) {
    switch (dir_) {
      case 0: return {r.x, r.y + p * r.h, r.w, r.h * (1 - p)};
      case 1: return {r.x, r.y, r.w, r.h * (1 - p)};
      case 2: return {r.x + p * r.w, r.y, r.w * (1 - p), r.h};
      default: return {r.x, r.y, r.w * (1 - p), r.h};
    }
  }
  switch (dir_) {
    case 0: return {r.x, r.y, r.w, r.h * p};
    case 1: return {r.x, r.y + r.h * (1 - p), r.w, r.h * p};
    case 2: return {r.x, r.y, r.w * p, r.h};
    default: return {r.x + r.w * (1 - p), r.y, r.w * p, r.h};
  }
}

void WipeEffect::onPreDraw(VSprite& s, DrawEnv& env) {
  pushed_ = false;
  const gfx::RectF r = s.getRect(env.parent, -1);
  if (r.w <= 0 || r.h <= 0) return;
  const gfx::RectF c = clipOf(r);
  // pushClip intersects with the clip in force: the previous one is what popClip restores.
  env.r.pushClip({static_cast<int>(std::floor(c.x)), static_cast<int>(std::floor(c.y)), static_cast<int>(std::ceil(c.w)),
                  static_cast<int>(std::ceil(c.h))});
  pushed_ = true;
}

void WipeEffect::onPostDraw(VSprite& s, DrawEnv& env) {
  if (pushed_) env.r.popClip();
  pushed_ = false;
  if (progress_ >= 1) {
    inProgress_ = false;
    destroyed_ = true;
    if (!reveal_) s.setVisible(false);
  }
}

// ---- move ----

void SpriteMove::addedToSprite(VSprite& s) {
  SpriteEffect::addedToSprite(s);
  start_ = s.position();
}

bool SpriteMove::onUpdate(double now, bool softPaused) {
  t_ += elapsed(now);
  float u = duration_ > 0 ? static_cast<float>((t_ - delay_) / duration_) : 1.0f;
  u = std::clamp(u, 0.0f, 1.0f);
  eased_ = std::min(1.0f, u * (1 - accel_ / 2) + accel_ * u * u / 2);
  if (eased_ >= 1) inProgress_ = false;
  return SpriteEffect::onUpdate(now, softPaused);
}

void SpriteMove::onPreDraw(VSprite& s, DrawEnv&) {
  s.setPosition({start_.x + (target_.x - start_.x) * eased_, start_.y + (target_.y - start_.y) * eased_});
  if (eased_ >= 1) destroyed_ = true;
}

}  // namespace choco::games::greatcc
