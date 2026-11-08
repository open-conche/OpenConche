#include "sdk/TextSprite.h"

namespace choco::sdk {

TextSprite::TextSprite(int layer, std::string debugName, gfx::text::TextLayout layout, int flags, int boxW, int boxH)
    : Sprite(layer, std::move(debugName)), layout_(std::move(layout)), flags_(flags), w_(boxW), h_(boxH) {}

gfx::PointF TextSprite::boxOrigin() const {
  using namespace gfx::text;
  gfx::PointF o = pos;
  const int h = flags_ & 3, v = flags_ & 12;
  if (h == kHAlignRight) o.x -= static_cast<float>(w_);
  else if (h == kHAlignCenter) o.x -= static_cast<float>(w_) * 0.5f;
  if (v == kVAlignBottom) o.y -= static_cast<float>(h_);
  else if (v == kVAlignCenter) o.y -= static_cast<float>(h_) * 0.5f;
  return o;
}

void TextSprite::draw(gfx::Renderer& r, const SpriteSpec& parent) const {
  if (!visible || removed) return;
  SpriteSpec mine = parent;
  mine.origin = {parent.origin.x + pos.x * parent.scale, parent.origin.y + pos.y * parent.scale};
  mine.scale = parent.scale * scale;
  mine.alpha = parent.alpha * alpha;
  const gfx::PointF box = boxOrigin();
  std::size_t i = 0;
  for (; i < children().size() && children()[i]->layer() < 0; ++i) children()[i]->draw(r, mine);  // under the text
  layout_.draw(r, {parent.origin.x + box.x * parent.scale, parent.origin.y + box.y * parent.scale}, mine.alpha * tint.a);
  for (; i < children().size(); ++i) children()[i]->draw(r, mine);
}

}  // namespace choco::sdk
