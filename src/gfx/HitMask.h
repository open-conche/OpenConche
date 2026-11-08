#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace choco::gfx {

// CPU copy of an image's colour for pixel-accurate button hits. A pixel hits when any of R, G, B exceeds
// kThreshold (docs/re/window-system.md); alpha is ignored.
class HitMask {
 public:
  static constexpr int kThreshold = 19;

  HitMask() = default;
  HitMask(int w, int h, std::vector<std::uint8_t> rgb) : w_(w), h_(h), rgb_(std::move(rgb)) {}

  int width() const { return w_; }
  int height() const { return h_; }
  bool empty() const { return w_ <= 0 || h_ <= 0; }

  // Mask pixel coordinates; false outside the image.
  bool hit(int x, int y) const;
  // (px, py) is a point inside a rectangle of rectW x rectH on screen; it maps proportionally onto the mask.
  bool hitScaled(float px, float py, float rectW, float rectH) const;

 private:
  int w_ = 0, h_ = 0;
  std::vector<std::uint8_t> rgb_;  // w * h * 3
};

// CPU copy of an image's alpha channel, for the Chocolatier 2 sprite hit test (a pixel hits when alpha >= a threshold).
class AlphaMask {
 public:
  AlphaMask() = default;
  AlphaMask(int w, int h, std::vector<std::uint8_t> alpha) : w_(w), h_(h), alpha_(std::move(alpha)) {}
  int width() const { return w_; }
  int height() const { return h_; }
  // Alpha 0..255 at pixel (x, y); 0 outside the image.
  int alphaAt(int x, int y) const {
    return x < 0 || y < 0 || x >= w_ || y >= h_ ? 0 : alpha_[static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) + static_cast<std::size_t>(x)];
  }

 private:
  int w_ = 0, h_ = 0;
  std::vector<std::uint8_t> alpha_;
};

}  // namespace choco::gfx
