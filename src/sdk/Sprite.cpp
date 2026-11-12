#include "sdk/Sprite.h"

#include "sdk/SpriteScript.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace choco::sdk {

namespace {

constexpr float kDegToRad = 3.14159265358979f / 180.0f;

// Rounds to one decimal and avoids printing "-0.0".
std::string fmt(float v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.1f", static_cast<double>(v));
  std::string s = buf;
  if (s == "-0.0") s = "0.0";
  return s;
}

}  // namespace

Sprite::Sprite(int layer, std::string name) : debugName(std::move(name)), layer_(layer) {}
Sprite::~Sprite() = default;

Sprite& Sprite::addChild(std::unique_ptr<Sprite> child) {
  Sprite& ref = *child;
  ref.parent_ = this;
  auto at = std::upper_bound(children_.begin(), children_.end(), ref.layer_,
                             [](int l, const std::unique_ptr<Sprite>& c) { return l < c->layer_; });
  children_.insert(at, std::move(child));
  return ref;
}

std::unique_ptr<Sprite> Sprite::release(Sprite& child) {
  auto it = std::find_if(children_.begin(), children_.end(), [&](const std::unique_ptr<Sprite>& c) { return c.get() == &child; });
  if (it == children_.end()) return nullptr;
  std::unique_ptr<Sprite> out = std::move(*it);
  children_.erase(it);
  out->parent_ = nullptr;
  return out;
}

void Sprite::removeChild(Sprite& child) { release(child); }

void Sprite::setLayer(int layer) {
  if (layer == layer_) return;
  layer_ = layer;
  if (parent_) parent_->sortDirty_ = true;
}

void Sprite::resortDirty() {
  if (sortDirty_) {
    std::stable_sort(children_.begin(), children_.end(),
                     [](const std::unique_ptr<Sprite>& a, const std::unique_ptr<Sprite>& b) { return a->layer_ < b->layer_; });
    sortDirty_ = false;
  }
  for (auto& c : children_) c->resortDirty();
}

const gfx::SheetFrame* Sprite::currentFrame() const {
  if (!sheet || sheet->frames.empty() || frame < 0) return nullptr;  // frame -1 draws nothing
  const int i = std::clamp(frame, 0, static_cast<int>(sheet->frames.size()) - 1);
  return &sheet->frames[static_cast<std::size_t>(i)];
}

gfx::RectF Sprite::imageSize() const {
  if (const gfx::SheetFrame* f = currentFrame()) return {0, 0, static_cast<float>(f->src.w), static_cast<float>(f->src.h)};
  if (texture) return {0, 0, static_cast<float>(texture->width()), static_cast<float>(texture->height())};
  return {};
}

gfx::PointF Sprite::anchor() const {
  if (const gfx::SheetFrame* f = currentFrame()) return {static_cast<float>(f->reg.x), static_cast<float>(f->reg.y)};
  if (topLeft) return {0, 0};
  const gfx::RectF s = imageSize();
  return {s.w * 0.5f, s.h * 0.5f};
}

gfx::RectF Sprite::rect() const {
  const gfx::RectF s = imageSize();
  const gfx::PointF a = anchor();
  return {pos.x - a.x * scale, pos.y - a.y * scale, s.w * scale, s.h * scale};
}

bool Sprite::hit(gfx::PointF p) const {
  if (!visible || removed) return false;
  const gfx::RectF r = rect();
  return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

namespace {
// Composites the alpha (0..1) that `s` and its subtree draw at `p`, placed by `parent` exactly as Sprite::draw does.
float alphaAtPoint(const Sprite& s, gfx::PointF p, const SpriteSpec& parent, const gfx::TextureCache& textures) {
  if (!s.visible || s.removed) return 0;
  const float c = std::cos(parent.rotationDeg * kDegToRad), sn = std::sin(parent.rotationDeg * kDegToRad);
  const float ox = s.pos.x * parent.scale, oy = s.pos.y * parent.scale;
  SpriteSpec mine;
  mine.origin = {parent.origin.x + ox * c - oy * sn, parent.origin.y + ox * sn + oy * c};
  mine.scale = parent.scale * s.scale;
  mine.rotationDeg = parent.rotationDeg + s.rotationDeg;
  mine.alpha = parent.alpha * s.alpha;
  float out = 0;  // alpha of everything composited so far ("over" blending of alpha alone)
  auto over = [&out](float a) { out = out + a * (1 - out); };
  for (const auto& ch : s.children()) over(alphaAtPoint(*ch, p, mine, textures));
  const gfx::RectF size = s.imageSize();
  const gfx::Texture* tex = s.texture.get();
  gfx::RectI src{};
  if (s.sheet && !s.sheet->frames.empty() && s.frame >= 0) {
    const auto& f = s.sheet->frames[static_cast<std::size_t>(std::min(s.frame, static_cast<int>(s.sheet->frames.size()) - 1))];
    tex = s.sheet->image.get();
    src = f.src;
  }
  if (!tex || size.w <= 0 || size.h <= 0) return out;
  const float absScale = std::fabs(mine.scale);
  if (absScale <= 0) return out;
  const gfx::PointF a = s.anchor();
  const float ax = s.hflip ? size.w - a.x : a.x;
  // Undo the rotation about the sprite's position, then the scale, to land in the (possibly flipped) image.
  const float rot = (mine.rotationDeg + (mine.scale < 0 ? 180.0f : 0.0f)) * kDegToRad;
  const float dx = p.x - mine.origin.x, dy = p.y - mine.origin.y;
  const float lx = dx * std::cos(rot) + dy * std::sin(rot), ly = -dx * std::sin(rot) + dy * std::cos(rot);
  float ix = ax + lx / absScale;
  const float iy = a.y + ly / absScale;
  if (ix < 0 || iy < 0 || ix >= size.w || iy >= size.h) return out;
  if (s.hflip) ix = size.w - ix;
  float texel = 1;  // unreadable pixels count as opaque
  if (const gfx::AlphaMask* mask = textures.alphaOf(*tex)) {
    // The mask is in pixels; a texture's logical size may differ from its pixel size.
    const float sx = tex->width() > 0 ? static_cast<float>(mask->width()) / static_cast<float>(tex->width()) : 1;
    const float sy = tex->height() > 0 ? static_cast<float>(mask->height()) / static_cast<float>(tex->height()) : 1;
    texel = static_cast<float>(mask->alphaAt(static_cast<int>(std::floor((static_cast<float>(src.x) + ix) * sx)),
                                             static_cast<int>(std::floor((static_cast<float>(src.y) + iy) * sy)))) / 255.0f;
  }
  over(texel * s.tint.a * mine.alpha);
  return out;
}
}  // namespace

bool Sprite::hitOpaque(gfx::PointF p, const gfx::TextureCache& textures, int threshold) const {
  // alphaAtPoint is the whole test: a point outside the sprite's and its children's rectangles has no pixel under it.
  return alphaAtPoint(*this, p, SpriteSpec{}, textures) * 255.0f >= static_cast<float>(threshold) - 0.5f;
}

void Sprite::draw(gfx::Renderer& r, const SpriteSpec& parent) const {
  if (!visible || removed) return;
  // Where this sprite's position lands: the parent's rotation and scale apply to the offset.
  const float c = std::cos(parent.rotationDeg * kDegToRad), s = std::sin(parent.rotationDeg * kDegToRad);
  const float ox = pos.x * parent.scale, oy = pos.y * parent.scale;
  SpriteSpec mine;
  mine.origin = {parent.origin.x + ox * c - oy * s, parent.origin.y + ox * s + oy * c};
  mine.scale = parent.scale * scale;
  mine.rotationDeg = parent.rotationDeg + rotationDeg;
  mine.alpha = parent.alpha * alpha;

  std::size_t i = 0;
  for (; i < children_.size() && children_[i]->layer_ < 0; ++i) children_[i]->draw(r, mine);
  if (texture || currentFrame()) {
    const gfx::RectF size = imageSize();
    const gfx::PointF a = anchor();
    // A negative scale is a 180 degree turn about the anchor (bali's wave1 has scale -1).
    const float absScale = std::fabs(mine.scale);
    const float turn = mine.scale < 0 ? 180.0f : 0.0f;
    // A mirrored image keeps its anchor on the sprite's position: the anchor moves to the opposite side of the image.
    const float ax = hflip ? size.w - a.x : a.x;
    const gfx::RectF dst{mine.origin.x - ax * absScale, mine.origin.y - a.y * absScale, size.w * absScale, size.h * absScale};
    gfx::RectF src{};  // the whole texture
    const gfx::Texture* tex = texture.get();
    if (const gfx::SheetFrame* f = currentFrame()) {
      tex = sheet->image.get();
      src = {static_cast<float>(f->src.x), static_cast<float>(f->src.y), static_cast<float>(f->src.w), static_cast<float>(f->src.h)};
    }
    if (tex && size.w > 0 && size.h > 0) {
      gfx::ColorF t = tint;
      t.a *= mine.alpha;
      r.drawTexture(*tex, src, dst, t, gfx::Blend::Normal, mine.rotationDeg + turn, {ax * absScale, a.y * absScale}, hflip);
    }
  }
  for (; i < children_.size(); ++i) children_[i]->draw(r, mine);
}

bool Sprite::play(const char* fn, SpriteContext& ctx, double now) {
  if (!sheet) return false;
  if (!script) script = std::make_unique<SpriteScript>(*this, ctx);
  return script->play(fn, now);
}

void Sprite::scriptPause(bool pause, int depth, double now) {
  if (script) script->setPaused(pause, now);
  if (depth == 0) return;
  for (const auto& c : children_) c->scriptPause(pause, depth - 1, now);
}

bool Sprite::scriptIsPlaying(int depth) const {
  if (script && script->playing()) return true;
  if (depth == 0) return false;
  for (const auto& c : children_)
    if (c->scriptIsPlaying(depth - 1)) return true;
  return false;
}

void Sprite::tickScripts(double now) {
  if (script) script->tick(now);
  for (std::size_t i = 0; i < children_.size(); ++i) children_[i]->tickScripts(now);
}

int Sprite::countVisible(std::string_view prefix) const {
  if (!visible || removed) return 0;
  int n = std::string_view(debugName).substr(0, prefix.size()) == prefix ? 1 : 0;
  for (const auto& c : children_) n += c->countVisible(prefix);
  return n;
}

std::string Sprite::dumpLine() const {
  std::string s = "sprite " + (debugName.empty() ? std::string("-") : debugName) + " layer=" + std::to_string(layer_) +
                  " pos=" + fmt(pos.x) + "," + fmt(pos.y) + " scale=" + fmt(scale);
  const int frames = sheet ? static_cast<int>(sheet->frames.size()) : (texture ? 1 : 0);
  s += " frame=" + std::to_string(frames ? std::clamp(frame, 0, frames - 1) : 0) + "/" + std::to_string(frames);
  s += " alpha=" + fmt(alpha) + " tint=" + fmt(tint.r) + "," + fmt(tint.g) + "," + fmt(tint.b) + "," + fmt(tint.a);
  // A texture made in memory (the Chocolatier 2 logo sign, key "$logo-sign") names itself and its size.
  if (texture && !texture->path().empty() && texture->path()[0] == '$')
    s += " tex=" + texture->path() + ":" + std::to_string(texture->width()) + "x" + std::to_string(texture->height());
  if (!visible) s += " hidden";
  if (removed) s += " removed";
  return s;
}

void Sprite::dump(std::string& out, int indent) const {
  out.append(static_cast<std::size_t>(indent) * 2, ' ');
  out += dumpLine() + "\n";
  for (const auto& c : children_) c->dump(out, indent + 1);
}

}  // namespace choco::sdk
