#include "games/greatcc/scene/VSpriteWindow.h"

#include <cmath>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/scene/Effects.h"
#include "games/greatcc/scene/TextSprite.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {
constexpr double kTickMs = 16;  // StartWindowAnimation(0x10)

int pow10(int n) {
  int r = 1;
  for (int i = 0; i < n; ++i) r *= 10;
  return r;
}
}  // namespace

// ---- VSpriteWindow ----

VSpriteWindow::VSpriteWindow(ui::WindowManager& wm, script::ScriptContext& ctx)
    : Window(wm), ctx_(ctx), manager_(std::make_unique<SpriteManager>(ctx)) {
  setName("sprite_window");
}

void VSpriteWindow::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  manager_->setWindowName(name());
  manager_->setNow(manager().modalNow(*this));
  manager().startWindowAnimation(id(), kTickMs);
}

void VSpriteWindow::draw(ui::DrawContext& ctx) const {
  manager_->draw(ctx.renderer, ctx.origin);
  manager_->drawDebug(ctx.renderer, ctx.origin);
}

// The engine's modal push and pop leave the key focus on the modal itself; the original's modal hands it on to its default focus, which for
// the shop and the world map is the window that reads Ctrl+C / T / S. When this window is its modal's default focus and the modal is on
// top with the focus on it, the focus moves here.
void VSpriteWindow::claimKeyFocus() {
  ui::ModalWindow* modal = nullptr;
  for (ui::Window* p = parent(); p && !modal; p = p->parent()) modal = dynamic_cast<ui::ModalWindow*>(p);
  if (modal && modal->defaultFocus == id() && manager().topModal() == modal && manager().focus() == modal->id()) manager().setFocus(id());
}

bool VSpriteWindow::onAnimate() {
  claimKeyFocus();
  manager_->setNow(manager().modalNow(*this));
  manager_->updateObjects();
  return true;
}

std::string VSpriteWindow::detail() const {
  return " sprites=" + std::to_string(manager_->objects().size()) +
         (manager_->debugFlags() ? " debug=" + std::to_string(manager_->debugFlags()) : std::string());
}

// ---- VObjectWindow ----

void VObjectWindow::postChildrenInit(const ui::Style& style) {
  VSpriteWindow::postChildrenInit(style);
  auto objects = style.table("objects");
  if (!objects) return;
  objects->forEachEntry([&](const std::string&, const ui::Style* t, const std::optional<std::string>&) {
    if (!t) return;
    const StyleChain chain(*t, nullptr);
    auto obj = VObject::create(ctx_, "", "");
    obj->loadFromTable(chain);
    obj->init();
    sprites().addObject(obj);
    auto effects = t->table("effects");
    if (!effects) return;
    for (int i = 1; i <= effects->size(); ++i) {
      auto e = effects->element(i);
      if (!e) continue;
      std::string type = e->stringOr("type");
      for (char& c : type) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (type != "move") continue;
      obj->addEffect(std::make_shared<SpriteMove>(
          gfx::PointF{static_cast<float>(e->numberOr("x", 0)), static_cast<float>(e->numberOr("y", 0))},
          e->numberOr("time", 300), static_cast<float>(e->numberOr("accel", 0)), e->numberOr("delay", 0)));
    }
  });
}

std::string VObjectWindow::detail() const {
  TextLineCount n;
  for (const auto& o : sprites().objects()) {
    const TextLineCount c = countTextLines(*o);
    n.drawn += c.drawn;
    n.total += c.total;
  }
  return VSpriteWindow::detail() + (n.total > 0 ? " lines=" + std::to_string(n.drawn) + "/" + std::to_string(n.total) : std::string());
}

// ---- VRegister ----

void VRegister::postChildrenInit(const ui::Style& style) {
  numDigits_ = static_cast<int>(style.numberOr("numDigits", 0));
  auto digitStyle = style.table("digit");
  if (digitStyle && numDigits_ > 0) {
    const StyleChain chain(*digitStyle, nullptr);
    const int w = chain.intOr("w", 0);
    float x = static_cast<float>(rect().w - w);
    for (int i = 0; i < numDigits_; ++i) {
      auto d = Digit::create(ctx_, "digit_" + std::to_string(pow10(i)));
      d->loadFromTable(chain);
      d->init();
      sprites().addObject(d);
      d->setPosition({x, 0});
      x -= static_cast<float>(w);
      digits_.push_back(d);
    }
  }
  context_ = style.stringOr("context", "");
  updateDigits();
  VSpriteWindow::postChildrenInit(style);
}

void VRegister::updateDigits() {
  for (int i = 0; i < static_cast<int>(digits_.size()); ++i) {
    digits_[static_cast<std::size_t>(i)]->setVisible(i == 0 || pow10(i) <= value_);
    const int p = pow10(i), p1 = pow10(i + 1);
    digits_[static_cast<std::size_t>(i)]->setDigit((value_ % p1 - value_ % p) / p);
  }
}

int VRegister::valueNow() const {
  GreatCCWorld* w = worldOf(ctx_);
  if (!w) return 0;
  ValueManager& v = w->values();
  if (w->settings().mode != 0) return v.getInt("current_level_total", 0);
  int out = 0;
  if (context_ == "prelevel" || context_ == "endlevel") out = v.getInt("current_worth", 0);
  if (context_ != "endlevel") out += v.getInt("current_level_total", 0);
  return out;
}

bool VRegister::onAnimate() {
  const int v = valueNow();
  if (v != value_) {
    value_ = v;
    updateDigits();
  }
  return VSpriteWindow::onAnimate();
}

std::string VRegister::detail() const {
  return VSpriteWindow::detail() + " context=\"" + context_ + "\" digits=" + std::to_string(digits_.size()) +
         " value=" + std::to_string(value_);
}

}  // namespace choco::games::greatcc
