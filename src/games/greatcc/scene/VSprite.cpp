#include "games/greatcc/scene/VSprite.h"

#include <algorithm>
#include <cstdio>

#include "games/greatcc/scene/AnimScriptNatives.h"
#include "games/greatcc/scene/Effects.h"
#include "games/greatcc/scene/SpriteManager.h"
#include "gfx/Quad.h"
#include "gfx/TextureCache.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {
int gNextSpriteId = 1;  // SPRITE_ID_COUNT

gfx::RectF unite(const gfx::RectF& a, const gfx::RectF& b) {
  if (a.w <= 0 && a.h <= 0) return b;
  if (b.w <= 0 && b.h <= 0) return a;
  const float x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
  const float x1 = std::max(a.x + a.w, b.x + b.w), y1 = std::max(a.y + a.h, b.y + b.h);
  return {x0, y0, x1 - x0, y1 - y0};
}
}  // namespace

gfx::Blend blendOf(int mode) { return mode == 2 ? gfx::Blend::Additive : gfx::Blend::Normal; }

VSprite::VSprite(script::ScriptContext& ctx, std::string name) : ctx_(ctx), id_(gNextSpriteId++), name_(std::move(name)) {}

VSprite::~VSprite() {
  for (auto& c : children_) {
    c->parent_ = nullptr;
    c->setManager(nullptr);
  }
}

// ---- tree ----

void VSprite::addChild(std::shared_ptr<VSprite> c) {
  if (!c || c.get() == this) return;
  if (c->parent_) c->parent_->removeChild(*c);
  c->parent_ = this;
  children_.push_back(c);
  c->setManager(manager_);
  resortChildren();
  if (manager_) manager_->recalcMouse();
  for (const auto& [id, e] : effects_)
    if (e.effect->propagate()) c->addEffect(e.effect);
}

void VSprite::removeChild(VSprite& child) {
  auto it = std::find_if(children_.begin(), children_.end(), [&](const auto& p) { return p.get() == &child; });
  if (it == children_.end()) return;
  std::shared_ptr<VSprite> keep = *it;
  children_.erase(it);
  for (const auto& [id, e] : effects_)
    if (e.effect->propagate()) child.removeEffect(e.effect);
  child.parent_ = nullptr;
  child.setManager(nullptr);
  if (manager_) manager_->recalcMouse();
}

void VSprite::removeChildren() {
  while (!children_.empty()) removeChild(*children_.back());
}

VSprite* VSprite::findSpriteByName(const std::string& name) {
  if (name_ == name) return this;
  for (const auto& c : children_)
    if (VSprite* f = c->findSpriteByName(name)) return f;
  return nullptr;
}

void VSprite::setManager(SpriteManager* m) {
  manager_ = m;
  if (m)
    for (const auto& [id, e] : effects_) m->registerEffect(e.effect);
  for (const auto& c : children_) c->setManager(m);
}

void VSprite::setLayer(int l) {
  layer_ = l;
  if (parent_) parent_->resortChildren();
}

void VSprite::resortChildren() {
  std::stable_sort(children_.begin(), children_.end(), [](const auto& a, const auto& b) { return a->layer_ < b->layer_; });
}

bool VSprite::isHFlipped() const {
  for (const VSprite* s = this; s; s = s->parent_)
    if (s->hflip_) return true;
  return false;
}
bool VSprite::isVFlipped() const {
  for (const VSprite* s = this; s; s = s->parent_)
    if (s->vflip_) return true;
  return false;
}

// ---- transform ----

const Mat3& VSprite::localMatrix() const {
  if (matrixDirty_) {
    // TMat3::Scale multiplies the 2x2 part only (Multiply2x2 copies the translation across), so the translation of a rotation
    // about a point is not scaled; then the position is added to it.
    local_ = rotation_;
    for (int i = 0; i < 2; ++i) {
      local_.m[i][0] *= scale_.x;
      local_.m[i][1] *= scale_.y;
    }
    local_.m[2][0] += pos_.x;
    local_.m[2][1] += pos_.y;
    matrixDirty_ = false;
  }
  return local_;
}

// ---- image ----

void VSprite::newScript() {
  gfx::SheetCache* sheets = ctx_.wm.sheets();
  if (!sheets) return;  // no renderer (a test without one): nothing animates
  script_ = std::make_unique<sdk::SpriteScript>(*this, sdk::SpriteContext{*sheets, ctx_.rng, ctx_.log});
  addSpriteScriptNatives(*script_, ctx_, [this] { return static_cast<unsigned>(delay_); });
  if (scriptSetup_) scriptSetup_(*script_);
}

void VSprite::setSheet(std::shared_ptr<const gfx::SpriteSheet> sheet) {
  stop(false);
  if (script_) retired_.push_back(std::move(script_));
  script_.reset();
  plain_.reset();
  sheet_ = std::move(sheet);
  frame_ = 0;
  if (sheet_) newScript();
}

void VSprite::setPlainTexture(std::shared_ptr<gfx::Texture> tex) {
  stop(false);
  if (script_) retired_.push_back(std::move(script_));
  script_.reset();
  sheet_.reset();
  plain_ = std::move(tex);
  frame_ = 0;
}

void VSprite::clearImage() {
  setPlainTexture(nullptr);
}

void VSprite::setCurrentFrame(int f) {
  if (f == frame_) return;
  frame_ = f;
}

const gfx::SheetFrame* VSprite::frameInfo() const {
  if (!sheet_ || frame_ < 0 || frame_ >= static_cast<int>(sheet_->frames.size())) return nullptr;
  return &sheet_->frames[static_cast<std::size_t>(frame_)];
}

bool VSprite::anchorPoint(const std::string& name, gfx::PointF& out) const {
  const gfx::SheetFrame* f = frameInfo();
  if (!f) return false;
  for (const auto& a : f->anchors)
    if (a.name == name) {
      out = {static_cast<float>(a.pos.x), static_cast<float>(a.pos.y)};
      return true;
    }
  return false;
}

// ---- animation ----

double VSprite::clockNow() const { return manager_ ? manager_->now() : 0; }

bool VSprite::animPlaying() const { return script_ && script_->playing(); }

bool VSprite::startAnim(const std::string& fn) { return script_ && script_->play(fn.c_str(), clockNow()); }

void VSprite::play(const std::string& name, bool loop, bool playChildren, double delayMs) {
  loop_ = loop;
  const std::string fn = name.empty() ? "DoAnim" : name;
  if (fn != animName_ || !animPlaying()) {
    animName_ = fn;
    if (delayMs > 0) playDelay_ = delayMs;
    else {
      playDelay_ = 0;
      startAnim(fn);
    }
  }
  if (playChildren)
    for (std::size_t i = 0; i < children_.size(); ++i) children_[i]->play("", loop, true, 0);
}

void VSprite::stop(bool recursive) {
  playDelay_ = 0;
  loop_ = false;
  animName_.clear();
  if (script_) script_->stop();
  if (recursive)
    for (const auto& c : children_) c->stop(false);
}

void VSprite::setDelay(double ms, bool recursive) {
  delay_ = ms;
  if (script_) script_->runChunk("delay(" + std::to_string(static_cast<long long>(ms)) + ");");
  if (recursive)
    for (const auto& c : children_) c->setDelay(ms, true);
}

void VSprite::scriptRemove() {
  if (manager_) manager_->queueRemoval(*this);
}

void VSprite::scriptPause(bool pause, int depth, double now) {
  if (script_) script_->setPaused(pause, now);
  if (depth == 0) return;
  for (const auto& c : children_) c->scriptPause(pause, depth - 1, now);
}

bool VSprite::scriptIsPlaying(int depth) const {
  if (script_ && script_->playing()) return true;
  if (depth == 0) return false;
  for (const auto& c : children_)
    if (c->scriptIsPlaying(depth - 1)) return true;
  return false;
}

// ---- update ----

double VSprite::getElapsed() const {
  if (!manager_) return 0;
  const double now = manager_->now();
  if (lastUpdate_ == 0) lastUpdate_ = now;  // the first call is measured from now
  if (now <= lastUpdate_) return 0;
  const double d = now - lastUpdate_;
  return d >= kMaxElapsedMs ? 0 : d;
}

void VSprite::onAnimComplete(const std::string&) {}

void VSprite::update(bool softPaused) {
  retired_.clear();
  const double now = clockNow();
  // (a) the soft-pause bookkeeping
  if (softPaused) {
    if (softPauseStart_ == 0) softPauseStart_ = now;
  } else {
    softPauseStart_ = 0;
  }
  if (script_ && hasClock()) script_->tick(now);
  // (b) the animation state
  if (!animName_.empty()) {
    if (playDelay_ > 0) {
      playDelay_ -= getElapsed();
      if (playDelay_ <= 0) {
        playDelay_ = 0;
        startAnim(animName_);
      }
    } else if (!animPlaying()) {
      const std::string name = animName_;
      if (!loop_) {
        onAnimComplete(name);
        animName_.clear();
      } else {
        // TAnimatedSprite's Stop and Play: the function is played again directly, the name and loop flag stay
        if (script_) script_->stop();
        startAnim(name);
      }
    }
  }
  // (d) the anchor
  if (!anchor_.empty() && parent_) {
    gfx::PointF p;
    if (parent_->anchorPoint(anchor_, p)) {
      if (parent_->isHFlipped()) p.x = -p.x;
      if (parent_->isVFlipped()) p.y = -p.y;
      setPosition(p);
    }
  }
  reapEffects();
  housekeepListeners();
  // (g) the children, in list order
  for (std::size_t i = 0; i < children_.size(); ++i) {
    std::shared_ptr<VSprite> c = children_[i];
    c->update(softPaused);
  }
  lastUpdate_ = now;
}

void VSprite::reapEffects() {
  for (auto it = effects_.begin(); it != effects_.end();) {
    if (it->second.effect->shouldDelete() || it->second.count <= 0) {
      std::shared_ptr<SpriteEffect> e = it->second.effect;
      it = effects_.erase(it);
      releaseEffect(e);
    } else {
      ++it;
    }
  }
}

void VSprite::housekeepListeners() {
  for (const auto& l : listeners_)
    if (l->dead) pendingRemove_.push_back(l);
  for (const auto& r : pendingRemove_) listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), r), listeners_.end());
  pendingRemove_.clear();
  for (auto& a : pendingAdd_)
    if (std::find(listeners_.begin(), listeners_.end(), a) == listeners_.end()) listeners_.push_back(std::move(a));
  pendingAdd_.clear();
}

void VSprite::removeListener(const std::shared_ptr<SpriteListener>& l) {
  pendingAdd_.erase(std::remove(pendingAdd_.begin(), pendingAdd_.end(), l), pendingAdd_.end());
  if (std::find(pendingRemove_.begin(), pendingRemove_.end(), l) == pendingRemove_.end()) pendingRemove_.push_back(l);
}

// ---- effects ----

void VSprite::addEffect(const std::shared_ptr<SpriteEffect>& e) {
  if (!e) return;
  auto it = effects_.find(e->id());
  if (it != effects_.end()) {
    ++it->second.count;
    return;
  }
  effects_[e->id()] = {e, 1};
  e->addedToSprite(*this);
  if (manager_) manager_->registerEffect(e);
  if (e->propagate())
    for (const auto& c : children_) c->addEffect(e);
}

void VSprite::removeEffect(const std::shared_ptr<SpriteEffect>& e) {
  if (!e) return;
  auto it = effects_.find(e->id());
  if (it == effects_.end()) return;
  if (--it->second.count > 0) return;
  effects_.erase(it);
  releaseEffect(e);
}

void VSprite::releaseEffect(const std::shared_ptr<SpriteEffect>& e) {
  e->removedFromSprite(*this);
  // VSpriteEffect::RemovedFromSprite: a propagating effect leaves the children too
  if (e->propagate())
    for (std::size_t i = 0; i < children_.size(); ++i) children_[i]->removeEffect(e);
}

void VSprite::cancelEffects() {
  auto all = std::move(effects_);
  effects_.clear();
  for (auto& [id, entry] : all) releaseEffect(entry.effect);
}

std::vector<std::shared_ptr<SpriteEffect>> VSprite::effectList() const {
  std::vector<std::shared_ptr<SpriteEffect>> out;
  out.reserve(effects_.size());
  for (const auto& [id, e] : effects_) out.push_back(e.effect);
  return out;
}

void VSprite::runEffects(Hook hook, DrawEnv& env) {
  if (effects_.empty()) return;
  auto list = effectList();
  if (hook == Hook::PostRender || hook == Hook::PostDraw) std::reverse(list.begin(), list.end());
  for (const auto& e : list) {
    switch (hook) {
      case Hook::PreDraw: e->onPreDraw(*this, env); break;
      case Hook::PreRender: e->onPreRender(*this, env); break;
      case Hook::PostRender: e->onPostRender(*this, env); break;
      case Hook::PostDraw: e->onPostDraw(*this, env); break;
    }
  }
}

// ---- draw ----

DrawSpec VSprite::composeSpec(const DrawSpec& parent) const {
  DrawSpec s;
  s.m = localMatrix() * parent.m;
  s.alpha = alpha_ * parent.alpha;
  s.flip = ((hflip_ ? 1u : 0u) | (vflip_ ? 2u : 0u)) ^ (parent.flip & 3u);
  s.blend = blend_ == -1 ? parent.blend : blend_;
  return s;
}

void VSprite::doDraw(gfx::Renderer& r, const DrawSpec& parent) {
  if (!visible_) return;
  DrawEnv env{r, parent, composeSpec(parent)};
  runEffects(Hook::PreDraw, env);
  env.abs = composeSpec(parent);  // a move effect may have changed the position
  lastAbs_ = env.abs.m;
  // VSprite::DoDraw writes -1 over the blend of the spec it hands to its children: a blend mode reaches the sprite's own
  // image only, and a child without one of its own draws normally.
  DrawSpec forChildren = env.abs;
  forChildren.blend = -1;
  std::size_t i = 0;
  for (; i < children_.size() && children_[i]->layer_ < 0; ++i) children_[i]->doDraw(r, forChildren);
  if (selfVisible_) {
    runEffects(Hook::PreRender, env);
    env.abs = composeSpec(parent);  // an alpha effect changed the sprite's alpha
    drawImage(r, env.abs);
    runEffects(Hook::PostRender, env);
  }
  for (; i < children_.size(); ++i) children_[i]->doDraw(r, forChildren);
  runEffects(Hook::PostDraw, env);
}

void VSprite::drawImage(gfx::Renderer& r, const DrawSpec& spec) const {
  const gfx::Texture* tex = nullptr;
  gfx::RectF src{};  // the whole texture
  float w = 0, h = 0;
  gfx::PointF origin;
  float frameAlpha = 1;
  Mat3 m = spec.m;
  const bool hf = (spec.flip & 1u) != 0, vf = (spec.flip & 2u) != 0;
  if (sheet_) {
    const gfx::SheetFrame* f = frameInfo();
    if (!f || !sheet_->image) return;
    tex = sheet_->image.get();
    src = {static_cast<float>(f->src.x), static_cast<float>(f->src.y), static_cast<float>(f->src.w), static_cast<float>(f->src.h)};
    w = src.w;
    h = src.h;
    if (hasReg_) {
      origin = reg_;
    } else {
      origin = {static_cast<float>(f->reg.x), static_cast<float>(f->reg.y)};
      if (hf) origin.x = w - origin.x;
      if (vf) origin.y = h - origin.y;
    }
    frameAlpha = f->alpha;
    if (f->hasXform) m = Mat3::fromWords(f->xform) * spec.m;
  } else if (plain_) {
    tex = plain_.get();
    w = static_cast<float>(tex->width());
    h = static_cast<float>(tex->height());
    origin = hasReg_ ? reg_ : gfx::PointF{w / 2, h / 2};
  }
  if (!tex || w <= 0 || h <= 0) return;
  if (manager_) manager_->noteTextureUsed(*tex);
  auto quad = gfx::buildTexturedQuad(src, {-origin.x, -origin.y, w, h}, tex->width(), tex->height(),
                                     {tint_.r, tint_.g, tint_.b, spec.alpha * frameAlpha * tint_.a}, 0, {}, hf, vf);
  gfx::Vertex verts[4];
  for (int i = 0; i < 4; ++i) {
    verts[i] = quad[static_cast<std::size_t>(i)];
    verts[i].pos = m.apply(verts[i].pos);
  }
  static const int kIdx[6] = {0, 1, 2, 0, 2, 3};
  r.drawGeometry(tex, verts, kIdx, blendOf(spec.blend));
}

gfx::RectF VSprite::frameRect(const DrawSpec& spec) const {
  float w = 0, h = 0;
  gfx::PointF origin;
  const bool hf = (spec.flip & 1u) != 0, vf = (spec.flip & 2u) != 0;
  if (sheet_) {
    const gfx::SheetFrame* f = frameInfo();
    if (!f) return {};
    w = static_cast<float>(f->src.w);
    h = static_cast<float>(f->src.h);
    if (hasReg_) origin = reg_;
    else {
      origin = {static_cast<float>(f->reg.x), static_cast<float>(f->reg.y)};
      if (hf) origin.x = w - origin.x;
      if (vf) origin.y = h - origin.y;
    }
  } else if (plain_) {
    w = static_cast<float>(plain_->width());
    h = static_cast<float>(plain_->height());
    origin = hasReg_ ? reg_ : gfx::PointF{w / 2, h / 2};
  }
  if (w <= 0 || h <= 0) return {};
  const Mat3& abs = spec.m;
  const gfx::PointF c[4] = {abs.apply({-origin.x, -origin.y}), abs.apply({w - origin.x, -origin.y}),
                            abs.apply({w - origin.x, h - origin.y}), abs.apply({-origin.x, h - origin.y})};
  float x0 = c[0].x, x1 = c[0].x, y0 = c[0].y, y1 = c[0].y;
  for (const auto& p : c) {
    x0 = std::min(x0, p.x);
    x1 = std::max(x1, p.x);
    y0 = std::min(y0, p.y);
    y1 = std::max(y1, p.y);
  }
  return {x0, y0, x1 - x0, y1 - y0};
}

gfx::RectF VSprite::getRect(const DrawSpec& parent, int depth) const {
  // The sprite's own `visible` flag is not read here (callers test it); a child's is.
  const DrawSpec spec = composeSpec(parent);
  gfx::RectF r = selfVisible_ ? frameRect(spec) : gfx::RectF{};
  if (depth != 0)
    for (const auto& c : children_)
      if (c->visible_) r = unite(r, c->getRect(spec, depth < 0 ? -1 : depth - 1));
  return r;
}

// ---- mouse ----

namespace {
bool inside(const gfx::RectF& r, gfx::PointI p) {
  return static_cast<float>(p.x) >= r.x && static_cast<float>(p.x) < r.x + r.w && static_cast<float>(p.y) >= r.y &&
         static_cast<float>(p.y) < r.y + r.h;
}
}  // namespace

bool VSprite::imageHit(gfx::PointI p, const DrawSpec& spec, int threshold) const {
  const gfx::RectF r = frameRect(spec);
  if (r.w <= 0 || r.h <= 0 || !inside(r, p)) return false;
  if (threshold < 0) return true;
  // The alpha the pixel would be drawn with: the texel at the pixel's centre, times the sprite's alpha and tint.
  const gfx::Texture* tex = nullptr;
  gfx::RectF src{};
  float w = 0, h = 0;
  gfx::PointF origin;
  const bool hf = (spec.flip & 1u) != 0, vf = (spec.flip & 2u) != 0;
  if (sheet_) {
    const gfx::SheetFrame* f = frameInfo();
    if (!f || !sheet_->image) return false;
    tex = sheet_->image.get();
    src = {static_cast<float>(f->src.x), static_cast<float>(f->src.y), static_cast<float>(f->src.w), static_cast<float>(f->src.h)};
    w = src.w;
    h = src.h;
    if (hasReg_) {
      origin = reg_;
    } else {
      origin = {static_cast<float>(f->reg.x), static_cast<float>(f->reg.y)};
      if (hf) origin.x = w - origin.x;
      if (vf) origin.y = h - origin.y;
    }
  } else if (plain_) {
    tex = plain_.get();
    w = static_cast<float>(tex->width());
    h = static_cast<float>(tex->height());
    origin = hasReg_ ? reg_ : gfx::PointF{w / 2, h / 2};
  } else {
    return false;
  }
  const gfx::TextureCache* cache = context().wm.textures();
  const gfx::AlphaMask* mask = cache ? cache->alphaOf(*tex) : nullptr;
  if (!mask) return true;  // unreadable pixels count as opaque
  gfx::PointF local;
  if (!spec.m.applyInverse({static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) + 0.5f}, local)) return false;
  float u = local.x + origin.x, v = local.y + origin.y;
  if (u < 0 || v < 0 || u >= w || v >= h) return false;
  if (hf) u = w - u;
  if (vf) v = h - v;
  const float sx = tex->width() > 0 ? static_cast<float>(mask->width()) / static_cast<float>(tex->width()) : 1;
  const float sy = tex->height() > 0 ? static_cast<float>(mask->height()) / static_cast<float>(tex->height()) : 1;
  const int texel = mask->alphaAt(static_cast<int>(std::floor((src.x + u) * sx)), static_cast<int>(std::floor((src.y + v) * sy)));
  const float a = static_cast<float>(texel) * spec.alpha * tint_.a;
  return a >= static_cast<float>(threshold) - 0.5f;
}

bool VSprite::hitTest(gfx::PointI p, const DrawSpec& parent, int threshold, int depth) const {
  if (!visible_ || !(mouse_ || leaf_)) return false;
  const DrawSpec spec = composeSpec(parent);
  if (selfVisible_ && imageHit(p, spec, threshold)) return true;
  if (depth == 0) return false;
  if (!inside(getRect(parent, depth), p)) return false;
  const auto kids = children_;
  for (std::size_t i = kids.size(); i > 0; --i)
    if (kids[i - 1]->hitTest(p, spec, threshold, depth - 1)) return true;
  return false;
}

std::shared_ptr<VSprite> VSprite::getChildAt(gfx::PointI p, const DrawSpec& parent, int threshold) {
  std::shared_ptr<VSprite> self = shared_from_this();
  const DrawSpec spec = composeSpec(parent);
  const auto kids = children_;
  std::shared_ptr<VSprite> found;
  std::size_t i = kids.size();
  // the children at layer >= 0, back to front (the walk stops at the first child below layer 0)
  for (; i > 0 && !found; --i) {
    const auto& c = kids[i - 1];
    if (c->layer_ < 0) break;
    if (c->visible_) found = c->getChildAt(p, spec, threshold);
  }
  if (!found && selfVisible_ && hitTest(p, parent, threshold, 0)) return self;
  // then the children below layer 0, still back to front
  for (; i > 0 && !found; --i) {
    const auto& c = kids[i - 1];
    if (c->visible_) found = c->getChildAt(p, spec, threshold);
  }
  if (!found) return nullptr;
  if (manager_) manager_->setTraceSprite(name_);  // the debug overlay's TraceSprite (the outermost sprite with a hit descendant)
  return found->leaf_ ? self : found;
}

void VSprite::doEvent(SpriteEvent ev, gfx::PointI p) {
  const std::shared_ptr<VSprite> self = shared_from_this();
  switch (ev) {
    case SpriteEvent::Enter:
      onMouseEnter();
      hovered_ = true;
      break;
    case SpriteEvent::Move: onMouseMove(p); break;
    case SpriteEvent::Down:
      onMouseDown(p);
      pressed_ = true;
      break;
    case SpriteEvent::Up:
      onMouseUp(p);
      pressed_ = false;
      if (clickable_) doEvent(SpriteEvent::Click, p);
      break;
    case SpriteEvent::Click: onClick(); break;
    case SpriteEvent::Leave:
      onMouseLeave();
      hovered_ = false;
      break;
    case SpriteEvent::Drag: onMouseDrag(p); break;
  }
  // The listeners in the active set; additions and removals made meanwhile wait for the next update.
  const auto active = listeners_;
  for (const auto& l : active) {
    if (l->dead) continue;
    switch (ev) {
      case SpriteEvent::Down: l->onMouseDown(*this, p); break;
      case SpriteEvent::Move: l->onMouseMove(*this, p); break;
      case SpriteEvent::Up: l->onMouseUp(*this, p); break;
      case SpriteEvent::Click: l->onClick(*this); break;
      case SpriteEvent::Enter: l->onMouseEnter(*this); break;
      case SpriteEvent::Leave: l->onMouseLeave(*this); break;
      case SpriteEvent::Drag: l->onMouseDrag(*this, p); break;
    }
  }
}

void VSprite::copyTo(VSprite& dst) {
  if (dst.name_.empty()) {
    char n[16];
    std::snprintf(n, sizeof n, "(copy %02d)", copyCount_++);
    dst.name_ = name_ + n;
  }
  dst.setLayer(layer_);
  dst.depth_ = depth_;
  dst.leaf_ = leaf_;
  dst.anchor_ = anchor_;
  dst.mouse_ = mouse_;
  dst.clickable_ = clickable_;
  dst.alpha_ = alpha_;
  dst.tint_ = tint_;
  dst.blend_ = blend_;
  dst.hflip_ = hflip_;
  dst.vflip_ = vflip_;
  dst.hasReg_ = hasReg_;
  dst.reg_ = reg_;
  dst.rotation_ = rotation_;
  dst.scale_ = scale_;
  dst.pos_ = pos_;
  dst.matrixDirty_ = true;
}

}  // namespace choco::games::greatcc
