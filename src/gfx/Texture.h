#pragma once

#include <string>

#include "gfx/Types.h"

namespace choco::gfx {

class TextureUploader;
class TextureCache;
namespace text {
class GlyphAtlas;
}

// A GPU image. The handle is opaque to everything outside the renderer implementation. The cache replaces the handle
// in place after a device reset, so holders of a Texture stay valid.
class Texture {
 public:
  ~Texture();
  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;

  // Logical size: what layout sees. Equal to the pixel size until scaled variants are supported.
  int width() const { return width_; }
  int height() const { return height_; }
  int pixelWidth() const { return pixelWidth_; }
  int pixelHeight() const { return pixelHeight_; }
  const std::string& path() const { return path_; }        // resolved asset path, real case
  const std::string& maskPath() const { return maskPath_; }  // resolved mask the alpha was merged from, else empty
  const std::string& options() const { return options_; }  // text after '?' in the requested name, else empty
  void* handle() const { return handle_; }

 private:
  friend class TextureCache;
  friend class text::GlyphAtlas;  // atlas pages are textures built from memory
  Texture() = default;
  int width_ = 0, height_ = 0, pixelWidth_ = 0, pixelHeight_ = 0;
  std::string path_, options_, maskPath_;
  void* handle_ = nullptr;
  TextureUploader* uploader_ = nullptr;  // must outlive the texture
};

}  // namespace choco::gfx
