#include "ui/Layout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "ui/Window.h"

namespace choco::ui {

namespace {

constexpr int kCenterI = 80000;
constexpr int kMaxI = 160000;
constexpr int kBandLow = 40000;    // positions above this (and up to kBandHigh) are in the centre band
constexpr int kBandHigh = 106666;  // above this: anchored to the far edge

int toInt(double v) {
  if (std::isnan(v)) return 0;
  return static_cast<int>(std::clamp(v, -2.0e9, 2.0e9));
}

// The fill tail: P is already a signed offset.
void fillTail(int& p, int& s, int d) {
  if (p < 0) {
    s = d + s + 2 * p - kMaxI;
    p = 0;
  } else {
    s = d + s - 2 * p - kMaxI;
    p = 2 * p;
  }
}

// A literal size: positions in the three bands.
void literalSize(int& p, int s, int d, bool noWrap) {
  if (p > kBandHigh) p = d + p - kMaxI;
  else if (p > kBandLow) p = p - kCenterI + (d / 2 - s / 2);
  else if (!noWrap && p < 0) p = d + p;
}

}  // namespace

AxisOut resolveAxis(const AxisIn& in) {
  int p = in.pos, s = in.size;
  const int d = in.parentDim;

  // Step 1: alignment, only for positions outside the centre band.
  if (p < kBandLow || p > kBandHigh) {
    if (in.alignCenter) {
      if (s == 0) return {p, s};
      p -= s / 2;
    } else if (in.alignFar) {
      if (s == 0) return {p, s};
      p -= s;
    }
  }
  if (!in.hasParent) return {p, s};

  if (s >= 0 && s <= kCenterI) {
    literalSize(p, s, d, in.noWrap);
  } else if (s > kCenterI) {
    if (p <= kBandLow) {
      if (!in.noWrap && p < 0) p = d + p;
      s = d + s - p - kMaxI;
    } else {
      p = p > kBandHigh ? d + p - kMaxI : p - kCenterI;
      fillTail(p, s, d);
    }
  } else if (s >= -kCenterI) {
    // A negative size: the position names the far edge.
    const int n = -s;
    literalSize(p, n, d, in.noWrap);
    s = n;
    p -= n;
  } else {
    // Degenerate (below -80000); only has to be harmless.
    const int n = -s;
    if (p <= kBandLow) {
      if (!in.noWrap && p < 0) p = d + p;
      s = p;
      p = kMaxI - p;
    } else {
      p = p > kBandHigh ? d + p - kMaxI : p - kCenterI;
      s = n;
      fillTail(p, s, d);
    }
  }
  return {p, s};
}

void sizeAndPositionFromStyle(Window& win, const Style& style) {
  const gfx::RectI cur = win.rect();
  const double x = style.numberOr("x", 0), y = style.numberOr("y", 0);
  const double w = style.numberOr("w", cur.w), h = style.numberOr("h", cur.h);
  const int alignRaw = toInt(style.numberOr("align", -1));
  const bool noWrap = alignRaw != -1;  // align given at all (even 0) keeps negatives negative
  const int align = alignRaw == -1 ? 0 : alignRaw;

  const Window* parent = win.parent();
  if (parent) {
    if (parent->rect().w == 0 && (w < 0 || x < 0 || w > kCenterI || x > kBandLow)) return;
    if (parent->rect().h == 0 && (h < 0 || y < 0 || h > kCenterI || y > kBandLow)) return;
  }
  // The original compares these unsigned, so a negative x or y with a zero size also returns early.
  if (static_cast<std::uint32_t>(toInt(x)) > static_cast<std::uint32_t>(kBandLow) && w == 0) return;
  if (static_cast<std::uint32_t>(toInt(y)) > static_cast<std::uint32_t>(kBandLow) && h == 0) return;

  AxisIn ax;
  ax.hasParent = parent != nullptr;
  ax.noWrap = noWrap;
  ax.pos = toInt(x);
  ax.size = toInt(w);
  ax.parentDim = parent ? parent->rect().w : 0;
  ax.alignCenter = (align & 1) != 0;
  ax.alignFar = (align & 2) != 0;
  const AxisOut hx = resolveAxis(ax);

  ax.pos = toInt(y);
  ax.size = toInt(h);
  ax.parentDim = parent ? parent->rect().h : 0;
  ax.alignCenter = (align & 4) != 0;
  ax.alignFar = (align & 8) != 0;
  const AxisOut vy = resolveAxis(ax);

  gfx::RectI r = cur;
  if (r.x == 0) r.x = hx.pos;
  if (r.y == 0) r.y = vy.pos;
  if (r.w == 0) r.w = hx.size;
  if (r.h == 0) r.h = vy.size;
  win.setRect(r);
}

void setPositionFromScript(Window& win, double x, double y) {
  const Window* parent = win.parent();
  AxisIn ax;
  ax.hasParent = parent != nullptr;
  ax.pos = toInt(x);
  ax.size = win.rect().w;
  ax.parentDim = parent ? parent->rect().w : 0;
  const AxisOut hx = resolveAxis(ax);
  ax.pos = toInt(y);
  ax.size = win.rect().h;
  ax.parentDim = parent ? parent->rect().h : 0;
  const AxisOut vy = resolveAxis(ax);
  win.setPos(hx.pos, vy.pos);
}

void fitToChildren(Window& win) {
  int w = win.rect().w, h = win.rect().h;
  for (const auto& c : win.children()) {
    w = std::max(w, c->rect().x + c->rect().w);
    h = std::max(h, c->rect().y + c->rect().h);
  }
  win.setSize(w, h);
}

}  // namespace choco::ui
