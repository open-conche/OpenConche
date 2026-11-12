#pragma once

// VSpriteEffect and its subclasses (docs/re/greatcc-sprites-and-zones.md 3). An effect is shared by the sprites it is added to
// (a propagating one goes to the sprite's children too). The manager calls onUpdate once per frame per effect; the sprite
// calls the draw hooks: PreDraw and PreRender in ascending id order, PostRender and PostDraw in descending order, so effects
// nest. Times are in ms on the manager's clock.

#include <memory>

#include "games/greatcc/scene/VSprite.h"

namespace choco::games::greatcc {

class SpriteEffect {
 public:
  SpriteEffect();
  virtual ~SpriteEffect() = default;
  int id() const { return id_; }
  // Pushed to the children of a sprite it is added to (flag +0x34).
  bool propagate() const { return propagate_; }
  void setPropagate(bool p) { propagate_ = p; }

  virtual void addedToSprite(VSprite&) { ++refs_; }
  virtual void removedFromSprite(VSprite&) { --refs_; }
  virtual bool inProgress() const { return inProgress_; }
  // True once destroyed or no sprite holds it.
  virtual bool shouldDelete() const { return destroyed_ || refs_ <= 0; }
  virtual void stop() { inProgress_ = false; }
  void destroy() { destroyed_ = true; }
  bool destroyed() const { return destroyed_; }
  int refs() const { return refs_; }
  // The base records the time and returns true; subclasses read elapsed() first. Returning false drops the effect.
  virtual bool onUpdate(double now, bool softPaused);
  virtual void onPreRender(VSprite&, DrawEnv&) {}
  virtual void onPostRender(VSprite&, DrawEnv&) {}
  virtual void onPreDraw(VSprite&, DrawEnv&) {}
  virtual void onPostDraw(VSprite&, DrawEnv&) {}

 protected:
  // The time since the previous update (0 on the first).
  double elapsed(double now) const { return stamped_ ? now - last_ : 0; }

  bool inProgress_ = true;
  bool destroyed_ = false;

 private:
  int id_;
  int refs_ = 0;
  bool propagate_ = false;
  bool stamped_ = false;
  double last_ = 0;
};

// VAlphaEffect(fadeIn, max, durationMs, startAlpha, delayMs). startAlpha < 0 means 0 for a fade in and 1 for a fade out.
class AlphaEffect : public SpriteEffect {
 public:
  AlphaEffect(bool fadeIn, float max, double durationMs, float startAlpha = -1, double delayMs = 0);
  void addedToSprite(VSprite& s) override;
  bool onUpdate(double now, bool softPaused) override;
  void onPreRender(VSprite& s, DrawEnv&) override { s.setAlpha(alpha_); }
  void onPostRender(VSprite&, DrawEnv&) override {
    if (!inProgress_) destroyed_ = true;
  }
  float alpha() const { return alpha_; }
  bool fadeIn() const { return fadeIn_; }

 private:
  bool fadeIn_;
  float max_, alpha_;
  double duration_, delay_;
};

// VTintEffect(targetIntensity, durationMs, colour, startIntensity): propagating. After stop() it ramps back to 0 and destroys itself.
class TintEffect : public SpriteEffect {
 public:
  TintEffect(float target, double durationMs, gfx::ColorF color, float start = 0)
      : target_(target), duration_(durationMs), color_(color), intensity_(start) {
    setPropagate(true);
  }
  bool onUpdate(double now, bool softPaused) override;
  void onPreRender(VSprite& s, DrawEnv&) override;
  void onPostRender(VSprite& s, DrawEnv&) override;
  float intensity() const { return intensity_; }
  // VTintEffect::SetIntensity: the intensity it works toward.
  void setIntensity(float target) { target_ = target; }
  float target() const { return target_; }

 private:
  float target_;
  double duration_;
  gfx::ColorF color_;
  float intensity_;
  int nest_ = 0;
  gfx::ColorF saved_;
};

// VGlowEffect(peak, periodMs, cycles, accel): propagating. Draws the sprite again after itself, additive, with the intensity as
// alpha. Each `periodMs` is one half wave of a triangle between 0 and `peak` (even periods rise, odd ones fall;
// cycles <= 0 for ever, else it stops after `cycles` periods); once stopped it falls linearly and the effect ends. `accel` is added to the time accumulator as 0.5*accel*dt^2 as in the original, which is only
// meaningful for 0.
class GlowEffect : public SpriteEffect {
 public:
  GlowEffect(float peak, double periodMs, float cycles, float accel);
  bool onUpdate(double now, bool softPaused) override;
  void onPostRender(VSprite& s, DrawEnv& env) override;
  float intensity() const { return intensity_; }

 private:
  float peak_;
  double period_;
  float cycles_, accel_;
  double time_ = 0;
  float intensity_ = 0;
};

// VHoverGlow(max, propagate, offsetX, offsetY): a halo while the pointer is over the sprite: four additive passes, each shifted
// by half a pixel along the axes whose flag is set. The sprite's Enter and Leave events call
// setHovered (addedToSprite registers the listener that does).
class HoverGlow : public SpriteEffect {
 public:
  HoverGlow(float max, bool propagate, bool offsetX, bool offsetY);
  void addedToSprite(VSprite& s) override;
  void removedFromSprite(VSprite& s) override;
  bool onUpdate(double now, bool softPaused) override;
  void onPostRender(VSprite& s, DrawEnv& env) override;
  void setHovered(bool h) { hovered_ = h; }
  float intensity() const { return intensity_; }

 private:
  struct Hover;
  float max_;
  bool offsetX_, offsetY_;
  bool hovered_ = false;
  float intensity_ = 0;
  std::shared_ptr<SpriteListener> listener_;
};

// VWipeEffect(direction 0..3, reveal, durationMs): clips the sprite's rectangle by a growing amount. A wipe away (reveal false)
// hides the sprite when it ends.
class WipeEffect : public SpriteEffect {
 public:
  WipeEffect(int direction, bool reveal, double durationMs) : dir_(direction), reveal_(reveal), duration_(durationMs) {}
  bool onUpdate(double now, bool softPaused) override;
  void onPreDraw(VSprite& s, DrawEnv& env) override;
  void onPostDraw(VSprite& s, DrawEnv& env) override;
  float progress() const { return progress_; }
  // The clip rectangle for sprite rectangle `r` at the current progress.
  gfx::RectF clipOf(const gfx::RectF& r) const;

 private:
  int dir_;
  bool reveal_;
  double duration_;
  float progress_ = 0;
  bool pushed_ = false;
};

// VSpriteMove(target, durationMs, accel, delayMs): moves the sprite from where it was when the effect was added to `target`.
class SpriteMove : public SpriteEffect {
 public:
  SpriteMove(gfx::PointF target, double durationMs, float accel = 0, double delayMs = 0)
      : target_(target), duration_(durationMs), accel_(accel), delay_(delayMs) {}
  void addedToSprite(VSprite& s) override;
  bool onUpdate(double now, bool softPaused) override;
  void onPreDraw(VSprite& s, DrawEnv&) override;
  float eased() const { return eased_; }

 private:
  gfx::PointF target_, start_;
  double duration_;
  float accel_;
  double delay_;
  double t_ = 0;
  float eased_ = 0;
};

}  // namespace choco::games::greatcc
