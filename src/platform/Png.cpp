#include "platform/Png.h"

#include <SDL3/SDL_surface.h>

namespace choco::platform {

bool writePng(const std::string& path, int width, int height, const std::uint8_t* rgba) {
  if (width <= 0 || height <= 0 || rgba == nullptr) {
    return false;
  }
  // Image buffers are R, G, B, A in byte order, which is what SDL_PIXELFORMAT_RGBA32 means.
  SDL_Surface* surface =
      SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32,
                            const_cast<std::uint8_t*>(rgba), width * 4);
  if (surface == nullptr) {
    return false;
  }
  const bool ok = SDL_SavePNG(surface, path.c_str());
  SDL_DestroySurface(surface);
  return ok;
}

bool writePng(const std::string& path, const Image& image) {
  if (image.rgba.size() != static_cast<std::size_t>(image.width) * image.height * 4) {
    return false;
  }
  return writePng(path, image.width, image.height, image.rgba.data());
}

}  // namespace choco::platform
