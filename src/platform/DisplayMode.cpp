#include "platform/DisplayMode.h"

namespace choco::platform {

int chooseScale(int requested, int usableWidth, int usableHeight) {
  int scale = requested < 1 ? 1 : requested;
  while (scale > 1 && (kLogicalWidth * scale > usableWidth || kLogicalHeight * scale > usableHeight)) {
    --scale;
  }
  return scale;
}

}  // namespace choco::platform
