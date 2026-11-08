#include "games/choc2/FactoryStatus.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ui/DrawContext.h"
#include "ui/Style.h"

namespace choco::game::choc2 {

namespace {

// Colours for a style that gives none: the greens, yellows and reds of the ledger's price text.
constexpr gfx::ColorF kDefaults[4] = {{0.29f, 0.63f, 0.03f, 1}, {0.93f, 0.78f, 0.12f, 1}, {0.93f, 0.78f, 0.12f, 1}, {0.80f, 0.10f, 0.10f, 1}};

gfx::ColorF readColor(const ui::Style& style, const char* key, const gfx::ColorF& def) {
  if (auto v = style.numbers(key, 4, 1.0))
    return {static_cast<float>((*v)[0]), static_cast<float>((*v)[1]), static_cast<float>((*v)[2]), static_cast<float>((*v)[3])};
  return def;
}

}  // namespace

int FactoryStatus::band(double fill) {
  if (fill >= 1.0) return 0;
  if (fill > 0.75) return 1;
  if (fill > 0.5) return 2;
  return 3;
}

int FactoryStatus::periodMs(double rate) {
  if (rate < 1) return 0;
  if (rate <= 20) return 1000;
  if (rate <= 40) return 667;
  if (rate <= 60) return 500;
  return 400;
}

bool FactoryStatus::onAnimate() {
  elapsedMs_ += kTickMs;
  return true;
}

float FactoryStatus::fillHeight(const gfx::RectF& rect, double fill) {
  return static_cast<float>(std::clamp(fill, 0.0, 1.0)) * rect.h;
}

void FactoryStatus::init(const ui::Style& style) {
  Window::init(style);
  const char* keys[4] = {"colorLoads", "colorFull", "colorMedium", "colorTrouble"};
  for (int i = 0; i < 4; ++i) colors_[i] = readColor(style, keys[i], kDefaults[i]);

  bars_.clear();
  if (auto t = style.table("bars")) {
    for (int i = 1; i <= t->size(); ++i) {
      auto e = t->element(i);
      if (!e) continue;
      const float l = static_cast<float>(e->numberAt(1).value_or(0)), top = static_cast<float>(e->numberAt(2).value_or(0));
      const float r = static_cast<float>(e->numberAt(3).value_or(0)), b = static_cast<float>(e->numberAt(4).value_or(0));
      bars_.push_back({{l, top, std::max(0.0f, r - l), std::max(0.0f, b - top)}, e->numberAt(5).value_or(0)});
    }
  }

  products_.clear();
  pics_.clear();
  missed_ = tex_.texture("image/missed_ingredient");
  if (auto t = style.table("products")) {
    for (int i = 1; i <= t->size(); ++i) {
      auto e = t->element(i);
      if (!e) continue;
      Product p;
      p.at = {static_cast<float>(e->numberAt(1).value_or(0)), static_cast<float>(e->numberAt(2).value_or(0))};
      p.name = e->stringAt(3).value_or("");
      p.rate = e->numberAt(4).value_or(0);
      p.type = e->stringAt(5).value_or("");
      p.recipeSize = static_cast<int>(e->numberAt(6).value_or(0));
      products_.push_back(p);
      // A miss is logged once.
      pics_.push_back({tex_.texture("image/tray" + std::to_string(p.recipeSize) + "_mini"),
                       tex_.texture("image/machine_" + p.type + "_small"), tex_.texture("item/" + p.name)});
    }
  }
}

void FactoryStatus::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  manager().startWindowAnimation(id(), kTickMs);
}

void FactoryStatus::draw(ui::DrawContext& ctx) const {
  for (const Bar& b : bars_) {
    const float h = fillHeight(b.rect, b.fill);
    if (h <= 0 || b.rect.w <= 0) continue;
    gfx::ColorF c = colors_[band(b.fill)];
    c.a *= ctx.alpha;
    ctx.renderer.fillRect({ctx.origin.x + b.rect.x, ctx.origin.y + b.rect.y + b.rect.h - h, b.rect.w, h}, c, gfx::Blend::Normal);
  }
  const float bottom = static_cast<float>(rect().h) - 16.0f;  // where the product lands: 16 px above the panel's bottom
  for (std::size_t i = 0; i < products_.size(); ++i) {
    const Product& p = products_[i];
    const Pictures& pic = pics_[i];
    const int period = periodMs(p.rate);
    const double t = period > 0 ? std::fmod(elapsedMs_, period) / period : 0;
    auto put = [&](const gfx::Texture& tex, float cx, float cy, float alpha, float degrees) {
      const float w = static_cast<float>(tex.width()), h = static_cast<float>(tex.height());
      ctx.renderer.drawTexture(tex, {}, {ctx.origin.x + cx - w / 2, ctx.origin.y + cy - h / 2, w, h},
                               {1, 1, 1, alpha * ctx.alpha}, gfx::Blend::Normal, degrees, {w / 2, h / 2});
    };
    if (pic.item && period > 0) {
      if (animating_)
        put(*pic.item, p.at.x, p.at.y + (bottom - p.at.y) * static_cast<float>(t), 0.5f + 0.5f * static_cast<float>(t), 0);
      else
        put(*pic.item, p.at.x, bottom, 1, 0);
    }
    if (pic.machine) put(*pic.machine, p.at.x, p.at.y, 1, 0);
    if (pic.tray) put(*pic.tray, p.at.x, p.at.y, 1, animating_ ? static_cast<float>(t * 360.0) : 0);
    // An idle factory (rate 0) blinks its missing-ingredient picture in the second half of each second.
    if (period == 0 && missed_ && std::fmod(elapsedMs_, 1000.0) >= 500) put(*missed_, p.at.x, p.at.y, 0.65f, 0);
  }
}

std::string FactoryStatus::detail() const {
  char buf[64];
  std::snprintf(buf, sizeof buf, " bars=%zu products=%zu", bars_.size(), products_.size());
  return buf;
}

}  // namespace choco::game::choc2
