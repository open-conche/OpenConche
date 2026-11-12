#include "games/greatcc/scene/TextSprite.h"

#include <cmath>
#include <cstdio>

#include "ui/WindowManager.h"
#include "ui/widgets/TextWidgets.h"

namespace choco::games::greatcc {

namespace {
std::string hex2(float c) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "%02x", std::clamp(static_cast<int>(c * 255), 0, 255));
  return buf;
}
}  // namespace

TextSprite::TextSprite(script::ScriptContext& ctx, std::string name) : VSprite(ctx, std::move(name)) {
  setMouse(false);
  style_.size = 10;
  style_.flags = 5;
  style_.color = {1, 1, 1, 1};
  if (gfx::text::TextSystem* ts = ctx.wm.textSystem()) {
    style_.font = ts->fonts.get("__DEFAULT");
    style_.fonts = &ts->fonts;
  }
}

std::string TextSprite::markup() const {
  std::string s = text_;
  if (bold_) s = "<b>" + s + "</b>";
  if (italic_) s = "<i>" + s + "</i>";
  if (outlineWidth_ != 0)
    s = "<outline color='" + hex2(outlineColor_.r) + hex2(outlineColor_.g) + hex2(outlineColor_.b) + "' size=" +
        std::to_string(outlineWidth_) + ">" + s + "</outline>";
  return s;
}

void TextSprite::setText(const std::string& s) {
  text_ = s;
  invalidate();
}

void TextSprite::setSize(int w, int h) {
  w_ = w;
  h_ = h;
  invalidate();
}

void TextSprite::loadFromTable(const StyleChain& style) {
  setDepth(style.intOr("spriteLayer", depth(), 1));
  setLayer(style.intOr("layer", layer()));
  setName(style.stringOr("name", name()));
  const std::string label = style.stringOr("label", "");
  const double sc = style.numberOr("scale", 1.0);
  setScale(static_cast<float>(sc), static_cast<float>(sc));
  w_ = style.intOr("w", w_);
  h_ = style.intOr("h", h_);
  setPosition({static_cast<float>(style.numberOr("x", position().x)), static_cast<float>(style.numberOr("y", position().y))});
  bold_ = style.boolOr("bold", false);
  italic_ = style.boolOr("italic", false);
  if (auto m = style.numbers("margin", 4, 0.0)) {
    margin_[0] = static_cast<float>((*m)[0]);
    margin_[1] = static_cast<float>((*m)[1]);
    margin_[2] = static_cast<float>((*m)[0] + (*m)[2]);
    margin_[3] = static_cast<float>((*m)[1] + (*m)[3]);
  }
  style_.flags = static_cast<int>(style.numberOr("flags", 5));
  if (auto font = style.table("font")) {
    gfx::text::FontCache* fonts = style_.fonts;
    const std::string face = font->stringAt(1).value_or("__DEFAULT");
    if (fonts) style_.font = fonts->get(face);
    const int size = static_cast<int>(font->numberAt(2).value_or(style_.size));
    style_.size = static_cast<float>(size == 0 ? 12 : size);
    if (auto col = font->element(3)) {
      style_.color = {static_cast<float>(col->numberAt(1).value_or(1)), static_cast<float>(col->numberAt(2).value_or(1)),
                      static_cast<float>(col->numberAt(3).value_or(1)), static_cast<float>(col->numberAt(4).value_or(1))};
    }
  }
  if (auto o = style.table("outline")) {
    if (auto col = o->element(1))
      outlineColor_ = {static_cast<float>(col->numberAt(1).value_or(0)), static_cast<float>(col->numberAt(2).value_or(0)),
                       static_cast<float>(col->numberAt(3).value_or(0)), static_cast<float>(col->numberAt(4).value_or(1))};
    outlineWidth_ = static_cast<int>(o->numberAt(2).value_or(0));
  }
  if (auto sh = style.table("shadow")) {
    hasShadow_ = true;
    shadowOffset_ = {static_cast<float>(sh->numberAt(1).value_or(0)), static_cast<float>(sh->numberAt(2).value_or(0))};
    if (auto col = sh->element(3))
      shadowColor_ = {static_cast<float>(col->numberAt(1).value_or(0)), static_cast<float>(col->numberAt(2).value_or(0)),
                      static_cast<float>(col->numberAt(3).value_or(0)), static_cast<float>(col->numberAt(4).value_or(0.5))};
  }
  if (style.table("rotation") && context().warned.insert("greatcc-textsprite-rotation").second)
    context().log.write("[greatcc] VTextSprite: `rotation` is not supported (the text is drawn unrotated)");
  setText(ui::resolveLabel(label, context().wm.labelLookup()));
}

void TextSprite::copyStyleFrom(const TextSprite& o) {
  style_ = o.style_;
  w_ = o.w_;
  h_ = o.h_;
  bold_ = o.bold_;
  italic_ = o.italic_;
  for (int i = 0; i < 4; ++i) margin_[i] = o.margin_[i];
  outlineWidth_ = o.outlineWidth_;
  outlineColor_ = o.outlineColor_;
  hasShadow_ = o.hasShadow_;
  shadowOffset_ = o.shadowOffset_;
  shadowColor_ = o.shadowColor_;
  text_ = o.text_;
  setPosition(o.position());  // CopyStyle takes the place too
  invalidate();
}

const gfx::text::TextLayout& TextSprite::layoutFor(bool shadow) const {
  auto& slot = shadow ? shadowLayout_ : layout_;
  if (!slot) {
    gfx::text::TextStyle st = style_;
    if (shadow) st.color = {shadowColor_.r, shadowColor_.g, shadowColor_.b, 1};  // its alpha scales the draw
    slot = std::make_unique<gfx::text::TextLayout>(gfx::text::TextLayout::build(
        markup(), st, static_cast<int>(w_ - margin_[2]), static_cast<int>(h_ - margin_[3]), static_cast<int>(h_)));
  }
  return *slot;
}

gfx::RectI TextSprite::textBounds() const {
  gfx::RectI b{};
  if (!text_.empty() && w_ > 0 && h_ > 0) b = layoutFor(false).bounds();
  const int l = b.x - static_cast<int>(margin_[0]), t = b.y - static_cast<int>(margin_[1]);
  const int r = b.x + b.w + static_cast<int>(margin_[2] - margin_[0]), bt = b.y + b.h + static_cast<int>(margin_[3] - margin_[1]);
  return {l, t, r - l, bt - t};
}

void TextSprite::drawImage(gfx::Renderer& r, const DrawSpec& spec) const {
  const Mat3& abs = spec.m;
  const float alpha = spec.alpha;
  if (text_.empty() || w_ <= 0 || h_ <= 0) return;
  // The text origin is the matrix's translation rounded, plus the margins; the layout itself is not scaled or rotated.
  const gfx::PointF base{std::floor(abs.m[2][0] + 0.5f) + margin_[0], std::floor(abs.m[2][1] + 0.5f) + margin_[1]};
  gfx::text::TextSystem* ts = context().wm.textSystem();
  auto draw = [&](const gfx::text::TextLayout& l, gfx::PointF o, float a) {
    if (ts) l.draw(r, ts->atlas, o, a);
    else l.draw(r, o, a);
  };
  if (hasShadow_) {
    draw(layoutFor(true), {base.x + std::floor(shadowOffset_.x + 0.5f), base.y + std::floor(shadowOffset_.y + 0.5f)},
         alpha * shadowColor_.a);
  }
  draw(layoutFor(false), base, alpha);
}

gfx::RectF TextSprite::frameRect(const DrawSpec& spec) const {
  const Mat3& abs = spec.m;
  if (w_ <= 0 || h_ <= 0) return {};
  const gfx::PointF c[4] = {abs.apply({0, 0}), abs.apply({static_cast<float>(w_), 0}),
                            abs.apply({static_cast<float>(w_), static_cast<float>(h_)}), abs.apply({0, static_cast<float>(h_)})};
  float x0 = c[0].x, x1 = c[0].x, y0 = c[0].y, y1 = c[0].y;
  for (const auto& p : c) {
    x0 = std::min(x0, p.x);
    x1 = std::max(x1, p.x);
    y0 = std::min(y0, p.y);
    y1 = std::max(y1, p.y);
  }
  return {x0, y0, x1 - x0, y1 - y0};
}

TextLineCount countTextLines(const VSprite& root) {
  TextLineCount n;
  if (const auto* t = dynamic_cast<const TextSprite*>(&root)) {
    n.drawn += t->drawnLineCount();
    n.total += t->lineCount();
  }
  for (const auto& child : root.children()) {
    const TextLineCount c = countTextLines(*child);
    n.drawn += c.drawn;
    n.total += c.total;
  }
  return n;
}

}  // namespace choco::games::greatcc
