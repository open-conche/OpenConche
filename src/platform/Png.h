#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace choco::platform {

struct Image {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;  // width * height * 4
};

bool writePng(const std::string& path, const Image& image);

// The same for a raw buffer of width * height * 4 RGBA bytes.
bool writePng(const std::string& path, int width, int height, const std::uint8_t* rgba);

}  // namespace choco::platform
