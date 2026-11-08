#include "games/choc2/Character.h"

#include <algorithm>
#include <cstdio>

#include "gfx/TextureCache.h"
#include "ui/DrawContext.h"
#include "ui/WindowManager.h"

namespace choco::game::choc2 {

Character::Face Character::faceFor(double mood) {
  if (mood <= kAngry) return Displeased;
  if (mood < kDefault) return Neutral;
  return Pleased;
}

void Character::postChildrenInit(const ui::Style& style) {
  BSGWindow::postChildrenInit(style);
  setAlpha(static_cast<float>(style.numberOr("alpha", 1)));  // BSGWindow's default colour alpha 0 would hide it
  if (auto c = style.table("char")) {
    name_ = c->stringOr("name");
    mood_ = c->numberOr("mood", 50);
    appearance_ = c->stringOr("appearance", name_);  // the pictures' name; several characters may share one (the NY gamblers)
  }
  if (gfx::TextureCache* textures = manager().textures()) {
    const std::string base = "characters/" + appearance_;
    body_ = textures->load(base + ".png");
    // The faces count only as a set of three.
    const char* const kFaces[3] = {"_displeased.png", "_neutral.png", "_pleased.png"};
    bool all = true;
    for (const char* face : kFaces) all = all && !textures->resolve(base + face).path.empty();  // most characters have none
    if (all)
      for (int i = 0; i < 3; ++i) faces_[i] = textures->load(base + kFaces[i]);
  }
  if (body_) setSize(std::max(rect().w, body_->width()), std::max(rect().h, body_->height()));
  face_ = faceFor(mood_);
}

void Character::setMood(double mood) {
  mood_ = mood;
  face_ = faceFor(mood);
}

void Character::draw(ui::DrawContext& ctx) const {
  gfx::Renderer& r = ctx.renderer;
  const float alpha = ctx.alpha;
  if (alpha <= 0 || !body_) return;
  auto put = [&](const gfx::Texture& t, float a) {
    if (a <= 0) return;
    const gfx::RectF dst{ctx.origin.x, ctx.origin.y, static_cast<float>(t.width()), static_cast<float>(t.height())};
    r.drawTexture(t, {}, dst, {1, 1, 1, a}, gfx::Blend::Normal);
  };
  put(*body_, alpha);
  if (!hasFaces()) return;
  put(*faces_[face_], alpha);
}

std::string Character::detail() const {
  char buf[128];
  std::snprintf(buf, sizeof buf, " character=%s mood=%g face=%d%s", name_.c_str(), mood_, face_, hasFaces() ? "" : " nofaces");
  return buf;
}

}  // namespace choco::game::choc2
