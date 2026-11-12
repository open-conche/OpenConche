#pragma once
#include <cstddef>

namespace choco::platform {
// The window (and, unbundled, Dock) icon: a PNG embedded at build time from packaging/icon/window128.png.
extern const unsigned char kWindowIconPng[];
extern const std::size_t kWindowIconPngSize;
}  // namespace choco::platform
