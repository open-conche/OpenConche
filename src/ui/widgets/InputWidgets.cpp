#include "ui/widgets/InputWidgets.h"

#include "gfx/SheetCache.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "gfx/TextureCache.h"
#include "ui/Layout.h"
#include "ui/ScriptSink.h"
#include "ui/Transition.h"
#include "ui/VolumeSliders.h"
#include "ui/WindowManager.h"

namespace choco::ui {

namespace {

int scaled(int v, double scale) { return static_cast<int>(std::lround(v * scale)); }

// The length in bytes of the UTF-8 character that starts with `b0` (as the original's lead-byte tests count it).
std::size_t leadLength(unsigned char b0) { return b0 < 0xc0 ? 1 : b0 < 0xe0 ? 2 : b0 < 0xf0 ? 3 : 4; }

}  // namespace

// ================================================================ TextEdit

TextEditWindow::TextEditWindow(WindowManager& wm) : TextWindow(wm) {
  setFlags(flags() | kFocusableFlag);
  literalLabel_ = true;
}

void TextEditWindow::init(const Style& style) {
  cleared_ = !style.booleanOr("clearinitial", false);  // stored inverted: false means "clear on the first key or click"
  enableWindow_ = style.stringOr("enablewindow");
  if (auto f = style.function("onkey")) onKey_ = std::move(*f);
  widthLimited_ = style.booleanOr("widthlimited", false);
  password_ = style.booleanOr("password", false);
  maxLen_ = static_cast<int>(style.numberOr("length", 10));
  ignore_ = style.stringOr("ignore");
  utf8_ = style.booleanOr("utf8", false);
  TextWindow::init(style);  // the label comes out literal
  text_ = label_;
  caret_ = text_.size();
  refreshDisplay();
}

std::string TextEditWindow::displayText() const { return password_ ? std::string(text_.size(), '*') : text_; }

void TextEditWindow::refreshDisplay() {
  const std::string shown = displayText();
  if (shown != label_) TextWindow::setLabel(shown);
}

void TextEditWindow::setText(std::string s) {
  text_ = std::move(s);
  caret_ = text_.size();
  refreshDisplay();
}

void TextEditWindow::setLabel(std::string s) { setText(std::move(s)); }

bool TextEditWindow::caretPhaseVisible(double nowMs) {
  const long t = static_cast<long>(nowMs);
  return t % 800 >= 401;
}

std::size_t TextEditWindow::charLengthAt(const std::string& s, std::size_t at) {
  return at >= s.size() ? 0 : std::min(leadLength(static_cast<unsigned char>(s[at])), s.size() - at);
}

// The start of the character before byte offset `at` (stepping back over continuation bytes).
std::size_t TextEditWindow::prevCharStart(const std::string& s, std::size_t at) {
  if (at == 0) return 0;
  std::size_t i = at - 1;
  while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xc0) == 0x80) --i;
  return i;
}

int TextEditWindow::codePoints() const {
  int n = 0;
  for (unsigned char c : text_) n += (c & 0xc0) != 0x80;
  return n;
}

void TextEditWindow::clearInitial() {
  if (cleared_) return;
  cleared_ = true;
  setText({});
}

// TCustomTextEdit::KeyHit and TTextEdit::KeyHit: whatever the key, a pending initial text is cleared first. Type 0 then
// applies the width limit and, for a non-UTF-8 field, the length limit (length - 1 characters); type 5 is a rejection.
bool TextEditWindow::keyHit(KeyHitType type, std::string_view ch) {
  (void)ch;
  clearInitial();
  switch (type) {
    case kInsert:
      if (widthLimited_ && rect().w - layout().bounds().w < 20) return false;
      if (!utf8_ && maxLen_ <= static_cast<int>(text_.size()) + 1) return false;
      return true;
    case kEdit: return true;
    case kReject: return false;
  }
  return false;
}

void TextEditWindow::testEnable(int delta) {
  if (enableWindow_.empty()) return;
  Window* modal = parent();
  while (modal && !modal->isModal()) modal = modal->parent();
  Window* target = modal ? modal->find(enableWindow_) : nullptr;
  if (!target) return;
  target->setShown(static_cast<int>(text_.size()) + delta >= 1);
}

void TextEditWindow::notifyChanged() { manager().post({kTextEditChanged, name(), 0}); }

void TextEditWindow::callOnKey() {
  if (!onKey_.valid()) return;
  if (ScriptSink* sink = manager().scriptSink()) sink->runInScript(onKey_);
}

bool TextEditWindow::typeCharacter(std::string_view ch) {
  if (ch.empty()) return false;
  const unsigned char b0 = static_cast<unsigned char>(ch[0]);
  bool ok = false;
  if (ch == "\b") {
    if (caret_ == 0) {
      keyHit(kReject, ch);
      return false;
    }
    if (!keyHit(kEdit, ch)) return false;
    testEnable(-1);
    const std::size_t from = prevCharStart(text_, caret_);
    text_.erase(from, caret_ - from);
    caret_ = from;
    refreshDisplay();
    ok = true;
  } else {
    if (b0 < 0x20) return false;                           // control characters
    if (b0 >= 0x80 && !utf8_ && !password_) return false;  // non-ASCII needs utf8 or password
    if (ignore_.find(ch) != std::string::npos) {
      ok = true;  // swallowed: handled, nothing inserted
    } else {
      if (utf8_ && codePoints() >= maxLen_) {  // a UTF-8 field holds `length` characters
        keyHit(kReject, ch);
        return false;
      }
      if (!keyHit(kInsert, ch)) return false;
      soundSeed_ = soundSeed_ * 1103515245u + 12345u;
      const std::string sound = manager().typingSoundFile(soundSeed_ >> 16);
      if (!sound.empty()) manager().playSound(sound, false);
      testEnable(1);
      text_.insert(caret_, std::string(ch));
      caret_ += ch.size();
      refreshDisplay();
      ok = true;
    }
  }
  if (ok) notifyChanged();
  return ok;
}

bool TextEditWindow::typeText(std::string_view s) {
  bool any = false;
  for (std::size_t i = 0; i < s.size();) {
    const std::size_t n = std::min(leadLength(static_cast<unsigned char>(s[i])), s.size() - i);
    any |= typeCharacter(s.substr(i, n));
    i += n;
  }
  return any;
}

bool TextEditWindow::editKey(const std::string& k) {
  if (k == "Left") {
    if (caret_ == 0) {
      keyHit(kReject, {});
      return true;
    }
    if (keyHit(kEdit, {})) caret_ = prevCharStart(text_, caret_);
    return true;
  }
  if (k == "Right") {
    if (caret_ >= text_.size()) {
      keyHit(kReject, {});
      return true;
    }
    if (keyHit(kEdit, {})) caret_ += charLengthAt(text_, caret_);
    return true;
  }
  if (k == "Delete") {
    if (caret_ < text_.size()) {
      text_.erase(caret_, charLengthAt(text_, caret_));
      refreshDisplay();
    } else {
      keyHit(kReject, {});
    }
    return true;
  }
  return false;
}

bool TextEditWindow::onKeyDown(const KeyEvent& ev) {
  if (ev.name == "Backspace") {
    typeCharacter("\b");
    callOnKey();
    return true;
  }
  if (editKey(ev.name)) return true;
  // Printable keys are consumed so no hotkey fires while typing; the character itself arrives as text.
  if (!ev.ctrl && !ev.alt && (ev.name.size() == 1 || ev.name == "Space")) return true;
  return false;
}

bool TextEditWindow::onText(const std::string& utf8) {
  // onkey runs once per character (the original calls it after each OnChar), not once per text event.
  for (std::size_t i = 0; i < utf8.size();) {
    const std::size_t n = std::min(leadLength(static_cast<unsigned char>(utf8[i])), utf8.size() - i);
    typeCharacter(std::string_view(utf8).substr(i, n));
    callOnKey();
    i += n;
  }
  return !utf8.empty();
}

// TCustomTextEdit::OnChar then TTextEdit::OnChar: Enter and Tab clear a pending placeholder and run onkey; none of the
// three is consumed, so the key goes on to the modal's default and cancel handling.
bool TextEditWindow::onChar(const KeyEvent& ev) {
  if (ev.name == "Return" || ev.name == "Keypad Enter" || ev.name == "Tab") {
    clearInitial();
    callOnKey();
  } else if (ev.name == "Escape") {
    callOnKey();
  }
  return false;
}

bool TextEditWindow::onMouseUp(int x, int y) {
  (void)x, (void)y;
  if (cleared_) return false;
  clearInitial();
  return true;
}

void TextEditWindow::onSetFocus(WindowId) {}

std::size_t TextEditWindow::glyphIndexOfCaret() const {
  if (password_) return caret_;
  std::size_t n = 0;
  for (std::size_t i = 0; i < caret_ && i < text_.size(); ++i) n += (static_cast<unsigned char>(text_[i]) & 0xc0) != 0x80;
  return n;
}

void TextEditWindow::draw(DrawContext& ctx) const {
  const_cast<TextEditWindow*>(this)->refreshDisplay();
  TextWindow::draw(ctx);
  if (manager().focus() != id() || !caretPhaseVisible(manager().nowMs())) return;
  // The caret is a 1 pixel bar at the pen position, as tall as the line (the original's <cursor> tag).
  gfx::text::TextLayout::CaretPos c;
  const bool laidOut = !label_.empty() && rect().w > 0 && rect().h > 0 && layout().caretAt(glyphIndexOfCaret(), c);
  if (!laidOut) {
    const int halign = style_.flags & 3, valign = (style_.flags >> 2) & 3;
    const float size = style_.size;
    c.x = halign == 1 ? rect().w / 2.0f : halign == 2 ? static_cast<float>(rect().w) : 0.0f;
    c.top = valign == 1 ? (rect().h - size) / 2.0f : valign == 2 ? rect().h - size : 0.0f;
    c.height = size;
  }
  gfx::ColorF col = style_.color;
  col.a *= ctx.alpha;
  ctx.renderer.fillRect({ctx.origin.x + std::floor(c.x), ctx.origin.y + std::floor(c.top), 1.0f, c.height}, col,
                        gfx::Blend::Normal);
}

std::string TextEditWindow::detail() const {
  std::string s = TextWindow::detail() + " text=\"" + text_ + "\" caret=" + std::to_string(caret_) +
                  " length=" + std::to_string(maxLen_);
  if (utf8_) s += " utf8";
  if (password_) s += " password";
  if (!cleared_) s += " clearinitial";
  if (manager().focus() == id()) s += " focused";
  return s;
}

// ================================================================ Slider

void SliderWindow::init(const Style& style) {
  Window::init(style);
  const double w = style.numberOr("w", 0), h = style.numberOr("h", 0);
  const double hs = style.numberOr("hslider", -1);
  if (hs < 0) horizontal_ = static_cast<int>(h) < static_cast<int>(w);
  else horizontal_ = style.booleanOr("hslider", false);
  auto load = [&](const char* key) -> std::shared_ptr<gfx::Texture> {
    const std::string name = style.stringOr(key);
    gfx::TextureCache* cache = manager().textures();
    return cache && !name.empty() ? cache->load(name) : nullptr;
  };
  railTop_ = load("railtop");
  railMid_ = load("railmid");
  railBot_ = load("railbot");
  knob_ = load("sliderimage");
  knobOver_ = load("sliderrollimage");
  scale_ = style.numberOr("scale", 1.0);
  yoffset_ = static_cast<int>(style.numberOr("yoffset", 0));
  if (knob_) knobW_ = scaled(knob_->width(), scale_), knobH_ = scaled(knob_->height(), scale_);
  layoutSize();
  setValue(static_cast<float>(style.numberOr("value", 0)), true);
}

// SetSliderSize: across the rail the window is as thick as the largest rail image (times scale), at least the knob's
// thickness plus |yoffset|; along it the window keeps the size the style gave.
void SliderWindow::layoutSize() {
  int thick = 0;
  for (const auto& t : {railTop_, railMid_, railBot_})
    if (t) thick = std::max(thick, scaled(t->width(), scale_));
  if (knob_) thick = std::max(thick, (horizontal_ ? knobH_ : knobW_) + std::abs(yoffset_));
  if (horizontal_) setSize(rect().w, thick);
  else setSize(thick, rect().h);
}

gfx::RectI SliderWindow::knobAt(float v) const {
  if (horizontal_) return {static_cast<int>((rect().w - knobW_) * v), yoffset_, knobW_, knobH_};
  return {yoffset_, static_cast<int>((rect().h - knobH_) * v), knobW_, knobH_};
}

gfx::RectI SliderWindow::knobRect() const { return knobAt(value_); }

void SliderWindow::setValue(float v, bool quiet) {
  value_ = std::isnan(v) ? 1.0f : std::clamp(v, 0.0f, 1.0f);
  if (!quiet && knob_) send(kSliderValChanged);  // the original sends only when it has a knob image
}

void SliderWindow::send(int type) {
  if (Window* p = parent()) manager().sendMessage({type, name(), p->id()});
}

bool SliderWindow::onMouseDown(int x, int y) {
  if (!knob_) return false;
  const gfx::RectI k = knobRect();
  if (x >= k.x && x < k.x + k.w && y >= k.y && y < k.y + k.h) {
    dragging_ = true;
    manager().addMouseListener(id());
    return true;
  }
  const bool before = horizontal_ ? x < k.x : y < k.y;  // beside the knob: a page up (before it) or page down message
  send(before ? kSliderPageUp : kSliderPageDown);
  return true;
}

bool SliderWindow::onMouseUp(int x, int y) {
  if (!dragging_) return false;
  dragging_ = false;
  const gfx::RectI k = knobRect();
  hover_ = x >= k.x && x < k.x + k.w && y >= k.y && y < k.y + k.h;
  if (x < 0 || y < 0 || x >= rect().w || y >= rect().h) manager().removeMouseListener(id());
  send(kSliderMouseUp);
  return true;
}

bool SliderWindow::onMouseMove(int x, int y) {
  const gfx::RectI k = knobRect();
  hover_ = x >= k.x && x < k.x + k.w && y >= k.y && y < k.y + k.h;
  const bool inside = x >= 0 && y >= 0 && x < rect().w && y < rect().h;
  if (inside || dragging_) manager().addMouseListener(id());
  else manager().removeMouseListener(id());
  if (!dragging_) return false;
  const float span = static_cast<float>(horizontal_ ? rect().w : rect().h);
  setValue(span > 0 ? static_cast<float>(horizontal_ ? x : y) / span : 0.0f, false);
  return true;
}

void SliderWindow::onMouseLeave() {
  if (dragging_) return;
  hover_ = false;
  manager().removeMouseListener(id());
}

void SliderWindow::draw(DrawContext& ctx) const {
  gfx::Renderer& r = ctx.renderer;
  const gfx::ColorF white{1, 1, 1, ctx.alpha};
  const int W = rect().w, H = rect().h;
  // One rail piece: `tex` is a vertical image (cap at the top). Vertical sliders draw it as is; horizontal ones turn it a
  // quarter turn so the top cap sits at the left. `at` and `len` are the piece's start and length along the rail.
  auto piece = [&](const std::shared_ptr<gfx::Texture>& tex, int at, int len) {
    if (!tex || len <= 0) return;
    const int thick = scaled(tex->width(), scale_);
    if (horizontal_) {
      const float cy = static_cast<float>((H - thick) / 2) + thick / 2.0f;
      const gfx::RectF dst{ctx.origin.x + at + len / 2.0f - thick / 2.0f, ctx.origin.y + cy - len / 2.0f,
                           static_cast<float>(thick), static_cast<float>(len)};
      // FUN_0043bdc0 maps the image's x axis (the thickness) to +y and its y axis to +x: the image's left edge ends up
      // at the TOP of the piece. The rotation alone puts it at the bottom (image x inverted), so flip the source
      // horizontally first: without it the rail's art sits 12 of 24 pixels higher than the original's, over the text
      // of an options-dialog label.
      r.drawTexture(*tex, {}, dst, white, gfx::Blend::Normal, -90.0f, {thick / 2.0f, len / 2.0f}, true, false);
    } else {
      const gfx::RectF dst{ctx.origin.x + static_cast<float>((W - thick) / 2), ctx.origin.y + at,
                           static_cast<float>(thick), static_cast<float>(len)};
      r.drawTexture(*tex, {}, dst, white, gfx::Blend::Normal);
    }
  };
  const int total = horizontal_ ? W : H;
  const int topLen = railTop_ ? scaled(railTop_->height(), scale_) : 0;
  const int botLen = railBot_ ? scaled(railBot_->height(), scale_) : 0;
  piece(railTop_, 0, topLen);
  piece(railMid_, topLen, total - topLen - botLen);
  piece(railBot_, total - botLen, botLen);
  const auto& img = hover_ || dragging_ ? (knobOver_ ? knobOver_ : knob_) : knob_;
  if (!img) return;
  const gfx::RectI k = knobRect();
  r.drawTexture(*img, {}, {ctx.origin.x + k.x, ctx.origin.y + k.y, static_cast<float>(k.w), static_cast<float>(k.h)}, white,
                gfx::Blend::Normal);
}

std::string SliderWindow::detail() const {
  char buf[96];
  const gfx::RectI k = knobRect();
  std::snprintf(buf, sizeof buf, " value=%.3f %s knob=%d,%d,%d,%d%s", value_, horizontal_ ? "horizontal" : "vertical", k.x, k.y,
                k.w, k.h, knob_ ? "" : " noknob");
  return buf;
}

// ================================================================ BSGWindow

BSGWindow::BSGWindow(WindowManager& wm) : Window(wm) { setAlpha(0); }

void BSGWindow::postChildrenInit(const Style& style) {
  Window::postChildrenInit(style);
  swallow_ = style.booleanOr("swallowmouse", false);
  frame_ = style.stringOr("frame");
  if (auto c = style.numbers("color", 4, 1.0)) {
    color_ = {static_cast<float>((*c)[0]), static_cast<float>((*c)[1]), static_cast<float>((*c)[2]),
              static_cast<float>((*c)[3])};
  }
  fill_ = style.booleanOr("fill", fill_);
  if (color_.a >= 0 && color_.a <= 1) {
    // SetAlpha(a, true): the alpha goes to the window and its descendants, whatever it is (the default 0 hides the
    // children until a fade, such as the popup's, brings them back).
    setAlpha(color_.a);
    for (const auto& c : children()) setSubtreeAlpha(*c, color_.a);
  }
  if (!frame_.empty()) {
    if (gfx::SheetCache* sheets = manager().sheets()) {
      frameSheet_ = sheets->get(frame_.find(".xml") == std::string::npos ? frame_ + ".xml" : frame_);
      if (!frameSheet_ && manager().firstPlaceholderOf("BSGWindow.frame:" + frame_))
        manager().log("[frame] '" + frame_ + "' is not a sprite sheet; the border is not drawn");
    }
  }
}

// DrawFrame: pieces are placed by their registration points, 4 px outside the rectangle for controls/rollover. Top and
// bottom tile horizontally clipped to [L,R], left and right vertically clipped to [T,B], then the four corners.
void BSGWindow::drawOutside(DrawContext& ctx) const {
  const gfx::SpriteSheet& sh = *frameSheet_;
  if (sh.frames.size() < 8 || !sh.image || ctx.alpha <= 0) return;
  gfx::Renderer& r = ctx.renderer;
  const gfx::ColorF tint{1, 1, 1, ctx.alpha};
  const int L = static_cast<int>(ctx.origin.x), T = static_cast<int>(ctx.origin.y);
  const int R = L + rect().w, B = T + rect().h;
  constexpr int kBig = 1 << 20;
  auto piece = [&](int k, int x, int y) {
    const gfx::SheetFrame& f = sh.frames[static_cast<std::size_t>(k)];
    r.drawTexture(*sh.image, {static_cast<float>(f.src.x), static_cast<float>(f.src.y), static_cast<float>(f.src.w), static_cast<float>(f.src.h)},
                  {static_cast<float>(x - f.reg.x), static_cast<float>(y - f.reg.y), static_cast<float>(f.src.w), static_cast<float>(f.src.h)}, tint,
                  gfx::Blend::Normal);
  };
  r.pushClip({std::max(L, 0), -kBig / 2, std::max(R, 0) - std::max(L, 0), kBig});
  for (int x = L, w = std::max(1, sh.frames[6].src.w); x < R; x += w) piece(6, x, T);
  for (int x = L, w = std::max(1, sh.frames[7].src.w); x < R; x += w) piece(7, x, B);
  r.popClip();
  r.pushClip({-kBig / 2, std::max(T, 0), kBig, std::max(B, 0) - std::max(T, 0)});
  for (int y = T, h = std::max(1, sh.frames[4].src.h); y < B; y += h) piece(4, L, y);
  for (int y = T, h = std::max(1, sh.frames[5].src.h); y < B; y += h) piece(5, R, y);
  r.popClip();
  piece(0, L, T);
  piece(1, R, T);
  piece(2, L, B);
  piece(3, R, B);
  if (sh.frames.size() > 8) piece(8, (L + R) / 2, T);
}

void BSGWindow::draw(DrawContext& ctx) const {
  if (!fill_) return;
  gfx::ColorF c = color_;
  c.a = ctx.alpha;
  if (c.a <= 0) return;
  ctx.renderer.fillRect({ctx.origin.x, ctx.origin.y, static_cast<float>(rect().w), static_cast<float>(rect().h)}, c,
                        gfx::Blend::Normal);
}

std::string BSGWindow::detail() const {
  char buf[96];
  std::snprintf(buf, sizeof buf, " color=%.2f,%.2f,%.2f,%.2f%s%s", color_.r, color_.g, color_.b, color_.a,
                swallow_ ? " swallowmouse" : "", fill_ ? "" : " nofill");
  return buf;
}

// ================================================================ Popup and Rollover

PopupWindow::PopupWindow(WindowManager& wm, WindowId rollover, gfx::RectI keepVisible)
    : BSGWindow(wm), rollover_(rollover), keep_(keepVisible) {
  fill_ = false;  // PopupWindow turns the fill off; only its children show
}

std::pair<int, int> PopupWindow::reposition(int x, int y, int w, int h, int screenW, int screenH, int margin) {
  if (x + w > screenW - margin) {
    x -= x + w - (screenW - margin);
    y += 32;
  }
  if (y + h > screenH - margin) y -= y + h - (screenH - margin);
  return {x, y};
}

void PopupWindow::kill() {
  manager().removeMouseListener(id());
  if (!isDestroyed()) manager().destroyWindow(*this);
}

bool PopupWindow::onMouseMove(int x, int y) {
  const gfx::PointF o = WindowManager::screenOrigin(*this);
  const int sx = static_cast<int>(o.x) + x, sy = static_cast<int>(o.y) + y;
  if (sx >= keep_.x && sx < keep_.x + keep_.w && sy >= keep_.y && sy < keep_.y + keep_.h) {
    auto* owner = dynamic_cast<PopupClickTarget*>(manager().window(rollover_));
    if (!owner || owner->keepsPopup(sx, sy)) return true;
  }
  kill();
  return false;
}

bool PopupWindow::onMouseDown(int, int) {
  pressed_ = true;
  return true;
}

// A release always closes the popup; the Rollover's command runs only when the press began on the popup.
bool PopupWindow::onMouseUp(int, int) {
  const WindowId rollover = rollover_;
  const bool pressed = pressed_;
  WindowManager& wm = manager();
  kill();
  if (pressed) {
    Window* owner = wm.window(rollover);
    if (auto* r = dynamic_cast<RolloverWindow*>(owner)) r->click();
    else if (auto* t = dynamic_cast<PopupClickTarget*>(owner)) t->popupClicked();
  }
  return true;
}

void RolloverWindow::init(const Style& style) {
  Window::init(style);
  const bool legacy = manager().legacyRollover();
  xOffset_ = static_cast<int>(style.numberOr("xOffset", legacy ? 20 : 0));  // TRollover::Init: the default is 20
  yOffset_ = static_cast<int>(style.numberOr("yOffset", 0));
  xAbsolute_ = static_cast<int>(style.numberOr("xAbsolute", -1));
  yAbsolute_ = static_cast<int>(style.numberOr("yAbsolute", -1));
  contents_ = style.stringOr("contents");
  if (auto f = style.function("command")) command_ = std::move(*f);
  if (legacy) {
    rolloverSound_ = manager().resolveSoundName(style.stringOr("rolloverSound"));
    for (int i = 1; i < 256; ++i) {  // entries until the first missing one, like the original's GetTable(i) loop
      auto r = style.tableNumbers("area", i, 4, 0.0);
      if (!r) break;
      area_.push_back({static_cast<int>((*r)[0]), static_cast<int>((*r)[1]), static_cast<int>((*r)[2]), static_cast<int>((*r)[3])});
    }
  }
}

bool RolloverWindow::inArea(const std::vector<gfx::RectI>& area, int x, int y) {
  if (area.empty()) return true;
  for (const gfx::RectI& r : area)
    if (x >= r.x && x < r.w && y >= r.y && y < r.h) return true;
  return false;
}

void RolloverWindow::postChildrenInit(const Style& style) {
  Window::postChildrenInit(style);
  fitToChildren(*this);  // it fits whatever `fit` says
}

void RolloverWindow::click() {
  ScriptSink* sink = manager().scriptSink();
  if (!sink) return;
  if (command_.valid()) sink->runInScript(command_);
  else if (!clickString_.empty()) sink->runString(clickString_);
}

std::unique_ptr<RolloverWindow> RolloverWindow::makeNative(WindowManager& wm, const gfx::RectI& rect, std::string contents,
                                                           std::string clickString, int popupX, int popupY) {
  auto r = std::make_unique<RolloverWindow>(wm);
  r->setRect(rect);
  r->contents_ = std::move(contents);
  r->clickString_ = std::move(clickString);
  r->xAbsolute_ = popupX;
  r->yAbsolute_ = popupY;
  return r;
}

WindowId openContentsPopup(WindowManager& wm, WindowId owner, const gfx::RectI& keep, const std::string& contents, int ax, int ay) {
  return openPopup(wm, std::make_unique<PopupWindow>(wm, owner, keep), contents, ax, ay, PopupOptions{});
}

WindowId openPopup(WindowManager& wm, std::unique_ptr<PopupWindow> popupOwned, const std::string& contents, int ax, int ay,
                   const PopupOptions& options) {
  ScriptSink* sink = wm.scriptSink();
  if (!sink) return 0;
  Window& modal = *wm.topModal();
  PopupWindow& popup = static_cast<PopupWindow&>(modal.adoptFront(std::move(popupOwned)));
  const WindowId popupId = popup.id();
  const gfx::PointF mo = WindowManager::screenOrigin(modal);

  // SetContents: the popup is the window being built while `contents` runs (injected into Main like any command).
  const std::size_t depth = wm.creationDepth();
  wm.pushCreation(popup);
  sink->runString(contents);
  while (wm.creationDepth() > depth) wm.popCreation();  // even when contents errored or destroyed the popup
  if (Window* p = wm.window(popupId)) {
    fitToChildren(*p);
    if (options.instant) {
      setSubtreeAlpha(*p, 1, 1.0f);  // fully visible at once (a BSGWindow starts at its colour's alpha, 0)
    } else {
      Transition tr;
      if (wm.legacyPopupFader()) {
        // TPopupWindow::TFader::Process (C2 0x264b4): 125 ms in 50 ms steps (0.4, 0.8, 1), Rectangles end at 0.8 whatever their Lua alpha.
        tr.rectAlphaScale = 0.8f;
        tr.stepMs = 50;
      }
      setSubtreeAlpha(*p, 0, tr.rectAlphaScale);  // SetContents runs the fader at fade 0
      tr.kind = Transition::Kind::FadeIn;
      tr.target = popupId;
      tr.startMs = wm.nowMs();
      tr.durationMs = RolloverWindow::kFadeMs;
      tr.alpha = 1;
      wm.startTransition(std::move(tr));
    }

    const gfx::RectI& scr = wm.screen()->rect();
    const auto [px, py] = PopupWindow::reposition(ax, ay, p->rect().w, p->rect().h, scr.w, scr.h, options.margin);
    p->setPos(px - static_cast<int>(mo.x), py - static_cast<int>(mo.y));
    wm.addMouseListener(popupId);
  }
  return popupId;
}

bool RolloverWindow::onMouseMove(int x, int y) {
  WindowManager& wm = manager();
  if (!wm.scriptSink()) return true;
  if (!inArea(area_, x, y)) return false;  // outside every area rect: not handled (TRollover::OnMouseMove returns 0)
  const gfx::PointF self = WindowManager::screenOrigin(*this);
  const gfx::RectI screenRect{static_cast<int>(self.x), static_cast<int>(self.y), rect().w, rect().h};
  // The anchor is the pointer (screen coordinates) plus the offsets, or the absolute coordinate where one is given.
  int ax = static_cast<int>(self.x) + x + xOffset_, ay = static_cast<int>(self.y) + y + yOffset_;
  if (xAbsolute_ >= 0) ax = xAbsolute_;
  if (yAbsolute_ >= 0) ay = yAbsolute_;
  openContentsPopup(wm, id(), screenRect, contents_, ax, ay);
  if (!rolloverSound_.empty()) wm.playSound(rolloverSound_, true);
  return true;
}

std::string RolloverWindow::detail() const {
  return " contents=\"" + contents_ + "\"" + (command_.valid() ? " command" : "") +
         (clickString_.empty() ? "" : " click=\"" + clickString_ + "\"");
}

// ================================================================ Options

void OptionsWindow::postChildrenInit(const Style& style) {
  Window::postChildrenInit(style);
  // The stored volumes are per mille in the current user's preferences; the defaults 750, 650 and 500 apply when unset.
  ScriptSink* sink = manager().scriptSink();
  for (const VolumeSlider& row : kVolumeSliders)
    if (auto* s = dynamic_cast<SliderWindow*>(find(row.name))) {
      const std::string def = std::to_string(row.defaultPerMille);
      const std::string v = sink ? sink->userPref(row.pref, def) : def;
      s->setValue(static_cast<float>(std::atof(v.c_str())) / 1000.0f, true);
    }
}

bool OptionsWindow::onMessage(const Message& m) {
  if (m.type != kSliderValChanged) return false;
  if (findVolumeSlider(m.name)) {
    // BSGSetSoundLevel / MusicLevel / AmbientLevel: applied live; only SaveVolumes (OK or Esc) stores them.
    if (const auto* s = dynamic_cast<const SliderWindow*>(find(m.name))) manager().previewVolume(m.name, s->value());
    return true;
  }
  return false;
}

void registerInputWidgets(WindowManager& wm) {
  wm.addBuiltinType("TextEdit", [](WindowManager& m) { return std::make_unique<TextEditWindow>(m); });
  wm.addBuiltinType("Slider", [](WindowManager& m) { return std::make_unique<SliderWindow>(m); });
  wm.addBuiltinType("BSGWindow", [](WindowManager& m) { return std::make_unique<BSGWindow>(m); });
  wm.addBuiltinType("Rollover", [](WindowManager& m) { return std::make_unique<RolloverWindow>(m); });
  wm.addBuiltinType("OptionsWindow", [](WindowManager& m) { return std::make_unique<OptionsWindow>(m); });
}

}  // namespace choco::ui
