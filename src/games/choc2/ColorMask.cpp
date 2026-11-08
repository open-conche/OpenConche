#include "games/choc2/ColorMask.h"

#include <algorithm>
#include <cstddef>

namespace choco::game::choc2 {

void composeColorMask(gfx::ImageData& colour, const std::vector<const gfx::ImageData*>& masks) {
  const auto w = static_cast<std::size_t>(std::max(0, colour.width));
  const auto h = static_cast<std::size_t>(std::max(0, colour.height));
  if (colour.rgba.size() < w * h * 4) return;
  for (std::size_t i = 0; i < w * h; ++i) colour.rgba[i * 4 + 3] = 0;
  for (const gfx::ImageData* m : masks) {
    if (!m) continue;
    const std::size_t mw = std::min(w, static_cast<std::size_t>(std::max(0, m->width)));
    const std::size_t mh = std::min(h, static_cast<std::size_t>(std::max(0, m->height)));
    if (m->rgba.size() < static_cast<std::size_t>(m->width) * static_cast<std::size_t>(m->height) * 4) continue;
    for (std::size_t y = 0; y < mh; ++y)
      for (std::size_t x = 0; x < mw; ++x) {
        std::uint8_t& dst = colour.rgba[(y * w + x) * 4 + 3];
        dst = std::max(dst, m->rgba[(y * static_cast<std::size_t>(m->width) + x) * 4 + 3]);
      }
  }
}

}  // namespace choco::game::choc2
