#include "platform/DisplayMode.h"

#include <cmath>

namespace choco::platform {

namespace {
bool fits(double scale, int usableWidth, int usableHeight) {
  return scaledWindowSize(kLogicalWidth, scale) <= usableWidth && scaledWindowSize(kLogicalHeight, scale) <= usableHeight;
}
}  // namespace

int scaledWindowSize(int logical, double scale) { return static_cast<int>(std::lround(logical * scale)); }

double chooseScale(double requested, int usableWidth, int usableHeight) {
  double scale = requested < 1 ? 1 : requested;
  while (scale > 1 && !fits(scale, usableWidth, usableHeight)) {
    scale = std::ceil(scale) - 1;  // the whole number below: 2 -> 1, 1.5 -> 1, 3.5 -> 3
    if (scale < 1) scale = 1;
  }
  return scale;
}

}  // namespace choco::platform
