#include "gfx/Image.h"

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS (1 << 14)
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace choco::gfx {

bool decodeImage(std::span<const std::byte> bytes, ImageData& out) {
  int w = 0, h = 0, n = 0;
  unsigned char* px = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<int>(bytes.size()), &w, &h, &n, 4);
  if (!px) return false;
  out.width = w;
  out.height = h;
  out.rgba.assign(px, px + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
  stbi_image_free(px);
  return true;
}

}  // namespace choco::gfx
