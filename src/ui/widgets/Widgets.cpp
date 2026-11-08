#include "ui/widgets/Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "gfx/TextureCache.h"
#include "ui/Layout.h"
#include "ui/WindowManager.h"
#include "ui/widgets/TextWidgets.h"

namespace choco::ui {

namespace {

int scaled(int v, double scale) { return static_cast<int>(std::lround(v * scale)); }

gfx::ColorF colorFrom(const std::vector<double>& v) {
  return {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]), static_cast<float>(v[3])};
}

}  // namespace

// ---- Bitmap ----

void BitmapWindow::loadTexture(const std::string& image, const std::string& mask) {
  image_ = image;
  texture_.reset();
  if (image.empty()) return;
  if (gfx::TextureCache* cache = manager().textures()) texture_ = cache->load(image, mask);
}

void BitmapWindow::initImage(const Style& style) {
  image_ = style.stringOr("image");
  scale_ = style.numberOr("scale", 1.0);
  hflip_ = style.booleanOr("hflip", false);
  vflip_ = style.booleanOr("vflip", false);
  loadTexture(image_, style.stringOr("mask"));  // TBitmapTint::Init reads `mask` next to `image` (docs/re/choc1-port-view.md)
  // TImage sets the window to the scaled texture before TWindow::Init, and the layout keeps a non-zero size, so a style
  // w/h only feeds the position maths. It sizes the window only when there is no texture.
  if (texture_) setSize(scaled(texture_->width(), scale_), scaled(texture_->height(), scale_));
}

void BitmapWindow::init(const Style& style) {
  initImage(style);
  Window::init(style);
}

void BitmapWindow::setBitmap(const std::string& image, double scale) {
  scale_ = scale;
  loadTexture(image);
  if (texture_) setSize(scaled(texture_->width(), scale_), scaled(texture_->height(), scale_));
}

void BitmapWindow::setTexture(std::shared_ptr<gfx::Texture> tex) {
  texture_ = std::move(tex);
  if (texture_) setSize(scaled(texture_->width(), scale_), scaled(texture_->height(), scale_));
}

void BitmapWindow::setScale(double scale) {
  scale_ = scale;
  if (texture_) setSize(scaled(texture_->width(), scale_), scaled(texture_->height(), scale_));
}

void BitmapWindow::draw(DrawContext& ctx) const {
  if (!texture_) return;
  gfx::ColorF tint = tint_;
  tint.a *= ctx.alpha;
  const gfx::RectF dst{ctx.origin.x, ctx.origin.y, static_cast<float>(scaled(texture_->width(), scale_)),
                       static_cast<float>(scaled(texture_->height(), scale_))};
  ctx.renderer.drawTexture(*texture_, {}, dst, tint, gfx::Blend::Normal, 0, {}, hflip_, vflip_);
}

std::string BitmapWindow::detail() const {
  return " image=\"" + image_ + "\"" + (texture_ ? "" : " (not loaded)");
}

// ---- BitmapTint ----

void BitmapTintWindow::init(const Style& style) {
  initImage(style);
  if (auto t = style.numbers("tint", 4, 1.0)) tint_ = colorFrom(*t);
  centre_ = {static_cast<float>(style.numberOr("dx", kNoCentre)), static_cast<float>(style.numberOr("dy", kNoCentre))};
  const double w = style.numberOr("w", -1), h = style.numberOr("h", -1);
  if (static_cast<int>(w) > 0 && static_cast<int>(h) > 0) setSize(static_cast<int>(w), static_cast<int>(h));
  Window::init(style);
}

// dx/dy are the image centre in window space, scaled by `scale`; an absent axis centres on half the window size. The
// window's alpha is ignored. The tint's alpha is ignored too: the shipped Lua passes Color(255,255,255,0) for ordinary
// recipe-book icons and they are plainly visible in the original, so only the RGB modulates the image.
void BitmapTintWindow::draw(DrawContext& ctx) const {
  if (!texture() ) return;
  const float sc = static_cast<float>(scale());
  const float cx = centre_.x == kNoCentre ? rect().w / 2.0f : centre_.x * sc;
  const float cy = centre_.y == kNoCentre ? rect().h / 2.0f : centre_.y * sc;
  const float w = static_cast<float>(scaled(texture()->width(), scale())), h = static_cast<float>(scaled(texture()->height(), scale()));
  const gfx::RectF dst{ctx.origin.x + cx - w / 2, ctx.origin.y + cy - h / 2, w, h};
  gfx::ColorF tint = tint_;
  tint.a = 1.0f;
  ctx.renderer.drawTexture(*texture(), {}, dst, tint, gfx::Blend::Normal, 0, {}, hflip(), vflip());
}

std::string BitmapTintWindow::detail() const {
  char buf[96];
  std::snprintf(buf, sizeof buf, " tint=%.2f,%.2f,%.2f", tint_.r, tint_.g, tint_.b);
  return BitmapWindow::detail() + buf;
}

// ---- Rectangle ----

void RectangleWindow::postChildrenInit(const Style& style) {
  Window::postChildrenInit(style);
  gfx::ColorF c{1, 1, 1, 1};
  if (auto v = style.numbers("color", 4, 1.0)) c = colorFrom(*v);
  c.a = static_cast<float>(style.numberOr("alpha", c.a));
  color_ = c;
  swallow_ = style.booleanOr("swallow", false);
  invisible_ = style.booleanOr("invisible", false);
  if (style.booleanOr("shrink", false)) {
    const int pad = static_cast<int>(style.numberOr("pad", 5));
    int left = 0, top = 0, right = 0, bottom = 0;  // the union of the children, starting from an empty rectangle at the origin
    for (const auto& child : children()) {
      if (auto* t = dynamic_cast<TextWindow*>(child.get())) {
        const gfx::RectI b = t->textBounds();
        t->setSize(b.w + pad, b.h);
      }
      const gfx::RectI& r = child->rect();
      left = std::min(left, r.x);
      top = std::min(top, r.y);
      right = std::max(right, r.x + r.w);
      bottom = std::max(bottom, r.y + r.h);
    }
    bottom += pad;
    setSize(right - left, bottom - top);
    if (parent()) parent()->setSize(right - left, bottom - top);
  }
}

void RectangleWindow::draw(DrawContext& ctx) const {
  if (invisible_) return;
  gfx::ColorF c = color_;
  c.a *= ctx.alpha;
  ctx.renderer.fillRect({ctx.origin.x, ctx.origin.y, static_cast<float>(rect().w), static_cast<float>(rect().h)}, c,
                        gfx::Blend::Normal);
}

std::string RectangleWindow::detail() const {
  char buf[96];
  std::snprintf(buf, sizeof buf, " color=%.2f,%.2f,%.2f,%.2f", color_.r, color_.g, color_.b, color_.a);
  return buf;
}

// ---- LayeredWindow ----

LayeredWindow::LayeredWindow(WindowManager& wm, int layerCount)
    : Window(wm), layers_(static_cast<std::size_t>(std::max(layerCount, 0))) {}

Window& LayeredWindow::adoptFront(std::unique_ptr<Window> child) {
  const WindowId id = child->id();
  Window& ref = Window::adoptFront(std::move(child));
  if (current_ < 0) {
    for (auto& list : layers_) list.push_back(id);
  } else if (current_ < layerCount()) {
    layers_[static_cast<std::size_t>(current_)].push_back(id);
  }
  return ref;
}

const std::vector<WindowId>& LayeredWindow::layerMembers(int n) const {
  static const std::vector<WindowId> none;
  return n >= 0 && n < layerCount() ? layers_[static_cast<std::size_t>(n)] : none;
}

void LayeredWindow::addToLayer(int n, WindowId child) {
  if (n < 0 || n >= layerCount()) return;
  Window* w = manager().window(child);
  if (!w || w->parent() != this) return;
  auto& list = layers_[static_cast<std::size_t>(n)];
  if (std::find(list.begin(), list.end(), child) == list.end()) list.push_back(child);
}

void LayeredWindow::setCurrentLayer(int n) {
  if (n == current_) return;
  current_ = n;
  for (const auto& c : children()) c->setShown(false);
  for (WindowId id : layerMembers(n < 0 ? 0 : n)) {
    // Only direct children are listed; a destroyed one resolves to null.
    Window* w = manager().window(id);
    if (w && w->parent() == this) w->setShown(true);
  }
}

// ---- Button ----

ButtonWindow::ButtonWindow(WindowManager& wm) : LayeredWindow(wm, 4) {}

int ButtonWindow::layerFor(ButtonKind kind, int state, bool on) {
  if (state == kCommit) return -1;
  if (kind == ButtonKind::Push) return state;
  switch (state) {
    case kNormal: return on ? 1 : 0;
    case kDown: return on ? 0 : 1;
    case kOver: return on ? 3 : 2;
    default: return -1;
  }
}

void ButtonWindow::init(const Style& style) {
  const int type = static_cast<int>(style.numberOr("type", 0)) & 0xf;
  kind_ = type == 1 ? ButtonKind::Toggle : type == 2 ? ButtonKind::Radio : ButtonKind::Push;
  sound_ = style.stringOr("sound");
  rolloverSound_ = style.stringOr("rolloversound");
  sendToParent_ = style.booleanOr("sendToParent", false);
  beginGroup_ = style.booleanOr("beginGroup", false);
  close_ = style.booleanOr("close", false);
  default_ = style.booleanOr("default", false);
  cancel_ = style.booleanOr("cancel", false);
  labelKey_ = style.stringOr("label");
  on_ = style.booleanOr("on", false);
  const double scale = style.numberOr("scale", 1.0);
  const bool hflip = style.booleanOr("hflip", false), vflip = style.booleanOr("vflip", false);

  const std::string maskName = style.stringOr("mask");
  if (!maskName.empty()) {
    if (gfx::TextureCache* cache = manager().textures()) mask_ = cache->loadMask(maskName);
  }

  // One image per state, each on its own layer: three for a push button, four for a toggle or radio. As in
  // TButton::Init, the "current image" starts null and changes only on a non-empty entry; every layer adopts the
  // current image, so an empty entry carries the previous image forward (one shared window, not a copy).
  const int images = kind_ == ButtonKind::Push ? 3 : 4;
  if (auto graphics = style.strings("graphics", images)) {
    WindowId current = 0;
    for (int i = 0; i < images; ++i) {
      const std::string& path = (*graphics)[static_cast<std::size_t>(i)];
      if (!path.empty()) {
        auto img = std::make_unique<BitmapWindow>(manager());
        img->setName("image" + std::to_string(i));
        img->setBitmap(path, scale);
        img->setFlips(hflip, vflip);
        setCurrentLayer(i);
        current = adoptFront(std::move(img)).id();
      } else if (current != 0) {
        addToLayer(i, current);
      }
    }
  }
  // TButton::Init names and fixes the size from the style while the rect is still empty (FUN_00421d20), builds the
  // image layers, then fits to the children (FUN_00422280), which only grows: an explicit style w/h stands (`fit`
  // and the checkbox buttons of ui/options.lua), and without one the images give the size (as Bitmap and the
  // labelled buttons, whose own FitToChildren tag then grows them around the Text, do).
  Window::init(style);
  fitToChildren(*this);
  if (auto cmd = style.function("command")) command_ = std::move(*cmd);
  setCurrentLayer(-1);
}

void ButtonWindow::postChildrenInit(const Style& style) {
  Window::postChildrenInit(style);
  mouseState_ = kNormal;
  refreshLayer();
}

void ButtonWindow::refreshLayer() {
  const int layer = layerFor(kind_, mouseState_, on_);
  if (layer >= 0) setCurrentLayer(layer);
}

bool ButtonWindow::visibleChain() const {
  for (const Window* w = this; w; w = w->parent()) {
    if (!w->shown()) return false;
    if (w->isModal()) break;
  }
  return true;
}

void ButtonWindow::setMouseState(int s) {
  const int old = mouseState_;
  mouseState_ = s;
  refreshLayer();
  if (s != old && s != kDown && old != kCommit && s == kOver && !rolloverSound_.empty() && visibleChain())
    manager().playSound(rolloverSound_, true);
}

bool ButtonWindow::hitsPoint(int x, int y) const {
  if (x < 0 || y < 0 || x >= rect().w || y >= rect().h) return false;
  if (!mask_) return true;
  return mask_->hitScaled(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, static_cast<float>(rect().w),
                          static_cast<float>(rect().h));
}

bool ButtonWindow::onMouseDown(int x, int y) {
  const bool hovering = manager().isMouseListener(id());  // a hovered button is already a listener
  if (hovering && !hitsPoint(x, y)) {
    onMouseLeave();
    return false;
  }
  manager().addMouseListener(id());
  setMouseState(kDown);
  if (!hitsPoint(x, y)) onMouseLeave();  // a masked-out press is swallowed but starts nothing
  return true;
}

bool ButtonWindow::onMouseUp(int x, int y) {
  const bool wasDown = mouseState_ == kDown;
  const bool inside = hitsPoint(x, y);
  if (!wasDown && !inside && manager().isMouseListener(id())) {  // a hovering listener that the pointer has left
    onMouseLeave();
    return false;
  }
  manager().removeMouseListener(id());
  setMouseState(inside ? kOver : kNormal);
  if (wasDown && inside) press();  // last: the command may rebuild the whole screen
  return true;
}

// TButton::OnMouseMove: a hovered button registers as a mouse listener, so a modal push or pop sends it OnMouseLeave.
bool ButtonWindow::onMouseMove(int x, int y) {
  if (!hitsPoint(x, y)) {
    onMouseLeave();
    return false;  // let the window behind see it
  }
  if (mouseState_ == kNormal) {
    setMouseState(kOver);
    manager().addMouseListener(id());
  }
  return true;
}

void ButtonWindow::onMouseLeave() {
  manager().removeMouseListener(id());
  if (mouseState_ != kNormal) setMouseState(kNormal);
}

void ButtonWindow::resetState() {
  manager().removeMouseListener(id());
  mouseState_ = kNormal;
  refreshLayer();
}

void ButtonWindow::setOn(bool on) {
  on_ = on;
  if (kind_ == ButtonKind::Radio && on) clearRadioGroup();
  refreshLayer();
}

// TRadioButtonSpider: the group starts at the last `beginGroup` button at or before this one in creation order and runs
// to the next `beginGroup` button; every button in it except this one is turned off. With no group start before this
// button nothing is cleared. (The child list is front first, so creation order is by descending index.)
void ButtonWindow::clearRadioGroup() {
  Window* p = parent();
  if (!p) return;
  const auto& kids = p->children();
  const int n = static_cast<int>(kids.size());
  auto button = [&](int i) { return dynamic_cast<ButtonWindow*>(kids[static_cast<std::size_t>(i)].get()); };
  int self = -1;
  for (int i = 0; i < n; ++i)
    if (button(i) == this) self = i;
  if (self < 0) return;
  int hi = -1;  // the group's first member: the nearest group start at or before this button
  for (int i = self; i < n; ++i) {
    if (ButtonWindow* b = button(i); b && b->beginGroup_) {
      hi = i;
      break;
    }
  }
  if (hi < 0) return;
  int lo = 0;  // the latest member: the one before the next group start
  for (int i = self - 1; i >= 0; --i) {
    if (ButtonWindow* b = button(i); b && b->beginGroup_) {
      lo = i + 1;
      break;
    }
  }
  for (int i = lo; i <= hi; ++i) {
    ButtonWindow* b = button(i);
    if (b && b != this && b->on_) {
      b->on_ = false;
      b->refreshLayer();
    }
  }
}

// TButton::Press: the command runs first; the toggle flips or the radio turns on only if the button is still attached
// afterwards. If the button's modal is no longer the top one, the button then gets OnMouseLeave.
void ButtonWindow::press() {
  if (!sound_.empty() && visibleChain()) manager().playSound(sound_, false);
  WindowManager& wm = manager();
  if (!doCommand()) {
    if (kind_ == ButtonKind::Radio) setOn(true);
    else if (kind_ == ButtonKind::Toggle) setOn(!on_);
    const Window* modal = parent();
    while (modal && !modal->isModal()) modal = modal->parent();
    if (modal == wm.topModal()) return;
  }
  onMouseLeave();
}

bool ButtonWindow::doCommand() {
  WindowManager& wm = manager();
  const WindowId self = id();
  const Window* modal = parent();
  while (modal && !modal->isModal()) modal = modal->parent();
  ScriptSink* sink = wm.scriptSink();
  if (close_ && modal) {
    wm.post({kCloseWindow, WindowManager::idString(modal->id()), modal->id()});
    if (sink) sink->setGlobalString("gReturnValue", name());
  }
  Message pressed{kButtonPress, name(), parent() ? parent()->id() : 0};
  if (sendToParent_ && wm.topModal()->parent()) pressed.target = wm.topModal()->parent()->id();
  if (command_.valid() && sink) {
    Message cmd{kCommandOnly, "LuaAction:" + name(), 0};
    sink->deliver(&cmd, &command_);  // synchronous: the script may rebuild the whole screen, deletion is deferred
  }
  const bool detached = wm.window(self) == nullptr;
  if (!detached || sendToParent_) wm.post(std::move(pressed));
  return detached;
}

// GetLabel on a button is the text of its child Text named "label" ("" without one), not the style key (TButton::GetLabel).
const std::string& ButtonWindow::label() const {
  static const std::string none;
  const Window* t = find("label");
  return t && t != this && t->hasLabel() ? t->label() : none;
}

void ButtonWindow::setLabel(std::string s) {
  if (Window* t = find("label"); t && t != this && t->hasLabel()) t->setLabel(std::move(s));
}

std::string ButtonWindow::detail() const {
  static const char* kinds[] = {"push", "toggle", "radio"};
  std::string s = std::string(" type=") + kinds[static_cast<int>(kind_)] + " state=" + std::to_string(mouseState_) +
                  " layer=" + std::to_string(currentLayer());
  if (kind_ != ButtonKind::Push) s += on_ ? " on" : " off";
  if (close_) s += " close";
  if (command_.valid()) s += " command";
  if (mask_) s += " mask";
  return s;
}

void registerBuiltinWidgets(WindowManager& wm) {
  wm.addBuiltinType("Bitmap", [](WindowManager& m) { return std::make_unique<BitmapWindow>(m); });
  wm.addBuiltinType("BitmapTint", [](WindowManager& m) { return std::make_unique<BitmapTintWindow>(m); });
  // `Window` itself: a plain window sized by x, y, w, h and fit, real for every game.
  wm.addBuiltinType("Window", [](WindowManager& m) { return std::make_unique<PlaceholderWindow>(m, "Window"); });
  wm.addBuiltinType("Rectangle", [](WindowManager& m) { return std::make_unique<RectangleWindow>(m); });
  wm.addBuiltinType("Button", [](WindowManager& m) { return std::make_unique<ButtonWindow>(m); });
}

}  // namespace choco::ui
