#include "games/greatcc/scene/SpriteManager.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "gfx/Renderer.h"
#include "gfx/Texture.h"
#include "gfx/text/TextLayout.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

SpriteManager::~SpriteManager() {
  hover_.reset();
  pressed_.reset();
  exclusive_.clear();
  // A sprite that moves others out of this manager when it leaves (VBackground's statics) must not disturb the walk.
  const auto all = std::move(objects_);
  objects_.clear();
  for (const auto& s : all) s->setManager(nullptr);
}

void SpriteManager::addObject(std::shared_ptr<VSprite> s) {
  if (!s) return;
  s->setManager(this);
  objects_.push_back(s);
  for (const auto& e : globalEffects_) s->addEffect(e);
  recalcMouse();
}

void SpriteManager::removeObject(VSprite& s) {
  auto it = std::find_if(objects_.begin(), objects_.end(), [&](const auto& p) { return p.get() == &s; });
  if (it == objects_.end()) return;
  std::shared_ptr<VSprite> keep = *it;
  objects_.erase(it);
  keep->setManager(nullptr);
  recalcMouse();
}

void SpriteManager::removeAllObjects() {
  auto all = std::move(objects_);
  objects_.clear();
  hover_.reset();
  pressed_.reset();
  for (const auto& s : all) s->setManager(nullptr);
  recalcMouse();
}

void SpriteManager::reset() {
  removeAllObjects();
  lastMouse_ = {-10000, -10000};
  hoverMs_ = 0;
  haveLast_ = false;
  softPaused_ = false;
}

void SpriteManager::queueRemoval(VSprite& s) {
  for (const auto& r : removals_)
    if (r.get() == &s) return;
  removals_.push_back(s.shared_from_this());
}

VSprite* SpriteManager::findSpriteByName(const std::string& name) const {
  for (const auto& s : objects_)
    if (VSprite* f = s->findSpriteByName(name)) return f;
  return nullptr;
}

namespace {
bool walk(VSprite& s, const SpriteSpider& spider, bool recurse) {
  if (!spider(s)) return false;
  if (recurse) {
    const auto kids = s.children();
    for (const auto& c : kids)
      if (!walk(*c, spider, true)) return false;
  }
  return true;
}
}  // namespace

void SpriteManager::forEachSprite(const SpriteSpider& spider, bool recurse) const {
  const auto top = objects_;
  for (const auto& s : top)
    if (!walk(*s, spider, recurse)) return;
}

void SpriteManager::addGlobalEffect(const std::shared_ptr<SpriteEffect>& e) {
  if (!e || std::find(globalEffects_.begin(), globalEffects_.end(), e) != globalEffects_.end()) return;
  globalEffects_.push_back(e);
  for (const auto& s : objects_) s->addEffect(e);
}

void SpriteManager::registerEffect(const std::shared_ptr<SpriteEffect>& e) {
  if (std::find(effects_.begin(), effects_.end(), e) == effects_.end()) effects_.push_back(e);
}

void SpriteManager::updateObjects() {
  // (1) the hit test, when the mouse moved or the tree changed
  if (mouseDirty_) doHitTest(lastMouse_);
  // (2) the queued removals: top-level sprites leave the list, children leave their parent
  if (!removals_.empty()) {
    auto queued = std::move(removals_);
    removals_.clear();
    for (const auto& s : queued) {
      if (VSprite* p = s->parent()) p->removeChild(*s);
      else removeObject(*s);
    }
  }
  // (3) every top-level sprite, in list order
  const auto top = objects_;
  for (const auto& s : top) s->update(softPaused_);
  updateEffects();
  // (5) the hover timer, then the hover handler of the target
  hoverMs_ += getElapsed();
  if (hover_) {
    const std::shared_ptr<VSprite> target = hover_;
    target->onMouseHover(hoverMs_);
  }
  lastUpdate_ = now_;
  haveLast_ = true;
}

void SpriteManager::updateEffects() {
  const auto set = effects_;
  std::vector<std::shared_ptr<SpriteEffect>> keep;
  for (const auto& e : set) {
    if (e && !e->shouldDelete() && e->onUpdate(now_, softPaused_)) keep.push_back(e);
  }
  // Effects registered by this pass's callbacks are kept as well.
  for (const auto& e : effects_)
    if (std::find(set.begin(), set.end(), e) == set.end()) keep.push_back(e);
  effects_ = std::move(keep);
  globalEffects_.erase(std::remove_if(globalEffects_.begin(), globalEffects_.end(),
                                      [](const auto& e) { return !e || !e->inProgress(); }),
                       globalEffects_.end());
}

// ---- mouse ----

namespace {
gfx::PointI relativeTo(const VSprite& s, gfx::PointI p) {
  return {p.x - static_cast<int>(s.position().x), p.y - static_cast<int>(s.position().y)};
}
bool inside(const gfx::RectF& r, gfx::PointI p) {
  return static_cast<float>(p.x) >= r.x && static_cast<float>(p.x) < r.x + r.w && static_cast<float>(p.y) >= r.y &&
         static_cast<float>(p.y) < r.y + r.h;
}
}  // namespace

void SpriteManager::addExclusiveSprite(std::shared_ptr<VSprite> s) {
  if (std::find(exclusive_.begin(), exclusive_.end(), s) == exclusive_.end()) exclusive_.push_back(std::move(s));
  recalcMouse();
}

bool SpriteManager::isValidSprite(const VSprite* s) const {
  if (!s) return false;
  if (exclusive_.empty() && !lockDown_) return true;
  return std::any_of(exclusive_.begin(), exclusive_.end(), [&](const auto& e) { return e.get() == s; });
}

ui::WindowId SpriteManager::parentWindowId() const {
  if (windowName_.empty()) return 0;
  ui::ModalWindow* top = ctx_.wm.topModal();
  if (!top) return 0;
  const ui::Window* w = top->find(windowName_);
  return w ? w->id() : 0;
}

bool SpriteManager::onMouseMove(gfx::PointI p) {
  lastMouse_ = p;
  if (!pressed_) {
    mouseDirty_ = true;
    return hover_ != nullptr;
  }
  const std::shared_ptr<VSprite> s = pressed_;
  s->setHovered(s->hitTest(p, DrawSpec{}, 1, -1));
  s->doEvent(SpriteEvent::Drag, relativeTo(*s, p));
  return true;
}

bool SpriteManager::onMouseDown(gfx::PointI p) {
  if (hover_ && isValidSprite(hover_.get())) {
    const std::shared_ptr<VSprite> s = hover_;
    s->doEvent(SpriteEvent::Down, relativeTo(*s, p));
    pressed_ = hover_;
    return true;
  }
  pressed_.reset();
  return false;
}

bool SpriteManager::onMouseUp(gfx::PointI p) {
  const std::shared_ptr<VSprite> s = pressed_;
  const bool valid = s && isValidSprite(s.get());
  if (valid) s->doEvent(SpriteEvent::Up, relativeTo(*s, p));
  pressed_.reset();
  return valid;
}

bool SpriteManager::doHitTest(gfx::PointI p) {
  mouseDirty_ = false;
  const DrawSpec root;
  std::shared_ptr<VSprite> found;
  const auto top = objects_;
  for (std::size_t i = top.size(); i > 0; --i) {
    const std::shared_ptr<VSprite>& s = top[i - 1];
    if (!s->visible() || !s->mouse()) continue;
    if (!inside(s->getRect(root, -1), p)) continue;
    found = s->getChildAt(p, root, 1);
    if (isValidSprite(found.get())) break;
    found.reset();
  }
  const std::shared_ptr<VSprite> old = hover_;
  if (found == old) {
    if (old) old->doEvent(SpriteEvent::Move, relativeTo(*old, p));
  } else {
    if (old) old->doEvent(SpriteEvent::Leave, {0, 0});
    if (found) {
      found->doEvent(SpriteEvent::Enter, {0, 0});
      found->doEvent(SpriteEvent::Move, relativeTo(*found, p));
    }
    hover_ = found;
    hoverMs_ = 0;
  }
  return found != nullptr;
}

void SpriteManager::draw(gfx::Renderer& r, gfx::PointF origin, float scale) {
  usedNow_.clear();
  std::stable_sort(objects_.begin(), objects_.end(), [](const auto& a, const auto& b) {
    if (a->depth() != b->depth()) return a->depth() < b->depth();
    return a->position().y < b->position().y;
  });
  drawOrder_.clear();
  DrawSpec root;
  root.m = Mat3::translation(origin.x, origin.y);
  if (scale != 1.0f) root.m = Mat3::scaling(scale, scale) * root.m;  // the scene is scaled first, then moved to `origin`
  const auto top = objects_;
  for (const auto& s : top) {
    if (!s->visible()) continue;
    drawOrder_.push_back(s.get());
    s->doDraw(r, root);
  }
}

// ---- debug overlays ----

bool SpriteManager::onKeyDown(const std::string& key, bool ctrl) {
  if (!ctrl || key.size() != 1) return false;
  const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(key[0])));
  if (c == 't') debugFlags_ ^= kDebugAssets;
  if (c == 's') debugFlags_ ^= kDebugMouse;
  return false;
}

void SpriteManager::noteTextureUsed(const gfx::Texture& tex) {
  const TexSize size{tex.width(), tex.height()};
  usedNow_.emplace(&tex, size);
  usedEver_.emplace(&tex, size);
}

namespace {
// TexInfo::MemoryToString.
std::string memoryText(long long bytes) {
  char buf[48];
  if (bytes < 1024) std::snprintf(buf, sizeof buf, "%lld bytes", bytes);
  else if (bytes < 1024 * 1024) std::snprintf(buf, sizeof buf, "%0.2f kB", static_cast<double>(bytes) * 0.0009765625);
  else std::snprintf(buf, sizeof buf, "%0.2f MB", static_cast<double>(bytes) * 0.0009765625 * 0.0009765625);
  return buf;
}
unsigned nextPow2(int v) {
  unsigned p = 1;
  while (p < static_cast<unsigned>(std::max(v, 1))) p <<= 1;
  return p;
}
}  // namespace

// VAssetManager::GetStatus in the engine's own terms (a known difference, design 9): there is no texture ledger, so "Current VRam" is
// the bytes (w * h * 4) of the textures the sprites drew this frame, "Max VRam" those of every texture this manager has drawn, "Peak
// VRam" the largest "Current" seen and SystemRam is not tracked (0). The size lines count the used textures by their power-of-two
// width, each key printed twice as the original does.
std::string SpriteManager::assetStatus() const {
  long long now = 0, ever = 0;
  std::map<unsigned, int> sizes;
  for (const auto& [tex, size] : usedNow_) {
    now += static_cast<long long>(size.w) * size.h * 4;
    ++sizes[nextPow2(size.w)];
  }
  for (const auto& [tex, size] : usedEver_) ever += static_cast<long long>(size.w) * size.h * 4;
  peakBytes_ = std::max(peakBytes_, now);
  std::string out = "SystemRam: " + memoryText(0) + "\nCurrent VRam: " + memoryText(now) + "\nMax VRam: " + memoryText(ever) +
                    "\nPeak VRam: " + memoryText(peakBytes_) + "\n";
  for (const auto& [width, count] : sizes) out += std::to_string(width) + " x " + std::to_string(width) + " : " + std::to_string(count) + "\n";
  return out;
}

std::string SpriteManager::debugText() const {
  std::string text;
  if (debugFlags_ & kDebugAssets) text = assetStatus();
  if (debugFlags_ & kDebugMouse) {
    text = "x: " + std::to_string(lastMouse_.x) + "\ny: " + std::to_string(lastMouse_.y) + "\n";
    if (haveTrace_) text += "\nTraceSprite: " + traceSprite_;
  }
  return text;
}

void SpriteManager::drawDebug(gfx::Renderer& r, gfx::PointF origin) const {
  if (!debugFlags_) return;
  std::string text = debugText();
  if (text.empty()) return;
  gfx::text::TextSystem* ts = ctx_.wm.textSystem();
  if (!ts) return;
  for (char& c : text)
    if (c == '<') c = '(';  // the layout reads markup
  gfx::text::TextStyle style;
  style.font = ts->fonts.get("fonts/myriad_pro.mvec");
  style.fonts = &ts->fonts;
  style.size = 12;
  style.color = {1, 0, 0, 1};
  style.flags = 0;
  const gfx::text::TextLayout layout = gfx::text::TextLayout::build(text, style, 300, 400);
  const gfx::RectI b = layout.bounds();
  r.fillRect({origin.x, origin.y, static_cast<float>(b.w + 10), static_cast<float>(b.h + 10)}, {0, 0, 0, 1}, gfx::Blend::Opaque);
  layout.draw(r, ts->atlas, {origin.x + 5, origin.y + 5}, 1.0f);
}

}  // namespace choco::games::greatcc
