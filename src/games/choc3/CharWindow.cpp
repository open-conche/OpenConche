#include "games/choc3/CharWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"
#include "ui/DrawContext.h"
#include "ui/WindowManager.h"

namespace choco::game {

int CharWindow::moodFrame(int n, double h) {
  if (n < 2) return -1;
  if (n == 3) {  // unused by shipped data, kept as the original has it
    const int i = static_cast<int>(h * 3 / 100);
    return i == 0 ? 1 : (i == 1 ? 0 : 2);
  }
  const int last = n - 1;
  const int i = static_cast<int>(last * h / 100) + 1;
  return std::min(i, last);
}

void CharWindow::postChildrenInit(const ui::Style& style) {
  BSGWindow::postChildrenInit(style);
  charName_ = style.stringOr("name");
  setAlpha(static_cast<float>(style.numberOr("alpha", 1)));
  scale_ = static_cast<float>(style.numberOr("scale", 1));
  const double happiness = style.numberOr("happiness", 50);

  const std::string base = "characters/" + charName_;
  if (gfx::SheetCache* sheets = manager().sheets()) sheet_ = sheets->get(base + ".xml");
  if (!sheet_ && manager().textures()) {
    texture_ = manager().textures()->load(base + ".png");
    if (!texture_) texture_ = manager().textures()->load("characters/temp_char");
  }
  int w = 0, h = 0;
  if (sheet_ && !sheet_->frames.empty()) {
    w = sheet_->frames[0].src.w;
    h = sheet_->frames[0].src.h;
  } else if (texture_) {
    w = texture_->width();
    h = texture_->height();
  }
  if (scale_ != 1) {
    w = static_cast<int>(static_cast<float>(w) * scale_ + 0.5f);
    h = static_cast<int>(static_cast<float>(h) * scale_ + 0.5f);
  }
  setSize(std::max(rect().w, w), std::max(rect().h, h));

  setMood(happiness);
  fade_.snap();  // no fade at creation
  manager().stopWindowAnimation(id());
}

void CharWindow::setMood(double h) {
  if (!sheet_) return;
  const int t = moodFrame(static_cast<int>(sheet_->frames.size()), h);
  if (t < 0) return;
  if (fade_.setTarget(t)) manager().startWindowAnimation(id(), kTickMs);
}

bool CharWindow::onAnimate() {
  return fade_.tick();
}

void CharWindow::draw(ui::DrawContext& ctx) const {
  gfx::Renderer& r = ctx.renderer;
  const float alpha = ctx.alpha;
  if (alpha <= 0) return;
  if (sheet_ && sheet_->image && !sheet_->frames.empty()) {
    const auto& f0 = sheet_->frames[0];
    const float fx = static_cast<float>(f0.src.w) * scale_ * 0.5f, fy = static_cast<float>(f0.src.h) * scale_ * 0.5f;
    // Every frame lands with its own registration point at (fx, fy).
    auto put = [&](int k, float a) {
      if (k < 0 || k >= static_cast<int>(sheet_->frames.size()) || a <= 0) return;
      const gfx::SheetFrame& f = sheet_->frames[static_cast<std::size_t>(k)];
      const gfx::RectF src{static_cast<float>(f.src.x), static_cast<float>(f.src.y), static_cast<float>(f.src.w), static_cast<float>(f.src.h)};
      const gfx::RectF dst{ctx.origin.x + fx - static_cast<float>(f.reg.x) * scale_, ctx.origin.y + fy - static_cast<float>(f.reg.y) * scale_,
                           src.w * scale_, src.h * scale_};
      r.drawTexture(*sheet_->image, src, dst, {1, 1, 1, a}, gfx::Blend::Normal);
    };
    put(0, alpha);
    if (sheet_->frames.size() > 1) {
      put(fade_.shown, alpha);
      if (fade_.shown != fade_.target) put(fade_.target, alpha * static_cast<float>(fade_.timer) / kFadeMs);
    }
  } else if (texture_) {
    const gfx::RectF dst{ctx.origin.x, ctx.origin.y, static_cast<float>(texture_->width()) * scale_, static_cast<float>(texture_->height()) * scale_};
    r.drawTexture(*texture_, {}, dst, {1, 1, 1, alpha}, gfx::Blend::Normal);
  }
}

std::string CharWindow::detail() const {
  char buf[96];
  std::snprintf(buf, sizeof buf, " character=%s frame=%d target=%d timer=%d", charName_.c_str(), fade_.shown, fade_.target, fade_.timer);
  return buf;
}

}  // namespace choco::game
