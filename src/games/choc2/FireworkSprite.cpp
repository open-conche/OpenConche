#include "games/choc2/FireworkSprite.h"

#include "gfx/TextureCache.h"

namespace choco::game::choc2 {

void FireworkSprite::resolveTexture(gfx::TextureCache* cache) {
  if (texture_ || !cache) return;
  if (const char* name = c2FireworkTextureName(firework_->textureIndex())) {
    if (!cache->resolve(name).path.empty()) texture_ = cache->load(name);
  }
}

void FireworkSprite::draw(gfx::Renderer& r, const sdk::SpriteSpec& parent) const {
  if (!visible || removed) return;
  firework_->draw(r, texture_.get(), {parent.origin.x, parent.origin.y}, parent.alpha);
}

}  // namespace choco::game::choc2
