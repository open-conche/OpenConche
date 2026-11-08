#include "games/choc2/SpriteWindow.h"

#include <cstdio>

#include "sdk/SpriteScript.h"
#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"
#include "ui/DrawContext.h"
#include "ui/WindowManager.h"

namespace choco::game::choc2 {

namespace {
bool isSheet(const std::string& image) { return image.size() > 4 && image.compare(image.size() - 4, 4, ".xml") == 0; }
}  // namespace

void SpriteWindow::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  image_ = style.stringOr("image");
  const float scale = static_cast<float>(style.numberOr("scale", 1));
  const float x = static_cast<float>(rect().x), y = static_cast<float>(rect().y);

  sprite_ = std::make_unique<Sprite>(0, "sprite:" + image_);
  sprite_->scale = scale;
  sprite_->rotationDeg = static_cast<float>(style.numberOr("rotation", 0));
  if (auto t = style.numbers("tint", 4, 1.0)) sprite_->tint = {float((*t)[0]), float((*t)[1]), float((*t)[2]), float((*t)[3])};
  if (!image_.empty()) {
    if (isSheet(image_)) {
      if (gfx::SheetCache* sheets = manager().sheets()) sprite_->sheet = sheets->get(image_);
    } else if (gfx::TextureCache* textures = manager().textures()) {
      sprite_->texture = textures->load(image_);
    }
  }
  const gfx::RectF size = sprite_->imageSize();
  const gfx::PointF a = sprite_->anchor();
  // The registration point is (x,y) in the parent: the window is the image around it.
  setRect({static_cast<int>(x - a.x * scale), static_cast<int>(y - a.y * scale), static_cast<int>(size.w * scale),
           static_cast<int>(size.h * scale)});
  sprite_->pos = {a.x * scale, a.y * scale};

  if (sprite_->sheet && manager().sheets()) {
    SpriteContext sc{*manager().sheets(), ctx_.rng, ctx_.log};
    sprite_->play("DoAnim", sc, manager().nowMs());
    if (scriptRunning()) manager().startWindowAnimation(id(), kTickMs);
  }
}

bool SpriteWindow::onAnimate() {
  if (!sprite_) return false;
  sprite_->tickScripts(manager().nowMs());
  return scriptRunning();  // a script that has ended (or failed) needs no more ticks
}

bool SpriteWindow::scriptRunning() const { return sprite_ && sprite_->scriptOf() && sprite_->scriptOf()->playing(); }

void SpriteWindow::draw(ui::DrawContext& ctx) const {
  if (sprite_) sprite_->draw(ctx.renderer, SpriteSpec{ctx.origin, 1, 0, ctx.alpha});
}

std::string SpriteWindow::detail() const {
  char buf[200];
  std::snprintf(buf, sizeof buf, " image=\"%s\"%s", image_.c_str(),
                sprite_ && (sprite_->texture || sprite_->sheet) ? "" : " noimage");
  return buf;
}

void AnimationWindow::init(const ui::Style& style) {
  Window::init(style);
  fps_ = style.numberOr("fps", 0);
}

std::string AnimationWindow::detail() const { return " fps=" + std::to_string(static_cast<int>(fps_)); }

}  // namespace choco::game::choc2
