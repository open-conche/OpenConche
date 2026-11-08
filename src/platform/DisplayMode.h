#pragma once

namespace choco::platform {

constexpr int kLogicalWidth = 800;
constexpr int kLogicalHeight = 600;

// Largest integer scale <= requested (minimum 1) whose window fits the given usable desktop area.
int chooseScale(int requested, int usableWidth, int usableHeight);

}  // namespace choco::platform
