#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "gfx/SpriteSheet.h"
#include "gfx/Texture.h"
#include "ui/WindowManager.h"

namespace choco::script {
class Log;
}

namespace choco::game {

// A thin, caching view of the window manager's texture and sheet caches for the game windows that build many sprites
// (the factories). A name is resolved once; a missing one is logged once and answers null, so a sprite without its
// picture simply draws nothing. With no renderer (unit tests) everything answers null.
class Textures {
 public:
  Textures(ui::WindowManager& wm, script::Log* log) : wm_(wm), log_(log) {}
  std::shared_ptr<gfx::Texture> texture(const std::string& name);
  // A texture only when the file exists, without logging a miss (the optional `_highlight` pictures).
  std::shared_ptr<gfx::Texture> optionalTexture(const std::string& name);
  std::shared_ptr<const gfx::SpriteSheet> sheet(const std::string& name);

 private:
  void missing(const std::string& what);
  ui::WindowManager& wm_;
  script::Log* log_;
  std::unordered_map<std::string, std::shared_ptr<gfx::Texture>> textures_;
  std::unordered_map<std::string, std::shared_ptr<const gfx::SpriteSheet>> sheets_;
  std::unordered_set<std::string> warned_;
};

}  // namespace choco::game
