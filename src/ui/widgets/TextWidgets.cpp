#include "ui/widgets/TextWidgets.h"

#include <algorithm>

#include "ui/WindowManager.h"

namespace choco::ui {

std::string resolveLabel(std::string_view label, const std::function<std::string(const std::string&)>& lookup) {
  if (label.empty()) return {};
  if (label[0] == '#') return std::string(label.substr(1));
  if (!lookup || label.find_first_not_of("0123456789.") == std::string_view::npos) return std::string(label);
  return lookup(std::string(label));
}

gfx::text::TextStyle readTextStyle(const Style& style, gfx::text::FontCache* fonts) {
  gfx::text::TextStyle ts;
  const std::string face = style.tableString("font", 1).value_or("__DEFAULT");
  if (fonts) {
    ts.font = fonts->get(face);
    ts.fonts = fonts;
  }
  // The size is the line-box height as an integer: 16 when the font table gives none, and 12 when it gives 0 (TText.c
  // starts at 0x10, reads font[2] and falls back to 12 only if the result is 0).
  double size = 16;
  if (auto v = style.numbers("font", 2, 16.0)) size = (*v)[1];
  const int isize = static_cast<int>(size);
  ts.size = static_cast<float>(isize == 0 ? 12 : isize);
  ts.color = {1, 1, 1, 1};
  if (auto c = style.tableNumbers("font", 3, 4, 1.0))
    ts.color = {static_cast<float>((*c)[0]), static_cast<float>((*c)[1]), static_cast<float>((*c)[2]),
                static_cast<float>((*c)[3])};
  const int defflags = static_cast<int>(style.numberOr("defflags", 0));
  ts.flags = static_cast<int>(style.numberOr("flags", defflags));
  ts.padding = static_cast<int>(style.numberOr("padding", 0));
  return ts;
}

// ---- Text ----

void TextWindow::init(const Style& style) {
  Window::init(style);
  gfx::text::TextSystem* ts = manager().textSystem();
  style_ = readTextStyle(style, ts ? &ts->fonts : nullptr);
  style_.keepLastOverflowChar = manager().keepLastOverflowChar();
  label_ = literalLabel_ ? style.stringOr("label") : resolveLabel(style.stringOr("label"), manager().labelLookup());
  invalidate();
}

void TextWindow::setLabel(std::string s) {
  label_ = std::move(s);
  invalidate();
}

const gfx::text::TextLayout& TextWindow::layout() const {
  if (!layout_ || layoutW_ != rect().w || layoutH_ != rect().h) {
    layout_ = std::make_unique<gfx::text::TextLayout>(gfx::text::TextLayout::build(label_, style_, rect().w, rect().h));
    layoutW_ = rect().w;
    layoutH_ = rect().h;
  }
  return *layout_;
}

gfx::RectI TextWindow::measuredBounds() const { return layout().bounds(); }

void TextWindow::draw(DrawContext& ctx) const {
  if (label_.empty() || rect().w <= 0 || rect().h <= 0) return;
  if (startLine_ > 0) {
    if (gfx::text::TextSystem* ts = manager().textSystem())
      layout().drawScrolled(ctx.renderer, ts->atlas, ctx.origin, ctx.alpha, drawnStartLine(), 0);
    return;
  }
  if (gfx::text::TextSystem* ts = manager().textSystem()) layout().draw(ctx.renderer, ts->atlas, ctx.origin, ctx.alpha);
  else layout().draw(ctx.renderer, ctx.origin, ctx.alpha);
}

int TextWindow::maxStartLine() const {
  const int lineHeight = std::max(1, static_cast<int>(style_.size) + style_.padding);
  return std::max(0, layout().lineCount() - rect().h / lineHeight);
}

std::string TextWindow::detail() const {
  const gfx::text::TextLayout& l = layout();
  return " size=" + std::to_string(static_cast<int>(style_.size)) + " flags=" + std::to_string(style_.flags) +
         " lines=" + std::to_string(l.lineCount()) + " lh=" + std::to_string(l.bounds().h);
}

// ---- TightText ----

void TightTextWindow::init(const Style& style) {
  TextWindow::init(style);
  setSize(800, 600);
  pad_ = static_cast<int>(style.numberOr("pad", 3));
}

void TightTextWindow::postChildrenInit(const Style& style) {
  TextWindow::postChildrenInit(style);
  const gfx::RectI b = measuredBounds();
  setSize(b.w + pad_, b.h);
}

void registerTextWidgets(WindowManager& wm) {
  wm.addBuiltinType("Text", [](WindowManager& m) { return std::make_unique<TextWindow>(m); });
  wm.addBuiltinType("TightText", [](WindowManager& m) { return std::make_unique<TightTextWindow>(m); });
}

}  // namespace choco::ui
