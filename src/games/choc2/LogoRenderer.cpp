#include "games/choc2/LogoRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "gfx/text/GlyphAtlas.h"
#include "gfx/text/Raster.h"

namespace choco::game::choc2 {

namespace {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

// One straight-alpha pixel composited over the canvas.
void over(gfx::ImageData& canvas, int x, int y, float r, float g, float b, float a) {
  if (x < 0 || y < 0 || x >= canvas.width || y >= canvas.height || a <= 0) return;
  std::uint8_t* d = &canvas.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(canvas.width) + static_cast<std::size_t>(x)) * 4];
  const float da = d[3] / 255.0f;
  const float oa = a + da * (1 - a);
  if (oa <= 0) return;
  const float src[3] = {r, g, b};
  for (int c = 0; c < 3; ++c) {
    const float v = (src[c] * a + (d[c] / 255.0f) * da * (1 - a)) / oa;
    d[c] = static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
  }
  d[3] = static_cast<std::uint8_t>(std::lround(std::clamp(oa, 0.0f, 1.0f) * 255.0f));
}

// Bilinear sample of straight-alpha pixels; outside the image is transparent, so a turned edge is soft.
void sample(const gfx::ImageData& img, float u, float v, float out[4]) {
  const float fx = u - 0.5f, fy = v - 0.5f;
  const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
  const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
  float acc[4] = {0, 0, 0, 0};  // premultiplied
  for (int j = 0; j < 2; ++j)
    for (int i = 0; i < 2; ++i) {
      const int px = x0 + i, py = y0 + j;
      if (px < 0 || py < 0 || px >= img.width || py >= img.height) continue;
      const float w = (i ? tx : 1 - tx) * (j ? ty : 1 - ty);
      const std::uint8_t* p = &img.rgba[(static_cast<std::size_t>(py) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(px)) * 4];
      const float a = p[3] / 255.0f;
      acc[0] += w * a * p[0] / 255.0f;
      acc[1] += w * a * p[1] / 255.0f;
      acc[2] += w * a * p[2] / 255.0f;
      acc[3] += w * a;
    }
  out[3] = acc[3];
  for (int c = 0; c < 3; ++c) out[c] = acc[3] > 0 ? acc[c] / acc[3] : 0;
}

void drawGraphic(gfx::ImageData& canvas, const gfx::ImageData& img, const GraphicPlacement& g) {
  if (g.w <= 0 || g.h <= 0 || g.alpha <= 0) return;
  const float cx = g.x + g.w * 0.5f, cy = g.y + g.h * 0.5f;
  const float cs = std::cos(g.rotationDeg * kDegToRad), sn = std::sin(g.rotationDeg * kDegToRad);
  // The turned rectangle's bounds, to limit the scan.
  const float hw = g.w * 0.5f, hh = g.h * 0.5f;
  const float ex = std::abs(cs) * hw + std::abs(sn) * hh, ey = std::abs(sn) * hw + std::abs(cs) * hh;
  const int x0 = std::max(0, static_cast<int>(std::floor(cx - ex)) - 1), x1 = std::min(canvas.width, static_cast<int>(std::ceil(cx + ex)) + 1);
  const int y0 = std::max(0, static_cast<int>(std::floor(cy - ey)) - 1), y1 = std::min(canvas.height, static_cast<int>(std::ceil(cy + ey)) + 1);
  const float sx = static_cast<float>(img.width) / g.w, sy = static_cast<float>(img.height) / g.h;
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) {
      const float dx = static_cast<float>(x) + 0.5f - cx, dy = static_cast<float>(y) + 0.5f - cy;
      const float lx = dx * cs + dy * sn, ly = -dx * sn + dy * cs;  // back into the unturned rectangle
      float px[4];
      sample(img, (lx + hw) * sx, (ly + hh) * sy, px);
      over(canvas, x, y, px[0], px[1], px[2], px[3] * g.alpha);
    }
}

// A glyph's coverage laid on the canvas in one colour. (ox, oy) is the glyph origin on the canvas.
void blit(gfx::ImageData& canvas, const gfx::text::Bitmap& bm, int ox, int oy, const gfx::ColorF& c) {
  if (bm.empty()) return;
  for (int y = 0; y < bm.h; ++y)
    for (int x = 0; x < bm.w; ++x) {
      const std::uint8_t cov = bm.at(x, y);
      if (cov) over(canvas, ox + bm.x + x, oy + bm.y + y, c.r, c.g, c.b, c.a * cov / 255.0f);
    }
}

void drawText(gfx::ImageData& canvas, const TextBlock& block, const gfx::ColorF& shadow) {
  using gfx::text::RasterParams;
  const auto runs = block.layout.glyphRuns(block.origin);
  // Passes: the shadow (outline then fill), then the outline, then the fill, as TextLayout::draw orders its two passes.
  for (int pass = 0; pass < 3; ++pass) {
    const bool isShadow = pass == 0;
    for (const auto& r : runs) {
      if (!r.font) continue;
      const long q = std::lround(r.x * 4.0f);
      const long ix = q >= 0 ? q / 4 : -((-q + 3) / 4);
      RasterParams p;
      p.scale = r.font->pixelScale(r.size);
      p.subX = static_cast<int>(q - ix * 4);
      p.italic = r.italic;
      const float boldHalf = r.bold ? r.size * 0.0125f : 0.0f;
      const int ox = static_cast<int>(ix) + (isShadow ? 1 : 0), oy = r.baselineY + (isShadow ? 1 : 0);
      if (r.outline > 0 && pass != 2) {
        p.stroke = static_cast<float>(r.outline) + boldHalf;
        gfx::ColorF c = isShadow ? shadow : r.outlineColor;
        if (!isShadow) c.a *= r.color.a;
        blit(canvas, gfx::text::rasterizeGlyph(*r.font, r.glyph, p), ox, oy, c);
      }
      if (pass == 0 || pass == 2) {
        p.stroke = boldHalf;
        blit(canvas, gfx::text::rasterizeGlyph(*r.font, r.glyph, p), ox, oy, isShadow ? shadow : r.color);
      }
    }
  }
}

}  // namespace

const gfx::ImageData* LogoImages::get(const std::string& name) {
  if (auto it = images_.find(name); it != images_.end()) return it->second.get();
  std::unique_ptr<gfx::ImageData> img;
  if (textures_) {
    auto loaded = std::make_unique<gfx::ImageData>();
    if (textures_->loadImage(name, *loaded)) img = std::move(loaded);
  }
  return (images_[name] = std::move(img)).get();
}

LogoAssets LogoImages::assets(gfx::text::FontCache* fonts) {
  LogoAssets a;
  a.image = [this](const std::string& name) { return get(name); };
  a.fonts = fonts;
  return a;
}

gfx::text::FontCache* logoFonts() {
  gfx::text::TextSystem* ts = gfx::text::TextSystem::current();
  return ts ? &ts->fonts : nullptr;
}

GraphicPlacement graphicPlacement(const LogoSpec& spec, int sourceW, int sourceH) {
  GraphicPlacement g;
  const float k = static_cast<float>(spec.ls) / 1000.0f;
  g.w = static_cast<float>(sourceW) * k;
  g.h = static_cast<float>(sourceH) * k;
  g.x = static_cast<float>(spec.lx) / 1000.0f * std::max(0.0f, static_cast<float>(LogoSpec::kCanvasW) - g.w);
  g.y = static_cast<float>(spec.ly) / 1000.0f * std::max(0.0f, static_cast<float>(LogoSpec::kCanvasH) - g.h);
  g.rotationDeg = static_cast<float>(spec.lr) / 1000.0f * 360.0f;
  g.alpha = static_cast<float>(spec.la) / 1000.0f;
  return g;
}

gfx::ColorF logoTextColor(const LogoSpec& spec) {
  const Rgb8 c = hsvToRgb(spec.tch, spec.tcs, spec.tcv);
  return {static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f, static_cast<float>(c.b) / 255.0f,
          static_cast<float>(spec.ta) / 1000.0f};
}

TextBlock layoutLogoText(const LogoSpec& spec, gfx::text::FontCache* fonts) {
  TextBlock out;
  if (spec.t.empty() || !fonts) return out;
  gfx::text::TextStyle style;
  const auto& names = logoFontNames();
  style.font = fonts->get("fonts/" + names[static_cast<std::size_t>(std::clamp(spec.lf, 0, static_cast<int>(names.size()) - 1))] + ".mvec");
  style.size = static_cast<float>(spec.ts);
  style.color = logoTextColor(spec);
  style.flags = spec.to;
  style.fonts = fonts;
  std::string markup;
  if (spec.tfo > 0) markup += "<outline size=" + std::to_string(kLogoOutlinePx) + ">";
  if (spec.tfb > 0) markup += "<b>";
  if (spec.tfi > 0) markup += "<i>";
  for (char c : spec.t) markup += c == '<' ? ' ' : c;  // no markup from the player's text
  out.layout = gfx::text::TextLayout::build(markup, style, kLogoTextBoxW, LogoSpec::kCanvasH);
  const gfx::RectI tight = out.layout.bounds();
  const float freeW = std::max(0.0f, static_cast<float>(LogoSpec::kCanvasW - tight.w));
  const float freeH = std::max(0.0f, static_cast<float>(LogoSpec::kCanvasH - tight.h));
  const float left = static_cast<float>(spec.tx) / 1000.0f * freeW, top = static_cast<float>(spec.ty) / 1000.0f * freeH;
  out.origin = {std::floor(left + 0.5f) - static_cast<float>(tight.x), std::floor(top + 0.5f) - static_cast<float>(tight.y)};
  out.bounds = {static_cast<int>(out.origin.x) + tight.x, static_cast<int>(out.origin.y) + tight.y, tight.w, tight.h};
  return out;
}

gfx::ImageData renderLogo(const LogoSpec& spec, const LogoAssets& assets) {
  gfx::ImageData canvas;
  canvas.width = LogoSpec::kCanvasW;
  canvas.height = LogoSpec::kCanvasH;
  canvas.rgba.assign(static_cast<std::size_t>(canvas.width) * static_cast<std::size_t>(canvas.height) * 4, 0);

  if (assets.image) {
    if (const gfx::ImageData* bg = assets.image(logoBackgroundPath(spec.b)); bg && bg->width > 0 && bg->height > 0) {
      // Backgrounds are 350x200; anything else is stretched to the canvas.
      drawGraphic(canvas, *bg, {0, 0, static_cast<float>(canvas.width), static_cast<float>(canvas.height), 0, 1});
    }
    if (const gfx::ImageData* g = assets.image(spec.l); g && g->width > 0 && g->height > 0)
      drawGraphic(canvas, *g, graphicPlacement(spec, g->width, g->height));
  }
  const TextBlock block = layoutLogoText(spec, assets.fonts);
  drawText(canvas, block, {0, 0, 0, static_cast<float>(spec.ta) / 1000.0f});
  return canvas;
}

}  // namespace choco::game::choc2
