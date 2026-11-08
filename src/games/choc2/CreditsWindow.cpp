#include "games/choc2/CreditsWindow.h"

#include <algorithm>

#include "ui/DrawContext.h"
#include "ui/WindowManager.h"
#include "ui/widgets/TextWidgets.h"

namespace choco::game::choc2 {

namespace {

std::string_view trim(std::string_view s) {
  const auto pad = [](char c) { return c == ' ' || c == '\t'; };
  while (!s.empty() && pad(s.back())) s.remove_suffix(1);
  while (!s.empty() && pad(s.front())) s.remove_prefix(1);
  return s;
}

// One cell: a leading '+' is a header, '*' a title, else a name; the prefix is dropped.
Credits::Cell makeCell(std::string_view s) {
  s = trim(s);
  Credits::Cell c;
  if (!s.empty() && (s.front() == '+' || s.front() == '*')) {
    c.kind = s.front() == '+' ? Credits::Kind::Header : Credits::Kind::Title;
    s = trim(s.substr(1));
  }
  c.text = std::string(s);
  return c;
}

gfx::ColorF colourKey(const ui::Style& style, const char* key, gfx::ColorF def) {
  if (auto v = style.numbers(key, 4, 1.0))
    return {static_cast<float>((*v)[0]), static_cast<float>((*v)[1]), static_cast<float>((*v)[2]), static_cast<float>((*v)[3])};
  return def;
}

}  // namespace

std::vector<Credits::Page> Credits::parsePages(std::string_view file) {
  std::vector<Page> pages(1);
  std::size_t pos = 0;
  while (pos < file.size()) {
    const std::size_t nl = file.find('\n', pos);
    std::string_view line = file.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
    pos = nl == std::string_view::npos ? file.size() : nl + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.starts_with("---")) {
      pages.emplace_back();
      continue;
    }
    Row row;
    for (std::size_t from = 0;;) {
      const std::size_t bar = line.find('|', from);
      row.push_back(makeCell(line.substr(from, bar == std::string_view::npos ? std::string_view::npos : bar - from)));
      if (bar == std::string_view::npos) break;
      from = bar + 1;
    }
    pages.back().push_back(std::move(row));
  }
  std::erase_if(pages, [](const Page& p) { return p.empty(); });
  return pages;
}

float Credits::pageAlpha(double clockMs, double fadeUp, double showTime, double fadeDown) {
  if (clockMs <= fadeUp) return fadeUp > 0 ? static_cast<float>(clockMs / fadeUp) : 1.0f;
  if (clockMs <= fadeUp + showTime) return 1.0f;
  const double v = 1.0 - (clockMs - fadeUp - showTime) / fadeDown;
  return static_cast<float>(std::clamp(v, 0.0, 1.0));
}

void Credits::init(const ui::Style& style) {
  Window::init(style);
  fadeUp_ = style.numberOr("fadeUp", fadeUp_);
  fadeDown_ = style.numberOr("fadeDown", fadeDown_);
  showTime_ = style.numberOr("showTime", showTime_);
  sizes_[0] = static_cast<int>(style.numberOr("sizeName", kSizeName));
  sizes_[1] = static_cast<int>(style.numberOr("sizeTitle", kSizeTitle));
  sizes_[2] = static_cast<int>(style.numberOr("sizeHeader", kSizeHeader));
  face_ = style.stringOr("font", "__DEFAULT");
  colorName_ = colourKey(style, "colorName", colorName_);
  colorHeader_ = colourKey(style, "colorHeader", colorHeader_);
  colorTitle_ = colourKey(style, "colorTitle", colorName_);  // the title colour defaults to the name colour
  // INFERENCE: the `file` default is `credits.txt` (the shipped Lua passes none).
  const std::string file = style.stringOr("file", "credits.txt");
  const std::optional<std::string> text = manager().readTextFile(file);
  if (!text) {
    manager().log("[credits] cannot open '" + file + "': the credits stay empty");
    return;
  }
  pages_ = parsePages(*text);
  buildPage();
  manager().startWindowAnimation(id(), kTickMs, true);
}

void Credits::buildPage() {
  boxes_.clear();
  alpha_ = 0;
  clockMs_ = 0;
  gfx::text::TextSystem* ts = manager().textSystem();
  if (!ts || pages_.empty()) return;
  const Page& page = pages_[page_ % pages_.size()];
  const int W = rect().w;
  int y = 0;
  for (const Row& row : page) {
    int h = sizes_[0];
    for (const Cell& c : row) h = std::max(h, sizeOf(c.kind));
    // An empty line is a blank row of the name size; a lone empty cell has no glyphs to draw.
    const int cellW = W / static_cast<int>(row.size());
    for (std::size_t i = 0; i < row.size(); ++i) {
      const Cell& c = row[i];
      if (c.text.empty()) continue;
      gfx::text::TextStyle ls;
      ls.font = ts->fonts.get(face_);
      ls.fonts = &ts->fonts;
      ls.size = static_cast<float>(sizeOf(c.kind));
      ls.color = c.kind == Kind::Header ? colorHeader_ : c.kind == Kind::Title ? colorTitle_ : colorName_;
      // INFERENCE: flag 5 is kHAlignCenter (1) + kVAlignCenter (4), the same bit values as the Lua constants.
      ls.flags = gfx::text::kHAlignCenter | gfx::text::kVAlignCenter;
      Box b;
      b.layout = std::make_unique<gfx::text::TextLayout>(gfx::text::TextLayout::build(c.text, ls, cellW, h));
      b.x = static_cast<int>(i) * cellW;
      b.y = y;
      boxes_.push_back(std::move(b));
    }
    y += h;
  }
  const int shift = (rect().h - y) / 2;  // the whole stack is centred vertically
  for (Box& b : boxes_) b.y += shift;
}

bool Credits::onAnimate() {
  if (pages_.empty()) return false;
  clockMs_ += kTickMs;
  if (clockMs_ >= fadeUp_ + showTime_ + fadeDown_) {
    page_ = (page_ + 1) % pages_.size();
    buildPage();
  } else {
    alpha_ = pageAlpha(clockMs_, fadeUp_, showTime_, fadeDown_);
  }
  return true;
}

void Credits::draw(ui::DrawContext& ctx) const {
  gfx::text::TextSystem* ts = manager().textSystem();
  if (!ts || alpha_ <= 0) return;
  for (const Box& b : boxes_)
    b.layout->draw(ctx.renderer, ts->atlas,
                   {ctx.origin.x + static_cast<float>(b.x), ctx.origin.y + static_cast<float>(b.y)}, alpha_ * ctx.alpha);
}

std::string Credits::detail() const {
  return " pages=" + std::to_string(pages_.size()) + " page=" + std::to_string(page_) +
         " clock=" + std::to_string(static_cast<int>(clockMs_));
}

}  // namespace choco::game::choc2
