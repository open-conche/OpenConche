#include "games/choc1/SimpleAnimation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"
#include "ui/DrawContext.h"
#include "ui/Style.h"
#include "ui/WindowManager.h"

namespace choco::game::choc1 {

PolylinePath::PolylinePath(int timeMs, bool loop, std::vector<Point> points)
    : time_(timeMs > 0 ? timeMs : 1), loop_(loop), points_(std::move(points)) {
  for (std::size_t i = 1; i < points_.size(); ++i) total_ += std::hypot(points_[i].x - points_[i - 1].x, points_[i].y - points_[i - 1].y);
}

PolylinePath::Point PolylinePath::at(long long elapsedMs) const {
  if (points_.empty()) return {};
  long long t = elapsedMs < 0 ? 0 : elapsedMs;
  if (loop_) t %= time_;
  if (t >= time_) return points_.back();
  double left = static_cast<double>(t) / time_ * total_;
  for (std::size_t i = 1; i < points_.size(); ++i) {
    const Point &a = points_[i - 1], &b = points_[i];
    const double len = std::hypot(b.x - a.x, b.y - a.y);
    if (left <= len) {
      const double f = len > 0 ? left / len : 0;
      return {a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f};
    }
    left -= len;
  }
  return points_.back();
}

double PolylinePath::heading(long long elapsedMs) const {
  if (points_.size() < 2) return 0;
  long long t = elapsedMs < 0 ? 0 : elapsedMs;
  if (loop_) t %= time_;
  std::size_t seg = points_.size() - 1;  // at or past the end: the last segment
  if (t < time_) {
    double left = static_cast<double>(t) / time_ * total_;
    for (std::size_t i = 1; i < points_.size(); ++i) {
      const double len = std::hypot(points_[i].x - points_[i - 1].x, points_[i].y - points_[i - 1].y);
      if (left < len) {
        seg = i;
        break;
      }
      left -= len;
    }
  }
  const Point &a = points_[seg - 1], &b = points_[seg];
  const double len = std::hypot(b.x - a.x, b.y - a.y);
  if (len <= 0) return 0;
  const double nx = (b.x - a.x) / len;
  const double angle = std::acos(std::clamp(nx, -1.0, 1.0));
  return nx > 0 ? -angle : angle;
}

bool SimpleAnimation::mirrored(bool flip, bool hasPath, double heading) {
  if (!flip || !hasPath) return false;
  // Outside [0, pi). A right-moving segment gives -angle, and exactly
  // horizontal gives -0.0, which counts as negative here (the literal float
  // compare 0 <= -0.0 would not mirror it; see docs/re/choc1-port-view.md 2.4
  // step 5).
  return std::signbit(heading) || heading >= 3.14159274101257324;
}

gfx::RectF SimpleAnimation::stillRect(float px, float py, int w, int h, float scale) {
  return {px - static_cast<float>(w) * scale / 2, py - static_cast<float>(h) * scale / 2, static_cast<float>(w) * scale,
          static_cast<float>(h) * scale};
}

int SimpleAnimation::frameAt(double elapsedMs, int fps, int frameCount) {
  if (frameCount <= 0 || fps <= 0 || elapsedMs < 0) return 0;
  const long long period = std::max(1, 1000 / fps);  // whole ms, as the exe
  return static_cast<int>((static_cast<long long>(elapsedMs) / period) % frameCount);
}

std::vector<float> SimpleAnimation::tileOffsets(float start, float tile, float limit) {
  std::vector<float> v{start};
  if (tile > 0)
    for (float p = start + tile; p < limit && v.size() < 64; p += tile) v.push_back(p);
  return v;
}

void SimpleAnimation::init(const ui::Style& style) {
  Window::init(style);
  const std::string anim = style.stringOr("anim");
  const std::string image = style.stringOr("image");
  scale_ = style.numberOr("scale", 1);
  animX_ = static_cast<float>(static_cast<int>(style.numberOr("animx", 0)));  // stored as integers
  animY_ = static_cast<float>(static_cast<int>(style.numberOr("animy", 0)));
  xtile_ = static_cast<float>(style.numberOr("xtile", 8000));
  ytile_ = static_cast<float>(style.numberOr("ytile", 6000));
  fps_ = static_cast<int>(style.numberOr("fps", 10));
  flip_ = style.boolean("flip").value_or(true);  // the exe reads it with default true; only the clouds set it false

  if (auto p = style.table("path")) {
    std::vector<PolylinePath::Point> pts;
    for (int i = 1; i <= p->size(); ++i)
      if (auto e = p->element(i)) pts.push_back({e->numberAt(1).value_or(0), e->numberAt(2).value_or(0)});
    path_ = PolylinePath(static_cast<int>(p->number("time").value_or(1000)), p->boolean("loop").value_or(false), std::move(pts));
    hasPath_ = true;
  }
  if (style.boolean("random").value_or(false))
    randomMs_ = randomBackdate(path_.timeMs(), hasPath_, static_cast<unsigned>(ctx_.rng.fxRange(0, 0x7fffffff)));

  int w = 0, h = 0;
  if (!anim.empty() && manager().sheets()) {
    sheet_ = manager().sheets()->get(anim);
    source_ = anim;
    if (sheet_ && !sheet_->frames.empty()) {
      w = sheet_->frames[0].src.w;
      h = sheet_->frames[0].src.h;
    }
  }
  if (!sheet_ && !image.empty() && manager().textures()) {
    still_ = manager().textures()->load(image);
    source_ = image;
    if (still_) {
      w = still_->width();
      h = still_->height();
    }
  }
  // The window is the size of the first frame or the picture (docs: "size
  // defaults to the image size"), unless the style sets one.
  if (rect().w <= 0 || rect().h <= 0) setSize(w, h);
}

void SimpleAnimation::drawOutside(ui::DrawContext& ctx) const {
  if (ctx.alpha <= 0) return;
  const float sc = static_cast<float>(scale_);
  const double now = manager().modalNow(*this);
  if (!started_) {
    started_ = true;
    startMs_ = now - static_cast<double>(randomMs_);  // may be negative: the clock origin is not
                                                      // the window's start
  }
  const double elapsed = now - startMs_;
  float px = animX_, py = animY_;
  bool mirror = false;
  if (hasPath_) {
    mirror = mirrored(flip_, true, path_.heading(static_cast<long long>(elapsed)));
    const PolylinePath::Point o = path_.at(static_cast<long long>(elapsed));
    px += static_cast<float>(o.x);
    py += static_cast<float>(o.y);
  }
  lastMirror_ = mirror;
  lastX_ = px;
  lastY_ = py;
  if (sheet_ && sheet_->image && !sheet_->frames.empty()) {
    const int k = frameAt(elapsed, fps_, static_cast<int>(sheet_->frames.size()));
    const gfx::SheetFrame& f = sheet_->frames[static_cast<std::size_t>(k)];
    const gfx::RectF src{static_cast<float>(f.src.x), static_cast<float>(f.src.y), static_cast<float>(f.src.w),
                         static_cast<float>(f.src.h)};
    // The frame's registration point lands on the (offset) position; the scale
    // is taken around that point. Tiles run right then down, the limits from
    // the frame size in whole pixels (800x600 is the exe's hard-coded screen).
    const std::vector<float> xs = tileOffsets(px, xtile_, static_cast<float>(f.src.w / 2) + 800);
    const std::vector<float> ys = tileOffsets(py, ytile_, static_cast<float>(f.src.h / 2) + 600);
    for (const float ty : ys)
      for (const float tx : xs) {
        const gfx::RectF dst{ctx.origin.x + tx - static_cast<float>(mirror ? f.src.w - f.reg.x : f.reg.x) * sc,
                             ctx.origin.y + ty - static_cast<float>(f.reg.y) * sc, src.w * sc, src.h * sc};
        ctx.renderer.drawTexture(*sheet_->image, src, dst, {1, 1, 1, ctx.alpha}, gfx::Blend::Normal, 0, {}, mirror);
      }
  } else if (still_) {
    // A still is drawn centred on the point (the same sprite draw call, vtable
    // slot 0x18, takes the centre; the tiling bound frameW/2 + 800 also fits a
    // centre), the scale about the centre, mirrored about it.
    const std::vector<float> xs = tileOffsets(px, xtile_, static_cast<float>(still_->width() / 2) + 800);
    const std::vector<float> ys = tileOffsets(py, ytile_, static_cast<float>(still_->height() / 2) + 600);
    for (const float ty : ys)
      for (const float tx : xs) {
        gfx::RectF dst = stillRect(tx, ty, still_->width(), still_->height(), sc);
        dst.x += ctx.origin.x;
        dst.y += ctx.origin.y;
        if (&tx == &xs.front() && &ty == &ys.front()) lastTop_ = dst.y - ctx.origin.y;
        ctx.renderer.drawTexture(*still_, {}, dst, {1, 1, 1, ctx.alpha}, gfx::Blend::Normal, 0, {}, mirror);
      }
  }
}

std::string SimpleAnimation::detail() const {
  char buf[224], top[32] = "";
  if (still_ && !sheet_) std::snprintf(top, sizeof top, " top=%.0f", lastTop_);
  std::snprintf(buf, sizeof buf, " source=\"%s\" frames=%zu fps=%d scale=%.2f pos=%.0f,%.0f mirror=%d%s", source_.c_str(),
                sheet_   ? sheet_->frames.size()
                : still_ ? 1u
                         : 0u,
                fps_, scale_, lastX_, lastY_, lastMirror_ ? 1 : 0, top);
  return buf;
}

}  // namespace choco::game::choc1
