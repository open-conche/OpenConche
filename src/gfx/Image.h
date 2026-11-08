#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace choco::gfx {

struct ImageData {
  int width = 0, height = 0;
  std::vector<std::uint8_t> rgba;  // width * height * 4, straight alpha
};

// Decodes PNG or JPEG (stb_image). Returns false on any other format (JPEG 2000 is not supported) or on bad data.
bool decodeImage(std::span<const std::byte> bytes, ImageData& out);

}  // namespace choco::gfx
