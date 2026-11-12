#include "games/greatcc/scene/Digit.h"

#include "games/greatcc/scene/SpriteManager.h"

namespace choco::games::greatcc {

void Digit::loadFromTable(const StyleChain& style) {
  VObject::loadFromTable(style);
  if (text_) removeChild(*text_);
  text_ = TextSprite::create(context());
  addChild(text_);
  text_->loadFromTable(style);
  text_->setText("0");
  shown_ = -1;
}

void Digit::update(bool softPaused) {
  VObject::update(softPaused);
  if (!text_ || !manager()) return;
  const double now = manager()->now();
  if (now - lastTick_ < kTickMs) return;
  if (shown_ == target_) return;
  shown_ += shown_ < target_ ? 1 : -1;
  text_->setText(std::to_string(shown_));
  lastTick_ = now;
}

}  // namespace choco::games::greatcc
