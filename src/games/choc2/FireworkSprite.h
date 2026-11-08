#pragma once

// Chocolatier 2's port firework sprite (TPort::TFirework, docs/re/choc2-port-binary.md 7): one of the five fireworks
// StartFireworks adds to the port's sprite tree, at the layout position the call sat at (drawn between the sprites
// around it). The state machine is the Chocolatier 3 `game::Firework` with Chocolatier 2's numbers (`kC2Style`: the
// sound firework<1..6>, the burst at x in [100,699], y in [10,99]); its texture is one of Chocolatier 2's five
// particleOptions (blue, green, red, white, yellow - the doc's table order), rolled at construction and held here.

#include <memory>

#include "games/choc3/Particles.h"
#include "gfx/Texture.h"
#include "gfx/TextureCache.h"
#include "sdk/Sprite.h"

namespace choco::game::choc2 {

// TPort::TFirework's rolls (facts of the Mac binary, choc2-port-binary.md 7; the FireworksWindow numbers are
// Chocolatier 3's, docs/re/particles.md).
constexpr FireworkStyle kC2FireworkStyle{6, 100, 600, 10, 90};

// Chocolatier 2's particleOptions: five real entries (Chocolatier 3's has white missing and a NULL entry instead).
inline const char* c2FireworkTextureName(int index) {
  static const char* const names[] = {"glitz/firework_blue", "glitz/firework_green", "glitz/firework_red",
                                     "glitz/firework_white", "glitz/firework_yellow"};
  return index >= 0 && index < 5 ? names[index] : nullptr;
}

class FireworkSprite : public sdk::Sprite {
 public:
  FireworkSprite(int layer, std::unique_ptr<Firework> firework)
      : sdk::Sprite(layer, "firework"), firework_(std::move(firework)) {}

  void draw(gfx::Renderer& r, const sdk::SpriteSpec& parent) const override;

  Firework* firework() const { return firework_.get(); }
  const gfx::Texture* texture() const { return texture_.get(); }
  // Loads the rolled particleOptions picture once, at the port build.
  void resolveTexture(gfx::TextureCache* cache);

 private:
  std::unique_ptr<Firework> firework_;
  std::shared_ptr<gfx::Texture> texture_;
};

}  // namespace choco::game::choc2
