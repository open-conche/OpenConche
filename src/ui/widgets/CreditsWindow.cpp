#include "ui/widgets/CreditsWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ui/WindowManager.h"
#include "ui/widgets/TextWidgets.h"

namespace choco::ui {

namespace {

// CreditLine (0xaf22): trim spaces and tabs from both ends, turn '[' and ']' into the header colour tags, add '\n'.
void creditLine(std::string& out, std::string_view s, const std::string& headerColor) {
  const auto isPad = [](char c) { return c == ' ' || c == '\t'; };
  while (!s.empty() && isPad(s.back())) s.remove_suffix(1);
  while (!s.empty() && isPad(s.front())) s.remove_prefix(1);
  for (char c : s) {
    if (c == '[') out += "<font color='" + headerColor + "'>";
    else if (c == ']') out += "</font>";
    else out += c;
  }
  out += '\n';
}

}  // namespace

std::string colorHex(const gfx::ColorF& c) {
  const auto ch = [](float v) { return static_cast<unsigned>(std::clamp(static_cast<int>(v * 255.0f), 0, 255)); };
  char buf[16];
  std::snprintf(buf, sizeof buf, "%02x%02x%02x%02x", ch(c.r), ch(c.g), ch(c.b), ch(c.a));
  return buf;
}

CreditsColumns parseCredits(std::string_view file, const std::string& headerColor, int windowHeight, int fontSize) {
  CreditsColumns cols;
  const auto line1 = [&](std::string& out, std::string_view s) { creditLine(out, s, headerColor); };
  std::size_t pos = 0;
  while (pos < file.size()) {
    const std::size_t nl = file.find('\n', pos);
    std::string_view line;
    if (nl == std::string_view::npos) {
      line = file.substr(pos);
      line.remove_suffix(1);  // no terminator: the dropped character is a real one
      pos = file.size();
    } else {
      line = file.substr(pos, nl - pos);
      if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
      pos = nl + 1;
    }
    if (const std::size_t bar = line.find('|'); bar != std::string_view::npos) {
      line1(cols.left, line.substr(0, bar));
      line1(cols.right, line.substr(bar + 1));
      cols.centre += '\n';
    } else {
      cols.left += '\n';
      cols.right += '\n';
      line1(cols.centre, line);
    }
  }
  const int pad = fontSize > 0 ? windowHeight / fontSize : 0;
  for (int i = 0; i < pad; ++i) {
    cols.left += '\n';
    cols.right += '\n';
    cols.centre += '\n';
  }
  return cols;
}

double creditsScrollPx(double elapsedMs, int intropause, int time, int scrollHeight) {
  if (time <= 0) return 0;
  double e = elapsedMs < intropause ? 0.0 : elapsedMs - intropause;
  e = std::fmod(e, static_cast<double>(time));
  return e * scrollHeight / time;
}

CreditsParams CreditsWindow::readParams(const Style& style) const {
  CreditsParams p;
  p.time = static_cast<int>(style.numberOr("time", 0));
  p.intropause = static_cast<int>(style.numberOr("intropause", 0));
  p.columnGap = static_cast<int>(style.numberOr("columngap", 0));
  p.columnWidth = static_cast<int>(style.numberOr("columnwidth", 0));
  p.fontSize = static_cast<int>(style.numberOr("fontsize", 16));
  p.file = style.stringOr("file");
  p.face = style.stringOr("font", "__DEFAULT");
  const auto colour = [&](const char* key) {
    gfx::ColorF c{1, 1, 1, 1};
    if (auto v = style.numbers(key, 4, 1.0))
      c = {static_cast<float>((*v)[0]), static_cast<float>((*v)[1]), static_cast<float>((*v)[2]),
           static_cast<float>((*v)[3])};
    return c;
  };
  p.fontColor = colour("fontcolor");
  p.headerColor = colour("headercolor");
  return p;
}

std::optional<std::string> CreditsWindow::loadText(const CreditsParams& p) const { return manager().readTextFile(p.file); }

void CreditsWindow::init(const Style& style) {
  Window::init(style);
  const CreditsParams p = readParams(style);
  time_ = p.time;
  intropause_ = p.intropause;
  columnGap_ = p.columnGap;
  columnWidth_ = p.columnWidth;
  fontSize_ = p.fontSize;
  const std::string& file = p.file;
  const std::string& face = p.face;
  const gfx::ColorF fontColor = p.fontColor, headerColor = p.headerColor;

  const std::optional<std::string> text = loadText(p);
  if (!text) {
    manager().log("[credits] cannot open '" + file + "': the credits stay empty");
    return;
  }
  cols_ = parseCredits(*text, colorHex(headerColor), rect().h, fontSize_);

  gfx::text::TextSystem* ts = manager().textSystem();
  gfx::text::TextStyle ls;
  if (ts) {
    ls.font = ts->fonts.get(face);
    ls.fonts = &ts->fonts;
  }
  ls.size = static_cast<float>(fontSize_ > 0 ? fontSize_ : 12);
  ls.color = fontColor;
  ls.flags = gfx::text::kHAlignCenter;
  // The columns are laid out for the draw rectangle's height: top -fontsize, bottom h + 2*fontsize.
  const int boxH = rect().h + 3 * fontSize_;
  const int fullW = 2 * columnWidth_ + columnGap_;
  const auto build = [&](Column& c, const std::string& t, int w, int left) {
    c.layout = std::make_unique<gfx::text::TextLayout>(gfx::text::TextLayout::build(t, ls, w, boxH));
    c.left = left;
  };
  build(colA_, cols_.left, columnWidth_, columnGap_);
  build(colB_, cols_.right, columnWidth_, rect().w / 2 + columnGap_ / 2);
  build(colC_, cols_.centre, fullW, columnGap_);
  // H, once, from the bottom of column A's text bounds. The original uses half of it (docs/re/sweep-findings.md
  // section 3: the binary really computes floor(totalHeight / 2)), which leaves the last lines unreachable; here H is the
  // whole height unless kCreditsScrollFullHeight is turned off.
  const gfx::RectI b = colA_.layout->bounds();
  scrollHeight_ = kCreditsScrollFullHeight ? b.y + b.h : (b.y + b.h) / 2;
  loaded_ = true;
}

void CreditsWindow::draw(DrawContext& ctx) const {
  if (!loaded_) return;
  const double now = manager().nowMs();
  if (startMs_ < 0) startMs_ = now;
  const double s = creditsScrollPx(now - startMs_, intropause_, time_, scrollHeight_);
  const int fs = std::max(1, fontSize_);
  // firstVisibleLine = s / fontsize: whole part is the first line, the fraction is a sub-line shift of frac * fontsize.
  const int first = static_cast<int>(s / fs);
  const float shift = static_cast<float>((s / fs - first) * fs);
  gfx::text::TextSystem* ts = manager().textSystem();
  if (!ts) return;
  for (const Column* c : {&colA_, &colB_, &colC_})
    c->layout->drawScrolled(ctx.renderer, ts->atlas,
                            {ctx.origin.x + static_cast<float>(c->left), ctx.origin.y - static_cast<float>(fs)},
                            ctx.alpha, first, shift);
}

std::string CreditsWindow::detail() const {
  return " time=" + std::to_string(time_) + " intropause=" + std::to_string(intropause_) +
         " scrollheight=" + std::to_string(scrollHeight_) +
         " lines=" + (loaded_ ? std::to_string(colA_.layout->lineCount()) : std::string("none"));
}

void registerCreditsWindow(WindowManager& wm) {
  wm.addBuiltinType("CreditsWindow", [](WindowManager& m) { return std::make_unique<CreditsWindow>(m); });
}

}  // namespace choco::ui
