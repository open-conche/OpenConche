#pragma once

namespace choco::platform {

constexpr int kLogicalWidth = 800;
constexpr int kLogicalHeight = 600;

// The requested scale (any value >= 1, e.g. 1.5) if its window fits the given usable desktop area; otherwise the
// next whole number below it that does, down to a minimum of 1.
double chooseScale(double requested, int usableWidth, int usableHeight);

// The window size in pixels for a scale: the logical size times the scale, rounded to whole pixels.
int scaledWindowSize(int logical, double scale);

}  // namespace choco::platform
