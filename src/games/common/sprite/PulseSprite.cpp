#include "games/common/sprite/PulseSprite.h"

#include <cstdint>
#include <vector>

#include "gfx/HitMask.h"
#include "gfx/Image.h"

namespace choco::game {

std::shared_ptr<gfx::Texture> PulseSprite::makeSilhouette(gfx::TextureCache& cache, const gfx::Texture& src) {
  const gfx::AlphaMask* mask = cache.alphaOf(src);
  if (!mask || mask->width() <= 0 || mask->height() <= 0) return nullptr;
  gfx::ImageData img;
  img.width = mask->width();
  img.height = mask->height();
  img.rgba.assign(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 4, 255);
  for (int y = 0; y < img.height; ++y)
    for (int x = 0; x < img.width; ++x)
      img.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(x)) * 4 + 3] =
          static_cast<std::uint8_t>(mask->alphaAt(x, y));
  return cache.makeFromPixels("$pulse:" + src.path() + "|" + src.maskPath(), img);
}

}  // namespace choco::game
