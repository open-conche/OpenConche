#include "gfx/text/TextLayout.h"
#include "app/Diagnostics.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace choco::gfx::text {

namespace {

constexpr std::size_t kMaxTagLength = 128;
constexpr std::size_t kMaxStack = 32;

TextSystem* g_current = nullptr;

struct FontEntry {
  std::shared_ptr<const Font> font;
  int size = 12;
  ColorF color;
};

// Markup state: everything a tag can change. Copied whole at wrap candidates so a rewind restores it exactly.
struct State {
  std::vector<FontEntry> fonts;
  bool bold = false, italic = false, underline = false;
  int outline = 0;
  ColorF outlineColor{0, 0, 0, 1};
  std::vector<int> justify;
  bool link = false;
  std::string linkId;
  ColorF linkColor;
};

struct PlacedGlyph {
  std::shared_ptr<const Font> font;
  int glyph = 0;
  std::uint32_t cp = 0;
  double x = 0;  // pen, quarter pixels from the line origin
  double width = 0;
  float size = 12;
  ColorF color;
  bool bold = false, italic = false, underline = false;
  int outline = 0;
  ColorF outlineColor;
  std::string link;  // id of the <a> it sits in; the link colour applies only while that link is hovered
  ColorF linkColor;
};

struct Line {
  std::vector<PlacedGlyph> g;
  double widthQ = 0;     // pen x at the end, quarter pixels
  double startPad = 0;   // left bearing compensation of the first glyph, quarter pixels
  int height = 0;
  float baseline = 0;
  int justify = 0;
  int leftOutline = 0, rightOutline = 0;
  // Filled in by the alignment pass.
  float x0 = 0, top = 0;
  int bottom = 0;  // top + height, for the box overflow rule
};

std::string lowerCopy(std::string_view s) {
  std::string r(s);
  std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return r;
}

// Decodes one code point at s[pos], advancing pos. No validation of continuation bytes beyond staying in range.
std::uint32_t decodeUtf8(std::string_view s, std::size_t& pos) {
  const auto b0 = static_cast<unsigned char>(s[pos]);
  int extra = 0;
  std::uint32_t cp = b0;
  if (b0 >= 0xf0) extra = 3, cp = b0 & 0x07;
  else if (b0 >= 0xe0) extra = 2, cp = b0 & 0x0f;
  else if (b0 >= 0xc0) extra = 1, cp = b0 & 0x1f;
  else if (b0 >= 0x80) {
    ++pos;
    return b0;  // a stray continuation byte stands for itself
  }
  ++pos;
  for (int i = 0; i < extra && pos < s.size(); ++i, ++pos) cp = (cp << 6) | (static_cast<unsigned char>(s[pos]) & 0x3f);
  return cp;
}

struct Attrs {
  std::vector<std::pair<std::string, std::string>> kv;
  const std::string* get(const char* key) const {
    for (auto& p : kv)
      if (p.first == key) return &p.second;
    return nullptr;
  }
};

// key=value pairs after the tag name; values may be quoted with ' or ".
Attrs parseAttrs(std::string_view raw) {
  Attrs a;
  std::size_t i = 0;
  const std::size_t n = raw.size();
  // skip the tag name
  while (i < n && !std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
  while (i < n) {
    while (i < n && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
    std::size_t k0 = i;
    while (i < n && raw[i] != '=' && !std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
    std::string key = lowerCopy(raw.substr(k0, i - k0));
    while (i < n && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
    if (i >= n || raw[i] != '=') continue;
    ++i;
    while (i < n && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
    std::string val;
    if (i < n && (raw[i] == '\'' || raw[i] == '"')) {
      const char q = raw[i++];
      while (i < n && raw[i] != q) val += raw[i++];
      if (i < n) ++i;
    } else {
      while (i < n && !std::isspace(static_cast<unsigned char>(raw[i]))) val += raw[i++];
    }
    if (!key.empty()) a.kv.push_back({key, val});
  }
  return a;
}

// Spaces outside quotes removed; lower case until the first '=' (RE 3.3).
std::string normaliseTag(std::string_view raw) {
  std::string out;
  bool quoted = false, seenEq = false;
  char q = 0;
  for (char c : raw) {
    if (quoted) {
      out += c;
      if (c == q) quoted = false;
      continue;
    }
    if (c == '\'' || c == '"') {
      quoted = true;
      q = c;
      out += c;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) continue;
    if (c == '=') seenEq = true;
    out += seenEq ? c : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

int asInt(const std::string* s, int def) { return s ? std::atoi(s->c_str()) : def; }

}  // namespace

ParsedColor parseHexColor(std::string_view s) {
  ParsedColor r;
  if (!s.empty() && s[0] == '#') s.remove_prefix(1);
  if (s.size() != 6 && s.size() != 8) return r;
  unsigned v[4] = {0, 0, 0, 255};
  for (std::size_t i = 0; i < s.size() / 2; ++i) {
    unsigned t = 0;
    for (std::size_t k = 0; k < 2; ++k) {
      const char c = s[i * 2 + k];
      if (!std::isxdigit(static_cast<unsigned char>(c))) return r;
      t = t * 16 + static_cast<unsigned>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : std::tolower(c) - 'a' + 10);
    }
    v[i] = t;
  }
  r.ok = true;
  r.color = {static_cast<float>(v[0]) / 255.0f, static_cast<float>(v[1]) / 255.0f, static_cast<float>(v[2]) / 255.0f,
             static_cast<float>(v[3]) / 255.0f};
  return r;
}

TextSystem::~TextSystem() {
  if (g_current == this) g_current = nullptr;
}
void TextSystem::install(TextSystem* t) { g_current = t; }
TextSystem* TextSystem::current() { return g_current; }

struct TextLayout::Impl {
  std::vector<Line> lines;
  int boxW = 0, boxH = 0;
  int totalH = 0;
  float yTop = 0;  // block top relative to the window
  int padding = 0;
  RectI bounds;
  std::string hovered;
};

TextLayout::TextLayout() : impl_(std::make_unique<Impl>()) {}
TextLayout::~TextLayout() = default;
TextLayout::TextLayout(TextLayout&&) noexcept = default;
TextLayout& TextLayout::operator=(TextLayout&&) noexcept = default;

namespace {

class Builder {
 public:
  Builder(std::string_view text, const TextStyle& style, int w) : text_(text), style_(style), boxW_(w) {
    FontEntry base;
    base.font = style.font ? style.font : (style.fonts ? style.fonts->builtin() : makeMonospaceFont("__DEFAULT"));
    base.size = static_cast<int>(style.size);
    if (base.size <= 0) base.size = 12;
    base.color = style.color;
    st_.fonts.push_back(base);
    st_.linkColor = base.color;
  }

  std::vector<Line> run() {
    std::size_t pos = 0;
    while (pos < text_.size()) {
      const std::size_t p0 = pos;
      const char c = text_[pos];
      if (c == '<' && tryTag(pos)) continue;
      std::uint32_t cp;
      if (c == '\\' && pos + 1 < text_.size()) {
        ++pos;
        cp = decodeUtf8(text_, pos);
      } else {
        cp = decodeUtf8(text_, pos);
        if (cp == '\n') {
          finishLine(true);
          continue;
        }
        if (cp == '\r') continue;
      }
      if (!addGlyph(cp, p0, pos)) pos = p0;  // false: the line was closed before this character; process it again
      if (rewind_) {
        pos = rewindPos_;
        rewind_ = false;
      }
    }
    finishLine(false);
    return std::move(lines_);
  }

 private:
  struct Candidate {
    bool valid = false;
    std::size_t glyphs = 0;
    double x = 0;
    std::size_t pos = 0;
    State st;
    bool italic = false;
  };

  const FontEntry& fe() const { return st_.fonts.back(); }

  int effectiveJustify() const {
    if (!st_.justify.empty()) return st_.justify.back();
    if (style_.flags & kHAlignCenter) return 1;
    if (style_.flags & kHAlignRight) return 2;
    return 0;
  }

  std::shared_ptr<const Font> fallbackFont() const {
    return style_.fonts ? style_.fonts->defaultFont() : builtinFont();
  }

  bool addGlyph(std::uint32_t cp, std::size_t, std::size_t after) {
    const FontEntry& f = fe();
    std::shared_ptr<const Font> font = f.font;
    int gi = font->glyphIndex(cp);
    if (gi < 0) {  // the engine's default font takes over for characters the run's font lacks
      font = fallbackFont();
      gi = font->glyphIndex(cp);
      if (gi < 0) return true;  // in neither font: no width, nothing drawn
    }
    const GlyphMetrics m = font->metrics(gi);
    const double sQ = static_cast<double>(font->pixelScale(static_cast<float>(f.size))) * 4.0;

    if (cp == ' ' && x_ != 0.0) {
      cand_.valid = true;
      cand_.glyphs = cur_.g.size();
      cand_.x = x_;
      cand_.pos = after;
      cand_.st = st_;
      cand_.italic = lineItalic_;
    }

    // Turning italic on or off shifts the pen; the "previous" flag restarts with every line (RE 3.4, item 7).
    double italicShift = 0;
    if (st_.italic != lineItalic_) italicShift = sQ / 3.0 * (lineItalic_ ? font->ascent() : font->descent());

    const bool first = cur_.g.empty();
    const double x = x_ + corr_ + italicShift;
    const double pad = first ? -sQ * m.minX + 4.0 : cur_.startPad;
    double w, newCorr = 0;
    if (m.maxX <= m.advance) {
      w = m.advance * sQ;
    } else {
      w = m.maxX * sQ;
      newCorr = sQ * (m.advance - m.maxX);
    }
    // The first glyph's left-bearing pad is part of the pen (so of the line width); glyphs are placed without it
    // and the alignment offset adds it back.
    const double xEnd = x + (first ? pad : 0.0) + w;
    const int leftOut = first ? st_.outline : cur_.leftOutline;
    const bool overflow = std::lround(static_cast<double>(leftOut + 1 + st_.outline) * 4.0 + xEnd) > 4L * boxW_;
    // Chocolatier 1: the string's last glyph stays on the line when no space candidate exists (the line then ends after it).
    const bool keepLast = style_.keepLastOverflowChar && !cand_.valid && after >= text_.size();
    if (overflow && !first && !keepLast) {
      if (cand_.valid) {
        cur_.g.resize(cand_.glyphs);
        x_ = cand_.x;
        corr_ = 0;
        st_ = cand_.st;
        lineItalic_ = cand_.italic;
        rewind_ = true;
        rewindPos_ = cand_.pos;
        cand_.valid = false;
        finishLine(false);
        return true;
      }
      finishLine(false);
      return false;
    }
    if (first) {
      cur_.startPad = pad;
      cur_.leftOutline = st_.outline;
      cur_.justify = effectiveJustify();
    }
    PlacedGlyph g;
    g.font = font;
    g.glyph = gi;
    g.cp = cp;
    g.x = first ? x : x - cur_.startPad;
    g.width = w;
    g.size = static_cast<float>(f.size);
    g.color = f.color;
    g.bold = st_.bold;
    g.italic = st_.italic;
    g.underline = st_.underline;
    g.outline = st_.outline;
    g.outlineColor = st_.outlineColor;
    if (st_.link) {
      g.link = st_.linkId;
      g.linkColor = st_.linkColor;
    }
    cur_.g.push_back(std::move(g));
    cur_.rightOutline = st_.outline;
    cur_.height = std::max(cur_.height, f.size);
    cur_.baseline = std::max(cur_.baseline, font->baseline(static_cast<float>(f.size)));
    x_ = xEnd;
    corr_ = newCorr;
    lineItalic_ = st_.italic;
    if (overflow) finishLine(false);  // a single glyph wider than the box still takes the line
    return true;
  }

  void finishLine(bool explicitBreak) {
    if (cur_.g.empty()) {
      if (explicitBreak) {
        Line l;
        l.height = fe().size;
        l.baseline = fe().font->baseline(static_cast<float>(fe().size));
        l.justify = effectiveJustify();
        l.height += extraPad_;
        lines_.push_back(std::move(l));
      }
    } else {
      cur_.widthQ = x_;
      cur_.height += extraPad_;
      lines_.push_back(std::move(cur_));
    }
    cur_ = Line{};
    x_ = 0;
    corr_ = 0;
    extraPad_ = 0;
    cand_.valid = false;
    lineItalic_ = false;
  }

  static const char* const kTags[];

  bool tryTag(std::size_t& pos) {
    const std::size_t close = text_.find('>', pos + 1);
    if (close == std::string_view::npos || close - pos - 1 > kMaxTagLength) return false;
    const std::string_view raw = text_.substr(pos + 1, close - pos - 1);
    const std::string norm = normaliseTag(raw);
    int which = -1;
    for (int i = 0; kTags[i]; ++i) {
      if (norm.rfind(kTags[i], 0) == 0) {
        which = i;
        break;
      }
    }
    pos = close + 1;
    if (which < 0) return true;  // unknown tag: swallowed, zero width
    const std::string_view name = kTags[which];
    const Attrs a = parseAttrs(raw);
    FontEntry& top = st_.fonts.back();
    if (name == "br") {
      extraPad_ += asInt(a.get("pad"), 0);
      finishLine(true);
    } else if (name == "/p") {
      finishLine(true);
    } else if (name == "a") {
      st_.link = true;
      st_.linkId = a.get("id") ? *a.get("id") : std::string();
      st_.linkColor = top.color;
      if (auto* c = a.get("color")) {
        if (auto pc = parseHexColor(*c); pc.ok) st_.linkColor = pc.color;
      }
    } else if (name == "/a") {
      st_.link = false;
    } else if (name == "b") {
      st_.bold = true;
    } else if (name == "/b") {
      st_.bold = false;
    } else if (name == "i") {
      st_.italic = true;
    } else if (name == "/i") {
      st_.italic = false;
    } else if (name == "u") {
      st_.underline = true;
    } else if (name == "/u") {
      st_.underline = false;
    } else if (name == "cursor") {
      // caret placement belongs to the text-edit window, not the layout
    } else if (name == "center" || name == "left" || name == "right") {
      finishLine(false);
      if (st_.justify.size() < kMaxStack) st_.justify.push_back(name == "left" ? 0 : name == "center" ? 1 : 2);
    } else if (name == "/center" || name == "/left" || name == "/right") {
      finishLine(false);
      if (!st_.justify.empty()) st_.justify.pop_back();
    } else if (name == "font") {
      if (st_.fonts.size() < kMaxStack) {
        FontEntry e = top;
        if (auto* s = a.get("size")) e.size = std::max(1, std::atoi(s->c_str()));
        if (auto* c = a.get("color")) {
          if (auto pc = parseHexColor(*c); pc.ok) e.color = pc.color;
        }
        if (auto* face = a.get("face"); face && style_.fonts) e.font = style_.fonts->get(*face);
        st_.fonts.push_back(std::move(e));
      }
    } else if (name == "/font") {
      if (st_.fonts.size() > 1) st_.fonts.pop_back();
    } else if (name == "outline") {
      st_.outline = asInt(a.get("size"), 2);
      st_.outlineColor = {0, 0, 0, 1};
      if (auto* c = a.get("color")) {
        if (auto pc = parseHexColor(*c); pc.ok) st_.outlineColor = {pc.color.r, pc.color.g, pc.color.b, 1.0f};
      }
    } else if (name == "/outline") {
      st_.outline = 0;
    } else if (name == "tab") {
      x_ = asInt(a.get("pos"), 0) * 4.0 + (cur_.g.empty() ? 0.0 : cur_.startPad);
      corr_ = 0;
    }
    return true;
  }

  std::string_view text_;
  const TextStyle& style_;
  int boxW_;
  State st_;
  Line cur_;
  std::vector<Line> lines_;
  double x_ = 0, corr_ = 0;
  int extraPad_ = 0;
  Candidate cand_;
  bool lineItalic_ = false;
  bool rewind_ = false;
  std::size_t rewindPos_ = 0;
};

// Prefix-matched in this order (RE 3.3).
const char* const Builder::kTags[] = {"br",     "/p",      "a",     "/a",    "b",     "/b",       "i",       "/i",
                                      "u",      "/u",      "cursor", "center", "left",  "right",    "/center", "/left",
                                      "/right", "font",    "/font", "outline", "/outline", "tab",   nullptr};

}  // namespace

TextLayout TextLayout::build(std::string_view utf8, const TextStyle& style, int w, int h) {
  TextLayout out;
  Impl& d = *out.impl_;
  d.boxW = w;
  d.boxH = h;
  d.padding = style.padding;
  if (w <= 0 || h <= 0) {
    diag::err("[text] layout into a %dx%d box ignored\n", w, h);
    return out;
  }
  Builder b(utf8, style, w);
  d.lines = b.run();

  for (const Line& l : d.lines) d.totalH += l.height;
  float yOrigin = 0, shift = 0;
  if (style.flags & kVAlignCenter) {
    yOrigin = static_cast<float>(h / 2);
    shift = -static_cast<float>(d.totalH) * 0.5f;
  } else if (style.flags & kVAlignBottom) {
    yOrigin = static_cast<float>(h);
    shift = -static_cast<float>(d.totalH);
  }
  d.yTop = yOrigin + shift;

  float y = d.yTop;
  float minX = 1e30f, maxX = -1e30f;
  for (Line& l : d.lines) {
    // Line width in whole pixels: the pen (which includes the first glyph's pad) rounded, then >> 2.
    const int lineW = static_cast<int>(std::lround(l.widthQ)) >> 2;
    float originX, offset;
    switch (l.justify) {
      case 1:
        originX = static_cast<float>(w / 2);
        offset = static_cast<float>(-lineW) * 0.5f;
        break;
      case 2:
        originX = static_cast<float>(w - l.rightOutline - 1);
        offset = static_cast<float>(-lineW);
        break;
      default:
        originX = static_cast<float>(l.leftOutline);
        offset = 0;
        break;
    }
    offset += static_cast<float>(l.startPad * 0.25 + 0.5);
    l.x0 = originX + offset;
    l.top = y;
    y += static_cast<float>(l.height);
    l.bottom = static_cast<int>(std::lround(y));
    if (!l.g.empty()) {
      minX = std::min(minX, l.x0);
      maxX = std::max(maxX, l.x0 + static_cast<float>(lineW));
    }
  }
  // Like the original, bounds truncate toward zero.
  if (d.lines.empty() || minX > maxX) {
    d.bounds = {0, static_cast<int>(d.yTop), 0, d.totalH};
  } else {
    const int x0 = static_cast<int>(minX), x1 = static_cast<int>(maxX);
    d.bounds = {x0, static_cast<int>(d.yTop), x1 - x0, d.totalH};
  }
  return out;
}

RectI TextLayout::measure(std::string_view utf8, const TextStyle& style, int w, int h, int pad) {
  RectI b = build(utf8, style, w, h).bounds();
  b.w += pad;
  return b;
}

RectI TextLayout::bounds() const { return impl_->bounds; }
int TextLayout::lineCount() const { return static_cast<int>(impl_->lines.size()); }
void TextLayout::setHoveredLink(std::string_view id) { impl_->hovered = std::string(id); }

std::vector<TextLayout::GlyphPos> TextLayout::glyphPositions() const {
  std::vector<GlyphPos> out;
  int li = 0;
  for (const Line& l : impl_->lines) {
    for (const PlacedGlyph& g : l.g) out.push_back({g.cp, l.x0 + static_cast<float>(g.x / 4.0), l.top + l.baseline, li});
    ++li;
  }
  return out;
}

std::vector<TextLayout::GlyphRun> TextLayout::glyphRuns(PointF origin) const {
  std::vector<GlyphRun> out;
  int lineIndex = 0;
  for (const Line& l : impl_->lines) {
    const float bottom = static_cast<float>(l.bottom) + static_cast<float>(lineIndex * impl_->padding);
    const int li = lineIndex++;
    if (bottom > static_cast<float>(impl_->boxH)) break;
    if (bottom < 0) continue;
    const int baseY = static_cast<int>(std::floor(origin.y + l.top + l.baseline + static_cast<float>(li * impl_->padding) + 0.5f));
    for (const PlacedGlyph& g : l.g) {
      GlyphRun r;
      r.font = g.font.get();
      r.glyph = g.glyph;
      r.x = origin.x + l.x0 + static_cast<float>(g.x / 4.0);
      r.baselineY = baseY;
      r.size = g.size;
      r.color = g.color;
      r.bold = g.bold;
      r.italic = g.italic;
      r.outline = g.outline;
      r.outlineColor = g.outlineColor;
      out.push_back(r);
    }
  }
  return out;
}

bool TextLayout::caretAt(std::size_t glyphIndex, CaretPos& out) const {
  if (impl_->lines.empty()) return false;
  std::size_t seen = 0;
  for (const Line& l : impl_->lines) {
    if (glyphIndex < seen + l.g.size()) {
      out = {l.x0 + static_cast<float>(l.g[glyphIndex - seen].x / 4.0), l.top, static_cast<float>(l.height)};
      return true;
    }
    seen += l.g.size();
  }
  const Line& last = impl_->lines.back();
  float x = last.x0;
  if (!last.g.empty()) x += static_cast<float>((last.g.back().x + last.g.back().width) / 4.0);
  out = {x, last.top, static_cast<float>(last.height)};
  return true;
}

void TextLayout::draw(Renderer& r, PointF origin, float alpha) const {
  TextSystem* ts = TextSystem::current();
  if (!ts) {
    static bool warned = false;
    if (!warned) diag::err("[text] no TextSystem installed: text is not drawn\n");
    warned = true;
    return;
  }
  draw(r, ts->atlas, origin, alpha);
}

void TextLayout::draw(Renderer& r, GlyphAtlas& atlas, PointF origin, float alpha) const {
  drawImpl(r, atlas, origin, alpha, -1, 0.0f);
}

void TextLayout::drawScrolled(Renderer& r, GlyphAtlas& atlas, PointF origin, float alpha, int firstLine,
                              float shiftPx) const {
  drawImpl(r, atlas, origin, alpha, std::max(0, firstLine), shiftPx);
}

void TextLayout::drawImpl(Renderer& r, GlyphAtlas& atlas, PointF origin, float alpha, int firstLine,
                          float shiftPx) const {
  if (alpha <= 0 || impl_->lines.empty()) return;
  const PointF so = r.outputOrigin(), ss = r.outputScaleXY();
  const float sx = std::max(0.01f, ss.x), sy = std::max(0.01f, ss.y);
  atlas.beginDraw();

  struct Batch {
    const Texture* tex;
    std::vector<Vertex> v;
    std::vector<int> idx;
  };
  // Pass 0: outline strokes. Pass 1: fills (bold glyphs use the widened bitmap), then underlines.
  std::vector<Batch> batches[2];
  struct Rule {
    RectF rect;
    ColorF color;
  };
  std::vector<Rule> rules;

  auto emit = [&](int pass, const Texture* tex, float lx0, float ly0, float lx1, float ly1, float u0, float v0, float u1,
                  float v1, ColorF c) {
    auto& bs = batches[pass];
    auto it = std::find_if(bs.begin(), bs.end(), [&](const Batch& b) { return b.tex == tex; });
    if (it == bs.end()) {
      bs.push_back({tex, {}, {}});
      it = bs.end() - 1;
    }
    const int base = static_cast<int>(it->v.size());
    it->v.push_back({{lx0, ly0}, c, {u0, v0}});
    it->v.push_back({{lx1, ly0}, c, {u1, v0}});
    it->v.push_back({{lx1, ly1}, c, {u1, v1}});
    it->v.push_back({{lx0, ly1}, c, {u0, v1}});
    for (int k : {0, 1, 2, 0, 2, 3}) it->idx.push_back(base + k);
  };

  // A scrolled draw starts at `firstLine`, whose top is moved up to the window's top edge and then `shiftPx` further.
  float dy = 0;
  if (firstLine >= 0) {
    if (firstLine >= static_cast<int>(impl_->lines.size())) return;
    dy = -(impl_->lines[static_cast<std::size_t>(firstLine)].top + static_cast<float>(firstLine * impl_->padding)) -
         shiftPx;
  }
  int lineIndex = 0;
  for (const Line& l : impl_->lines) {
    // The original stops at the first line whose bottom is below the box and skips lines that end above it.
    const float bottom = static_cast<float>(l.bottom) + static_cast<float>(lineIndex * impl_->padding) + dy;
    const int li = lineIndex++;
    if (li < firstLine) continue;
    if (bottom > static_cast<float>(impl_->boxH)) break;
    if (bottom < 0) continue;
    const float baseY = origin.y + dy + l.top + l.baseline + static_cast<float>(li * impl_->padding);
    // Whole output pixels in absolute device coordinates, which include the letterbox offset.
    const int devY = static_cast<int>(std::floor(baseY * sy + so.y + 0.5f));
    for (const PlacedGlyph& g : l.g) {
      const float devX0 = (origin.x + l.x0 + static_cast<float>(g.x / 4.0)) * sx + so.x;
      const long q = std::lround(devX0 * 4.0f);  // quarter pixels, rounded
      const long ixl = q >= 0 ? q / 4 : -((-q + 3) / 4);
      const float ix = static_cast<float>(ixl);
      const int sub = static_cast<int>(q - ixl * 4);
      RasterParams p;
      p.scale = g.font->pixelScale(g.size) * sx;
      p.yRatio = sy / sx;
      p.subX = sub;
      p.italic = g.italic;
      auto place = [&](int pass, const GlyphAtlas::Entry& e, ColorF c) {
        if (!e.texture) return;
        c.a *= alpha;
        emit(pass, e.texture, (ix + static_cast<float>(e.x) - so.x) / sx, (static_cast<float>(devY + e.y) - so.y) / sy,
             (ix + static_cast<float>(e.x + e.w) - so.x) / sx, (static_cast<float>(devY + e.y + e.h) - so.y) / sy, e.u0,
             e.v0, e.u1, e.v1, c);
      };
      const float boldHalf = g.bold ? g.size * 0.0125f * sx : 0.0f;
      if (g.outline > 0) {
        p.stroke = static_cast<float>(g.outline) * sx + boldHalf;
        place(0, atlas.get(*g.font, g.glyph, p), g.outlineColor);
      }
      p.stroke = boldHalf;
      const ColorF fillColor = (!g.link.empty() && g.link == impl_->hovered) ? g.linkColor : g.color;
      place(1, atlas.get(*g.font, g.glyph, p), fillColor);
      if (g.underline) {
        ColorF c = fillColor;
        c.a *= alpha;
        const float x = origin.x + l.x0 + static_cast<float>(g.x / 4.0);
        rules.push_back({{x, baseY + 2.0f, static_cast<float>(g.width / 4.0), 1.0f}, c});
      }
    }
  }

  atlas.flush();
  for (auto& pass : batches)
    for (auto& b : pass) r.drawGeometry(b.tex, b.v, b.idx, Blend::Normal);
  for (const Rule& u : rules) r.fillRect(u.rect, u.color, Blend::Normal);
}

}  // namespace choco::gfx::text
