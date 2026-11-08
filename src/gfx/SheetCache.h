#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>

#include "assets/AssetFS.h"
#include "gfx/SpriteSheet.h"
#include "gfx/TextureCache.h"

namespace choco::gfx {

// Loads sprite sheets by name, as TAnimatedTexture::Get does. Entries are keyed by the resolved XML path and the two
// overrides. `image` replaces the sheet's image and `mask` its mask; both are looked up next to the XML first, then
// from the asset root.
class SheetCache {
 public:
  SheetCache(const assets::AssetFS& fs, TextureCache& textures) : fs_(fs), textures_(textures) {}
  SheetCache(const SheetCache&) = delete;
  SheetCache& operator=(const SheetCache&) = delete;

  // Probes `<name>`, then `<name>.xml`, then `<name>.anm` (logged once as unsupported). Null when there is no sheet, so
  // callers can fall back to a plain texture; a sheet that exists but is broken is logged once and also gives null.
  std::shared_ptr<const SpriteSheet> get(std::string_view name, std::string_view image = {}, std::string_view mask = {});

  // Drops sheets nobody else holds. Run before TextureCache::trim so the sheets let go of their images.
  void trim();
  std::size_t size() const { return entries_.size(); }

 private:
  void logOnce(const std::string& key, const std::string& line);
  const assets::AssetFS& fs_;
  TextureCache& textures_;
  std::map<std::string, std::shared_ptr<const SpriteSheet>> entries_;
  std::set<std::string> logged_;
};

}  // namespace choco::gfx
