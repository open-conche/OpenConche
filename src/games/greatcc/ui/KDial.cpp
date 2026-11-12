#include "games/greatcc/ui/KDial.h"

#include <cmath>

#include "games/greatcc/scene/Mat3.h"
#include "gfx/TextureCache.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {
constexpr float kTwoPi = 6.2831855f;
constexpr float kOvershoot = 0.09817477f;  // pi / 32: the wedge's closing edge passes the true angle by this much
}  // namespace

std::shared_ptr<gfx::Texture> KDial::loadTexture(const std::string& path) {
  if (path.empty()) return nullptr;
  gfx::TextureCache* cache = manager().textures();
  return cache ? cache->load(path) : nullptr;
}

void KDial::init(const ui::Style& style) {
  Window::init(style);
  cx_ = static_cast<int>(style.numberOr("center_x", -1337));
  cy_ = static_cast<int>(style.numberOr("center_y", -1337));
  regx_ = static_cast<int>(style.numberOr("needle_regx", -1337));
  regy_ = static_cast<int>(style.numberOr("needle_regy", -1337));
  minrot_ = static_cast<float>(style.numberOr("minrot", 0));
  maxrot_ = static_cast<float>(style.numberOr("maxrot", kTwoPi));
  needle_ = loadTexture(style.stringOr("needle"));
  overlay_ = loadTexture(style.stringOr("overlay"));
  assert_ = style.booleanOr("assert", true);
  if (needle_) {
    regx_ -= needle_->width() / 2;
    regy_ -= needle_->height() / 2;
  }
  setGauge(static_cast<float>(style.numberOr("startpos", 0)));
}

void KDial::setGauge(float p) {
  // KDial::SetPos: below 0 is 0, above 1 is 1, and a NaN (a 0/0 from the HUD) is 1.
  p = p < 0 ? 0 : (p <= 1 ? p : 1);
  angle_ = minrot_ + (maxrot_ - minrot_) * p;
}

void KDial::drawNeedleAndOverlay(ui::DrawContext& ctx) const {
  if (needle_) {
    float a = angle_;
    while (a < 0) a += kTwoPi;
    while (a > kTwoPi) a -= kTwoPi;
    // translate(-reg), rotate, translate(pivot): the registration point lands on the pivot and the needle turns about it.
    const Mat3 m = Mat3::translation(-static_cast<float>(regx_), -static_cast<float>(regy_)) * Mat3::rotationAbout(0, 0, a) *
                   Mat3::translation(static_cast<float>(cx_), static_cast<float>(cy_));
    const float hw = static_cast<float>(needle_->width()) / 2, hh = static_cast<float>(needle_->height()) / 2;
    const gfx::PointF corner[4] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
    const gfx::PointF uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    gfx::Vertex v[4];
    for (int i = 0; i < 4; ++i) {
      const gfx::PointF p = m.apply(corner[i]);
      v[i] = {{ctx.origin.x + p.x, ctx.origin.y + p.y}, {1, 1, 1, alpha_}, uv[i]};
    }
    static const int kIdx[6] = {0, 1, 2, 0, 2, 3};
    ctx.renderer.drawGeometry(needle_.get(), v, kIdx, gfx::Blend::Normal);
  }
  if (overlay_)
    ctx.renderer.drawTexture(*overlay_, {}, {ctx.origin.x, ctx.origin.y, static_cast<float>(overlay_->width()), static_cast<float>(overlay_->height())},
                             {1, 1, 1, 1}, gfx::Blend::Normal);
}

void KDial::postDraw(ui::DrawContext& ctx) const { drawNeedleAndOverlay(ctx); }

std::string KDial::detail() const {
  char buf[96];
  std::snprintf(buf, sizeof buf, " gauge=%.3f angle=%.3f%s", gauge(), angle_, needle_ ? "" : " (no needle)");
  return buf;
}

// ---- KSweepDial ----

void KSweepDial::init(const ui::Style& style) {
  KDial::init(style);
  sweep_ = loadTexture(style.stringOr("sweep"));
  numSegments_ = static_cast<int>(style.numberOr("numsegments", 32));
  if (numSegments_ < 2) numSegments_ = 2;
}

void KSweepDial::postChildrenInit(const ui::Style& style) {
  KDial::postChildrenInit(style);
  start_ = end_ = angle_;
  w_ = static_cast<float>(rect().w);
  h_ = static_cast<float>(rect().h);
  aspect_ = w_ > 0 ? h_ / w_ : 0;
  ready_ = true;
  setGauge(gauge());
}

void KSweepDial::setGauge(float p) {
  KDial::setGauge(p);
  end_ = angle_;
  if (ready_) updateSegments();
}

gfx::PointF KSweepDial::thetaToWindow(float theta) const {
  const float cx = static_cast<float>(cx_), cy = static_cast<float>(cy_);
  const float s = std::sin(theta), c = std::cos(theta);
  if (s == 0) return {cx, 0};
  const float k = -c / s;
  if (s > 0) {
    if (k < -aspect_) return {cx - cy / k, 0};
    if (k < aspect_) return {w_, cy + k * cx};
    return {cx + cy / k, h_};
  }
  if (k > aspect_) return {cx - cy / k, 0};
  if (k > -aspect_) return {0, cy - k * cx};
  return {cx + cy / k, h_};
}

void KSweepDial::updateSegments() {
  float lo = start_ < end_ ? start_ : end_;
  const float hi = (start_ < end_ ? end_ : start_) + kOvershoot;
  const float step = kTwoPi / static_cast<float>(numSegments_);
  wedge_.clear();
  int count = 0;
  while ((lo < hi || count == 0) && count < numSegments_ - 1) {
    wedge_.push_back(thetaToWindow(lo));
    ++count;
    lo += step;
  }
  wedge_.push_back(thetaToWindow(hi));
  count_ = count;
}

void KSweepDial::postDraw(ui::DrawContext& ctx) const {
  if (sweep_ && gauge() != 0 && count_ > 0 && w_ > 0 && h_ > 0) {
    std::vector<gfx::Vertex> v;
    v.reserve(wedge_.size() + 1);
    for (const gfx::PointF& p : wedge_) v.push_back({{ctx.origin.x + p.x, ctx.origin.y + p.y}, {1, 1, 1, alpha_}, {p.x / w_, p.y / h_}});
    const gfx::PointF centre{static_cast<float>(cx_), static_cast<float>(cy_)};
    v.push_back({{ctx.origin.x + centre.x, ctx.origin.y + centre.y}, {1, 1, 1, alpha_}, {centre.x / w_, centre.y / h_}});
    const int hub = static_cast<int>(v.size()) - 1;
    std::vector<int> idx;
    for (int i = 0; i < count_; ++i) {
      idx.push_back(hub);
      idx.push_back(i);
      idx.push_back(i + 1);
    }
    ctx.renderer.drawGeometry(sweep_.get(), v, idx, gfx::Blend::Normal);
  }
  drawNeedleAndOverlay(ctx);
}

std::string KSweepDial::detail() const {
  return KDial::detail() + " wedge=" + std::to_string(count_) + (sweep_ ? "" : " (no sweep)");
}

}  // namespace choco::games::greatcc
