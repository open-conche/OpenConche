#include "games/common/Textures.h"

#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"
#include "script/ScriptContext.h"

namespace choco::game {

void Textures::missing(const std::string& what) {
  if (log_ && warned_.insert(what).second) log_->write("[sprite] cannot load '" + what + "'");
}

std::shared_ptr<gfx::Texture> Textures::optionalTexture(const std::string& name) {
  gfx::TextureCache* cache = wm_.textures();
  if (!cache) return nullptr;
  auto it = textures_.find(name);
  if (it != textures_.end()) return it->second;
  std::shared_ptr<gfx::Texture> t;
  if (!cache->resolve(name).path.empty()) t = cache->load(name);
  textures_.emplace(name, t);
  return t;
}

std::shared_ptr<gfx::Texture> Textures::texture(const std::string& name) {
  if (!wm_.textures()) return nullptr;
  auto t = optionalTexture(name);
  if (!t) missing(name);
  return t;
}

std::shared_ptr<const gfx::SpriteSheet> Textures::sheet(const std::string& name) {
  gfx::SheetCache* cache = wm_.sheets();
  if (!cache) return nullptr;
  auto it = sheets_.find(name);
  if (it != sheets_.end()) return it->second;
  auto s = cache->get(name);
  if (!s) missing(name);
  sheets_.emplace(name, s);
  return s;
}

}  // namespace choco::game
