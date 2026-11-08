#pragma once

#include <cmath>
#include <memory>
#include <string>

#include "sdk/Sprite.h"
#include "gfx/Texture.h"
#include "gfx/TextureCache.h"
#include "games/common/SdkNames.h"

namespace choco::game {

// Chocolatier 2's building pulse (docs/re/choc2-port-binary.md 1.6 and 3.2, TPort::UpdatePulseTexture and Draw). A child of the
// building sprite drawn behind it. The original makes a texture of the building's size that is flat white with the building's own
// alpha (a white silhouette), then draws it at alpha 0.75 and the building's scale, grown by `phase` * 20 px in width and in
// height. makeSilhouette builds that texture; the owner sets it as `texture`.
//
// The first version filled an opaque white box (a white square showed around the building), the second drew the building's own
// picture (wrong colours, a user video of the original shows white). The silhouette is the FACT construction of 3.2.
class PulseSprite : public Sprite {
 public:
  static constexpr float kGrowPx = 20;
  static constexpr float kAlpha = 0.75f;

  explicit PulseSprite(int layer = 0, std::string debugName = {}) : Sprite(layer, std::move(debugName)) {}

  // A texture the size of `src` with white RGB and src's alpha, cached under "$pulse:" + the picture's path. Null when src's
  // pixels cannot be read or there is no cache (the caller then falls back to src).
  static std::shared_ptr<gfx::Texture> makeSilhouette(gfx::TextureCache& cache, const gfx::Texture& src);

  // 0..1, set by the window each tick.
  float phase = 0;

  // The stretch of one axis: 1 + phase * ((size * scale + 20) / (size * scale) - 1), 1 for an empty image.
  static float stretch(float phase, float sizePx, float scale) {
    const float full = sizePx * scale;
    return full > 0 ? 1.0f + phase * (kGrowPx / full) : 1.0f;
  }

  void draw(gfx::Renderer& r, const SpriteSpec& parent) const override {
    if (!visible || removed || !texture) return;
    const float w = static_cast<float>(texture->width()), h = static_cast<float>(texture->height());
    const float s = std::fabs(parent.scale * scale);
    const float dw = w * s * stretch(phase, w, s), dh = h * s * stretch(phase, h, s);
    const gfx::PointF c{parent.origin.x + pos.x * parent.scale, parent.origin.y + pos.y * parent.scale};
    const gfx::RectF dst{c.x - dw / 2, c.y - dh / 2, dw, dh};
    const float a = parent.alpha * alpha * kAlpha;
    r.drawTexture(*texture, {}, dst, {1, 1, 1, a}, gfx::Blend::Normal, 0, {dw / 2, dh / 2}, false);
  }
};

}  // namespace choco::game
