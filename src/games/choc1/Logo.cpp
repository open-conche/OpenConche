// Chocolatier 1's Logo window (TLogo): the composite of a background, a graphic and the company text, rendered
// once at init into an offscreen canvas (the exe's FUN_004155f0 through the render target of FUN_0042ae10) and
// blitted over the window rect (the vtable Draw 00415410). Facts in docs/re/choc1-logo-and-hiscore.md.

#include "games/choc1/Logo.h"

#include <algorithm>
#include <cstdint>

#include "gfx/text/GlyphAtlas.h"
#include "gfx/text/Raster.h"

namespace choco::game::choc1 {

namespace {

// One straight-alpha pixel composited over the canvas.
void over(gfx::ImageData& canvas, int x, int y, float r, float g, float b, float a) {
  if (x < 0 || y < 0 || x >= canvas.width || y >= canvas.height || a <= 0) return;
  std::uint8_t* d = &canvas.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(canvas.width) +
                                 static_cast<std::size_t>(x)) * 4];
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

// Bilinear sample of straight-alpha pixels; outside the image is transparent.
void sample(const gfx::ImageData& img, float u, float v, float out[4]) {
  const float fx = u - 0.5f, fy = v - 0.5f;
  const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
  const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
  float acc[4] = {0, 0, 0, 0};
  for (int j = 0; j < 2; ++j)
    for (int i = 0; i < 2; ++i) {
      const int px = x0 + i, py = y0 + j;
      if (px < 0 || py < 0 || px >= img.width || py >= img.height) continue;
      const float w = (i ? tx : 1 - tx) * (j ? ty : 1 - ty);
      const std::uint8_t* p =
          &img.rgba[(static_cast<std::size_t>(py) * static_cast<std::size_t>(img.width) +
                     static_cast<std::size_t>(px)) * 4];
      const float a = p[3] / 255.0f;
      acc[0] += w * a * p[0] / 255.0f;
      acc[1] += w * a * p[1] / 255.0f;
      acc[2] += w * a * p[2] / 255.0f;
      acc[3] += w * a;
    }
  out[3] = acc[3];
  for (int c = 0; c < 3; ++c) out[c] = acc[3] > 0 ? acc[c] / acc[3] : 0;
}

// An image stretched over the rectangle centred at (cx, cy) with the size (w, h): TLogo draws each picture
// through the image manager's Draw, whose visible extent is the natural size scaled by s (the background covers
// the whole canvas this way; the graphic is centred by (gx, gy), both facts of the redraw 004155f0).
void drawStretch(gfx::ImageData& canvas, const gfx::ImageData& img, float cx, float cy, float w, float h) {
  if (w <= 0 || h <= 0) return;
  const int x0 = std::max(0, static_cast<int>(std::floor(cx - w * 0.5f)));
  const int x1 = std::min(canvas.width, static_cast<int>(std::ceil(cx + w * 0.5f)));
  const int y0 = std::max(0, static_cast<int>(std::floor(cy - h * 0.5f)));
  const int y1 = std::min(canvas.height, static_cast<int>(std::ceil(cy + h * 0.5f)));
  const float sx = static_cast<float>(img.width) / w, sy = static_cast<float>(img.height) / h;
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) {
      float px[4];
      sample(img, (static_cast<float>(x) + 0.5f - (cx - w * 0.5f)) * sx,
             (static_cast<float>(y) + 0.5f - (cy - h * 0.5f)) * sy, px);
      over(canvas, x, y, px[0], px[1], px[2], px[3]);
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

}  // namespace

LogoWindow::LogoWindow(ui::WindowManager& wm) : ui::Window(wm) {}

void LogoWindow::init(const ui::Style& style) {
  // TLogo::Init (00415a90) fixes the size from the style first (FUN_00421d20), then reads its keys, builds the
  // canvas (FUN_0042ae10 of round(350*s) x round(200*s)) and ends with the one-time render (FUN_004155f0).
  Window::init(style);
  keys_.background = style.stringOr("background", "logo/bg1");
  keys_.graphic = style.stringOr("graphic", "logo/logo1");
  keys_.text = style.stringOr("text", "");
  keys_.flags = static_cast<int>(style.numberOr("flags", 5));
  keys_.textX = static_cast<int>(style.numberOr("textX", 10));
  keys_.textY = static_cast<int>(style.numberOr("textY", 10));
  keys_.textW = static_cast<int>(style.numberOr("textW", 330));
  keys_.textH = static_cast<int>(style.numberOr("textH", 180));
  keys_.gx = static_cast<float>(style.numberOr("gx", 0));
  keys_.gy = static_cast<float>(style.numberOr("gy", 0));
  keys_.font = style.stringOr("font", "");
  keys_.fontSize = static_cast<int>(style.numberOr("fontSize", 30));
  const auto color = [&style](const char* key, gfx::ColorF def) {
    gfx::ColorF c = def;
    if (auto v = style.numbers(key, 4, 0.0))
      c = {static_cast<float>((*v)[0]), static_cast<float>((*v)[1]), static_cast<float>((*v)[2]),
           static_cast<float>((*v)[3])};
    return c;
  };
  keys_.fontColor = color("fontColor", {1, 1, 1, 1});
  keys_.dropColor = color("dropColor", {0, 0, 0, 1});

  Assets assets;
  if (gfx::TextureCache* textures = manager().textures()) {
    (void)textures->loadImage(keys_.background, backgroundImage_);
    (void)textures->loadImage(keys_.graphic, graphicImage_);
    assets.background = backgroundImage_.width > 0 ? &backgroundImage_ : nullptr;
    assets.graphic = graphicImage_.width > 0 ? &graphicImage_ : nullptr;
    if (gfx::text::TextSystem* ts = gfx::text::TextSystem::current()) assets.fonts = &ts->fonts;
  }

  gfx::ImageData canvas;
  canvas.width = canvasW(rect().w);
  canvas.height = canvasH(rect().w);
  canvas.rgba.assign(static_cast<std::size_t>(canvas.width) * static_cast<std::size_t>(canvas.height) * 4, 0);
  render(keys_, assets, canvas);
  if (gfx::TextureCache* textures = manager().textures())
    texture_ = textures->makeFromPixels("$logo-" + std::to_string(id()), canvas);
}

void LogoWindow::draw(ui::DrawContext& ctx) const {
  // The vtable Draw (00415410): the whole canvas over the window's rect.
  if (!texture_ || rect().w <= 0 || rect().h <= 0) return;
  ctx.renderer.drawTexture(*texture_, {}, {static_cast<float>(ctx.origin.x), static_cast<float>(ctx.origin.y),
                                           static_cast<float>(rect().w), static_cast<float>(rect().h)},
                           gfx::ColorF{1, 1, 1, ctx.alpha}, gfx::Blend::Normal);
}

void LogoWindow::render(const Keys& k, const Assets& assets, gfx::ImageData& canvas) {
  const float s = static_cast<float>(canvas.width) / 350.0f;
  if (assets.background)
    drawStretch(canvas, *assets.background, canvas.width * 0.5f, canvas.height * 0.5f,
                static_cast<float>(canvas.width), static_cast<float>(canvas.height));
  if (assets.graphic)
    drawStretch(canvas, *assets.graphic, (assets.graphic->width * 0.5f + k.gx) * s,
                (assets.graphic->height * 0.5f + k.gy) * s, assets.graphic->width * s, assets.graphic->height * s);
  if (k.text.empty() || !assets.fonts) return;

  // FUN_0044ad20 built one TextGraphic per colour: the box is (round(textW*s+0.5), round(textH*s+0.5)) laid out
  // with `flags`, and the graphic is drawn at (round(textX*s+0.5), round(textY*s+0.5)) - Init 00415a90 OVERWRITES the
  // raw textX/textY/textW/textH keys with their scaled, rounded values right after building the canvas (the stores at
  // 0x415f84..0x416016; a first reading kept the raw keys and drew the box's top-left unscaled, which put the factory
  // sign's text bottom-right - the Playtesters' report of the sign being 24 px off; the scaled origin centres it).
  // The drop copy at +1,+1 (plus one at +2,+2 when s > 0.5) comes before the text in fontColor.
  gfx::text::TextStyle ts;
  ts.font = assets.fonts->get(k.font);
  ts.size = static_cast<float>(scaled(k.fontSize, s));
  ts.color = k.fontColor;
  ts.flags = k.flags;
  ts.fonts = assets.fonts;
  ts.keepLastOverflowChar = true;
  const gfx::text::TextLayout layout =
      gfx::text::TextLayout::build(k.text, ts, scaled(k.textW, s), scaled(k.textH, s));
  const auto runs = layout.glyphRuns({static_cast<float>(scaled(k.textX, s)), static_cast<float>(scaled(k.textY, s))});
  const int passes = 2 + (s > 0.5f ? 1 : 0);
  using gfx::text::RasterParams;
  for (int pass = 0; pass < passes; ++pass) {
    int dx = 0, dy = 0;
    gfx::ColorF fill = k.fontColor;
    if (pass < passes - 1) {  // the drop passes: +2,+2 first, then +1,+1
      dx = pass + 1;
      dy = pass + 1;
      fill = k.dropColor;
    }
    for (const auto& r : runs) {
      if (!r.font) continue;
      const long q = std::lround(r.x * 4.0f);
      const long ix = q >= 0 ? q / 4 : -((-q + 3) / 4);
      RasterParams p;
      p.scale = r.font->pixelScale(r.size);
      p.subX = static_cast<int>(q - ix * 4);
      p.italic = r.italic;
      const float boldHalf = r.bold ? r.size * 0.0125f : 0.0f;
      const int ox = static_cast<int>(ix) + dx, oy = r.baselineY + dy;
      if (r.outline > 0) {
        p.stroke = static_cast<float>(r.outline) + boldHalf;
        blit(canvas, gfx::text::rasterizeGlyph(*r.font, r.glyph, p), ox, oy, fill);
      }
      p.stroke = boldHalf;
      blit(canvas, gfx::text::rasterizeGlyph(*r.font, r.glyph, p), ox, oy, fill);
    }
  }
}

std::string LogoWindow::detail() const {
  char buf[160];
  std::snprintf(buf, sizeof buf, " scale=%.4f pos=%.0f,%.0f text=\"%s\"", static_cast<float>(rect().w) / 350.0f,
                keys_.gx, keys_.gy, keys_.text.c_str());
  return std::string(" bg=") + keys_.background + " graphic=" + keys_.graphic + buf;
}

}  // namespace choco::game::choc1
