#include "gfx/HitMask.h"

#include <cmath>

namespace choco::gfx {

bool HitMask::hit(int x, int y) const {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return false;
  const std::uint8_t* p = &rgb_[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) + static_cast<std::size_t>(x)) * 3];
  return p[0] > kThreshold || p[1] > kThreshold || p[2] > kThreshold;
}

bool HitMask::hitScaled(float px, float py, float rectW, float rectH) const {
  if (empty() || rectW <= 0 || rectH <= 0 || px < 0 || py < 0 || px >= rectW || py >= rectH) return false;
  int x = static_cast<int>(std::floor(px * static_cast<float>(w_) / rectW));
  int y = static_cast<int>(std::floor(py * static_cast<float>(h_) / rectH));
  return hit(x, y);
}

}  // namespace choco::gfx
