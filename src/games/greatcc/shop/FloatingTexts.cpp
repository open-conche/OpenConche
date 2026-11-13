#include "games/greatcc/shop/FloatingTexts.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ui/DrawContext.h"

namespace choco::games::greatcc {

namespace {
std::string wrap(const std::string& text, const char* outline) {
  return std::string("<outline color='") + outline + "' size='2'><b>" + text + "</b></outline>";
}
}  // namespace

FloatingText::FloatingText(Kind kind, gfx::text::TextSystem* text, std::string markup, int x, int y, int w, int h, float size,
                           gfx::ColorF color, float alpha)
    : kind_(kind), text_(text), markup_(std::move(markup)), x_(x), y_(y), w_(w), h_(h), startX_(x), startY_(y), size_(size), color_(color),
      alpha_(alpha) {}

bool FloatingText::update(double dtMs) {
  const unsigned t = elapsed_ + static_cast<unsigned>(dtMs);
  elapsed_ = t;
  // thresholds: the rise ends at 501 ms (every kind), the hold at 1001 (payment), 1501 (combo) or 1701 (goal), the end at 1501, 2001, 2001
  const unsigned holdEnd = kind_ == Kind::Payment ? 1001u : (kind_ == Kind::Combo ? 1501u : 1701u);
  const unsigned end = kind_ == Kind::Payment ? 1501u : 2001u;
  if (t >= end) return false;
  if (t < holdEnd) {
    if (t < 501u) {
      const float rise = kind_ == Kind::Goal ? 100.0f : 75.0f;
      x_ = startX_;
      y_ = startY_ - static_cast<int>((static_cast<float>(t) * rise) / 500.0f);
      alpha_ = static_cast<float>(t) / 500.0f;
    }
  } else {
    float a;
    if (kind_ == Kind::Payment) a = static_cast<float>(t - 1000u) / -500.0f + 1.0f;
    else if (kind_ == Kind::Combo) a = static_cast<float>(t - 1500u) / -500.0f + 1.0f;
    else a = static_cast<float>(2000 - static_cast<int>(t)) / 500.0f;
    alpha_ = a <= 0.0f ? 0.0f : a;
    y_ -= 1;
  }
  return true;
}

void FloatingText::draw(ui::DrawContext& ctx) const {
  if (!text_ || alpha_ <= 0.0f) return;
  if (!layout_) {
    gfx::text::TextStyle st;
    st.fonts = &text_->fonts;
    // The original names "edwardian_medium.mvec" without its folder, a file that does not exist, so the three floating
    // texts are drawn in the font at the end of the chain: the one in the executable (GreatCCWorld sets it).
    st.font = text_->fonts.get("edwardian_medium.mvec");
    st.size = size_;
    st.color = color_;
    st.flags = gfx::text::kHAlignCenter;
    layout_ = std::make_unique<gfx::text::TextLayout>(gfx::text::TextLayout::build(markup_, st, w_, h_));
  }
  const gfx::PointF origin{ctx.origin.x + static_cast<float>(x_) - static_cast<float>(w_) / 2.0f,
                           ctx.origin.y + static_cast<float>(y_) - static_cast<float>(h_) / 2.0f};
  layout_->draw(ctx.renderer, text_->atlas, origin, alpha_ * ctx.alpha);
}

std::unique_ptr<FloatingText> makePaymentText(gfx::text::TextSystem* text, float amount, int x, int y) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "$%.2i", static_cast<int>(amount));
  return std::make_unique<FloatingText>(FloatingText::Kind::Payment, text, wrap(buf, "000000"), x, y, 150, 30, 25.0f,
                                        gfx::ColorF{1, 1, 1, 1}, 1.0f);
}

std::unique_ptr<FloatingText> makeComboText(gfx::text::TextSystem* text, float bonus, int chain, const std::string& label, int x, int y) {
  char buf[48];
  std::snprintf(buf, sizeof buf, " x%i\n+$%i", chain, static_cast<int>(bonus));
  return std::make_unique<FloatingText>(FloatingText::Kind::Combo, text, wrap(label + buf, "000000"), x, y, 150, 36, 18.0f,
                                        gfx::ColorF{1, 1, 0, 1}, 1.0f);
}

std::unique_ptr<FloatingText> makeGoalText(gfx::text::TextSystem* text, int which, const std::string& label) {
  const gfx::ColorF color = which == 1 ? gfx::ColorF{0.2539f, 0.69921875f, 0.1953125f, 1} : gfx::ColorF{0.8125f, 0.7226562f, 0.109375f, 1};
  return std::make_unique<FloatingText>(FloatingText::Kind::Goal, text, wrap(label, "ffffff"), 400, 250, 800, 50, 46.0f, color, 0.0f);
}

}  // namespace choco::games::greatcc
