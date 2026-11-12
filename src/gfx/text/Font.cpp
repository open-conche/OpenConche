#include "gfx/text/Font.h"
#include "app/Diagnostics.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace choco::gfx::text {

namespace {

constexpr std::size_t kHeader = 12, kRecord = 18;
constexpr float kFixed = 1.0f / 128.0f;

std::uint16_t rd16(const std::byte* p) {
  return static_cast<std::uint16_t>(std::to_integer<unsigned>(p[0]) | (std::to_integer<unsigned>(p[1]) << 8));
}
std::int16_t rds16(const std::byte* p) { return static_cast<std::int16_t>(rd16(p)); }
std::uint32_t rd32(const std::byte* p) {
  return static_cast<std::uint32_t>(rd16(p)) | (static_cast<std::uint32_t>(rd16(p + 2)) << 16);
}

// Walks one outline program. `visit(op, operands)` gets the opcode and a pointer to its operand bytes. Returns false
// if the program runs off the end of the data before an 08.
template <class F>
bool walkProgram(const std::vector<std::byte>& d, std::size_t pos, F&& visit) {
  const std::size_t n = d.size();
  while (pos < n) {
    const unsigned op = std::to_integer<unsigned>(d[pos]);
    std::size_t len = 1;  // operand bytes after the opcode
    switch (op) {
      case 2:
      case 3: len = 1; break;
      case 4:
      case 5: len = 4; break;
      case 6: len = 8; break;
      case 8: len = 0; break;
      default: len = 0; break;  // 00 01 07 and 09..ff: skipped one byte at a time
    }
    if (pos + 1 + len > n) return false;
    visit(op, d.data() + pos + 1);
    pos += 1 + len;
    if (op == 8) return true;
  }
  return false;
}

std::int16_t quantise(float v) {
  const long r = std::lround(v * 128.0f);
  return static_cast<std::int16_t>(std::clamp(r, -32768L, 32767L));
}

void put16(std::vector<std::byte>& out, std::uint16_t v) {
  out.push_back(static_cast<std::byte>(v & 0xff));
  out.push_back(static_cast<std::byte>(v >> 8));
}
void put32(std::vector<std::byte>& out, std::uint32_t v) {
  put16(out, static_cast<std::uint16_t>(v & 0xffff));
  put16(out, static_cast<std::uint16_t>(v >> 16));
}
void putS(std::vector<std::byte>& out, float v) { put16(out, static_cast<std::uint16_t>(quantise(v))); }

std::atomic<std::uint64_t> g_nextId{1};

}  // namespace

std::shared_ptr<const Font> Font::fromBytes(std::vector<std::byte> bytes, std::string name, std::string* error) {
  auto fail = [&](const char* why) -> std::shared_ptr<const Font> {
    if (error) *error = why;
    return nullptr;
  };
  if (bytes.size() < kHeader) return fail("file too short");
  if (std::memcmp(bytes.data(), "MVC4", 4) != 0) return fail("bad magic (not MVC4)");
  const std::size_t count = rd16(bytes.data() + 4);
  if (count == 0) return fail("no glyphs");
  const std::size_t tableEnd = kHeader + count * kRecord;
  if (tableEnd > bytes.size()) return fail("glyph table runs past the end of the file");

  std::shared_ptr<Font> f(new Font());
  f->glyphs_.resize(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::byte* r = bytes.data() + kHeader + i * kRecord;
    Rec& g = f->glyphs_[i];
    g.cp = rd32(r);
    g.minX = rds16(r + 4);
    g.maxX = rds16(r + 6);
    g.minY = rds16(r + 8);
    g.maxY = rds16(r + 10);
    g.advance = rds16(r + 12);
    g.offset = rd32(r + 14);
    if (i > 0 && g.cp <= f->glyphs_[i - 1].cp) return fail("code points are not strictly ascending");
    if (g.offset < tableEnd || g.offset >= bytes.size()) return fail("glyph outline offset outside the data area");
  }
  f->data_ = std::move(bytes);
  for (const Rec& g : f->glyphs_) {
    if (!walkProgram(f->data_, g.offset, [](unsigned, const std::byte*) {})) return fail("outline program is not terminated");
  }

  // Ascent from 'W', descent from the ASCII range without '_' (RE 1.3).
  float ascent = 0, descent = 0;
  bool haveW = false, haveDescent = false;
  float fallbackAscent = 0;
  for (const Rec& g : f->glyphs_) {
    if (g.cp > 0x7f) break;
    if (g.cp == 0x57) {
      ascent = -g.minY * kFixed;
      haveW = true;
    }
    if (g.cp != 0x5f) {
      descent = haveDescent ? std::max(descent, g.maxY * kFixed) : g.maxY * kFixed;
      haveDescent = true;
    }
    fallbackAscent = std::max(fallbackAscent, -g.minY * kFixed);
  }
  if (!haveW) ascent = fallbackAscent;
  if (ascent <= 0) ascent = 1;
  if (descent < 0) descent = 0;
  if (ascent + descent <= 0) descent = 0.25f * ascent;
  f->ascent_ = ascent;
  f->descent_ = descent;

  // Per-glyph squash to the line box (RE 1.3).
  const float base = (ascent + descent) * 0.25f;
  const float upLimit = 0.75f * base + ascent, downLimit = 0.25f * base + descent;
  f->scaleY_.resize(count, 1.0f);
  for (std::size_t i = 0; i < count; ++i) {
    const Rec& g = f->glyphs_[i];
    const float up = upLimit > 0 ? -g.minY * kFixed / upLimit : 0;
    const float down = downLimit > 0 ? g.maxY * kFixed / downLimit : 0;
    const float m = std::max(up, down);
    if (m > 1.0f) f->scaleY_[i] = 1.0f / m;
  }

  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  f->lithos_ = lower.find("lithos") != std::string::npos;
  f->name_ = std::move(name);
  f->id_ = g_nextId.fetch_add(1);
  return f;
}

int Font::glyphIndex(std::uint32_t cp) const {
  auto it = std::lower_bound(glyphs_.begin(), glyphs_.end(), cp, [](const Rec& r, std::uint32_t v) { return r.cp < v; });
  if (it == glyphs_.end() || it->cp != cp) return -1;
  return static_cast<int>(it - glyphs_.begin());
}

GlyphMetrics Font::metrics(int index) const {
  if (index < 0 || static_cast<std::size_t>(index) >= glyphs_.size()) return {};
  const Rec& g = glyphs_[static_cast<std::size_t>(index)];
  return {g.minX * kFixed, g.maxX * kFixed, g.minY * kFixed, g.maxY * kFixed, g.advance * kFixed};
}

std::vector<Contour> Font::outline(int index) const {
  std::vector<Contour> out;
  if (index < 0 || static_cast<std::size_t>(index) >= glyphs_.size()) return out;
  PointF pen;
  auto pt = [](const std::byte* p) { return PointF{rds16(p) * kFixed, rds16(p + 2) * kFixed}; };
  walkProgram(data_, glyphs_[static_cast<std::size_t>(index)].offset, [&](unsigned op, const std::byte* a) {
    if (op == 4) {
      pen = pt(a);
      out.push_back({pen, {}});
    } else if ((op == 5 || op == 6) && !out.empty()) {
      Segment s;
      if (op == 5) {
        s.p = pt(a);
      } else {
        s.quad = true;
        s.c = pt(a);
        s.p = pt(a + 4);
      }
      out.back().segs.push_back(s);
    }
  });
  return out;
}

std::size_t Font::programLength(int index) const {
  const std::size_t start = glyphs_[static_cast<std::size_t>(index)].offset;
  std::size_t len = 0;
  walkProgram(data_, start, [&](unsigned op, const std::byte*) {
    len += op == 2 || op == 3 ? 2 : op == 4 || op == 5 ? 5 : op == 6 ? 9 : 1;
  });
  return len;
}

float Font::pixelScale(float size) const {
  return 0.8f * size / (ascent_ + descent_) * (lithos_ ? 0.7f : 1.0f);
}

float Font::baseline(float size) const {
  return pixelScale(size) * (ascent_ + 0.1875f * (ascent_ + descent_));
}

std::vector<std::byte> buildMvec(std::vector<GlyphSpec> glyphs) {
  std::sort(glyphs.begin(), glyphs.end(), [](const GlyphSpec& a, const GlyphSpec& b) { return a.cp < b.cp; });
  std::vector<std::byte> programs;
  std::vector<std::uint32_t> offsets;
  const std::size_t tableEnd = kHeader + glyphs.size() * kRecord;
  for (const GlyphSpec& g : glyphs) {
    offsets.push_back(static_cast<std::uint32_t>(tableEnd + programs.size()));
    if (!g.contours.empty()) {
      programs.push_back(std::byte{2});
      programs.push_back(std::byte{1});
    }
    for (const Contour& c : g.contours) {
      programs.push_back(std::byte{4});
      putS(programs, c.start.x);
      putS(programs, c.start.y);
      for (const Segment& s : c.segs) {
        if (s.quad) {
          programs.push_back(std::byte{6});
          putS(programs, s.c.x);
          putS(programs, s.c.y);
        } else {
          programs.push_back(std::byte{5});
        }
        putS(programs, s.p.x);
        putS(programs, s.p.y);
      }
    }
    programs.push_back(std::byte{8});
  }
  std::vector<std::byte> out;
  for (char ch : {'M', 'V', 'C', '4'}) out.push_back(static_cast<std::byte>(ch));
  put16(out, static_cast<std::uint16_t>(glyphs.size()));
  for (int i = 0; i < 6; ++i) out.push_back(std::byte{0});
  for (std::size_t i = 0; i < glyphs.size(); ++i) {
    const GlyphSpec& g = glyphs[i];
    float x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    if (g.explicitBounds) {
      x0 = g.minX, x1 = g.maxX, y0 = g.minY, y1 = g.maxY;
    } else {
      bool first = true;
      auto add = [&](PointF p) {
        if (first) x0 = x1 = p.x, y0 = y1 = p.y, first = false;
        x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
      };
      for (const Contour& c : g.contours) {
        add(c.start);
        for (const Segment& s : c.segs) {
          if (s.quad) add(s.c);
          add(s.p);
        }
      }
    }
    put32(out, g.cp);
    putS(out, x0);
    putS(out, x1);
    putS(out, y0);
    putS(out, y1);
    putS(out, g.advance);
    put32(out, offsets[i]);
  }
  out.insert(out.end(), programs.begin(), programs.end());
  return out;
}

std::shared_ptr<const Font> makeMonospaceFont(std::string name, float advance, bool boxes) {
  std::vector<GlyphSpec> specs;
  for (std::uint32_t cp = 0x20; cp <= 0x7e; ++cp) {
    GlyphSpec g;
    g.cp = cp;
    g.advance = advance;
    const float bottom = cp == 'y' ? 25.0f : 0.0f;
    if (boxes && cp != 0x20) {
      Contour c;
      c.start = {advance * 0.1f, -75};
      c.segs = {{false, {}, {advance * 0.7f, -75}}, {false, {}, {advance * 0.7f, bottom}}, {false, {}, {advance * 0.1f, bottom}}};
      g.contours.push_back(c);
    } else if (cp == 'W' || cp == 'y') {
      g.explicitBounds = true;
      g.minY = cp == 'W' ? -75.0f : 0.0f;
      g.maxY = bottom;
    }
    specs.push_back(std::move(g));
  }
  return Font::fromBytes(buildMvec(std::move(specs)), std::move(name));
}

namespace {

// Built-in fallback font: 5x7 pixel patterns, top row first, one 12-unit square per set pixel
// (advance 6 pixels, ascent 84, descent 12 from the comma). Lower case letters reuse the capitals; U+FFFD is a
// hollow box.
struct Pattern {
  std::uint32_t cp;
  const char* rows;  // seven rows of five '0'/'1'
};
constexpr Pattern kPatterns[] = {
    {48, "01110100011001110101110011000101110"},
    {49, "00100011000010000100001000010001110"},
    {50, "01110100010000100010001000100011111"},
    {51, "11110000010000101110000010000111110"},
    {52, "00010001100101010010111110001000010"},
    {53, "11111100001111000001000011000101110"},
    {54, "00110010001000011110100011000101110"},
    {55, "11111000010001000100010000100001000"},
    {56, "01110100011000101110100011000101110"},
    {57, "01110100011000101111000010001001100"},
    {65, "01110100011000111111100011000110001"},
    {66, "11110100011000111110100011000111110"},
    {67, "01110100011000010000100001000101110"},
    {68, "11110100011000110001100011000111110"},
    {69, "11111100001000011110100001000011111"},
    {70, "11111100001000011110100001000010000"},
    {71, "01110100011000010111100011000101111"},
    {72, "10001100011000111111100011000110001"},
    {73, "01110001000010000100001000010001110"},
    {74, "00111000100001000010000101001001100"},
    {75, "10001100101010011000101001001010001"},
    {76, "10000100001000010000100001000011111"},
    {77, "10001110111010110101100011000110001"},
    {78, "10001110011010110011100011000110001"},
    {79, "01110100011000110001100011000101110"},
    {80, "11110100011000111110100001000010000"},
    {81, "01110100011000110001101011001001101"},
    {82, "11110100011000111110101001001010001"},
    {83, "01111100001000001110000010000111110"},
    {84, "11111001000010000100001000010000100"},
    {85, "10001100011000110001100011000101110"},
    {86, "10001100011000110001100010101000100"},
    {87, "10001100011000110101101011010101010"},
    {88, "10001100010101000100010101000110001"},
    {89, "10001100010101000100001000010000100"},
    {90, "11111000010001000100010001000011111"},
    {46, "00000000000000000000000000000000100"},
    {33, "00100001000010000100001000000000100"},
    {63, "01110100010000100010001000000000100"},
    {45, "00000000000000011111000000000000000"},
    {39, "00100001000000000000000000000000000"},
    {43, "00000001000010011111001000010000000"},
    {47, "00001000100001000100010000100010000"},
    {58, "00000001000000000000000000010000000"},
    {34, "01010010100000000000000000000000000"},
    {44, "00000000000000000000000000010001000"},
    {0xFFFD, "11111100011000110001100011000111111"},
};

}  // namespace


std::shared_ptr<const Font> builtinFont() {
  static const std::shared_ptr<const Font> font = [] {
    std::vector<GlyphSpec> specs;
    auto addGlyph = [&](std::uint32_t cp, const char* rows) {
      GlyphSpec g;
      g.cp = cp;
      g.advance = 72;
      if (rows) {
        for (int r = 0; r < 7; ++r) {
          int c = 0;
          while (c < 5) {
            if (rows[r * 5 + c] != '1') {
              ++c;
              continue;
            }
            int e = c;
            while (e < 5 && rows[r * 5 + e] == '1') ++e;
            Contour k;
            const float x0 = static_cast<float>(c * 12 + 6), x1 = static_cast<float>(e * 12 + 6);
            const float y0 = static_cast<float>((r - 7) * 12), y1 = y0 + 12;
            k.start = {x0, y0};
            k.segs = {{false, {}, {x1, y0}}, {false, {}, {x1, y1}}, {false, {}, {x0, y1}}};
            g.contours.push_back(k);
            c = e;
          }
        }
      }
      if (cp == ',') {  // the tail dips below the baseline: this gives the font its descent
        Contour k;
        k.start = {30, 0};
        k.segs = {{false, {}, {42, 0}}, {false, {}, {42, 12}}, {false, {}, {30, 12}}};
        g.contours.push_back(k);
      }
      specs.push_back(std::move(g));
    };
    addGlyph(' ', nullptr);
    for (const Pattern& p : kPatterns) {
      addGlyph(p.cp, p.rows);
      if (p.cp >= 'A' && p.cp <= 'Z') addGlyph(p.cp + 32, p.rows);
    }
    return Font::fromBytes(buildMvec(std::move(specs)), "__DEFAULT");
  }();
  return font;
}

std::shared_ptr<const Font> FontCache::builtin() { return builtinFont(); }

std::shared_ptr<const Font> FontCache::defaultFont() { return get("fonts/default.mvec"); }

std::shared_ptr<const Font> FontCache::get(std::string_view name) {
  std::string key = assets::AssetFS::normalize(name);
  if (key.empty()) key = std::string(name);
  if (auto it = cache_.find(key); it != cache_.end()) return it->second;

  std::shared_ptr<const Font> font;
  if (name != "__DEFAULT" && fs_) {
    auto tryLoad = [&](std::string_view path) -> std::shared_ptr<const Font> {
      auto bytes = fs_->readFile(path);
      if (!bytes) return nullptr;
      std::string err;
      auto f = Font::fromBytes(std::move(*bytes), std::string(path), &err);
      if (!f) diag::err("[font] '%.*s' is not a usable font: %s\n", static_cast<int>(path.size()), path.data(), err.c_str());
      return f;
    };
    font = tryLoad(name);
    if (!font) {
      diag::err("[font] '%.*s' not found, using fonts/default.mvec or the built-in font\n",
                   static_cast<int>(name.size()), name.data());
      if (key != "fonts/default.mvec") font = tryLoad("fonts/default.mvec");
    }
  }
  if (!font) font = embedded_ ? embedded_ : builtinFont();
  cache_[key] = font;
  return font;
}

}  // namespace choco::gfx::text
