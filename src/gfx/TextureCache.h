#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "assets/AssetFS.h"
#include "gfx/HitMask.h"
#include "gfx/Image.h"
#include "gfx/Texture.h"

namespace choco::gfx {

// Creates and destroys GPU textures. Implemented by the SDL renderer.
class TextureUploader {
 public:
  virtual ~TextureUploader() = default;
  // rgba is w*h*4 straight-alpha bytes. Returns an opaque handle or null on failure.
  virtual void* upload(const std::uint8_t* rgba, int w, int h) = 0;
  virtual void release(void* handle) = 0;
  // Overwrites the w*h rectangle at (x, y) of an uploaded texture in place. `rgba` points at that rectangle's first
  // texel; `stride` is the byte distance between its rows. Returns false when unsupported or failed, and the caller
  // uploads the whole image again.
  virtual bool update(void* handle, int x, int y, int w, int h, const std::uint8_t* rgba, int stride) {
    (void)handle; (void)x; (void)y; (void)w; (void)h; (void)rgba; (void)stride;
    return false;
  }
};

// Loads textures by asset name, e.g. "image/title_background" or "image/foo.png?options". Resolution probes
// .png .jpg .jp2 (an explicit extension is used as given). Entries are keyed by the lowercased resolved path; the
// options of the first request for a path stay on its Texture. The uploader must outlive every Texture it made.
class TextureCache {
 public:
  TextureCache(const assets::AssetFS& fs, TextureUploader* uploader) : fs_(fs), uploader_(uploader) {}
  TextureCache(const TextureCache&) = delete;
  TextureCache& operator=(const TextureCache&) = delete;

  // Null (and a logged line) if the file is missing or cannot be decoded.
  // `mask` names an image whose blue channel becomes the alpha (resolved like `name`). With no mask given, a JPG looks
  // for a sibling "<base>.mask.png". A mask of another size is logged and ignored, leaving the texture opaque. A
  // missing explicit mask is logged and ignored. Entries are keyed by image and mask together.
  std::shared_ptr<Texture> load(std::string_view name, std::string_view mask = {});
  // CPU-only mask from the same name resolution and decoder.
  std::shared_ptr<HitMask> loadMask(std::string_view name) const;

  // The decoded pixels of `name` (resolved and masked as load() does) without making a texture: for compositing on the CPU.
  // False (and a logged line) if the file is missing or cannot be decoded.
  bool loadImage(std::string_view name, ImageData& out) const;

  // A texture whose pixels are made in memory (the Chocolatier 2 logo). `key` names it: a second call with the same key and
  // the same size updates that texture in place (the Texture object stays), another size replaces its image. The cache
  // keeps a copy of the pixels, so reuploadAll() restores it after a device reset. Dropped by trim() once nobody else holds
  // it. Null if the upload fails. `key` must not be an asset path (it starts with '$' by convention).
  std::shared_ptr<Texture> makeFromPixels(const std::string& key, const ImageData& img);

  // The alpha channel of a texture this cache made (decoded again from its file, or copied from its memory pixels), kept
  // for later calls. Null when the texture is not in this cache or cannot be decoded.
  const AlphaMask* alphaOf(const Texture& tex) const;

  // Drops entries nobody else holds.
  void trim();
  // Re-creates every cached texture from its file after a device reset. The Texture objects stay the same.
  void reuploadAll();

  std::size_t size() const { return entries_.size(); }

  // "name?options" -> resolved path (empty if missing) and options.
  struct Resolved {
    std::string path, options;
  };
  Resolved resolve(std::string_view name) const;

 private:
  const assets::AssetFS& fs_;
  TextureUploader* uploader_;
  // Decodes `path` and merges the alpha of `maskPath` (when not empty) into it.
  bool loadPixels(const std::string& path, const std::string& maskPath, ImageData& img) const;
  // The mask to merge for an image at `path`: the explicit one resolved, else the JPG sibling; empty if none.
  std::string findMask(const std::string& path, std::string_view explicitMask) const;
  std::map<std::string, std::shared_ptr<Texture>> entries_;
  mutable std::map<std::string, std::unique_ptr<AlphaMask>> alphas_;  // by path and mask; an entry may be null (undecodable)
  std::map<std::string, ImageData> memory_;  // pixels of the makeFromPixels textures, by key
};

}  // namespace choco::gfx
